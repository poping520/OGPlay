#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

#include "bootdex_library_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {

struct ShutdownHookVm final {
    Dvm87Vm f;
    VmObjectRef runtime;
    explicit ShutdownHookVm(InterpreterBackend backend,
                            IntrinsicHandler run = [](IntrinsicContext&) { return VmValue::Void(); },
                            IntrinsicHandler uncaught = [](IntrinsicContext&) { return VmValue::Void(); })
        : f(backend, "en", "eng", "USA", "GMT", HookClass(std::move(run), std::move(uncaught))) {
        const auto result = f.Static("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;");
        f.RequireOk(result);
        runtime = result.value.ref;
    }
    static std::vector<IntrinsicClassDecl> HookClass(IntrinsicHandler run, IntrinsicHandler uncaught) {
        auto b = IntrinsicClassBuilder::Class("Ltest/ShutdownHook;", "Ljava/lang/Thread;");
        b.OverrideMethod("run", "()V", std::move(run));
        auto handler = IntrinsicClassBuilder::Class("Ltest/ShutdownExceptionHandler;", "Ljava/lang/Object;",
            {"Ljava/lang/Thread$UncaughtExceptionHandler;"});
        handler.VirtualMethod("uncaughtException", "(Ljava/lang/Thread;Ljava/lang/Throwable;)V", std::move(uncaught));
        return {std::move(b).Build(), std::move(handler).Build()};
    }
    VmObjectRef Hook() {
        auto ref = f.vm.NewIntrinsicInstance("Ltest/ShutdownHook;");
        auto root = f.vm.ProtectReferences(std::array{ref});
        f.Construct(ref, "Ljava/lang/Thread;", "()V");
        return ref;
    }
    VmCallOutcome Add(VmObjectRef hook) {
        return f.Virtual(runtime, "addShutdownHook", "(Ljava/lang/Thread;)V", {VmValue::Ref(hook)});
    }
    VmCallOutcome Remove(VmObjectRef hook) {
        return f.Virtual(runtime, "removeShutdownHook", "(Ljava/lang/Thread;)Z", {VmValue::Ref(hook)});
    }
    void Error(const VmCallOutcome& result, const char* expected) {
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == expected);
    }
};
constexpr std::array kShutdownBackends{InterpreterBackend::switch_dispatch,
                                       InterpreterBackend::threaded};

} // namespace

TEST_CASE("DVM-150 Runtime shutdown hook registration uses BootDex rules and GC roots") {
    for (const auto backend : kShutdownBackends) {
        ShutdownHookVm h(backend);
        const auto type = h.f.linker.ResolveDescriptor("Ljava/lang/Runtime;");
        for (const auto* name : {"addShutdownHook", "removeShutdownHook", "exit", "halt"}) {
            const auto signature = std::string_view(name) == "removeShutdownHook"
                ? "(Ljava/lang/Thread;)Z" : std::string_view(name) == "addShutdownHook"
                ? "(Ljava/lang/Thread;)V" : "(I)V";
            auto slot = h.f.linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value());
            CHECK(h.f.linker.Method(h.f.linker.Class(type).vtable[*slot]).kind == MethodKind::interpreted);
        }
        h.Error(h.Add(VmObjectRef{}), "Ljava/lang/NullPointerException;");
        h.Error(h.Remove(VmObjectRef{}), "Ljava/lang/NullPointerException;");
        h.Error(h.f.Static("Ljava/lang/Runtime;", "runFinalizersOnExit", "(Z)V", {VmValue::Int(1)}),
                "Ljava/lang/UnsupportedOperationException;");
        h.f.RequireOk(h.f.Static("Ljava/lang/Runtime;", "runFinalizersOnExit", "(Z)V", {VmValue::Int(0)}));
        const auto removed = h.Hook();
        h.f.RequireOk(h.Add(removed));
        CHECK(h.Remove(removed).value.AsInt() == 1);
        static_cast<void>(h.f.vm.CollectGarbage("removed-shutdown-hook"));
        CHECK_THROWS_AS(static_cast<void>(h.f.model.ObjectClass(removed)), DexVmError);
        const auto hook = h.Hook();
        CHECK(h.Remove(hook).value.AsInt() == 0);
        h.f.RequireOk(h.Add(hook));
        h.Error(h.Add(hook), "Ljava/lang/IllegalArgumentException;");
        static_cast<void>(h.f.vm.CollectGarbage("shutdown-hook-only-root"));
        // No host RootScope holds hook here: Runtime.shutdownHooks owns it.
        CHECK(h.f.model.ObjectClass(hook) == h.f.linker.ResolveDescriptor("Ltest/ShutdownHook;"));
        CHECK(h.Remove(hook).value.AsInt() == 1);
        CHECK(h.Remove(hook).value.AsInt() == 0);
        h.f.RequireOk(h.Add(hook));
        CHECK(h.Remove(hook).value.AsInt() == 1);
        h.f.RequireOk(h.f.Virtual(hook, "start", "()V"));
        h.Error(h.Add(hook), "Ljava/lang/IllegalArgumentException;");
        h.f.RequireOk(h.f.Virtual(hook, "join", "()V"));
        h.Error(h.Add(hook), "Ljava/lang/IllegalArgumentException;");
        CHECK_FALSE(h.f.threads.TakeFailure().has_value());
    }
}

TEST_CASE("DVM-150 Runtime exit starts every hook before join and rejects mutation") {
    for (const auto backend : kShutdownBackends) {
        std::mutex mutex;
        std::condition_variable changed;
        int started = 0, finished = 0;
        bool overlapped = true, rejected = true;
        ShutdownHookVm h(backend, [&](IntrinsicContext& c) {
            const auto get = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Ljava/lang/Runtime;"), "getRuntime", "()Ljava/lang/Runtime;");
            const auto runtime = c.vm.Call(*get, {}).value.ref;
            const auto invoke = [&](const char* name, const char* signature) {
                auto type = c.vm.Model().ObjectClass(runtime);
                auto slot = c.vm.Linker().FindVtableIndex(type, name, signature);
                return c.vm.Call(c.vm.Linker().Class(type).vtable[*slot],
                                std::array{VmValue::Ref(runtime), VmValue::Ref(c.receiver)});
            };
            for (const auto& result : {invoke("addShutdownHook", "(Ljava/lang/Thread;)V"),
                                       invoke("removeShutdownHook", "(Ljava/lang/Thread;)Z")}) {
                rejected = rejected && result.exception.IsValid() &&
                    c.vm.Linker().Class(result.exception_class).descriptor == "Ljava/lang/IllegalStateException;";
            }
            const auto depth = c.vm.ExecutionLock().ReleaseForBlocking();
            {
                std::unique_lock lock(mutex);
                ++started;
                changed.notify_all();
                overlapped = changed.wait_for(lock, std::chrono::seconds(3), [&] { return started == 2; }) && overlapped;
                ++finished;
            }
            c.vm.ExecutionLock().ReacquireAfterBlocking(depth);
            return VmValue::Void();
        });
        h.f.RequireOk(h.Add(h.Hook()));
        h.f.RequireOk(h.Add(h.Hook()));
        bool returned = false;
        try {
            static_cast<void>(h.f.Static("Ljava/lang/System;", "exit", "(I)V", {VmValue::Int(17)}));
            returned = true;
        } catch (const DexVmError& e) {
            CHECK(e.Reason() == DexVmErrorReason::thread_stopped);
        }
        h.f.threads.Shutdown();
        CHECK_FALSE(returned);
        CHECK(started == 2);
        CHECK(finished == 2);
        CHECK(overlapped);
        CHECK(rejected);
        CHECK(h.f.vm.ExitCode() == 17);
        CHECK_FALSE(h.f.threads.TakeFailure().has_value());
        CHECK_THROWS_AS(static_cast<void>(h.f.Static("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;")), DexVmError);
    }
}

TEST_CASE("DVM-150 Runtime halt skips hooks and exit state is per VM") {
    for (const auto backend : kShutdownBackends) {
        int calls = 0;
        ShutdownHookVm h(backend, [&](IntrinsicContext&) { ++calls; return VmValue::Void(); });
        h.f.RequireOk(h.Add(h.Hook()));
        CHECK_THROWS_AS(static_cast<void>(h.f.Virtual(h.runtime, "halt", "(I)V", {VmValue::Int(-9)})), DexVmError);
        CHECK(h.f.vm.ExitCode() == -9);
        CHECK(calls == 0);
        ShutdownHookVm other(backend);
        CHECK_FALSE(other.f.vm.ExitCode().has_value());
        other.f.RequireOk(other.Add(other.Hook()));
    }
}

TEST_CASE("DVM-150 Runtime exit from a worker never joins itself") {
    for (const auto backend : kShutdownBackends) {
        bool returned = false;
        ShutdownHookVm h(backend, [&](IntrinsicContext& c) {
            auto method = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Ljava/lang/System;"), "exit", "(I)V");
            static_cast<void>(c.vm.Call(*method, std::array{VmValue::Int(23)}));
            returned = true;
            return VmValue::Void();
        });
        const auto worker = h.Hook();
        h.f.RequireOk(h.f.Virtual(worker, "start", "()V"));
        h.f.threads.Join(worker);
        h.f.threads.Shutdown();
        CHECK(h.f.vm.ExitCode() == 23);
        CHECK_FALSE(returned);
        CHECK_FALSE(h.f.threads.TakeFailure().has_value());
    }
}

TEST_CASE("DVM-150 LogManager shutdown hook closes a real registered Handler") {
    for (const auto backend : kShutdownBackends) {
        int closed = 0;
        auto handler = IntrinsicClassBuilder::Class("Ltest/ShutdownLogHandler;", "Ljava/util/logging/Handler;");
        handler.OverrideMethod("publish", "(Ljava/util/logging/LogRecord;)V", [](IntrinsicContext&) { return VmValue::Void(); });
        handler.OverrideMethod("flush", "()V", [](IntrinsicContext&) { return VmValue::Void(); });
        handler.OverrideMethod("close", "()V", [&](IntrinsicContext&) { ++closed; return VmValue::Void(); });
        Dvm87Vm f(backend, "en", "eng", "USA", "GMT", {std::move(handler).Build()});
        const auto logger = f.Static("Ljava/util/logging/Logger;", "getLogger",
            "(Ljava/lang/String;)Ljava/util/logging/Logger;", {VmValue::Ref(f.vm.NewStringUtf8("shutdown-test"))});
        f.RequireOk(logger);
        const auto logger_root = f.vm.ProtectReferences(std::array{logger.value.ref});
        const auto ref = f.vm.NewIntrinsicInstance("Ltest/ShutdownLogHandler;");
        const auto handler_root = f.vm.ProtectReferences(std::array{ref});
        f.Construct(ref, "Ljava/util/logging/Handler;", "()V");
        f.RequireOk(f.Virtual(logger.value.ref, "addHandler", "(Ljava/util/logging/Handler;)V", {VmValue::Ref(ref)}));
        CHECK(closed == 0);
        CHECK_THROWS_AS(static_cast<void>(f.Static("Ljava/lang/System;", "exit", "(I)V", {VmValue::Int(0)})), DexVmError);
        f.threads.Shutdown();
        CHECK(f.vm.ExitCode() == 0);
        CHECK(closed == 1);
        CHECK_FALSE(f.threads.TakeFailure().has_value());
    }
}

TEST_CASE("DVM-150 halt inside a hook aborts the outer exit without self join") {
    for (const auto backend : kShutdownBackends) {
        ShutdownHookVm h(backend, [](IntrinsicContext& c) -> VmValue {
            c.vm.Exit(41);
        });
        h.f.RequireOk(h.Add(h.Hook()));
        CHECK_THROWS_AS(static_cast<void>(h.f.Static("Ljava/lang/System;", "exit", "(I)V", {VmValue::Int(0)})), DexVmError);
        h.f.threads.Shutdown();
        CHECK(h.f.vm.ExitCode() == 41);
        CHECK_FALSE(h.f.threads.TakeFailure().has_value());
    }
}

TEST_CASE("DVM-150 shutdown hook exceptions use the Thread uncaught handler") {
    for (const auto backend : kShutdownBackends) {
        int runs = 0, caught = 0;
        std::string exception;
        ShutdownHookVm h(backend, [&](IntrinsicContext&) -> VmValue {
            if (++runs == 1) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "hook failed"};
            return VmValue::Void();
        }, [&](IntrinsicContext& c) {
            ++caught;
            exception = c.vm.Linker().Class(c.vm.Model().ObjectClass(c.arguments[1].ref)).descriptor;
            return VmValue::Void();
        });
        auto handler = h.f.vm.NewIntrinsicInstance("Ltest/ShutdownExceptionHandler;");
        h.f.RequireOk(h.f.Static("Ljava/lang/Thread;", "setDefaultUncaughtExceptionHandler",
            "(Ljava/lang/Thread$UncaughtExceptionHandler;)V", {VmValue::Ref(handler)}));
        h.f.RequireOk(h.Add(h.Hook()));
        h.f.RequireOk(h.Add(h.Hook()));
        CHECK_THROWS_AS(static_cast<void>(h.f.Static("Ljava/lang/System;", "exit", "(I)V", {VmValue::Int(0)})), DexVmError);
        h.f.threads.Shutdown();
        CHECK(h.f.vm.ExitCode() == 0);
        CHECK(runs == 2);
        CHECK(caught == 1);
        CHECK(exception == "Ljava/lang/IllegalStateException;");
        CHECK_FALSE(h.f.threads.TakeFailure().has_value());
    }
}
