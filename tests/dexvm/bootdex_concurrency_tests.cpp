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
#include "android_value_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {

struct ScheduledVm final {
    struct State {
        std::atomic<std::int64_t> now{0};
        std::vector<std::pair<int, std::int64_t>> calls;  // Guarded by VM execution lock.
        bool slow{}, fail{}, block{};
        std::atomic<bool> interrupted{false};
        VmObjectRef failure;
        std::uint64_t thread{};
    };

    std::shared_ptr<State> state{std::make_shared<State>()};
    Dvm87Vm f;
    std::vector<Interpreter::RootScope> roots;
    VmObjectRef unit;

    static std::vector<IntrinsicClassDecl> Tasks(const std::shared_ptr<State>& s) {
        auto b = IntrinsicClassBuilder::Class("Ltest/ScheduledTask;", "Ljava/lang/Object;",
                                              {"Ljava/lang/Runnable;", "Ljava/util/concurrent/Callable;"});
        const auto id = b.BoundInstanceField("id", "I");
        const auto run = [s, id](IntrinsicContext& c) {
            s->calls.emplace_back(IntrinsicCall(c).GetInt(id), s->now.load());
            s->thread = c.vm.Threads().ThreadId(c.vm.Threads().CurrentThreadObject());
            if (s->slow) s->now.fetch_add(3);  // Work duration measured only by the injected Clock.
            if (s->block) {
                try {
                    c.vm.Threads().Sleep(1000);
                } catch (const VmJavaThrow& e) {
                    if (e.descriptor != "Ljava/lang/InterruptedException;") throw;
                    s->interrupted = true;
                }
            }
            if (s->fail) {
                s->failure = c.vm.MakeThrowable("Ljava/lang/IllegalStateException;", "scheduled failure");
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "scheduled failure", s->failure};
            }
        };
        b.VirtualMethod("run", "()V", [run](IntrinsicContext& c) {
            run(c);
            return VmValue::Void();
        });
        b.VirtualMethod("call", "()Ljava/lang/Object;", [run](IntrinsicContext& c) {
            run(c);
            return VmValue::Ref(c.vm.NewStringUtf8("scheduled result"));
        });
        return {std::move(b).Build()};
    }

    explicit ScheduledVm(InterpreterBackend backend) : f(backend, "en", "US", "GMT", Tasks(state)) {
        f.vm.Monitors().SetTimeSource([s = state] { return s->now.load(); });
        const auto value = f.Static("Ljava/util/concurrent/TimeUnit;", "valueOf",
                                    "(Ljava/lang/String;)Ljava/util/concurrent/TimeUnit;",
                                    {VmValue::Ref(f.vm.NewStringUtf8("MILLISECONDS"))});
        f.RequireOk(value);
        unit = value.value.ref;
    }

    ~ScheduledVm() { f.threads.Shutdown(); }

    VmObjectRef Keep(VmObjectRef ref) {
        roots.push_back(f.vm.ProtectReferences(std::array{ref}));
        return ref;
    }

    VmValue Call(VmObjectRef obj, const char* name, const char* signature, std::vector<VmValue> args = {}) {
        VmExecutionLockScope lock(f.vm.ExecutionLock());
        const auto result = f.Virtual(obj, name, signature, std::move(args));
        f.RequireOk(result);
        return result.value;
    }

    VmObjectRef Exception(VmObjectRef obj, const char* name, const char* signature, const char* expected,
                          std::vector<VmValue> args = {}) {
        VmExecutionLockScope lock(f.vm.ExecutionLock());
        const auto result = f.Virtual(obj, name, signature, std::move(args));
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == expected);
        return Keep(result.exception);
    }

    VmObjectRef Pool(const char* factory = nullptr) {
        VmExecutionLockScope lock(f.vm.ExecutionLock());
        if (factory) {
            const bool single = std::string_view(factory) == "newSingleThreadScheduledExecutor";
            const auto result = f.Static("Ljava/util/concurrent/Executors;", factory,
                                         single ? "()Ljava/util/concurrent/ScheduledExecutorService;"
                                                : "(I)Ljava/util/concurrent/ScheduledExecutorService;",
                                         single ? std::vector<VmValue>{} : std::vector{VmValue::Int(1)});
            f.RequireOk(result);
            return Keep(result.value.ref);
        }
        const auto pool = Keep(f.vm.NewIntrinsicInstance("Ljava/util/concurrent/ScheduledThreadPoolExecutor;"));
        f.Construct(pool, "Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "(I)V", {VmValue::Int(1)});
        return pool;
    }

    VmObjectRef Task(int id = 1) {
        VmExecutionLockScope lock(f.vm.ExecutionLock());
        const auto task = f.vm.NewIntrinsicInstance("Ltest/ScheduledTask;");
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(task), "id", "I");
        REQUIRE(field);
        f.model.InstanceSlots(task)[f.linker.Field(*field).slot] = {static_cast<std::uint32_t>(id), SlotTag::cat1};
        return task;
    }

    VmObjectRef Schedule(VmObjectRef pool, VmObjectRef task, std::int64_t delay, bool callable = false) {
        VmExecutionLockScope lock(f.vm.ExecutionLock());
        return Keep(
            Call(
                pool, "schedule",
                callable
                    ? "(Ljava/util/concurrent/Callable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;"
                    : "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                {VmValue::Ref(task), VmValue::Long(delay), VmValue::Ref(unit)})
                .ref);
    }

    template <class Predicate>
    void Wait(Predicate predicate) {
        // Wall time bounds a broken test; only state.now advances guest deadlines.
        for (int i = 0; i < 3000; ++i) {
            {
                VmExecutionLockScope lock(f.vm.ExecutionLock());
                if (predicate()) return;
            }
            if (auto error = f.threads.TakeFailure()) FAIL(error.value());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        FAIL("scheduled worker did not reach the expected state");
    }

    void Stop(VmObjectRef pool) {
        Call(pool, "shutdown", "()V");
        Wait([&] { return Call(pool, "isTerminated", "()Z").AsInt() != 0; });
        CHECK_FALSE(f.threads.TakeFailure().has_value());
    }
};

}  // namespace

TEST_CASE("DVM-110 scheduled one-shot tasks use Clock FIFO factories and GC roots") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const char* factory :
             std::array<const char*, 3>{nullptr, "newScheduledThreadPool", "newSingleThreadScheduledExecutor"}) {
            ScheduledVm s(backend);
            const auto pool = s.Pool(factory);
            const auto task = s.Task(1);
            const auto first = s.Schedule(pool, task, 10);
            const auto second = s.Schedule(pool, s.Task(2), 10, true);
            s.Wait([&] { return s.f.threads.LiveCount() == 1; });
            CHECK(s.Call(first, "getDelay", "(Ljava/util/concurrent/TimeUnit;)J", {VmValue::Ref(s.unit)}).AsLong() ==
                  10);
            CHECK(s.Call(first, "isDone", "()Z").AsInt() == 0);
            while (s.roots.size() > 1)
                s.roots.pop_back();  // Only the pool is rooted: queue -> future -> runnable must survive.
            static_cast<void>(s.f.vm.CollectGarbage("scheduled-queued"));
            {
                VmExecutionLockScope lock(s.f.vm.ExecutionLock());
                CHECK(s.f.model.ObjectClass(task).IsValid());
                CHECK(s.state->calls.empty());
            }
            s.Keep(first);
            s.Keep(second);
            s.state->now = 9;
            CHECK(s.Call(first, "getDelay", "(Ljava/util/concurrent/TimeUnit;)J", {VmValue::Ref(s.unit)}).AsLong() ==
                  1);
            CHECK(s.Call(first, "isDone", "()Z").AsInt() == 0);
            s.state->now = 10;
            s.Wait([&] { return s.Call(second, "isDone", "()Z").AsInt() != 0; });
            CHECK_FALSE(s.Call(first, "get", "()Ljava/lang/Object;").ref.IsValid());
            CHECK(s.f.vm.StringUtf8(s.Call(second, "get", "()Ljava/lang/Object;").ref) == "scheduled result");
            {
                VmExecutionLockScope lock(s.f.vm.ExecutionLock());
                REQUIRE(s.state->calls.size() == 2);
                CHECK(s.state->calls[0] == std::pair{1, INT64_C(10)});
                CHECK(s.state->calls[1] == std::pair{2, INT64_C(10)});
                CHECK(s.state->thread > 1);
            }
            s.Stop(pool);
        }
    }
}

TEST_CASE("DVM-110 fixed rate and fixed delay use distinct deadlines and stop after cancellation") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool fixed_rate : {false, true}) {
            ScheduledVm s(backend);
            s.state->slow = true;
            const auto pool = s.Pool();
            const auto future = s.Keep(
                s.Call(pool, fixed_rate ? "scheduleAtFixedRate" : "scheduleWithFixedDelay",
                       "(Ljava/lang/Runnable;JJLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                       {VmValue::Ref(s.Task()), VmValue::Long(10), VmValue::Long(10), VmValue::Ref(s.unit)})
                    .ref);
            const auto queue = s.Call(pool, "getQueue", "()Ljava/util/concurrent/BlockingQueue;").ref;
            s.state->now = 10;
            s.Wait([&] { return s.state->calls.size() == 1 && s.Call(queue, "size", "()I").AsInt() == 1; });
            CHECK(s.Call(future, "getDelay", "(Ljava/util/concurrent/TimeUnit;)J", {VmValue::Ref(s.unit)}).AsLong() ==
                  (fixed_rate ? 7 : 10));
            CHECK(s.Call(future, "isDone", "()Z").AsInt() == 0);
            s.state->now = fixed_rate ? 20 : 23;
            s.Wait([&] { return s.state->calls.size() == 2 && s.Call(queue, "size", "()I").AsInt() == 1; });
            CHECK(s.Call(future, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 1);
            s.state->now = 100;
            s.Stop(pool);
            CHECK(s.state->calls.size() == 2);
            CHECK(s.Call(future, "isCancelled", "()Z").AsInt() == 1);
        }
    }
}

TEST_CASE("DVM-110 cancellation timeout rejection and periodic failure preserve Future semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ScheduledVm s(backend);
        const auto pool = s.Pool();
        const auto queue = s.Call(pool, "getQueue", "()Ljava/util/concurrent/BlockingQueue;").ref;
        const auto future = s.Schedule(pool, s.Task(), 100);
        s.Exception(future, "get", "(JLjava/util/concurrent/TimeUnit;)Ljava/lang/Object;",
                    "Ljava/util/concurrent/TimeoutException;", {VmValue::Long(0), VmValue::Ref(s.unit)});
        CHECK(s.Call(future, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 1);
        CHECK(s.Call(future, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 0);
        s.Exception(future, "get", "()Ljava/lang/Object;", "Ljava/util/concurrent/CancellationException;");
        CHECK(s.Call(queue, "size", "()I").AsInt() == 1);
        s.Call(pool, "purge", "()V");
        CHECK(s.Call(queue, "size", "()I").AsInt() == 0);
        s.Call(pool, "setRemoveOnCancelPolicy", "(Z)V", {VmValue::Int(1)});
        const auto removed = s.Schedule(pool, s.Task(), 100);
        s.Call(removed, "cancel", "(Z)Z", {VmValue::Int(0)});
        CHECK(s.Call(queue, "size", "()I").AsInt() == 0);
        const auto signature =
            "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;";
        s.Exception(pool, "schedule", signature, "Ljava/lang/NullPointerException;",
                    {VmValue::Ref(VmObjectRef{}), VmValue::Long(0), VmValue::Ref(s.unit)});
        s.Exception(pool, "schedule", signature, "Ljava/lang/NullPointerException;",
                    {VmValue::Ref(s.Task()), VmValue::Long(0), VmValue::Ref(VmObjectRef{})});
        for (const auto* method : {"scheduleAtFixedRate", "scheduleWithFixedDelay"}) {
            s.Exception(pool, method,
                        "(Ljava/lang/Runnable;JJLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                        "Ljava/lang/IllegalArgumentException;",
                        {VmValue::Ref(s.Task()), VmValue::Long(0), VmValue::Long(0), VmValue::Ref(s.unit)});
        }
        {
            VmExecutionLockScope lock(s.f.vm.ExecutionLock());
            s.state->fail = true;
        }
        const auto failed = s.Keep(
            s.Call(pool, "scheduleAtFixedRate",
                   "(Ljava/lang/Runnable;JJLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                   {VmValue::Ref(s.Task()), VmValue::Long(-1), VmValue::Long(10), VmValue::Ref(s.unit)})
                .ref);
        s.Wait([&] { return s.Call(failed, "isDone", "()Z").AsInt() != 0; });
        const auto error =
            s.Exception(failed, "get", "()Ljava/lang/Object;", "Ljava/util/concurrent/ExecutionException;");
        static_cast<void>(s.f.vm.CollectGarbage("scheduled-failure"));
        CHECK(s.Call(error, "getCause", "()Ljava/lang/Throwable;").ref == s.state->failure);
        s.state->now = 100;
        s.Stop(pool);
        CHECK(s.state->calls.size() == 1);
        s.Exception(pool, "schedule", signature, "Ljava/util/concurrent/RejectedExecutionException;",
                    {VmValue::Ref(s.Task()), VmValue::Long(0), VmValue::Ref(s.unit)});
    }
}

TEST_CASE("DVM-110 shutdown honors delayed and periodic continuation policies") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool periodic : {false, true})
            for (const bool delayed : {false, true}) {
                ScheduledVm s(backend);
                const auto pool = s.Pool();
                CHECK(s.Call(pool, "getContinueExistingPeriodicTasksAfterShutdownPolicy", "()Z").AsInt() == 0);
                CHECK(s.Call(pool, "getExecuteExistingDelayedTasksAfterShutdownPolicy", "()Z").AsInt() == 1);
                s.Call(pool, "setContinueExistingPeriodicTasksAfterShutdownPolicy", "(Z)V", {VmValue::Int(periodic)});
                s.Call(pool, "setExecuteExistingDelayedTasksAfterShutdownPolicy", "(Z)V", {VmValue::Int(delayed)});
                const auto once = s.Schedule(pool, s.Task(1), 10);
                const auto repeating = s.Keep(
                    s.Call(
                         pool, "scheduleAtFixedRate",
                         "(Ljava/lang/Runnable;JJLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                         {VmValue::Ref(s.Task(2)), VmValue::Long(10), VmValue::Long(10), VmValue::Ref(s.unit)})
                        .ref);
                s.Call(pool, "shutdown", "()V");
                CHECK((s.Call(once, "isCancelled", "()Z").AsInt() != 0) == !delayed);
                CHECK((s.Call(repeating, "isCancelled", "()Z").AsInt() != 0) == !periodic);
                s.state->now = 10;
                if (delayed) s.Wait([&] { return s.Call(once, "isDone", "()Z").AsInt() != 0; });
                if (periodic) {
                    const auto queue = s.Call(pool, "getQueue", "()Ljava/util/concurrent/BlockingQueue;").ref;
                    s.Wait([&] {
                        return s.state->calls.size() == std::size_t(delayed) + 1 &&
                               s.Call(queue, "size", "()I").AsInt() == 1;
                    });
                    CHECK(s.Call(repeating, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 1);
                }
                s.Stop(pool);
                CHECK(s.state->calls.size() == std::size_t(periodic) + std::size_t(delayed));
                CHECK(s.Call(pool, "awaitTermination", "(JLjava/util/concurrent/TimeUnit;)Z",
                             {VmValue::Long(0), VmValue::Ref(s.unit)})
                          .AsInt() == 1);
            }
    }
}

TEST_CASE("DVM-110 cancelling running work and shutdownNow interrupt real workers") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const bool shutdown_now : {false, true}) {
            ScheduledVm s(backend);
            s.state->block = true;
            const auto pool = s.Pool();
            const auto running = s.Schedule(pool, s.Task(), 0);
            const auto queued = s.Schedule(pool, s.Task(2), 100);
            s.Wait([&] { return s.state->calls.size() == 1; });
            if (shutdown_now) {
                const auto pending = s.Keep(s.Call(pool, "shutdownNow", "()Ljava/util/List;").ref);
                CHECK(s.Call(pending, "size", "()I").AsInt() == 1);
                CHECK(s.Call(pending, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref == queued);
            } else
                CHECK(s.Call(running, "cancel", "(Z)Z", {VmValue::Int(1)}).AsInt() == 1);
            s.Wait([&] { return s.state->interrupted.load(); });
            s.Call(queued, "cancel", "(Z)Z", {VmValue::Int(0)});
            s.Stop(pool);
            CHECK(s.state->calls.size() == 1);
            if (!shutdown_now)
                s.Exception(running, "get", "()Ljava/lang/Object;", "Ljava/util/concurrent/CancellationException;");
        }
    }
}

TEST_CASE("DVM-110 core pool runs independent guest workers and executor completion APIs stay Java") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ScheduledVm s(backend);
        s.state->block = true;
        const auto pool = s.Pool();
        s.Call(pool, "setCorePoolSize", "(I)V", {VmValue::Int(2)});
        const auto first = s.Schedule(pool, s.Task(1), 0);
        const auto second = s.Schedule(pool, s.Task(2), 0);
        s.Wait([&] { return s.state->calls.size() == 2; });
        CHECK(s.f.threads.LiveCount() == 2);
        CHECK(s.Call(first, "cancel", "(Z)Z", {VmValue::Int(1)}).AsInt() == 1);
        CHECK(s.Call(second, "cancel", "(Z)Z", {VmValue::Int(1)}).AsInt() == 1);
        s.Stop(pool);
        {
            VmExecutionLockScope lock(s.f.vm.ExecutionLock());
            s.state->block = false;
        }
        const auto regular = s.f.Static("Ljava/util/concurrent/Executors;", "newFixedThreadPool",
                                        "(I)Ljava/util/concurrent/ExecutorService;", {VmValue::Int(2)});
        s.f.RequireOk(regular);
        s.Keep(regular.value.ref);
        const auto tasks = s.Keep(s.f.vm.NewIntrinsicInstance("Ljava/util/ArrayList;"));
        s.f.Construct(tasks, "Ljava/util/ArrayList;", "()V");
        s.Call(tasks, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(s.Task(3))});
        s.Call(tasks, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(s.Task(4))});
        CHECK(s.f.vm.StringUtf8(s.Call(regular.value.ref, "invokeAny", "(Ljava/util/Collection;)Ljava/lang/Object;",
                                       {VmValue::Ref(tasks)})
                                    .ref) == "scheduled result");
        const auto results = s.Keep(
            s.Call(regular.value.ref, "invokeAll", "(Ljava/util/Collection;)Ljava/util/List;", {VmValue::Ref(tasks)})
                .ref);
        CHECK(s.Call(results, "size", "()I").AsInt() == 2);
        for (int i = 0; i < 2; ++i) {
            const auto done = s.Call(results, "get", "(I)Ljava/lang/Object;", {VmValue::Int(i)}).ref;
            CHECK(s.f.vm.StringUtf8(s.Call(done, "get", "()Ljava/lang/Object;").ref) == "scheduled result");
        }
        s.Stop(regular.value.ref);
    }
}

TEST_CASE("DVM-87 FutureTask and atomic state expose core semantics") {
    Dvm87Vm fixture;
    const auto callable = fixture.vm.NewIntrinsicInstance("Ltest/Dvm87Callable;");
    const auto future = fixture.vm.NewIntrinsicInstance("Ljava/util/concurrent/FutureTask;");
    fixture.Construct(future, "Ljava/util/concurrent/FutureTask;", "(Ljava/util/concurrent/Callable;)V",
                      {VmValue::Ref(callable)});
    Dvm87Vm::RequireOk(fixture.Virtual(future, "run", "()V"));
    CHECK(fixture.Virtual(future, "isDone", "()Z").value.AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(future, "get", "()Ljava/lang/Object;").value.ref) == "done");

    const auto atomic = fixture.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicInteger;");
    fixture.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicInteger;", "(I)V", {VmValue::Int(4)});
    CHECK(fixture.Virtual(atomic, "compareAndSet", "(II)Z", {VmValue::Int(4), VmValue::Int(9)}).value.AsInt() == 1);
    CHECK(fixture.Virtual(atomic, "incrementAndGet", "()I").value.AsInt() == 10);

    const auto executor = fixture
                              .Static("Ljava/util/concurrent/Executors;", "newSingleThreadExecutor",
                                      "()Ljava/util/concurrent/ExecutorService;")
                              .value.ref;
    const auto submitted =
        fixture
            .Virtual(executor, "submit", "(Ljava/util/concurrent/Callable;)Ljava/util/concurrent/Future;",
                     {VmValue::Ref(callable)})
            .value.ref;
    CHECK(fixture.vm.StringUtf8(fixture.Virtual(submitted, "get", "()Ljava/lang/Object;").value.ref) == "done");
    Dvm87Vm::RequireOk(fixture.Virtual(executor, "shutdown", "()V"));
}

TEST_CASE("DVM-103 collection VM primitives use injected time and wrapping atomics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        auto time = f.Static("Ljava/lang/System;", "nanoTime", "()J");
        REQUIRE(time.exception.IsValid());
        CHECK(f.linker.Class(time.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.vm.Monitors().SetTimeSource([] { return std::int64_t{123}; });
        time = f.Static("Ljava/lang/System;", "nanoTime", "()J");
        f.RequireOk(time);
        CHECK(time.value.AsLong() == 123000000);
        const auto runtime = f.Static("Ljava/lang/Runtime;", "getRuntime", "()Ljava/lang/Runtime;");
        f.RequireOk(runtime);
        const auto processors = f.Virtual(runtime.value.ref, "availableProcessors", "()I");
        f.RequireOk(processors);
        CHECK(processors.value.AsInt() == 1);
        const auto atomic = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicInteger;");
        f.Construct(atomic, "Ljava/util/concurrent/atomic/AtomicInteger;", "(I)V", {VmValue::Int(2147483647)});
        const auto old = f.Virtual(atomic, "getAndAdd", "(I)I", {VmValue::Int(1)});
        f.RequireOk(old);
        CHECK(old.value.AsInt() == 2147483647);
        const auto wrapped = f.Virtual(atomic, "get", "()I");
        f.RequireOk(wrapped);
        CHECK(wrapped.value.AsInt() == (-2147483647 - 1));
    }
}

TEST_CASE("DVM-104 synchronizers park real guest threads and handle interruption") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto* descriptor : {"Ljava/util/concurrent/CountDownLatch;", "Ljava/util/concurrent/Semaphore;",
                                       "Ljava/util/concurrent/CyclicBarrier;"}) {
            CAPTURE(std::string(descriptor));
            for (const bool interrupt : {false, true}) {
                CAPTURE(interrupt);
                auto sync = std::make_shared<VmObjectRef>();
                auto finished = std::make_shared<std::atomic<bool>>(false);
                auto interrupted = std::make_shared<std::atomic<bool>>(false);
                auto runner =
                    IntrinsicClassBuilder::Class("Ltest/SyncRunner;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
                const bool barrier = std::string_view(descriptor).find("CyclicBarrier") != std::string_view::npos;
                const bool semaphore = std::string_view(descriptor).find("Semaphore") != std::string_view::npos;
                runner.VirtualMethod("run", "()V",
                                     [sync, finished, interrupted, barrier, semaphore](IntrinsicContext& c) {
                                         const auto cls = c.vm.Model().ObjectClass(*sync);
                                         const auto slot = c.vm.Linker().FindVtableIndex(
                                             cls, semaphore ? "acquire" : "await", barrier ? "()I" : "()V");
                                         const std::array args{VmValue::Ref(*sync)};
                                         const auto result = c.vm.Call(c.vm.Linker().Class(cls).vtable[*slot], args);
                                         if (result.exception.IsValid()) {
                                             if (c.vm.Linker().Class(result.exception_class).descriptor ==
                                                 "Ljava/lang/InterruptedException;")
                                                 *interrupted = true;
                                             else
                                                 c.vm.SetPendingException(result.exception);
                                         }
                                         *finished = true;
                                         return VmValue::Void();
                                     });
                const std::vector<IntrinsicClassDecl> extras{std::move(runner).Build()};
                Dvm87Vm f(backend, "en", "US", "GMT", extras);
                f.vm.Monitors().SetTimeSource([] { return std::int64_t{0}; });
                *sync = f.vm.NewIntrinsicInstance(descriptor);
                f.Construct(*sync, descriptor, "(I)V", {VmValue::Int(barrier ? 2 : semaphore ? 0 : 1)});
                const std::array roots{*sync};
                const auto protect = f.vm.ProtectReferences(roots);
                const auto target = f.vm.NewIntrinsicInstance("Ltest/SyncRunner;");
                const auto thread = f.vm.NewIntrinsicInstance("Ljava/lang/Thread;");
                f.Construct(thread, "Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(target)});
                f.RequireOk(f.Virtual(thread, "start", "()V"));
                bool parked = false;
                for (int i = 0; i < 3000; ++i) {
                    for (const auto& t : f.threads.Snapshot())
                        if (t.object == thread.Value() && t.wait_state != VmThreadWaitState::none) parked = true;
                    if (parked || finished->load()) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!parked) {
                    f.threads.Interrupt(thread);
                    f.threads.Join(thread);
                }
                REQUIRE(parked);
                CHECK_FALSE(finished->load());
                if (interrupt)
                    f.threads.Interrupt(thread);
                else if (barrier)
                    f.RequireOk(f.Virtual(*sync, "await", "()I"));
                else
                    f.RequireOk(f.Virtual(*sync, semaphore ? "release" : "countDown", "()V"));
                bool done = false;
                for (int i = 0; i < 3000; ++i) {
                    if (finished->load()) {
                        done = true;
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!done) f.threads.Interrupt(thread);
                f.threads.Join(thread);
                CHECK(done);
                CHECK(interrupted->load() == interrupt);
                const auto failure = f.threads.TakeFailure();
                CHECK_MESSAGE(!failure, failure.value_or(""));
                if (barrier) {
                    const auto broken = f.Virtual(*sync, "isBroken", "()Z");
                    f.RequireOk(broken);
                    CHECK((broken.value.AsInt() != 0) == interrupt);
                    f.RequireOk(f.Virtual(*sync, "reset", "()V"));
                }
            }
        }
    }
}

TEST_CASE("DVM-104 atomic arrays references and immediate timeouts follow API19") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        f.vm.Monitors().SetTimeSource([] { return std::int64_t{123}; });
        for (const bool wide : {false, true}) {
            const auto type =
                std::string("Ljava/util/concurrent/atomic/Atomic") + (wide ? "LongArray;" : "IntegerArray;");
            const auto array = f.vm.NewIntrinsicInstance(type);
            f.Construct(array, type, "(I)V", {VmValue::Int(2)});
            auto result = f.Virtual(array, "compareAndSet", wide ? "(IJJ)Z" : "(III)Z",
                                    {VmValue::Int(1), wide ? VmValue::Long(0) : VmValue::Int(0),
                                     wide ? VmValue::Long(INT64_MAX) : VmValue::Int(INT32_MAX)});
            f.RequireOk(result);
            CHECK(result.value.AsInt() == 1);
            result = f.Virtual(array, "incrementAndGet", wide ? "(I)J" : "(I)I", {VmValue::Int(1)});
            f.RequireOk(result);
            CHECK((wide ? result.value.AsLong() : result.value.AsInt()) == (wide ? INT64_MIN : INT32_MIN));
            result = f.Virtual(array, "get", wide ? "(I)J" : "(I)I", {VmValue::Int(2)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/IndexOutOfBoundsException;");
        }
        const auto original = f.vm.NewStringUtf8("old"), next = f.vm.NewStringUtf8("new");
        for (const bool marked : {false, true}) {
            const auto type = std::string("Ljava/util/concurrent/atomic/Atomic") +
                              (marked ? "MarkableReference;" : "StampedReference;");
            const auto reference = f.vm.NewIntrinsicInstance(type);
            f.Construct(reference, type, marked ? "(Ljava/lang/Object;Z)V" : "(Ljava/lang/Object;I)V",
                        {VmValue::Ref(original), VmValue::Int(0)});
            const auto cas = f.Virtual(reference, "compareAndSet",
                                       marked ? "(Ljava/lang/Object;Ljava/lang/Object;ZZ)Z"
                                              : "(Ljava/lang/Object;Ljava/lang/Object;II)Z",
                                       {VmValue::Ref(original), VmValue::Ref(next), VmValue::Int(0), VmValue::Int(1)});
            f.RequireOk(cas);
            CHECK(cas.value.AsInt() == 1);
            const auto value = f.Virtual(reference, "getReference", "()Ljava/lang/Object;");
            f.RequireOk(value);
            CHECK(value.value.ref == next);
        }
        const auto references = f.vm.NewIntrinsicInstance("Ljava/util/concurrent/atomic/AtomicReferenceArray;");
        f.Construct(references, "Ljava/util/concurrent/atomic/AtomicReferenceArray;", "(I)V", {VmValue::Int(1)});
        f.RequireOk(f.Virtual(references, "lazySet", "(ILjava/lang/Object;)V", {VmValue::Int(0), VmValue::Ref(next)}));
        const std::array roots{references};
        const auto protect = f.vm.ProtectReferences(roots);
        static_cast<void>(f.vm.CollectGarbage());
        const auto kept = f.Virtual(references, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)});
        f.RequireOk(kept);
        CHECK(f.vm.StringUtf8(kept.value.ref) == "new");
        const auto unit_type = f.linker.ResolveDescriptor("Ljava/util/concurrent/TimeUnit;");
        f.RequireOk(f.vm.EnsureClassInitialized(unit_type));
        const auto unit_field =
            f.linker.FindFieldRecursive(unit_type, "MILLISECONDS", "Ljava/util/concurrent/TimeUnit;");
        REQUIRE(unit_field);
        const auto unit = VmObjectRef(f.linker.Class(unit_type).static_storage[f.linker.Field(*unit_field).slot]);
        for (const auto* type : {"Ljava/util/concurrent/CountDownLatch;", "Ljava/util/concurrent/Semaphore;",
                                 "Ljava/util/concurrent/CyclicBarrier;"}) {
            const bool barrier = std::string_view(type).find("CyclicBarrier") != std::string_view::npos;
            const bool semaphore = std::string_view(type).find("Semaphore") != std::string_view::npos;
            const auto sync = f.vm.NewIntrinsicInstance(type);
            f.Construct(sync, type, "(I)V", {VmValue::Int(barrier ? 2 : semaphore ? 0 : 1)});
            const auto result =
                f.Virtual(sync, semaphore ? "tryAcquire" : "await",
                          barrier ? "(JLjava/util/concurrent/TimeUnit;)I" : "(JLjava/util/concurrent/TimeUnit;)Z",
                          {VmValue::Long(0), VmValue::Ref(unit)});
            if (barrier) {
                REQUIRE(result.exception.IsValid());
                CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/util/concurrent/TimeoutException;");
                const auto broken = f.Virtual(sync, "isBroken", "()Z");
                f.RequireOk(broken);
                CHECK(broken.value.AsInt() == 1);
            } else {
                f.RequireOk(result);
                CHECK(result.value.AsInt() == 0);
            }
        }
    }
}

TEST_CASE("DVM-267 ROM Thread subclass executes start and join") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        CHECK(f.Static("Lfixture/PlatformTables;", "romThread", "()I").AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());
    }
}
