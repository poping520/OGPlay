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

    explicit Animations(InterpreterBackend backend, bool start = true)
        : app("fixture.LauncherActivity", true, false, {}, {}, true, backend), vm(app.app->DexVm().Vm()) {
        app.app->ActivityLifecycle().SetRealtimeVideoClock(false);
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
        return app.CallGuest(vm.Linker().Class(type).vtable[*slot], args);
    }

    VmValue Call(VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto result = Outcome(receiver, name, sig, std::move(args));
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }

    VmObjectRef New(const char* type, const char* sig, std::vector<VmValue> args = {}) {
        app.RunOnMainThread([&] {
            const auto initialized = vm.EnsureClassInitialized(vm.Linker().ResolveDescriptor(type));
            REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        });
        const auto object = app.NewGuest(type);
        const auto roots = vm.ProtectReferences(std::array{object});
        const auto init = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(object), "<init>", sig);
        REQUIRE(init.has_value());
        args.insert(args.begin(), VmValue::Ref(object));
        const auto result = app.CallGuest(*init, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return object;
    }

    VmCallOutcome StaticOutcome(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto type = vm.Linker().ResolveDescriptor(owner);
        const auto method = vm.Linker().FindDirectMethod(type, name, sig);
        REQUIRE(method.has_value());
        return app.CallGuest(*method, args);
    }

    VmValue Static(const char* owner, const char* name, const char* sig, std::vector<VmValue> args = {}) {
        const auto result = StaticOutcome(owner, name, sig, args);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }

    VmObjectRef Floats(std::initializer_list<float> values) {
        const auto array =
            vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[F"), runtime::JniPrimitiveKind::float_value,
                                         static_cast<int>(values.size()));
        int i = 0;
        for (float value : values)
            vm.Model().SetPrimitiveElement(array, i++, std::bit_cast<std::uint32_t>(value));
        return array;
    }

    VmObjectRef Animate(VmObjectRef target, const char* property, std::initializer_list<float> values) {
        const auto array = Floats(values);
        const auto name = vm.NewStringUtf8(property);
        const auto roots = vm.ProtectReferences(std::array{array, name});
        return Static("Landroid/animation/ObjectAnimator;", "ofFloat",
                      "(Ljava/lang/Object;Ljava/lang/String;[F)Landroid/animation/ObjectAnimator;",
                      {VmValue::Ref(target), VmValue::Ref(name), VmValue::Ref(array)})
            .ref;
    }

    void Duration(VmObjectRef animator) {
        Call(animator, "setDuration", "(J)Landroid/animation/ObjectAnimator;", {VmValue::Long(600)});
    }

    VmObjectRef Owner() { return app.context->foreground_activity; }

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

    void Int(VmObjectRef object, const char* name, int value) {
        Field(object, name) = {static_cast<std::uint32_t>(value), SlotTag::cat1};
    }

    void Add(VmObjectRef view, VmObjectRef params) {
        static_cast<void>(Call(Manager(), "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                               {VmValue::Ref(view), VmValue::Ref(params)}));
    }

    void Remove(VmObjectRef view) {
        static_cast<void>(Call(Manager(), "removeView", "(Landroid/view/View;)V", {VmValue::Ref(view)}));
    }

    void Update(VmObjectRef view, VmObjectRef params) {
        static_cast<void>(Call(Manager(), "updateViewLayout",
                               "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                               {VmValue::Ref(view), VmValue::Ref(params)}));
    }

    void Step() { static_cast<void>(app.app->ActivityLifecycle().StepFrame()); }

    void Launch() {
        const auto intent = New("Landroid/content/Intent;", "()V");
        const auto roots = vm.ProtectReferences(std::array{intent});
        static_cast<void>(
            Call(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
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
}  // namespace

TEST_CASE("DVM-337 PVH method IDs inherit reuse virtually dispatch and propagate errors") {
    Animations f(InterpreterBackend::switch_dispatch);
    constexpr auto pvh = "Landroid/animation/PropertyValuesHolder;";
    const auto view = f.New("Lfixture/AlphaProbe;", "(Landroid/content/Context;)V", {VmValue::Ref(f.Owner())});
    const auto type = f.vm.Model().ClassObject(f.vm.Linker().ResolveDescriptor("Landroid/view/View;"));
    const auto name = f.vm.NewStringUtf8("setAlpha");
    const auto roots = f.vm.ProtectReferences(std::array{view, type, name});
    const auto id = f.Static(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
                             {VmValue::Ref(type), VmValue::Ref(name)})
                        .AsInt();
    CHECK(id > 0);
    CHECK(f.Static(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
                   {VmValue::Ref(type), VmValue::Ref(name)})
              .AsInt() == id);
    int count = 0;
    f.Hook("Lfixture/AlphaProbe;", "setAlpha", "(F)V", [&](IntrinsicContext&) {
        ++count;
        throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "setter failed"};
        return VmValue::Void();
    });
    const auto thrown = f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V",
                                        {VmValue::Ref(view), VmValue::Int(id), VmValue::Float(0.4F)});
    f.Error(thrown, "Ljava/lang/IllegalStateException;", "setter failed");
    CHECK(count == 1);
    const auto missing = f.StaticOutcome(pvh, "nGetFloatMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
                                         {VmValue::Ref(type), VmValue::Ref(f.vm.NewStringUtf8("missingSetter"))});
    f.Error(missing, "Ljava/lang/NoSuchMethodError;", "missingSetter");
    f.Error(f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V",
                            {VmValue::Ref(view), VmValue::Int(999), VmValue::Float(0)}),
            "Ljava/lang/IllegalArgumentException;", "Invalid");
    const auto int_id = f.Static(pvh, "nGetIntMethod", "(Ljava/lang/Class;Ljava/lang/String;)I",
                                 {VmValue::Ref(type), VmValue::Ref(f.vm.NewStringUtf8("setId"))})
                            .AsInt();
    f.Static(pvh, "nCallIntMethod", "(Ljava/lang/Object;II)V",
             {VmValue::Ref(view), VmValue::Int(int_id), VmValue::Int(123)});
    CHECK(f.Call(view, "getId", "()I").AsInt() == 123);
    const auto wrong_target = f.New("Ljava/lang/Object;", "()V");
    f.Error(f.StaticOutcome(pvh, "nCallFloatMethod", "(Ljava/lang/Object;IF)V",
                            {VmValue::Ref(wrong_target), VmValue::Int(id), VmValue::Float(0)}),
            "Ljava/lang/IllegalArgumentException;", "mismatch");
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
    const auto animator =
        f.Animate(target, "value", {0.0F});  // No primitive setter: ROM catches native failure and tries reflection.
    const auto roots = f.vm.ProtectReferences(std::array{target, animator});
    int writes = 0, updates = 0, reflection_attempts = 0;
    const auto class_type = f.vm.Linker().ResolveDescriptor("Ljava/lang/Class;");
    const auto lookup_slot = f.vm.Linker().FindVtableIndex(
        class_type, "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    REQUIRE(lookup_slot);
    const auto original_lookup =
        f.vm.Linker().Method(f.vm.Linker().Class(class_type).vtable[*lookup_slot]).implementation;
    f.Hook("Ljava/lang/Class;", "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
           [&, original_lookup](IntrinsicContext& call) {
               if (f.vm.StringUtf8(call.arguments[0].ref) == "setValue") ++reflection_attempts;
               return original_lookup(call);
           });
    std::vector<char> events;
    f.Hook("Lfixture/AnimationProbe;", "setValue", "(Ljava/lang/Float;)V", [&](IntrinsicContext&) {
        ++writes;
        return VmValue::Void();
    });
    for (const auto [name, event] :
         {std::pair{"onAnimationStart", 'S'}, {"onAnimationCancel", 'C'}, {"onAnimationEnd", 'E'}})
        f.Hook("Lfixture/AnimationProbe;", name, "(Landroid/animation/Animator;)V", [&, event](IntrinsicContext&) {
            events.push_back(event);
            return VmValue::Void();
        });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationUpdate", "(Landroid/animation/ValueAnimator;)V",
           [&](IntrinsicContext&) {
               ++updates;
               return VmValue::Void();
           });
    f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(target)});
    f.Call(animator, "addUpdateListener", "(Landroid/animation/ValueAnimator$AnimatorUpdateListener;)V",
           {VmValue::Ref(target)});
    f.Duration(animator);
    f.Call(animator, "start", "()V");
    f.Step();
    f.Step();
    CHECK(reflection_attempts > 0);
    CHECK(writes == 0);  // ROM primitive float lookup cannot use a boxed-only setter.
    CHECK(updates > 0);
    f.Call(animator, "cancel", "()V");
    CHECK(events == std::vector<char>{'S', 'C', 'E'});
    events.clear();
    f.Call(animator, "start", "()V");
    f.Call(animator, "end", "()V");
    CHECK(events == std::vector<char>{'S', 'E'});
    static_cast<void>(f.app.app->Stop());
}

TEST_CASE("DVM-337 ValueAnimator updates are frame driven and process stop synthesizes no end") {
    Animations f(InterpreterBackend::switch_dispatch);
    const auto values = f.Floats({0.0F, 1.0F});
    const auto animator = f.Static("Landroid/animation/ValueAnimator;", "ofFloat",
                                   "([F)Landroid/animation/ValueAnimator;", {VmValue::Ref(values)})
                              .ref;
    const auto listener = f.New("Lfixture/AnimationProbe;", "()V");
    const auto roots = f.vm.ProtectReferences(std::array{values, animator, listener});
    int updates = 0, ends = 0, cancels = 0;
    float value = 0;
    f.Hook("Lfixture/AnimationProbe;", "onAnimationUpdate", "(Landroid/animation/ValueAnimator;)V",
           [&](IntrinsicContext& call) {
               ++updates;
               const auto boxed = f.Call(call.arguments[0].ref, "getAnimatedValue", "()Ljava/lang/Object;").ref;
               value = f.Call(boxed, "floatValue", "()F").AsFloat();
               return VmValue::Void();
           });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationEnd", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) {
        ++ends;
        return VmValue::Void();
    });
    f.Hook("Lfixture/AnimationProbe;", "onAnimationCancel", "(Landroid/animation/Animator;)V", [&](IntrinsicContext&) {
        ++cancels;
        return VmValue::Void();
    });
    f.Call(animator, "addUpdateListener", "(Landroid/animation/ValueAnimator$AnimatorUpdateListener;)V",
           {VmValue::Ref(listener)});
    f.Call(animator, "addListener", "(Landroid/animation/Animator$AnimatorListener;)V", {VmValue::Ref(listener)});
    f.Call(animator, "setDuration", "(J)Landroid/animation/ValueAnimator;", {VmValue::Long(600)});
    f.Call(animator, "start", "()V");
    CHECK(ends == 0);
    const int initial = updates;
    for (int i = 0; i < 15; ++i)
        f.Step();
    CHECK(updates > initial);
    CHECK(value > 0.0F);
    CHECK(value < 1.0F);
    CHECK(ends == 0);
    static_cast<void>(f.app.app->Stop());
    const auto stopped_updates = updates;
    runtime::QueueAndroidDisplayFrame(f.vm.ExecutionLock(), *f.app.context);
    CHECK(updates == stopped_updates);
    CHECK(ends == 0);
    CHECK(cancels == 0);
}
