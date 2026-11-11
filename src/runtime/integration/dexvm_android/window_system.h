#pragma once

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/video/video_player.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace ogplay::runtime::android_intrinsics {

// Process-local identities, never guest or host pointers.
struct WindowId {
    std::uint32_t value{};
    auto operator<=>(const WindowId&) const = default;
};

struct WindowRect {
    int left{}, top{}, right{}, bottom{};

    int Width() const { return right - left; }

    int Height() const { return bottom - top; }

    bool Empty() const { return right <= left || bottom <= top; }

    bool Contains(float x, float y) const;
    bool Covers(WindowRect other) const;
    auto operator<=>(const WindowRect&) const = default;
};

struct WindowAttributes {
    int type{2}, flags{}, format{-1}, width{-1}, height{-1};
    int x{}, y{}, gravity{0x33};
    float horizontal_margin{}, vertical_margin{}, alpha{1};
    WindowId parent{};
    std::uint32_t application_token{};
};

struct WindowLayer {
    WindowId window;
    WindowRect frame, crop;
    std::vector<WindowRect> transparent;
    int format{};
    float alpha{1};
    bool premultiplied{}, opaque_pixels{};
    std::shared_ptr<const video::VideoFrame> pixels;
};

struct WindowComposition {
    // A null image denotes FrameService's lazy black base. A single full-screen
    // opaque producer is returned by lease, without visiting or copying pixels.
    std::shared_ptr<const video::VideoFrame> pixels;
    bool blank{true}, borrowed{};
};

struct WindowInputDelivery {
    WindowId window;
    AndroidBoundaryInput input;
};

struct SurfaceUpdate {
    enum class Kind { position, size, alpha, layer, visible, opaque, crop, transparent } kind;
    WindowId window;
    int a{}, b{};
    float x{}, y{};
    WindowRect rect{};
    std::vector<WindowRect> region{};
};

// WMS facts and composition are independent of Java View hierarchy/projection.
// The lock protects both producer publication and host-side input selection.
// Java callbacks run after returning from this object, never under its lock.
class WindowSystem final {
public:
    WindowSystem(int width, int height);
    WindowId Add(const WindowAttributes& attrs, bool visible, bool input_channel = true);
    WindowRect Relayout(WindowId window, const WindowAttributes& attrs, int requested_width, int requested_height,
                        bool visible);
    void Remove(WindowId window);
    void SetTransparentRegion(WindowId window, std::vector<WindowRect> region);
    void SetCrop(WindowId window, WindowRect crop);
    void SetInsets(WindowId window, int touchable, WindowRect content, WindowRect visible,
                   std::vector<WindowRect> region);
    void Post(WindowId window, std::shared_ptr<const video::VideoFrame> pixels, bool premultiplied,
              bool opaque_pixels = false, bool transparent_pixels = false);
    WindowRect Frame(WindowId window) const;
    bool Exists(WindowId window) const;

    WindowRect DisplayFrame() const { return {0, 0, width_, height_}; }

    std::vector<WindowLayer> Layers() const;
    WindowComposition Compose() const;
    std::vector<WindowInputDelivery> Dispatch(const AndroidBoundaryInput& input);
    std::vector<WindowInputDelivery> CancelTouch();
    std::vector<WindowInputDelivery> CancelWindowTouch(WindowId window);
    std::vector<WindowInputDelivery> CancelApplicationTouch(std::uint32_t token);
    WindowId Focus() const;
    bool InTouchMode() const;
    void SetInTouchMode(bool value);
    void UpdateSurfaces(std::span<const SurfaceUpdate> updates);
    void SetApplicationVisible(std::uint32_t token, bool visible);

private:
    struct Window {
        WindowAttributes attrs;
        WindowRect frame, crop;
        bool visible{}, input_channel{}, premultiplied{}, opaque_pixels{};
        bool transparent_pixels{};
        int touchable{};
        WindowRect content_insets, visible_insets;
        std::vector<WindowRect> transparent, touchable_region;
        std::shared_ptr<const video::VideoFrame> pixels;
        std::optional<int> layer;
    };

    Window& Require(WindowId window);
    const Window& Require(WindowId window) const;
    bool Visible(WindowId window) const;
    WindowRect Layout(const WindowAttributes& attrs, int requested_width, int requested_height) const;
    std::vector<WindowId> Ordered() const;
    WindowId FocusLocked() const;
    WindowInputDelivery Deliver(WindowId window, const AndroidBoundaryInput& input) const;
    std::vector<WindowInputDelivery> CancelTouchLocked();

    int width_, height_;
    mutable std::mutex mutex_;
    std::map<WindowId, Window> windows_;
    std::map<std::uint32_t, bool> application_visibility_;
    std::uint32_t next_window_{1};
    bool in_touch_mode_{true};
    WindowId touch_target_{};
    std::optional<AndroidBoundaryInput> last_touch_;
    bool drop_touch_tail_{};
};

}  // namespace ogplay::runtime::android_intrinsics
