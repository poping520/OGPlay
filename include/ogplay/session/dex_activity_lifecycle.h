#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/hal/clock.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/session/lifecycle.h"

namespace ogplay::session {

// Minimal API-19 Application startup. Repeated calls for the same descriptor
// return the process root; a different descriptor or a failed startup is an
// explicit error and never permits Activity startup to continue.
[[nodiscard]] runtime::dexvm::VmObjectRef StartDexApplication(
    runtime::DexVmGuestBridge& bridge,
    const std::shared_ptr<runtime::DexVmAndroidContext>& context,
    const std::string& application_descriptor);

struct ContentViewGestureDispatchResult final {
    bool handled{};
    bool keep_capture{};
    std::optional<std::string> error;
};
// Dispatches to the content View's virtual onTouchEvent. Only a handled DOWN
// establishes capture; a captured target keeps receiving MOVE/UP even if an
// individual later event returns false. Non-captured MOVE/UP are not invoked.
[[nodiscard]] ContentViewGestureDispatchResult DispatchContentViewGestureEvent(
    runtime::dexvm::Interpreter& vm,
    runtime::dexvm::VmObjectRef content_view, std::int32_t action, float x,
    float y, bool captured);

// dex_activity lifecycle template (docs/design/dexvm/04-integration.md §2):
// the real interpreted onCreate/onStart/onResume drive the title; the host
// render loop calls the captured Renderer's onDrawFrame; input dispatches
// through interpreted onTouchEvent/onKeyDown overrides. Profile entry scope
// may select the Activity and initialize real static fields before Start().

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
    // Runs after guest onPause and again after Java threads stop during clean
    // teardown. The frontend binds this to VFS FlushAll (ADR-0020).
    std::function<void()> flush_persistent_state;
    // Guest-owned GLSurfaceView needs the host-created ANGLE context released
    // before holder callbacks start its GLThread. Intrinsic-renderer sessions
    // retain the original currency so their exact render path is unchanged.
    std::function<void()> release_surface_currency;
    // Manifest-selected process Application. Kept at the aggregate tail so
    // existing lifecycle hosts retain source compatibility.
    std::string application_descriptor{"Landroid/app/Application;"};
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
    [[nodiscard]] runtime::AndroidBoundaryFrame ComposePresentedFrame(
        runtime::AndroidBoundaryFrame frame);
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
    // Managed surface lifecycle for guest-implemented SurfaceViews, which
    // wait on these before they will touch EGL.
    void DispatchSurfaceHolder(runtime::SurfaceHolderPhase phase);
    // Replaces a teardown-shaped error with the uncaught Java thread death
    // that actually caused it, when one was recorded.
    void RethrowFatalThreadFailure();
    // Cooperative Timer tasks + in-process startActivity, both serviced at
    // frame boundaries, plus uncaught failures from real Java threads.
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
    // View that owns the current gesture. Click eligibility is captured
    // separately from cumulative OnTouchListener consumption.
    std::uint64_t gesture_candidate_{};
    bool gesture_click_eligible_{};
    bool gesture_touch_consumed_{};
    bool content_view_captured_{};
    bool guest_finalized_{};
    // Renderer callbacks fire once when the interpreted glue registers a
    // renderer; installer phases run frames without one.
    bool renderer_ready_{};
    // Android delivers onStart/onResume only when the activity does not
    // finish inside onCreate; a never-started activity retires with just
    // onDestroy (no onPause/onStop).
    bool activity_started_{};
    bool egl_pacer_attached_{};
};

}  // namespace ogplay::session
