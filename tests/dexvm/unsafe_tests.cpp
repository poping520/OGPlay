#include "boot_dex.h"
#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace {
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
constexpr auto kUnsafe = "Lsun/misc/Unsafe;";

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

struct UnsafeVm {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::atomic<std::int64_t> millis{};
    Interpreter vm;
    VmThreadRuntime threads;
    VmObjectRef unsafe;

    explicit UnsafeVm(InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                      bool boot = true)
        : vm([&]() -> DexClassLinker& {
            CoreIntrinsicServices services;
            services.current_time_millis = [this] { return 1'400'000'000'000LL + millis.load(); };
            linker.RegisterIntrinsics(CoreIntrinsicCatalog(services));
            if (boot) {
                const auto raw = ReadBytes(std::filesystem::path(OGPLAY_SOURCE_DIR) /
                    "data/android/19/framework/bootdex.jar");
                std::vector<std::byte> bytes(raw.size());
                std::memcpy(bytes.data(), raw.data(), raw.size());
                const auto dex = ogplay::loader::ReadApkEntry(bytes,
                    ogplay::loader::ParseApkArchive(bytes), "classes.dex");
                std::vector<std::uint8_t> data(dex.size());
                std::memcpy(data.data(), dex.data(), dex.size());
                ogplay::test::BindBootDexPlatformNatives(linker);
                linker.RegisterBootDex(std::move(data));
            }
            linker.RegisterDex(ReadBytes(std::filesystem::path(OGPLAY_DEXVM_FIXTURE_DIR) / "unsafe.dex"));
            linker.Link();
            return linker;
        }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}), threads(vm) {
        vm.Monitors().SetTimeSource([this] { return millis.load(); });
        vm.Monitors().SetClockAdvance([this](std::int64_t delta) { millis += delta; });
        unsafe = Ok(Static(kUnsafe, "getUnsafe", "()Lsun/misc/Unsafe;")).ref;
    }
    ~UnsafeVm() { threads.Shutdown(); }
    static VmValue Ok(const VmCallOutcome& result) {
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    void Throws(const VmCallOutcome& result, std::string_view descriptor) {
        REQUIRE_MESSAGE(result.exception.IsValid(), descriptor);
        CHECK(linker.Class(model.ObjectClass(result.exception)).descriptor == descriptor);
    }
    VmCallOutcome Static(std::string_view owner, std::string name, std::string signature,
                          std::vector<VmValue> args = {}) {
        const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, signature);
        REQUIRE(method.has_value());
        return vm.Call(*method, args);
    }
    VmCallOutcome Virtual(VmObjectRef object, std::string name, std::string signature,
                           std::vector<VmValue> args = {}) {
        const auto owner = model.ObjectClass(object);
        const auto index = linker.FindVtableIndex(owner, name, signature);
        REQUIRE(index.has_value());
        args.insert(args.begin(), VmValue::Ref(object));
        return vm.Call(linker.Class(owner).vtable[*index], args);
    }
    VmCallOutcome Call(std::string name, std::string signature, std::vector<VmValue> args = {}) {
        return Virtual(unsafe, std::move(name), std::move(signature), std::move(args));
    }
    VmObjectRef Class(std::string_view descriptor) {
        return model.ClassObject(linker.ResolveDescriptor(descriptor));
    }
    VmObjectRef Allocate(std::string_view descriptor) {
        return Ok(Call("allocateInstance", "(Ljava/lang/Class;)Ljava/lang/Object;",
                       {VmValue::Ref(Class(descriptor))})).ref;
    }
    std::int64_t Offset(std::string_view owner, std::string_view field) {
        const auto meta = vm.Reflection().FindDeclaredField(linker.ResolveDescriptor(owner), field);
        REQUIRE(meta.has_value());
        const auto wrapper = vm.Reflection().MaterializeField(*meta);
        return Ok(Call("objectFieldOffset", "(Ljava/lang/reflect/Field;)J",
                       {VmValue::Ref(wrapper)})).AsLong();
    }
};
}

TEST_CASE("Unsafe API19 singleton caller checks and allocation") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        UnsafeVm f(backend);
        CHECK(UnsafeVm::Ok(f.Static(kUnsafe, "getUnsafe", "()Lsun/misc/Unsafe;")).ref == f.unsafe);
        for (const auto name : {"THE_ONE", "theUnsafe"}) {
            const auto id = f.linker.FindFieldRecursive(f.linker.ResolveDescriptor(kUnsafe), name, kUnsafe);
            REQUIRE(id.has_value());
            const auto& field = f.linker.Field(*id);
            CHECK(f.linker.Class(field.owner).static_storage[field.slot] == f.unsafe.Value());
        }
        f.Throws(f.Static("LUnsafeBean;", "denied", "()Lsun/misc/Unsafe;"), "Ljava/lang/SecurityException;");
        const auto object = f.Allocate("LUnsafeBean;");
        CHECK(UnsafeVm::Ok(f.Virtual(object, "get", "()I")).AsInt() == 0);
        const auto id = f.linker.FindFieldRecursive(f.linker.ResolveDescriptor("LUnsafeBean;"), "initialized", "I");
        REQUIRE(id.has_value());
        const auto& field = f.linker.Field(*id);
        CHECK(f.linker.Class(field.owner).static_storage[field.slot] == 7);
        for (const auto type : {"I", "[I", "Ljava/lang/Runnable;", "LUnsafeAbstract;"})
            f.Throws(f.Call("allocateInstance", "(Ljava/lang/Class;)Ljava/lang/Object;",
                            {VmValue::Ref(f.Class(type))}), "Ljava/lang/InstantiationException;");
        f.Throws(f.Call("allocateInstance", "(Ljava/lang/Class;)Ljava/lang/Object;",
                        {VmValue::Ref(VmObjectRef{})}), "Ljava/lang/NullPointerException;");
        const auto failed = f.Call("allocateInstance", "(Ljava/lang/Class;)Ljava/lang/Object;",
                                   {VmValue::Ref(f.Class("LUnsafeBadInit;"))});
        f.Throws(failed, "Ljava/lang/AssertionError;");
        const auto failure_id = f.linker.FindFieldRecursive(
            f.linker.ResolveDescriptor("LUnsafeBadInit;"), "failure", "Ljava/lang/Throwable;");
        REQUIRE(failure_id.has_value());
        const auto& failure_field = f.linker.Field(*failure_id);
        CHECK(f.linker.Class(failure_field.owner).static_storage[failure_field.slot] ==
              failed.exception.Value());
    }
}

TEST_CASE("Unsafe fields share DEX storage and reject invalid locations") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        UnsafeVm f(backend);
        const auto object = f.Allocate("LUnsafeChild;");
        const auto offset = f.Offset("LUnsafeBean;", "value");
        CHECK(offset == f.Offset("LUnsafeBean;", "value"));
        UnsafeVm::Ok(f.Virtual(object, "set", "(I)V", {VmValue::Int(-13)}));
        for (const auto get : {"getInt", "getIntVolatile"})
            CHECK(UnsafeVm::Ok(f.Call(get, "(Ljava/lang/Object;J)I",
                  {VmValue::Ref(object), VmValue::Long(offset)})).AsInt() == -13);
        for (const auto put : {"putInt", "putIntVolatile", "putOrderedInt"}) {
            UnsafeVm::Ok(f.Call(put, "(Ljava/lang/Object;JI)V",
                {VmValue::Ref(object), VmValue::Long(offset), VmValue::Int(31)}));
            CHECK(UnsafeVm::Ok(f.Virtual(object, "get", "()I")).AsInt() == 31);
        }
        for (const auto expected : {30, 31})
            CHECK((UnsafeVm::Ok(f.Call("compareAndSwapInt", "(Ljava/lang/Object;JII)Z",
                {VmValue::Ref(object), VmValue::Long(offset), VmValue::Int(expected), VmValue::Int(88)})).AsInt() != 0) == (expected == 31));
        CHECK(UnsafeVm::Ok(f.Virtual(object, "get", "()I")).AsInt() == 88);
        f.Throws(f.Call("getLong", "(Ljava/lang/Object;J)J", {VmValue::Ref(object), VmValue::Long(offset)}),
                 "Ljava/lang/IllegalArgumentException;");
        for (const auto bad : {-1LL, 0LL, std::numeric_limits<long long>::max()})
            f.Throws(f.Call("getInt", "(Ljava/lang/Object;J)I", {VmValue::Ref(object), VmValue::Long(bad)}),
                     "Ljava/lang/IllegalArgumentException;");
        f.Throws(f.Call("getInt", "(Ljava/lang/Object;J)I", {VmValue::Ref(VmObjectRef{}), VmValue::Long(offset)}),
                 "Ljava/lang/NullPointerException;");
        f.Throws(f.Call("getInt", "(Ljava/lang/Object;J)I", {VmValue::Ref(f.Allocate("Ljava/lang/Object;")), VmValue::Long(offset)}),
                 "Ljava/lang/IllegalArgumentException;");
        const auto meta = f.vm.Reflection().FindDeclaredField(f.linker.ResolveDescriptor("LUnsafeBean;"), "initialized");
        REQUIRE(meta.has_value());
        f.Throws(f.Call("objectFieldOffset", "(Ljava/lang/reflect/Field;)J",
            {VmValue::Ref(f.vm.Reflection().MaterializeField(*meta))}), "Ljava/lang/IllegalArgumentException;");
        f.Throws(f.Call("objectFieldOffset", "(Ljava/lang/reflect/Field;)J", {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");
        const auto wide = f.Offset("LUnsafeBean;", "wide");
        const auto bits = std::numeric_limits<std::int64_t>::min() + 0x12345678;
        for (const auto put : {"putLong", "putLongVolatile", "putOrderedLong"}) {
            UnsafeVm::Ok(f.Call(put, "(Ljava/lang/Object;JJ)V", {VmValue::Ref(object), VmValue::Long(wide), VmValue::Long(bits)}));
            for (const auto get : {"getLong", "getLongVolatile"})
                CHECK(UnsafeVm::Ok(f.Call(get, "(Ljava/lang/Object;J)J", {VmValue::Ref(object), VmValue::Long(wide)})).AsLong() == bits);
        }
        CHECK(UnsafeVm::Ok(f.Call("compareAndSwapLong", "(Ljava/lang/Object;JJJ)Z",
            {VmValue::Ref(object), VmValue::Long(wide), VmValue::Long(bits + 1), VmValue::Long(1)})).AsInt() == 0);
        CHECK(UnsafeVm::Ok(f.Call("compareAndSwapLong", "(Ljava/lang/Object;JJJ)Z",
            {VmValue::Ref(object), VmValue::Long(wide), VmValue::Long(bits), VmValue::Long(1)})).AsInt() == 1);
    }
}

TEST_CASE("Unsafe references preserve identity and GC edges") {
    UnsafeVm f;
    const auto object = f.Allocate("LUnsafeBean;");
    const auto offset = f.Offset("LUnsafeBean;", "reference");
    const auto first = f.vm.NewStringUtf8("same");
    const auto second = f.vm.NewStringUtf8("same");
    const std::array roots{object};
    auto protected_roots = f.vm.ProtectReferences(roots);
    for (const auto put : {"putObject", "putObjectVolatile", "putOrderedObject"})
        UnsafeVm::Ok(f.Call(put, "(Ljava/lang/Object;JLjava/lang/Object;)V",
                           {VmValue::Ref(object), VmValue::Long(offset), VmValue::Ref(first)}));
    CHECK(UnsafeVm::Ok(f.Call("compareAndSwapObject", "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z",
        {VmValue::Ref(object), VmValue::Long(offset), VmValue::Ref(second), VmValue::Ref(VmObjectRef{})})).AsInt() == 0);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK(f.model.IsValidRef(first));
    CHECK_FALSE(f.model.IsValidRef(second));
    for (const auto get : {"getObject", "getObjectVolatile"})
        CHECK(UnsafeVm::Ok(f.Call(get, "(Ljava/lang/Object;J)Ljava/lang/Object;",
            {VmValue::Ref(object), VmValue::Long(offset)})).ref == first);
    f.Throws(f.Call("putObject", "(Ljava/lang/Object;JLjava/lang/Object;)V",
        {VmValue::Ref(object), VmValue::Long(offset), VmValue::Ref(object)}), "Ljava/lang/IllegalArgumentException;");
    CHECK(UnsafeVm::Ok(f.Call("compareAndSwapObject", "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z",
        {VmValue::Ref(object), VmValue::Long(offset), VmValue::Ref(first), VmValue::Ref(VmObjectRef{})})).AsInt() == 1);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK_FALSE(f.model.IsValidRef(first));
    CHECK(f.model.IsValidRef(f.unsafe));
}

TEST_CASE("Unsafe arrays use checked base scale and shared array stores") {
    UnsafeVm f;
    for (const auto& [descriptor, scale] : std::vector<std::pair<std::string, int>>{
         {"[Z", 1}, {"[B", 1}, {"[C", 2}, {"[S", 2}, {"[I", 4}, {"[F", 4},
         {"[J", 8}, {"[D", 8}, {"[Ljava/lang/String;", 4}, {"[[I", 4}}) {
        CHECK(UnsafeVm::Ok(f.Call("arrayBaseOffset", "(Ljava/lang/Class;)I", {VmValue::Ref(f.Class(descriptor))})).AsInt() == 16);
        CHECK(UnsafeVm::Ok(f.Call("arrayIndexScale", "(Ljava/lang/Class;)I", {VmValue::Ref(f.Class(descriptor))})).AsInt() == scale);
    }
    f.Throws(f.Call("arrayBaseOffset", "(Ljava/lang/Class;)I", {VmValue::Ref(f.Class("Ljava/lang/Object;"))}), "Ljava/lang/IllegalArgumentException;");
    f.Throws(f.Call("arrayIndexScale", "(Ljava/lang/Class;)I", {VmValue::Ref(VmObjectRef{})}), "Ljava/lang/NullPointerException;");
    const auto ints = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
    UnsafeVm::Ok(f.Call("putOrderedInt", "(Ljava/lang/Object;JI)V", {VmValue::Ref(ints), VmValue::Long(20), VmValue::Int(8)}));
    CHECK(f.model.GetPrimitiveElement(ints, 1) == 8);
    CHECK(UnsafeVm::Ok(f.Call("compareAndSwapInt", "(Ljava/lang/Object;JII)Z",
        {VmValue::Ref(ints), VmValue::Long(20), VmValue::Int(8), VmValue::Int(9)})).AsInt() == 1);
    for (const auto offset : {15, 17})
        f.Throws(f.Call("getInt", "(Ljava/lang/Object;J)I", {VmValue::Ref(ints), VmValue::Long(offset)}), "Ljava/lang/IllegalArgumentException;");
    f.Throws(f.Call("getInt", "(Ljava/lang/Object;J)I", {VmValue::Ref(ints), VmValue::Long(24)}), "Ljava/lang/ArrayIndexOutOfBoundsException;");
    f.Throws(f.Call("getLong", "(Ljava/lang/Object;J)J", {VmValue::Ref(ints), VmValue::Long(16)}), "Ljava/lang/IllegalArgumentException;");
    const auto longs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[J"), JniPrimitiveKind::long_integer, 1);
    CHECK(UnsafeVm::Ok(f.Call("compareAndSwapLong", "(Ljava/lang/Object;JJJ)Z",
        {VmValue::Ref(longs), VmValue::Long(16), VmValue::Long(0), VmValue::Long(-1)})).AsInt() == 1);
    CHECK(f.model.GetPrimitiveElement(longs, 0) == std::numeric_limits<std::uint64_t>::max());
    const auto refs = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/String;"), f.linker.ResolveDescriptor("Ljava/lang/String;"), 1);
    const auto text = f.vm.NewStringUtf8("array edge");
    const std::array roots{refs};
    auto protected_roots = f.vm.ProtectReferences(roots);
    CHECK(UnsafeVm::Ok(f.Call("compareAndSwapObject", "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z",
        {VmValue::Ref(refs), VmValue::Long(16), VmValue::Ref(VmObjectRef{}), VmValue::Ref(text)})).AsInt() == 1);
    static_cast<void>(f.vm.CollectGarbage());
    CHECK(f.model.IsValidRef(text));
    CHECK(f.model.GetObjectElement(refs, 0) == text);
}

TEST_CASE("Unsafe park uses one permit and unified epoch Clock") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        UnsafeVm f(backend);
        const auto thread = UnsafeVm::Ok(f.Static("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;")).ref;
        for (int i = 0; i < 2; ++i)
            UnsafeVm::Ok(f.Call("unpark", "(Ljava/lang/Object;)V", {VmValue::Ref(thread)}));
        UnsafeVm::Ok(f.Call("park", "(ZJ)V", {VmValue::Int(0), VmValue::Long(0)}));
        CHECK(f.millis.load() == 0);
        UnsafeVm::Ok(f.Call("park", "(ZJ)V", {VmValue::Int(0), VmValue::Long(1'500'001)}));
        CHECK(f.millis.load() == 2);
        UnsafeVm::Ok(f.Call("park", "(ZJ)V", {VmValue::Int(1), VmValue::Long(1'400'000'000'012LL)}));
        CHECK(f.millis.load() == 12);
        UnsafeVm::Ok(f.Call("park", "(ZJ)V", {VmValue::Int(1), VmValue::Long(-1)}));
        CHECK(f.millis.load() == 12);
        f.Throws(f.Call("park", "(ZJ)V", {VmValue::Int(0), VmValue::Long(-1)}), "Ljava/lang/IllegalArgumentException;");
        f.Throws(f.Call("unpark", "(Ljava/lang/Object;)V", {VmValue::Ref(VmObjectRef{})}), "Ljava/lang/IllegalArgumentException;");
        f.Throws(f.Call("unpark", "(Ljava/lang/Object;)V", {VmValue::Ref(f.unsafe)}), "Ljava/lang/IllegalArgumentException;");
        UnsafeVm::Ok(f.Virtual(thread, "interrupt", "()V"));
        UnsafeVm::Ok(f.Call("park", "(ZJ)V", {VmValue::Int(0), VmValue::Long(0)}));
        CHECK(UnsafeVm::Ok(f.Virtual(thread, "isInterrupted", "()Z")).AsInt() == 1);
    }
}

TEST_CASE("Unsafe CAS is atomic across guest execution contexts") {
    UnsafeVm f;
    const auto object = f.Allocate("LUnsafeBean;");
    const auto offset = f.Offset("LUnsafeBean;", "value");
    const auto method = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("LUnsafeBean;"),
        "increment", "(Lsun/misc/Unsafe;Ljava/lang/Object;J)I");
    REQUIRE(method.has_value());
    const std::array args{VmValue::Ref(f.unsafe), VmValue::Ref(object), VmValue::Long(offset)};
    const auto first = f.vm.CreateExecutionContext();
    const auto second = f.vm.CreateExecutionContext();
    std::atomic<int> failures{};
    const auto run = [&](const InterpreterExecutionContext& context) {
        for (int i = 0; i < 100; ++i) {
            if (f.vm.Call(context, *method, args).exception.IsValid()) ++failures;
        }
    };
    std::thread a(run, std::cref(first)), b(run, std::cref(second));
    a.join(); b.join();
    CHECK(failures.load() == 0);
    CHECK(UnsafeVm::Ok(f.Virtual(object, "get", "()I")).AsInt() == 200);
    f.vm.DiscardExecutionContext(first);
    f.vm.DiscardExecutionContext(second);
}

TEST_CASE("Unsafe boots real API19 AtomicInteger and AQS") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        UnsafeVm f(backend, true);
        const auto object = f.Allocate("Ljava/util/concurrent/atomic/AtomicInteger;");
        const auto type = f.model.ObjectClass(object);
        const auto index = f.linker.FindVtableIndex(type, "weakCompareAndSet", "(II)Z");
        REQUIRE(index.has_value());
        CHECK(f.linker.Method(f.linker.Class(type).vtable[*index]).kind == MethodKind::interpreted);
        CHECK(UnsafeVm::Ok(f.Virtual(object, "weakCompareAndSet", "(II)Z", {VmValue::Int(0), VmValue::Int(42)})).AsInt() == 1);
        CHECK(UnsafeVm::Ok(f.Virtual(object, "get", "()I")).AsInt() == 42);
        UnsafeVm::Ok(f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor("Ljava/util/concurrent/locks/AbstractQueuedSynchronizer;")));
        UnsafeVm::Ok(f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor("Ljava/util/concurrent/locks/LockSupport;")));
        for (const bool fair : {false, true}) {
            INFO("fair=", fair);
            const auto lock = f.Allocate("Ljava/util/concurrent/locks/ReentrantLock;");
            const auto roots = f.vm.ProtectReferences(std::array{lock});
            UnsafeVm::Ok(f.Static("Ljava/util/concurrent/locks/ReentrantLock;", "<init>", "(Z)V",
                                 {VmValue::Ref(lock), VmValue::Int(fair ? 1 : 0)}));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "isFair", "()Z")).AsInt() == (fair ? 1 : 0));
            const auto sync = f.linker.ResolveDescriptor(fair
                ? "Ljava/util/concurrent/locks/ReentrantLock$FairSync;"
                : "Ljava/util/concurrent/locks/ReentrantLock$NonfairSync;");
            CHECK(f.linker.Class(sync).is_boot_dex);
            const auto acquire = f.linker.FindVtableIndex(sync, "tryAcquire", "(I)Z");
            REQUIRE(acquire.has_value());
            CHECK(f.linker.Method(f.linker.Class(sync).vtable[*acquire]).kind == MethodKind::interpreted);
            UnsafeVm::Ok(f.Virtual(lock, "lock", "()V"));
            UnsafeVm::Ok(f.Virtual(lock, "lock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getHoldCount", "()I")).AsInt() == 2);
            UnsafeVm::Ok(f.Virtual(lock, "unlock", "()V"));
            UnsafeVm::Ok(f.Virtual(lock, "unlock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "isLocked", "()Z")).AsInt() == 0);
        }
        const auto thread = UnsafeVm::Ok(f.Static("Ljava/lang/Thread;", "currentThread",
                                                 "()Ljava/lang/Thread;")).ref;
        UnsafeVm::Ok(f.Static("Ljava/util/concurrent/locks/LockSupport;", "parkNanos",
            "(Ljava/lang/Object;J)V", {VmValue::Ref(object), VmValue::Long(1)}));
        CHECK(f.millis.load() == 1);
        CHECK_FALSE(UnsafeVm::Ok(f.Static("Ljava/util/concurrent/locks/LockSupport;", "getBlocker",
            "(Ljava/lang/Thread;)Ljava/lang/Object;", {VmValue::Ref(thread)})).ref.IsValid());
    }
}

TEST_CASE("API19 read write locks preserve reentrancy sharing and ThreadLocal holds") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool fair : {false, true}) {
            INFO("fair=", fair);
            UnsafeVm f(backend, true);
            constexpr auto descriptor = "Ljava/util/concurrent/locks/ReentrantReadWriteLock;";
            const auto lock = f.Allocate(descriptor);
            const auto lock_root = f.vm.ProtectReferences(std::array{lock});
            UnsafeVm::Ok(f.Static(descriptor, "<init>", "(Z)V", {VmValue::Ref(lock), VmValue::Int(fair ? 1 : 0)}));
            CHECK(f.linker.Class(f.model.ObjectClass(lock)).is_boot_dex);
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "isFair", "()Z")).AsInt() == (fair ? 1 : 0));
            const auto read = UnsafeVm::Ok(f.Virtual(lock, "readLock", "()Ljava/util/concurrent/locks/ReentrantReadWriteLock$ReadLock;")).ref;
            const auto write = UnsafeVm::Ok(f.Virtual(lock, "writeLock", "()Ljava/util/concurrent/locks/ReentrantReadWriteLock$WriteLock;")).ref;
            const auto roots = f.vm.ProtectReferences(std::array{read, write});
            const auto method = [&](VmObjectRef object, const char* name, const char* signature) {
                const auto type = f.model.ObjectClass(object);
                const auto index = f.linker.FindVtableIndex(type, name, signature);
                REQUIRE(index.has_value());
                const auto id = f.linker.Class(type).vtable[*index];
                CHECK(f.linker.Method(id).kind == MethodKind::interpreted);
                return id;
            };
            const auto read_try = method(read, "tryLock", "()Z");
            const auto read_unlock = method(read, "unlock", "()V");
            const auto write_try = method(write, "tryLock", "()Z");
            const auto hold_count = method(lock, "getReadHoldCount", "()I");
            UnsafeVm::Ok(f.Virtual(write, "lock", "()V"));
            UnsafeVm::Ok(f.Virtual(write, "lock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getWriteHoldCount", "()I")).AsInt() == 2);
            UnsafeVm::Ok(f.Virtual(read, "lock", "()V"));
            UnsafeVm::Ok(f.Virtual(write, "unlock", "()V"));
            UnsafeVm::Ok(f.Virtual(write, "unlock", "()V")); // downgrade leaves read ownership
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "isWriteLocked", "()Z")).AsInt() == 0);
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadHoldCount", "()I")).AsInt() == 1);
            CHECK(UnsafeVm::Ok(f.Virtual(write, "tryLock", "()Z")).AsInt() == 0); // no read-to-write upgrade
            UnsafeVm::Ok(f.Virtual(read, "lock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadHoldCount", "()I")).AsInt() == 2);
            UnsafeVm::Ok(f.Virtual(read, "unlock", "()V"));
            UnsafeVm::Ok(f.Virtual(read, "unlock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadLockCount", "()I")).AsInt() == 0);
            f.Throws(f.Virtual(read, "unlock", "()V"), "Ljava/lang/IllegalMonitorStateException;");
            f.Throws(f.Virtual(write, "unlock", "()V"), "Ljava/lang/IllegalMonitorStateException;");
            f.Throws(f.Virtual(read, "newCondition", "()Ljava/util/concurrent/locks/Condition;"), "Ljava/lang/UnsupportedOperationException;");

            const auto worker_context = f.vm.CreateExecutionContext();
            f.threads.RegisterNativeContext(worker_context, false);
            const auto current_thread = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/lang/Thread;"), "currentThread", "()Ljava/lang/Thread;");
            REQUIRE(current_thread.has_value());
            const auto main_thread = UnsafeVm::Ok(f.Static("Ljava/lang/Thread;", "currentThread", "()Ljava/lang/Thread;")).ref;
            const auto id_index = f.linker.FindVtableIndex(f.model.ObjectClass(main_thread), "getId", "()J");
            REQUIRE(id_index.has_value());
            const auto get_id = f.linker.Class(f.model.ObjectClass(main_thread)).vtable[*id_index];
            const auto main_id = UnsafeVm::Ok(f.Virtual(main_thread, "getId", "()J")).AsLong();
            std::mutex gate;
            std::condition_variable_any changed;
            bool ready{}, release{};
            std::string worker_error;
            std::array<int, 5> observations{};
            std::int64_t worker_id{};
            UnsafeVm::Ok(f.Virtual(read, "lock", "()V")); // firstReader fast path
            std::jthread worker([&](std::stop_token stop) {
                const auto call = [&](VmMethodId target, std::vector<VmValue> args) {
                    const auto outcome = f.vm.Call(worker_context, target, args);
                    if (outcome.exception.IsValid()) throw std::runtime_error(outcome.exception_message);
                    return outcome.value;
                };
                try {
                    const auto thread = call(*current_thread, {}).ref;
                    worker_id = call(get_id, {VmValue::Ref(thread)}).AsLong();
                    observations[0] = call(read_try, {VmValue::Ref(read)}).AsInt();
                    observations[1] = call(read_try, {VmValue::Ref(read)}).AsInt();
                    observations[2] = call(hold_count, {VmValue::Ref(lock)}).AsInt(); // ThreadLocalHoldCounter path
                    observations[3] = call(write_try, {VmValue::Ref(write)}).AsInt();
                    {
                        std::unique_lock guard(gate);
                        ready = true;
                        changed.notify_all();
                        changed.wait(guard, stop, [&] { return release; });
                    }
                    call(read_unlock, {VmValue::Ref(read)});
                    call(read_unlock, {VmValue::Ref(read)});
                    observations[4] = call(hold_count, {VmValue::Ref(lock)}).AsInt();
                } catch (const std::exception& error) {
                    std::lock_guard guard(gate);
                    worker_error = error.what();
                    ready = true;
                    changed.notify_all();
                }
            });
            {
                std::unique_lock guard(gate);
                REQUIRE(changed.wait_for(guard, std::chrono::seconds(5), [&] { return ready; }));
                REQUIRE_MESSAGE(worker_error.empty(), worker_error);
            }
            CHECK(worker_id != main_id);
            CHECK(observations[0] == 1);
            CHECK(observations[1] == 1);
            CHECK(observations[2] == 2);
            CHECK(observations[3] == 0);
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadLockCount", "()I")).AsInt() == 3);
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadHoldCount", "()I")).AsInt() == 1);
            CHECK(UnsafeVm::Ok(f.Virtual(write, "tryLock", "()Z")).AsInt() == 0);
            UnsafeVm::Ok(f.Virtual(read, "unlock", "()V"));
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadLockCount", "()I")).AsInt() == 2);
            {
                std::lock_guard guard(gate);
                release = true;
            }
            changed.notify_all();
            worker.join();
            REQUIRE_MESSAGE(worker_error.empty(), worker_error);
            CHECK(observations[4] == 0);
            CHECK(UnsafeVm::Ok(f.Virtual(lock, "getReadLockCount", "()I")).AsInt() == 0);
            CHECK(UnsafeVm::Ok(f.Virtual(write, "tryLock", "()Z")).AsInt() == 1);
            UnsafeVm::Ok(f.Virtual(write, "unlock", "()V"));
            f.threads.DetachNativeContext(worker_context.Token());
            f.vm.DiscardExecutionContext(worker_context);
        }
    }
}
