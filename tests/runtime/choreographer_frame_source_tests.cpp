#include "native_library_fixture.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include <doctest/doctest.h>
#include <array>
#include <bit>
#include <limits>
#include <vector>

namespace {
using namespace ogplay;
using namespace runtime::dexvm;
struct Frames final {
    test::native_library::OrchestratedApp f;
    Interpreter& vm;
    explicit Frames(InterpreterBackend backend) : f("fixture.HandshakeActivity", true, false, {}, {}, true, backend),
                                                vm(f.app->DexVm().Vm()) {
        f.app->StartApplication();
        REQUIRE(f.app->StartLauncherActivity().state == session::LifecycleRunState::running);
    }
    VmCallOutcome StaticOutcome(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto init = vm.EnsureClassInitialized(type);
        if (init.exception.IsValid()) return init;
        const auto method = vm.Linker().FindDirectMethod(type, name, sig);
        REQUIRE(method.has_value());
        return vm.Call(*method, args);
    }
    VmValue Value(const VmCallOutcome& result) {
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    VmValue Static(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        return Value(StaticOutcome(owner, name, sig, std::move(args)));
    }
    VmValue Call(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Model().ObjectClass(receiver);
        const auto slot = vm.Linker().FindVtableIndex(type, name, sig);
        REQUIRE(slot.has_value());
        args.insert(args.begin(), VmValue::Ref(receiver));
        return Value(vm.Call(vm.Linker().Class(type).vtable[*slot], args));
    }
    VmObjectRef New(const char* owner, const char* sig, std::vector<VmValue> args = {}) {
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
    void Hook(const char* owner, const char* name, const char* sig, IntrinsicHandler handler) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto slot = vm.Linker().FindVtableIndex(type, name, sig);
        REQUIRE(slot.has_value());
        auto& method = vm.Linker().MutableMethod(vm.Linker().Class(type).vtable[*slot]);
        method.kind = MethodKind::intrinsic; method.implementation = std::move(handler);
    }
    VmObjectRef Choreographer() { return Static("Landroid/view/Choreographer;", "getInstance", "()Landroid/view/Choreographer;").ref; }
    void Post(VmObjectRef choreographer, VmObjectRef callback, std::int64_t delay = 0) {
        static_cast<void>(Call(choreographer, "postFrameCallbackDelayed", "(Landroid/view/Choreographer$FrameCallback;J)V",
                              {VmValue::Ref(callback), VmValue::Long(delay)}));
    }
    void Step() { static_cast<void>(f.app->ActivityLifecycle().StepFrame()); }
};
}

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
        CHECK(f.f.context->display_receivers.size() == 1);
        f.Post(choreographer, callback);
        CHECK(stamps->empty());
        const auto next_time = f.f.context->uptime_millis.load() * 1'000'000;
        f.Step();
        REQUIRE(stamps->size() == 1);
        CHECK(stamps->front() == next_time);
        f.Step();
        CHECK(stamps->size() == 1); // No schedule, no callback.
        f.Post(choreographer, callback, 32);
        f.Step();
        CHECK(stamps->size() == 1);
        f.Step();
        CHECK(stamps->size() == 1);
        f.Step();
        CHECK(stamps->size() == 2);
        f.Post(choreographer, callback);
        static_cast<void>(f.Call(choreographer, "removeFrameCallback", "(Landroid/view/Choreographer$FrameCallback;)V", {VmValue::Ref(callback)}));
        f.Step();
        CHECK(stamps->size() == 2);
        f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [&, stamps](IntrinsicContext& call) {
            stamps->push_back(call.arguments[0].AsLong());
            f.Post(choreographer, callback);
            return VmValue::Void();
        });
        f.Post(choreographer, callback);
        for (int i = 0; i < 3; ++i) { f.Step(); CHECK(stamps->size() == static_cast<std::size_t>(3 + i)); }
        CHECK(f.f.ledger.Unimplemented().empty());
        static_cast<void>(f.f.app->Stop());
        REQUIRE(f.f.context->display_receivers.empty());
        const auto count = stamps->size();
        runtime::DispatchAndroidDisplayFrame(f.vm, *f.f.context);
        CHECK(stamps->size() == count);
    }
}

TEST_CASE("DVM-336 DisplayEventReceiver one shot merge dispose invalid queue and token") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Frames f(backend);
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
        f.Step(); REQUIRE(frames->size() == 1);
        f.Step(); CHECK(frames->size() == 1);
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        f.Step(); REQUIRE(frames->size() == 2); CHECK(frames->back() > frames->front());
        static_cast<void>(f.Call(receiver, "scheduleVsync", "()V"));
        static_cast<void>(f.Call(receiver, "dispose", "()V"));
        static_cast<void>(f.Call(receiver, "dispose", "()V"));
        f.Step(); CHECK(frames->size() == 2); CHECK(f.f.context->display_receivers.empty());
        const auto invalid = f.StaticOutcome("Landroid/view/DisplayEventReceiver;", "nativeInit",
            "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I", {VmValue::Ref(receiver), VmValue::Ref(VmObjectRef{})});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.vm.Linker().Class(invalid.exception_class).descriptor == "Ljava/lang/RuntimeException;");
        CHECK(invalid.exception_message == "MessageQueue is not initialized.");
        const auto stale = f.StaticOutcome("Landroid/view/DisplayEventReceiver;", "nativeScheduleVsync", "(I)V", {VmValue::Int(123456)});
        CHECK(stale.exception.IsValid());
        static_cast<void>(f.f.app->Stop());
    }
}

TEST_CASE("DVM-336 Choreographer requires a Looper and uses injected refresh rate") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto worker = f.vm.CreateExecutionContext();
    f.vm.Threads().RegisterNativeContext(worker, false, "frame-no-looper");
    std::uint64_t worker_token{};
    f.vm.RunInExecutionContext(worker, [&] {
        worker_token = f.vm.CurrentContextToken();
        const auto result = f.StaticOutcome("Landroid/view/Choreographer;", "getInstance", "()Landroid/view/Choreographer;");
        REQUIRE(result.exception.IsValid());
        CHECK(f.vm.Linker().Class(result.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
    });
    f.vm.Threads().DetachNativeContext(worker_token);
    f.vm.DiscardExecutionContext(worker);
    f.f.context->display_refresh_rate_hz = 75;
    const auto global = f.Static("Landroid/hardware/display/DisplayManagerGlobal;", "getInstance", "()Landroid/hardware/display/DisplayManagerGlobal;").ref;
    const auto info = f.Call(global, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;", {VmValue::Int(0)}).ref;
    const auto field = f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(info), "refreshRate", "F");
    REQUIRE(field.has_value());
    CHECK(std::bit_cast<float>(f.vm.Model().InstanceSlots(info)[f.vm.Linker().Field(*field).slot].bits) == 75);
    CHECK_FALSE(f.Call(global, "getDisplayInfo", "(I)Landroid/view/DisplayInfo;", {VmValue::Int(1)}).ref.IsValid());
    const auto choreographer = f.Choreographer();
    const auto interval = f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(choreographer), "mFrameIntervalNanos", "J");
    REQUIRE(interval.has_value());
    CHECK(f.vm.Model().InstanceSlots(choreographer)[f.vm.Linker().Field(*interval).slot].bits == 13333333);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 asynchronous flags persist and front messages precede FIFO work") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto handler = f.New("Landroid/os/Handler;", "()V");
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{handler, callback});
    auto order = std::make_shared<std::vector<int>>();
    f.Hook("Lfixture/FrameProbe;", "run", "()V", [order](IntrinsicContext&) { order->push_back(1); return VmValue::Void(); });
    f.Hook("Landroid/os/Handler;", "handleMessage", "(Landroid/os/Message;)V", [&, order](IntrinsicContext& call) {
        const auto field = f.vm.Linker().FindFieldRecursive(f.vm.Model().ObjectClass(call.arguments[0].ref), "what", "I");
        order->push_back(static_cast<int>(f.vm.Model().InstanceSlots(call.arguments[0].ref)[f.vm.Linker().Field(*field).slot].bits));
        return VmValue::Void();
    });
    const auto runnable = f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Handler;Ljava/lang/Runnable;)Landroid/os/Message;",
                                  {VmValue::Ref(handler), VmValue::Ref(callback)}).ref;
    static_cast<void>(f.Call(runnable, "setAsynchronous", "(Z)V", {VmValue::Int(1)}));
    CHECK(f.Call(runnable, "isAsynchronous", "()Z").AsInt() == 1);
    const auto copy = f.Static("Landroid/os/Message;", "obtain", "(Landroid/os/Message;)Landroid/os/Message;", {VmValue::Ref(runnable)}).ref;
    CHECK(f.Call(copy, "isAsynchronous", "()Z").AsInt() == 1);
    static_cast<void>(f.Call(copy, "setAsynchronous", "(Z)V", {VmValue::Int(0)}));
    CHECK(f.Call(copy, "isAsynchronous", "()Z").AsInt() == 0);
    static_cast<void>(f.Call(handler, "sendMessage", "(Landroid/os/Message;)Z", {VmValue::Ref(runnable)}));
    for (const int what : {2, 3}) {
        const auto message = f.Call(handler, "obtainMessage", "(I)Landroid/os/Message;", {VmValue::Int(what)}).ref;
        CHECK(f.Call(handler, "sendMessageAtFrontOfQueue", "(Landroid/os/Message;)Z", {VmValue::Ref(message)}).AsInt() == 1);
    }
    f.Step();
    CHECK(*order == std::vector<int>{3, 2, 1});
    CHECK(f.f.ledger.Unimplemented().empty());
    static_cast<void>(f.f.app->Stop());
}


TEST_CASE("DVM-336 DisplayEventReceiver finalization releases weak native registration") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto looper = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
    static_cast<void>(f.New("Lfixture/DisplayProbe;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)}));
    REQUIRE(f.f.context->display_receivers.size() == 1);
    static_cast<void>(f.vm.CollectGarbage("display-receiver-weak-registration"));
    f.vm.RunFinalization();
    CHECK(f.f.context->display_receivers.empty());
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 animation callbacks precede the frame owner traversal") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto choreographer = f.Choreographer(), callback = f.New("Lfixture/FrameProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{callback, choreographer});
    auto order = std::make_shared<std::vector<int>>();
    f.Hook("Lfixture/FrameProbe;", "doFrame", "(J)V", [order](IntrinsicContext&) {
        order->push_back(1); return VmValue::Void();
    });
    f.Hook("Lfixture/HandshakeView;", "onSizeChanged", "(IIII)V", [order](IntrinsicContext&) {
        order->push_back(2); return VmValue::Void();
    });
    const auto params = f.New("Landroid/widget/FrameLayout$LayoutParams;", "(II)V", {VmValue::Int(32), VmValue::Int(18)});
    static_cast<void>(f.Call(f.f.context->content_view, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
                            {VmValue::Ref(params)}));
    f.Post(choreographer, callback);
    f.Step();
    REQUIRE(order->size() >= 2);
    CHECK(order->front() == 1);
    CHECK(order->at(1) == 2);
    static_cast<void>(f.f.app->Stop());
}

TEST_CASE("DVM-336 non main Looper receiver initialization fails explicitly") {
    Frames f(InterpreterBackend::switch_dispatch);
    const auto main = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
    const auto receiver = f.New("Lfixture/DisplayProbe;", "(Landroid/os/Looper;)V", {VmValue::Ref(main)});
    const auto roots = f.vm.ProtectReferences(std::array{receiver});
    const auto callback = f.New("Lfixture/FrameProbe;", "()V");
    auto rejected = std::make_shared<bool>(false);
    f.Hook("Lfixture/FrameProbe;", "run", "()V", [&, rejected](IntrinsicContext&) {
        static_cast<void>(f.Static("Landroid/os/Looper;", "prepare", "()V"));
        const auto looper = f.Static("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
        const auto queue = f.Call(looper, "getQueue", "()Landroid/os/MessageQueue;").ref;
        const auto result = f.StaticOutcome("Landroid/view/DisplayEventReceiver;", "nativeInit",
            "(Landroid/view/DisplayEventReceiver;Landroid/os/MessageQueue;)I", {VmValue::Ref(receiver), VmValue::Ref(queue)});
        REQUIRE(result.exception.IsValid());
        CHECK(f.vm.Linker().Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(result.exception_message.find("main Looper") != std::string::npos);
        *rejected = true;
        static_cast<void>(f.Call(looper, "quit", "()V"));
        return VmValue::Void();
    });
    const auto thread = f.New("Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(callback)});
    const auto thread_roots = f.vm.ProtectReferences(std::array{thread, callback});
    static_cast<void>(f.Call(thread, "start", "()V"));
    static_cast<void>(f.Call(thread, "join", "()V"));
    CHECK(*rejected);
    REQUIRE(f.f.context->display_receivers.size() == 1); // Rejected registration added nothing.
    const auto hits = f.f.ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().id == "dexvm.choreographer_frame_source");
    static_cast<void>(f.f.app->Stop());
}
