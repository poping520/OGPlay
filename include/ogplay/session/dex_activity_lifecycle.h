#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/hal/clock.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/session/android_input_mapping.h"
#include "ogplay/session/lifecycle.h"

namespace ogplay::session {

enum class DexProcessStopReason : std::uint8_t {
    host_shutdown,
    guest_exit,
    runtime_failure,
};

void AttachDexApplicationThread(runtime::dexvm::Interpreter& vm, runtime::DexVmAndroidContext& context);
void PublishDexApplication(runtime::dexvm::Interpreter& vm, runtime::DexVmAndroidContext& context,
                           const std::string& application_descriptor);

[[nodiscard]] std::int64_t VideoClockAdvanceMillis(std::uint64_t elapsed_ms, std::int64_t advanced_ms) noexcept;

struct DexActivityLifecycleBindings final {
    runtime::DexVmGuestBridge* bridge{};
    std::shared_ptr<runtime::DexVmAndroidContext> context;
    std::string launcher_descriptor;
    std::string launcher_component_name;
    std::function<void()> open_surface;
    std::function<void()> close_surface;
    std::function<void()> finalize_guest;
    std::function<void()> check_main_thread;
    std::function<void()> flush_persistent_state;
    std::shared_ptr<runtime::debug::DiagnosticState> diagnostics;
    core::Logger* logger{};
};

class DexActivityLifecycleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DexActivityLifecycle final {
public:
    DexActivityLifecycle(DexActivityLifecycleBindings bindings, std::uint64_t ticks_per_frame = 1'000,
                         std::uint64_t ticks_per_second = 60'000);
    ~DexActivityLifecycle();
    DexActivityLifecycle(const DexActivityLifecycle&) = delete;
    DexActivityLifecycle& operator=(const DexActivityLifecycle&) = delete;

    [[nodiscard]] LifecycleFrameState Start();
    [[nodiscard]] LifecycleFrameState Suspend();
    [[nodiscard]] LifecycleFrameState Resume();
    [[nodiscard]] LifecycleFrameState StepFrame();
    [[nodiscard]] bool ReadyForFrame();
    void SetRealtimeVideoClock(bool enabled) noexcept;
    void SetEglProducerWaitBudgetForTesting(std::uint64_t budget_ns) noexcept;
    void QueueInput(const runtime::AndroidBoundaryInput& input);
    void CancelInput();
    [[nodiscard]] LifecycleFrameState Stop(DexProcessStopReason reason = DexProcessStopReason::host_shutdown);
    [[nodiscard]] LifecycleFrameState State() const;
    [[nodiscard]] std::uint64_t TicksPerSecond() const noexcept;

private:
    void AwaitIdle(std::uint64_t budget_ns);
    void CheckGuest();
    void AwaitProducers();
    bool ProducersReady(bool wait);

    DexActivityLifecycleBindings bindings_;
    std::vector<runtime::AndroidBoundaryInput> pending_input_;
    AndroidInputTimeline input_timeline_;
    hal::FixedStepClock clock_;
    hal::FramePacer display_pacer_;
    LifecycleRunState state_{LifecycleRunState::ready};
    std::uint64_t frame_{}, window_revision_{}, previous_step_ns_{}, producer_wait_started_ns_{};
    std::uint64_t producer_budget_ns_{30'000'000'000};
    bool surface_open_{}, suspended_{}, realtime_{true}, stop_completed_{};
    bool posted_frame_{}, blank_frame_{}, reported_borrow_{};
};

}  // namespace ogplay::session
