#pragma once

#include <optional>
#include <map>
#include "ogplay/input/touch_mapper.h"

#include "ogplay/hal/window_input.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"

namespace ogplay::session {

[[nodiscard]] std::optional<runtime::AndroidBoundaryInput> MapAndroidInput(
    const hal::InputEvent& event);

[[nodiscard]] runtime::AndroidBoundaryInput MapAndroidTouch(const input::TouchSnapshot& touch);

// Session Clock owns timestamps; host timestamps never become guest uptime.
class AndroidInputTimeline final {
public:
    [[nodiscard]] std::optional<runtime::AndroidBoundaryInput> Stamp(
        runtime::AndroidBoundaryInput input, std::int64_t now_ns);
    [[nodiscard]] std::vector<runtime::AndroidBoundaryInput> Cancel(std::int64_t now_ns);
private:
    std::int64_t last_time_ns_{};
    std::map<std::pair<std::int32_t, std::int32_t>, runtime::AndroidBoundaryInput> keys_;
    std::map<std::int32_t, runtime::AndroidBoundaryInput> motions_;
};

}  // namespace ogplay::session
