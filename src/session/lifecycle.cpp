#include "ogplay/session/lifecycle.h"

#include <limits>
#include <string>

namespace ogplay::session {

LifecycleTemplateDescription DescribeLifecycle(
    const ProfileLifecycle lifecycle) {
    switch (lifecycle) {
    case ProfileLifecycle::native_activity:
        return {lifecycle, LifecycleCallbackRoute::native_activity,
                LifecycleCallbackRoute::native_activity,
                LifecycleCallbackRoute::native_activity,
                LifecycleCallbackRoute::native_activity};
    case ProfileLifecycle::gl_surface_view:
        return {lifecycle, LifecycleCallbackRoute::framework_activity,
                LifecycleCallbackRoute::framework_activity,
                LifecycleCallbackRoute::gl_surface_view_renderer,
                LifecycleCallbackRoute::framework_activity};
    case ProfileLifecycle::custom_jni:
        return {lifecycle, LifecycleCallbackRoute::custom_jni,
                LifecycleCallbackRoute::custom_jni,
                LifecycleCallbackRoute::custom_jni,
                LifecycleCallbackRoute::custom_jni};
    }
    throw LifecycleSequenceError("unsupported profile lifecycle");
}

LifecycleFrameRunner::LifecycleFrameRunner(const ProfileLifecycle lifecycle,
                                           hal::Clock& clock,
                                           LifecycleFrameHost& host)
    : description_(DescribeLifecycle(lifecycle)), clock_(clock), host_(host) {}

LifecycleFrameState LifecycleFrameRunner::Start() {
    RequireState(LifecycleRunState::ready, "start");
    try {
        host_.Start(description_.startup);
        state_ = LifecycleRunState::running;
    } catch (...) {
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState LifecycleFrameRunner::StepFrame() {
    RequireState(LifecycleRunState::running, "step frame");
    if (frame_ == std::numeric_limits<std::uint64_t>::max()) {
        throw LifecycleSequenceError("lifecycle frame counter overflow");
    }
    try {
        host_.InjectInput();
        host_.LifecycleCallback(description_.frame_lifecycle);
        host_.RenderCallback(description_.render);
        host_.Present();
        host_.PumpAudio();
        host_.Schedule();
        host_.UpdateTime(clock_);
        ++frame_;
    } catch (...) {
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState LifecycleFrameRunner::Stop() {
    if (state_ != LifecycleRunState::running &&
        state_ != LifecycleRunState::failed) {
        throw LifecycleSequenceError("cannot stop lifecycle while " +
                                     std::string(ToString(state_)));
    }
    try {
        host_.Stop(description_.shutdown);
        state_ = LifecycleRunState::stopped;
    } catch (...) {
        state_ = LifecycleRunState::failed;
        throw;
    }
    return State();
}

LifecycleFrameState LifecycleFrameRunner::State() const {
    return {state_, frame_, clock_.Ticks()};
}

const LifecycleTemplateDescription& LifecycleFrameRunner::Description() const noexcept {
    return description_;
}

void LifecycleFrameRunner::RequireState(const LifecycleRunState expected,
                                        const std::string_view operation) const {
    if (state_ != expected) {
        throw LifecycleSequenceError("cannot " + std::string(operation) +
                                     " lifecycle while " +
                                     std::string(ToString(state_)));
    }
}

std::string_view ToString(const LifecycleCallbackRoute route) noexcept {
    switch (route) {
    case LifecycleCallbackRoute::native_activity: return "native_activity";
    case LifecycleCallbackRoute::framework_activity: return "framework_activity";
    case LifecycleCallbackRoute::gl_surface_view_renderer:
        return "gl_surface_view_renderer";
    case LifecycleCallbackRoute::custom_jni: return "custom_jni";
    }
    return "unknown";
}

std::string_view ToString(const LifecycleRunState state) noexcept {
    switch (state) {
    case LifecycleRunState::ready: return "ready";
    case LifecycleRunState::running: return "running";
    case LifecycleRunState::failed: return "failed";
    case LifecycleRunState::stopped: return "stopped";
    }
    return "unknown";
}

}  // namespace ogplay::session
