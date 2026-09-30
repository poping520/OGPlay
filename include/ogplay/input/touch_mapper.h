#pragma once

#include "ogplay/input/mouse_touch_mapper.h"
#include <vector>

namespace ogplay::input {

enum class TouchPhase { down, move, up, cancel };
struct TouchPoint final {
    std::int32_t id{};
    float x{}, y{}, pressure{};
};
struct TouchSnapshot final {
    TouchPhase phase{};
    std::size_t changed_index{};
    std::vector<TouchPoint> points;
    std::uint32_t modifiers{};
};

// One logical touchscreen: desktop mouse and physical touch never overlap.
// Finger identities remain stable while array indices may change after UP.
class TouchMapper final {
public:
    [[nodiscard]] std::optional<TouchSnapshot> Map(
        const hal::InputEvent&, const hal::WindowState&, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool Active() const noexcept;
    void Reset() noexcept;
private:
    MouseTouchMapper mouse_;
    std::uint64_t device_{};
    std::uint32_t window_{};
    struct Contact { std::uint64_t host_id{}; TouchPoint point; };
    std::vector<Contact> contacts_;
    TouchPoint mouse_point_;
    std::uint32_t modifiers_{};
};

} // namespace ogplay::input
