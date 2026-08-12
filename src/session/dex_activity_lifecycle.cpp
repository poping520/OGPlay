#include "ogplay/session/dex_activity_lifecycle.h"

#include <utility>

namespace ogplay::session {
namespace {

namespace dx = ogplay::runtime::dexvm;

constexpr std::int32_t kMotionActionDown = 0;
constexpr std::int32_t kMotionActionUp = 1;
constexpr std::int32_t kMotionActionMove = 2;
constexpr std::int64_t kMillisPerFrame = 16;

[[noreturn]] void Fail(const std::string& message) {
    throw DexActivityLifecycleError(message);
}

void RequireOutcome(const dx::VmCallOutcome& outcome,
                    const std::string& what) {
    if (!outcome.exception.IsValid()) return;
    std::string rendered =
        what + " raised an uncaught Java exception: " +
        outcome.exception_message;
    for (const auto& entry : outcome.exception_stack) {
        rendered += "\n  at " + entry.class_descriptor + "." +
                    entry.method_name + " (pc " +
                    std::to_string(entry.pc) + ")";
    }
    Fail(rendered);
}

}  // namespace

DexActivityLifecycle::DexActivityLifecycle(
    DexActivityLifecycleBindings bindings,
    const std::uint64_t ticks_per_frame,
    const std::uint64_t ticks_per_second)
    : bindings_(std::move(bindings)),
      clock_(ticks_per_frame, ticks_per_second) {
    if (bindings_.bridge == nullptr || !bindings_.context ||
        bindings_.launcher_descriptor.empty()) {
        Fail("dex_activity lifecycle requires a bridge, a platform context "
             "and a launcher activity");
    }
}

DexActivityLifecycle::~DexActivityLifecycle() = default;

void DexActivityLifecycle::CallActivity(
    const std::string& name, const std::string& descriptor,
    std::vector<dx::VmValue> arguments) {
    auto& vm = bindings_.bridge->Vm();
    auto& linker = bindings_.bridge->Linker();
    const auto activity = bindings_.context->activity;
    if (!activity.IsValid()) Fail("activity instance is not constructed");
    const auto activity_class = vm.Model().ObjectClass(activity);
    const auto index = linker.FindVtableIndex(activity_class, name,
                                              descriptor);
    if (!index.has_value()) {
        Fail("activity method is not linked: " + name + descriptor);
    }
    arguments.insert(arguments.begin(), dx::VmValue::Ref(activity));
    const auto target = linker.Class(activity_class).vtable[*index];
    RequireOutcome(vm.Call(target, arguments), name);
}

void DexActivityLifecycle::CallOnView(
    const dx::VmObjectRef receiver, const std::string& name,
    const std::string& descriptor, std::vector<dx::VmValue> arguments) {
    if (!receiver.IsValid()) Fail("view receiver is missing for " + name);
    auto& vm = bindings_.bridge->Vm();
    auto& linker = bindings_.bridge->Linker();
    const auto receiver_class = vm.Model().ObjectClass(receiver);
    const auto index =
        linker.FindVtableIndex(receiver_class, name, descriptor);
    if (!index.has_value()) {
        Fail("view method is not linked: " + name + descriptor);
    }
    arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
    const auto target = linker.Class(receiver_class).vtable[*index];
    RequireOutcome(vm.Call(target, arguments), name);
}

LifecycleFrameState DexActivityLifecycle::Start() {
    if (state_ != LifecycleRunState::ready) {
        Fail("dex_activity lifecycle started twice");
    }
    try {
        auto& vm = bindings_.bridge->Vm();
        auto& linker = bindings_.bridge->Linker();
        auto& context = *bindings_.context;

        if (bindings_.open_surface) bindings_.open_surface();
        surface_open_ = true;

        // 04 §2 step 4: instantiate the launcher activity; the subclass
        // <clinit> (System.loadLibrary et al) runs here.
        const auto activity_class =
            linker.FindClass(bindings_.launcher_descriptor);
        if (!activity_class.has_value()) {
            Fail("launcher activity class is not in the dex: " +
                 bindings_.launcher_descriptor);
        }
        RequireOutcome(vm.EnsureClassInitialized(*activity_class),
                       "launcher <clinit>");
        const auto init = linker.FindDirectMethod(*activity_class, "<init>",
                                                  "()V");
        if (!init.has_value()) {
            Fail("launcher activity has no default constructor");
        }
        const auto activity = vm.Model().NewInstance(
            *activity_class, linker.Class(*activity_class).instance_slots);
        context.activity = activity;
        RequireOutcome(
            vm.Call(*init, std::vector<dx::VmValue>{
                               dx::VmValue::Ref(activity)}),
            "activity <init>");

        // 04 §2 steps 5..7: interpreted lifecycle chain. An activity that
        // requested a switch (startActivity + finish) inside onCreate never
        // starts, matching the platform contract.
        CallActivity("onCreate", "(Landroid/os/Bundle;)V",
                     {dx::VmValue::Ref(dx::VmObjectRef{})});
        if (context.pending_activity_descriptor.empty()) {
            CallActivity("onStart", "()V", {});
            CallActivity("onResume", "()V", {});
            activity_started_ = true;
        }

        // Installer-style launchers may request the game activity right in
        // onCreate (startActivity + finish); service that before demanding
        // a content view.
        ServiceActivitySwitch();

        if (!context.content_view.IsValid()) {
            Fail("onCreate did not install a content view");
        }
        // Surface geometry precedes renderer callbacks (GLSurfaceView
        // semantics; the pilot's onSurfaceCreated spins until size != -1).
        CallOnView(context.content_view, "onSizeChanged", "(IIII)V",
                   {dx::VmValue::Int(static_cast<std::int32_t>(
                        context.surface_width)),
                    dx::VmValue::Int(static_cast<std::int32_t>(
                        context.surface_height)),
                    dx::VmValue::Int(0), dx::VmValue::Int(0)});
        CallOnView(context.content_view, "onWindowFocusChanged", "(Z)V",
                   {dx::VmValue::Int(1)});

        // A renderer may not exist yet (installer phase draws nothing);
        // frames then only pump cooperative threads until the interpreted
        // glue registers one.
        EnsureRendererCallbacks();

        state_ = LifecycleRunState::running;
    } catch (...) {
        MarkFailed();
        throw;
    }
    return State();
}

LifecycleFrameState DexActivityLifecycle::Suspend() {
    if (state_ != LifecycleRunState::running || suspended_) {
        Fail("dex_activity lifecycle cannot suspend in this state");
    }
    try {
        CallOnView(bindings_.context->content_view, "onWindowFocusChanged",
                   "(Z)V", {dx::VmValue::Int(0)});
        CallActivity("onPause", "()V", {});
        suspended_ = true;
    } catch (...) {
        MarkFailed();
        throw;
    }
    return State();
}

LifecycleFrameState DexActivityLifecycle::Resume() {
    if (state_ != LifecycleRunState::running || !suspended_) {
        Fail("dex_activity lifecycle cannot resume in this state");
    }
    try {
        CallActivity("onResume", "()V", {});
        CallOnView(bindings_.context->content_view, "onWindowFocusChanged",
                   "(Z)V", {dx::VmValue::Int(1)});
        suspended_ = false;
    } catch (...) {
        MarkFailed();
        throw;
    }
    return State();
}

void DexActivityLifecycle::QueueInput(
    const runtime::AndroidBoundaryInput& input) {
    if (state_ != LifecycleRunState::running || suspended_) return;
    pending_input_.push_back(input);
}

void DexActivityLifecycle::DispatchInput() {
    auto& vm = bindings_.bridge->Vm();
    for (const auto& input : pending_input_) {
        using Type = runtime::AndroidBoundaryInputType;
        if (input.type == Type::key) {
            CallActivity(input.pressed ? "onKeyDown" : "onKeyUp",
                         "(ILandroid/view/KeyEvent;)Z",
                         {dx::VmValue::Int(input.code),
                          dx::VmValue::Ref(dx::VmObjectRef{})});
            continue;
        }
        std::int32_t action{};
        if (input.type == Type::pointer_button) {
            action = input.pressed ? kMotionActionDown : kMotionActionUp;
            pointer_down_ = input.pressed;
            pointer_x_ = input.x;
            pointer_y_ = input.y;
        } else {
            pointer_x_ = input.x;
            pointer_y_ = input.y;
            if (!pointer_down_) continue;  // hover is not a touch
            action = kMotionActionMove;
        }
        // Device semantics: a down on a visible view with an
        // OnClickListener is consumed by that view; the matching up inside
        // its bounds fires onClick. Everything else reaches the activity.
        if (action == kMotionActionDown) {
            const auto hit = runtime::FindClickableViewAt(
                *bindings_.context, pointer_x_, pointer_y_);
            click_candidate_ = hit.value_or(0U);
        }
        if (click_candidate_ != 0U) {
            if (action == kMotionActionUp) {
                const auto target = std::exchange(click_candidate_, 0U);
                if (runtime::ViewContainsPoint(*bindings_.context, target,
                                               pointer_x_, pointer_y_)) {
                    const auto error = runtime::InvokeViewOnClick(
                        vm, *bindings_.context, target);
                    if (error.has_value()) Fail(*error);
                }
            }
            continue;  // the owning view consumed the gesture
        }
        const auto event = runtime::MakeMotionEvent(
            vm, action, pointer_x_, pointer_y_, 0);
        CallActivity("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
                     {dx::VmValue::Ref(event)});
    }
    pending_input_.clear();
}

LifecycleFrameState DexActivityLifecycle::StepFrame() {
    if (state_ != LifecycleRunState::running) {
        Fail("dex_activity lifecycle is not running");
    }
    if (suspended_) return State();
    try {
        DispatchInput();
        PumpJavaThreads();
        PumpVideo();
        ServiceActivitySwitch();
        EnsureRendererCallbacks();
        if (renderer_ready_) {
            CallOnView(bindings_.context->renderer, "onDrawFrame",
                       "(Ljavax/microedition/khronos/opengles/GL10;)V",
                       {dx::VmValue::Ref(dx::VmObjectRef{})});
            if (bindings_.present_surface) bindings_.present_surface();
        }
        clock_.AdvanceFrames(1);
        bindings_.context->uptime_millis += kMillisPerFrame;
        ++frame_;
    } catch (...) {
        MarkFailed();
        throw;
    }
    return State();
}

void DexActivityLifecycle::PumpJavaThreads() {
    const auto error = runtime::PumpJavaThreads(bindings_.bridge->Vm(),
                                                *bindings_.context);
    if (error.has_value()) Fail(*error);
}

void DexActivityLifecycle::PumpVideo() {
    if (bindings_.context->video_views.empty()) return;
    const auto error = runtime::PumpVideoViews(
        bindings_.bridge->Vm(), *bindings_.context,
        bindings_.publish_video_frame);
    if (error.has_value()) Fail(*error);
}

void DexActivityLifecycle::ServiceActivitySwitch() {
    auto& context = *bindings_.context;
    while (!context.pending_activity_descriptor.empty()) {
        const auto descriptor =
            std::exchange(context.pending_activity_descriptor, {});
        // finish() on the departing activity targets that activity, not
        // the session.
        context.exit_requested = false;
        if (auto* logger = bindings_.bridge->Vm().Log(); logger != nullptr) {
            logger->Write(core::LogLevel::info, "session.dex_lifecycle",
                          "switching activity: " + descriptor);
        }

        auto& vm = bindings_.bridge->Vm();
        auto& linker = bindings_.bridge->Linker();

        // Retire the old activity deterministically before the new one.
        // A never-started activity (finished inside its onCreate) only
        // receives onDestroy, as on the platform.
        if (activity_started_) {
            CallActivity("onPause", "()V", {});
            CallActivity("onStop", "()V", {});
        }
        CallActivity("onDestroy", "()V", {});
        activity_started_ = false;
        context.content_view = dx::VmObjectRef{};
        context.renderer = dx::VmObjectRef{};
        renderer_ready_ = false;

        const auto activity_class = linker.FindClass(descriptor);
        if (!activity_class.has_value()) {
            Fail("startActivity target is not in the dex: " + descriptor);
        }
        RequireOutcome(vm.EnsureClassInitialized(*activity_class),
                       "activity <clinit>");
        const auto init = linker.FindDirectMethod(*activity_class,
                                                  "<init>", "()V");
        if (!init.has_value()) {
            Fail("activity has no default constructor: " + descriptor);
        }
        const auto activity = vm.Model().NewInstance(
            *activity_class, linker.Class(*activity_class).instance_slots);
        context.activity = activity;
        RequireOutcome(
            vm.Call(*init, std::vector<dx::VmValue>{
                               dx::VmValue::Ref(activity)}),
            "activity <init>");
        CallActivity("onCreate", "(Landroid/os/Bundle;)V",
                     {dx::VmValue::Ref(dx::VmObjectRef{})});
        if (context.pending_activity_descriptor.empty()) {
            CallActivity("onStart", "()V", {});
            CallActivity("onResume", "()V", {});
            activity_started_ = true;
        }

        if (!context.content_view.IsValid() &&
            context.pending_activity_descriptor.empty()) {
            Fail("activity did not install a content view: " + descriptor);
        }
        if (context.content_view.IsValid()) {
            CallOnView(context.content_view, "onSizeChanged", "(IIII)V",
                       {dx::VmValue::Int(static_cast<std::int32_t>(
                            context.surface_width)),
                        dx::VmValue::Int(static_cast<std::int32_t>(
                            context.surface_height)),
                        dx::VmValue::Int(0), dx::VmValue::Int(0)});
            CallOnView(context.content_view, "onWindowFocusChanged",
                       "(Z)V", {dx::VmValue::Int(1)});
        }
    }
}

void DexActivityLifecycle::EnsureRendererCallbacks() {
    auto& context = *bindings_.context;
    if (renderer_ready_ || !context.renderer.IsValid()) return;
    CallOnView(context.renderer, "onSurfaceCreated",
               "(Ljavax/microedition/khronos/opengles/GL10;"
               "Ljavax/microedition/khronos/egl/EGLConfig;)V",
               {dx::VmValue::Ref(dx::VmObjectRef{}),
                dx::VmValue::Ref(dx::VmObjectRef{})});
    CallOnView(context.renderer, "onSurfaceChanged",
               "(Ljavax/microedition/khronos/opengles/GL10;II)V",
               {dx::VmValue::Ref(dx::VmObjectRef{}),
                dx::VmValue::Int(static_cast<std::int32_t>(
                    context.surface_width)),
                dx::VmValue::Int(static_cast<std::int32_t>(
                    context.surface_height))});
    renderer_ready_ = true;
}

LifecycleFrameState DexActivityLifecycle::Stop() {
    if (state_ == LifecycleRunState::stopped) return State();
    const bool was_running = state_ == LifecycleRunState::running;
    try {
        if (was_running && !suspended_) {
            CallOnView(bindings_.context->content_view,
                       "onWindowFocusChanged", "(Z)V",
                       {dx::VmValue::Int(0)});
            CallActivity("onPause", "()V", {});
        }
        if (was_running && bindings_.context->renderer.IsValid()) {
            // GLSurfaceView notifies the renderer before teardown.
            auto& linker = bindings_.bridge->Linker();
            auto& vm = bindings_.bridge->Vm();
            const auto renderer_class =
                vm.Model().ObjectClass(bindings_.context->renderer);
            const auto index = linker.FindVtableIndex(
                renderer_class, "surfaceDestroyed",
                "(Ljavax/microedition/khronos/opengles/GL10;)V");
            if (index.has_value()) {
                RequireOutcome(
                    vm.Call(linker.Class(renderer_class).vtable[*index],
                            std::vector<dx::VmValue>{
                                dx::VmValue::Ref(bindings_.context->renderer),
                                dx::VmValue::Ref(dx::VmObjectRef{})}),
                    "surfaceDestroyed");
            }
            CallActivity("onStop", "()V", {});
            CallActivity("onDestroy", "()V", {});
        }
    } catch (const std::exception&) {
        // Teardown continues; the guest still gets finalized below.
        state_ = LifecycleRunState::failed;
    }
    // 04 §2 step 10: guest Java threads are interrupted and joined before
    // the native side is finalized, so no interpreted frame can still be
    // running when the object world is torn down.
    bindings_.bridge->Threads().Shutdown();
    if (bindings_.interrupt_guest_waits) bindings_.interrupt_guest_waits();
    if (!guest_finalized_ && bindings_.finalize_guest) {
        bindings_.finalize_guest();
        guest_finalized_ = true;
    }
    if (surface_open_ && bindings_.close_surface) {
        bindings_.close_surface();
        surface_open_ = false;
    }
    if (state_ != LifecycleRunState::failed) {
        state_ = LifecycleRunState::stopped;
    }
    return State();
}

LifecycleFrameState DexActivityLifecycle::State() const {
    return {state_, frame_, clock_.Ticks()};
}

void DexActivityLifecycle::MarkFailed() noexcept {
    state_ = LifecycleRunState::failed;
}

}  // namespace ogplay::session
