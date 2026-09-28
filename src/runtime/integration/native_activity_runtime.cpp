#include "ogplay/runtime/integration/native_activity_runtime.h"

#include <array>
#include <algorithm>
#include "ogplay/loader/apk.h"
#include <bit>
#include <map>
#include <utility>
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/native_library_loader.h"

namespace ogplay::runtime {
namespace dx = dexvm;

class NativeActivityRuntime::Impl {
public:
    struct Instance {
        memory::GuestAddress address;
        memory::GuestAddress window;
        std::uint32_t bytes{};
        JniReference activity;
        JniReference assets;
        dx::VmObjectRef owner;
        std::uint64_t thread{};
        std::size_t module_index{};
        bool registered{};
        bool window_active{}, input_active{};
    };
    AndroidGuestCallSession& session;
    std::weak_ptr<DexVmAndroidContext> context;
    Publish publish;
    Resolve resolve;
    Thread thread;
    dx::NioDirectMemoryAccess memory;
    std::map<std::uint32_t, Instance> instances;
    std::uint32_t next_handle{1};

    Impl(AndroidGuestCallSession& s, std::weak_ptr<DexVmAndroidContext> c,
         Publish p, Resolve r, Thread t)
        : session(s), context(std::move(c)), publish(std::move(p)), resolve(std::move(r)),
          thread(std::move(t)), memory(s.Process().GuestMemoryAccess()) {}

    void Write(memory::GuestAddress address, std::uint32_t value) {
        std::array<std::byte, 4> bytes{};
        for (std::size_t i=0; i<4; ++i) bytes[i] = static_cast<std::byte>((value >> (i*8)) & 255U);
        memory.write(address, bytes);
    }
    std::uint32_t Read(memory::GuestAddress address) {
        std::array<std::byte, 4> bytes{};
        memory.read(address, bytes);
        std::uint32_t value{};
        for (std::size_t i=0; i<4; ++i) value |= std::to_integer<std::uint32_t>(bytes[i]) << (i*8);
        return value;
    }
    A32GuestCallResult Invoke(dx::IntrinsicContext& call, const Instance& item,
                              memory::GuestAddress target, std::span<const std::uint32_t> args) {
        if (thread(call.vm.CurrentContextToken()) != item.thread)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity callback requires its creating thread"};
        auto& environment = session.Environment();
        environment.PushLocalFrame(item.thread, 16);
        struct LocalFrameScope final {
            JniEnvironment& environment;
            std::uint64_t thread;
            ~LocalFrameScope() {
                try { static_cast<void>(environment.PopLocalFrame(thread)); }
                catch (const std::exception&) {} // the original guest failure wins
            }
        } local_frame{environment, item.thread};
        A32GuestCallFrame frame;
        frame.target = target;
        frame.thread_id = item.thread;
        frame.context_token = call.vm.CurrentContextToken();
        frame.renewable_native_frame = true;
        for (std::size_t i=0; i<args.size() && i<4; ++i) frame.registers[i] = args[i];
        auto& lock = call.vm.ExecutionLock();
        const auto depth = lock.ReleaseForBlocking();
        A32GuestCallResult result;
        try { result = session.Invoke(frame); }
        catch (...) { lock.ReacquireAfterBlocking(depth); throw; }
        lock.ReacquireAfterBlocking(depth);
        auto& env = session.Environment();
        if (env.ExceptionCheck(item.thread)) {
            const auto reference = env.ExceptionOccurred(item.thread);
            const auto exception = resolve(reference, item.thread);
            env.ExceptionClear(item.thread);
            throw dx::VmJavaThrow{call.vm.Linker().Class(call.vm.Model().ObjectClass(exception)).descriptor,
                                  {}, exception};
        }
        return result;
    }
    A32GuestCallResult Callback(dx::IntrinsicContext& call, Instance& item, std::size_t index,
                                std::initializer_list<std::uint32_t> extra = {}) {
        // The guest owns callbacks and may replace slots after onCreate.
        const auto callbacks = memory::GuestAddress{Read(item.address)};
        if (callbacks.IsNull()) return {};
        const auto target = memory::GuestAddress{Read(callbacks.Add(index*4))};
        if (target.IsNull()) return {};
        std::vector<std::uint32_t> args{item.address.Value()};
        args.insert(args.end(), extra.begin(), extra.end());
        return Invoke(call, item, target, args);
    }
    void Free(Instance& item) {
        if (item.registered) session.Process().UnregisterNativeActivity(item.address);
        auto& env = session.Environment();
        if (!item.activity.IsNull()) env.DeleteGlobalRef(item.thread, item.activity);
        if (!item.assets.IsNull()) env.DeleteGlobalRef(item.thread, item.assets);
        if (!item.address.IsNull()) memory.release(item.address, item.bytes);
        item = Instance();
    }
    dx::VmValue Load(dx::IntrinsicContext& call) {
        const auto ctx = context.lock();
        if (!ctx || !ctx->native_libraries)
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "NativeActivity has no current-process loader"};
        if (next_handle > 0x000fffffU)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity identity budget exhausted"};
        dx::IntrinsicCall typed(call);
        const auto path = call.vm.StringUtf8(typed.NonNullRef(0, "path"));
        const auto function = call.vm.StringUtf8(typed.NonNullRef(1, "function"));
        NativeLibraryLoadResult loaded;
        memory::GuestAddress entry;
        try {
            loaded = ctx->native_libraries->LoadPath(path, ctx->application_class_loader_token, NativeLibraryEntry::native_activity);
            entry = session.Process().FindModuleExport(loaded.module_index, function);
            if (entry.IsNull()) throw std::runtime_error("NativeActivity entry is missing: " + function);
        } catch (const std::exception& error) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.native_activity.load", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;", error.what()};
        }
        Instance item;
        item.module_index = loaded.module_index;
        item.thread = thread(call.vm.CurrentContextToken());
        item.owner = call.receiver;
        std::vector<std::byte> bytes(256);
        const auto append_string = [&](std::size_t argument) {
            const auto ref = call.arguments[argument].ref;
            if (!ref.IsValid()) return std::uint32_t{};
            const auto value = call.vm.StringUtf8(ref);
            const auto offset = static_cast<std::uint32_t>(bytes.size());
            for (auto c : value) bytes.push_back(static_cast<std::byte>(c));
            bytes.push_back(std::byte{});
            return offset;
        };
        const auto internal = append_string(3), obb = append_string(4), external = append_string(5);
        const auto saved_offset = static_cast<std::uint32_t>(bytes.size());
        std::uint32_t saved_size{};
        if (const auto saved = call.arguments[8].ref; saved.IsValid()) {
            const auto state = call.vm.Model().ReadByteRegion(saved, 0, call.vm.Model().ArrayLength(saved));
            saved_size = static_cast<std::uint32_t>(state.size());
            bytes.insert(bytes.end(), state.begin(), state.end());
        }
        if (bytes.size() > 1024U*1024U)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "NativeActivity state/path budget exceeded"};
        item.bytes = static_cast<std::uint32_t>(bytes.size());
        item.address = memory.allocate(item.bytes);
        try {
            auto& env = session.Environment();
            const auto activity = publish(call.receiver, item.thread);
            item.activity = env.NewGlobalRef(item.thread, activity);
            env.DeleteLocalRef(item.thread, activity);
            const auto assets = publish(typed.NonNullRef(7, "assets"), item.thread);
            item.assets = env.NewGlobalRef(item.thread, assets);
            env.DeleteLocalRef(item.thread, assets);
            memory.write(item.address, bytes);
            // API19 ARMv7 ANativeActivity: callbacks, vm, env, clazz,
            // internalDataPath, externalDataPath, sdkVersion, instance,
            // assetManager, obbPath. Callback table is 16 function pointers.
            Write(item.address, item.address.Add(40).Value());
            Write(item.address.Add(4), session.GuestJavaVm().Value());
            Write(item.address.Add(8), session.GuestEnvironment().Value());
            Write(item.address.Add(12), item.activity.Value());
            Write(item.address.Add(16), internal ? item.address.Add(internal).Value() : 0);
            Write(item.address.Add(20), external ? item.address.Add(external).Value() : 0);
            Write(item.address.Add(24), call.arguments[6].cat1);
            Write(item.address.Add(32), item.address.Add(128).Value());
            Write(item.address.Add(128), item.assets.Value());
            Write(item.address.Add(36), obb ? item.address.Add(obb).Value() : 0);
            session.Process().RegisterNativeActivity({item.address, item.address.Add(128),
                item.address.Add(148), ctx->surface_width, ctx->surface_height,
                [weak = context](std::string_view name) -> std::optional<std::vector<std::byte>> {
                    const auto context = weak.lock();
                    if (!context) throw std::runtime_error("NativeActivity assets retired");
                    const auto path = "assets/" + std::string(name);
                    if (std::ranges::none_of(context->archive.entries, [&](const auto& entry) { return entry.name == path; }))
                        return std::nullopt;
                    return loader::ReadApkEntry(context->apk_bytes, context->archive, path);
                }});
            item.registered = true;
            const std::array args{item.address.Value(), saved_size ? item.address.Add(saved_offset).Value() : 0U, saved_size};
            static_cast<void>(Invoke(call, item, entry, args));
            const auto handle = next_handle++;
            instances.emplace(handle, item);
            return dx::VmValue::Int(static_cast<std::int32_t>(handle));
        } catch (...) { Free(item); throw; }
    }
    dx::VmValue Call(dx::IntrinsicContext& call, std::string_view method) {
        if (method == "loadNativeCode") return Load(call);
        const auto handle = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        auto found = instances.find(handle);
        if (found == instances.end() || found->second.owner != call.receiver)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity handle is stale or belongs to another Activity"};
        auto& item = found->second;
        if (method == "unloadNativeCode") {
            try { static_cast<void>(Callback(call, item, 5)); }
            catch (...) { Free(item); instances.erase(found); throw; }
            Free(item); instances.erase(found);
        } else if (method == "onStartNative") static_cast<void>(Callback(call, item, 0));
        else if (method == "onResumeNative") static_cast<void>(Callback(call, item, 1));
        else if (method == "onPauseNative") static_cast<void>(Callback(call, item, 3));
        else if (method == "onStopNative") static_cast<void>(Callback(call, item, 4));
        else if (method == "onConfigurationChangedNative") static_cast<void>(Callback(call, item, 14));
        else if (method == "onLowMemoryNative") static_cast<void>(Callback(call, item, 15));
        else if (method == "onWindowFocusChangedNative") static_cast<void>(Callback(call, item, 6, {call.arguments[1].cat1}));
        else if (method == "onSurfaceCreatedNative") {
            if (item.window_active) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity surface already created"};
            item.window = session.Process().SetNativeActivityWindow(item.address, true);
            item.window_active = true;
            static_cast<void>(Callback(call, item, 7, {item.window.Value()}));
        }
        else if (method == "onSurfaceChangedNative") static_cast<void>(Callback(call, item, 8, {item.window.Value()}));
        else if (method == "onSurfaceRedrawNeededNative") static_cast<void>(Callback(call, item, 9, {item.window.Value()}));
        else if (method == "onSurfaceDestroyedNative") {
            if (item.window_active) {
                const auto retire = [&] {
                    session.Process().SetNativeActivityWindow(item.address, false);
                    item.window_active = false;
                };
                try { static_cast<void>(Callback(call, item, 10, {item.window.Value()})); }
                catch (...) { retire(); throw; }
                retire();
            }
        }
        else if (method == "onInputQueueCreatedNative" || method == "onInputQueueDestroyedNative") {
            if (call.arguments[1].cat1 != item.address.Add(148).Value())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity received another input queue"};
            if (method == "onInputQueueCreatedNative") {
                session.Process().SetNativeActivityInput(item.address, true);
                item.input_active = true;
                static_cast<void>(Callback(call, item, 11, {call.arguments[1].cat1}));
            } else if (item.input_active) {
                const auto retire = [&] {
                    session.Process().SetNativeActivityInput(item.address, false);
                    item.input_active = false;
                };
                try { static_cast<void>(Callback(call, item, 12, {call.arguments[1].cat1})); }
                catch (...) { retire(); throw; }
                retire();
            }
        }
        else if (method == "onContentRectChangedNative") {
            const auto rect = item.address.Add(160);
            Write(rect, call.arguments[1].cat1); Write(rect.Add(4), call.arguments[2].cat1);
            Write(rect.Add(8), call.arguments[1].cat1 + call.arguments[3].cat1);
            Write(rect.Add(12), call.arguments[2].cat1 + call.arguments[4].cat1);
            static_cast<void>(Callback(call, item, 13, {rect.Value()}));
        } else if (method == "onSaveInstanceStateNative") {
            const auto size = item.address.Add(176);
            Write(size, 0);
            const auto state = memory::GuestAddress{Callback(call, item, 2, {size.Value()}).return_value};
            if (state.IsNull()) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto free = session.Process().FindModuleExport(item.module_index, "free");
            const auto release_state = [&] {
                static_cast<void>(Invoke(call, item, free, std::array{state.Value()}));
            };
            std::vector<std::byte> bytes;
            try {
                const auto length = Read(size);
                if (length > 1024U*1024U)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NativeActivity saved state exceeds budget"};
                bytes.resize(length);
                memory.read(state, bytes);
            } catch (...) { release_state(); throw; }
            release_state();
            const auto array = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(bytes.size()));
            call.vm.Model().WriteByteRegion(array, 0, bytes);
            return dx::VmValue::Ref(array);
        } else throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unknown NativeActivity native method"};
        return dx::VmValue::Void();
    }
};

NativeActivityRuntime::NativeActivityRuntime(AndroidGuestCallSession& s,
        std::weak_ptr<DexVmAndroidContext> c, Publish p, Resolve r, Thread t)
    : impl_(std::make_unique<Impl>(s, std::move(c), std::move(p), std::move(r), std::move(t))) {}
NativeActivityRuntime::~NativeActivityRuntime() { Release(); }
dexvm::VmValue NativeActivityRuntime::Call(dexvm::IntrinsicContext& call, std::string_view method) { return impl_->Call(call, method); }
memory::GuestAddress NativeActivityRuntime::InputQueuePointer(dexvm::VmObjectRef owner) const {
    for (const auto& [_, item] : impl_->instances)
        if (item.owner == owner) return item.address.Add(148);
    return memory::GuestAddress{};
}
void NativeActivityRuntime::Release() {
    for (auto& [_, item] : impl_->instances) impl_->Free(item);
    impl_->instances.clear();
}
} // namespace ogplay::runtime
