#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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
    std::uint64_t present_count{};
};

struct DisplayRect final {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};

    bool operator==(const DisplayRect&) const = default;
};

[[nodiscard]] DisplayRect FitDisplayRect(
    std::uint32_t source_width, std::uint32_t source_height,
    std::uint32_t target_width, std::uint32_t target_height);

struct MappedDisplayPoint final {
    float x{};
    float y{};
    bool inside{};
};

[[nodiscard]] MappedDisplayPoint MapDisplayPoint(
    float target_x, float target_y,
    std::uint32_t source_width, std::uint32_t source_height,
    std::uint32_t target_width, std::uint32_t target_height);

class FrameRateSampler final {
public:
    FrameRateSampler(std::uint64_t ticks_per_second,
                     std::uint64_t update_interval_ticks);
    [[nodiscard]] std::optional<double> Observe(
        std::uint64_t presented_frames, std::uint64_t ticks);

private:
    std::uint64_t ticks_per_second_{};
    std::uint64_t update_interval_ticks_{};
    std::uint64_t previous_frames_{};
    std::uint64_t previous_ticks_{};
    bool initialized_{};
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
    virtual void SetTitle(std::string_view title) = 0;
    [[nodiscard]] virtual WindowState State() const = 0;
    [[nodiscard]] virtual std::string_view BackendName() const noexcept = 0;
    [[nodiscard]] virtual std::vector<InputEvent> PollEvents() = 0;
    virtual void PresentRgba8(std::span<const std::uint8_t> pixels,
                              std::uint32_t width, std::uint32_t height) = 0;
};

[[nodiscard]] std::unique_ptr<WindowInput> CreateSdlWindowInput(
    VideoBackend backend = VideoBackend::automatic);

}  // namespace ogplay::hal
