#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/session/ui_compositor.h"

#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"

#include <exception>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace ogplay::session {

namespace {
void ProjectRenderer(runtime::DexVmAndroidContext& context,
                     const runtime::DexVmAndroidContext::GlSurfaceRuntime& state) {
    if (context.gl_surface_renderer_view != state.view) return;
    context.renderer = state.renderer;
    context.egl_context_factory = state.egl_context_factory;
    context.egl_config_chooser = state.egl_config_chooser;
    context.renderer_egl = state.renderer_egl;
    context.renderer_display = state.renderer_display;
    context.renderer_config = state.renderer_config;
    context.renderer_context = state.renderer_context;
    context.renderer_surface = state.renderer_surface;
    context.renderer_gl = state.renderer_gl;
}
}

struct DexActivityLifecycle::RendererThread {
    runtime::dexvm::VmObjectRef object;
    std::shared_ptr<runtime::DexVmAndroidContext::GlSurfaceRuntime> state;
    std::uint64_t context_token{};
    std::mutex mutex;
    std::condition_variable changed;
    std::function<void()> action;
    std::exception_ptr failure;
    bool busy{}, events{}, stopping{}, exited{};
};

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
        const std::uint64_t captured_view, const runtime::AndroidBoundaryInput* snapshot) {
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
            auto adjusted = snapshot ? *snapshot : runtime::AndroidBoundaryInput{};
            if (snapshot && action == kMotionActionCancel) adjusted.action = kMotionActionCancel;
            const auto event = snapshot ? runtime::MakeMotionEvent(vm, adjusted)
                                       : runtime::MakeMotionEvent(vm, action, x, y, 0);
            const auto outcome = vm.Call(
                method, std::vector<dx::VmValue>{dx::VmValue::Ref(receiver),
                                                 dx::VmValue::Ref(event)});
            if (outcome.exception.IsValid()) {
                return {.error = "View onTouchEvent failed: uncaught Java exception\n" +
                                 RenderJavaException(linker, outcome)};
            }
            const bool handled = outcome.value.AsInt() != 0;
            return {.handled = handled,
                    .captured_view = (action == kMotionActionUp || action == kMotionActionCancel) ? 0U : handle};
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
        bindings_.context->retire_gl_surface_view = [this](const dx::VmObjectRef view) { StopRendererThread(view); };
    }

    DexActivityLifecycle::~DexActivityLifecycle() {
        try { StopAllRenderers(); } catch (...) {}
        bindings_.context->retire_gl_surface_view = {};
        bindings_.context->run_gl_surface_thread = {};
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

    dx::VmValue DexActivityLifecycle::CallOnView(
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
        const auto outcome = vm.Call(target, arguments);
        RequireOutcome(vm, outcome, name);
        return outcome.value;
    }

    void DexActivityLifecycle::SetWindowFocus(const bool has_focus) {
        auto& context = *bindings_.context;
        if (!has_focus && state_ == LifecycleRunState::running) {
            CancelInput();
        }
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

        DispatchViewWindowFocus(has_focus);
    }

    void DexActivityLifecycle::DispatchViewWindowFocus(const bool has_focus) {
        auto& context = *bindings_.context;
        std::vector<dx::VmObjectRef> attached;
        std::vector<runtime::ui::UiNodeId> pending{
            runtime::ActivityContentRoot(context)};
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            const auto* state = context.ui_tree.Get(node);
            if (state == nullptr || !context.ui_tree.IsVisible(node)) continue;
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
            context.defer_content_surface_callbacks = true;

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
            context.activity_stack.push_back({activity});
            context.activity_stack_depth.store(context.activity_stack.size());
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
            if (context.activity_commands.empty() && !runtime::SessionExitRequested(context)) {
                StartCurrentActivity();
                if (activity_resumed_) AwaitInitialThreadQuiescence();
            }

            // Consume immediate handoffs before the first traversal. Empty
            // launchers may instead post a handoff to the regular frame pump.
            ServiceActivitySwitch();

            // Guest-owned GLSurfaceView keeps its existing swap pacer. The
            // intrinsic renderer releases host currency when its GLThread starts.
            if (!context.renderer.IsValid() &&
                bindings_.release_surface_currency) {
                runtime::AttachEglSwapPacer(context, vm.ExecutionLock());
                egl_pacer_attached_ = true;
                bindings_.release_surface_currency();
            }
            // Surface geometry precedes renderer callbacks (GLSurfaceView
            // semantics; the pilot's onSurfaceCreated spins until size != -1).
            SynchronizeContentView();

            // A title that brings its own GLSurfaceView is waiting on these
            // before it will touch EGL; the intrinsic one ignores them.
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::created);
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::changed);
            runtime::DispatchAndroidGlobalLayout(vm, context);

            // ViewRootImpl establishes and sizes the Surface during its first
            // traversal; window focus arrives later through a separate
            // message. Keep that boundary instead of folding focus into
            // Start(): guest worker threads must first observe the completed
            // Surface setup before focus can act as a native resume signal.
            initial_focus_pending_ = true;

            // A renderer may not exist yet (installer phase draws nothing);
            // frames then only pump cooperative threads until the interpreted
            // glue registers one.
            if (context.renderer.IsValid())
                RunOnRenderer([this] { EnsureRendererCallbacks(); });

            state_ = LifecycleRunState::running;
            if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("running", false);
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
            previous_step_ns_ = 0;
            if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("running", false);
        } catch (...) {
            if (bindings_.bridge->Vm().ExitCode().has_value()) return Stop();
            MarkFailed();
            throw;
        }
        return State();
    }

    void DexActivityLifecycle::CancelInput() {
        if (state_ != LifecycleRunState::running) return;
        const auto cancelled = input_timeline_.Cancel(bindings_.context->uptime_millis.load() * 1000000);
        pending_input_.insert(pending_input_.end(), cancelled.begin(), cancelled.end());
        if (!pending_input_.empty()) DispatchInput();
    }

    void DexActivityLifecycle::QueueInput(
        const runtime::AndroidBoundaryInput& input) {
        if (state_ != LifecycleRunState::running || suspended_ || bindings_.context->process_stopping) return;
        auto logical = input;
        // Both delivery paths expose the process input inventory, not SDL ids.
        logical.device_id = input.type == runtime::AndroidBoundaryInputType::key
            ? runtime::kAndroidKeyboardDeviceId : runtime::kAndroidTouchDeviceId;
        const auto now = bindings_.context->uptime_millis.load() * 1000000;
        if (auto snapshot = input_timeline_.Stamp(std::move(logical), now))
            pending_input_.push_back(std::move(*snapshot));
    }

    void DexActivityLifecycle::DispatchInput() {
        auto& vm = bindings_.bridge->Vm();
        auto& context = *bindings_.context;
        for (const auto& input: pending_input_) {
            if (context.window_input_queue.IsValid()) {
                bindings_.bridge->Session().PushInput(input);
                continue;
            }
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
                                      "(JJIIIIIIII)V");
                if (!constructor.has_value()) {
                    Fail("KeyEvent has no timed input constructor");
                }
                RequireOutcome(
                    vm,
                    vm.Call(*constructor,
                            std::vector<dx::VmValue>{
                                dx::VmValue::Ref(key_event),
                                dx::VmValue::Long(input.down_time_ns / 1000000),
                                dx::VmValue::Long(input.event_time_ns / 1000000),
                                dx::VmValue::Int(input.action),
                                dx::VmValue::Int(input.code),
                                dx::VmValue::Int(input.repeat_count),
                                dx::VmValue::Int(input.meta_state),
                                dx::VmValue::Int(input.device_id),
                                dx::VmValue::Int(input.scan_code),
                                dx::VmValue::Int(input.flags),
                                dx::VmValue::Int(input.source)}),
                    "KeyEvent <init>");
                runtime::SetAndroidKeyEventUnicode(
                    vm, key_event, input.unicode_char);
                if (input.action == 2) {
                    CallActivity("onKeyMultiple", "(IILandroid/view/KeyEvent;)Z",
                        {dx::VmValue::Int(input.code), dx::VmValue::Int(input.repeat_count),
                         dx::VmValue::Ref(key_event)});
                    continue;
                }
                CallActivity(input.pressed ? "onKeyDown" : "onKeyUp",
                             "(ILandroid/view/KeyEvent;)Z",
                             {
                                 dx::VmValue::Int(input.code),
                                 dx::VmValue::Ref(key_event)
                             });
                continue;
            }
            const auto action = input.action & 0xff;
            pointer_x_ = input.x;
            pointer_y_ = input.y;
            if (action == kMotionActionDown) pointer_down_ = true;
            if (action == kMotionActionUp || action == kMotionActionCancel) pointer_down_ = false;
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
                                gesture_click_eligible_, gesture_touch_consumed_, &input);
                            if (cancelled.error.has_value()) Fail(*cancelled.error);
                        }
                        if (deep_touch_handle_ != 0U) {
                            const auto cancelled = DispatchDeepTouchEvent(
                                vm, context, kMotionActionCancel, pointer_x_,
                                pointer_y_, deep_touch_handle_, &input);
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
            if (action == kMotionActionUp || action == kMotionActionCancel) {
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
                    gesture_touch_consumed_, &input);
                if (result.error.has_value()) Fail(*result.error);
                gesture_click_eligible_ = result.click_eligible;
                gesture_touch_consumed_ = result.touch_consumed;
                if (!result.keep_capture) gesture_candidate_ = 0U;
                if (result.handled) continue;
            }
            if (!had_listener_candidate) {
                const auto result = DispatchDeepTouchEvent(
                    vm, *bindings_.context, action, pointer_x_, pointer_y_,
                    deep_touch_handle_, &input);
                if (result.error.has_value()) Fail(*result.error);
                deep_touch_handle_ = result.captured_view;
                if (result.handled) continue;
            }
            const auto event = runtime::MakeMotionEvent(
                vm, input);
            CallActivity("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
                         {dx::VmValue::Ref(event)});
        }
        pending_input_.clear();
    }

    std::int64_t VideoClockAdvanceMillis(const std::uint64_t elapsed_ms,
                                        const std::int64_t advanced_ms) noexcept {
        const auto elapsed = static_cast<std::int64_t>(
            std::clamp<std::uint64_t>(elapsed_ms, 16U, 100U));
        return std::max<std::int64_t>(0, elapsed - std::max<std::int64_t>(0, advanced_ms));
    }

    void DexActivityLifecycle::SetRealtimeVideoClock(const bool enabled) noexcept {
        realtime_video_clock_ = enabled;
        previous_step_ns_ = 0;
    }

    LifecycleFrameState DexActivityLifecycle::StepFrame() {
        if (state_ != LifecycleRunState::running) {
            Fail("dex_activity lifecycle is not running");
        }
        if (suspended_) return State();
        const auto wall_ns = hal::Clock::SteadyTimestampNs();
        const auto elapsed_ms = previous_step_ns_ == 0 ? 16U
            : (wall_ns - previous_step_ns_) / 1'000'000U;
        // Keep fractional milliseconds across steps instead of losing a
        // fraction of the playback duration on every frame.
        previous_step_ns_ = previous_step_ns_ == 0 ? wall_ns
            : previous_step_ns_ + elapsed_ms * 1'000'000U;
        const auto uptime_before = bindings_.context->uptime_millis.load();
        try {
            RethrowFatalThreadFailure();
            auto& context = *bindings_.context;
            {
              const dx::VmExecutionLockScope execution(bindings_.bridge->Vm().ExecutionLock());
              if (context.ui_tree.Get(context.ui_tree.Root())->layout_dirty) {
                runtime::ui::LayoutUiTree(context.ui_tree, {
                    static_cast<std::int32_t>(context.surface_width),
                    static_cast<std::int32_t>(context.surface_height)});
                runtime::DispatchAndroidGlobalLayout(bindings_.bridge->Vm(), context);
              }
            }
            DispatchInput();
            PumpJavaThreads();
            PumpVideo();
            PumpAudioTracks();
            ServiceActivitySwitch();
            SynchronizeContentView();
            CompleteWindowHandoffs();
            if (initial_focus_pending_) {
                if (activity_started_) SetWindowFocus(true);
                initial_focus_pending_ = false;
            }
            if (context.renderer.IsValid()) {
                RunOnRenderer([this] {
                  auto& context = *bindings_.context;
                  auto runtime = RendererState();
                  if (runtime->paused || !runtime->surface_available) return;
                  EnsureRendererCallbacks();
                  RunRendererEvents();
                  if (ConsumeGlSurfaceDrawRequest(context)) {
                    CallOnView(runtime->renderer, "onDrawFrame",
                               "(Ljavax/microedition/khronos/opengles/GL10;)V",
                               {dx::VmValue::Ref(runtime->renderer_gl)});
                    const auto swapped = CallOnView(
                        runtime->renderer_egl, "eglSwapBuffers",
                        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                        {dx::VmValue::Ref(runtime->renderer_display), dx::VmValue::Ref(runtime->renderer_surface)});
                    if (!swapped.AsInt()) {
                        const auto error = CallOnView(runtime->renderer_egl, "eglGetError", "()I", {}).AsInt();
                        if (error == 0x300e) ReleaseRendererEgl(*runtime);
                        else Fail("renderer eglSwapBuffers failed: EGL error=" + std::to_string(error));
                    }
                  }
                });
            } else if (!bindings_.context->renderer.IsValid() &&
                       bindings_.context->active_surface_holders.empty() &&
                       runtime::AnyVideoPlaying(*bindings_.context) == false &&
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
                realtime_video_clock_ && runtime::AnyVideoPlaying(context)
                    ? VideoClockAdvanceMillis(elapsed_ms,
                        context.uptime_millis.load() - uptime_before)
                    : kMillisPerFrame);
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
        const dx::VmExecutionLockScope execution(bindings_.bridge->Vm().ExecutionLock());
        auto& context = *bindings_.context;
        runtime::RefreshAndroidImageDrawables(context);
        if (context.ui_tree.Get(context.ui_tree.Root())->layout_dirty) {
            runtime::ui::LayoutUiTree(
                context.ui_tree,
                {
                    static_cast<std::int32_t>(frame.width),
                    static_cast<std::int32_t>(frame.height)
                });
        }
        runtime::ComposeVideoViews(context, frame.rgba8, frame.width, frame.height, false);
        const auto& overlay = context.ui_overlay_renderer.Render(
            context.ui_tree, context.ui_bitmaps,
            {
                static_cast<std::int32_t>(frame.width),
                static_cast<std::int32_t>(frame.height)
            });
        ComposeUiOverlayInPlace(frame.rgba8, overlay);
        runtime::ComposeVideoViews(context, frame.rgba8, frame.width, frame.height, true);
        return frame;
    }

    void DexActivityLifecycle::RethrowFatalThreadFailure() {
        bindings_.bridge->Session().RethrowAsyncFailure();
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
        RethrowFatalThreadFailure();
        const auto error = runtime::PumpJavaThreads(bindings_.bridge->Vm(),
                                                    *bindings_.context);
        if (error.has_value()) Fail(*error);
    }

    void DexActivityLifecycle::PumpVideo() {
        const dx::VmExecutionLockScope execution(bindings_.bridge->Vm().ExecutionLock());
        {
            std::scoped_lock lock(bindings_.context->video_views_mutex);
            if (bindings_.context->video_views.empty()) {
                return;
            }
        }
        const auto error = runtime::PumpVideoViews(
            bindings_.bridge->Vm(), *bindings_.context,
            [this](std::vector<std::uint8_t> rgba8) {
                const auto& context = *bindings_.context;
                // GLES/Canvas producers keep authority over the base frame.
                // Video pixels are composed once at the frontend handoff.
                if (!runtime::HasOpaqueFullscreenVideo(context) &&
                    (context.renderer.IsValid() || !context.active_surface_holders.empty() ||
                     !context.holder_canvases.empty())) return;
                rgba8.assign(static_cast<std::size_t>(context.surface_width) * context.surface_height * 4U, 0U);
                for (std::size_t i = 3; i < rgba8.size(); i += 4) rgba8[i] = 255U;
                if (bindings_.publish_video_frame) bindings_.publish_video_frame(std::move(rgba8));
            }, false);
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

    void DexActivityLifecycle::StartCurrentActivity() {
        auto& context = *bindings_.context;
        auto& record = context.activity_stack.back();
        if (!record.started || record.stopped) {
            if (record.stopped) CallActivity("onRestart", "()V", {});
            activity_started_ = true;
            CallActivity("onStart", "()V", {});
            record.started = true;
            record.stopped = false;
        } else activity_started_ = true;
        const auto transitioning = [&] {
            return std::ranges::any_of(context.activity_commands, [&](const auto& command) {
                return command.kind == runtime::DexVmAndroidContext::ActivityCommand::Kind::launch ||
                       command.owner == context.activity;
            });
        };
        if (transitioning() || runtime::SessionExitRequested(context)) return;
        DeliverActivityResults();
        if (!transitioning() && !runtime::SessionExitRequested(context)) {
            CallActivity("onResume", "()V", {});
            activity_resumed_ = true;
            context.activity_stack.back().resumed = true;
            record.resumed = true;
        }
    }

    void DexActivityLifecycle::DeliverActivityResults() {
        auto& context = *bindings_.context;
        auto& vm = bindings_.bridge->Vm();
        auto results = std::move(context.activity_stack.back().results);
        context.activity_stack.back().results.clear();
        std::vector<dx::VmObjectRef> data;
        for (const auto& result : results) data.push_back(result.data);
        const auto roots = vm.ProtectReferences(data);
        for (const auto& result : results)
            CallActivity("onActivityResult", "(IILandroid/content/Intent;)V",
                {dx::VmValue::Int(result.request_code), dx::VmValue::Int(result.result_code), dx::VmValue::Ref(result.data)});
    }

    void DexActivityLifecycle::SetActivityWindowVisible(const dx::VmObjectRef activity, const bool visible) {
        auto& context = *bindings_.context;
        const auto found = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
            [&](const auto& record) { return record.object == activity; });
        if (found == context.activity_stack.end() || !found->window_root || found->visible == visible) return;
        found->visible = visible;
        context.ui_tree.SetWindowVisible(*found->window_root, visible);
        std::vector<dx::VmObjectRef> views;
        std::vector<runtime::ui::UiNodeId> nodes{*found->window_root};
        while (!nodes.empty()) {
            const auto node = nodes.back(); nodes.pop_back();
            const auto* state = context.ui_tree.Get(node);
            if (!state) continue;
            nodes.insert(nodes.end(), state->children.rbegin(), state->children.rend());
            const auto view = runtime::ViewObjectForUiNode(context, node);
            if (view.IsValid()) views.push_back(view);
        }
        auto& vm = bindings_.bridge->Vm();
        const auto roots = vm.ProtectReferences(views);
        for (const auto view : views)
            CallOnView(view, "onWindowVisibilityChanged", "(I)V", {dx::VmValue::Int(visible ? 0 : 4)});
    }

    void DexActivityLifecycle::RestoreActivity() {
        auto& context = *bindings_.context;
        auto& record = context.activity_stack.back();
        context.activity = record.object;
        context.current_intent = record.intent;
        context.content_view = record.content;
        context.focused_edit_text = record.focused_edit_text;
        context.gl_surface_renderer_view = record.render_view;
        context.renderer = dx::VmObjectRef{};
        if (const auto state = context.gl_surface_runtimes.find(record.render_view.Value()); state != context.gl_surface_runtimes.end()) {
            ProjectRenderer(context, *state->second);
        }
        context.window_focus_activity.store(record.object.Value());
        activity_started_ = false;
        activity_resumed_ = false;
        SetActivityWindowVisible(record.object, true);
        if (record.content.IsValid()) {
            const auto node = runtime::FindViewUiNode(context, record.content.Value());
            if (node && !context.ui_tree.Get(*node)->parent)
                context.ui_tree.Attach(context.ui_tree.Root(), *node);
        }
        if (record.focused_node && context.ui_tree.Get(*record.focused_node))
            static_cast<void>(context.ui_tree.RequestFocus(*record.focused_node, false));
        DeliverActivityResults();
        StartCurrentActivity();
        SynchronizeContentView();
        if (activity_resumed_) SetWindowFocus(true);
    }

    void DexActivityLifecycle::ServiceActivitySwitch() {
        auto& context = *bindings_.context;
        auto& vm = bindings_.bridge->Vm();
        const dx::VmExecutionLockScope execution(vm.ExecutionLock());
        using Command = runtime::DexVmAndroidContext::ActivityCommand;
        const auto refresh = [&] {
            context.activity_stack_depth.store(context.activity_stack.size());
            context.activity_switch_pending.store(!context.activity_commands.empty());
            context.pending_activity_descriptor = context.activity_commands.empty() ? "" : context.activity_commands.front().descriptor;
            context.pending_activity_component_name = context.activity_commands.empty() ? "" : context.activity_commands.front().component;
        };
        const auto retire_active = [&] {
            CancelInput();
            SetWindowFocus(false);
            if (activity_resumed_) CallActivity("onPause", "()V", {});
            if (activity_started_) CallActivity("onStop", "()V", {});
            if (!context.activity_stack.empty() && context.activity_stack.back().object == context.activity) {
                auto& record = context.activity_stack.back();
                record.content = context.content_view;
                record.focused_node = context.ui_tree.Focused();
                record.focused_edit_text = context.focused_edit_text;
            }
            if (context.window_surface_callback.IsValid()) {
                if (const auto error = runtime::RetireSurfaceHolderGeneration(vm, context)) Fail(*error);
            } else if (context.content_view.IsValid()) {
                const auto node = runtime::FindViewUiNode(context, context.content_view.Value());
                if (node) {
                    if (const auto error = runtime::DetachSurfaceViewSubtree(vm, context, *node)) Fail(*error);
                }
            }
            StopRendererThread();
            if (context.content_view.IsValid()) {
                const auto node = runtime::FindViewUiNode(context, context.content_view.Value());
                if (node) context.ui_tree.SetWindowVisible(*node, false);
            }
            context.content_view = dx::VmObjectRef{};
            context.focused_edit_text = dx::VmObjectRef{};
            context.renderer = dx::VmObjectRef{}; context.egl_context_factory = dx::VmObjectRef{}; context.egl_config_chooser = dx::VmObjectRef{};
            sized_content_view_ = dx::VmObjectRef{}; sized_content_node_.reset();
            gesture_candidate_ = 0; gesture_click_eligible_ = false; gesture_touch_consumed_ = false; deep_touch_handle_ = 0;
            activity_started_ = false; activity_resumed_ = false;
        };
        std::size_t serviced{};
        while (!context.activity_commands.empty()) {
            if (++serviced > 128) Fail("Activity transition limit exceeded");
            auto command = std::move(context.activity_commands.front());
            context.activity_commands.pop_front();
            const auto command_roots = vm.ProtectReferences(std::array{command.owner, command.intent});
            refresh();
            if (command.kind == Command::Kind::finish) {
                const auto found = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                    [&](const auto& record) { return record.object == command.owner; });
                if (found == context.activity_stack.end()) continue;
                const bool top = std::next(found) == context.activity_stack.end();
                if (top && context.activity_stack.size() == 1) {
                    context.finishing_activity.store(command.owner.Value());
                    const bool launching = std::ranges::any_of(context.activity_commands, [](const auto& item) {
                        return item.kind == Command::Kind::launch;
                    });
                    if (!launching) {
                        context.activity_commands.clear();
                        refresh();
                        return;
                    }
                    // The last instance remains alive until an already queued
                    // launch can establish the replacement foreground.
                    context.activity_commands.push_back(std::move(command));
                    refresh();
                    continue;
                }
                if (top) {
                    found->content = context.content_view;
                    found->render_view = context.gl_surface_renderer_view;
                }
                if (top) retire_active();
                const auto old = *found;
                StopRendererThread(old.render_view);
                // Background finish destroys that instance without changing foreground identity.
                CallOnView(old.object, "onDestroy", "()V", {});
                if (old.window_root) {
                    runtime::RetireViewUiSubtree(context, *old.window_root);
                } else if (old.content.IsValid()) {
                    const auto node = runtime::FindViewUiNode(context, old.content.Value());
                    if (node) runtime::RetireViewUiSubtree(context, *node);
                }
                context.activity_stack.erase(found);
                if (old.request_code >= 0 && old.caller.IsValid()) {
                    const auto caller = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                        [&](const auto& record) { return record.object == old.caller; });
                    if (caller != context.activity_stack.end())
                        caller->results.push_back({old.request_code, command.result_code, command.intent});
                }
                auto finishing = command.owner.Value();
                context.finishing_activity.compare_exchange_strong(finishing, 0U);
                refresh();
                if (top) RestoreActivity();
                continue;
            }
            if (context.activity_stack.size() >= 32) Fail("Activity stack limit exceeded");
            const bool retiring = std::ranges::any_of(context.activity_commands, [&](const auto& item) {
                return item.kind == Command::Kind::finish && item.owner == context.activity;
            });
            if (!retiring && (context.window_surface_callback.IsValid() || runtime::AnyVideoPlaying(context))) {
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.activity_result", 0);
                // This deferred command runs outside vm.Call: a VmJavaThrow
                // here would escape conversion into a Java throwable.
                Fail("Activity switch failed: retaining an Activity with live renderer, "
                     "native surface or video is unsupported");
            }
            const auto source = context.activity;
            const bool source_started = activity_started_;
            if (!context.activity_stack.empty()) {
                auto& record = context.activity_stack.back();
                record.content = context.content_view;
                record.focused_node = context.ui_tree.Focused();
                record.focused_edit_text = context.focused_edit_text;
                record.intent = CallOnView(record.object, "getIntent", "()Landroid/content/Intent;", {}).ref;
                record.started = activity_started_;
                record.render_view = context.gl_surface_renderer_view;
                if (retiring) retire_active();
                else {
                    CancelInput();
                    SetWindowFocus(false);
                    if (activity_resumed_) CallActivity("onPause", "()V", {});
                    record.resumed = false;
                    context.renderer = dx::VmObjectRef{};
                    context.gl_surface_renderer_view = dx::VmObjectRef{};
                    context.egl_context_factory = dx::VmObjectRef{};
                    context.egl_config_chooser = dx::VmObjectRef{};
                    context.renderer_egl = dx::VmObjectRef{}; context.renderer_display = dx::VmObjectRef{}; context.renderer_config = dx::VmObjectRef{};
                    context.renderer_context = dx::VmObjectRef{}; context.renderer_surface = dx::VmObjectRef{}; context.renderer_gl = dx::VmObjectRef{};
                    context.content_view = dx::VmObjectRef{};
                    context.focused_edit_text = dx::VmObjectRef{};
                    sized_content_view_ = dx::VmObjectRef{};
                    sized_content_node_.reset();
                    activity_started_ = false;
                    activity_resumed_ = false;
                }
            }
            auto& linker = vm.Linker();
            const auto type = linker.FindClass(command.descriptor);
            if (!type) Fail("startActivity target is not in the dex: " + command.descriptor);
            RequireOutcome(vm, vm.EnsureClassInitialized(*type), "activity <clinit>");
            const auto init = linker.FindDirectMethod(*type, "<init>", "()V");
            if (!init) Fail("Activity has no default constructor");
            const auto activity = vm.Model().NewInstance(*type, linker.Class(*type).instance_slots);
            context.activity = activity; context.current_intent = command.intent;
            context.activity_stack.push_back({activity, command.intent, dx::VmObjectRef{},
                command.request_code >= 0 ? command.owner : dx::VmObjectRef{}, command.request_code});
            refresh();
            context.window_focus_activity.store(activity.Value());
            RequireOutcome(vm, vm.Call(*init, std::array{dx::VmValue::Ref(activity)}), "activity <init>");
            AttachBaseContext(vm, linker, *type, activity, context.application_base_context, "Activity");
            runtime::AttachAndroidActivityIdentity(vm, bindings_.context, activity, command.component);
            CallActivity("onCreate", "(Landroid/os/Bundle;)V", {dx::VmValue::Ref(dx::VmObjectRef{})});
            const bool own_transition = std::ranges::any_of(context.activity_commands, [&](const auto& item) {
                return item.kind == Command::Kind::launch || item.owner == activity;
            });
            if (!own_transition && !runtime::SessionExitRequested(context)) StartCurrentActivity();
            SynchronizeContentView();
            if (activity_resumed_) SetWindowFocus(true);
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::created);
            DispatchSurfaceHolder(runtime::SurfaceHolderPhase::changed);
            runtime::DispatchAndroidGlobalLayout(vm, context);
            if (!retiring) window_handoffs_.push_back({source, activity, source_started});
            CompleteWindowHandoffs();
        }
        refresh();
        CompleteWindowHandoffs();
        if (activity_started_ && !activity_resumed_ && !runtime::SessionExitRequested(context)) {
            CallActivity("onResume", "()V", {});
            activity_resumed_ = true;
        }
    }

    void DexActivityLifecycle::CompleteWindowHandoffs() {
        auto& context = *bindings_.context;
        auto& vm = bindings_.bridge->Vm();
        const auto current = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
            [&](const auto& record) { return record.object == context.activity; });
        for (auto pending = window_handoffs_.begin(); pending != window_handoffs_.end();) {
            const auto source = std::find_if(context.activity_stack.begin(), context.activity_stack.end(),
                [&](const auto& record) { return record.object == pending->source; });
            if (source == context.activity_stack.end() || source == current) {
                pending = window_handoffs_.erase(pending);
                continue;
            }
            if (current == context.activity_stack.end() || current < source || !current->resumed ||
                !current->window_root || !current->visible) { ++pending; continue; }
            if (pending->source_started && !source->stopped) {
                CallOnView(source->object, "onStop", "()V", {});
                source->stopped = true;
            }
            if (const auto state = context.gl_surface_runtimes.find(source->render_view.Value()); state != context.gl_surface_runtimes.end())
                RunOnRenderer(state->second, [this, runtime = state->second] { ReleaseRendererEgl(*runtime, true); });
            if (source->content.IsValid()) {
                const auto node = runtime::FindViewUiNode(context, source->content.Value());
                if (node) {
                    if (const auto error = runtime::DetachSurfaceViewSubtree(vm, context, *node)) Fail(*error);
                }
            }
            SetActivityWindowVisible(source->object, false);
            pending = window_handoffs_.erase(pending);
        }
    }

    void DexActivityLifecycle::SynchronizeContentView() {
        const dx::VmExecutionLockScope execution(bindings_.bridge->Vm().ExecutionLock());
        auto& context = *bindings_.context;
        const auto content = context.content_view;
        const auto node = content.IsValid()
            ? runtime::FindViewUiNode(context, content.Value()) : std::nullopt;
        std::shared_ptr<GlSurfaceRuntime> selected;
        if (node) {
            for (const auto& [_, state] : context.gl_surface_runtimes) {
                if (!state->renderer.IsValid()) continue;
                auto current = runtime::FindViewUiNode(context, state->view.Value());
                if (!current || !context.ui_tree.IsVisible(*current)) continue;
                while (current && *current != *node) current = context.ui_tree.Get(*current)->parent;
                if (!current) continue;
                if (selected) Fail("multiple intrinsic GL producers in one content tree are unsupported");
                selected = state;
            }
        }
        context.renderer = selected ? selected->renderer : dx::VmObjectRef{};
        context.gl_surface_renderer_view = selected ? selected->view : dx::VmObjectRef{};
        if (selected) { selected->owner = context.activity; ProjectRenderer(context, *selected); }
        const bool content_changed=content!=sized_content_view_ || node!=sized_content_node_;
        sized_content_view_ = content;
        sized_content_node_ = node;
        if (!content.IsValid()) return;
        const auto roots = bindings_.bridge->Vm().ProtectReferences(std::array{content});
        const bool layout_dirty = context.ui_tree.Get(context.ui_tree.Root())->layout_dirty;
        const bool layout_changed=runtime::DispatchAndroidViewSizes(bindings_.bridge->Vm(),context);
        if (!content_changed && !layout_changed && !layout_dirty) return;
        // A virtual size callback may replace its own content. Reconcile the
        // replacement next frame; never attach the retired subtree.
        if (context.content_view != content ||
            runtime::FindViewUiNode(context, content.Value()) != node) return;
        if (!context.managed_host_surface_open) return;
        if (node.has_value()) {
            const auto error = runtime::AttachSurfaceViewSubtree(
                bindings_.bridge->Vm(), context, *node);
            if (error.has_value()) Fail(*error);
        }
        if (content_changed || layout_changed) {
            runtime::DispatchAndroidGlobalLayout(bindings_.bridge->Vm(), context);
            if (context.window_has_focus.load()) DispatchViewWindowFocus(true);
        }
    }


    std::shared_ptr<DexActivityLifecycle::GlSurfaceRuntime> DexActivityLifecycle::RendererState() {
        const auto token = bindings_.bridge->Vm().CurrentContextToken();
        for (const auto& [_, worker] : renderer_threads_)
            if (worker->context_token == token) return worker->state;
        const auto found = bindings_.context->gl_surface_runtimes.find(bindings_.context->gl_surface_renderer_view.Value());
        if (found == bindings_.context->gl_surface_runtimes.end()) Fail("renderer has no instance runtime");
        return found->second;
    }

    void DexActivityLifecycle::RunRendererEvents() {
        auto state = RendererState();
        auto& vm = bindings_.bridge->Vm();
        std::vector<dx::VmObjectRef> events;
        { std::scoped_lock lock(bindings_.context->scheduler_mutex); events.swap(state->events); }
        const auto roots = vm.ProtectReferences(events);
        for (const auto event : events) CallOnView(event, "run", "()V", {});
    }

    void DexActivityLifecycle::EnsureRendererThread(const std::shared_ptr<GlSurfaceRuntime>& state) {
        if (renderer_threads_.contains(state->view.Value())) return;
        auto& vm = bindings_.bridge->Vm();
        const dx::VmExecutionLockScope execution(vm.ExecutionLock());
        if (bindings_.release_surface_currency) bindings_.release_surface_currency();
        auto thread = std::make_unique<RendererThread>();
        thread->state = state;
        thread->object = vm.NewIntrinsicInstance("Landroid/opengl/GLSurfaceView$GLThread;");
        const auto roots = vm.ProtectReferences(std::array{thread->object});
        const auto type = vm.Linker().ResolveDescriptor("Ljava/lang/Thread;");
        const auto ctor = vm.Linker().FindDirectMethod(type, "<init>", "(Ljava/lang/String;)V");
        if (!ctor) Fail("GLThread requires Thread(String)");
        RequireOutcome(vm, vm.Call(*ctor, std::array{dx::VmValue::Ref(thread->object),
            dx::VmValue::Ref(vm.NewStringUtf8("GLThread"))}), "GLThread <init>");
        auto* worker = thread.get();
        renderer_threads_.emplace(state->view.Value(), std::move(thread));
        bindings_.context->run_gl_surface_thread = [this](const dx::VmObjectRef object) { RendererThreadBody(object); };
        state->wake = [worker] {
            std::scoped_lock lock(worker->mutex);
            worker->events = true;
            worker->changed.notify_all();
        };
        state->pause = [this, weak = std::weak_ptr<GlSurfaceRuntime>{state}](const bool paused) {
            const auto runtime = weak.lock();
            if (!runtime || runtime->stopped) Fail("GLThread is stopped");
            RunOnRenderer(runtime, [this, runtime, paused] {
                if (paused && !runtime->paused) {
                    const bool preserve = CallOnView(runtime->view, "getPreserveEGLContextOnPause", "()Z", {}).AsInt() != 0;
                    ReleaseRendererEgl(*runtime, preserve);
                }
                runtime->paused = paused;
            });
        };
        try { CallOnView(worker->object, "start", "()V", {}); }
        catch (...) {
            state->wake = {}; state->pause = {};
            renderer_threads_.erase(state->view.Value());
            throw;
        }
    }

    void DexActivityLifecycle::RendererThreadBody(const dx::VmObjectRef object) {
        RendererThread* current{};
        for (const auto& [_, candidate] : renderer_threads_)
            if (candidate->object == object) current = candidate.get();
        if (!current) Fail("GLThread has no owning View");
        auto& worker = *current;
        auto& vm = bindings_.bridge->Vm();
        auto& execution = vm.ExecutionLock();
        worker.context_token = vm.CurrentContextToken();
        auto& state = *worker.state;
        try {
            for (;;) {
                std::function<void()> action;
                bool events = false;
                {
                    std::unique_lock lock(worker.mutex);
                    if (worker.stopping || vm.Threads().ShuttingDown()) break;
                    if (!worker.action && !worker.events) {
                        vm.Threads().SetWaitState(vm.CurrentContextToken(), dx::VmThreadWaitState::monitor);
                        const auto depth = execution.ReleaseForBlocking();
                        worker.changed.wait_for(lock, std::chrono::milliseconds(2));
                        lock.unlock();
                        execution.ReacquireAfterBlocking(depth);
                        vm.Threads().SetWaitState(vm.CurrentContextToken(), dx::VmThreadWaitState::none);
                        continue;
                    }
                    action.swap(worker.action);
                    events = std::exchange(worker.events, false);
                }
                if (action) action();
                if (events) {
                    if (!state.paused && state.surface_available) EnsureRendererCallbacks();
                    RunRendererEvents();
                }
                {
                    std::scoped_lock lock(worker.mutex);
                    if (action) worker.busy = false;
                    worker.changed.notify_all();
                }
            }
        } catch (const dx::DexVmError& error) {
            if (error.Reason() != dx::DexVmErrorReason::thread_stopped ||
                (!vm.ExitCode() && !bindings_.bridge->Session().NativeExitCode())) {
                std::scoped_lock lock(worker.mutex);
                worker.failure = std::current_exception();
            }
        } catch (...) {
            std::scoped_lock lock(worker.mutex);
            worker.failure = std::current_exception();
        }
        const bool guest_live = !runtime::SessionExitRequested(*bindings_.context) &&
            !vm.ExitCode() && !bindings_.bridge->Session().NativeExitCode();
        if (guest_live && state.ready && !worker.failure && !vm.Threads().ShuttingDown()) {
            try {
                auto& linker = bindings_.bridge->Linker();
                const auto type = vm.Model().ObjectClass(state.renderer);
                if (linker.FindVtableIndex(type, "surfaceDestroyed",
                        "(Ljavax/microedition/khronos/opengles/GL10;)V"))
                    CallOnView(state.renderer, "surfaceDestroyed",
                        "(Ljavax/microedition/khronos/opengles/GL10;)V",
                        {dx::VmValue::Ref(dx::VmObjectRef{})});
            } catch (...) { worker.failure = std::current_exception(); }
        }
        try { if (guest_live) ReleaseRendererEgl(state); }
        catch (...) { if (!worker.failure) worker.failure = std::current_exception(); }
        std::scoped_lock lock(worker.mutex);
        state.stopped = true;
        worker.exited = true; worker.busy = false;
        worker.changed.notify_all();
    }

    void DexActivityLifecycle::RunOnRenderer(std::function<void()> action) {
        RunOnRenderer(RendererState(), std::move(action));
    }

    void DexActivityLifecycle::RunOnRenderer(const std::shared_ptr<GlSurfaceRuntime>& state, std::function<void()> action) {
        EnsureRendererThread(state);
        auto& worker = *renderer_threads_.at(state->view.Value());
        auto& vm = bindings_.bridge->Vm();
        const dx::VmExecutionLockScope execution(vm.ExecutionLock());
        {
            std::scoped_lock lock(worker.mutex);
            if (worker.failure) std::rethrow_exception(worker.failure);
            if (worker.exited || worker.stopping) Fail("GLThread is stopped");
            if (worker.busy) Fail("recursive GLThread submission");
            worker.action = std::move(action); worker.busy = true;
            worker.changed.notify_all();
        }
        for (;;) {
            {
                std::unique_lock lock(worker.mutex);
                if (worker.failure) std::rethrow_exception(worker.failure);
                if (worker.exited) Fail("GLThread stopped before completing its command");
                if (!worker.busy) return;
                if (!vm.Threads().IsAlive(worker.object)) {
                    lock.unlock(); RethrowFatalThreadFailure();
                    Fail("GLThread terminated before completing its command");
                }
                vm.Threads().SetWaitState(dx::kRootLifecycleToken, dx::VmThreadWaitState::joining);
                const auto depth = vm.ExecutionLock().ReleaseForBlocking();
                worker.changed.wait_for(lock, std::chrono::milliseconds(2));
                lock.unlock();
                vm.ExecutionLock().ReacquireAfterBlocking(depth);
                vm.Threads().SetWaitState(dx::kRootLifecycleToken, dx::VmThreadWaitState::none);
                vm.CheckExecutionDeadline();
                if (bindings_.pump_host_events) {
                    const auto unlocked = vm.ExecutionLock().ReleaseForBlocking();
                    try { bindings_.pump_host_events(); }
                    catch (...) { vm.ExecutionLock().ReacquireAfterBlocking(unlocked); throw; }
                    vm.ExecutionLock().ReacquireAfterBlocking(unlocked);
                }
            }
            PumpJavaThreads();
        }
    }

    void DexActivityLifecycle::StopRendererThread() {
        StopRendererThread(bindings_.context->gl_surface_renderer_view);
    }

    void DexActivityLifecycle::StopRendererThread(const dx::VmObjectRef view) {
        const auto found = renderer_threads_.find(view.Value());
        if (found == renderer_threads_.end()) return;
        auto& worker = *found->second;
        auto& vm = bindings_.bridge->Vm();
        const dx::VmExecutionLockScope execution(vm.ExecutionLock());
        { std::scoped_lock lock(worker.mutex); worker.stopping = true; worker.changed.notify_all(); }
        std::exception_ptr wait_failure;
        while (vm.Threads().IsAlive(worker.object)) {
            std::unique_lock lock(worker.mutex);
            vm.Threads().SetWaitState(dx::kRootLifecycleToken, dx::VmThreadWaitState::joining);
            const auto depth = vm.ExecutionLock().ReleaseForBlocking();
            worker.changed.wait_for(lock, std::chrono::milliseconds(2));
            lock.unlock(); vm.ExecutionLock().ReacquireAfterBlocking(depth);
            vm.Threads().SetWaitState(dx::kRootLifecycleToken, dx::VmThreadWaitState::none);
            vm.CheckExecutionDeadline();
            if (!vm.Threads().ShuttingDown()) {
                try { PumpJavaThreads(); }
                catch (...) { if (!wait_failure) wait_failure = std::current_exception(); }
            }
        }
        const auto failure = worker.failure;
        worker.state->wake = {}; worker.state->pause = {};
        renderer_threads_.erase(found);
        if (renderer_threads_.empty()) bindings_.context->run_gl_surface_thread = {};
        if (failure) std::rethrow_exception(failure);
        if (wait_failure) std::rethrow_exception(wait_failure);
    }

    void DexActivityLifecycle::StopAllRenderers() {
        std::vector<dx::VmObjectRef> views;
        for (const auto& [_, worker] : renderer_threads_) views.push_back(worker->state->view);
        std::exception_ptr failure;
        for (const auto view : views) {
            try { StopRendererThread(view); }
            catch (...) { if (!failure) failure = std::current_exception(); }
        }
        if (failure) std::rethrow_exception(failure);
    }

    void DexActivityLifecycle::InitializeRendererEgl(GlSurfaceRuntime& r) {
        auto& c = *bindings_.context;
        if (r.renderer_surface.IsValid()) return;
        auto& vm = bindings_.bridge->Vm();
        const auto ref = dx::VmValue::Ref;
        const auto integer = dx::VmValue::Int;
        const auto on = [&](dx::VmObjectRef receiver, const char* name, const char* signature,
                            std::vector<dx::VmValue> args = {}) {
            return CallOnView(receiver, name, signature, std::move(args));
        };
        const auto ints = [&](const std::vector<std::int32_t>& values) {
            const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[I"),
                runtime::JniPrimitiveKind::integer, static_cast<runtime::JniSize>(values.size()));
            for (std::size_t i = 0; i < values.size(); ++i)
                vm.Model().SetPrimitiveElement(array, static_cast<runtime::JniSize>(i), static_cast<std::uint32_t>(values[i]));
            return array;
        };
        if (!r.renderer_context.IsValid()) {
        const auto type = vm.Linker().ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLContext;");
        const auto get = vm.Linker().FindDirectMethod(type, "getEGL", "()Ljavax/microedition/khronos/egl/EGL;");
        if (!get) Fail("EGLContext.getEGL is unavailable");
        const auto result = vm.Call(*get, {});
        RequireOutcome(vm, result, "EGLContext.getEGL");
        r.renderer_egl = result.value.ref;
        r.renderer_display = on(r.renderer_egl, "eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;", {ref(dx::VmObjectRef{})}).ref;
        if (!on(r.renderer_egl, "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
                {ref(r.renderer_display), ref(dx::VmObjectRef{})}).AsInt()) Fail("renderer eglInitialize failed");
        if (r.egl_config_chooser.IsValid()) {
            r.renderer_config = on(r.egl_config_chooser, "chooseConfig",
                "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;)Ljavax/microedition/khronos/egl/EGLConfig;",
                {ref(r.renderer_egl), ref(r.renderer_display)}).ref;
        } else {
            const auto version_it = c.gl_surface_client_versions.find(r.view.Value());
            const int version = version_it == c.gl_surface_client_versions.end() ? 1 : version_it->second;
            std::vector<std::int32_t> attrs{0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3025, 16};
            if (const auto it = c.gl_surface_config_specs.find(r.view.Value()); it != c.gl_surface_config_specs.end()) {
                attrs = {0x3024,it->second[0],0x3023,it->second[1],0x3022,it->second[2],0x3021,it->second[3],0x3025,it->second[4],0x3026,it->second[5]};
            }
            attrs.insert(attrs.end(), {0x3040, version >= 3 ? 0x40 : version == 2 ? 4 : 1, 0x3038});
            const auto attributes = ints(attrs); const auto ar = vm.ProtectReferences(std::array{attributes});
            const auto count = ints({0}); const auto cr = vm.ProtectReferences(std::array{count});
            const auto configs = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljavax/microedition/khronos/egl/EGLConfig;"), vm.Linker().ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;"), 1);
            const auto roots = vm.ProtectReferences(std::array{configs});
            if (!on(r.renderer_egl, "eglChooseConfig", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {ref(r.renderer_display),ref(attributes),ref(configs),integer(1),ref(count)}).AsInt() || vm.Model().GetPrimitiveElement(count,0)==0)
                Fail("renderer EGL config is unavailable");
            r.renderer_config = vm.Model().GetObjectElement(configs,0);
        }
        if (!r.renderer_config.IsValid()) {
            std::string message = "renderer config chooser returned null";
            if (r.egl_config_chooser.IsValid())
                message += "; chooser=" + vm.Linker().Class(vm.Model().ObjectClass(r.egl_config_chooser)).descriptor;
            try {
                const auto count = ints({0}); const auto cr = vm.ProtectReferences(std::array{count});
                if (on(r.renderer_egl,"eglGetConfigs",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {ref(r.renderer_display),ref(dx::VmObjectRef{}),integer(0),ref(count)}).AsInt()) {
                    const auto size = std::min<std::uint64_t>(vm.Model().GetPrimitiveElement(count,0),16U);
                    const auto configs = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljavax/microedition/khronos/egl/EGLConfig;"),
                        vm.Linker().ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;"),static_cast<runtime::JniSize>(size));
                    const auto roots = vm.ProtectReferences(std::array{configs});
                    if (on(r.renderer_egl,"eglGetConfigs",
                        "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                        {ref(r.renderer_display),ref(configs),integer(static_cast<int>(size)),ref(count)}).AsInt()) {
                        message += "; candidates=";
                        for (std::uint32_t i=0;i<size;++i) {
                            const auto config = vm.Model().GetObjectElement(configs,static_cast<runtime::JniSize>(i));
                            if (!config.IsValid()) continue;
                            message += " [";
                            for (const auto attribute : {0x3024,0x3023,0x3022,0x3021,0x3025,0x3026}) {
                                if (on(r.renderer_egl,"eglGetConfigAttrib",
                                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                                    {ref(r.renderer_display),ref(config),integer(attribute),ref(count)}).AsInt())
                                    message += std::to_string(vm.Model().GetPrimitiveElement(count,0)) + "/";
                            }
                            message += "]";
                        }
                    }
                }
            } catch (const std::exception&) { message += "; config diagnostics unavailable"; }
            Fail(message);
        }
        if (r.egl_context_factory.IsValid()) {
            r.renderer_context = on(r.egl_context_factory, "createContext",
                "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;)Ljavax/microedition/khronos/egl/EGLContext;",
                {ref(r.renderer_egl),ref(r.renderer_display),ref(r.renderer_config)}).ref;
        } else {
            const auto it = c.gl_surface_client_versions.find(r.view.Value());
            const auto attributes = ints({0x3098,it == c.gl_surface_client_versions.end() ? 1 : it->second,0x3038});
            const auto roots = vm.ProtectReferences(std::array{attributes});
            r.renderer_context = on(r.renderer_egl,"eglCreateContext",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
                {ref(r.renderer_display),ref(r.renderer_config),ref(c.egl.no_context),ref(attributes)}).ref;
        }
        if (!r.renderer_context.IsValid() || r.renderer_context == c.egl.no_context) Fail("renderer EGL context creation failed");
        }
        const auto holder = on(r.view,"getHolder","()Landroid/view/SurfaceHolder;").ref;
        r.renderer_surface = on(r.renderer_egl,"eglCreateWindowSurface",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
            {ref(r.renderer_display),ref(r.renderer_config),ref(holder),ref(dx::VmObjectRef{})}).ref;
        if (!r.renderer_surface.IsValid() || r.renderer_surface == c.egl.no_surface) {
            const auto error = on(r.renderer_egl, "eglGetError", "()I").AsInt();
            Fail("renderer EGL surface creation failed: EGL error=" + std::to_string(error));
        }
        if (!on(r.renderer_egl,"eglMakeCurrent",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                {ref(r.renderer_display),ref(r.renderer_surface),ref(r.renderer_surface),ref(r.renderer_context)}).AsInt()) Fail("renderer eglMakeCurrent failed");
        r.renderer_gl = on(r.renderer_context,"getGL","()Ljavax/microedition/khronos/opengles/GL;").ref;
        ProjectRenderer(c, r);
    }

    void DexActivityLifecycle::ReleaseRendererEgl(GlSurfaceRuntime& r, const bool preserve_context) {
        auto& c = *bindings_.context;
        if (!r.renderer_egl.IsValid() || !r.renderer_display.IsValid()) return;
        const auto ref = dx::VmValue::Ref;
        if (r.renderer_context.IsValid() && r.renderer_context != c.egl.no_context) {
            if (!CallOnView(r.renderer_egl, "eglMakeCurrent",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                    {ref(r.renderer_display), ref(c.egl.no_surface), ref(c.egl.no_surface), ref(c.egl.no_context)}).AsInt())
                Fail("renderer EGL release current failed");
        }
        if (r.renderer_surface.IsValid() && r.renderer_surface != c.egl.no_surface) {
            if (!CallOnView(r.renderer_egl, "eglDestroySurface",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                    {ref(r.renderer_display), ref(r.renderer_surface)}).AsInt())
                Fail("renderer EGL surface destruction failed");
            r.renderer_surface = dx::VmObjectRef{};
        }
        if (preserve_context) { ProjectRenderer(c, r); return; }
        if (r.renderer_context.IsValid() && r.renderer_context != c.egl.no_context) {
            if (r.egl_context_factory.IsValid()) {
                CallOnView(r.egl_context_factory, "destroyContext",
                    "(Ljavax/microedition/khronos/egl/EGL10;Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)V",
                    {ref(r.renderer_egl), ref(r.renderer_display), ref(r.renderer_context)});
            } else if (!CallOnView(r.renderer_egl, "eglDestroyContext",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                    {ref(r.renderer_display), ref(r.renderer_context)}).AsInt()) {
                Fail("renderer EGL context destruction failed");
            }
        }
        r.renderer_context = dx::VmObjectRef{};
        r.renderer_surface = dx::VmObjectRef{};
        r.renderer_config = dx::VmObjectRef{};
        r.renderer_display = dx::VmObjectRef{};
        r.renderer_egl = dx::VmObjectRef{};
        r.renderer_gl = dx::VmObjectRef{};
        r.ready = false;
        ProjectRenderer(c, r);
    }


    void DexActivityLifecycle::EnsureRendererCallbacks() {
        auto runtime = RendererState();
        auto& r = *runtime;
        if (r.paused || !r.surface_available || !r.renderer.IsValid()) return;
        const bool surface_created = !r.renderer_surface.IsValid();
        InitializeRendererEgl(r);
        RunRendererEvents();
        if (!r.ready) {
            CallOnView(r.renderer, "onSurfaceCreated",
                "(Ljavax/microedition/khronos/opengles/GL10;Ljavax/microedition/khronos/egl/EGLConfig;)V",
                {dx::VmValue::Ref(r.renderer_gl), dx::VmValue::Ref(r.renderer_config)});
            r.ready = true;
        }
        if (surface_created) CallOnView(r.renderer, "onSurfaceChanged",
            "(Ljavax/microedition/khronos/opengles/GL10;II)V",
            {dx::VmValue::Ref(r.renderer_gl), dx::VmValue::Int(static_cast<std::int32_t>(bindings_.context->surface_width)),
             dx::VmValue::Int(static_cast<std::int32_t>(bindings_.context->surface_height))});
    }

    LifecycleFrameState DexActivityLifecycle::Stop(DexProcessStopReason reason) {
        if (state_ == LifecycleRunState::stopped || stop_completed_) return State();
        std::string_view current_phase;
        const auto phase = [this, &current_phase](const std::string_view name,
                                  const bool active = true) {
            current_phase = name;
            if (bindings_.diagnostics)
                bindings_.diagnostics->SetLifecyclePhase(name, active);
        };
        auto& session = bindings_.bridge->Session();
        const auto live = [&] {
            return !runtime::SessionExitRequested(*bindings_.context) &&
                !bindings_.bridge->Vm().ExitCode() && !session.NativeExitCode();
        };
        if (!live()) reason = DexProcessStopReason::guest_exit;
        else if (state_ == LifecycleRunState::failed) reason = DexProcessStopReason::runtime_failure;
        if (reason == DexProcessStopReason::runtime_failure) state_ = LifecycleRunState::failed;
        bindings_.context->process_stopping = true;
        if (bindings_.logger) {
            const auto name = reason == DexProcessStopReason::host_shutdown ? "host_shutdown" :
                reason == DexProcessStopReason::guest_exit ? "guest_exit" : "runtime_failure";
            bindings_.logger->Write(core::LogLevel::info, "session.process_stop",
                "stopping guest process", {}, {{"reason", name}});
        }
        std::exception_ptr first_failure;
        const auto record_failure = [&] {
            state_ = LifecycleRunState::failed;
            if (!first_failure) {
                first_failure = std::current_exception();
            } else if (bindings_.logger) {
                try { throw; }
                catch (const dx::VmJavaThrow& error) {
                    bindings_.logger->Write(core::LogLevel::error, "session.teardown.secondary",
                        error.descriptor + ": " + error.message, {},
                        {{"phase", std::string(current_phase)}});
                } catch (const std::exception& error) {
                    bindings_.logger->Write(core::LogLevel::error, "session.teardown.secondary",
                        error.what(), {}, {{"phase", std::string(current_phase)}});
                } catch (...) {
                    bindings_.logger->Write(core::LogLevel::error, "session.teardown.secondary",
                        "non-standard cleanup exception", {}, {{"phase", std::string(current_phase)}});
                }
            }
        };
        const auto attempt = [&](const auto& action) {
            try { action(); }
            catch (...) {
                record_failure();
            }
        };
        const auto guest = [&](const auto& action) {
            if (!live()) return;
            const auto deadline = session.CleanupDeadlineNs();
            if (first_failure && deadline != 0 &&
                hal::Clock::SteadyTimestampNs() >= deadline) return;
            try {
                const auto bounded = [&] {
                    bindings_.bridge->Vm().RunWithExecutionDeadline(
                        session.CleanupDeadlineNs(), action);
                };
                session.RunGracefulCleanup(bounded);
            } catch (...) {
                if (live()) {
                    record_failure();
                }
            }
        };
        phase("teardown.begin");
        const bool was_running = reason == DexProcessStopReason::host_shutdown &&
            state_ == LifecycleRunState::running && live();
        // Stop producing host frame permits, but retain real graphics and live
        // workers until the application's cooperative callbacks have returned.
        if (egl_pacer_attached_) runtime::ShutdownEglSwapPacer(*bindings_.context);
        phase("teardown.guest_callbacks");
        if (was_running && activity_started_ && !suspended_) {
            guest([&] { SetWindowFocus(false); });
            guest([&] { CallActivity("onPause", "()V", {}); });
        }
        if (was_running) {
            guest([&] { StopAllRenderers(); });
            guest([&] {
                if (const auto error = runtime::RetireSurfaceHolderGeneration(
                        bindings_.bridge->Vm(), *bindings_.context))
                    throw std::runtime_error(*error);
            });
            if (activity_started_) guest([&] { CallActivity("onStop", "()V", {}); });
        }
        if (egl_pacer_attached_) runtime::ShutdownEglSwapPacer(*bindings_.context);
        guest([&] { runtime::ShutdownLocalServices(bindings_.bridge->Vm(), *bindings_.context); });
        attempt([&] { runtime::ShutdownPendingIntents(bindings_.bridge->Vm(), *bindings_.context); });
        runtime::RetireGuestEglSurface(*bindings_.context);
        session.BeginTeardown();
        attempt([&] { StopAllRenderers(); });
        phase("teardown.scheduler_shutdown");
        attempt([&] { runtime::ShutdownAndroidScheduler(*bindings_.context); });
        if (bindings_.interrupt_guest_waits) attempt(bindings_.interrupt_guest_waits);
        phase("teardown.thread_join");
        attempt([&] { session.QuiesceNativeWorkers(); });
        attempt([&] { bindings_.bridge->Threads().Shutdown(); });
        // A process close is not Activity.finish(). Instance destruction remains
        // in ServiceActivitySwitch; never synthesize onDestroy for the stack.
        phase("teardown.media_retire");
        attempt([&] { runtime::ReleaseAndroidMediaResources(*bindings_.context); });
        GlSurfaceRuntime retired_renderer;
        retired_renderer.view = bindings_.context->gl_surface_renderer_view;
        ProjectRenderer(*bindings_.context, retired_renderer);
        bindings_.context->gl_surface_renderer_view = dx::VmObjectRef{};
        bindings_.context->gl_surface_runtimes.clear();
        bindings_.context->active_surface_holders.clear();
        bindings_.context->surface_callbacks.clear();
        bindings_.context->holder_surfaces.clear();
        bindings_.context->surface_holders.clear();
        bindings_.context->surface_callback_sizes.clear();
        bindings_.context->managed_host_surface_open = false;
        bindings_.context->activity_stack.clear();
        window_handoffs_.clear();
        bindings_.context->activity_commands.clear();
        bindings_.context->activity_stack_depth.store(0);
        bindings_.context->activity_switch_pending.store(false);
        phase("teardown.persistence");
        if (bindings_.flush_persistent_state) attempt(bindings_.flush_persistent_state);
        phase("teardown.guest_finalize");
        if (!guest_finalized_ && bindings_.finalize_guest) {
            guest_finalized_ = true;
            attempt(bindings_.finalize_guest);
        }
        phase("teardown.surface_close");
        if (surface_open_ && bindings_.close_surface) {
            attempt(bindings_.close_surface);
            surface_open_ = false;
        }
        phase("teardown.complete", false);
        bindings_.context->defer_content_surface_callbacks = false;
        sized_content_view_ = dx::VmObjectRef{};
        sized_content_node_.reset();
        stop_completed_ = true;
        if (state_ != LifecycleRunState::failed) state_ = LifecycleRunState::stopped;
        if (first_failure) std::rethrow_exception(first_failure);
        return State();
    }

    LifecycleFrameState DexActivityLifecycle::State() const {
        return {state_, frame_, clock_.Ticks()};
    }

    std::uint64_t DexActivityLifecycle::TicksPerSecond() const noexcept {
        return clock_.TicksPerSecond();
    }

    void DexActivityLifecycle::MarkFailed() noexcept {
        state_ = LifecycleRunState::failed;
    }
} // namespace ogplay::session
