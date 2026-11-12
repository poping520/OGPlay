#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from shared_handlers.cpp ----
// Factories for handlers shared by several per-class declaration units
// (and the platform core bindings in dexvm_bridge.cpp).

#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>
#include <thread>
#include <tuple>
#include <utility>

#include "shared.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/integration/native_library_loader.h"

namespace ogplay::runtime::android_intrinsics {

void InitializeAndroidSystemProperties(dx::Interpreter& vm, const Context& context) {
    static_cast<void>(vm.SetSystemProperty("user.language", context->locale_language));
    static_cast<void>(vm.SetSystemProperty("user.region", context->locale_region));
    if (!context->session) return;
    auto& process = context->session->Process();
    const auto memory = process.GuestMemoryAccess();
    const auto invoke = [&](memory::GuestAddress target, std::array<std::uint32_t, 4> args) {
        A32GuestCallFrame frame;
        frame.target = target;
        frame.registers = args;
        frame.context_token = vm.CurrentContextToken();
        frame.thread_id =
            context->native_thread_for_context ? context->native_thread_for_context(vm.CurrentContextToken()) : 1;
        auto& execution_lock = vm.ExecutionLock();
        const auto depth = execution_lock.ReleaseForBlocking();
        std::uint32_t value{};
        try {
            value = process.Invoke(frame).return_value;
        } catch (...) {
            execution_lock.ReacquireAfterBlocking(depth);
            throw;
        }
        execution_lock.ReacquireAfterBlocking(depth);
        return value;
    };
    const auto word = [&](memory::GuestAddress address) {
        std::array<std::byte, 4> bytes{};
        memory.read(address, bytes);
        std::uint32_t value{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
            value |= std::to_integer<std::uint32_t>(bytes[i]) << (8U * i);
        return value;
    };
    const auto string = [&](memory::GuestAddress address) {
        if (address.Value() == 0) throw std::runtime_error("null platform property source");
        std::string result;
        for (std::uint32_t i = 0; i < 4096; ++i) {
            std::array<std::byte, 1> byte{};
            memory.read(address.Add(i), byte);
            const auto c = static_cast<char>(std::to_integer<unsigned char>(byte[0]));
            if (c == '\0') return result;
            result.push_back(c);
        }
        throw std::runtime_error("unterminated platform property source");
    };
    const auto getuid = process.FindLibraryExport("libc.so", "getuid");
    const auto getpwuid = process.FindLibraryExport("libc.so", "getpwuid");
    if (getuid && getpwuid) {
        const auto uid = invoke(*getuid, {});
        const memory::GuestAddress passwd{invoke(*getpwuid, {uid, 0, 0, 0})};
        if (passwd.Value() == 0) throw dx::VmJavaThrow{"Ljava/lang/AssertionError;", "guest getpwuid failed"};
        // Pinned Bionic ARM struct passwd: name, passwd, uid, gid, dir, shell.
        static_cast<void>(vm.SetSystemProperty("user.name", string(memory::GuestAddress{word(passwd)})));
        static_cast<void>(vm.SetSystemProperty("user.home", string(memory::GuestAddress{word(passwd.Add(16))})));
    }
    const auto storage = memory.allocate(8);

    struct Release final {
        dx::NioDirectMemoryAccess memory;
        memory::GuestAddress address;

        ~Release() { memory.release(address, 8); }
    } release{memory, storage};

    const auto version = [&](memory::GuestAddress function, const char* key, bool status) {
        const std::array<std::byte, 8> zero{};
        memory.write(storage, zero);
        static_cast<void>(invoke(function, {storage.Value(), status ? storage.Add(4).Value() : 0, 0, 0}));
        if (status && static_cast<std::int32_t>(word(storage.Add(4))) > 0)
            throw std::runtime_error("ICU version query failed");
        std::array<std::byte, 4> bytes{};
        memory.read(storage, bytes);
        std::size_t end = 4;
        while (end > 2 && bytes[end - 1] == std::byte{})
            --end;
        std::string value;
        for (std::size_t i = 0; i < end; ++i) {
            if (i) value += '.';
            value += std::to_string(std::to_integer<unsigned>(bytes[i]));
        }
        static_cast<void>(vm.SetSystemProperty(key, value));
    };
    // Missing exports retain manifest-derived defaults. Resolve the full loaded
    // namespace by library identity, including modules loaded after process creation.
    for (const auto& source :
         std::array{std::tuple{"libicuuc.so", "u_getVersion_51", "android.icu.library.version", false},
                    std::tuple{"libicuuc.so", "u_getUnicodeVersion_51", "android.icu.unicode.version", false},
                    std::tuple{"libicui18n.so", "ulocdata_getCLDRVersion_51", "android.icu.cldr.version", true}}) {
        const auto& [library, symbol, key, status] = source;
        if (const auto address = process.FindLibraryExport(library, symbol)) version(*address, key, status);
    }
    // Synthetic native shells may have no Bionic environment source either.
    if (process.FindLibraryExport("libc.so", "environ")) {
        for (const auto& [environment, key] :
             std::array{std::pair{"JAVA_HOME", "java.home"}, std::pair{"LD_LIBRARY_PATH", "java.library.path"}}) {
            if (const auto value = process.ProcessEnvironmentValue(environment))
                static_cast<void>(vm.SetSystemProperty(key, *value));
        }
    }
    static_cast<void>(vm.SetSystemProperty("java.class.path", context->package_resource_path));
}

namespace {

[[nodiscard]] const dx::LinkedField& ApplicationInfoField(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                                                          const std::string_view name,
                                                          const std::string_view descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), std::string(name),
                                                           std::string(descriptor));
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "ApplicationInfo field is not linked: " + std::string(name));
    }
    return call.vm.Linker().Field(*field);
}

void SetApplicationInfoInt(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
                           const std::int32_t value) {
    const auto& field = ApplicationInfoField(call, object, name, "I");
    call.vm.Model().InstanceSlots(object)[field.slot] = {static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetApplicationInfoBoolean(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
                               const bool value) {
    const auto& field = ApplicationInfoField(call, object, name, "Z");
    call.vm.Model().InstanceSlots(object)[field.slot] = {value ? 1U : 0U, dx::SlotTag::cat1};
}

void SetApplicationInfoRef(dx::IntrinsicContext& call, const dx::VmObjectRef object, const std::string_view name,
                           const std::string_view descriptor, const dx::VmObjectRef value) {
    const auto& field = ApplicationInfoField(call, object, name, descriptor);
    call.vm.Model().InstanceSlots(object)[field.slot] = {value.Value(), dx::SlotTag::ref};
}

// commit()/apply() is the flush point: the VFS close persists it.

// Typed preference getter: absent keys answer the caller's default, a
// type mismatch throws the real ClassCastException.

}  // namespace

void PutAndroidMetaData(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef bundle,
                        const std::string& name, const loader::AndroidManifestMetaDataValue& value) {
    const auto key = call.vm.NewStringUtf8(name);
    const auto key_root = call.vm.ProtectReferences(std::array{key, bundle});
    const auto put_integer = [&](const std::int32_t integer) {
        static_cast<void>(CallAndroidMethod(call.vm, bundle, "putInt", "(Ljava/lang/String;I)V",
                                            {dx::VmValue::Ref(key), dx::VmValue::Int(integer)}));
    };
    const auto put_float = [&](const float number) {
        static_cast<void>(CallAndroidMethod(call.vm, bundle, "putFloat", "(Ljava/lang/String;F)V",
                                            {dx::VmValue::Ref(key), dx::VmValue::Float(number)}));
    };
    const auto put_boolean = [&](const bool boolean) {
        static_cast<void>(CallAndroidMethod(call.vm, bundle, "putBoolean", "(Ljava/lang/String;Z)V",
                                            {dx::VmValue::Ref(key), dx::VmValue::Int(boolean ? 1 : 0)}));
    };
    const auto put_string = [&](const std::string& text) {
        static_cast<void>(CallAndroidMethod(call.vm, bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
                                            {dx::VmValue::Ref(key), dx::VmValue::Ref(call.vm.NewStringUtf8(text))}));
    };
    if (std::holds_alternative<loader::AndroidManifestMetaDataNullString>(value)) {
        static_cast<void>(CallAndroidMethod(call.vm, bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
                                            {dx::VmValue::Ref(key), dx::VmValue::Ref(dx::VmObjectRef{})}));
    } else if (const auto* integer = std::get_if<std::int32_t>(&value)) {
        put_integer(*integer);
    } else if (const auto* number = std::get_if<float>(&value)) {
        put_float(*number);
    } else if (const auto* boolean = std::get_if<bool>(&value)) {
        put_boolean(*boolean);
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        put_string(*text);
    } else if (const auto* reference = std::get_if<loader::AndroidManifestMetaDataValueReference>(&value)) {
        const auto resources = ResourceObject(call, context, false);
        const auto typed = call.vm.NewIntrinsicInstance("Landroid/util/TypedValue;");
        const auto roots = call.vm.ProtectReferences(std::array{resources, typed});
        static_cast<void>(CallAndroidMethod(call.vm, resources, "getValue", "(ILandroid/util/TypedValue;Z)V",
                                            {dx::VmValue::Int(static_cast<std::int32_t>(reference->resource_id)),
                                             dx::VmValue::Ref(typed), dx::VmValue::Int(1)}));
        const auto get = [&](const char* field, const char* descriptor) {
            const auto member =
                call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(typed), field, descriptor);
            if (!member) throw std::logic_error("ROM TypedValue field is missing");
            return call.vm.Model().InstanceSlots(typed)[call.vm.Linker().Field(*member).slot].bits;
        };
        const auto type = get("type", "I"), data = get("data", "I");
        if (!type) throw std::runtime_error("meta-data value resource is null: " + name);
        if (type == 3) {
            const auto text = dx::VmObjectRef(get("string", "Ljava/lang/CharSequence;"));
            if (!text.IsValid()) throw std::runtime_error("meta-data value resource string is missing: " + name);
            const auto string = CallAndroidMethod(call.vm, text, "toString", "()Ljava/lang/String;");
            static_cast<void>(CallAndroidMethod(call.vm, bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
                                                {dx::VmValue::Ref(key), string}));
        } else if (type == 4) {
            put_float(std::bit_cast<float>(data));
        } else if (type == 0x12) {
            put_boolean(data != 0);
        } else if (type >= 0x10 && type <= 0x1f) {
            put_integer(static_cast<std::int32_t>(data));
        } else if (auto* logger = call.vm.Log()) {
            logger->Write(core::LogLevel::warn, "runtime.manifest", "ignored unsupported meta-data value type", {},
                          {{"name", name}, {"type", static_cast<std::uint64_t>(type)}});
        }
    } else {
        put_integer(
            static_cast<std::int32_t>(std::get<loader::AndroidManifestMetaDataResourceReference>(value).resource_id));
    }
}

dx::VmObjectRef MakeApplicationInfo(dx::IntrinsicContext& call, const Context& context, const bool include_meta_data,
                                    const PackageInfoKind kind) {
    const auto info = call.vm.NewIntrinsicInstance("Landroid/content/pm/ApplicationInfo;");
    const auto info_root = call.vm.ProtectReferences(std::array{info});
    const auto string = [&call](const std::string& value) { return call.vm.NewStringUtf8(value); };
    const auto package = string(context->package_name);
    const auto application_name = string(context->application_class_name);
    SetApplicationInfoRef(call, info, "name", "Ljava/lang/String;", application_name);
    SetApplicationInfoRef(call, info, "packageName", "Ljava/lang/String;", package);
    SetApplicationInfoRef(call, info, "className", "Ljava/lang/String;", application_name);
    SetApplicationInfoRef(
        call, info, "processName", "Ljava/lang/String;",
        string(context->application_process_name.empty() ? context->package_name : context->application_process_name));
    SetApplicationInfoInt(call, info, "icon", static_cast<std::int32_t>(context->application_icon));
    SetApplicationInfoInt(call, info, "theme", static_cast<std::int32_t>(context->application_theme));
    // Every DexVM process is assembled from a non-empty classes.dex. This is
    // the API19 PackageParser default unless android:hasCode explicitly says
    // otherwise; that attribute is not yet part of the sealed manifest facts.
    constexpr std::int32_t kApplicationFlagHasCode = 1 << 2;
    // Both the current installed APK and API19 archive queries' default
    // PackageUserState have installed=true.
    constexpr std::int32_t kApplicationFlagInstalled = 1 << 23;
    const auto archive = kind == PackageInfoKind::archive;
    SetApplicationInfoInt(call, info, "flags",
                          kApplicationFlagHasCode | kApplicationFlagInstalled |
                              (context->application_large_heap ? 0x00100000 : 0));
    SetApplicationInfoInt(call, info, "uid", archive ? -1 : static_cast<std::int32_t>(context->application_uid));
    SetApplicationInfoInt(call, info, "targetSdkVersion", static_cast<std::int32_t>(context->target_sdk_version));
    SetApplicationInfoBoolean(call, info, "enabled", context->application_enabled);
    // PackageParser does not assign installed paths when inspecting an archive.
    if (!archive) {
        const auto package_path = string(context->package_resource_path);
        SetApplicationInfoRef(call, info, "sourceDir", "Ljava/lang/String;", package_path);
        SetApplicationInfoRef(call, info, "publicSourceDir", "Ljava/lang/String;", package_path);
        SetApplicationInfoRef(call, info, "dataDir", "Ljava/lang/String;",
                              string("/data/data/" + context->package_name));
        SetApplicationInfoRef(call, info, "nativeLibraryDir", "Ljava/lang/String;", string("/data/app-lib"));
    }
    if (context->application_label.has_value()) {
        if (const auto* resource = std::get_if<std::uint32_t>(&*context->application_label)) {
            SetApplicationInfoInt(call, info, "labelRes", static_cast<std::int32_t>(*resource));
        } else {
            SetApplicationInfoRef(call, info, "nonLocalizedLabel", "Ljava/lang/CharSequence;",
                                  string(std::get<std::string>(*context->application_label)));
        }
    }
    if (include_meta_data) {
        const auto info_root = call.vm.ProtectReferences(std::array{info});
        const auto bundle = NewAndroidBundle(call.vm);
        const auto bundle_root = call.vm.ProtectReferences(std::array{bundle});
        for (const auto& [name, value] : context->application_meta_data) {
            PutAndroidMetaData(call, context, bundle, name, value);
        }
        SetApplicationInfoRef(call, info, "metaData", "Landroid/os/Bundle;", bundle);
    }
    return info;
}

namespace {}  // namespace

dx::IntrinsicHandler WidgetNoopHandler() {
    return dx::IntrinsicHandler([](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
}

dx::IntrinsicHandler PlatformSystemCurrentTimeMillisHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext&) {
        // Deterministic epoch base plus lifecycle-published uptime.
        return dx::VmValue::Long(1'400'000'000'000LL + context->uptime_millis.load());
    });
}

dx::IntrinsicHandler PlatformRuntimeNativeExitHandler(const Context& context) {
    return dx::IntrinsicHandler([context](dx::IntrinsicContext& call) -> dx::VmValue {
        context->exit_requested = true;
        call.vm.Exit(call.arguments[0].AsInt());
    });
}

namespace {

[[nodiscard]] std::string SystemLoadArgument(dx::IntrinsicContext& call, const char* name) {
    const auto reference = call.arguments[0].ref;
    if (!reference.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", std::string(name) + " == null"};
    }
    return call.vm.StringUtf8(reference);
}

template <typename Load>
dx::IntrinsicHandler PlatformSystemLoadHandlerImpl(const Context& context, const char* argument_name, Load load,
                                                   bool resolve_application_library = false) {
    return dx::IntrinsicHandler(
        [context, argument_name, load = std::move(load), resolve_application_library](dx::IntrinsicContext& call) {
            const auto argument = SystemLoadArgument(call, argument_name);
            if (argument == "soundpool" || argument == "media_jni") {
                return dx::VmValue::Void();
            }
            if (context->native_libraries == nullptr || context->application_class_loader_token == 0U) {
                throw dx::VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;", "APK native library loader is unavailable"};
            }
            std::optional<std::string> application_path;
            if (resolve_application_library && context->sealed_apk_bytes && argument != "javacrypto") {
                const auto caller = call.vm.CurrentCallerClass();
                if (caller && call.vm.Linker().ClassHeader(*caller).defining_loader == dx::kApplicationLoader) {
                    const auto loader = call.vm.ClassLoaders().ApplicationLoader();
                    const auto result = CallAndroidMethod(call.vm, loader, "findLibrary",
                                                          "(Ljava/lang/String;)Ljava/lang/String;", {call.arguments[0]})
                                            .ref;
                    if (!result.IsValid())
                        throw dx::VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;",
                                              "APK native logical library is unavailable: " + argument};
                    application_path = call.vm.StringUtf8(result);
                }
            }
            // Loading can wait on process call/load locks and invoke guest
            // constructors/JNI_OnLoad. JNI reentry acquires the VM lock itself.
            auto& execution_lock = call.vm.ExecutionLock();
            const auto depth = execution_lock.ReleaseForBlocking();
            try {
                if (application_path)
                    static_cast<void>(context->native_libraries->LoadPath(*application_path,
                                                                          context->application_class_loader_token));
                else
                    load(*context->native_libraries, argument, context->application_class_loader_token);
            } catch (const NativeLibraryLoadError& error) {
                execution_lock.ReacquireAfterBlocking(depth);
                throw dx::VmJavaThrow{"Ljava/lang/UnsatisfiedLinkError;", error.what()};
            } catch (...) {
                execution_lock.ReacquireAfterBlocking(depth);
                throw;
            }
            execution_lock.ReacquireAfterBlocking(depth);
            return dx::VmValue::Void();
        });
}

}  // namespace

dx::IntrinsicHandler PlatformSystemLoadHandler(const Context& context) {
    return PlatformSystemLoadHandlerImpl(
        context, "pathName",
        [](NativeLibraryLoader& libraries, const std::string_view path, const JavaClassLoaderToken class_loader) {
            static_cast<void>(libraries.LoadPath(path, class_loader));
        });
}

dx::IntrinsicHandler PlatformSystemLoadLibraryHandler(const Context& context) {
    return PlatformSystemLoadHandlerImpl(
        context, "libraryName",
        [](NativeLibraryLoader& libraries, const std::string_view name, const JavaClassLoaderToken class_loader) {
            const auto resolved = name == "javacrypto" ? std::string_view{"ogplay_jni"} : name;
            static_cast<void>(libraries.LoadLibrary(resolved, class_loader));
        },
        true);
}

dx::IntrinsicHandler PlatformSystemNanoTimeHandler(const Context& context) {
    return dx::IntrinsicHandler(
        [context](dx::IntrinsicContext&) { return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL); });
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from shared.cpp ----
// android.* intrinsic assembly: helpers shared by the handler batches
// plus the registration entry point. Batches live in the sibling
// dexvm_android_*.cpp files, one per platform area.

#include <algorithm>
#include <bit>
#include <cstring>

#include "ogplay/runtime/vfs/vfs.h"

#include "shared.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime {

namespace dx = dexvm;

namespace android_intrinsics {

[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call) {
    return dx::VmValue::Ref(call.receiver);
}

[[nodiscard]] dx::VmObjectRef ResourceObject(dx::IntrinsicContext& call, const Context& context, bool system) {
    if (system) {
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/content/res/Resources;");
        const auto method = call.vm.Linker().FindDirectMethod(type, "getSystem", "()Landroid/content/res/Resources;");
        if (!method) throw std::logic_error("ROM Resources.getSystem is missing");
        const auto result = call.vm.Call(*method, {});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value.ref;
    }
    if (const auto found = context->singletons.find("resources"); found != context->singletons.end())
        return found->second;
    const auto resources = CreateApplicationResources(call, context);
    context->singletons.emplace("resources", resources);
    return resources;
}

[[nodiscard]] dx::VmValue MakeString(dx::IntrinsicContext& call, const std::string& value) {
    return dx::VmValue::Ref(call.vm.NewStringUtf8(value));
}

dx::IntrinsicHandler NeutralHandler(const char shorty) {
    return [shorty](dx::IntrinsicContext&) {
        switch (shorty) {
        case 'V': return dx::VmValue::Void();
        case 'J': return dx::VmValue::Long(0);
        case 'F': return dx::VmValue::Float(0.0F);
        case 'D': return dx::VmValue::Double(0.0);
        case 'L': return dx::VmValue::Ref(dx::VmObjectRef{});
        case 'Z':
        case 'B':
        case 'S':
        case 'C':
        case 'I': return dx::VmValue::Int(0);
        default:
            throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                                 "unsupported neutral android intrinsic shorty");
        }
    };
}

void GuestLog(dx::IntrinsicContext& call, const core::LogLevel level, const std::string& line) {
    auto* logger = call.vm.Log();
    if (logger == nullptr) return;
    logger->Write(level, "runtime.dexvm.guest", line);
}

void RecordUiDegraded(dx::Interpreter& vm, const std::string_view what) {
    vm.RecordUiDegraded(what);
}

namespace {

[[nodiscard]] dx::VmCallOutcome NormalOutcome() {
    return {dx::VmValue::Void(), dx::VmObjectRef{}, dx::DexClassId{}, {}, {}};
}

[[nodiscard]] std::string RenderScheduledFailure(dx::Interpreter& vm, const dx::VmCallOutcome& outcome,
                                                 const char* callback) {
    std::string rendered = std::string(callback) + " raised: " + vm.Linker().Class(outcome.exception_class).descriptor +
                           ": " + outcome.exception_message;
    for (const auto& entry : outcome.exception_stack) {
        rendered +=
            "\n  at " + entry.class_descriptor + "." + entry.method_name + " (pc " + std::to_string(entry.pc) + ")";
    }
    rendered += vm.DescribeThrowableCauses(outcome.exception);
    return rendered;
}

[[nodiscard]] bool HasDue(DexVmAndroidContext& context, const dx::VmObjectRef looper, const std::int64_t now) {
    std::scoped_lock lock(context.scheduler_mutex);
    return std::ranges::any_of(context.scheduled_work,
                               [&](const auto& work) { return work.looper == looper && work.deadline_millis <= now; });
}

[[nodiscard]] std::optional<DexVmAndroidContext::ScheduledWork>
TakeDue(DexVmAndroidContext& context, const dx::VmObjectRef looper, const std::int64_t now) {
    std::scoped_lock lock(context.scheduler_mutex);
    auto selected = context.scheduled_work.end();
    for (auto it = context.scheduled_work.begin(); it != context.scheduled_work.end(); ++it) {
        if (it->looper != looper || it->deadline_millis > now) continue;
        if (selected == context.scheduled_work.end() || it->deadline_millis < selected->deadline_millis ||
            (it->deadline_millis == selected->deadline_millis &&
             (it->deadline_millis == std::numeric_limits<std::int64_t>::min() ? it->sequence > selected->sequence
                                                                              : it->sequence < selected->sequence))) {
            selected = it;
        }
    }
    if (selected == context.scheduled_work.end()) return std::nullopt;
    auto work = *selected;
    context.scheduled_work.erase(selected);
    return work;
}

[[nodiscard]] dx::VmCallOutcome DispatchScheduled(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                  const DexVmAndroidContext::ScheduledWork& work) {
    using Kind = DexVmAndroidContext::ScheduledWorkKind;
    if (work.kind == Kind::display_vsync) {
        {
            std::scoped_lock lock(context.scheduler_mutex);
            const auto found = context.display_receivers.find(static_cast<std::uint32_t>(work.generation));
            if (context.scheduler_shutdown || context.process_stopping || found == context.display_receivers.end() ||
                found->second.receiver != work.payload)
                return NormalOutcome();
        }
        const auto owner = vm.Linker().ResolveDescriptor("Landroid/view/DisplayEventReceiver;");
        const auto dispatch = vm.Linker().FindDirectMethod(owner, "dispatchVsync", "(JII)V");
        if (!dispatch) throw std::logic_error("DisplayEventReceiver.dispatchVsync is not linked");
        return vm.Call(*dispatch, std::array{dx::VmValue::Ref(work.payload), dx::VmValue::Long(work.timestamp_nanos),
                                             dx::VmValue::Int(0), dx::VmValue::Int(work.what)});
    }
    return NormalOutcome();
}

[[nodiscard]] std::optional<std::string> PumpLooperDue(dx::Interpreter& vm, DexVmAndroidContext& context,
                                                       const dx::VmObjectRef looper,
                                                       const bool yield_on_budget = false) {
    constexpr std::size_t kMaxCallbacksPerPump = 10'000;
    for (std::size_t count = 0; count < kMaxCallbacksPerPump; ++count) {
        const auto now = context.uptime_millis.load();
        // An idle pump must not wait for the VM lock.
        if (!HasDue(context, looper, now)) return std::nullopt;
        // Queued work is a GC root only while queued. The lifecycle thread pumps
        // the main Looper without the VM lock, so hold it from removal until the
        // call has copied the references into its frame. Otherwise another Java
        // thread can collect in that window and reclaim the payload.
        const dx::VmExecutionLockScope guard(vm.ExecutionLock());
        const auto work = TakeDue(context, looper, now);
        if (!work.has_value()) return std::nullopt;
        const auto outcome = DispatchScheduled(vm, context, *work);
        if (outcome.exception.IsValid()) {
            return RenderScheduledFailure(vm, outcome, "scheduled callback");
        }
    }
    // A worker Looper is a continuous message loop. A self-reposting producer
    // yields the execution lock at this boundary instead of killing its thread.
    return yield_on_budget ? std::nullopt
                           : std::optional<std::string>{"Android scheduler exceeded the per-pump callback budget"};
}

}  // namespace

namespace {
dx::Slot& RomField(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error(std::string("ROM field missing: ") + name);
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
}

dx::VmObjectRef RomLooperStatic(dx::Interpreter& vm, const char* name) {
    const auto owner = vm.Linker().ResolveDescriptor("Landroid/os/Looper;");
    const auto method = vm.Linker().FindDirectMethod(owner, name, "()Landroid/os/Looper;");
    if (!method) throw std::logic_error("ROM Looper entry unavailable");
    const auto outcome = vm.Call(*method, {});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value.ref;
}

void RegisterRomLooper(dx::Interpreter& vm, const Context& context, dx::VmObjectRef looper, bool main) {
    if (!looper.IsValid()) return;
    const auto queue = dx::VmObjectRef{RomField(vm, looper, "mQueue", "Landroid/os/MessageQueue;").bits};
    const auto thread = dx::VmObjectRef{RomField(vm, looper, "mThread", "Ljava/lang/Thread;").bits};
    std::scoped_lock lock(context->scheduler_mutex);
    const auto token = vm.CurrentContextToken();
    context->thread_loopers[token] = looper;
    context->loopers[looper.Value()] = {token, thread, main, false, queue};
    if (main) context->main_looper = looper;
}
}  // namespace

dx::VmObjectRef RegisterCurrentRomLooper(dx::Interpreter& vm, const Context& context) {
    const auto looper = RomLooperStatic(vm, "myLooper");
    RegisterRomLooper(vm, context, looper, looper.IsValid() && looper == context->main_looper);
    return looper;
}

dx::VmObjectRef EnsureMainLooper(dx::Interpreter& vm, const Context& context) {
    {
        std::scoped_lock lock(context->scheduler_mutex);
        if (context->main_looper.IsValid()) return context->main_looper;
    }
    auto main = RomLooperStatic(vm, "getMainLooper");
    if (!main.IsValid()) {
        if (vm.CurrentContextToken() != 1U) throw std::logic_error("main Looper must be prepared by root thread");
        const auto owner = vm.Linker().ResolveDescriptor("Landroid/os/Looper;");
        const auto prepare = vm.Linker().FindDirectMethod(owner, "prepareMainLooper", "()V");
        const auto result = vm.Call(*prepare, {});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        main = RomLooperStatic(vm, "getMainLooper");
    }
    RegisterRomLooper(vm, context, main, true);
    return main;
}

dx::VmObjectRef EnsureMainLooper(dx::IntrinsicContext& call, const Context& context) {
    return EnsureMainLooper(call.vm, context);
}

dx::VmObjectRef CurrentLooper(const Context& context, std::uint64_t context_token) {
    std::scoped_lock lock(context->scheduler_mutex);
    const auto found = context->thread_loopers.find(context_token);
    return found == context->thread_loopers.end() ? dx::VmObjectRef{} : found->second;
}

void PumpNativeLooperEvents(dx::Interpreter& vm, DexVmAndroidContext& context, dx::VmObjectRef looper) {
    if (const auto error = PumpLooperDue(vm, context, looper)) throw std::runtime_error(*error);
}

DisabledWebTarget ParseDisabledWebTarget(const std::string_view url) {
    DisabledWebTarget result;
    result.url = std::string(url);
    std::string lower(url);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    result.javascript = lower.starts_with("javascript:");
    std::size_t authority = std::string_view::npos;
    if (lower.starts_with("http://")) authority = 7U;
    if (lower.starts_with("https://")) authority = 8U;
    if (authority == std::string_view::npos) return result;
    result.http_or_https = true;
    const auto host_end = url.find_first_of("/?#", authority);
    result.host = std::string(
        url.substr(authority, host_end == std::string_view::npos ? url.size() - authority : host_end - authority));
    if (host_end != std::string_view::npos && url[host_end] == '/') {
        const auto path_end = url.find_first_of("?#", host_end);
        result.path = std::string(
            url.substr(host_end, path_end == std::string_view::npos ? url.size() - host_end : path_end - host_end));
    }
    if (result.path.empty()) result.path = "/";
    return result;
}

void LogDisabledWeb(dx::Interpreter& vm, const std::string_view action, const DisabledWebTarget& target) {
    auto* logger = vm.Log();
    if (logger == nullptr) return;
    std::vector<core::LogField> fields{{"action", std::string(action)}};
    if (!target.url.empty()) fields.push_back({"url", target.url});
    if (!target.host.empty()) fields.push_back({"host", target.host});
    if (!target.path.empty()) fields.push_back({"path", target.path});
    logger->Write(core::LogLevel::warn, "runtime.web.disabled",
                  "web feature disabled by policy: " + std::string(action), {}, std::move(fields),
                  {.mode = core::RateLimitMode::first_and_count});
}

DexVmAndroidContext::VideoViewState* VideoStateOf(const Context& context, const std::uint64_t handle) {
    const auto found = context->video_views.find(handle);
    return found == context->video_views.end() ? nullptr : &found->second;
}

std::int64_t VideoPositionOf(const DexVmAndroidContext::VideoViewState& state, const std::int64_t uptime_ms) {
    if (!state.playing) return state.base_position_ms;
    const auto elapsed = std::max<std::int64_t>(uptime_ms - state.start_uptime_ms, 0);
    const auto position = state.base_position_ms + elapsed;
    return state.looping && state.duration_ms > 0 ? position % state.duration_ms
                                                  : std::min(position, state.duration_ms);
}

std::optional<std::string> InvokeVideoEvent(dx::Interpreter& vm, DexVmAndroidContext& context,
                                            const std::uint64_t handle, const std::int32_t generation,
                                            const std::int32_t event) {
    dexvm::VmObjectRef weak;
    std::int32_t width{}, height{};
    {
        std::scoped_lock lock(context.video_views_mutex);
        const auto video = context.video_views.find(handle);
        if (video == context.video_views.end() || video->second.generation != generation) return std::nullopt;
        const auto media = context.media_players.find(static_cast<std::uint32_t>(handle));
        if (media == context.media_players.end()) return std::nullopt;
        weak = media->second.jni_weak;
        if (video->second.player) {
            width = static_cast<std::int32_t>(video->second.player->Metadata().width);
            height = static_cast<std::int32_t>(video->second.player->Metadata().height);
        }
        using Phase = DexVmAndroidContext::MediaPlayerState::Phase;
        if (event == 2) media->second.phase = Phase::completed;
        if (event == 100) media->second.phase = Phase::error;
    }
    const auto owner = dx::VmObjectRef{static_cast<std::uint32_t>(handle)};
    const auto roots = vm.ProtectReferences(std::array{owner, weak});
    const auto type = vm.Linker().ResolveDescriptor("Landroid/media/MediaPlayer;");
    const auto method =
        vm.Linker().FindDirectMethod(type, "postEventFromNative", "(Ljava/lang/Object;IIILjava/lang/Object;)V");
    if (!method) return "MediaPlayer event protocol is unavailable";
    const auto post = [&](int what, int arg1, int arg2) -> std::optional<std::string> {
        const auto outcome =
            vm.Call(*method, std::array{dx::VmValue::Ref(weak), dx::VmValue::Int(what), dx::VmValue::Int(arg1),
                                        dx::VmValue::Int(arg2), dx::VmValue::Ref(dx::VmObjectRef{})});
        if (outcome.exception.IsValid()) return "MediaPlayer event raised: " + outcome.exception_message;
        return std::nullopt;
    };
    if (event == 1)
        if (auto error = post(5, width, height)) return error;
    return post(event, event == 100 ? 1 : 0, event == 100 ? -1 : 0);
}

}  // namespace android_intrinsics

void PrepareAndroidMainLooper(dx::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    static_cast<void>(android_intrinsics::EnsureMainLooper(vm, context));
}

void NotifyAndroidScheduler(DexVmAndroidContext& context) {
    context.scheduler_changed.notify_all();
    if (context.notify_native_loopers) context.notify_native_loopers();
}

void AdvanceAndroidClock(DexVmAndroidContext& context, const std::int64_t delta_millis) {
    if (delta_millis <= 0) return;
    {
        std::scoped_lock lock(context.scheduler_mutex);
        context.uptime_millis.fetch_add(delta_millis);
    }
    NotifyAndroidScheduler(context);
}

void ShutdownAndroidScheduler(DexVmAndroidContext& context) {
    {
        std::scoped_lock lock(context.scheduler_mutex);
        context.scheduler_shutdown = true;
        context.display_receivers.clear();
        for (auto& [_, looper] : context.loopers)
            looper.quitting = true;
        context.scheduled_work.clear();
    }
    NotifyAndroidScheduler(context);
}

void RegisterAndroidSchedulerStateTable(dx::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.Threads().SetShutdownObserver([weak = std::weak_ptr<DexVmAndroidContext>(context)] {
        if (const auto current = weak.lock()) ShutdownAndroidScheduler(*current);
    });
    vm.RegisterIntrinsicStateTable(
        {"android.scheduler",
         [context](const dx::VmObjectRef owner, const dx::VmRootVisitor& visit) {
             std::scoped_lock lock(context->scheduler_mutex);
             const auto visit_if = [&visit](const dx::VmObjectRef ref) {
                 if (ref.IsValid()) visit(ref);
             };
             for (const auto& [_, receiver] : context->display_receivers) {
                 if (receiver.receiver == owner) {
                     visit_if(receiver.looper);
                     visit_if(receiver.queue);
                 }
             }
             if (const auto found = context->loopers.find(owner.Value()); found != context->loopers.end()) {
                 visit_if(found->second.thread);
             }
             for (const auto& [looper_handle, state] : context->loopers) {
                 if (state.thread == owner) {
                     visit_if(dx::VmObjectRef(looper_handle));
                 }
             }
         },
         [context](const dx::VmObjectRef owner) {
             std::scoped_lock lock(context->scheduler_mutex);
             std::erase_if(context->display_receivers,
                           [owner](const auto& item) { return item.second.receiver == owner; });
             context->loopers.erase(owner.Value());
             std::erase_if(context->thread_loopers, [owner](const auto& entry) { return entry.second == owner; });
             std::erase_if(context->scheduled_work, [owner](const auto& work) {
                 return work.looper == owner || work.owner == owner || work.target == owner || work.payload == owner ||
                        work.token == owner;
             });
         },
         {}});
}

bool SessionExitRequested(const DexVmAndroidContext& context) {
    return context.exit_requested.load();
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
dx::VmValue CallAndroidMethod(dx::Interpreter& vm, dx::VmObjectRef receiver, const char* name, const char* signature,
                              std::vector<dx::VmValue> arguments) {
    if (!receiver.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", name};
    const auto type = vm.Model().ObjectClass(receiver);
    const auto index = vm.Linker().FindVtableIndex(type, name, signature);
    if (!index) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
    arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
    const auto outcome = vm.Call(vm.Linker().Class(type).vtable[*index], arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value;
}

dx::VmObjectRef NewAndroidBundle(dx::Interpreter& vm, dx::VmObjectRef source) {
    const auto source_root = vm.ProtectReferences(std::array{source});
    const auto initialized = vm.EnsureClassInitialized(vm.Linker().ResolveDescriptor("Landroid/os/Bundle;"));
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    const auto bundle = vm.NewIntrinsicInstance("Landroid/os/Bundle;");
    const auto root = vm.ProtectReferences(std::array{bundle});
    const auto constructor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(bundle), "<init>",
                                                          source.IsValid() ? "(Landroid/os/Bundle;)V" : "()V");
    if (!constructor)
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Bundle constructor is not linked");
    std::vector arguments{dx::VmValue::Ref(bundle)};
    if (source.IsValid()) arguments.push_back(dx::VmValue::Ref(source));
    const auto outcome = vm.Call(*constructor, arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return bundle;
}

dx::VmObjectRef NewAndroidComponentName(dx::Interpreter& vm, dx::VmObjectRef package, dx::VmObjectRef class_name) {
    const auto inputs = vm.ProtectReferences(std::array{package, class_name});
    constexpr auto descriptor = "Landroid/content/ComponentName;";
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto roots = vm.ProtectReferences(std::array{object});
    const auto type = vm.Linker().ResolveDescriptor(descriptor);
    const auto init = vm.Linker().FindDirectMethod(type, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!init) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, descriptor);
    const auto outcome =
        vm.Call(*init, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(package), dx::VmValue::Ref(class_name)});
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return object;
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
void QueueAndroidDisplayFrame(dexvm::VmExecutionLock& execution_lock, DexVmAndroidContext& context) {
    const dexvm::VmExecutionLockScope execution(execution_lock);
    if (context.process_stopping || context.scheduler_shutdown) return;
    {
        std::scoped_lock lock(context.scheduler_mutex);
        if (context.display_vsync_frame == static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            throw std::overflow_error("Display vsync frame counter exhausted");
        const auto frame = ++context.display_vsync_frame;
        const auto now = context.uptime_millis.load();
        for (auto& [token, receiver] : context.display_receivers) {
            if (!receiver.requested) continue;
            receiver.requested = false;
            const auto looper = context.loopers.find(receiver.looper.Value());
            if (looper == context.loopers.end() || looper->second.quitting) continue;
            DexVmAndroidContext::ScheduledWork work{now,
                                                    context.next_scheduler_sequence++,
                                                    DexVmAndroidContext::ScheduledWorkKind::display_vsync,
                                                    receiver.looper,
                                                    receiver.receiver,
                                                    receiver.receiver,
                                                    receiver.receiver,
                                                    dexvm::VmObjectRef{},
                                                    static_cast<std::int32_t>(frame),
                                                    token};
            work.timestamp_nanos = now * 1'000'000;
            context.scheduled_work.push_back(work);
        }
    }
    NotifyAndroidScheduler(context);
}

}  // namespace ogplay::runtime
