#include <doctest/doctest.h>
#include <limits>
#include <thread>

#include "android_value_fixture.h"
#include "runtime/integration/dexvm_android/rom_input.h"
#include "runtime/integration/dexvm_android/velocity_tracker.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using namespace ogplay::runtime::android_intrinsics;
using namespace ogplay::test::android_value;
using V = VmValue;

namespace {
constexpr auto surface = "Landroid/view/Surface;", control = "Landroid/view/SurfaceControl;";
const auto null = V::Ref(VmObjectRef{});

VmObjectRef Service(AndroidValueVm& f, const char* name) {
    return f
        .Static("Landroid/os/ServiceManager;", "getService", "(Ljava/lang/String;)Landroid/os/IBinder;",
                {V::Ref(f.vm.NewStringUtf8(name))})
        .ref;
}

void Int(AndroidValueVm& f, VmObjectRef ref, const char* name, int value) {
    WindowField(f.vm, ref, name, "I") = {static_cast<unsigned>(value), SlotTag::cat1};
}

std::unique_ptr<AndroidValueVm> WindowVm(InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                                         std::vector<IntrinsicClassDecl> extras = {}) {
    auto client =
        IntrinsicClassBuilder::Class("Lprobe/WindowImeClient;", "Lcom/android/internal/view/IInputMethodClient$Stub;");
    client.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    extras.push_back(std::move(client).Build());
    return std::make_unique<AndroidValueVm>(backend, extras, false, nullptr);
}

VmObjectRef OpenSession(AndroidValueVm& f, VmObjectRef wms) {
    const auto client = f.New("Lprobe/WindowImeClient;");
    const auto root = f.vm.ProtectReferences(std::array{client});
    return f
        .On(wms, "openSession",
            "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;)Landroid/view/IWindowSession;",
            {V::Ref(client), null})
        .ref;
}

IntrinsicClassDecl WindowProbe() {
    auto b = IntrinsicClassBuilder::Class("Lprobe/Window;", "Landroid/view/IWindow$Stub;");
    b.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    b.VirtualMethod("windowFocusChanged", "(ZZ)V", [](IntrinsicContext&) { return V::Void(); });
    return std::move(b).Build();
}
}  // namespace

TEST_CASE("P3 local WMS opens a rooted session and supplies display facts") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto fixture = WindowVm(backend);
        auto& f = *fixture;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto wms = Service(f, "window");
        const auto roots = f.vm.ProtectReferences(std::array{wms});
        const auto point = f.New("Landroid/graphics/Point;");
        const auto p = f.vm.ProtectReferences(std::array{point});
        f.On(wms, "getInitialDisplaySize", "(ILandroid/graphics/Point;)V", {V::Int(0), V::Ref(point)});
        CHECK(WindowField(f.vm, point, "x", "I").bits == f.context->surface_width);
        CHECK(f.On(wms, "getInitialDisplayDensity", "(I)I", {V::Int(0)}).AsInt() == DeviceDensityDpi(f.context));
        CHECK(f.On(wms, "hasNavigationBar", "()Z").AsInt() == 0);
        CHECK(Service(f, "input").IsValid());
        CHECK(Service(f, "input_method").IsValid());
        const auto session = OpenSession(f, wms);
        REQUIRE(session.IsValid());
        CHECK(f.vm.MarkReachable().IsMarked(session));
        f.On(session, "setInTouchMode", "(Z)V", {V::Int(0)});
        CHECK(f.On(session, "getInTouchMode", "()Z").AsInt() == 0);
    }
}

TEST_CASE("P3 local WMS add relayout and remove use one Surface and InputChannel") {
    auto fixture = WindowVm(InterpreterBackend::threaded, {WindowProbe()});
    auto& f = *fixture;
    f.context->surface_width = 800;
    f.context->surface_height = 480;
    auto wms = Service(f, "window");
    auto session = OpenSession(f, wms);
    auto window = f.New("Lprobe/Window;");
    auto attrs = f.New("Landroid/view/WindowManager$LayoutParams;");
    auto inset = f.New("Landroid/graphics/Rect;");
    auto channel = f.New("Landroid/view/InputChannel;");
    const auto roots = f.vm.ProtectReferences(std::array{session, window, attrs, inset, channel});
    Int(f, attrs, "width", 100);
    Int(f, attrs, "height", 80);
    Int(f, attrs, "gravity", 0x11);
    const auto added =
        f.On(session, "add",
             "(Landroid/view/IWindow;ILandroid/view/WindowManager$LayoutParams;ILandroid/graphics/Rect;Landroid/view/InputChannel;)I",
             {V::Ref(window), V::Int(0), V::Ref(attrs), V::Int(0), V::Ref(inset), V::Ref(channel)})
            .AsInt();
    CHECK((added & 2) == 2);
    CHECK(WindowField(f.vm, channel, "mPtr", "I").bits != 0);
    CHECK(f.context->rom_windows->system.Focus().value == 0);
    const auto frame = f.New("Landroid/graphics/Rect;"), config = f.New("Landroid/content/res/Configuration;"),
               output = f.New(surface);
    const auto out_roots = f.vm.ProtectReferences(std::array{frame, config, output});
    const auto relayout =
        f.On(session, "relayout",
             "(Landroid/view/IWindow;ILandroid/view/WindowManager$LayoutParams;IIIILandroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/content/res/Configuration;Landroid/view/Surface;)I",
             {V::Ref(window), V::Int(0), V::Ref(attrs), V::Int(100), V::Int(80), V::Int(0), V::Int(0), V::Ref(frame),
              V::Ref(inset), V::Ref(inset), V::Ref(inset), V::Ref(config), V::Ref(output)})
            .AsInt();
    CHECK((relayout & 2) == 2);
    CHECK(f.context->rom_windows->system.Focus().value != 0);
    const auto token = static_cast<int>(WindowField(f.vm, output, "mNativeObject", "I").bits);
    CHECK(f.Static(surface, "nativeIsValid", "(I)Z", {V::Int(token)}).AsInt() == 1);
    f.On(session, "finishDrawing", "(Landroid/view/IWindow;)V", {V::Ref(window)});
    f.On(session, "remove", "(Landroid/view/IWindow;)V", {V::Ref(window)});
    CHECK(f.Static(surface, "nativeIsValid", "(I)Z", {V::Int(token)}).AsInt() == 0);
    CHECK(f.context->rom_windows->system.Compose().blank);
}

TEST_CASE("P3 ROM Surface native dirty lock retains pixels and publishes immutable frames") {
    auto fixture = WindowVm();
    auto& f = *fixture;
    f.context->surface_width = 4;
    f.context->surface_height = 4;
    const auto session = f.New("Landroid/view/SurfaceSession;");
    const auto session_root = f.vm.ProtectReferences(std::array{session});
    const auto handle =
        f.Static(control, "nativeCreate", "(Landroid/view/SurfaceSession;Ljava/lang/String;IIII)I",
                 {V::Ref(session), V::Ref(f.vm.NewStringUtf8("software")), V::Int(4), V::Int(4), V::Int(1), V::Int(0)})
            .AsInt();
    const auto token = f.Static(surface, "nativeCreateFromSurfaceControl", "(I)I", {V::Int(handle)}).AsInt();
    auto canvas = f.New("Landroid/graphics/Canvas;"),
         dirty = f.New("Landroid/graphics/Rect;", "(IIII)V", {V::Int(0), V::Int(0), V::Int(1), V::Int(1)});
    const auto roots = f.vm.ProtectReferences(std::array{canvas, dirty});
    auto lock = [&] {
        return f
            .Static(surface, "nativeLockCanvas", "(ILandroid/graphics/Canvas;Landroid/graphics/Rect;)I",
                    {V::Int(token), V::Ref(canvas), V::Ref(dirty)})
            .AsInt();
    };
    auto post = [&](int locked) {
        f.Static(surface, "nativeUnlockCanvasAndPost", "(ILandroid/graphics/Canvas;)V",
                 {V::Int(locked), V::Ref(canvas)});
        f.Static(surface, "nativeRelease", "(I)V", {V::Int(locked)});
    };
    int locked = lock();
    CHECK(WindowField(f.vm, dirty, "right", "I").bits == 4);
    f.On(canvas, "drawColor", "(I)V", {V::Int(static_cast<int>(0xffff0000U))});
    post(locked);
    const auto old = f.context->rom_windows->system.Layers().front().pixels;
    REQUIRE(old->rgba8[0] == 255);
    Int(f, dirty, "right", 1);
    Int(f, dirty, "bottom", 1);
    locked = lock();
    CHECK(f.context->rom_windows->system.Layers().front().pixels == old);
    f.On(canvas, "drawColor", "(I)V", {V::Int(static_cast<int>(0xff0000ffU))});
    post(locked);
    const auto next = f.context->rom_windows->system.Layers().front().pixels;
    CHECK(next != old);
    CHECK(old->rgba8[0] == 255);
    CHECK(next->rgba8[2] == 255);
    CHECK(next->rgba8[4] == 255);
    CHECK(next->rgba8[6] == 0);
    f.Static(surface, "nativeRelease", "(I)V", {V::Int(token)});
    f.Static(control, "nativeDestroy", "(I)V", {V::Int(handle)});
}

TEST_CASE("P3 VelocityTracker ROM computes API19 lsq2 and preserves lift velocity") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto fixture = WindowVm(backend);
        auto& f = *fixture;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto tracker =
            f.Static("Landroid/view/VelocityTracker;", "obtain", "()Landroid/view/VelocityTracker;").ref;
        const auto root = f.vm.ProtectReferences(std::array{tracker});
        for (int i = 0; i < 4; ++i) {
            AndroidBoundaryInput input;
            input.type = AndroidBoundaryInputType::pointer_motion;
            input.action = i == 0 ? 0 : 2;
            input.event_time_ns = static_cast<std::int64_t>(i) * 10'000'000;
            input.source = 0x1002;
            input.pointers = {AndroidInputPointer{}};
            input.pointers[0].axes[0] = static_cast<float>(i) * 10;
            input.pointers[0].axes[1] = static_cast<float>(i) * 20;
            const auto event = MakeInputEvent(f.vm, f.context, input);
            f.On(tracker, "addMovement", "(Landroid/view/MotionEvent;)V", {V::Ref(event)});
        }
        f.On(tracker, "computeCurrentVelocity", "(IF)V", {V::Int(1000), V::Float(1500)});
        CHECK(f.On(tracker, "getXVelocity", "()F").AsFloat() == doctest::Approx(1000));
        CHECK(f.On(tracker, "getYVelocity", "()F").AsFloat() == doctest::Approx(1500));
        const auto estimator = f.New("Landroid/view/VelocityTracker$Estimator;");
        CHECK(f.On(tracker, "getEstimator", "(ILandroid/view/VelocityTracker$Estimator;)Z",
                   {V::Int(0), V::Ref(estimator)})
                  .AsInt() == 1);
        CHECK(WindowField(f.vm, estimator, "degree", "I").bits == 2);
        f.On(tracker, "clear", "()V");
        CHECK(f.On(tracker, "getXVelocity", "()F").AsFloat() == 0);
        f.On(tracker, "recycle", "()V");
    }
}

TEST_CASE("P3 InputEventReceiver dispatch and Choreographer batch consumption use the queue owner") {
    struct Observed {
        std::vector<int> actions, histories;
        std::vector<int> keys, system_keys, default_actions;
        int batches{};
        std::uint64_t thread{};
    };

    auto observed = std::make_shared<Observed>();
    auto b = IntrinsicClassBuilder::Class("Lprobe/InputReceiver;", "Landroid/view/InputEventReceiver;");
    b.Constructor("(Landroid/view/InputChannel;Landroid/os/Looper;)V", [](IntrinsicContext& c) {
        const auto ctor =
            c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/view/InputEventReceiver;"),
                                           "<init>", "(Landroid/view/InputChannel;Landroid/os/Looper;)V");
        const auto out = c.vm.Call(*ctor, std::vector<V>{V::Ref(c.receiver), c.arguments[0], c.arguments[1]});
        if (out.exception.IsValid()) throw VmJavaThrow{"", "", out.exception};
        return V::Void();
    });
    b.VirtualMethod("onInputEvent", "(Landroid/view/InputEvent;)V", [observed](IntrinsicContext& c) {
        observed->thread = c.vm.CurrentContextToken();
        if (c.vm.Linker().Class(c.vm.Model().ObjectClass(c.arguments[0].ref)).descriptor == "Landroid/view/KeyEvent;") {
            observed->keys.push_back(CallAndroidMethod(c.vm, c.arguments[0].ref, "getKeyCode", "()I").AsInt());
            observed->system_keys.push_back(CallAndroidMethod(c.vm, c.arguments[0].ref, "isSystem", "()Z").AsInt());
            observed->default_actions.push_back(
                CallAndroidMethod(c.vm, c.arguments[0].ref, "hasDefaultAction", "()Z").AsInt());
        } else {
            observed->actions.push_back(CallAndroidMethod(c.vm, c.arguments[0].ref, "getAction", "()I").AsInt());
            observed->histories.push_back(CallAndroidMethod(c.vm, c.arguments[0].ref, "getHistorySize", "()I").AsInt());
        }
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "finishInputEvent", "(Landroid/view/InputEvent;Z)V",
                                            {c.arguments[0], V::Int(1)}));
        return V::Void();
    });
    b.VirtualMethod("onBatchedInputEventPending", "()V", [observed](IntrinsicContext&) {
        ++observed->batches;
        return V::Void();
    });
    auto fixture = WindowVm(InterpreterBackend::threaded, {std::move(b).Build()});
    auto& f = *fixture;
    f.context->surface_width = 100;
    f.context->surface_height = 100;
    PrepareAndroidMainLooper(f.vm, f.context);
    auto looper = f.Static("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
    auto channel = f.New("Landroid/view/InputChannel;");
    const auto roots = f.vm.ProtectReferences(std::array{channel, looper});
    auto& windows = RomWindows(f.context);
    const auto id = windows.system.Add({}, true);
    const auto server = CreateWindowInputChannel(f.vm, f.context, channel);
    windows.clients.emplace(1, RomWindowState::Client{id, VmObjectRef{}, VmObjectRef{}, 0, server});
    const auto receiver = f.New("Lprobe/InputReceiver;", "(Landroid/view/InputChannel;Landroid/os/Looper;)V",
                                {V::Ref(channel), V::Ref(looper)});
    const auto receiver_root = f.vm.ProtectReferences(std::array{receiver});
    auto dispatch = [&](int action, int time) {
        AndroidBoundaryInput e;
        e.type = AndroidBoundaryInputType::pointer_motion;
        e.action = action;
        e.event_time_ns = static_cast<std::int64_t>(time) * 1'000'000;
        e.event_time_ms = time;
        e.source = 0x1002;
        e.pointers = {AndroidInputPointer{}};
        e.x = static_cast<float>(time);
        e.pointers[0].axes[0] = e.x;
        DispatchRomWindowInput(f.vm, f.context, e);
    };
    dispatch(0, 0);
    PumpRomInputEvents(f.vm, f.context);
    REQUIRE(observed->actions == std::vector<int>{0});
    dispatch(2, 10);
    dispatch(2, 20);
    PumpRomInputEvents(f.vm, f.context);
    CHECK(observed->batches == 1);
    CHECK(observed->actions.size() == 1);
    CHECK(f.On(receiver, "consumeBatchedInputEvents", "(J)Z", {V::Long(20'000'000)}).AsInt() == 1);
    CHECK(observed->actions == std::vector<int>{0, 2});
    CHECK(observed->histories.back() == 1);
    CHECK(observed->thread == f.vm.CurrentContextToken());
    dispatch(1, 30);
    PumpRomInputEvents(f.vm, f.context);
    CHECK(observed->actions.back() == 1);
    for (const int code : {4, 29}) {
        AndroidBoundaryInput key;
        key.type = AndroidBoundaryInputType::key;
        key.code = code;
        key.pressed = true;
        DispatchRomWindowInput(f.vm, f.context, key);
        PumpRomInputEvents(f.vm, f.context);
    }
    CHECK(observed->keys == std::vector<int>{4, 29});
    CHECK(observed->system_keys == std::vector<int>{1, 0});
    CHECK(observed->default_actions == std::vector<int>{1, 0});
    CHECK(f.context->rom_input->receivers.begin()->second.inflight.empty());
    f.On(receiver, "dispose", "()V");
    CHECK(f.context->rom_input->receivers.empty());
}

TEST_CASE("P3 input_method transports commit composing hide and SDL activation") {
    struct Text {
        std::u16string committed, composing;
        std::vector<bool> active;
    };

    const auto text = std::make_shared<Text>();
    auto client =
        IntrinsicClassBuilder::Class("Lprobe/ImeClient;", "Lcom/android/internal/view/IInputMethodClient$Stub;");
    client.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    client.VirtualMethod("setActive", "(Z)V", [](IntrinsicContext&) { return V::Void(); });
    auto input = IntrinsicClassBuilder::Class("Lprobe/InputContext;", "Lcom/android/internal/view/IInputContext$Stub;");
    input.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    for (bool composing : {false, true})
        input.VirtualMethod(composing ? "setComposingText" : "commitText", "(Ljava/lang/CharSequence;I)V",
                            [text, composing](IntrinsicContext& c) {
                                const auto value = c.vm.Model().StringValue(c.arguments[0].ref);
                                if (composing)
                                    text->composing = value;
                                else
                                    text->committed = value;
                                return V::Void();
                            });
    auto fixture = WindowVm(InterpreterBackend::threaded, {std::move(client).Build(), std::move(input).Build()});
    auto& f = *fixture;
    auto service = Service(f, "input_method"), ic = f.New("Lprobe/InputContext;"), cl = f.New("Lprobe/ImeClient;");
    const auto roots = f.vm.ProtectReferences(std::array{service, ic, cl});
    f.On(service, "addClient",
         "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;II)V",
         {V::Ref(cl), V::Ref(ic), V::Int(10000), V::Int(1)});
    f.On(
        service, "startInput",
        "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;Landroid/view/inputmethod/EditorInfo;I)Lcom/android/internal/view/InputBindResult;",
        {V::Ref(cl), V::Ref(ic), null, V::Int(0)});
    RomInput(f.context).text_input = {};
    const auto absent = f.OnOutcome(service, "showSoftInput",
                                    "(Lcom/android/internal/view/IInputMethodClient;ILandroid/os/ResultReceiver;)Z",
                                    {V::Ref(cl), V::Int(0), null});
    CHECK(absent.exception.IsValid());
    CHECK_FALSE(f.context->rom_input->ime_visible);
    RomInput(f.context).text_input = [text](bool value) { text->active.push_back(value); };
    CHECK(f.On(service, "showSoftInput",
               "(Lcom/android/internal/view/IInputMethodClient;ILandroid/os/ResultReceiver;)Z",
               {V::Ref(cl), V::Int(0), null})
              .AsInt() == 1);
    CommitRomInputText(f.vm, f.context, u"文字", false);
    CommitRomInputText(f.vm, f.context, u"输入", true);
    CHECK(text->committed == u"文字");
    CHECK(text->composing == u"输入");
    f.On(service, "hideSoftInput", "(Lcom/android/internal/view/IInputMethodClient;ILandroid/os/ResultReceiver;)Z",
         {V::Ref(cl), V::Int(0), null});
    CommitRomInputText(f.vm, f.context, u"ignored", false);
    CHECK(text->committed == u"文字");
    CHECK(text->active == std::vector<bool>{true, false});
}

TEST_CASE("P3 ROM InputQueue native predispatch completes on the queue thread") {
    auto completed = std::make_shared<int>(0);
    auto callback = IntrinsicClassBuilder::Class("Lprobe/FinishedInput;", "Ljava/lang/Object;",
                                                 {"Landroid/view/InputQueue$FinishedInputEventCallback;"});
    callback.Constructor("()V", [](IntrinsicContext&) { return V::Void(); });
    callback.VirtualMethod("onFinishedInputEvent", "(Ljava/lang/Object;Z)V", [completed](IntrinsicContext& c) {
        CHECK(c.arguments[1].AsInt() == 0);
        ++*completed;
        return V::Void();
    });
    auto fixture = WindowVm(InterpreterBackend::threaded, {std::move(callback).Build()});
    auto& f = *fixture;
    PrepareAndroidMainLooper(f.vm, f.context);
    auto queue = f.New("Landroid/view/InputQueue;"), done = f.New("Lprobe/FinishedInput;"),
         event = f.New("Landroid/view/KeyEvent;", "(II)V", {V::Int(0), V::Int(29)});
    const auto roots = f.vm.ProtectReferences(std::array{queue, done, event});
    const auto native = f.context->rom_input->native_queues.at(f.On(queue, "getNativePtr", "()I").AsInt());
    native->native = ogplay::memory::GuestAddress{0x1000};
    native->post = [](const AndroidBoundaryInput& input, std::function<void(bool)> finish) {
        CHECK((input.flags & kAndroidKeyPreDispatchFlag) != 0);
        std::thread consumer([finish = std::move(finish)] { finish(false); });
        consumer.join();
    };
    f.On(queue, "sendInputEvent",
         "(Landroid/view/InputEvent;Ljava/lang/Object;ZLandroid/view/InputQueue$FinishedInputEventCallback;)V",
         {V::Ref(event), null, V::Int(1), V::Ref(done)});
    CHECK(*completed == 0);
    PumpRomInputEvents(f.vm, f.context);
    CHECK(*completed == 1);
    f.On(queue, "dispose", "()V");
    CHECK(f.context->rom_input->native_queues.empty());
}

TEST_CASE("P3 MotionEvent native snapshots preserve axes offsets histories and copies") {
    auto fixture = WindowVm();
    auto& f = *fixture;
    const auto props =
        f.model.NewObjectArray(f.linker.ResolveDescriptor("[Landroid/view/MotionEvent$PointerProperties;"),
                               f.linker.ResolveDescriptor("Landroid/view/MotionEvent$PointerProperties;"), 1);
    const auto p_root = f.vm.ProtectReferences(std::array{props});
    const auto coords =
        f.model.NewObjectArray(f.linker.ResolveDescriptor("[Landroid/view/MotionEvent$PointerCoords;"),
                               f.linker.ResolveDescriptor("Landroid/view/MotionEvent$PointerCoords;"), 1);
    const auto c_root = f.vm.ProtectReferences(std::array{coords});
    auto p = f.New("Landroid/view/MotionEvent$PointerProperties;"),
         c = f.New("Landroid/view/MotionEvent$PointerCoords;");
    f.model.SetObjectElement(props, 0, p);
    f.model.SetObjectElement(coords, 0, c);
    Int(f, p, "id", 3);
    Int(f, p, "toolType", 1);
    f.On(c, "setAxisValue", "(IF)V", {V::Int(0), V::Float(20)});
    constexpr auto cls = "Landroid/view/MotionEvent;";
    const auto token =
        f.Static(
             cls, "nativeInitialize",
             "(IIIIIIIIFFFFJJI[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;)I",
             {V::Int(0), V::Int(0), V::Int(0x1002), V::Int(2), V::Int(0), V::Int(0), V::Int(0), V::Int(0), V::Float(5),
              V::Float(0), V::Float(1), V::Float(1), V::Long(0), V::Long(10'000'000), V::Int(1), V::Ref(props),
              V::Ref(coords)})
            .AsInt();
    CHECK(f.Static(cls, "nativeGetPointerId", "(II)I", {V::Int(token), V::Int(0)}).AsInt() == 3);
    const auto axis = [&](int id, const char* name, int h) {
        return f.Static(cls, name, "(IIII)F", {V::Int(id), V::Int(0), V::Int(0), V::Int(h)}).AsFloat();
    };
    CHECK(axis(token, "nativeGetRawAxisValue", std::numeric_limits<int>::min()) == 15);
    CHECK(axis(token, "nativeGetAxisValue", std::numeric_limits<int>::min()) == 20);
    f.On(c, "setAxisValue", "(IF)V", {V::Int(0), V::Float(30)});
    f.Static(cls, "nativeAddBatch", "(IJ[Landroid/view/MotionEvent$PointerCoords;I)V",
             {V::Int(token), V::Long(20'000'000), V::Ref(coords), V::Int(1)});
    CHECK(axis(token, "nativeGetAxisValue", 0) == 20);
    const auto copy = f.Static(cls, "nativeCopy", "(IIZ)I", {V::Int(0), V::Int(token), V::Int(0)}).AsInt();
    CHECK(f.Static(cls, "nativeGetHistorySize", "(I)I", {V::Int(copy)}).AsInt() == 0);
    CHECK(axis(copy, "nativeGetAxisValue", std::numeric_limits<int>::min()) == 30);
    f.Static(cls, "nativeDispose", "(I)V", {V::Int(token)});
    f.Static(cls, "nativeDispose", "(I)V", {V::Int(copy)});
}

TEST_CASE("P3 host vsync queues a deterministic Clock pulse without running guest callbacks") {
    struct Pulse {
        std::int64_t time{};
        std::uint64_t thread{};
        int count{};
    };

    const auto pulse = std::make_shared<Pulse>();
    auto b = IntrinsicClassBuilder::Class("Lprobe/DisplayReceiver;", "Landroid/view/DisplayEventReceiver;");
    b.Constructor("(Landroid/os/Looper;)V", [](IntrinsicContext& c) {
        const auto ctor = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/view/DisplayEventReceiver;"), "<init>", "(Landroid/os/Looper;)V");
        const auto result = c.vm.Call(*ctor, std::vector<V>{V::Ref(c.receiver), c.arguments[0]});
        if (result.exception.IsValid()) throw VmJavaThrow{"", "", result.exception};
        return V::Void();
    });
    b.VirtualMethod("onVsync", "(JII)V", [pulse](IntrinsicContext& c) {
        pulse->time = c.arguments[0].AsLong();
        pulse->thread = c.vm.CurrentContextToken();
        ++pulse->count;
        return V::Void();
    });
    auto fixture = WindowVm(InterpreterBackend::threaded, {std::move(b).Build()});
    auto& f = *fixture;
    PrepareAndroidMainLooper(f.vm, f.context);
    const auto looper = f.Static("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;").ref;
    const auto receiver = f.New("Lprobe/DisplayReceiver;", "(Landroid/os/Looper;)V", {V::Ref(looper)});
    const auto roots = f.vm.ProtectReferences(std::array{receiver, looper});
    f.On(receiver, "scheduleVsync", "()V");
    const auto before = f.context->uptime_millis.load();
    std::thread host([&] {
        AdvanceAndroidClock(*f.context, 16);
        QueueAndroidDisplayFrame(f.vm.ExecutionLock(), *f.context);
    });
    host.join();
    CHECK(pulse->count == 0);
    PumpNativeLooperEvents(f.vm, *f.context, looper);
    CHECK(pulse->count == 1);
    CHECK(pulse->thread == f.vm.CurrentContextToken());
    CHECK(pulse->time == (before + 16) * 1'000'000);
    f.On(receiver, "dispose", "()V");
}
