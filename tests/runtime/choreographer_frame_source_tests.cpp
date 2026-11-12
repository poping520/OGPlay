#include "native_library_fixture.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/hal/clock.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <future>
#include <bit>
#include <limits>
#include <vector>

namespace {
using namespace ogplay;
using namespace runtime::dexvm;

struct Frames final {
    test::native_library::OrchestratedApp f;
    Interpreter& vm;
    std::vector<core::UnimplementedHit> startup_gaps;

    explicit Frames(InterpreterBackend backend, float refresh_rate = 60)
        : f("fixture.HandshakeActivity", true, false, {}, {}, true, backend), vm(f.app->DexVm().Vm()) {
        f.context->display_refresh_rate_hz = refresh_rate;
        f.app->ActivityLifecycle().SetRealtimeVideoClock(false);
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        startup_gaps = f.ledger.Unimplemented();
        for (const auto& hit : startup_gaps)
            CHECK_MESSAGE((hit.id == "dexvm.service_manager.dropbox" || hit.id == "libcore.io.Posix.chmod"), hit.id);
    }

    std::vector<core::UnimplementedHit> NewGaps() {
        auto gaps = f.ledger.Unimplemented();
        std::erase_if(gaps, [&](const auto& hit) {
            return std::ranges::any_of(
                startup_gaps, [&](const auto& startup) { return hit.id == startup.id && hit.count == startup.count; });
        });
        return gaps;
    }

    VmCallOutcome StaticInCurrentContext(const char* owner, const char* name, const char* sig,
                                         const std::vector<VmValue>& args = {}) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto init = vm.EnsureClassInitialized(type);
        if (init.exception.IsValid()) return init;
        const auto method = vm.Linker().FindDirectMethod(type, name, sig);
        REQUIRE(method.has_value());
        return vm.Call(*method, args);
    }

    VmCallOutcome StaticOutcome(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        VmCallOutcome outcome;
        f.RunOnMainThread([&] { outcome = StaticInCurrentContext(owner, name, sig, args); });
        return outcome;
    }

    VmValue Value(const VmCallOutcome& result) {
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }

    VmValue Static(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        return Value(StaticOutcome(owner, name, sig, std::move(args)));
    }

    VmValue StaticInCurrent(const char* owner, const char* name, const char* sig) {
        return Value(StaticInCurrentContext(owner, name, sig));
    }

    VmValue CallInCurrent(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Model().ObjectClass(receiver);
        const auto slot = vm.Linker().FindVtableIndex(type, name, sig);
        REQUIRE(slot.has_value());
        args.insert(args.begin(), VmValue::Ref(receiver));
        return Value(vm.Call(vm.Linker().Class(type).vtable[*slot], args));
    }

    VmValue Call(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        VmValue outcome;
        f.RunOnMainThread([&] { outcome = CallInCurrent(receiver, name, sig, std::move(args)); });
        return outcome;
    }

    VmObjectRef NewInCurrent(const char* owner, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        static_cast<void>(Value(vm.EnsureClassInitialized(type)));
        const auto object = vm.NewIntrinsicInstance(owner);
        const auto roots = vm.ProtectReferences(std::array{object});
        const auto init = vm.Linker().FindDirectMethod(type, "<init>", sig);
        REQUIRE(init.has_value());
        args.insert(args.begin(), VmValue::Ref(object));
        static_cast<void>(Value(vm.Call(*init, args)));
        return object;
    }

    VmObjectRef New(const char* owner, const char* sig, std::vector<VmValue> args = {}) {
        VmObjectRef object;
        f.RunOnMainThread([&] { object = NewInCurrent(owner, sig, std::move(args)); });
        return object;
    }

    void Hook(const char* owner, const char* name, const char* sig, IntrinsicHandler handler) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto slot = vm.Linker().FindVtableIndex(type, name, sig);
        REQUIRE(slot.has_value());
        auto& method = vm.Linker().MutableMethod(vm.Linker().Class(type).vtable[*slot]);
        method.kind = MethodKind::intrinsic;
        method.implementation = std::move(handler);
    }

    VmObjectRef Choreographer() {
        return Static("Landroid/view/Choreographer;", "getInstance", "()Landroid/view/Choreographer;").ref;
    }

    void Post(VmObjectRef choreographer, VmObjectRef callback, std::int64_t delay = 0) {
        static_cast<void>(Call(choreographer, "postFrameCallbackDelayed",
                               "(Landroid/view/Choreographer$FrameCallback;J)V",
                               {VmValue::Ref(callback), VmValue::Long(delay)}));
    }

    void Step() { static_cast<void>(f.app->ActivityLifecycle().StepFrame()); }

    std::size_t ReceiverCount() {
        std::size_t count{};
        f.RunOnMainThread([&] { count = f.context->display_receivers.size(); });
        return count;
    }
};
}  // namespace

TEST_CASE("DVM-336 ROM Choreographer single frame callbacks use the host frame Clock before traversal") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Frames f(backend);
        const auto choreographer = f.Choreographer();
        CHECK(f.Choreographer() == choreographer);
        const auto callback = f.New("Lfixture/FrameProbe;", "()V");
        const auto roots = f.vm.ProtectReferences(std::array{choreographer, callback});
        auto stamps = std::make_shared<std::vector<std::int64_t>>();
        f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [&, stamps](IntrinsicContext& call) {
            CHECK(f.vm.CurrentContextToken() == 1);
            const auto time = call.arguments[0].AsLong();
            CHECK(time == f.Static("Ljava/lang/System;", "nanoTime", "()J").AsLong());
            CHECK(f.Call(choreographer, "getFrameTimeNanos", "()J").AsLong() == time);
            CHECK(f.Call(choreographer, "getFrameTime", "()J").AsLong() == time / 1'000'000);
            stamps->push_back(time);
            return VmValue::Void();
        });
        CHECK(f.ReceiverCount() == 1);
        f.Post(choreographer, callback);
        CHECK(stamps->empty());
        const auto next_time = (f.f.context->uptime_millis.load() + 16) * 1'000'000;
        f.Step();
        REQUIRE(stamps->size() == 1);
        CHECK(stamps->front() == next_time);
        f.Step();
        CHECK(stamps->size() == 1);  // No schedule, no callback.
        const auto due = (f.f.context->uptime_millis.load() + 32) * 1'000'000;
        f.Post(choreographer, callback, 32);
        f.Step();
        CHECK(stamps->size() == 1);
        f.Step();
        f.Step();
        REQUIRE(stamps->size() == 2);
        CHECK(stamps->back() >= due);
        f.Post(choreographer, callback);
        static_cast<void>(f.Call(choreographer, "removeFrameCallback", "(Landroid/view/Choreographer$FrameCallback;)V",
                                 {VmValue::Ref(callback)}));
        f.Step();
        CHECK(stamps->size() == 2);
        f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [&, stamps](IntrinsicContext& call) {
            stamps->push_back(call.arguments[0].AsLong());
            f.Post(choreographer, callback);
            return VmValue::Void();
        });
        f.Post(choreographer, callback);
        for (int i = 0; i < 3; ++i) {
            f.Step();
            CHECK(stamps->size() == static_cast<std::size_t>(3 + i));
        }
        for (const auto& hit : f.NewGaps())
            CHECK_MESSAGE(false, hit.id);
        static_cast<void>(f.f.app->Stop());
        REQUIRE(f.f.context->display_receivers.empty());
        const auto count = stamps->size();
        runtime::QueueAndroidDisplayFrame(f.vm.ExecutionLock(), *f.f.context);
        CHECK(stamps->size() == count);
    }
}

TEST_CASE("DVM-336 DisplayEventReceiver one shot merge dispose invalid queue and token") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Frames f(backend);
        const auto baseline = f.ReceiverCount();
        const auto looper = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
        CHECK(f.Call(looper, "getQueue", "()Landroid/os/MessageQueue;").ref ==
              f.Static("Landroid/os/Looper;", "myQueue", "()Landroid/os/MessageQueue;").ref);
        const auto receiver = f.New("Lfixture/DisplayProbe;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
        const auto roots = f.vm.ProtectReferences(std::array{receiver});
        auto frames = std::make_shared<std::vector<int>>();
        f.Hook("Lfixture/DisplayProbe;", "onVsync", "(JII)V", [&, frames](IntrinsicContext& call) {
            CHECK(call.arguments[0].AsLong() == f.f.context->uptime_millis.load() * 1'000'000);
            CHECK(call.arguments[1].AsInt() == 0);
            frames->push_back(call.arguments[2].AsInt());
            return VmValue::Void();
        });
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        f.Step();
        REQUIRE(frames->size() == 1);
        f.Step();
        CHECK(frames->size() == 1);
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        f.Step();
        REQUIRE(frames->size() == 2);
        CHECK(frames->back() > frames->front());
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        static_cast<void>(f.Call(receiver, "dispose", "()V"));
        static_cast<void>(f.Call(receiver, "dispose", "()V"));
        f.Step();
        CHECK(frames->size() == 2);
        CHECK(f.ReceiverCount() == baseline);
        const auto invalid = f.StaticOutcome("Landroid/view/DisplayEventReceiver;", "nativeInit",
                                             "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I",
                                             {VmValue::Ref(receiver), VmValue::Ref(VmObjectRef{})});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.vm.Linker().Class(invalid.exception_class).descriptor == "Ljava/lang/RuntimeException;");
        CHECK(invalid.exception_message == "MessageQueue is not initialized.");
        const auto stale = f.StaticOutcome("Landroid/view/DisplayEventReceiver;", "nativeScheduleVsync", "(I)V",
                                           {VmValue::Int(123456)});
        CHECK(stale.exception.IsValid());
        static_cast<void>(f.f.app->Stop());
    }
}

TEST_CASE("DVM-336 Choreographer requires a Looper and uses injected refresh rate") {
    Frames f(InterpreterBackend::switch_dispatch, 75);
    const auto worker = f.vm.CreateExecutionContext();
    f.vm.Threads().RegisterNativeContext(worker, false, "frame-no-looper");
    std::uint64_t worker_token{};
    f.vm.RunInExecutionContext(worker, [&] {
        worker_token = f.vm.CurrentContextToken();
        const auto result =
            f.StaticInCurrentContext("Landroid/view/Choreographer;", "getInstance", "()Landroid/view/Choreographer;");
        REQUIRE(result.exception.IsValid());
        CHECK(f.vm.Linker().Class(result.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
    });
    f.vm.Threads().DetachNativeContext(worker_token);
    f.vm.DiscardExecutionContext(worker);
    const auto global = f.Static("Landroid/hardware/display/DisplayManagerGlobal;", "getInstance",
                                 "()Landroid/hardware/display/DisplayManagerGlobal;")
                            .ref;
    const auto info = f.Call(global, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;", {VmValue::Int(0)}).ref;
    const auto field = f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(info), "refreshRate", "F");
    REQUIRE(field.has_value());
    CHECK(std::bit_cast<float>(f.vm.Model().InstanceSlots(info)[f.vm.Linker().Field(*field).slot].bits) == 75);
    CHECK_FALSE(f.Call(global, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;", {VmValue::Int(1)}).ref.IsValid());
    const auto choreographer = f.Choreographer();
    const auto interval =
        f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(choreographer), "mFrameIntervalNanos", "J");
    REQUIRE(interval.has_value());
    CHECK(f.vm.Model().InstanceSlots(choreographer)[f.vm.Linker().Field(*interval).slot].bits == 13333333);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 ROM Message copy flags and front-of-queue ordering") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto handler = f.New("Landroid/os/Handler;", "()V");
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{handler, callback});
    auto order = std::make_shared<std::vector<int>>();
    f.Hook("Lfixture/FrameProbe;", "run", "()V", [order](IntrinsicContext&) {
        order->push_back(1);
        return VmValue::Void();
    });
    f.Hook("Landroid/os/Handler;", "handleMessage", "(Landroid/os/Message;)V", [&, order](IntrinsicContext& call) {
        const auto field =
            f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(call.arguments[0].ref), "what", "I");
        order->push_back(
            static_cast<int>(f.vm.Model().InstanceSlots(call.arguments[0].ref)[f.vm.Linker().Field(*field).slot].bits));
        return VmValue::Void();
    });
    f.f.RunOnMainThread([&] {
        const auto runnable =
            f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;Ljava/lang/Runnable;)Landroid/os/Message;",
                     {VmValue::Ref(handler), VmValue::Ref(callback)})
                .ref;
        static_cast<void>(f.Call(runnable, "setAsynchronous", "(Z)V", {VmValue::Int(1)}));
        CHECK(f.Call(runnable, "isAsynchronous", "()Z").AsInt() == 1);
        const auto copy = f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Message;)Landroid/os/Message;",
                                   {VmValue::Ref(runnable)})
                              .ref;
        CHECK(f.Call(copy, "isAsynchronous", "()Z").AsInt() == 0);
        static_cast<void>(f.Call(handler, "sendMessage", "(Landroid/os/Message;)Z", {VmValue::Ref(runnable)}));
        for (const int what : {2, 3}) {
            const auto message = f.Call(handler, "obtainMessage", "(I)Landroid/os/Message;", {VmValue::Int(what)}).ref;
            CHECK(f.Call(handler, "sendMessageAtFrontOfQueue", "(Landroid/os/Message;)Z", {VmValue::Ref(message)})
                      .AsInt() == 1);
        }
    });
    f.Step();
    CHECK(*order == std::vector<int>{3, 2, 1});
    for (const auto& hit : f.NewGaps())
        CHECK_MESSAGE(false, hit.id);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 DisplayEventReceiver finalization releases weak native registration") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto baseline = f.ReceiverCount();
    const auto looper = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
    static_cast<void>(f.New("Lfixture/DisplayProbe;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)}));
    REQUIRE(f.ReceiverCount() == baseline + 1);
    f.f.RunOnMainThread([&] {
        static_cast<void>(f.vm.CollectGarbage("display-receiver-weak-registration"));
        f.vm.RunFinalization();
    });
    CHECK(f.ReceiverCount() == baseline);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 receiver initializes on its own Looper thread and rejects a foreign queue") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto baseline = f.ReceiverCount();
    const auto main = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
    const auto receiver = f.New("Lfixture/DisplayProbe;", "(Landroid/os/Looper;)V", {VmValue::Ref(main)});
    const auto roots = f.vm.ProtectReferences(std::array{receiver});
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    auto rejected = std::make_shared<bool>(false);
    f.Hook("Lfixture/FrameProbe;", "run", "()V", [&, rejected](IntrinsicContext&) {
        static_cast<void>(f.StaticInCurrent("Landroid/os/Looper;", "prepare", "()V"));
        const auto looper = f.StaticInCurrent("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
        const auto queue = f.CallInCurrent(looper, "getQueue", "()Landroid/os/MessageQueue;").ref;
        const auto accepted =
            f.StaticInCurrentContext("Landroid/view/DisplayEventReceiver;", "nativeInit",
                                     "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I",
                                     {VmValue::Ref(receiver), VmValue::Ref(queue)});
        REQUIRE_FALSE(accepted.exception.IsValid());
        CHECK(accepted.value.AsInt() > 0);
        static_cast<void>(f.StaticInCurrentContext("Landroid/view/DisplayEventReceiver;", "nativeDispose", "(I)V",
                                                   {VmValue::Int(accepted.value.AsInt())}));
        const auto main_queue = f.CallInCurrent(main, "getQueue", "()Landroid/os/MessageQueue;").ref;
        const auto result = f.StaticInCurrentContext("Landroid/view/DisplayEventReceiver;", "nativeInit",
                                                     "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I",
                                                     {VmValue::Ref(receiver), VmValue::Ref(main_queue)});
        REQUIRE(result.exception.IsValid());
        CHECK(f.vm.Linker().Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(result.exception_message.find("own Looper") != std::string::npos);
        *rejected = true;
        static_cast<void>(f.CallInCurrent(looper, "quit", "()V"));
        return VmValue::Void();
    });
    const auto thread = f.New("Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(callback)});
    const auto thread_roots = f.vm.ProtectReferences(std::array{thread, callback});
    static_cast<void>(f.Call(thread, "start", "()V"));
    static_cast<void>(f.Call(thread, "join", "()V"));
    CHECK(*rejected);
    REQUIRE(f.ReceiverCount() == baseline + 1);
    const auto hits = f.NewGaps();
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().id == "dexvm.choreographer_frame_source");
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("frame-rate cap gates display frames while lifecycle steps keep the logical clock") {
    Frames f(InterpreterBackend::switch_dispatch);
    f.f.app->ActivityLifecycle().SetRealtimeVideoClock(true);
    f.f.app->ActivityLifecycle().SetRealtimeTimeSourceForTesting(
        [now = std::uint64_t{1'000'000'000}]() mutable { return now += 16'000'000; });
    const auto choreographer = f.Choreographer();
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{choreographer, callback});
    auto frames = std::make_shared<int>(0);
    f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [&, frames](IntrinsicContext&) {
        ++*frames;
        f.Post(choreographer, callback);
        return VmValue::Void();
    });
    f.Post(choreographer, callback);
    const auto step_and_wait = [&] {
        f.Step();
        const auto deadline = hal::Clock::SteadyTimestampNs() + 2'000'000'000;
        while (!runtime::AndroidMainQueueIdle(*f.f.context) && hal::Clock::SteadyTimestampNs() < deadline)
            hal::SleepPreciseNs(150'000);
        REQUIRE(runtime::AndroidMainQueueIdle(*f.f.context));
    };
    // A period far longer than the test: the first step claims the only display frame.
    f.f.context->frame_period_ns = 3'600'000'000'000ULL;
    const auto uptime = f.f.context->uptime_millis.load();
    for (int i = 0; i < 4; ++i)
        step_and_wait();
    CHECK(*frames == 1);
    CHECK(f.f.context->uptime_millis.load() - uptime == 4 * 16);
    // Without a cap every step is a display frame again.
    f.f.context->frame_period_ns = 0;
    for (int i = 0; i < 3; ++i)
        step_and_wait();
    CHECK(*frames == 4);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("Choreographer delivers vsync on a non-main owner Looper") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Frames f(backend);
        std::promise<void> ready, delivered;
        auto ready_future = ready.get_future(), delivered_future = delivered.get_future();
        const auto callback = f.New("Lfixture/FrameProbe;", "()V");
        f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [&](IntrinsicContext&) {
            CHECK(f.vm.CurrentContextToken() != 1);
            const auto looper = f.StaticInCurrent("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
            static_cast<void>(f.CallInCurrent(looper, "quit", "()V"));
            delivered.set_value();
            return VmValue::Void();
        });
        f.Hook("Lfixture/FrameProbe;", "run", "()V", [&](IntrinsicContext&) {
            static_cast<void>(f.StaticInCurrent("Landroid/os/Looper;", "prepare", "()V"));
            const auto choreographer =
                f.StaticInCurrent("Landroid/view/Choreographer;", "getInstance", "()Landroid/view/Choreographer;").ref;
            static_cast<void>(f.CallInCurrent(choreographer, "postFrameCallback",
                                              "(Landroid/view/Choreographer$FrameCallback;)V",
                                              {VmValue::Ref(callback)}));
            ready.set_value();
            static_cast<void>(f.StaticInCurrent("Landroid/os/Looper;", "loop", "()V"));
            return VmValue::Void();
        });
        const auto thread = f.New("Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(callback)});
        const auto roots = f.vm.ProtectReferences(std::array{thread, callback});
        static_cast<void>(f.Call(thread, "start", "()V"));
        REQUIRE(ready_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        f.Step();
        CHECK(delivered_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        static_cast<void>(f.Call(thread, "join", "()V"));
        static_cast<void>(f.f.app->Stop());
    }
}

TEST_CASE("Worker Looper yields after a callback batch without terminating a self-reposting producer") {
    Frames f(InterpreterBackend::switch_dispatch);
    int count = 0;
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    f.Hook("Landroid/os/Handler;", "handleMessage", "(Landroid/os/Message;)V", [&](IntrinsicContext& call) {
        if (++count < 10005) {
            static_cast<void>(f.CallInCurrent(call.receiver, "sendEmptyMessage", "(I)Z", {VmValue::Int(1)}));
        } else {
            const auto looper = f.StaticInCurrent("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
            static_cast<void>(f.CallInCurrent(looper, "quit", "()V"));
        }
        return VmValue::Void();
    });
    f.Hook("Lfixture/FrameProbe;", "run", "()V", [&](IntrinsicContext&) {
        static_cast<void>(f.StaticInCurrent("Landroid/os/Looper;", "prepare", "()V"));
        const auto handler = f.NewInCurrent("Landroid/os/Handler;", "()V");
        static_cast<void>(f.CallInCurrent(handler, "sendEmptyMessage", "(I)Z", {VmValue::Int(1)}));
        static_cast<void>(f.StaticInCurrent("Landroid/os/Looper;", "loop", "()V"));
        return VmValue::Void();
    });
    const auto thread = f.New("Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(callback)});
    const auto roots = f.vm.ProtectReferences(std::array{thread, callback});
    static_cast<void>(f.Call(thread, "start", "()V"));
    static_cast<void>(f.Call(thread, "join", "()V"));
    CHECK(count == 10005);
    static_cast<void>(f.f.app->Stop());
}
