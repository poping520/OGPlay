#pragma once

#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "ogplay/hal/clock.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

enum class LifecycleCallbackRoute : std::uint8_t {
    native_activity,
    framework_activity,
    gl_surface_view_renderer,
    custom_jni,
};

struct LifecycleTemplateDescription final {
    ProfileLifecycle lifecycle{ProfileLifecycle::native_activity};
    LifecycleCallbackRoute startup{LifecycleCallbackRoute::native_activity};
    LifecycleCallbackRoute frame_lifecycle{
        LifecycleCallbackRoute::native_activity};
    LifecycleCallbackRoute render{LifecycleCallbackRoute::native_activity};
    LifecycleCallbackRoute shutdown{LifecycleCallbackRoute::native_activity};
};

enum class LifecycleRunState : std::uint8_t {
    ready,
    running,
    failed,
    stopped,
};

struct LifecycleFrameState final {
    LifecycleRunState state{LifecycleRunState::ready};
    std::uint64_t frame{};
    std::uint64_t clock_ticks{};
};

class LifecycleSequenceError final : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

class LifecycleFrameHost {
public:
    virtual ~LifecycleFrameHost() = default;

    virtual void Start(LifecycleCallbackRoute route) = 0;
    virtual void InjectInput() = 0;
    virtual void LifecycleCallback(LifecycleCallbackRoute route) = 0;
    virtual void RenderCallback(LifecycleCallbackRoute route) = 0;
    virtual void Present() = 0;
    virtual void PumpAudio() = 0;
    virtual void Schedule() = 0;
    virtual void UpdateTime(hal::Clock& clock) = 0;
    virtual void Stop(LifecycleCallbackRoute route) = 0;
};

class LifecycleFrameRunner final {
public:
    LifecycleFrameRunner(ProfileLifecycle lifecycle, hal::Clock& clock,
                         LifecycleFrameHost& host);

    [[nodiscard]] LifecycleFrameState Start();
    [[nodiscard]] LifecycleFrameState StepFrame();
    [[nodiscard]] LifecycleFrameState Stop();
    [[nodiscard]] LifecycleFrameState State() const;
    [[nodiscard]] const LifecycleTemplateDescription& Description() const noexcept;

private:
    void RequireState(LifecycleRunState expected,
                      std::string_view operation) const;

    LifecycleTemplateDescription description_;
    hal::Clock& clock_;
    LifecycleFrameHost& host_;
    LifecycleRunState state_{LifecycleRunState::ready};
    std::uint64_t frame_{};
};

[[nodiscard]] LifecycleTemplateDescription DescribeLifecycle(
    ProfileLifecycle lifecycle);
[[nodiscard]] std::string_view ToString(LifecycleCallbackRoute route) noexcept;
[[nodiscard]] std::string_view ToString(LifecycleRunState state) noexcept;

}  // namespace ogplay::session
