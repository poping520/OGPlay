// DVM-48 java.lang.Thread integration tests. Observable behavior is mapped
// to pinned libcore Thread.java/VMThread.java and Dalvik Thread.cpp/Sync.cpp.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct ThreadVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;
    VmThreadRuntime threads;
    std::atomic<std::int64_t> clock_millis{};

    ThreadVm()
        : model(strings, arrays),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}),
          threads(interpreter) {
        interpreter.Monitors().SetTimeSource(
            [this] { return clock_millis.load(); });
    }

    [[nodiscard]] DexClassId Class(const std::string& descriptor) {
        const auto java_class = linker.FindClass(descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), descriptor);
        return *java_class;
    }

    [[nodiscard]] VmCallOutcome Static(
        const std::string& owner, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto method =
            linker.FindDirectMethod(Class(owner), name, descriptor);
        const auto qualified = owner + "." + name;
        REQUIRE_MESSAGE(method.has_value(), qualified);
        return interpreter.Call(*method, arguments);
    }

    [[nodiscard]] VmCallOutcome Virtual(
        const VmObjectRef receiver, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index =
            linker.FindVtableIndex(receiver_class, name, descriptor);
        const auto signature = name + descriptor;
        REQUIRE_MESSAGE(index.has_value(), signature);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linker.Class(receiver_class).vtable[*index],
                                arguments);
    }

    [[nodiscard]] VmObjectRef New(const std::string& descriptor) {
        return interpreter.NewIntrinsicInstance(descriptor);
    }

    [[nodiscard]] VmCallOutcome Construct(
        const VmObjectRef object, const std::string& owner,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        const auto constructor =
            linker.FindDirectMethod(Class(owner), "<init>", descriptor);
        const auto signature = owner + descriptor;
        REQUIRE_MESSAGE(constructor.has_value(), signature);
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        return interpreter.Call(*constructor, arguments);
    }

    [[nodiscard]] VmObjectRef Runnable(const std::string& owner) {
        const auto outcome = Static(owner, "make", "()Ljava/lang/Runnable;");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.ref;
    }

    [[nodiscard]] std::int32_t Observed(const std::string& owner) {
        const auto outcome = Static(owner, "observed", "()I");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }
};

void RequireOk(const VmCallOutcome& outcome) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
}

void RequireThrow(const ThreadVm& vm, const VmCallOutcome& outcome,
                  const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("dexvm Thread constructors allocate stable per-VM identity") {
    ThreadVm vm;
    const auto root_first =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;");
    const auto root_second =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;");
    RequireOk(root_first);
    RequireOk(root_second);
    CHECK(root_first.value.ref == root_second.value.ref);
    CHECK(vm.Virtual(root_first.value.ref, "getId", "()J").value.AsLong() ==
          1);
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(root_first.value.ref, "getName",
                         "()Ljava/lang/String;")
                  .value.ref) == "main");
    CHECK(vm.Virtual(root_first.value.ref, "getPriority", "()I")
              .value.AsInt() == 5);
    CHECK(vm.Virtual(root_first.value.ref, "isDaemon", "()Z")
              .value.AsInt() == 0);
    RequireOk(vm.Virtual(root_first.value.ref, "setPriority", "(I)V",
                         {VmValue::Int(7)}));

    const auto runnable = vm.Runnable("LThreadProbe;");
    const auto plain = vm.New("Ljava/lang/Thread;");
    const auto with_target = vm.New("Ljava/lang/Thread;");
    const auto with_name = vm.New("Ljava/lang/Thread;");
    const auto with_both = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(plain, "Ljava/lang/Thread;", "()V"));
    RequireOk(vm.Construct(with_target, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(runnable)}));
    const auto named = vm.interpreter.NewStringUtf8("worker");
    RequireOk(vm.Construct(with_name, "Ljava/lang/Thread;",
                           "(Ljava/lang/String;)V",
                           {VmValue::Ref(named)}));
    RequireOk(vm.Construct(with_both, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;Ljava/lang/String;)V",
                           {VmValue::Ref(runnable), VmValue::Ref(named)}));

    const auto id_plain = vm.Virtual(plain, "getId", "()J").value.AsLong();
    const auto id_target =
        vm.Virtual(with_target, "getId", "()J").value.AsLong();
    CHECK(id_plain > 1);
    CHECK(id_target > id_plain);
    CHECK(vm.Virtual(plain, "getPriority", "()I").value.AsInt() == 7);
    CHECK(vm.Virtual(plain, "getId", "()J").value.AsLong() == id_plain);
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(plain, "getName", "()Ljava/lang/String;").value.ref) ==
          "Thread-" + std::to_string(id_plain));
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(with_name, "getName", "()Ljava/lang/String;")
                  .value.ref) == "worker");

    const auto bad = vm.New("Ljava/lang/Thread;");
    RequireThrow(vm,
                 vm.Construct(bad, "Ljava/lang/Thread;",
                              "(Ljava/lang/String;)V",
                              {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm Thread.start virtual-dispatches this.run and currentThread") {
    ThreadVm vm;
    const auto target = vm.Runnable("LThreadProbe;");
    const auto overridden = vm.New("LThreadOverride;");
    RequireOk(vm.Construct(overridden, "LThreadOverride;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    RequireOk(vm.Virtual(overridden, "start", "()V"));
    RequireOk(vm.Virtual(overridden, "join", "()V"));
    CHECK(vm.Observed("LThreadOverride;") == 1);
    const auto target_counter =
        vm.Static("LThreadProbe;", "counter", "()I");
    RequireOk(target_counter);
    CHECK(target_counter.value.AsInt() == 0);
    RequireThrow(vm, vm.Virtual(overridden, "start", "()V"),
                 "Ljava/lang/IllegalThreadStateException;");

    const auto base = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(base, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    const auto stable_id = vm.Virtual(base, "getId", "()J").value.AsLong();
    RequireOk(vm.Virtual(base, "start", "()V"));
    RequireOk(vm.Virtual(base, "join", "()V"));
    CHECK(vm.Static("LThreadProbe;", "counter", "()I").value.AsInt() ==
          4950);
    CHECK(vm.Virtual(base, "getId", "()J").value.AsLong() == stable_id);
    RequireOk(vm.Virtual(base, "interrupt", "()V"));
    CHECK(vm.Virtual(base, "isInterrupted", "()Z").value.AsInt() == 0);

    const auto identity = vm.New("LThreadIdentity;");
    RequireOk(vm.Construct(identity, "LThreadIdentity;", "()V"));
    RequireOk(vm.Virtual(identity, "start", "()V"));
    RequireOk(vm.Virtual(identity, "join", "()V"));
    CHECK(vm.Observed("LThreadIdentity;") == 1);
    CHECK(vm.Virtual(identity, "isAlive", "()Z").value.AsInt() == 0);
}

TEST_CASE("dexvm Thread name priority daemon and holdsLock are Java facts") {
    ThreadVm vm;
    const auto thread = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(thread, "Ljava/lang/Thread;", "()V"));
    RequireOk(vm.Virtual(thread, "setPriority", "(I)V", {VmValue::Int(7)}));
    CHECK(vm.Virtual(thread, "getPriority", "()I").value.AsInt() == 7);
    RequireThrow(vm,
                 vm.Virtual(thread, "setPriority", "(I)V",
                            {VmValue::Int(11)}),
                 "Ljava/lang/IllegalArgumentException;");
    RequireOk(vm.Virtual(thread, "setDaemon", "(Z)V", {VmValue::Int(1)}));
    CHECK(vm.Virtual(thread, "isDaemon", "()Z").value.AsInt() == 1);

    const auto renamed = vm.interpreter.NewStringUtf8("renamed");
    RequireOk(vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                         {VmValue::Ref(renamed)}));
    CHECK(vm.interpreter.StringUtf8(
              vm.Virtual(thread, "getName", "()Ljava/lang/String;").value.ref) ==
          "renamed");
    RequireThrow(vm,
                 vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                            {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");

    const auto lock = vm.New("Ljava/lang/Object;");
    const auto outside = vm.Static("Ljava/lang/Thread;", "holdsLock",
                                   "(Ljava/lang/Object;)Z",
                                   {VmValue::Ref(lock)});
    RequireOk(outside);
    CHECK(outside.value.AsInt() == 0);
    vm.interpreter.Monitors().Enter(lock,
                                    vm.interpreter.CurrentContextToken());
    const auto inside = vm.Static("Ljava/lang/Thread;", "holdsLock",
                                  "(Ljava/lang/Object;)Z",
                                  {VmValue::Ref(lock)});
    RequireOk(inside);
    CHECK(inside.value.AsInt() == 1);
    vm.interpreter.Monitors().Exit(lock,
                                   vm.interpreter.CurrentContextToken());
    vm.interpreter.Monitors().Enter(lock, 2U);
    const auto owned_by_other =
        vm.Static("Ljava/lang/Thread;", "holdsLock",
                  "(Ljava/lang/Object;)Z", {VmValue::Ref(lock)});
    RequireOk(owned_by_other);
    CHECK(owned_by_other.value.AsInt() == 0);
    vm.interpreter.Monitors().Exit(lock, 2U);
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "holdsLock",
                           "(Ljava/lang/Object;)Z",
                           {VmValue::Ref(VmObjectRef{})}),
                 "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm Thread interrupt and sleep share one runtime flag and clock") {
    ThreadVm vm;
    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    RequireOk(vm.Virtual(root, "interrupt", "()V"));
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 1);
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 1);
    CHECK(vm.Static("Ljava/lang/Thread;", "interrupted", "()Z")
              .value.AsInt() == 1);
    CHECK(vm.Static("Ljava/lang/Thread;", "interrupted", "()Z")
              .value.AsInt() == 0);

    RequireOk(vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                        {VmValue::Long(0), VmValue::Int(0)}));
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                           {VmValue::Long(-1), VmValue::Int(0)}),
                 "Ljava/lang/IllegalArgumentException;");
    RequireThrow(vm,
                 vm.Static("Ljava/lang/Thread;", "sleep", "(JI)V",
                           {VmValue::Long(0), VmValue::Int(1'000'000)}),
                 "Ljava/lang/IllegalArgumentException;");

    const auto sleeper = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(sleeper, "LThreadSleeper;", "()V"));
    RequireOk(vm.Virtual(sleeper, "start", "()V"));
    REQUIRE(WaitFor([&] { return vm.threads.IsAlive(sleeper); }));
    RequireOk(vm.Virtual(sleeper, "interrupt", "()V"));
    RequireOk(vm.Virtual(sleeper, "join", "()V"));
    CHECK(vm.Observed("LThreadSleeper;") == 2);
    CHECK(vm.Virtual(sleeper, "isInterrupted", "()Z").value.AsInt() == 0);
}

TEST_CASE("dexvm timed join returns at the injected monotonic deadline") {
    ThreadVm vm;
    const auto target = vm.New("LThreadSleeper;");
    RequireOk(vm.Construct(target, "LThreadSleeper;", "()V"));
    const auto thread = vm.New("Ljava/lang/Thread;");
    RequireOk(vm.Construct(thread, "Ljava/lang/Thread;",
                           "(Ljava/lang/Runnable;)V",
                           {VmValue::Ref(target)}));
    RequireOk(vm.Virtual(thread, "start", "()V"));
    REQUIRE(WaitFor([&] { return vm.threads.IsAlive(thread); }));

    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    RequireOk(vm.Virtual(root, "interrupt", "()V"));
    const auto interrupted_join = vm.Virtual(thread, "join", "()V");
    RequireThrow(vm, interrupted_join, "Ljava/lang/InterruptedException;");
    CHECK(vm.Virtual(root, "isInterrupted", "()Z").value.AsInt() == 0);

    const auto renamed = vm.interpreter.NewStringUtf8("live-spin");
    RequireOk(vm.Virtual(thread, "setName", "(Ljava/lang/String;)V",
                         {VmValue::Ref(renamed)}));
    bool saw_rename = false;
    for (const auto& entry : vm.threads.Snapshot()) {
        if (entry.object == thread.Value() && entry.name == "live-spin") {
            saw_rename = true;
        }
    }
    CHECK(saw_rename);
    RequireThrow(vm,
                 vm.Virtual(thread, "setDaemon", "(Z)V",
                            {VmValue::Int(1)}),
                 "Ljava/lang/IllegalThreadStateException;");

    const auto active_mark = vm.interpreter.MarkReachable();
    CHECK(active_mark.IsMarked(root));
    CHECK(active_mark.IsMarked(thread));
    CHECK(active_mark.IsMarked(target));

    std::thread clock_driver([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(49);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(50);
    });
    RequireOk(vm.Virtual(thread, "join", "(JI)V",
                         {VmValue::Long(49), VmValue::Int(1)}));
    clock_driver.join();
    CHECK(vm.threads.IsAlive(thread));
    vm.threads.Shutdown();
    const auto finished_mark = vm.interpreter.MarkReachable();
    CHECK(finished_mark.IsMarked(root));
    CHECK_FALSE(finished_mark.IsMarked(thread));
}

TEST_CASE("dexvm timed self join follows the Clock instead of throwing") {
    ThreadVm vm;
    const auto root =
        vm.Static("Ljava/lang/Thread;", "currentThread",
                  "()Ljava/lang/Thread;")
            .value.ref;
    std::thread clock_driver([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        vm.clock_millis.store(1);
    });
    RequireOk(vm.Virtual(root, "join", "(J)V", {VmValue::Long(1)}));
    clock_driver.join();
    CHECK(vm.Virtual(root, "isAlive", "()Z").value.AsInt() == 1);
}
