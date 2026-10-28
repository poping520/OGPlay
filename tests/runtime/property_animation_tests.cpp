#include "native_library_fixture.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <map>
#include <bit>
#include <cmath>

namespace {
using namespace ogplay;
using namespace runtime::dexvm;
struct Animations final {
    test::native_library::OrchestratedApp app;
    Interpreter& vm;
    explicit Animations(InterpreterBackend backend, bool start = true) : app("fixture.WindowAttachmentActivity", true, false, {}, {}, true, backend),
                                                  vm(app.app->DexVm().Vm()) {
        app.app->StartApplication();
        if (start) {
            REQUIRE(app.app->StartLauncherActivity().state == session::LifecycleRunState::running);
            Step();
        }
    }
    VmCallOutcome Outcome(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Model().ObjectClass(receiver);
        const auto slot = vm.Linker().FindVtableIndex(type, name, sig);
        REQUIRE(slot.has_value());
        args.insert(args.begin(), VmValue::Ref(receiver));
        return vm.Call(vm.Linker().Class(type).vtable[*slot], args);
    }
    VmValue Call(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto result = Outcome(receiver, name, sig, std::move(args));
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    VmObjectRef New(const char* type, const char* sig, std::vector<VmValue> args = {}) {
        const auto initialized = vm.EnsureClassInitialized(vm.Linker().ResolveDescriptor(type));
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        const auto object = vm.NewIntrinsicInstance(type);
        const auto roots = vm.ProtectReferences(std::array{object});
        const auto init = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(object), "<init>", sig);
        REQUIRE(init.has_value());
        args.insert(args.begin(), VmValue::Ref(object));
        const auto result = vm.Call(*init, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return object;
    }
    VmCallOutcome StaticOutcome(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto init = vm.EnsureClassInitialized(type);
        if (init.exception.IsValid()) return init;
        const auto method = vm.Linker().FindDirectMethod(type, name, sig);
        REQUIRE(method.has_value());
        return vm.Call(*method, args);
    }
    VmValue Static(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto result = StaticOutcome(owner, name, sig, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }
    VmObjectRef Floats(std::initializer_list<float> values) {
        const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[F"), runtime::JniPrimitiveKind::float_value, static_cast<int>(values.size()));
        int i = 0;
        for (float value : values) vm.Model().SetPrimitiveElement(array, i++, std::bit_cast<std::uint32_t>(value));
        return array;
    }
    VmObjectRef Animate(VmObjectRef target, const char* property, std::initializer_list<float> values) {
        const auto array = Floats(values);
        const auto name = vm.NewStringUtf8(property);
        const auto roots = vm.ProtectReferences(std::array{array, name});
        return Static("Landroid/animation/ObjectAnimator;", "ofFloat", "(Ljava/lang/Object;Ljava/lang/String;[F)Landroid/animation/ObjectAnimator;",
            {VmValue::Ref(target), VmValue::Ref(name), VmValue::Ref(array)}).ref;
    }
    void Duration(VmObjectRef animator) {
        Call(animator, "setDuration", "(J)Landroid/animation/ObjectAnimator;", {VmValue::Long(600)});
    }
    VmObjectRef Owner() { return app.context->activity; }
    VmObjectRef Manager() { return Call(Owner(), "getWindowManager", "()Landroid/view/WindowManager;").ref; }
    VmObjectRef View() { return New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(Owner())}); }
    VmObjectRef Params(int flags = 0) {
        return New("Landroid/view/WindowManager$LayoutParams;", "(III)V",
                   {VmValue::Int(99), VmValue::Int(flags), VmValue::Int(-3)});
    }
    Slot& Field(VmObjectRef object, const char* name, const char* type = "I") {
        const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
        REQUIRE(field.has_value());
        return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
    }
    void Int(VmObjectRef object, const char* name, int value) { Field(object, name) = {static_cast<std::uint32_t>(value), SlotTag::cat1}; }
    void Add(VmObjectRef view, VmObjectRef params) {
        static_cast<void>(Call(Manager(), "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                               {VmValue::Ref(view), VmValue::Ref(params)}));
    }
    void Remove(VmObjectRef view) { static_cast<void>(Call(Manager(), "removeView", "(Landroid/view/View;)V", {VmValue::Ref(view)})); }
    void Update(VmObjectRef view, VmObjectRef params) {
        static_cast<void>(Call(Manager(), "updateViewLayout", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                               {VmValue::Ref(view), VmValue::Ref(params)}));
    }
    void Step() { static_cast<void>(app.app->ActivityLifecycle().StepFrame()); }
    void Launch() {
        const auto intent = New("Landroid/content/Intent;", "()V");
        const auto roots = vm.ProtectReferences(std::array{intent});
        static_cast<void>(Call(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                               {VmValue::Ref(vm.NewStringUtf8("fixture")), VmValue::Ref(vm.NewStringUtf8("fixture.ResultChild"))}));
        static_cast<void>(Call(Owner(), "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)}));
        Step();
    }
    void Hook(const char* type, const char* name, const char* sig, IntrinsicHandler handler) {
        const auto owner = vm.Linker().ResolveDescriptor(type);
        const auto slot = vm.Linker().FindVtableIndex(owner, name, sig);
        REQUIRE(slot.has_value());
        auto& method = vm.Linker().MutableMethod(vm.Linker().Class(owner).vtable[*slot]);
        method.kind = MethodKind::intrinsic;
        method.implementation = std::move(handler);
    }
    void Error(const VmCallOutcome& result, const char* type, const char* text) {
        REQUIRE(result.exception.IsValid());
        INFO(result.exception_message);
        CHECK(vm.Linker().Class(result.exception_class).descriptor == std::string(type));
        CHECK(result.exception_message.find(text) != std::string::npos);
    }
};
}

TEST_CASE("DVM-337 ROM alpha animation advances from reflected getter on presentation frames") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Animations f(backend);
        const auto view = f.View();
        const auto params = f.Params();
        f.Add(view, params);
        f.Step();
        f.Call(view, "setAlpha", "(F)V", {VmValue::Float(0.8F)});
        const auto animator = f.Animate(view, "alpha", {0.0F});
        const auto listener = f.New("Lfixture/AnimationProbe;", "()V");
        const auto interpolation = f.New("Landroid/view/animation/AccelerateInterpolator;", "()V");
        const auto roots = f.vm.ProtectReferences(std::array{view, params, animator, listener, interpolation});
        std::vector<char> events;
        f.Hook("Lfixture/AnimationProbe;", "onAnimationStart", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) { events.push_back('S'); return VmValue::Void(); });
        f.Hook("Lfixture/AnimationProbe;", "onAnimationEnd", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) { events.push_back('E'); return VmValue::Void(); });
        f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(listener)});
        f.Call(animator, "setInterpolator", "(Landroid/animation/TimeInterpolator;)V", {VmValue::Ref(interpolation)});
        f.Duration(animator);
        f.Call(animator, "start", "()V");
        CHECK(f.Call(view, "getAlpha", "()F").AsFloat() == doctest::Approx(0.8));
        const auto start = f.app.context->uptime_millis.load();
        bool intermediate = false;
        for (int i = 0; i < 42; ++i) {
            const auto time = f.app.context->uptime_millis.load();
            f.Step();
            const float fraction = std::clamp(static_cast<float>(time - start) / 600.0F, 0.0F, 1.0F);
            const auto alpha = f.Call(view, "getAlpha", "()F").AsFloat();
            CHECK(alpha == doctest::Approx(0.8F * (1.0F - fraction * fraction)).epsilon(0.001));
            if (fraction < 1.0F) CHECK(events == std::vector<char>{'S'});
            else CHECK(events == std::vector<char>{'S', 'E'});
            const auto info = VmObjectRef{static_cast<std::uint32_t>(f.Field(view, "mTransformationInfo", "Landroid/view/View$TransformationInfo;").bits)};
            CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(f.Field(info, "mAlpha", "F").bits)) == alpha);
            const auto node = runtime::FindViewUiNode(*f.app.context, view.Value());
            REQUIRE(node);
            const auto snapshot = runtime::AcquireAndroidPresentation(*f.app.context);
            CHECK(snapshot->application_windows.front().projection->Get(*node)->alpha == alpha);
            intermediate |= alpha > 0.05F && alpha < 0.7F;
        }
        CHECK(intermediate);
        CHECK(events == std::vector<char>{'S', 'E'});
        CHECK(f.app.ledger.Unimplemented().empty());
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-337 PVH method IDs inherit reuse virtually dispatch and propagate errors") {
    Animations f(InterpreterBackend::switch_dispatch);
    constexpr auto pvh = "Landroid/animation/PropertyValuesHolder;";
    const auto view = f.New("Lfixture/AlphaProbe;", "(Landroid/content/Context;)V", {VmValue::Ref(f.Owner())});
    const auto type = f.vm.Model().ClassObject(f.vm.Linker().ResolveDescriptor("Landroid/view/View;"));
    const auto name = f.vm.NewStringUtf8("setAlpha");
    const auto roots = f.vm.ProtectReferences(std::array{view, type, name});
    const auto id = f.Static(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I", {VmValue::Ref(type), VmValue::Ref(name)}).AsInt();
    CHECK(id > 0);
    CHECK(f.Static(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I", {VmValue::Ref(type), VmValue::Ref(name)}).AsInt() == id);
    int count = 0;
    f.Hook("Lfixture/AlphaProbe;", "setAlpha", "(F)V", [&](IntrinsicContext&) { ++count; throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "setter failed"}; return VmValue::Void(); });
    const auto thrown = f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V", {VmValue::Ref(view), VmValue::Int(id), VmValue::Float(0.4F)});
    f.Error(thrown, "Ljava/lang/IllegalStateException;", "setter failed");
    CHECK(count == 1);
    const auto missing = f.StaticOutcome(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I", {VmValue::Ref(type), VmValue::Ref(f.vm.NewStringUtf8("missingSetter"))});
    f.Error(missing, "Ljava/lang/NoSuchMethodError;", "missingSetter");
    f.Error(f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V", {VmValue::Ref(view), VmValue::Int(999), VmValue::Float(0)}), "Ljava/lang/IllegalArgumentException;", "Invalid");
    const auto int_id = f.Static(pvh, "nGetIntMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
        {VmValue::Ref(type), VmValue::Ref(f.vm.NewStringUtf8("setId"))}).AsInt();
    f.Static(pvh, "nCallIntMethod", "(Ljava/lang/Object;II)V", {VmValue::Ref(view), VmValue::Int(int_id), VmValue::Int(123)});
    CHECK(f.Call(view, "getId", "()I").AsInt() == 123);
    const auto wrong_target = f.New("Ljava/lang/Object;", "()V");
    f.Error(f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V", {VmValue::Ref(wrong_target), VmValue::Int(id), VmValue::Float(0)}), "Ljava/lang/IllegalArgumentException;", "mismatch");
    const auto null_animator = f.Animate(VmObjectRef{}, "alpha", {0});
    f.Error(f.Outcome(null_animator, "start", "()V"), "Ljava/lang/NullPointerException;", "");
    const auto animator = f.Animate(view, "alpha", {0});
    const auto worker = f.vm.CreateExecutionContext();
    f.vm.Threads().RegisterNativeContext(worker, false, "animation-no-looper");
    std::uint64_t token{};
    f.vm.RunInExecutionContext(worker, [&] {
        token = f.vm.CurrentContextToken();
        f.Error(f.Outcome(animator, "start", "()V"), "Landroid/util/AndroidRuntimeException;", "Looper");
    });
    f.vm.Threads().DetachNativeContext(token);
    f.vm.DiscardExecutionContext(worker);
    static_cast<void>(f.app.app->Stop());
}

TEST_CASE("DVM-337 ROM update listeners cancel and end order and reflection fallback") {
    Animations f(InterpreterBackend::switch_dispatch);
    const auto target = f.New("Lfixture/AnimationProbe;", "()V");
    const auto animator = f.Animate(target, "value", {0.0F}); // No primitive setter: ROM catches native failure and tries reflection.
    const auto roots = f.vm.ProtectReferences(std::array{target, animator});
    int writes = 0, updates = 0, reflection_attempts = 0;
    const auto class_type = f.vm.Linker().ResolveDescriptor("Ljava/lang/Class;");
    const auto lookup_slot = f.vm.Linker().FindVtableIndex(class_type, "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    REQUIRE(lookup_slot);
    const auto original_lookup = f.vm.Linker().Method(f.vm.Linker().Class(class_type).vtable[*lookup_slot]).implementation;
    f.Hook("Ljava/lang/Class;", "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        [&, original_lookup](IntrinsicContext& call) {
            if (f.vm.StringUtf8(call.arguments[0].ref) == "setValue") ++reflection_attempts;
            return original_lookup(call);
        });
    std::vector<char> events;
    f.Hook("Lfixture/AnimationProbe;", "setValue", "(Ljava/lang/Float;)V", [&](IntrinsicContext&) { ++writes; return VmValue::Void(); });
    for (const auto [name, event] : {std::pair{"onAnimationStart", 'S'}, {"onAnimationCancel", 'C'}, {"onAnimationEnd", 'E'}})
        f.Hook("Lfixture/AnimationProbe;", name, "(Landroid/animation/Animator;)V", [&, event](IntrinsicContext&) { events.push_back(event); return VmValue::Void(); });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationUpdate", "(Landroid/animation/ValueAnimator;)V", [&](IntrinsicContext&) { ++updates; return VmValue::Void(); });
    f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(target)});
    f.Call(animator, "addUpdateListener", "(Landroid/animation/ValueAnimator$AnimatorUpdateListener;)V", {VmValue::Ref(target)});
    f.Duration(animator);
    f.Call(animator, "start", "()V");
    f.Step(); f.Step();
    CHECK(reflection_attempts > 0);
    CHECK(writes == 0); // ROM primitive float lookup cannot use a boxed-only setter.
    CHECK(updates > 0);
    f.Call(animator, "cancel", "()V");
    CHECK(events == std::vector<char>{'S', 'C', 'E'});
    events.clear();
    f.Call(animator, "start", "()V");
    f.Call(animator, "end", "()V");
    CHECK(events == std::vector<char>{'S', 'E'});
    static_cast<void>(f.app.app->Stop());
}

TEST_CASE("DVM-337 translucent window uses pixel opacity opaque window covers lower pixels") {
    for (const int format : {-3, -2, -1}) {
        Animations f(InterpreterBackend::switch_dispatch);
        const auto view = f.New("Landroid/widget/ImageView;", "(Landroid/content/Context;)V", {VmValue::Ref(f.Owner())});
        const auto params = f.Params();
        f.Int(params, "format", format);
        const auto color = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V", {VmValue::Int(static_cast<int>(0xffff0000U))});
        f.Call(view, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(color)});
        f.Call(view, "setAlpha", "(F)V", {VmValue::Float(0.5F)});
        f.Add(view, params); f.Step();
        const auto snapshot = runtime::AcquireAndroidPresentation(*f.app.context);
        const auto w = static_cast<unsigned>(snapshot->viewport.width), h = static_cast<unsigned>(snapshot->viewport.height);
        runtime::AndroidBoundaryFrame frame{.width=w, .height=h, .rgba8=std::vector<std::uint8_t>(w*h*4, 0)};
        for (std::size_t i = 0; i < frame.rgba8.size(); i+=4) { frame.rgba8[i+2]=255; frame.rgba8[i+3]=255; }
        const auto result = f.app.app->ActivityLifecycle().ComposePresentedFrame(frame);
        CHECK(result.rgba8[0] == 128);
        CHECK(result.rgba8[2] == (format == -1 ? 0 : 127));
        CHECK(result.rgba8[3] == 255);
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-337 ancestor alpha freezes on redraw without measurement and honors onSetAlpha") {
    Animations f(InterpreterBackend::switch_dispatch);
    const auto parent = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(f.Owner())});
    const auto child = f.View();
    const auto params = f.Params();
    const auto roots = f.vm.ProtectReferences(std::array{parent, child, params});
    f.Call(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
    f.Add(parent, params); f.Step();
    int measures = 0;
    f.Hook("Landroid/view/View;", "onMeasure", "(II)V", [&](IntrinsicContext&) { ++measures; return VmValue::Void(); });
    f.Call(parent, "setAlpha", "(F)V", {VmValue::Float(0.5F)});
    f.Call(child, "setAlpha", "(F)V", {VmValue::Float(0.4F)});
    f.Step();
    CHECK(measures == 0);
    const auto node = runtime::FindViewUiNode(*f.app.context, child.Value());
    REQUIRE(node);
    CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.front().projection->Get(*node)->alpha == doctest::Approx(0.2));
    f.Hook("Landroid/view/View;", "onSetAlpha", "(I)Z", [](IntrinsicContext&) { return VmValue::Int(1); });
    f.Call(child, "setAlpha", "(F)V", {VmValue::Float(0.3F)});
    CHECK((f.Field(child, "mPrivateFlags").bits & 0x40000) != 0);
    f.Step();
    CHECK(measures == 0);
    CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.front().projection->Get(*node)->alpha == doctest::Approx(0.5));
    const auto info = VmObjectRef{static_cast<std::uint32_t>(f.Field(child, "mTransformationInfo", "Landroid/view/View$TransformationInfo;").bits)};
    f.Field(info, "mScaleX", "F") = {std::bit_cast<std::uint32_t>(2.0F), SlotTag::cat1};
    f.Error(f.Outcome(child, "setAlpha", "(F)V", {VmValue::Float(0.25F)}), "Ljava/lang/UnsupportedOperationException;", "Only View alpha");
    CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.front().projection->Get(*node)->alpha == doctest::Approx(0.5));
    f.Field(info, "mScaleX", "F") = {std::bit_cast<std::uint32_t>(1.0F), SlotTag::cat1};
    static_cast<void>(f.app.app->Stop());
}

TEST_CASE("DVM-337 ValueAnimator updates are frame driven and process stop synthesizes no end") {
    Animations f(InterpreterBackend::switch_dispatch);
    const auto values = f.Floats({0.0F, 1.0F});
    const auto animator = f.Static("Landroid/animation/ValueAnimator;", "ofFloat", "([F)Landroid/animation/ValueAnimator;", {VmValue::Ref(values)}).ref;
    const auto listener = f.New("Lfixture/AnimationProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{values, animator, listener});
    int updates = 0, ends = 0, cancels = 0;
    float value = 0;
    f.Hook("Lfixture/AnimationProbe;", "onAnimationUpdate", "(Landroid/animation/ValueAnimator;)V", [&](IntrinsicContext& call) {
        ++updates;
        const auto boxed = f.Call(call.arguments[0].ref, "getAnimatedValue", "()Ljava/lang/Object;").ref;
        value = f.Call(boxed, "floatValue", "()F").AsFloat();
        return VmValue::Void();
    });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationEnd", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) { ++ends; return VmValue::Void(); });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationCancel", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) { ++cancels; return VmValue::Void(); });
    f.Call(animator, "addUpdateListener", "(Landroid/animation/ValueAnimator$AnimatorUpdateListener;)V", {VmValue::Ref(listener)});
    f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(listener)});
    f.Call(animator, "setDuration", "(J)Landroid/animation/ValueAnimator;", {VmValue::Long(600)});
    f.Call(animator, "start", "()V");
    CHECK(ends == 0);
    const int initial = updates;
    for (int i = 0; i < 15; ++i) f.Step();
    CHECK(updates > initial);
    CHECK(value > 0.0F); CHECK(value < 1.0F);
    CHECK(ends == 0);
    static_cast<void>(f.app.app->Stop());
    const auto stopped_updates = updates;
    runtime::DispatchAndroidDisplayFrame(f.vm, *f.app.context);
    CHECK(updates == stopped_updates);
    CHECK(ends == 0); CHECK(cancels == 0);
}

TEST_CASE("DVM-337 animation end removes child window and restores the focused Activity") {
    Animations f(InterpreterBackend::switch_dispatch);
    const auto child = f.View(), params = f.Params();
    const auto animator = f.Animate(child, "alpha", {0.0F});
    const auto listener = f.New("Lfixture/AnimationProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{child, params, animator, listener});
    std::vector<bool> focus;
    f.Hook("Lfixture/WindowAttachmentActivity;", "onWindowFocusChanged", "(Z)V", [&](IntrinsicContext& call) {
        focus.push_back(call.arguments[0].AsInt() != 0);
        return VmValue::Void();
    });
    int ended = 0;
    f.Hook("Lfixture/AnimationProbe;", "onAnimationEnd", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) {
        CHECK(f.Call(child, "getAlpha", "()F").AsFloat() == 0.0F);
        ++ended; f.Remove(child);
        return VmValue::Void();
    });
    f.Add(child, params); f.Step();
    CHECK_FALSE(f.app.context->window_has_focus.load());
    f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(listener)});
    f.Duration(animator); f.Call(animator, "start", "()V");
    for (int i = 0; i < 45; ++i) f.Step();
    CHECK(ended == 1);
    CHECK(f.app.context->application_windows.empty());
    CHECK(f.app.context->window_has_focus.load());
    CHECK(focus == std::vector<bool>{false, true});
    CHECK_FALSE(f.app.context->focused_application_window.IsValid());
    CHECK(f.app.ledger.Unimplemented().empty());
    static_cast<void>(f.app.app->Stop());
}
