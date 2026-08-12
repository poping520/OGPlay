#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/hal/clock.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/session/lifecycle.h"

namespace ogplay::session {

// dex_activity lifecycle template (docs/design/dexvm/04-integration.md §2):
// the real interpreted onCreate/onStart/onResume drive the title; the host
// render loop calls the captured Renderer's onDrawFrame; input dispatches
// through interpreted onTouchEvent/onKeyDown overrides. No profile
// native_call or [[java.class]] declarations participate.

struct DexActivityLifecycleBindings final {
    runtime::DexVmGuestBridge* bridge{};
    std::shared_ptr<runtime::DexVmAndroidContext> context;
    std::string launcher_descriptor;  // "Lcom/example/Game;"
    std::function<void()> open_surface;
    std::function<void()> present_surface;
    // Receives letterboxed surface-sized RGBA frames from the video pump;
    // unset drops the frames (video still advances and completes).
    std::function<void(std::vector<std::uint8_t> rgba8)> publish_video_frame;
    std::function<void()> interrupt_guest_waits;
    std::function<void()> finalize_guest;
    std::function<void()> close_surface;
};

class DexActivityLifecycleError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DexActivityLifecycle final {
public:
    DexActivityLifecycle(DexActivityLifecycleBindings bindings,
                         std::uint64_t ticks_per_frame = 1'000,
                         std::uint64_t ticks_per_second = 60'000);
    ~DexActivityLifecycle();
    DexActivityLifecycle(const DexActivityLifecycle&) = delete;
    DexActivityLifecycle& operator=(const DexActivityLifecycle&) = delete;

    [[nodiscard]] LifecycleFrameState Start();
    [[nodiscard]] LifecycleFrameState Suspend();
    [[nodiscard]] LifecycleFrameState Resume();
    [[nodiscard]] LifecycleFrameState StepFrame();
    void QueueInput(const runtime::AndroidBoundaryInput& input);
    [[nodiscard]] LifecycleFrameState Stop();
    [[nodiscard]] LifecycleFrameState State() const;

private:
    void CallActivity(const std::string& name, const std::string& descriptor,
                      std::vector<runtime::dexvm::VmValue> arguments);
    void CallOnView(runtime::dexvm::VmObjectRef receiver,
                    const std::string& name, const std::string& descriptor,
                    std::vector<runtime::dexvm::VmValue> arguments);
    void DispatchInput();
    void MarkFailed() noexcept;
    // Cooperative Java threads + in-process startActivity, both serviced
    // at frame boundaries (installer-style titles run a worker thread and
    // then switch to the game activity).
    void PumpJavaThreads();
    // Decoded VideoView playback advances with the same frame clock; frames
    // publish through the binding and onCompletion fires on this thread.
    void PumpVideo();
    void ServiceActivitySwitch();
    void EnsureRendererCallbacks();

    DexActivityLifecycleBindings bindings_;
    std::vector<runtime::AndroidBoundaryInput> pending_input_;
    hal::FixedStepClock clock_;
    LifecycleRunState state_{LifecycleRunState::ready};
    std::uint64_t frame_{};
    bool surface_open_{};
    bool suspended_{};
    bool pointer_down_{};
    float pointer_x_{};
    float pointer_y_{};
    bool guest_finalized_{};
    // Renderer callbacks fire once when the interpreted glue registers a
    // renderer; installer phases run frames without one.
    bool renderer_ready_{};
};

}  // namespace ogplay::session
