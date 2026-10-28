#include "native_library_fixture.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <map>

namespace {
using namespace ogplay;
using namespace runtime::dexvm;
struct Windows final {
    test::native_library::OrchestratedApp app;
    Interpreter& vm;
    explicit Windows(InterpreterBackend backend, bool start = true) : app("fixture.WindowAttachmentActivity", true, false, {}, {}, true, backend),
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
constexpr auto layout_sig = "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V";
constexpr auto view_sig = "(Landroid/view/View;)V";
}

TEST_CASE("DVM-335 application WindowManager identity validation and bounded layout") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Windows f(backend);
        const auto manager = f.Manager(), view = f.View(), params = f.Params();
        const auto roots = f.vm.ProtectReferences(std::array{manager, view, params});
        CHECK(f.Manager() == manager);
        CHECK(f.Call(f.Owner(), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                     {VmValue::Ref(f.vm.NewStringUtf8("window"))}).ref == manager);
        const auto display = f.Call(manager, "getDefaultDisplay", "()Landroid/view/Display;").ref;
        CHECK(display.IsValid());
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(VmObjectRef{}), VmValue::Ref(params)}),
                "Ljava/lang/IllegalArgumentException;", "view must not be null");
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(VmObjectRef{})}),
                "Ljava/lang/IllegalArgumentException;", "Params must be WindowManager.LayoutParams");
        const auto wrong = f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V", {VmValue::Int(-1), VmValue::Int(-1)});
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(wrong)}),
                "Ljava/lang/IllegalArgumentException;", "Params must be WindowManager.LayoutParams");
        f.Error(f.Outcome(manager, "removeView", view_sig, {VmValue::Ref(view)}),
                "Ljava/lang/IllegalArgumentException;", "not attached to window manager");
        f.Error(f.Outcome(manager, "updateViewLayout", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Ljava/lang/IllegalArgumentException;", "not attached to window manager");
        for (const auto& [field, value] : std::array<std::pair<const char*, int>, 7>{{
                 {"type", 0}, {"type", 1000}, {"type", 2000}, {"width", 100}, {"height", 1}, {"flags", 1}, {"gravity", 17}}}) {
            const auto invalid = f.Params();
            f.Int(invalid, field, value);
            f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(invalid)}),
                    "Ljava/lang/UnsupportedOperationException;", "layout is not supported");
            CHECK(f.app.context->application_windows.empty());
        }
        REQUIRE_FALSE(f.app.ledger.Unimplemented().empty());
        const auto context = f.New("Landroid/content/Context;", "()V");
        const auto global = f.Call(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                    {VmValue::Ref(f.vm.NewStringUtf8("window"))}).ref;
        f.Error(f.Outcome(global, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Landroid/view/WindowManager$BadTokenException;", "is your activity running?");
        const auto other_token = f.vm.NewIntrinsicInstance("Logplay/runtime/ManagedWindowToken;");
        f.Field(params, "token", "Landroid/os/IBinder;") = {other_token.Value(), SlotTag::ref};
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Landroid/view/WindowManager$BadTokenException;", "is your activity running?");
        f.Field(params, "token", "Landroid/os/IBinder;") = {0, SlotTag::ref};
        f.Add(view, params);
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Ljava/lang/IllegalStateException;", "has already been added to the window manager.");
        CHECK_FALSE(f.Call(view, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK(f.Call(view, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == params);
        CHECK(f.Field(params, "token", "Landroid/os/IBinder;").bits == f.app.context->activity_stack.back().app_window_token.Value());
        CHECK(f.Field(params, "mTitle", "Ljava/lang/CharSequence;").bits != 0);
        f.Step();
        const auto old_generation = f.app.context->application_windows.front().generation;
        f.Remove(view);
        f.Step();
        CHECK(f.app.context->application_windows.empty());
        f.Add(view, params);
        f.Step();
        CHECK(f.app.context->application_windows.front().generation > old_generation);
        f.Int(params, "flags", 8 | 16 | 32 | 128 | 256 | 1024);
        f.Update(view, params);
        f.Step();
        CHECK(f.app.context->application_windows.front().flags == 0x5b8);
        f.Int(params, "flags", 2);
        f.Error(f.Outcome(manager, "updateViewLayout", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Ljava/lang/UnsupportedOperationException;", "layout is not supported");
        CHECK(f.app.context->application_windows.front().flags == 0x5b8);
        static_cast<void>(f.Call(manager, "removeViewImmediate", view_sig, {VmValue::Ref(view)}));
        f.Add(view, f.Params()); // A pending removal does not prevent re-adding the same root.
        f.Step();
        REQUIRE(f.app.context->application_windows.size() == 1);
        CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.size() == 1);
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-335 application windows publish independent partitions and composite above main") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Windows f(backend);
        const auto view = f.New("Landroid/widget/ImageView;", "(Landroid/content/Context;)V", {VmValue::Ref(f.Owner())});
        const auto params = f.Params();
        const auto color = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V",
                                 {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
        static_cast<void>(f.Call(view, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(color)}));
        f.Add(view, params);
        f.Step();
        const auto snapshot = runtime::AcquireAndroidPresentation(*f.app.context);
        REQUIRE(snapshot);
        REQUIRE(snapshot->guest_window_epoch_refs.size() == 2);
        CHECK(snapshot->guest_window_epoch_refs.front() != snapshot->main_window);
        CHECK(snapshot->application_windows.front().root == view);
        const auto node = runtime::FindViewUiNode(*f.app.context, view.Value());
        REQUIRE(node);
        CHECK(snapshot->guest_window_epoch_refs.front()->IsAttached(*node));
        CHECK_FALSE(snapshot->main_window->IsAttached(*node));
        CHECK(snapshot->guest_window_epoch_refs.front()->Get(*node)->measured == runtime::ui::Size{64, 36});
        runtime::AndroidBoundaryFrame frame{.width = 64, .height = 36, .rgba8 = std::vector<std::uint8_t>(64 * 36 * 4, 0)};
        const auto composed = f.app.app->ActivityLifecycle().ComposePresentedFrame(frame);
        CHECK(composed.rgba8[0] == 255);
        CHECK(composed.rgba8[1] == 0);
        CHECK(composed.rgba8[3] == 255);
        f.Remove(view);
        f.Step();
        const auto after = runtime::AcquireAndroidPresentation(*f.app.context);
        CHECK(after->guest_window_epoch_refs.size() == 1);
        CHECK(after->application_windows.empty());
        CHECK(snapshot->guest_window_epoch_refs.front()->IsAttached(*node)); // Old immutable lease remains valid.
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-335 application window input and focus follow flags and revoke pointer tails") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Windows f(backend);
        const auto owner = f.Owner(), low = f.View(), high = f.View();
        const auto lp = f.Params(), hp = f.Params();
        auto touch = std::make_shared<std::map<VmObjectRef, std::vector<int>>>();
        auto focus = std::make_shared<std::vector<std::pair<VmObjectRef, bool>>>();
        auto keys = std::make_shared<std::vector<VmObjectRef>>();
        auto activity_keys = std::make_shared<int>();
        f.Hook("Landroid/view/View;", "onTouchEvent", "(Landroid/view/MotionEvent;)Z", [touch, &f](IntrinsicContext& call) {
            (*touch)[call.receiver].push_back(f.Call(call.arguments[0].ref, "getAction", "()I").AsInt());
            return VmValue::Int(1);
        });
        f.Hook("Landroid/view/View;", "onWindowFocusChanged", "(Z)V", [focus](IntrinsicContext& call) {
            focus->emplace_back(call.receiver, call.arguments[0].AsInt() != 0);
            return VmValue::Void();
        });
        f.Hook("Landroid/view/View;", "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z", [keys](IntrinsicContext& call) {
            keys->push_back(call.receiver);
            return VmValue::Int(0);
        });
        f.Hook("Landroid/app/Activity;", "onKeyDown", "(ILandroid/view/KeyEvent;)Z", [activity_keys](IntrinsicContext&) {
            ++*activity_keys;
            return VmValue::Int(0);
        });
        f.Add(low, lp);
        f.Step();
        CHECK_FALSE(f.Call(owner, "hasWindowFocus", "()Z").AsInt());
        CHECK(f.Call(low, "hasWindowFocus", "()Z").AsInt() == 1);
        f.Add(high, hp);
        f.Step();
        CHECK(focus->at(focus->size() - 2) == std::pair{low, false});
        CHECK(focus->back() == std::pair{high, true});
        const auto pointer = [&](int action) {
            f.app.app->ActivityLifecycle().QueueInput({.type = runtime::AndroidBoundaryInputType::pointer_button,
                .x = 5, .y = 5, .pressed = action == 0});
            f.Step();
        };
        pointer(0);
        REQUIRE((*touch)[high] == std::vector<int>{0});
        CHECK((*touch)[low].empty());
        f.Remove(high);
        f.Step();
        CHECK((*touch)[high] == std::vector<int>{0, 3});
        pointer(1);
        CHECK((*touch)[low].empty());
        f.Int(lp, "flags", 8);
        f.Update(low, lp);
        f.Step();
        CHECK(f.Call(owner, "hasWindowFocus", "()Z").AsInt() == 1);
        CHECK_FALSE(f.Call(low, "hasWindowFocus", "()Z").AsInt());
        f.Int(lp, "flags", 0);
        f.Update(low, lp);
        f.Step();
        f.app.app->ActivityLifecycle().QueueInput({.type = runtime::AndroidBoundaryInputType::key, .code = 4, .pressed = true, .action = 0});
        f.Step();
        REQUIRE_FALSE(keys->empty());
        CHECK(keys->back() == low);
        CHECK(*activity_keys == 0);
        f.Add(high, hp);
        f.Int(hp, "flags", 16 | 8 | 32);
        f.Update(high, hp);
        f.Step();
        pointer(0);
        pointer(1);
        CHECK((*touch)[low] == std::vector<int>{0, 1});
        f.Remove(low);
        f.Step();
        CHECK(f.Call(owner, "hasWindowFocus", "()Z").AsInt() == 1);
        // Non-focusable/non-touchable overlay neither takes keys nor blocks touch.
        CHECK_FALSE(f.Call(high, "hasWindowFocus", "()Z").AsInt());
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-335 application windows follow owner visibility and retire leaked windows after destroy") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Windows f(backend);
        const auto owner = f.Owner(), manager = f.Manager(), view = f.View(), params = f.Params();
        const auto roots = f.vm.ProtectReferences(std::array{owner, manager, view, params});
        f.Add(view, params);
        f.Step();
        static_cast<void>(f.app.app->ActivityLifecycle().Suspend());
        CHECK(f.Call(view, "getWindowVisibility", "()I").AsInt() == 4);
        CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.empty());
        static_cast<void>(f.app.app->ActivityLifecycle().Resume());
        CHECK(f.Call(view, "getWindowVisibility", "()I").AsInt() == 0);
        f.Launch();
        CHECK(f.Call(view, "getWindowVisibility", "()I").AsInt() == 4);
        CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.empty());
        static_cast<void>(f.Call(f.Owner(), "finish", "()V"));
        f.Step();
        CHECK(f.Owner() == owner);
        CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.size() == 1);
        CHECK(f.app.CallStaticInt("Lfixture/WindowAttachmentActivity;", "getAttaches") == 1);
        f.Launch();
        const auto reset = f.vm.Linker().FindDirectMethod(f.vm.Linker().ResolveDescriptor("Lfixture/WindowAttachmentActivity;"),
                                                        "resetTrace", "()V");
        REQUIRE(reset.has_value());
        REQUIRE_FALSE(f.vm.Call(*reset, {}).exception.IsValid());
        static_cast<void>(f.Call(owner, "finish", "()V"));
        f.Step();
        CHECK(f.app.CallStaticInt("Lfixture/WindowAttachmentActivity;", "getTrace") % 100 == 67);
        CHECK(f.app.context->application_windows.empty());
        CHECK(f.app.logger.Snapshot(core::LogLevel::warn, "android.window").size() == 1);
        f.Error(f.Outcome(manager, "addView", layout_sig, {VmValue::Ref(view), VmValue::Ref(params)}),
                "Landroid/view/WindowManager$BadTokenException;", "is your activity running?");
        static_cast<void>(f.app.app->Stop());
    }
}

TEST_CASE("DVM-335 application window limit and process teardown do not synthesize detach callbacks") {
    Windows f(InterpreterBackend::switch_dispatch);
    auto detached = std::make_shared<int>();
    f.Hook("Landroid/view/View;", "onDetachedFromWindow", "()V", [detached](IntrinsicContext&) {
        ++*detached;
        return VmValue::Void();
    });
    for (int i = 0; i < 8; ++i) f.Add(f.View(), f.Params());
    f.Step();
    const auto extra = f.View(), params = f.Params();
    f.Error(f.Outcome(f.Manager(), "addView", layout_sig, {VmValue::Ref(extra), VmValue::Ref(params)}),
            "Ljava/lang/UnsupportedOperationException;", "layout is not supported");
    REQUIRE(f.app.context->application_windows.size() == 8);
    static_cast<void>(f.app.app->Stop());
    CHECK(f.app.context->application_windows.empty());
    CHECK(runtime::AcquireAndroidPresentation(*f.app.context)->application_windows.empty());
    CHECK(*detached == 0);
    CHECK(f.app.logger.Snapshot(core::LogLevel::warn, "android.window").empty());
}


TEST_CASE("DVM-335 window added during Activity attachment gains focus after attachment returns") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Windows f(backend, false);
        VmObjectRef root;
        bool callback_returned = false;
        f.Hook("Lfixture/WindowAttachmentActivity;", "onAttachedToWindow", "()V", [&](IntrinsicContext&) {
            CHECK(f.app.context->activity_stack.back().window_attached);
            CHECK_FALSE(f.app.context->window_has_focus.load());
            root = f.View();
            f.Add(root, f.Params());
            callback_returned = true;
            return VmValue::Void();
        });
        auto gains = std::make_shared<int>();
        f.Hook("Landroid/view/View;", "onWindowFocusChanged", "(Z)V", [&, gains](IntrinsicContext& call) {
            if (call.receiver == root && call.arguments[0].AsInt()) {
                CHECK(callback_returned);
                CHECK_FALSE(f.app.context->window_has_focus.load());
                ++*gains;
            }
            return VmValue::Void();
        });
        REQUIRE(f.app.app->StartLauncherActivity().state == session::LifecycleRunState::running);
        CHECK(*gains == 0);
        f.Step();
        CHECK(*gains == 1);
        CHECK(f.Call(root, "hasWindowFocus", "()Z").AsInt() == 1);
        CHECK_FALSE(f.Call(f.Owner(), "hasWindowFocus", "()Z").AsInt());
        static_cast<void>(f.app.app->Stop());
    }
}
