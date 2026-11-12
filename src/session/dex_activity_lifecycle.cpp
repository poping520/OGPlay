#include "ogplay/session/dex_activity_lifecycle.h"

#include "../runtime/integration/dexvm_android/rom_input.h"
#include "../runtime/integration/dexvm_android/rom_window.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include "ogplay/runtime/integration/apk_assets.h"

#include <algorithm>
#include <array>
#include <exception>

namespace ogplay::session {
namespace {
namespace dx = runtime::dexvm;
using ActivityRequest = runtime::DexVmAndroidContext::HostActivityRequest;

constexpr std::int64_t kMillisPerFrame = 16;
constexpr std::uint64_t kMaxCatchUpMillis = 100;
constexpr std::uint64_t kCallbackBudgetNs = 2'000'000'000;
constexpr std::uint64_t kStartupBudgetNs = 10'000'000'000;

[[noreturn]] void Fail(const std::string& message) {
    throw DexActivityLifecycleError(message);
}

[[nodiscard]] std::string RenderJavaException(dx::Interpreter& vm, const dx::VmCallOutcome& outcome) {
    const auto& linker = vm.Linker();
    std::string rendered = "  exception: ";
    rendered += outcome.exception_class.IsValid() ? linker.Class(outcome.exception_class).descriptor
                                                  : "<unknown Java exception>";
    if (!outcome.exception_message.empty()) {
        rendered += "\n  message: " + outcome.exception_message;
    }
    if (!outcome.exception_stack.empty()) {
        rendered += "\n  stack trace:";
        for (const auto& entry : outcome.exception_stack) {
            rendered += "\n    at " + entry.class_descriptor + "." + entry.method_name + " (pc " +
                        std::to_string(entry.pc) + ")";
        }
    }
    return rendered + vm.DescribeThrowableCauses(outcome.exception);
}

void RequireOutcome(dx::Interpreter& vm, const dx::VmCallOutcome& outcome, const std::string& what) {
    if (!outcome.exception.IsValid()) return;
    Fail(what + " failed: uncaught Java exception\n" + RenderJavaException(vm, outcome));
}

dx::VmValue CallDirect(dx::Interpreter& vm, const char* descriptor, const char* name, const char* signature,
                       std::span<const dx::VmValue> args = {}) {
    const auto owner = vm.Linker().ResolveDescriptor(descriptor);
    const auto method = vm.Linker().FindDirectMethod(owner, name, signature);
    const auto label = std::string(descriptor) + " " + name;
    if (!method) Fail(label + " is not linked");
    const auto result = vm.Call(*method, args);
    RequireOutcome(vm, result, label);
    return result.value;
}

dx::VmValue CallOn(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* signature,
                   std::vector<dx::VmValue> args = {}) {
    const auto type = vm.Model().ObjectClass(object);
    const auto slot = vm.Linker().FindVtableIndex(type, name, signature);
    if (!slot) Fail(std::string(name) + " is not linked");
    args.insert(args.begin(), dx::VmValue::Ref(object));
    const auto result = vm.Call(vm.Linker().Class(type).vtable[*slot], args);
    RequireOutcome(vm, result, name);
    return result.value;
}

}  // namespace

void AttachDexApplicationThread(dx::Interpreter& vm, runtime::DexVmAndroidContext& context) {
    static_cast<void>(context);
    constexpr auto owner = "Landroid/app/ActivityThread;";
    const auto type = vm.Linker().ResolveDescriptor(owner);
    RequireOutcome(vm, vm.EnsureClassInitialized(type), "ActivityThread initialization");
    const auto thread = vm.NewIntrinsicInstance(owner);
    const auto root = vm.ProtectReferences(std::array{thread});
    static_cast<void>(CallDirect(vm, owner, "<init>", "()V", std::array{dx::VmValue::Ref(thread)}));
    static_cast<void>(
        CallDirect(vm, owner, "attach", "(Z)V", std::array{dx::VmValue::Ref(thread), dx::VmValue::Int(0)}));
    const auto handler = CallOn(vm, thread, "getHandler", "()Landroid/os/Handler;").ref;
    vm.SetIntrinsicStaticRef(owner, "sMainThreadHandler", "Landroid/os/Handler;", handler);
    static_cast<void>(CallDirect(vm, "Landroid/os/AsyncTask;", "init", "()V"));
}

void PublishDexApplication(dx::Interpreter& vm, runtime::DexVmAndroidContext& context,
                           const std::string& application_descriptor) {
    const auto app =
        CallDirect(vm, "Landroid/app/ActivityThread;", "currentApplication", "()Landroid/app/Application;").ref;
    if (!app.IsValid() || vm.Linker().Class(vm.Model().ObjectClass(app)).descriptor != application_descriptor)
        Fail("ActivityThread did not bind the manifest Application");
    const auto base = CallOn(vm, app, "getBaseContext", "()Landroid/content/Context;").ref;
    if (!base.IsValid() || vm.Model().ObjectClass(base) != vm.Linker().ResolveDescriptor("Landroid/app/ContextImpl;"))
        Fail("bound Application does not have a ContextImpl");
    const auto loader = CallOn(vm, base, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
    if (loader != vm.ClassLoaders().ApplicationLoader()) Fail("LoadedApk returned a different application ClassLoader");
    context.application = app;
    context.application_base_context = base;
    context.application_descriptor = application_descriptor;
    const auto resources = CallOn(vm, base, "getResources", "()Landroid/content/res/Resources;").ref;
    const auto roots = vm.ProtectReferences(std::array{resources});
    const auto assets = CallOn(vm, resources, "getAssets", "()Landroid/content/res/AssetManager;").ref;
    context.singletons["resources"] = resources;
    context.application_resource_manager = runtime::ResolveResourceAssetManager(vm, context, assets);
}

DexActivityLifecycle::DexActivityLifecycle(DexActivityLifecycleBindings bindings, std::uint64_t ticks_per_frame,
                                           std::uint64_t ticks_per_second)
    : bindings_(std::move(bindings)), clock_(ticks_per_frame, ticks_per_second) {}

DexActivityLifecycle::~DexActivityLifecycle() = default;

void DexActivityLifecycle::CheckGuest() {
    bindings_.bridge->Session().RethrowAsyncFailure();
    if (const auto failure = bindings_.bridge->Threads().TakeFailure()) Fail(*failure);
    if (bindings_.check_main_thread) bindings_.check_main_thread();
}

void DexActivityLifecycle::AwaitIdle(std::uint64_t budget_ns) {
    const auto deadline = hal::Clock::SteadyTimestampNs() + budget_ns;
    while (!runtime::AndroidMainQueueIdle(*bindings_.context)) {
        CheckGuest();
        if (hal::Clock::SteadyTimestampNs() >= deadline) Fail("ROM main MessageQueue did not become idle");
        hal::SleepPreciseNs(150'000);
    }
    CheckGuest();
}

LifecycleFrameState DexActivityLifecycle::Start() {
    if (state_ != LifecycleRunState::ready) Fail("Activity lifecycle already started");
    try {
        if (bindings_.open_surface) bindings_.open_surface();
        surface_open_ = true;
        runtime::AttachEglSwapPacer(*bindings_.context, bindings_.bridge->Vm().ExecutionLock());
        runtime::QueueAndroidActivityRequest(
            *bindings_.context,
            {ActivityRequest::Kind::launch, bindings_.launcher_descriptor, bindings_.launcher_component_name});
        AwaitIdle(kStartupBudgetNs);
        state_ = LifecycleRunState::running;
        if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("running", false);
    } catch (...) {
        if (bindings_.bridge->Vm().ExitCode()) return Stop(DexProcessStopReason::guest_exit);
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState DexActivityLifecycle::Suspend() {
    if (state_ != LifecycleRunState::running || suspended_) Fail("Activity lifecycle cannot suspend");
    CancelInput();
    runtime::QueueAndroidActivityRequest(*bindings_.context, {ActivityRequest::Kind::suspend, {}, {}});
    AwaitIdle(kCallbackBudgetNs);
    // The visibility change can leave a traversal behind a sync barrier.
    runtime::QueueAndroidDisplayFrame(bindings_.bridge->Vm().ExecutionLock(), *bindings_.context);
    runtime::AdvanceEglSwapPacer(*bindings_.context);
    AwaitIdle(kCallbackBudgetNs);
    suspended_ = true;
    if (bindings_.flush_persistent_state) bindings_.flush_persistent_state();
    return State();
}

LifecycleFrameState DexActivityLifecycle::Resume() {
    if (state_ != LifecycleRunState::running || !suspended_) Fail("Activity lifecycle cannot resume");
    runtime::QueueAndroidActivityRequest(*bindings_.context, {ActivityRequest::Kind::resume, {}, {}});
    AwaitIdle(kCallbackBudgetNs);
    runtime::QueueAndroidDisplayFrame(bindings_.bridge->Vm().ExecutionLock(), *bindings_.context);
    runtime::AdvanceEglSwapPacer(*bindings_.context);
    AwaitIdle(kCallbackBudgetNs);
    suspended_ = false;
    previous_step_ns_ = 0;
    producer_wait_started_ns_ = 0;
    return State();
}

void DexActivityLifecycle::SetRealtimeVideoClock(bool enabled) noexcept {
    realtime_ = enabled;
}

void DexActivityLifecycle::QueueInput(const runtime::AndroidBoundaryInput& input) {
    if (state_ != LifecycleRunState::running || suspended_) return;
    auto event = input;
    event.device_id = event.type == runtime::AndroidBoundaryInputType::key ? runtime::kAndroidKeyboardDeviceId
                                                                           : runtime::kAndroidTouchDeviceId;
    const auto now = bindings_.context->uptime_millis.load() * 1'000'000;
    if (auto stamped = input_timeline_.Stamp(std::move(event), now)) pending_input_.push_back(std::move(*stamped));
}

void DexActivityLifecycle::CancelInput() {
    if (state_ != LifecycleRunState::running) return;
    pending_input_.clear();
    auto& context = *bindings_.context;
    static_cast<void>(input_timeline_.Cancel(context.uptime_millis.load() * 1'000'000));
    auto& vm = bindings_.bridge->Vm();
    const dx::VmExecutionLockScope execution(vm.ExecutionLock());
    if (context.rom_windows)
        runtime::android_intrinsics::QueueRomWindowInput(vm, bindings_.context,
                                                         context.rom_windows->system.CancelTouch());
}

void DexActivityLifecycle::SetRealtimeTimeSourceForTesting(std::function<std::uint64_t()> now_ns) {
    realtime_now_ns_ = std::move(now_ns);
}

void DexActivityLifecycle::SetEglProducerWaitBudgetForTesting(std::uint64_t budget_ns) noexcept {
    producer_budget_ns_ = budget_ns;
}

bool DexActivityLifecycle::ProducersReady(bool wait) {
    if (!producer_wait_started_ns_) producer_wait_started_ns_ = hal::Clock::SteadyTimestampNs();
    if (!runtime::EglProducersSettled(*bindings_.context, wait)) {
        const auto waited = hal::Clock::SteadyTimestampNs() - producer_wait_started_ns_;
        if (waited < producer_budget_ns_) return false;
        runtime::ReleaseOverdueEglProducers(*bindings_.context, waited, bindings_.logger);
    }
    producer_wait_started_ns_ = 0;
    return true;
}

void DexActivityLifecycle::AwaitProducers() {
    while (!ProducersReady(true))
        CheckGuest();
    CheckGuest();
}

bool DexActivityLifecycle::ReadyForFrame() {
    if (state_ != LifecycleRunState::running || suspended_) return true;
    try {
        CheckGuest();
        return ProducersReady(false);
    } catch (...) {
        if (bindings_.bridge->Vm().ExitCode()) return true;
        state_ = LifecycleRunState::failed;
        throw;
    }
}

LifecycleFrameState DexActivityLifecycle::StepFrame() {
    if (state_ != LifecycleRunState::running) Fail("Activity lifecycle is not running");
    if (suspended_) return State();
    auto& vm = bindings_.bridge->Vm();
    auto& context = *bindings_.context;
    try {
        CheckGuest();
        AwaitProducers();
        const auto now = realtime_now_ns_ ? realtime_now_ns_() : hal::Clock::SteadyTimestampNs();
        // Realtime uptime follows host time, independent of the step rate; the frame-rate cap only
        // paces steps. Sub-millisecond remainders carry over, and a stalled step catches up at most
        // kMaxCatchUpMillis. Manual stepping keeps one fixed frame period per step.
        auto delta = kMillisPerFrame;
        if (realtime_ && previous_step_ns_ != 0U) {
            const auto elapsed_ms = now > previous_step_ns_ ? (now - previous_step_ns_) / 1'000'000U : 0U;
            previous_step_ns_ += elapsed_ms * 1'000'000U;
            delta = static_cast<std::int64_t>(std::min<std::uint64_t>(elapsed_ms, kMaxCatchUpMillis));
        } else if (realtime_) {
            previous_step_ns_ = now;
        }
        runtime::AdvanceAndroidClock(context, delta);
        std::shared_ptr<runtime::RomWindowState> windows;
        bool media_tick{};
        {
            const dx::VmExecutionLockScope execution(vm.ExecutionLock());
            windows = context.rom_windows;
            for (const auto& input : pending_input_)
                runtime::android_intrinsics::DispatchRomWindowInput(vm, bindings_.context, input);
            pending_input_.clear();
            media_tick = !context.audio_tracks.empty() || !context.media_players.empty() ||
                         !context.sound_pools.empty() || !context.surface_textures.empty();
        }
        if (media_tick) runtime::QueueAndroidMediaTick(context);
        if (display_pacer_.PeriodNs() != context.frame_period_ns)
            display_pacer_ = hal::FramePacer{context.frame_period_ns};
        const bool display_frame = !realtime_ || display_pacer_.TryClaim(now);
        if (display_frame && !realtime_) runtime::AdvanceEglSwapPacer(context);
        if (!realtime_) AwaitIdle(kCallbackBudgetNs);
        if (display_frame) {
            runtime::QueueAndroidDisplayFrame(vm.ExecutionLock(), context);
        }
        if (!realtime_) AwaitIdle(kCallbackBudgetNs);
        if (!realtime_) AwaitProducers();
        clock_.AdvanceFrames(1);
        ++frame_;
        if (windows && context.session) {
            if (const auto frame = windows->system.ComposeIfChanged(window_revision_)) {
                if (frame->pixels) {
                    context.session->PublishSharedFrame(frame->pixels);
                    if (!reported_borrow_ && frame->borrowed_egl && bindings_.logger) {
                        reported_borrow_ = true;
                        bindings_.logger->Write(core::LogLevel::info, "runtime.window.compose",
                                                "borrowed full-screen EGL window pixels");
                    }
                    posted_frame_ = true;
                    blank_frame_ = false;
                } else if (posted_frame_ && !blank_frame_) {
                    context.session->PublishBlankFrame(255U);
                    blank_frame_ = true;
                }
            }
        }
        // Lease the completed frame before allowing the producer to replace it.
        // SDL present can then overlap production of the next display frame.
        if (display_frame && realtime_) runtime::AdvanceEglSwapPacer(context);
    } catch (...) {
        if (vm.ExitCode()) return Stop(DexProcessStopReason::guest_exit);
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState DexActivityLifecycle::Stop(DexProcessStopReason reason) {
    if (stop_completed_) return State();
    auto& context = *bindings_.context;
    auto& vm = bindings_.bridge->Vm();
    auto& session = bindings_.bridge->Session();
    std::exception_ptr failure;
    const auto attempt = [&](const auto& action) {
        try {
            action();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    };
    const bool live = !runtime::SessionExitRequested(context) && !vm.ExitCode() && !session.NativeExitCode();
    if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("teardown.guest_callbacks", true);
    if (live && reason == DexProcessStopReason::host_shutdown && state_ == LifecycleRunState::running) {
        attempt([&] {
            CancelInput();
            runtime::ShutdownEglSwapPacer(context);
            const auto deadline = hal::Clock::SteadyTimestampNs() + kCallbackBudgetNs;
            vm.SetDefaultExecutionDeadline(deadline);
            runtime::QueueAndroidActivityRequest(context, {ActivityRequest::Kind::shutdown, {}, {}});
            auto last_ms = hal::Clock::SteadyTimestampNs() / 1'000'000;
            while (!context.activities_destroyed.load() || !runtime::AndroidMainQueueIdle(context)) {
                if (vm.ExitCode() || session.NativeExitCode()) break;
                const auto now = hal::Clock::SteadyTimestampNs();
                if (now >= deadline) {
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("session.lifecycle.cleanup_timeout", 0);
                    Fail("ROM Activity shutdown exceeded its Clock deadline");
                }
                try {
                    CheckGuest();
                } catch (...) {
                    if (vm.ExitCode() || session.NativeExitCode()) break;
                    throw;
                }
                const auto ms = now / 1'000'000;
                runtime::AdvanceAndroidClock(context, static_cast<std::int64_t>(ms - last_ms));
                last_ms = ms;
                runtime::QueueAndroidDisplayFrame(vm.ExecutionLock(), context);
                hal::SleepPreciseNs(1'000'000);
            }
        });
    }
    context.process_stopping = true;
    if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("teardown.thread_join", true);
    if (bindings_.finalize_guest) attempt(bindings_.finalize_guest);
    if (bindings_.flush_persistent_state) attempt(bindings_.flush_persistent_state);
    if (surface_open_ && bindings_.close_surface) attempt(bindings_.close_surface);
    if (surface_open_) runtime::DetachEglSwapPacer(context, vm.ExecutionLock());
    surface_open_ = false;
    stop_completed_ = true;
    if (bindings_.diagnostics) bindings_.diagnostics->SetLifecyclePhase("teardown.complete", false);
    if (failure || state_ == LifecycleRunState::failed || reason == DexProcessStopReason::runtime_failure)
        state_ = LifecycleRunState::failed;
    else
        state_ = LifecycleRunState::stopped;
    if (failure) std::rethrow_exception(failure);
    return State();
}

LifecycleFrameState DexActivityLifecycle::State() const {
    return {state_, frame_, clock_.Ticks()};
}

std::uint64_t DexActivityLifecycle::TicksPerSecond() const noexcept {
    return clock_.TicksPerSecond();
}
}  // namespace ogplay::session
