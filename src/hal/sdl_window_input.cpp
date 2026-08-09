#include "ogplay/hal/window_input.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#if OGPLAY_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace ogplay::hal {

DisplayRect FitDisplayRect(const std::uint32_t source_width,
                           const std::uint32_t source_height,
                           const std::uint32_t target_width,
                           const std::uint32_t target_height) {
    if (source_width == 0 || source_height == 0 ||
        target_width == 0 || target_height == 0) {
        throw std::invalid_argument("display layout dimensions must be non-zero");
    }
    const auto width_limited = static_cast<std::uint64_t>(target_width) * source_height <=
                               static_cast<std::uint64_t>(target_height) * source_width;
    std::uint32_t width{};
    std::uint32_t height{};
    if (width_limited) {
        width = target_width;
        height = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(target_width) * source_height / source_width);
        if (height == 0) height = 1;
    } else {
        height = target_height;
        width = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(target_height) * source_width / source_height);
        if (width == 0) width = 1;
    }
    return {(target_width - width) / 2U, (target_height - height) / 2U,
            width, height};
}

MappedDisplayPoint MapDisplayPoint(
    const float target_x, const float target_y,
    const std::uint32_t source_width, const std::uint32_t source_height,
    const std::uint32_t target_width, const std::uint32_t target_height) {
    if (!std::isfinite(target_x) || !std::isfinite(target_y)) {
        throw std::invalid_argument("display point coordinates must be finite");
    }
    const auto layout = FitDisplayRect(
        source_width, source_height, target_width, target_height);
    const auto left = static_cast<float>(layout.x);
    const auto top = static_cast<float>(layout.y);
    const auto right = left + static_cast<float>(layout.width);
    const auto bottom = top + static_cast<float>(layout.height);
    const auto inside = target_x >= left && target_x < right &&
                        target_y >= top && target_y < bottom;
    const auto normalized_x = std::clamp(
        (target_x - left) / static_cast<float>(layout.width), 0.0F, 1.0F);
    const auto normalized_y = std::clamp(
        (target_y - top) / static_cast<float>(layout.height), 0.0F, 1.0F);
    return {normalized_x * static_cast<float>(source_width),
            normalized_y * static_cast<float>(source_height), inside};
}

FrameRateSampler::FrameRateSampler(
    const std::uint64_t ticks_per_second,
    const std::uint64_t update_interval_ticks)
    : ticks_per_second_(ticks_per_second),
      update_interval_ticks_(update_interval_ticks) {
    if (ticks_per_second_ == 0U || update_interval_ticks_ == 0U) {
        throw std::invalid_argument("frame-rate sampler periods must be non-zero");
    }
}

std::optional<double> FrameRateSampler::Observe(
    const std::uint64_t presented_frames, const std::uint64_t ticks) {
    if (!initialized_) {
        previous_frames_ = presented_frames;
        previous_ticks_ = ticks;
        initialized_ = true;
        return std::nullopt;
    }
    if (presented_frames < previous_frames_ || ticks < previous_ticks_) {
        throw std::invalid_argument("frame-rate sample must be monotonic");
    }
    const auto elapsed = ticks - previous_ticks_;
    if (elapsed < update_interval_ticks_) return std::nullopt;
    const auto frames = presented_frames - previous_frames_;
    const auto rate = static_cast<long double>(frames) *
                      static_cast<long double>(ticks_per_second_) /
                      static_cast<long double>(elapsed);
    previous_frames_ = presented_frames;
    previous_ticks_ = ticks;
    return static_cast<double>(rate);
}

#if OGPLAY_HAS_SDL3
namespace {

constexpr SDL_InitFlags kSdlSubsystems = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;

std::string RequestedBackendName(const VideoBackend backend) {
    switch (backend) {
    case VideoBackend::automatic: return {};
    case VideoBackend::dummy: return "dummy";
    case VideoBackend::offscreen: return "offscreen";
    }
    throw std::invalid_argument("unknown video backend");
}

std::runtime_error SdlError(const std::string_view operation) {
    return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
}

[[nodiscard]] PointerButton MapPointerButton(const Uint8 button) noexcept {
    switch (button) {
    case SDL_BUTTON_LEFT: return PointerButton::primary;
    case SDL_BUTTON_MIDDLE: return PointerButton::middle;
    case SDL_BUTTON_RIGHT: return PointerButton::secondary;
    case SDL_BUTTON_X1: return PointerButton::auxiliary_1;
    case SDL_BUTTON_X2: return PointerButton::auxiliary_2;
    default: return PointerButton::unknown;
    }
}

class SdlWindowInput final : public WindowInput {
public:
    explicit SdlWindowInput(const VideoBackend backend) {
        const auto requested = RequestedBackendName(backend);
        const auto already_initialized = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0;
        if (already_initialized && !requested.empty()) {
            const auto* current = SDL_GetCurrentVideoDriver();
            if (current == nullptr || requested != current) {
                throw std::logic_error("SDL video subsystem already uses another backend");
            }
        }
        if (!already_initialized && !requested.empty() &&
            !SDL_SetHint(SDL_HINT_VIDEO_DRIVER, requested.c_str())) {
            throw SdlError("SDL_SetHint(SDL_HINT_VIDEO_DRIVER)");
        }
        if (!SDL_InitSubSystem(kSdlSubsystems)) {
            SDL_QuitSubSystem(kSdlSubsystems);
            throw SdlError("SDL_InitSubSystem");
        }
        initialized_ = true;
    }

    ~SdlWindowInput() override {
        Close();
        if (initialized_) SDL_QuitSubSystem(kSdlSubsystems);
    }

    void Open(const WindowOptions& options) override {
        if (window_ != nullptr) throw std::logic_error("window is already open");
        if (options.width == 0 || options.height == 0 ||
            options.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            options.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("window dimensions are out of range");
        }

        SDL_WindowFlags flags = 0;
        if (options.hidden) flags |= SDL_WINDOW_HIDDEN;
        if (options.resizable) flags |= SDL_WINDOW_RESIZABLE;
        window_ = SDL_CreateWindow(options.title.c_str(), static_cast<int>(options.width),
                                   static_cast<int>(options.height), flags);
        if (window_ == nullptr) throw SdlError("SDL_CreateWindow");
        present_count_ = 0;
    }

    void Close() noexcept override {
        if (window_ == nullptr) return;
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    void SetTitle(const std::string_view title) override {
        if (window_ == nullptr) {
            throw std::logic_error("window title requires an open window");
        }
        if (title.find('\0') != std::string_view::npos) {
            throw std::invalid_argument("window title contains an embedded NUL");
        }
        const std::string owned{title};
        if (!SDL_SetWindowTitle(window_, owned.c_str())) {
            throw SdlError("SDL_SetWindowTitle");
        }
    }

    WindowState State() const override {
        if (window_ == nullptr) return {};
        int width{};
        int height{};
        if (!SDL_GetWindowSize(window_, &width, &height)) {
            throw SdlError("SDL_GetWindowSize");
        }
        return {
            .open = true,
            .id = SDL_GetWindowID(window_),
            .width = static_cast<std::uint32_t>(width),
            .height = static_cast<std::uint32_t>(height),
            .present_count = present_count_,
        };
    }

    std::string_view BackendName() const noexcept override {
        const auto* name = SDL_GetCurrentVideoDriver();
        return name == nullptr ? std::string_view{} : std::string_view{name};
    }

    std::vector<InputEvent> PollEvents() override {
        if (window_ == nullptr) throw std::logic_error("event polling requires an open window");
        std::vector<InputEvent> result;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            AppendEvent(event, result);
        }
        return result;
    }

    void PresentRgba8(const std::span<const std::uint8_t> pixels,
                      const std::uint32_t width, const std::uint32_t height) override {
        if (window_ == nullptr) throw std::logic_error("frame presentation requires an open window");
        if (width == 0 || height == 0 ||
            width > static_cast<std::uint32_t>(std::numeric_limits<int>::max() / 4) ||
            height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("RGBA8 frame dimensions are out of range");
        }
        const auto row_bytes = static_cast<std::size_t>(width) * 4;
        if (height > std::numeric_limits<std::size_t>::max() / row_bytes ||
            pixels.size() != row_bytes * height) {
            throw std::invalid_argument("RGBA8 frame byte count does not match dimensions");
        }

        auto* target = SDL_GetWindowSurface(window_);
        if (target == nullptr) throw SdlError("SDL_GetWindowSurface");
        if (target->w <= 0 || target->h <= 0) {
            throw std::runtime_error("SDL window surface dimensions are invalid");
        }
        auto* source = SDL_CreateSurfaceFrom(
            static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
            const_cast<std::uint8_t*>(pixels.data()), static_cast<int>(row_bytes));
        if (source == nullptr) throw SdlError("SDL_CreateSurfaceFrom");
        const auto blend_disabled = SDL_SetSurfaceBlendMode(
            source, SDL_BLENDMODE_NONE);
        const auto layout = FitDisplayRect(
            width, height, static_cast<std::uint32_t>(target->w),
            static_cast<std::uint32_t>(target->h));
        const SDL_Rect destination{
            static_cast<int>(layout.x), static_cast<int>(layout.y),
            static_cast<int>(layout.width), static_cast<int>(layout.height)};
        const auto cleared = SDL_FillSurfaceRect(
            target, nullptr, SDL_MapSurfaceRGBA(target, 0, 0, 0, 255));
        const auto blitted = blend_disabled && cleared && SDL_BlitSurfaceScaled(
            source, nullptr, target, &destination, SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(source);
        if (!blend_disabled) throw SdlError("SDL_SetSurfaceBlendMode");
        if (!cleared) throw SdlError("SDL_FillSurfaceRect");
        if (!blitted) throw SdlError("SDL_BlitSurfaceScaled");
        if (!SDL_UpdateWindowSurface(window_)) throw SdlError("SDL_UpdateWindowSurface");
        ++present_count_;
    }

private:
    [[nodiscard]] bool IsOwnedWindow(const SDL_WindowID id) const noexcept {
        return id == 0 || id == SDL_GetWindowID(window_);
    }

    void AppendEvent(const SDL_Event& event, std::vector<InputEvent>& result) const {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            result.push_back({.type = InputEventType::quit,
                              .timestamp_ns = event.quit.timestamp});
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (IsOwnedWindow(event.window.windowID)) {
                result.push_back({.type = InputEventType::quit,
                                  .timestamp_ns = event.window.timestamp,
                                  .window_id = event.window.windowID});
            }
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (IsOwnedWindow(event.key.windowID)) {
                result.push_back({
                    .type = InputEventType::key,
                    .timestamp_ns = event.key.timestamp,
                    .window_id = event.key.windowID,
                    .device_id = event.key.which,
                    .code = static_cast<std::int32_t>(event.key.scancode),
                    .pressed = event.key.down,
                    .repeat = event.key.repeat,
                });
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (IsOwnedWindow(event.motion.windowID)) {
                result.push_back({
                    .type = InputEventType::pointer_motion,
                    .timestamp_ns = event.motion.timestamp,
                    .window_id = event.motion.windowID,
                    .device_id = event.motion.which,
                    .x = event.motion.x,
                    .y = event.motion.y,
                    .delta_x = event.motion.xrel,
                    .delta_y = event.motion.yrel,
                });
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (IsOwnedWindow(event.button.windowID)) {
                result.push_back({
                    .type = InputEventType::pointer_button,
                    .timestamp_ns = event.button.timestamp,
                    .window_id = event.button.windowID,
                    .device_id = event.button.which,
                    .code = static_cast<std::int32_t>(
                        MapPointerButton(event.button.button)),
                    .x = event.button.x,
                    .y = event.button.y,
                    .pressed = event.button.down,
                });
            }
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            result.push_back({
                .type = InputEventType::gamepad_axis,
                .timestamp_ns = event.gaxis.timestamp,
                .device_id = event.gaxis.which,
                .code = event.gaxis.axis,
                .value = event.gaxis.value,
            });
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            result.push_back({
                .type = InputEventType::gamepad_button,
                .timestamp_ns = event.gbutton.timestamp,
                .device_id = event.gbutton.which,
                .code = event.gbutton.button,
                .pressed = event.gbutton.down,
            });
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            result.push_back({
                .type = event.type == SDL_EVENT_GAMEPAD_ADDED
                            ? InputEventType::gamepad_added
                            : InputEventType::gamepad_removed,
                .timestamp_ns = event.gdevice.timestamp,
                .device_id = event.gdevice.which,
            });
            break;
        default: break;
        }
    }

    SDL_Window* window_{};
    bool initialized_{};
    std::uint64_t present_count_{};
};

}  // namespace
#endif

std::unique_ptr<WindowInput> CreateSdlWindowInput(const VideoBackend backend) {
#if OGPLAY_HAS_SDL3
    return std::make_unique<SdlWindowInput>(backend);
#else
    static_cast<void>(backend);
    throw std::runtime_error("SDL3 support is disabled in this build");
#endif
}

}  // namespace ogplay::hal
