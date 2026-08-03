#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::hal {

enum class VideoBackend : std::uint8_t { automatic, dummy, offscreen };

enum class InputEventType : std::uint8_t {
    quit,
    key,
    pointer_motion,
    pointer_button,
    gamepad_axis,
    gamepad_button,
    gamepad_added,
    gamepad_removed,
};

struct WindowOptions {
    std::string title{"OGPlay"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool hidden{};
    bool resizable{true};
};

struct WindowState {
    bool open{};
    std::uint32_t id{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct InputEvent {
    InputEventType type{InputEventType::quit};
    std::uint64_t timestamp_ns{};
    std::uint32_t window_id{};
    std::uint32_t device_id{};
    std::int32_t code{};
    std::int32_t value{};
    float x{};
    float y{};
    float delta_x{};
    float delta_y{};
    bool pressed{};
    bool repeat{};
};

class WindowInput {
public:
    virtual ~WindowInput() = default;
    virtual void Open(const WindowOptions& options) = 0;
    virtual void Close() noexcept = 0;
    [[nodiscard]] virtual WindowState State() const = 0;
    [[nodiscard]] virtual std::string_view BackendName() const noexcept = 0;
    [[nodiscard]] virtual std::vector<InputEvent> PollEvents() = 0;
};

[[nodiscard]] std::unique_ptr<WindowInput> CreateSdlWindowInput(
    VideoBackend backend = VideoBackend::automatic);

}  // namespace ogplay::hal
