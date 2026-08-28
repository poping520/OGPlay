#pragma once

#include <cstdint>

namespace ogplay::runtime {

// Observable progress made while consuming a guest supervisor boundary.
// Unknown handled work is deliberately idle: watchdog renewal must be earned.
enum class SupervisorCallProgress : std::uint8_t {
    not_handled,
    handled_idle,
    handled_advanced,
};

[[nodiscard]] constexpr SupervisorCallProgress ClassifyJniReentry(
    const bool handled) noexcept {
    return handled ? SupervisorCallProgress::handled_advanced
                   : SupervisorCallProgress::not_handled;
}

}  // namespace ogplay::runtime
