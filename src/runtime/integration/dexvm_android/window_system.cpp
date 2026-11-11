#include "window_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>

namespace ogplay::runtime::android_intrinsics {
namespace {
constexpr int kNotFocusable = 0x8, kNotTouchable = 0x10, kNotTouchModal = 0x20;
constexpr int kWatchOutside = 0x40000, kLayoutInScreen = 0x100, kLayoutNoLimits = 0x200;

bool HasAlpha(int format) {
    return format == -3 || format == -2 || format == 1 || format == 6 || format == 7 || format == 8 || format == 10;
}

int SubLayer(int type) {
    switch (type) {
    case 1000:
    case 1003: return 1;
    case 1001: return -2;
    case 1002: return 2;
    case 1004: return -1;
    default: return 0;
    }
}

WindowRect Intersect(WindowRect a, WindowRect b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

void Validate(const WindowAttributes& a) {
    if (!((a.type >= 1 && a.type <= 99) || (a.type >= 1000 && a.type <= 1004) || a.type == 2005))
        throw std::invalid_argument("unsupported window type");
    if ((a.type >= 1000 && a.type <= 1004) != (a.parent.value != 0))
        throw std::invalid_argument("subwindow requires an application parent");
    if (a.width < -2 || a.height < -2 || a.width > 4096 || a.height > 4096 ||
        std::abs(static_cast<std::int64_t>(a.x)) > 16384 || std::abs(static_cast<std::int64_t>(a.y)) > 16384 ||
        !std::isfinite(a.alpha) || a.alpha < 0 || a.alpha > 1 || !std::isfinite(a.horizontal_margin) ||
        !std::isfinite(a.vertical_margin) || std::abs(a.horizontal_margin) > 1 || std::abs(a.vertical_margin) > 1)
        throw std::invalid_argument("invalid window geometry or alpha");
}
}  // namespace

bool WindowRect::Contains(float x, float y) const {
    return x >= left && y >= top && x < right && y < bottom;
}

bool WindowRect::Covers(WindowRect b) const {
    return !b.Empty() && left <= b.left && top <= b.top && right >= b.right && bottom >= b.bottom;
}

WindowSystem::WindowSystem(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        throw std::invalid_argument("invalid logical display");
}

WindowSystem::Window& WindowSystem::Require(WindowId id) {
    const auto found = windows_.find(id);
    if (found == windows_.end()) throw std::invalid_argument("unknown window");
    return found->second;
}

const WindowSystem::Window& WindowSystem::Require(WindowId id) const {
    const auto found = windows_.find(id);
    if (found == windows_.end()) throw std::invalid_argument("unknown window");
    return found->second;
}

bool WindowSystem::Visible(WindowId id) const {
    const auto& window = Require(id);
    const auto app = application_visibility_.find(window.attrs.application_token);
    return window.visible && (app == application_visibility_.end() || app->second) &&
           (!window.attrs.parent.value || Visible(window.attrs.parent));
}

void WindowSystem::SetApplicationVisible(std::uint32_t token, bool visible) {
    std::scoped_lock lock(mutex_);
    ++revision_;
    application_visibility_.insert_or_assign(token, visible);
}

WindowRect WindowSystem::Layout(const WindowAttributes& a, int requested_width, int requested_height) const {
    auto container = DisplayFrame();
    if (a.parent.value && !(a.flags & kLayoutInScreen)) container = Require(a.parent).frame;
    const auto dimension = [](int value, int requested, int available) {
        return value == -1 ? available : value == -2 ? std::clamp(requested, 0, available) : value;
    };
    const auto w = dimension(a.width, requested_width, container.Width());
    const auto h = dimension(a.height, requested_height, container.Height());
    int x = container.left, y = container.top;
    const auto dx = a.x + static_cast<int>(a.horizontal_margin * container.Width());
    const auto dy = a.y + static_cast<int>(a.vertical_margin * container.Height());
    switch (a.gravity & 7) {
    case 1: x += (container.Width() - w) / 2 + dx; break;
    case 5: x = container.right - w - dx; break;
    default: x += dx; break;
    }
    switch (a.gravity & 0x70) {
    case 0x10: y += (container.Height() - h) / 2 + dy; break;
    case 0x50: y = container.bottom - h - dy; break;
    default: y += dy; break;
    }
    WindowRect result{x, y, x + w, y + h};
    // Gravity.applyDisplay shifts a fitting window, then clips oversized bounds.
    if (!(a.flags & kLayoutNoLimits)) {
        if (result.left < container.left) {
            result.right += container.left - result.left;
            result.left = container.left;
        }
        if (result.top < container.top) {
            result.bottom += container.top - result.top;
            result.top = container.top;
        }
        if (result.right > container.right) {
            result.left -= result.right - container.right;
            result.right = container.right;
        }
        if (result.bottom > container.bottom) {
            result.top -= result.bottom - container.bottom;
            result.bottom = container.bottom;
        }
        result = Intersect(result, container);
    }
    return result;
}

WindowId WindowSystem::Add(const WindowAttributes& a, bool visible, bool input_channel) {
    Validate(a);
    std::scoped_lock lock(mutex_);
    ++revision_;
    if (a.parent.value && Require(a.parent).attrs.parent.value)
        throw std::invalid_argument("subwindow cannot parent another subwindow");
    if (windows_.size() >= 256 || next_window_ == std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("window budget exhausted");
    Window window;
    window.attrs = a;
    window.frame = Layout(a, std::max(a.width, 0), std::max(a.height, 0));
    window.visible = visible;
    window.input_channel = input_channel;
    const WindowId id{next_window_++};
    windows_.emplace(id, std::move(window));
    return id;
}

WindowRect WindowSystem::Relayout(WindowId id, const WindowAttributes& a, int rw, int rh, bool visible) {
    Validate(a);
    if (rw < 0 || rh < 0 || rw > 4096 || rh > 4096) throw std::invalid_argument("invalid requested buffer size");
    std::scoped_lock lock(mutex_);
    ++revision_;
    auto& window = Require(id);
    if (a.type != window.attrs.type || a.parent != window.attrs.parent)
        throw std::invalid_argument("window type and parent are immutable");
    const auto frame = Layout(a, rw, rh);
    window.attrs = a;
    window.frame = frame;
    window.visible = visible;
    if (!visible) window.pixels.reset();
    // Existing posted buffers remain immutable and can be scaled until replaced.
    return frame;
}

void WindowSystem::Remove(WindowId id) {
    std::scoped_lock lock(mutex_);
    ++revision_;
    Require(id);
    std::erase_if(windows_, [&](const auto& pair) { return pair.first == id || pair.second.attrs.parent == id; });
    if (touch_target_.value && !windows_.contains(touch_target_)) {
        touch_target_ = {};
        drop_touch_tail_ = true;
    }
}

void WindowSystem::SetTransparentRegion(WindowId id, std::vector<WindowRect> region) {
    std::scoped_lock lock(mutex_);
    ++revision_;
    Require(id).transparent = std::move(region);
}

void WindowSystem::SetCrop(WindowId id, WindowRect crop) {
    std::scoped_lock lock(mutex_);
    ++revision_;
    Require(id).crop = crop;
}

void WindowSystem::SetInsets(WindowId id, int touchable, WindowRect content, WindowRect visible,
                             std::vector<WindowRect> region) {
    if (touchable < 0 || touchable > 3) throw std::invalid_argument("unknown touchable insets mode");
    std::scoped_lock lock(mutex_);
    auto& window = Require(id);
    window.touchable = touchable;
    window.content_insets = content;
    window.visible_insets = visible;
    window.touchable_region = std::move(region);
}

void WindowSystem::Post(WindowId id, std::shared_ptr<const video::VideoFrame> pixels, bool premultiplied,
                        bool opaque_pixels, bool transparent_pixels, bool egl) {
    if (!pixels || pixels->width == 0 || pixels->height == 0 || pixels->width > 4096 || pixels->height > 4096 ||
        pixels->rgba8.size() != static_cast<std::size_t>(pixels->width) * pixels->height * 4)
        throw std::invalid_argument("invalid posted buffer");
    std::scoped_lock lock(mutex_);
    ++revision_;
    auto& window = Require(id);
    window.pixels = std::move(pixels);
    window.premultiplied = premultiplied;
    window.opaque_pixels = opaque_pixels;
    window.transparent_pixels = transparent_pixels;
    window.egl = egl;
}

void WindowSystem::ClearFrame(WindowId id, const std::shared_ptr<const video::VideoFrame>& expected) {
    std::scoped_lock lock(mutex_);
    const auto window = windows_.find(id);
    if (!expected || window == windows_.end() || window->second.pixels != expected) return;
    window->second.pixels.reset();
    ++revision_;
}

WindowRect WindowSystem::Frame(WindowId id) const {
    std::scoped_lock lock(mutex_);
    return Require(id).frame;
}

bool WindowSystem::Exists(WindowId id) const {
    std::scoped_lock lock(mutex_);
    return windows_.contains(id);
}

bool WindowSystem::OpaqueFormat(WindowId id) const {
    std::scoped_lock lock(mutex_);
    return !HasAlpha(Require(id).attrs.format);
}

std::vector<WindowId> WindowSystem::Ordered() const {
    std::vector<WindowId> ordered;
    for (const auto& [id, _] : windows_)
        ordered.push_back(id);
    const auto key = [&](WindowId id) {
        const auto& a = Require(id).attrs;
        const auto root = a.parent.value ? a.parent : id;
        const auto& ra = Require(root).attrs;
        const auto base = Require(id).layer ? static_cast<std::int64_t>(*Require(id).layer)
                                            : static_cast<std::int64_t>(root.value) * 10 + SubLayer(a.type);
        return std::tuple{ra.type == 2005 ? 6 : 2, base, id.value};
    };
    std::ranges::sort(ordered, [&](WindowId a, WindowId b) { return key(a) < key(b); });
    return ordered;
}

std::vector<WindowLayer> WindowSystem::Layers() const {
    std::scoped_lock lock(mutex_);
    return ReadLayers();
}

std::vector<WindowLayer> WindowSystem::ReadLayers() const {
    std::vector<WindowLayer> layers;
    for (const auto id : Ordered()) {
        const auto& w = Require(id);
        if (!Visible(id) || !w.pixels || w.frame.Empty() || w.attrs.alpha == 0) continue;
        if (w.transparent_pixels && HasAlpha(w.attrs.format)) continue;
        const auto local = WindowRect{0, 0, w.frame.Width(), w.frame.Height()};
        const auto crop = w.crop.Empty() ? local : Intersect(local, w.crop);
        // SurfaceFlinger transparent hints are exclusions, even for opaque formats.
        if (std::ranges::any_of(w.transparent, [&](const auto& r) { return r.Covers(crop); })) continue;
        layers.push_back({id, w.frame, crop, w.transparent, w.attrs.format, w.attrs.alpha, w.premultiplied,
                          w.opaque_pixels, w.egl, w.pixels});
    }
    return layers;
}

WindowComposition WindowSystem::Compose() const {
    return Compose(Layers());
}

std::optional<WindowComposition> WindowSystem::ComposeIfChanged(std::uint64_t& revision) const {
    std::vector<WindowLayer> layers;
    {
        std::scoped_lock lock(mutex_);
        if (revision == revision_) return std::nullopt;
        layers = ReadLayers();
        revision = revision_;
    }
    return Compose(std::move(layers));
}

WindowComposition WindowSystem::Compose(std::vector<WindowLayer> layers) const {
    if (layers.empty()) return {};
    // Discard everything below the topmost certified covering opaque layer.
    for (std::size_t i = layers.size(); i-- > 0;) {
        const auto& l = layers[i];
        if (l.frame == DisplayFrame() && l.crop == DisplayFrame() && l.transparent.empty() && l.alpha == 1 &&
            (!HasAlpha(l.format) || l.opaque_pixels)) {
            layers.erase(layers.begin(), layers.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    if (layers.size() == 1) {
        const auto& l = layers.front();
        if (l.frame == DisplayFrame() && l.crop == DisplayFrame() && l.transparent.empty() && l.alpha == 1 &&
            l.opaque_pixels && l.pixels->width == static_cast<unsigned>(width_) &&
            l.pixels->height == static_cast<unsigned>(height_))
            return {l.pixels, false, true, l.egl};
    }
    auto result = std::make_shared<video::VideoFrame>();
    result->width = static_cast<unsigned>(width_);
    result->height = static_cast<unsigned>(height_);
    result->rgba8.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4);
    auto& out = result->rgba8;
    for (std::size_t i = 3; i < out.size(); i += 4)
        out[i] = 255;
    for (const auto& l : layers) {
        const auto area = Intersect(DisplayFrame(), {l.frame.left + l.crop.left, l.frame.top + l.crop.top,
                                                     l.frame.left + l.crop.right, l.frame.top + l.crop.bottom});
        const bool opaque = !HasAlpha(l.format) || l.opaque_pixels;
        const auto wa = static_cast<unsigned>(std::lround(l.alpha * 255));
        for (int y = area.top; y < area.bottom; ++y) {
            const auto sy = static_cast<std::uint64_t>(y - l.frame.top) * l.pixels->height /
                            static_cast<unsigned>(l.frame.Height());
            if (opaque && wa == 255 && l.transparent.empty() &&
                l.pixels->width == static_cast<unsigned>(l.frame.Width())) {
                auto* dst = out.data() + (static_cast<std::size_t>(y) * static_cast<unsigned>(width_) +
                                          static_cast<unsigned>(area.left)) *
                                             4;
                const auto* src = l.pixels->rgba8.data() +
                                  (sy * l.pixels->width + static_cast<unsigned>(area.left - l.frame.left)) * 4;
                std::copy_n(src, static_cast<std::size_t>(area.Width()) * 4, dst);
                if (!l.opaque_pixels)
                    for (int x = 0; x < area.Width(); ++x)
                        dst[x * 4 + 3] = 255;
                continue;
            }
            for (int x = area.left; x < area.right; ++x) {
                if (std::ranges::any_of(l.transparent, [&](const auto& r) {
                        return r.Contains(static_cast<float>(x - l.frame.left), static_cast<float>(y - l.frame.top));
                    }))
                    continue;
                const auto sx = static_cast<std::uint64_t>(x - l.frame.left) * l.pixels->width /
                                static_cast<unsigned>(l.frame.Width());
                const auto si = static_cast<std::size_t>((sy * l.pixels->width + sx) * 4);
                const auto di =
                    (static_cast<std::size_t>(y) * static_cast<unsigned>(width_) + static_cast<unsigned>(x)) * 4;
                const auto a = opaque ? wa : (l.pixels->rgba8[si + 3] * wa + 127) / 255;
                for (std::size_t c = 0; c < 3; ++c) {
                    const auto sc = l.pixels->rgba8[si + c];
                    const auto color = l.premultiplied && !opaque ? sc * wa : sc * a;
                    out[di + c] =
                        static_cast<std::uint8_t>(std::min(255U, (color + out[di + c] * (255 - a) + 127) / 255));
                }
            }
        }
    }
    return {std::move(result), false, false};
}

WindowId WindowSystem::FocusLocked() const {
    auto ordered = Ordered();
    for (const auto id : std::views::reverse(ordered)) {
        const auto& w = Require(id);
        if (Visible(id) && w.input_channel && !(w.attrs.flags & kNotFocusable)) return id;
    }
    return {};
}

WindowId WindowSystem::Focus() const {
    std::scoped_lock lock(mutex_);
    return FocusLocked();
}

bool WindowSystem::InTouchMode() const {
    std::scoped_lock lock(mutex_);
    return in_touch_mode_;
}

void WindowSystem::SetInTouchMode(bool value) {
    std::scoped_lock lock(mutex_);
    in_touch_mode_ = value;
}

void WindowSystem::UpdateSurfaces(std::span<const SurfaceUpdate> updates) {
    std::scoped_lock lock(mutex_);
    ++revision_;
    // Validate the whole batch before publishing any state. Shared posted pixels
    // are leased by both versions; this never copies a frame.
    auto candidate = windows_;
    for (const auto& u : updates) {
        if (!candidate.contains(u.window)) throw std::invalid_argument("unknown SurfaceControl window");
        auto& w = candidate.at(u.window);
        switch (u.kind) {
        case SurfaceUpdate::Kind::position:
            if (!std::isfinite(u.x) || !std::isfinite(u.y) || std::abs(u.x) > 16384 || std::abs(u.y) > 16384)
                throw std::invalid_argument("invalid Surface position");
            w.attrs.x = static_cast<int>(std::floor(u.x));
            w.attrs.y = static_cast<int>(std::floor(u.y));
            break;
        case SurfaceUpdate::Kind::size:
            w.attrs.width = u.a;
            w.attrs.height = u.b;
            break;
        case SurfaceUpdate::Kind::alpha: w.attrs.alpha = u.x; break;
        case SurfaceUpdate::Kind::layer: w.layer = u.a; break;
        case SurfaceUpdate::Kind::visible: w.visible = u.a != 0; break;
        case SurfaceUpdate::Kind::opaque: w.attrs.format = u.a ? -1 : 1; break;
        case SurfaceUpdate::Kind::crop: w.crop = u.rect; break;
        case SurfaceUpdate::Kind::transparent: w.transparent = u.region; break;
        }
        Validate(w.attrs);
        if (u.kind == SurfaceUpdate::Kind::position || u.kind == SurfaceUpdate::Kind::size)
            w.frame = Layout(w.attrs, std::max(w.attrs.width, 0), std::max(w.attrs.height, 0));
    }
    windows_.swap(candidate);
}

WindowInputDelivery WindowSystem::Deliver(WindowId id, const AndroidBoundaryInput& input) const {
    auto event = input;
    const auto& frame = Require(id).frame;
    if (event.type != AndroidBoundaryInputType::key) {
        event.x -= frame.left;
        event.y -= frame.top;
        event.x_offset -= frame.left;
        event.y_offset -= frame.top;
    }
    return {id, std::move(event)};
}

std::vector<WindowInputDelivery> WindowSystem::Dispatch(const AndroidBoundaryInput& input) {
    auto event = NormalizeAndroidInput(input);
    std::scoped_lock lock(mutex_);
    std::vector<WindowInputDelivery> deliveries;
    if (event.type == AndroidBoundaryInputType::key) {
        const auto focus = FocusLocked();
        if (focus.value) deliveries.push_back(Deliver(focus, event));
        return deliveries;
    }
    const int action = event.action & 255;
    if (action == 0) {
        if (touch_target_.value && last_touch_) {
            auto cancel = *last_touch_;
            cancel.action = 3;
            deliveries.push_back(Deliver(touch_target_, cancel));
        }
        touch_target_ = {};
        drop_touch_tail_ = false;
        in_touch_mode_ = true;
        auto ordered = Ordered();
        for (const auto id : std::views::reverse(ordered)) {
            const auto& w = Require(id);
            if (!Visible(id) || !w.input_channel || (w.attrs.flags & kNotTouchable)) continue;
            bool inside = w.frame.Contains(event.x, event.y);
            const auto local_x = event.x - w.frame.left, local_y = event.y - w.frame.top;
            if (inside && w.touchable == 3)
                inside = std::ranges::any_of(w.touchable_region,
                                             [&](const auto& r) { return r.Contains(local_x, local_y); });
            else if (inside && (w.touchable == 1 || w.touchable == 2)) {
                const auto& i = w.touchable == 1 ? w.content_insets : w.visible_insets;
                inside = WindowRect{i.left, i.top, w.frame.Width() - i.right, w.frame.Height() - i.bottom}.Contains(
                    local_x, local_y);
            }
            if (inside || !(w.attrs.flags & (kNotFocusable | kNotTouchModal))) {
                touch_target_ = id;
                break;
            }
            if (w.attrs.flags & kWatchOutside) {
                auto outside = event;
                outside.action = 4;
                deliveries.push_back(Deliver(id, outside));
            }
        }
    }
    if (touch_target_.value) {
        if (!Visible(touch_target_) || (Require(touch_target_).attrs.flags & kNotTouchable)) {
            if (last_touch_) {
                auto cancel = *last_touch_;
                cancel.action = 3;
                deliveries.push_back(Deliver(touch_target_, cancel));
            }
            touch_target_ = {};
            drop_touch_tail_ = true;
        } else if (!drop_touch_tail_)
            deliveries.push_back(Deliver(touch_target_, event));
    }
    last_touch_ = event;
    if (action == 1 || action == 3) {
        touch_target_ = {};
        last_touch_.reset();
        drop_touch_tail_ = false;
    }
    return deliveries;
}

std::vector<WindowInputDelivery> WindowSystem::CancelTouch() {
    std::scoped_lock lock(mutex_);
    return CancelTouchLocked();
}

std::vector<WindowInputDelivery> WindowSystem::CancelWindowTouch(WindowId window) {
    std::scoped_lock lock(mutex_);
    if (!touch_target_.value || (touch_target_ != window && Require(touch_target_).attrs.parent != window)) return {};
    return CancelTouchLocked();
}

std::vector<WindowInputDelivery> WindowSystem::CancelApplicationTouch(std::uint32_t token) {
    std::scoped_lock lock(mutex_);
    if (!touch_target_.value || Require(touch_target_).attrs.application_token != token) return {};
    return CancelTouchLocked();
}

std::vector<WindowInputDelivery> WindowSystem::CancelTouchLocked() {
    std::vector<WindowInputDelivery> result;
    if (touch_target_.value && last_touch_) {
        auto cancel = *last_touch_;
        cancel.action = 3;
        result.push_back(Deliver(touch_target_, cancel));
    }
    touch_target_ = {};
    last_touch_.reset();
    drop_touch_tail_ = true;
    return result;
}
}  // namespace ogplay::runtime::android_intrinsics
