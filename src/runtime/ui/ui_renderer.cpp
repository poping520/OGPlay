#include "ogplay/runtime/ui/ui_renderer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ogplay::runtime::ui {
namespace {

[[nodiscard]] Rect Intersect(const Rect a, const Rect b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top),
            std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

void AppendNode(const UiTree& tree, const UiNodeId id,
                const UiBitmapCache& bitmaps, UiRenderList& out) {
    const auto& node = *tree.Get(id);
    if (node.visibility != Visibility::Visible) return;
    out.emplace_back(PushClip{node.screen_frame});
    if (node.background_color.has_value()) {
        out.emplace_back(DrawSolidRect{node.screen_frame,
                                       *node.background_color, node.alpha});
    }
    if (node.image_resource_id != 0) {
        const auto found = bitmaps.find(node.image_resource_id);
        if (found != bitmaps.end() && found->second != nullptr) {
            const auto& bitmap = *found->second;
            out.emplace_back(DrawBitmap{
                {node.screen_frame.left, node.screen_frame.top,
                 node.screen_frame.left + bitmap.width,
                 node.screen_frame.top + bitmap.height},
                found->second, node.alpha});
        }
    }
    for (const auto child : node.children) AppendNode(tree, child, bitmaps, out);
    out.emplace_back(PopClip{});
}

[[nodiscard]] std::uint8_t AlphaByte(const float alpha) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
}

void Blend(std::uint8_t* destination, const std::uint8_t red,
           const std::uint8_t green, const std::uint8_t blue,
           const std::uint8_t source_alpha) {
    const auto inverse = 255U - source_alpha;
    const auto blend = [source_alpha, inverse](const std::uint8_t source,
                                                const std::uint8_t target) {
        return static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(source) * source_alpha +
             static_cast<std::uint32_t>(target) * inverse + 127U) /
            255U);
    };
    destination[0] = blend(red, destination[0]);
    destination[1] = blend(green, destination[1]);
    destination[2] = blend(blue, destination[2]);
    destination[3] = static_cast<std::uint8_t>(
        source_alpha +
        (static_cast<std::uint32_t>(destination[3]) * inverse + 127U) / 255U);
}

template <typename Pixel>
void PaintRect(UiOverlayFrame& frame, const Rect rect, const Rect clip,
               Pixel&& pixel) {
    const auto bounds = Intersect(Intersect(rect, clip),
                                  {0, 0, static_cast<std::int32_t>(frame.width),
                                   static_cast<std::int32_t>(frame.height)});
    for (auto y = bounds.top; y < bounds.bottom; ++y) {
        for (auto x = bounds.left; x < bounds.right; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * frame.width +
                 static_cast<std::size_t>(x)) * 4U;
            pixel(x, y, frame.rgba8.data() + offset);
        }
    }
}

}  // namespace

UiRenderList BuildUiRenderList(const UiTree& tree,
                               const UiBitmapCache& bitmaps) {
    UiRenderList commands;
    AppendNode(tree, tree.Root(), bitmaps, commands);
    return commands;
}

UiOverlayFrame RasterizeUiOverlay(const UiRenderList& commands,
                                  const UiMetrics metrics) {
    if (metrics.width < 0 || metrics.height < 0) {
        throw std::runtime_error("UI overlay metrics must be non-negative");
    }
    UiOverlayFrame frame{.width = static_cast<std::uint32_t>(metrics.width),
                         .height = static_cast<std::uint32_t>(metrics.height),
                         .rgba8 = {}};
    frame.rgba8.resize(static_cast<std::size_t>(frame.width) * frame.height * 4U);
    std::vector<Rect> clips{{0, 0, metrics.width, metrics.height}};
    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushClip>(&command)) {
            clips.push_back(Intersect(clips.back(), push->rect));
        } else if (std::holds_alternative<PopClip>(command)) {
            if (clips.size() <= 1) throw std::runtime_error("UI clip stack underflow");
            clips.pop_back();
        } else if (const auto* solid = std::get_if<DrawSolidRect>(&command)) {
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(solid->rgba & 0xffU) *
                 AlphaByte(solid->alpha) + 127U) / 255U);
            PaintRect(frame, solid->rect, clips.back(), [solid, alpha](auto, auto,
                                                                       auto* dst) {
                Blend(dst, static_cast<std::uint8_t>(solid->rgba >> 24U),
                      static_cast<std::uint8_t>(solid->rgba >> 16U),
                      static_cast<std::uint8_t>(solid->rgba >> 8U), alpha);
            });
        } else if (const auto* bitmap = std::get_if<DrawBitmap>(&command)) {
            if (bitmap->bitmap == nullptr || bitmap->bitmap->width <= 0 ||
                bitmap->bitmap->height <= 0 ||
                bitmap->bitmap->rgba8.size() !=
                    static_cast<std::size_t>(bitmap->bitmap->width) *
                        static_cast<std::size_t>(bitmap->bitmap->height) * 4U) {
                throw std::runtime_error("invalid UI bitmap command");
            }
            PaintRect(frame, bitmap->rect, clips.back(), [bitmap](const auto x,
                                                                   const auto y,
                                                                   auto* dst) {
                const auto bx = x - bitmap->rect.left;
                const auto by = y - bitmap->rect.top;
                const auto offset =
                    (static_cast<std::size_t>(by) *
                         static_cast<std::size_t>(bitmap->bitmap->width) +
                     static_cast<std::size_t>(bx)) * 4U;
                const auto* src = bitmap->bitmap->rgba8.data() + offset;
                const auto alpha = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(src[3]) *
                     AlphaByte(bitmap->alpha) + 127U) / 255U);
                Blend(dst, src[0], src[1], src[2], alpha);
            });
        }
    }
    if (clips.size() != 1) throw std::runtime_error("UI clip stack is unbalanced");
    return frame;
}

const UiOverlayFrame& UiOverlayRenderer::Render(UiTree& tree,
                                                const UiBitmapCache& bitmaps,
                                                const UiMetrics metrics) {
    const auto* root = tree.Get(tree.Root());
    const bool rebuild = generation_ != tree.Generation() ||
                         metrics_.width != metrics.width ||
                         metrics_.height != metrics.height || root->draw_dirty;
    if (rebuild) {
        cached_ = RasterizeUiOverlay(BuildUiRenderList(tree, bitmaps), metrics);
        generation_ = tree.Generation();
        metrics_ = metrics;
        ++build_count_;
        tree.ClearDirty();
    }
    return cached_;
}

}  // namespace ogplay::runtime::ui
