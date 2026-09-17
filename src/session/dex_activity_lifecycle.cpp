#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/session/ui_compositor.h"

#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"

#include <exception>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace ogplay::session {

bool ConsumeGlSurfaceDrawRequest(runtime::DexVmAndroidContext& context) {
    const auto view = context.gl_surface_renderer_view;
    const auto mode = context.gl_surface_render_modes.find(view.Value());
    if (mode == context.gl_surface_render_modes.end() || mode->second == 1) {
        return true;
    }
    const auto requested = context.gl_surface_render_requests.find(view.Value());
    if (requested == context.gl_surface_render_requests.end() ||
        !requested->second) {
        return false;
    }
    requested->second = false;
    return true;
}
    namespace {
        namespace dx = ogplay::runtime::dexvm;

        constexpr std::int32_t kMotionActionDown = 0;
        constexpr std::int32_t kMotionActionUp = 1;
        constexpr std::int32_t kMotionActionMove = 2;
        constexpr std::int32_t kMotionActionCancel = 3;
        constexpr std::int32_t kKeyActionDown = 0;
        constexpr std::int32_t kKeyActionUp = 1;
        constexpr std::int64_t kMillisPerFrame = 16;
        constexpr std::size_t kInitialThreadQuiescenceYieldLimit = 64U;

        [[noreturn]] void Fail(const std::string& message) {
            throw DexActivityLifecycleError(message);
        }

        [[nodiscard]] std::string RenderJavaException(
            const dx::DexClassLinker& linker,
            const dx::VmCallOutcome& outcome) {
            std::string rendered = "  exception: ";
            rendered += outcome.exception_class.IsValid()
                            ? linker.Class(outcome.exception_class).descriptor
                            : "<unknown Java exception>";
            if (!outcome.exception_message.empty()) {
                rendered += "\n  message: " + outcome.exception_message;
            }
            if (!outcome.exception_stack.empty()) {
                rendered += "\n  stack trace:";
                for (const auto& entry: outcome.exception_stack) {
                    rendered += "\n    at " + entry.class_descriptor + "." +
                                entry.method_name + " (pc " +
                                std::to_string(entry.pc) + ")";
                }
            }
            return rendered;
        }

        void RequireOutcome(dx::Interpreter& vm,
                            const dx::VmCallOutcome& outcome,
                            const std::string& what) {
            if (!outcome.exception.IsValid()) return;
            Fail(what + " failed: uncaught Java exception\n" +
                 RenderJavaException(vm.Linker(), outcome));
        }

        void AttachBaseContext(dx::Interpreter& vm,
                               dx::DexClassLinker& linker,
                               const dx::DexClassId java_class,
                               const dx::VmObjectRef object,
                               const dx::VmObjectRef base_context,
                               const std::string& what) {
            const auto attach = linker.FindVtableIndex(
                java_class, "attachBaseContext",
                "(Landroid/content/Context;)V");
            if (!attach.has_value()) {
                Fail(what + " has no attachBaseContext method");
            }
            RequireOutcome(
                vm,
                vm.Call(linker.Class(java_class).vtable[*attach],
                        std::vector<dx::VmValue>{
                            dx::VmValue::Ref(object),
                            dx::VmValue::Ref(base_context)}),
                what + " attachBaseContext");
        }
    } // namespace

    dx::VmObjectRef StartDexApplication(
        runtime::DexVmGuestBridge& bridge,
        const std::shared_ptr<runtime::DexVmAndroidContext>& context,
        const std::string& application_descriptor) {
        if (!context || application_descriptor.empty()) {
            Fail("Application startup requires a platform context and descriptor");
        }
        if (context->application.IsValid()) {
            if (context->application_descriptor != application_descriptor) {
                Fail("a different Application is already started in this process");
            }
            return context->application;
        }

        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        const auto java_class = linker.FindClass(application_descriptor);
        if (!java_class.has_value()) {
            Fail("Application class is not linked: " + application_descriptor);
        }
        RequireOutcome(vm, vm.EnsureClassInitialized(*java_class),
                       "Application <clinit>");
        const auto init = linker.FindDirectMethod(*java_class, "<init>", "()V");
        if (!init.has_value()) {
            Fail("Application has no default constructor: " +
                 application_descriptor);
        }
        const auto application = vm.Model().NewInstance(
            *java_class, linker.Class(*java_class).instance_slots);
        const auto base_context =
                vm.NewIntrinsicInstance("Landroid/content/Context;");
        context->application = application;
        context->application_base_context = base_context;
        try {
            RequireOutcome(
                vm,
                vm.Call(*init, std::vector<dx::VmValue>{
                            dx::VmValue::Ref(application)
                        }),
                "Application <init>");
            AttachBaseContext(vm, linker, *java_class, application,
                              base_context, "Application");
            const auto on_create = linker.FindVtableIndex(
                *java_class, "onCreate", "()V");
            if (!on_create.has_value()) {
                Fail("Application has no onCreate method: " +
                     application_descriptor);
            }
            RequireOutcome(
                vm,
                vm.Call(linker.Class(*java_class).vtable[*on_create],
                        std::vector<dx::VmValue>{
                            dx::VmValue::Ref(application)
                        }),
                "Application onCreate");
            context->application_descriptor = application_descriptor;
            return application;
        } catch (...) {
            context->application = dx::VmObjectRef{};
            context->application_base_context = dx::VmObjectRef{};
            context->application_descriptor.clear();
            throw;
        }
    }

    bool ShouldInterceptScrollGesture(const std::int32_t scroll_range,
                                      const float down_y,
                                      const float current_y,
                                      const float density) noexcept {
        return scroll_range > 0 && density > 0.0F &&
               std::abs(current_y - down_y) > 8.0F * density;
    }

    DeepTouchDispatchResult DispatchDeepTouchEvent(
        dx::Interpreter& vm, runtime::DexVmAndroidContext& context,
        const std::int32_t action, const float x, const float y,
        const std::uint64_t captured_view) {
        auto& linker = vm.Linker();
        const auto invoke = [&](const std::uint64_t handle)
            -> DeepTouchDispatchResult {
            const dx::VmObjectRef receiver{static_cast<std::uint32_t>(handle)};
            const auto receiver_class = vm.Model().ObjectClass(receiver);
            const auto index = linker.FindVtableIndex(
                receiver_class, "onTouchEvent",
                "(Landroid/view/MotionEvent;)Z");
            if (!index.has_value()) {
                return {.error = "View has no onTouchEvent method"};
            }
            const auto method = linker.Class(receiver_class).vtable[*index];
            const auto& owner = linker.Class(linker.Method(method).owner);
            if (action == kMotionActionDown &&
                owner.descriptor == "Landroid/app/Activity;") {
                return {};
            }
            const auto event = runtime::MakeMotionEvent(vm, action, x, y, 0);
            const auto outcome = vm.Call(
                method, std::vector<dx::VmValue>{dx::VmValue::Ref(receiver),
                                                 dx::VmValue::Ref(event)});
            if (outcome.exception.IsValid()) {
                return {.error = "View onTouchEvent failed: uncaught Java exception\n" +
                                 RenderJavaException(linker, outcome)};
            }
            const bool handled = outcome.value.AsInt() != 0;
            return {.handled = handled,
                    .captured_view = action == kMotionActionUp ? 0U : handle};
        };

        if (action != kMotionActionDown) {
            if (captured_view == 0U) return {};
            const auto node = runtime::FindViewUiNode(context, captured_view);
            if (!node.has_value() || !context.ui_tree.IsAttached(*node)) {
                return {};
            }
            return invoke(captured_view);
        }
        for (const auto handle : runtime::FindTouchReceiversAt(context, x, y)) {
            auto result = invoke(handle);
            if (result.error.has_value() || result.handled) return result;
        }
        return {};
    }

    DexActivityLifecycle::DexActivityLifecycle(
        DexActivityLifecycleBindings bindings,
        const std::uint64_t ticks_per_frame,
        const std::uint64_t ticks_per_second)
        : bindings_(std::move(bindings)),
          clock_(ticks_per_frame, ticks_per_second) {
        if (bindings_.bridge == nullptr || !bindings_.context ||
            bindings_.launcher_descriptor.empty() ||
            bindings_.application_descriptor.empty()) {
            Fail("dex_activity lifecycle requires a bridge, a platform context "
                "and Application/launcher descriptors");
        }
    }

    DexActivityLifecycle::~DexActivityLifecycle() {
        if (egl_pacer_attached_) {
            runtime::DetachEglSwapPacer(*bindings_.context,
                                        bindings_.bridge->Vm().ExecutionLock());
        }
    }

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
        RequireOutcome(vm, vm.Call(target, arguments), name);
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
        RequireOutcome(vm, vm.Call(target, arguments), name);
    }

    void DexActivityLifecycle::SetWindowFocus(const bool has_focus) {
        auto& context = *bindings_.context;
        const auto activity = context.activity;
        if (!activity.IsValid()) Fail("window focus has no Activity owner");
        const auto owner = activity.Value();
        if (context.window_has_focus.load() == has_focus &&
            context.window_focus_activity.load() == owner) {
            return;
        }

        // ViewRootImpl updates AttachInfo before dispatch. Keep the same
        // observable ordering even when an override omits super.
        context.window_focus_activity.store(owner);
        context.window_has_focus.store(has_focus);
        CallActivity("onWindowFocusChanged", "(Z)V",
                     {dx::VmValue::Int(has_focus ? 1 : 0)});

        std::vector<dx::VmObjectRef> attached;
        std::vector<runtime::ui::UiNodeId> pending{
            context.ui_tree.Root()};
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            const auto* state = context.ui_tree.Get(node);
            if (state == nullptr) continue;
            for (auto child = state->children.rbegin();
                 child != state->children.rend(); ++child) {
                pending.push_back(*child);
            }
            if (node == context.ui_tree.Root() ||
                !context.ui_tree.IsAttached(node)) {
                continue;
            }
            const auto view = runtime::ViewObjectForUiNode(context, node);
            if (view.IsValid()) attached.push_back(view);
        }
        const auto roots = bindings_.bridge->Vm().ProtectReferences(attached);
        for (const auto view : attached) {
            CallOnView(view, "onWindowFocusChanged", "(Z)V",
                       {dx::VmValue::Int(has_focus ? 1 : 0)});
        }
    }

    LifecycleFrameState DexActivityLifecycle::Start() {
        if (state_ != LifecycleRunState::ready) {
            Fail("dex_activity lifecycle started twice");
        }
        try {
            auto& vm = bindings_.bridge->Vm();
            auto& linker = bindings_.bridge->Linker();
            auto& context = *bindings_.context;

            // The process Application is fully initialized before any Activity
            // class initialization, construction, or surface side effect.
            static_cast<void>(StartDexApplication(
                *bindings_.bridge, bindings_.context,
                bindings_.application_descriptor));

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
            RequireOutcome(vm, vm.EnsureClassInitialized(*activity_class),
                           "launcher <clinit>");
            const auto init = linker.FindDirectMethod(*activity_class, "<init>",
                                                      "()V");
            if (!init.has_value()) {
                Fail("launcher activity has no default constructor");
            }
            const auto activity = vm.Model().NewInstance(
                *activity_class, linker.Class(*activity_class).instance_slots);
            context.activity = activity;
            context.window_focus_activity.store(activity.Value());
            // The manifest launcher opened the process's single task, so it
            // stays the task root across later startActivity handoffs.
            context.task_root_activity = activity.Value();
            RequireOutcome(
                vm,
                vm.Call(*init, std::vector<dx::VmValue>{
                            dx::VmValue::Ref(activity)
                        }),
                "activity <init>");
            AttachBaseContext(vm, linker, *activity_class, activity,
                              context.application_base_context, "Activity");
            auto component_name = vm.Linker().Class(*activity_class).descriptor;
            component_name = component_name.substr(1, component_name.size() - 2);
            std::replace(component_name.begin(), component_name.end(), '/', '.');
            if (!bindings_.launcher_component_name.empty())
                component_name = bindings_.launcher_component_name;
            runtime::AttachAndroidActivityIdentity(vm, bindings_.context, activity,
                                                   component_name);

            // 04 §2 steps 5..7: interpreted lifecycle chain. An activity that
            // requested a switch (startActivity + finish) inside onCreate never
            // starts, matching the platform contract.
            CallActivity("onCreate", "(Landroid/os/Bundle;)V",
                         {dx::VmValue::Ref(dx::VmObjectRef{})});
            if (context.pending_activity_descriptor.empty()) {
                CallActivity("onStart", "()V", {});
                CallActivity("onResume", "()V", {});
                activity_started_ = true;
                // Threads created by onStart/onResume run concurrently on
                // Android. Before the first traversal, observe each worker
                // that exists now reach a real park point (or terminate).
                // The handshake is bounded and deliberately ignores workers
                // created after this requirement set is captured.
                AwaitInitialThreadQuiescence();
            }

            // Installer-style launchers may request the game activity right in
            // onCreate (startActivity + finish); service that before demanding
            // a content view.
            ServiceActivitySwitch();

            if (!context.content_view.IsValid()) {
                Fail("onCreate did not install a content view");
            }
            // The two render drivers are mutually exclusive. An intrinsic
            // renderer keeps the context current on this lifecycle thread (the
            // established exact path). A guest-owned GLSurfaceView has no
            // context.renderer and needs currency handed to its GLThread before
            // surface callbacks start the thread's EGL handshake.
            if (!context.renderer.IsValid() &&
                bindings_.release_surface_currency) {
                runtime::AttachEglSwapPacer(context, vm.ExecutionLock());
                egl_pacer_attached_ = true;
                bindings_.release_surface_currency();
            }
            // Surface geometry precedes renderer callbacks (GLSurfaceView
            // semantics; the pilot's onSurfaceCreated spins until size != -1).
            CallOnView(context.content_view, "onSizeChanged", "(IIII)V",
                       {
                           dx::VmValue::Int(static_cast<std::int32_t>(
                               context.surface_width)),
                           dx::VmValue::Int(static_cast<std::int32_t>(
                               context.surface_height)),
                           dx::VmValue::Int(0), dx::VmValue::Int(0)
                       });

            // A title that brings its own GLSurfaceView is waiting on these
            // before it will touch EGL; the intrinsic one ignores them.
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::created);
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::changed);

            // ViewRootImpl establishes and sizes the Surface during its first
            // traversal; window focus arrives later through a separate
            // message. Keep that boundary instead of folding focus into
            // Start(): guest worker threads must first observe the completed
            // Surface setup before focus can act as a native resume signal.
            initial_focus_pending_ = true;

            // A renderer may not exist yet (installer phase draws nothing);
            // frames then only pump cooperative threads until the interpreted
            // glue registers one.
            EnsureRendererCallbacks();

            state_ = LifecycleRunState::running;
        } catch (...) {
            if (bindings_.bridge->Vm().ExitCode().has_value()) return Stop();
            MarkFailed();
            RethrowFatalThreadFailure();
            throw;
        }
        return State();
    }

    LifecycleFrameState DexActivityLifecycle::Suspend() {
        if (state_ != LifecycleRunState::running || suspended_) {
            Fail("dex_activity lifecycle cannot suspend in this state");
        }
        if (bindings_.diagnostics) {
            bindings_.diagnostics->SetLifecyclePhase("suspend.begin", false);
        }
        try {
            initial_focus_pending_ = false;
            SetWindowFocus(false);
            CallActivity("onPause", "()V", {});
            if (bindings_.flush_persistent_state) {
                bindings_.flush_persistent_state();
            }
            suspended_ = true;
            if (bindings_.diagnostics) {
                bindings_.diagnostics->SetLifecyclePhase(
                    "suspend.complete", false);
            }
        } catch (...) {
            if (bindings_.bridge->Vm().ExitCode().has_value()) return Stop();
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
            SetWindowFocus(true);
            suspended_ = false;
        } catch (...) {
            if (bindings_.bridge->Vm().ExitCode().has_value()) return Stop();
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
        auto& context = *bindings_.context;
        for (const auto& input: pending_input_) {
            using Type = runtime::AndroidBoundaryInputType;
            if (input.type == Type::key) {
                if (input.pressed && context.focused_edit_text.IsValid()) {
                    const auto node = runtime::FindViewUiNode(
                        context, context.focused_edit_text.Value());
                    if (node.has_value()) {
                        auto& state = *context.ui_tree.Get(*node);
                        auto changed = state.text;
                        if (input.code == 67 && !changed.empty()) {
                            changed.pop_back();
                        } else if (input.unicode_char > 0 &&
                                   (!state.numeric_input ||
                                    (input.unicode_char >= '0' &&
                                     input.unicode_char <= '9')) &&
                                   changed.size() < static_cast<std::size_t>(
                                                        state.max_length)) {
                            changed.push_back(static_cast<char16_t>(
                                input.unicode_char));
                        }
                        if (changed != state.text) {
                            const auto before_size = state.text.size();
                            if (changed.size() < before_size) {
                                static_cast<void>(runtime::android_intrinsics::ApplyTextEdit(
                                    vm, context, context.focused_edit_text,
                                    static_cast<std::int32_t>(before_size - 1),
                                    1, {}));
                            } else {
                                static_cast<void>(runtime::android_intrinsics::ApplyTextEdit(
                                    vm, context, context.focused_edit_text,
                                    static_cast<std::int32_t>(before_size), 0,
                                    std::u16string(1, changed.back())));
                            }
                            continue;
                        }
                    }
                }
                const auto key_event =
                    vm.NewIntrinsicInstance("Landroid/view/KeyEvent;");
                const auto key_event_class =
                    vm.Model().ObjectClass(key_event);
                const auto constructor = bindings_.bridge->Linker()
                    .FindDirectMethod(key_event_class, "<init>",
                                      "(JJIIIIII)V");
                if (!constructor.has_value()) {
                    Fail("KeyEvent has no timed input constructor");
                }
                RequireOutcome(
                    vm,
                    vm.Call(*constructor,
                            std::vector<dx::VmValue>{
                                dx::VmValue::Ref(key_event),
                                dx::VmValue::Long(input.event_time_ms),
                                dx::VmValue::Long(input.event_time_ms),
                                dx::VmValue::Int(
                                    input.pressed ? kKeyActionDown
                                                  : kKeyActionUp),
                                dx::VmValue::Int(input.code),
                                dx::VmValue::Int(input.repeat_count),
                                dx::VmValue::Int(input.meta_state),
                                dx::VmValue::Int(input.device_id),
                                dx::VmValue::Int(input.scan_code)}),
                    "KeyEvent <init>");
                runtime::SetAndroidKeyEventUnicode(
                    vm, key_event, input.unicode_char);
                CallActivity(input.pressed ? "onKeyDown" : "onKeyUp",
                             "(ILandroid/view/KeyEvent;)Z",
                             {
                                 dx::VmValue::Int(input.code),
                                 dx::VmValue::Ref(key_event)
                             });
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
                if (!pointer_down_) continue; // hover is not a touch
                action = kMotionActionMove;
            }
            // A visible listener target may capture DOWN. Touch consumption and
            // click eligibility are independent; an unconsumed touch-only DOWN
            // falls through to Activity and does not retain the gesture.
            if (action == kMotionActionDown) {
                context.focused_edit_text = dx::VmObjectRef{};
                scroll_view_handle_ = 0U;
                scroll_dragging_ = false;
                scroll_start_y_ = pointer_y_;
                scroll_last_y_ = pointer_y_;
                const auto edit_class = vm.Linker().ResolveDescriptor(
                    "Landroid/widget/EditText;");
                for (const auto handle : runtime::FindTouchReceiversAt(
                         context, pointer_x_, pointer_y_)) {
                    const dx::VmObjectRef candidate{
                        static_cast<std::uint32_t>(handle)};
                    if (vm.Linker().IsAssignable(
                            edit_class, vm.Model().ObjectClass(candidate))) {
                        context.focused_edit_text = candidate;
                    }
                    const auto node = runtime::FindViewUiNode(context, handle);
                    if (node.has_value() &&
                        context.ui_tree.Get(*node)->kind ==
                            runtime::ui::UiClass::ScrollView) {
                        scroll_view_handle_ = handle;
                    }
                }
                const auto hit = runtime::FindClickableViewAt(
                    *bindings_.context, pointer_x_, pointer_y_);
                gesture_candidate_ = hit.value_or(0U);
                gesture_click_eligible_ = false;
                gesture_touch_consumed_ = false;
                deep_touch_handle_ = 0U;
            }
            if (action == kMotionActionMove && scroll_view_handle_ != 0U) {
                const auto node = runtime::FindViewUiNode(
                    context, scroll_view_handle_);
                if (node.has_value()) {
                    auto& scroll = *context.ui_tree.Get(*node);
                    const auto content_height = scroll.children.empty()
                        ? 0 : context.ui_tree.Get(scroll.children.front())->measured.height;
                    const auto maximum = std::max(
                        0, content_height - scroll.measured.height +
                               scroll.padding.top + scroll.padding.bottom);
                    if (!scroll_dragging_ && ShouldInterceptScrollGesture(
                            maximum, scroll_start_y_, pointer_y_,
                            context.ui_density)) {
                        // Once the ancestor ScrollView intercepts, Android
                        // cancels the original child target. This revokes both
                        // its touch capture and click eligibility.
                        if (gesture_candidate_ != 0U) {
                            const auto cancelled = runtime::DispatchViewGestureEvent(
                                vm, context, gesture_candidate_,
                                kMotionActionCancel, pointer_x_, pointer_y_,
                                gesture_click_eligible_, gesture_touch_consumed_);
                            if (cancelled.error.has_value()) Fail(*cancelled.error);
                        }
                        if (deep_touch_handle_ != 0U) {
                            const auto cancelled = DispatchDeepTouchEvent(
                                vm, context, kMotionActionCancel, pointer_x_,
                                pointer_y_, deep_touch_handle_);
                            if (cancelled.error.has_value()) Fail(*cancelled.error);
                        }
                        gesture_candidate_ = 0U;
                        gesture_click_eligible_ = false;
                        gesture_touch_consumed_ = false;
                        deep_touch_handle_ = 0U;
                        scroll_dragging_ = true;
                    }
                    if (scroll_dragging_) {
                        const auto previous = scroll.scroll_y;
                        scroll.scroll_y = std::clamp(
                            scroll.scroll_y + static_cast<std::int32_t>(
                                std::lround(scroll_last_y_ - pointer_y_)),
                            0, maximum);
                        scroll_last_y_ = pointer_y_;
                        if (scroll.scroll_y != previous) {
                            context.ui_tree.MarkLayoutDirty(*node);
                        }
                        continue;
                    }
                }
            }
            if (action == kMotionActionUp) {
                scroll_view_handle_ = 0U;
                if (scroll_dragging_) {
                    scroll_dragging_ = false;
                    continue;
                }
            }
            const bool had_listener_candidate = gesture_candidate_ != 0U;
            if (had_listener_candidate) {
                const auto result = runtime::DispatchViewGestureEvent(
                    vm, *bindings_.context, gesture_candidate_, action,
                    pointer_x_, pointer_y_, gesture_click_eligible_,
                    gesture_touch_consumed_);
                if (result.error.has_value()) Fail(*result.error);
                gesture_click_eligible_ = result.click_eligible;
                gesture_touch_consumed_ = result.touch_consumed;
                if (!result.keep_capture) gesture_candidate_ = 0U;
                if (result.handled) continue;
            }
            if (!had_listener_candidate) {
                const auto result = DispatchDeepTouchEvent(
                    vm, *bindings_.context, action, pointer_x_, pointer_y_,
                    deep_touch_handle_);
                if (result.error.has_value()) Fail(*result.error);
                deep_touch_handle_ = result.captured_view;
                if (result.handled) continue;
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
            if (initial_focus_pending_) {
                if (activity_started_) SetWindowFocus(true);
                initial_focus_pending_ = false;
            }
            PumpVideo();
            PumpAudioTracks();
            ServiceActivitySwitch();
            EnsureRendererCallbacks();
            if (renderer_ready_) {
                std::vector<dx::VmObjectRef> gl_events;
                {
                    std::scoped_lock lock(bindings_.context->scheduler_mutex);
                    gl_events.swap(bindings_.context->gl_surface_events);
                }
                for (const auto event : gl_events) {
                    CallOnView(event, "run", "()V", {});
                }
                const bool draw = ConsumeGlSurfaceDrawRequest(*bindings_.context);
                if (draw) {
                    CallOnView(bindings_.context->renderer, "onDrawFrame",
                               "(Ljavax/microedition/khronos/opengles/GL10;)V",
                               {dx::VmValue::Ref(dx::VmObjectRef{})});
                    if (bindings_.present_surface) bindings_.present_surface();
                }
            } else if (!bindings_.context->renderer.IsValid() &&
                       bindings_.context->active_surface_holders.empty() &&
                       bindings_.context->video_views.empty() &&
                       bindings_.context->holder_canvases.empty() &&
                       bindings_.context->content_view.IsValid() &&
                       bindings_.context->session != nullptr &&
                       bindings_.context->ui_tree.Get(
                           bindings_.context->ui_tree.Root())->draw_dirty) {
                // A View-only Activity has no GLES producer to create the
                // boundary frame that the frontend uses as the UI composition
                // trigger. Publish an opaque window-sized software base when
                // the retained tree is dirty; ComposePresentedFrame remains
                // the single authority for layout and drawing.
                const auto pixels =
                    static_cast<std::size_t>(bindings_.context->surface_width) *
                    bindings_.context->surface_height;
                std::vector<std::uint8_t> rgba8(pixels * 4U, 0U);
                for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
                    rgba8[pixel * 4U + 3U] = 0xffU;
                }
                bindings_.context->session->PublishSoftwareFrame(
                    std::move(rgba8));
            }
            clock_.AdvanceFrames(1);
            runtime::AdvanceAndroidClock(*bindings_.context,
                                         kMillisPerFrame);
            ++frame_;
            if (egl_pacer_attached_) {
                runtime::AdvanceEglSwapPacer(*bindings_.context);
            }
        } catch (...) {
            if (bindings_.bridge->Vm().ExitCode().has_value()) return Stop();
            MarkFailed();
            RethrowFatalThreadFailure();
            throw;
        }
        return State();
    }

    runtime::AndroidBoundaryFrame DexActivityLifecycle::ComposePresentedFrame(
        runtime::AndroidBoundaryFrame frame) {
        auto& context = *bindings_.context;
        if (context.ui_tree.Get(context.ui_tree.Root())->layout_dirty) {
            runtime::ui::LayoutUiTree(
                context.ui_tree,
                {
                    static_cast<std::int32_t>(frame.width),
                    static_cast<std::int32_t>(frame.height)
                });
        }
        const auto& overlay = context.ui_overlay_renderer.Render(
            context.ui_tree, context.ui_bitmaps,
            {
                static_cast<std::int32_t>(frame.width),
                static_cast<std::int32_t>(frame.height)
            });
        frame.rgba8 = ComposeUiOverlay(frame.rgba8, overlay);
        return frame;
    }

    void DexActivityLifecycle::RethrowFatalThreadFailure() {
        // A Java thread that died takes the VM down with it, which is what
        // unblocked this thread. Report the death, not the teardown it caused.
        const auto failure = bindings_.bridge->Threads().TakeFailure();
        if (failure.has_value()) Fail(*failure);
    }

    void DexActivityLifecycle::DispatchSurfaceHolder(
        const runtime::SurfaceHolderPhase phase) {
        const auto error = runtime::DispatchSurfaceHolderCallbacks(
            bindings_.bridge->Vm(), *bindings_.context, phase);
        if (error.has_value()) Fail(*error);
    }

    void DexActivityLifecycle::PumpJavaThreads() {
        const auto error = runtime::PumpJavaThreads(bindings_.bridge->Vm(),
                                                    *bindings_.context);
        if (error.has_value()) Fail(*error);
    }

    void DexActivityLifecycle::PumpVideo() {
        if (bindings_.context->video_views.empty() &&
            bindings_.context->pending_video_completion.empty()) {
            return;
        }
        const auto error = runtime::PumpVideoViews(
            bindings_.bridge->Vm(), *bindings_.context,
            bindings_.publish_video_frame);
        if (error.has_value()) Fail(*error);
    }

    void DexActivityLifecycle::PumpAudioTracks() {
        if (bindings_.context->audio_tracks.empty()) return;
        const auto error = runtime::PumpAndroidAudioTracks(
            bindings_.bridge->Vm(), *bindings_.context);
        if (error.has_value()) Fail(*error);
    }

    void DexActivityLifecycle::AwaitInitialThreadQuiescence() {
        auto& threads = bindings_.bridge->Threads();
        std::unordered_set<std::uint64_t> pending;
        const auto terminal = [](const dx::VmThreadStatus status) {
            return status == dx::VmThreadStatus::finished ||
                   status == dx::VmThreadStatus::stopped ||
                   status == dx::VmThreadStatus::failed;
        };
        for (const auto& thread : threads.Snapshot()) {
            if (thread.context_token == dx::kRootLifecycleToken ||
                thread.wait_state != dx::VmThreadWaitState::none ||
                terminal(thread.status)) {
                continue;
            }
            pending.insert(thread.context_token);
        }
        if (pending.empty()) return;

        const auto observe = [&] {
            for (const auto& thread : threads.Snapshot()) {
                if (!pending.contains(thread.context_token)) continue;
                if (thread.wait_state != dx::VmThreadWaitState::none ||
                    terminal(thread.status)) {
                    pending.erase(thread.context_token);
                }
            }
        };
        for (std::size_t round = 0;
             round < kInitialThreadQuiescenceYieldLimit; ++round) {
            static_cast<void>(threads.WaitForHostProgress(
                std::chrono::milliseconds(2)));
            observe();
            if (pending.empty()) return;
        }

        if (auto* logger = bindings_.bridge->Vm().Log(); logger != nullptr) {
            logger->Write(
                core::LogLevel::warn, "session.dex_lifecycle",
                "initial Java thread quiescence handshake reached yield limit",
                {},
                {{"yield_limit", static_cast<std::uint64_t>(
                                      kInitialThreadQuiescenceYieldLimit)},
                 {"pending_threads",
                  static_cast<std::uint64_t>(pending.size())}});
        }
    }

    void DexActivityLifecycle::ServiceActivitySwitch() {
        auto& context = *bindings_.context;
        while (!context.pending_activity_descriptor.empty()) {
            const auto descriptor =
                    std::exchange(context.pending_activity_descriptor, {});
            const auto pending_component =
                    std::exchange(context.pending_activity_component_name, {});
            context.activity_switch_pending = false;
            const auto departing = context.activity.Value();
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
                SetWindowFocus(false);
                CallActivity("onPause", "()V", {});
                CallActivity("onStop", "()V", {});
            }
            const auto surface_error = runtime::RetireSurfaceHolderGeneration(
                vm, context);
            if (surface_error.has_value()) Fail(*surface_error);
            CallActivity("onDestroy", "()V", {});
            activity_started_ = false;
            // The departing activity's own finish() is answered by its retirement.
            // Its run() may still be executing on its host thread and repeat the
            // call afterwards; that lands on a handle nothing owns any more.
            auto finished = departing;
            context.finishing_activity.compare_exchange_strong(finished, 0U);
            context.content_view = dx::VmObjectRef{};
            gesture_candidate_ = 0U;
            gesture_click_eligible_ = false;
            gesture_touch_consumed_ = false;
            deep_touch_handle_ = 0U;
            runtime::ResetViewUiState(context);
            context.renderer = dx::VmObjectRef{};
            context.egl_context_factory = dx::VmObjectRef{};
            context.egl_config_chooser = dx::VmObjectRef{};
            renderer_ready_ = false;

            const auto activity_class = linker.FindClass(descriptor);
            if (!activity_class.has_value()) {
                Fail("startActivity target is not in the dex: " + descriptor);
            }
            RequireOutcome(vm, vm.EnsureClassInitialized(*activity_class),
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
                vm,
                vm.Call(*init, std::vector<dx::VmValue>{
                            dx::VmValue::Ref(activity)
                        }),
                "activity <init>");
            AttachBaseContext(vm, linker, *activity_class, activity,
                              context.application_base_context, "Activity");
            auto component_name = pending_component;
            if (component_name.empty()) {
                component_name = vm.Linker().Class(*activity_class).descriptor;
                component_name = component_name.substr(1, component_name.size() - 2);
                std::replace(component_name.begin(), component_name.end(), '/', '.');
            }
            runtime::AttachAndroidActivityIdentity(vm, bindings_.context, activity,
                                                   component_name);

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
                           {
                               dx::VmValue::Int(static_cast<std::int32_t>(
                                   context.surface_width)),
                               dx::VmValue::Int(static_cast<std::int32_t>(
                                   context.surface_height)),
                               dx::VmValue::Int(0), dx::VmValue::Int(0)
                           });
                if (activity_started_) SetWindowFocus(true);
            }
            // A replacement Activity installs a new SurfaceView generation.
            // Callbacks registered during onCreate must observe the already-open
            // managed host surface before its GL thread can render.
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::created);
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::changed);
        }
    }

    void DexActivityLifecycle::EnsureRendererCallbacks() {
        auto& context = *bindings_.context;
        if (renderer_ready_ || !context.renderer.IsValid()) return;
        CallOnView(context.renderer, "onSurfaceCreated",
                   "(Ljavax/microedition/khronos/opengles/GL10;"
                   "Ljavax/microedition/khronos/egl/EGLConfig;)V",
                   {
                       dx::VmValue::Ref(dx::VmObjectRef{}),
                       dx::VmValue::Ref(dx::VmObjectRef{})
                   });
        CallOnView(context.renderer, "onSurfaceChanged",
                   "(Ljavax/microedition/khronos/opengles/GL10;II)V",
                   {
                       dx::VmValue::Ref(dx::VmObjectRef{}),
                       dx::VmValue::Int(static_cast<std::int32_t>(
                           context.surface_width)),
                       dx::VmValue::Int(static_cast<std::int32_t>(
                           context.surface_height))
                   });
        renderer_ready_ = true;
    }

    LifecycleFrameState DexActivityLifecycle::Stop() {
        if (state_ == LifecycleRunState::stopped) return State();
        const auto phase = [this](const std::string_view name,
                                  const bool active = true) {
            if (bindings_.diagnostics) {
                bindings_.diagnostics->SetLifecyclePhase(name, active);
            }
        };
        phase("teardown.begin");
        const bool was_running = state_ == LifecycleRunState::running &&
                                 !bindings_.bridge->Vm().ExitCode().has_value();
        // Device services and the graphics boundary outlive guest callbacks on
        // Android. OGPlay owns both in-process, so retire graphics and publish
        // cancellation before onPause can wait for a render-thread handshake.
        runtime::RetireGuestEglSurface(*bindings_.context);
        bindings_.bridge->Session().BeginTeardown();
        phase("teardown.guest_callbacks");
        try {
            if (was_running && !suspended_) {
                SetWindowFocus(false);
                CallActivity("onPause", "()V", {});
            }
            if (was_running) {
                const auto error = runtime::RetireSurfaceHolderGeneration(
                    bindings_.bridge->Vm(), *bindings_.context);
                if (error.has_value()) state_ = LifecycleRunState::failed;
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
                        vm,
                        vm.Call(linker.Class(renderer_class).vtable[*index],
                                std::vector<dx::VmValue>{
                                    dx::VmValue::Ref(bindings_.context->renderer),
                                    dx::VmValue::Ref(dx::VmObjectRef{})
                                }),
                        "surfaceDestroyed");
                }
                CallActivity("onStop", "()V", {});
                CallActivity("onDestroy", "()V", {});
            }
        } catch (const std::exception&) {
            // Teardown continues; the guest still gets finalized below.
            if (!bindings_.bridge->Vm().ExitCode().has_value())
                state_ = LifecycleRunState::failed;
        }
        // 04 §2 step 10: guest Java threads are interrupted and joined before
        // the native side is finalized, so no interpreted frame can still be
        // running when the object world is torn down.
        std::exception_ptr persistence_failure;
        if (egl_pacer_attached_) {
            runtime::ShutdownEglSwapPacer(*bindings_.context);
        }
        phase("teardown.scheduler_shutdown");
        runtime::ShutdownAndroidScheduler(*bindings_.context);
        // A callback may have entered a new futex after BeginTeardown's first
        // wake. Re-interrupt immediately before join so every waiter observes
        // cancellation instead of making shutdown depend on a guest wake.
        if (bindings_.interrupt_guest_waits) bindings_.interrupt_guest_waits();
        phase("teardown.thread_join");
        bindings_.bridge->Threads().Shutdown();
        phase("teardown.persistence");
        if (bindings_.flush_persistent_state) {
            try {
                bindings_.flush_persistent_state();
            } catch (...) {
                state_ = LifecycleRunState::failed;
                persistence_failure = std::current_exception();
            }
        }
        phase("teardown.guest_finalize");
        if (!guest_finalized_ && bindings_.finalize_guest) {
            bindings_.finalize_guest();
            guest_finalized_ = true;
        }
        phase("teardown.surface_close");
        if (surface_open_ && bindings_.close_surface) {
            bindings_.close_surface();
            surface_open_ = false;
        }
        phase("teardown.complete", false);
        if (persistence_failure) std::rethrow_exception(persistence_failure);
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
} // namespace ogplay::session
