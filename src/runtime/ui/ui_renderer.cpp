#include "ogplay/runtime/ui/ui_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime::ui {
namespace {

using Glyph = std::array<std::uint8_t, 7>;

[[nodiscard]] Glyph GlyphRows(char16_t value) {
    if (value >= u'a' && value <= u'z') value -= u'a' - u'A';
    switch (value) {
        case u' ': return {};
        case u'A': return {14, 17, 17, 31, 17, 17, 17};
        case u'B': return {30, 17, 17, 30, 17, 17, 30};
        case u'C': return {14, 17, 16, 16, 16, 17, 14};
        case u'D': return {30, 17, 17, 17, 17, 17, 30};
        case u'E': return {31, 16, 16, 30, 16, 16, 31};
        case u'F': return {31, 16, 16, 30, 16, 16, 16};
        case u'G': return {14, 17, 16, 23, 17, 17, 15};
        case u'H': return {17, 17, 17, 31, 17, 17, 17};
        case u'I': return {14, 4, 4, 4, 4, 4, 14};
        case u'J': return {7, 2, 2, 2, 18, 18, 12};
        case u'K': return {17, 18, 20, 24, 20, 18, 17};
        case u'L': return {16, 16, 16, 16, 16, 16, 31};
        case u'M': return {17, 27, 21, 21, 17, 17, 17};
        case u'N': return {17, 25, 21, 19, 17, 17, 17};
        case u'O': return {14, 17, 17, 17, 17, 17, 14};
        case u'P': return {30, 17, 17, 30, 16, 16, 16};
        case u'Q': return {14, 17, 17, 17, 21, 18, 13};
        case u'R': return {30, 17, 17, 30, 20, 18, 17};
        case u'S': return {15, 16, 16, 14, 1, 1, 30};
        case u'T': return {31, 4, 4, 4, 4, 4, 4};
        case u'U': return {17, 17, 17, 17, 17, 17, 14};
        case u'V': return {17, 17, 17, 17, 17, 10, 4};
        case u'W': return {17, 17, 17, 21, 21, 21, 10};
        case u'X': return {17, 17, 10, 4, 10, 17, 17};
        case u'Y': return {17, 17, 10, 4, 4, 4, 4};
        case u'Z': return {31, 1, 2, 4, 8, 16, 31};
        case u'0': return {14, 17, 19, 21, 25, 17, 14};
        case u'1': return {4, 12, 4, 4, 4, 4, 14};
        case u'2': return {14, 17, 1, 2, 4, 8, 31};
        case u'3': return {30, 1, 1, 14, 1, 1, 30};
        case u'4': return {2, 6, 10, 18, 31, 2, 2};
        case u'5': return {31, 16, 16, 30, 1, 1, 30};
        case u'6': return {14, 16, 16, 30, 17, 17, 14};
        case u'7': return {31, 1, 2, 4, 8, 8, 8};
        case u'8': return {14, 17, 17, 14, 17, 17, 14};
        case u'9': return {14, 17, 17, 15, 1, 1, 14};
        case u'.': return {0, 0, 0, 0, 0, 6, 6};
        case u',': return {0, 0, 0, 0, 0, 6, 4};
        case u':': return {0, 6, 6, 0, 6, 6, 0};
        case u';': return {0, 6, 6, 0, 6, 4, 8};
        case u'!': return {4, 4, 4, 4, 4, 0, 4};
        case u'?': return {14, 17, 1, 2, 4, 0, 4};
        case u'-': return {0, 0, 0, 31, 0, 0, 0};
        case u'_': return {0, 0, 0, 0, 0, 0, 31};
        case u'/': return {1, 2, 2, 4, 8, 8, 16};
        case u'+': return {0, 4, 4, 31, 4, 4, 0};
        case u'=': return {0, 31, 0, 31, 0, 0, 0};
        case u'(': return {2, 4, 8, 8, 8, 4, 2};
        case u')': return {8, 4, 2, 2, 2, 4, 8};
        default:
            throw std::runtime_error("unsupported fixed-font glyph");
    }
}

[[nodiscard]] Rect Intersect(const Rect a, const Rect b) {
    return {std::max(a.left, b.left), std::max(a.top, b.top),
            std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
}

[[nodiscard]] std::int32_t ScaleRounded(const std::int32_t value,
                                        const std::int32_t numerator,
                                        const std::int32_t denominator) {
    if (denominator <= 0) throw std::runtime_error("invalid UI bitmap scale");
    return static_cast<std::int32_t>(
        (static_cast<std::int64_t>(value) * numerator + denominator / 2) /
        denominator);
}

[[nodiscard]] Rect ImageDestination(const UiNode& node,
                                    const UiBitmap& bitmap) {
    const Rect content{node.screen_frame.left + node.padding.left,
                       node.screen_frame.top + node.padding.top,
                       node.screen_frame.right - node.padding.right,
                       node.screen_frame.bottom - node.padding.bottom};
    const auto content_width = std::max(0, content.right - content.left);
    const auto content_height = std::max(0, content.bottom - content.top);
    if (content_width == 0 || content_height == 0 || bitmap.width <= 0 ||
        bitmap.height <= 0) {
        return {content.left, content.top, content.left, content.top};
    }
    std::int32_t width = bitmap.width;
    std::int32_t height = bitmap.height;
    if (node.image_scale_type == ImageScaleType::FitXy) {
        width = content_width;
        height = content_height;
    } else if (node.image_scale_type == ImageScaleType::FitCenter ||
               (node.image_scale_type == ImageScaleType::CenterInside &&
                (bitmap.width > content_width ||
                 bitmap.height > content_height))) {
        if (static_cast<std::int64_t>(content_width) * bitmap.height <=
            static_cast<std::int64_t>(content_height) * bitmap.width) {
            width = content_width;
            height = ScaleRounded(bitmap.height, content_width, bitmap.width);
        } else {
            height = content_height;
            width = ScaleRounded(bitmap.width, content_height, bitmap.height);
        }
    } else if (node.image_scale_type == ImageScaleType::CenterCrop) {
        if (static_cast<std::int64_t>(content_width) * bitmap.height >=
            static_cast<std::int64_t>(content_height) * bitmap.width) {
            width = content_width;
            height = ScaleRounded(bitmap.height, content_width, bitmap.width);
        } else {
            height = content_height;
            width = ScaleRounded(bitmap.width, content_height, bitmap.height);
        }
    }
    // API19 ImageView adds 0.5 then truncates the centered translation.
    const auto left = content.left + (content_width - width + 1) / 2;
    const auto top = content.top + (content_height - height + 1) / 2;
    return {left, top, left + width, top + height};
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
            out.emplace_back(DrawBitmap{ImageDestination(node, bitmap),
                                        found->second, node.alpha});
        }
    }
    if (!node.text.empty()) {
        const auto metrics = MeasureFixedText(node.text, node.text_size_px);
        const auto content = Rect{
            node.screen_frame.left + node.padding.left,
            node.screen_frame.top + node.padding.top,
            node.screen_frame.right - node.padding.right,
            node.screen_frame.bottom - node.padding.bottom};
        std::int32_t x = content.left;
        std::int32_t y = content.top;
        if ((node.gravity & 0x07U) == 0x01U) {
            x += (content.right - content.left - metrics.width) / 2;
        } else if ((node.gravity & 0x07U) == 0x05U) {
            x = content.right - metrics.width;
        }
        if ((node.gravity & 0x70U) == 0x10U) {
            y += (content.bottom - content.top - metrics.height) / 2;
        } else if ((node.gravity & 0x70U) == 0x50U) {
            y = content.bottom - metrics.height;
        }
        out.emplace_back(DrawText{x, y, node.text, node.text_color,
                                  node.text_size_px, node.alpha});
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

FixedTextMetrics MeasureFixedText(const std::u16string_view text,
                                  const float size_px) {
    if (!std::isfinite(size_px) || size_px < 1.0F || size_px > 128.0F) {
        throw std::runtime_error("fixed-font text size is outside 1..128 px");
    }
    if (text.size() > 1024U) {
        throw std::runtime_error("fixed-font text exceeds 1024 code units");
    }
    for (const auto unit : text) {
        if (unit == u'\n' || unit == u'\r') {
            throw std::runtime_error("multiline fixed-font text is unsupported");
        }
        static_cast<void>(GlyphRows(unit));
    }
    const auto scale = std::max(1, static_cast<std::int32_t>(
                                       std::lround(size_px / 8.0F)));
    const auto cells = static_cast<std::int64_t>(text.size());
    const auto width = text.empty() ? 0 : (cells * 6 - 1) * scale;
    if (width > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("fixed-font measured width overflows");
    }
    return {static_cast<std::int32_t>(width), text.empty() ? 0 : 7 * scale,
            scale};
}

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
            const auto destination_width =
                bitmap->rect.right - bitmap->rect.left;
            const auto destination_height =
                bitmap->rect.bottom - bitmap->rect.top;
            if (destination_width <= 0 || destination_height <= 0) continue;
            PaintRect(frame, bitmap->rect, clips.back(),
                      [bitmap, destination_width, destination_height](
                          const auto x, const auto y, auto* dst) {
                const auto bx = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(x - bitmap->rect.left) *
                    bitmap->bitmap->width / destination_width);
                const auto by = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(y - bitmap->rect.top) *
                    bitmap->bitmap->height / destination_height);
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
        } else if (const auto* text = std::get_if<DrawText>(&command)) {
            const auto metrics = MeasureFixedText(text->text, text->size_px);
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(text->rgba & 0xffU) *
                     AlphaByte(text->alpha) +
                 127U) /
                255U);
            for (std::size_t index = 0; index < text->text.size(); ++index) {
                const auto glyph = GlyphRows(text->text[index]);
                const auto origin_x =
                    text->x + static_cast<std::int32_t>(index) *
                                  6 * metrics.scale;
                for (std::int32_t row = 0; row < 7; ++row) {
                    for (std::int32_t column = 0; column < 5; ++column) {
                        if ((glyph[static_cast<std::size_t>(row)] &
                             (1U << static_cast<unsigned>(4 - column))) == 0) {
                            continue;
                        }
                        const Rect pixel{
                            origin_x + column * metrics.scale,
                            text->y + row * metrics.scale,
                            origin_x + (column + 1) * metrics.scale,
                            text->y + (row + 1) * metrics.scale};
                        PaintRect(frame, pixel, clips.back(),
                                  [text, alpha](auto, auto, auto* dst) {
                                      Blend(dst,
                                            static_cast<std::uint8_t>(
                                                text->rgba >> 24U),
                                            static_cast<std::uint8_t>(
                                                text->rgba >> 16U),
                                            static_cast<std::uint8_t>(
                                                text->rgba >> 8U),
                                            alpha);
                                  });
                    }
                }
            }
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
        tree.ClearDrawDirty();
    }
    return cached_;
}

}  // namespace ogplay::runtime::ui
