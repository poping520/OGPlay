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
        case u'a': return {0, 0, 14, 1, 15, 17, 15};
        case u'b': return {16, 16, 30, 17, 17, 17, 30};
        case u'c': return {0, 0, 14, 16, 16, 17, 14};
        case u'd': return {1, 1, 15, 17, 17, 17, 15};
        case u'e': return {0, 0, 14, 17, 31, 16, 14};
        case u'f': return {6, 8, 8, 30, 8, 8, 8};
        case u'g': return {0, 0, 15, 17, 15, 1, 14};
        case u'h': return {16, 16, 30, 17, 17, 17, 17};
        case u'i': return {4, 0, 12, 4, 4, 4, 14};
        case u'j': return {2, 0, 6, 2, 2, 18, 12};
        case u'k': return {16, 16, 18, 20, 24, 20, 18};
        case u'l': return {12, 4, 4, 4, 4, 4, 14};
        case u'm': return {0, 0, 26, 21, 21, 21, 21};
        case u'n': return {0, 0, 30, 17, 17, 17, 17};
        case u'o': return {0, 0, 14, 17, 17, 17, 14};
        case u'p': return {0, 0, 30, 17, 30, 16, 16};
        case u'q': return {0, 0, 15, 17, 15, 1, 1};
        case u'r': return {0, 0, 22, 25, 16, 16, 16};
        case u's': return {0, 0, 15, 16, 14, 1, 30};
        case u't': return {8, 8, 30, 8, 8, 9, 6};
        case u'u': return {0, 0, 17, 17, 17, 19, 13};
        case u'v': return {0, 0, 17, 17, 17, 10, 4};
        case u'w': return {0, 0, 17, 17, 21, 21, 10};
        case u'x': return {0, 0, 17, 10, 4, 10, 17};
        case u'y': return {0, 0, 17, 17, 15, 1, 14};
        case u'z': return {0, 0, 31, 2, 4, 8, 31};
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

[[nodiscard]] Rect ContentBox(const UiNode& node) {
    return {node.screen_frame.left + node.padding.left,
            node.screen_frame.top + node.padding.top,
            node.screen_frame.right - node.padding.right,
            node.screen_frame.bottom - node.padding.bottom};
}

[[nodiscard]] bool HasPadding(const Insets& padding) {
    return padding.left != 0 || padding.top != 0 || padding.right != 0 ||
           padding.bottom != 0;
}

void AppendNode(const UiTree& tree, const UiNodeId id,
                const UiBitmapCache& bitmaps, UiRenderList& out,
                const bool parent_clips_children) {
    const auto& node = *tree.Get(id);
    if (node.visibility != Visibility::Visible) return;
    // Parent clipChildren clips this view to its bounds (AOSP View.draw).
    const bool clip_to_bounds = parent_clips_children;
    if (clip_to_bounds) {
        out.emplace_back(PushClip{node.screen_frame});
    }
    if (node.background_color.has_value()) {
        out.emplace_back(DrawSolidRect{node.screen_frame,
                                       *node.background_color,
                                       node.alpha * node.background_alpha});
    }
    if (node.background_resource_id != 0) {
        const auto found = bitmaps.find(node.background_resource_id);
        if (found != bitmaps.end() && found->second != nullptr) {
            out.emplace_back(DrawBitmap{node.screen_frame, found->second,
                                        node.alpha * node.background_alpha});
        }
    }
    if (node.image_resource_id != 0) {
        const auto found = bitmaps.find(node.image_resource_id);
        if (found != bitmaps.end() && found->second != nullptr) {
            const auto& bitmap = *found->second;
            out.emplace_back(DrawBitmap{ImageDestination(node, bitmap),
                                        found->second, node.alpha});
        }
    }
    if (!node.text.empty() ||
        std::any_of(node.compound_drawables.begin(),
                    node.compound_drawables.end(),
                    [](const CompoundDrawable& drawable) {
                        return drawable.resource_id != 0;
                    })) {
        auto rendered_text = node.text;
        const auto content = Rect{
            node.screen_frame.left + node.padding.left,
            node.screen_frame.top + node.padding.top,
            node.screen_frame.right - node.padding.right,
            node.screen_frame.bottom - node.padding.bottom};
        // API19 TextView: compound drawables sit on the content edges; the
        // text is laid out inside the remaining band.
        const auto& compound = node.compound_drawables;
        const Rect text_content{content.left + compound[0].width,
                                content.top + compound[1].height,
                                content.right - compound[2].width,
                                content.bottom - compound[3].height};
        if (node.max_lines > 1 && !rendered_text.empty()) {
            rendered_text = WrapFixedText(
                rendered_text, node.text_size_px, node.text_style,
                text_content.right - text_content.left, node.max_lines);
        }
        const auto metrics = MeasureFixedText(rendered_text, node.text_size_px,
                                              node.text_style);
        const auto drawable_rect =
            [&text_content](const std::size_t index, const Rect content_box,
                        const std::int32_t width, const std::int32_t height) {
                if (width <= 0 || height <= 0) {
                    return Rect{content_box.left, content_box.top,
                                content_box.left, content_box.top};
                }
                const auto box_width = text_content.right - text_content.left;
                const auto box_height = text_content.bottom - text_content.top;
                switch (index) {
                    case 0:  // left, vertically centered
                        return Rect{content_box.left,
                                    text_content.top + (box_height - height) / 2,
                                    content_box.left + width,
                                    text_content.top + (box_height - height) / 2 + height};
                    case 1:  // top, horizontally centered
                        return Rect{text_content.left + (box_width - width) / 2,
                                    content_box.top,
                                    text_content.left + (box_width - width) / 2 + width,
                                    content_box.top + height};
                    case 2:  // right, vertically centered
                        return Rect{content_box.right - width,
                                    text_content.top + (box_height - height) / 2,
                                    content_box.right,
                                    text_content.top + (box_height - height) / 2 + height};
                    default:  // bottom, horizontally centered
                        return Rect{text_content.left + (box_width - width) / 2,
                                    content_box.bottom - height,
                                    text_content.left + (box_width - width) / 2 + width,
                                    content_box.bottom};
                }
            };
        for (std::size_t index = 0; index < compound.size(); ++index) {
            const auto& drawable = compound[index];
            if (drawable.resource_id == 0) continue;
            const auto found = bitmaps.find(drawable.resource_id);
            if (found == bitmaps.end() || found->second == nullptr) continue;
            out.emplace_back(DrawBitmap{
                drawable_rect(index, content, found->second->width,
                              found->second->height),
                found->second, node.alpha});
        }
        std::int32_t x = text_content.left;
        std::int32_t y = text_content.top;
        if ((node.gravity & 0x07U) == 0x01U) {
            x += (text_content.right - text_content.left - metrics.width) / 2;
        } else if ((node.gravity & 0x07U) == 0x05U) {
            x = text_content.right - metrics.width;
        }
        if ((node.gravity & 0x70U) == 0x10U) {
            y += (text_content.bottom - text_content.top - metrics.height) / 2;
        } else if ((node.gravity & 0x70U) == 0x50U) {
            y = text_content.bottom - metrics.height;
        }
        if (!rendered_text.empty()) {
            out.emplace_back(DrawText{x, y, rendered_text, node.text_color,
                                      node.text_size_px, node.alpha, node.text_style});
        }
    }
    // clipToPadding clips descendants to the padding box (AOSP dispatchDraw),
    // not this node's background or onDraw content.
    const bool clip_children_to_padding =
        node.clip_to_padding && HasPadding(node.padding);
    if (clip_children_to_padding) {
        out.emplace_back(PushClip{ContentBox(node)});
    }
    for (const auto child : node.children) {
        AppendNode(tree, child, bitmaps, out, node.clip_children);
    }
    if (clip_children_to_padding) {
        out.emplace_back(PopClip{});
    }
    if (clip_to_bounds) {
        out.emplace_back(PopClip{});
    }
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
                                  const float size_px, const std::uint32_t style) {
    if (style > 3U) throw std::runtime_error("invalid built-in font style");
    if (!std::isfinite(size_px) || size_px < 1.0F || size_px > 128.0F) {
        throw std::runtime_error("fixed-font text size is outside 1..128 px");
    }
    if (text.size() > 1024U) {
        throw std::runtime_error("fixed-font text exceeds 1024 code units");
    }
    for (const auto unit : text) {
        if (unit == u'\n' || unit == u'\r') continue;
        static_cast<void>(GlyphRows(unit));
    }
    const auto scale = std::max(1, static_cast<std::int32_t>(
                                       std::lround(size_px / 8.0F)));
    const auto extra = ((style & 1U) ? 1 : 0) + ((style & 2U) ? 2 : 0);
    std::int64_t columns = 0;
    std::int64_t max_columns = 0;
    std::int32_t lines = text.empty() ? 0 : 1;
    for (const auto unit : text) {
        if (unit == u'\n') {
            max_columns = std::max(max_columns, columns);
            columns = 0;
            ++lines;
        } else if (unit != u'\r') {
            ++columns;
        }
    }
    max_columns = std::max(max_columns, columns);
    const auto width = max_columns == 0 ? 0 :
        (max_columns * (6 + extra) - 1) * scale;
    if (width > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("fixed-font measured width overflows");
    }
    return {static_cast<std::int32_t>(width),
            lines == 0 ? 0 : (lines * 8 - 1) * scale,
            scale};
}

std::u16string WrapFixedText(const std::u16string_view text,
                             const float size_px, const std::uint32_t style,
                             const std::int32_t width_px,
                             const std::int32_t max_lines) {
    if (text.empty() || max_lines <= 1 || width_px <= 0) {
        return std::u16string(text);
    }
    const auto probe = MeasureFixedText(u"M", size_px, style);
    const auto cell = std::max(1, probe.width + probe.scale);
    const auto columns = std::max(1, width_px / cell);
    std::u16string result;
    result.reserve(text.size() + text.size() /
                                   static_cast<std::size_t>(columns));
    std::int32_t line = 1;
    std::int32_t column = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == u'\n') {
            result.push_back(text[index++]);
            column = 0;
            ++line;
            continue;
        }
        if (text[index] == u' ') {
            if (column > 0) {
                result.push_back(u' ');
                ++column;
            }
            ++index;
            continue;
        }
        auto end = index;
        while (end < text.size() && text[end] != u' ' &&
               text[end] != u'\n') {
            ++end;
        }
        const auto length = static_cast<std::int32_t>(end - index);
        if (column > 0 && column + length > columns && line < max_lines) {
            if (!result.empty() && result.back() == u' ') result.pop_back();
            result.push_back(u'\n');
            column = 0;
            ++line;
        }
        for (; index < end; ++index) {
            if (column == columns && line < max_lines) {
                result.push_back(u'\n');
                column = 0;
                ++line;
            }
            result.push_back(text[index]);
            ++column;
        }
    }
    return result;
}

UiRenderList BuildUiRenderList(const UiTree& tree,
                               const UiBitmapCache& bitmaps) {
    UiRenderList commands;
    // The window clips the content root to its bounds, matching default
    // parent clipChildren=true.
    AppendNode(tree, tree.Root(), bitmaps, commands, true);
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
                const auto map_axis = [](const std::int32_t position,
                                         const std::int32_t destination,
                                         const std::int32_t source,
                                         const std::array<std::int32_t, 2> divs) {
                    if (divs[0] < 0 || divs[1] <= divs[0] ||
                        divs[1] > source) {
                        return static_cast<std::int32_t>(
                            static_cast<std::int64_t>(position) * source /
                            destination);
                    }
                    const auto leading = divs[0];
                    const auto trailing = source - divs[1];
                    if (position < leading) return position;
                    if (position >= destination - trailing) {
                        return source - (destination - position);
                    }
                    const auto stretch_destination = std::max(
                        1, destination - leading - trailing);
                    return divs[0] + static_cast<std::int32_t>(
                        static_cast<std::int64_t>(position - leading) *
                        (divs[1] - divs[0]) / stretch_destination);
                };
                const auto bx = bitmap->bitmap->nine_patch
                    ? map_axis(x - bitmap->rect.left, destination_width,
                               bitmap->bitmap->width,
                               bitmap->bitmap->stretch_x)
                    : static_cast<std::int32_t>(
                          static_cast<std::int64_t>(x - bitmap->rect.left) *
                          bitmap->bitmap->width / destination_width);
                const auto by = bitmap->bitmap->nine_patch
                    ? map_axis(y - bitmap->rect.top, destination_height,
                               bitmap->bitmap->height,
                               bitmap->bitmap->stretch_y)
                    : static_cast<std::int32_t>(
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
            const auto text_metrics = MeasureFixedText(text->text, text->size_px, text->style);
            const int bold = (text->style & 1U) ? 1 : 0;
            const int italic = (text->style & 2U) ? 2 : 0;
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(text->rgba & 0xffU) *
                     AlphaByte(text->alpha) +
                 127U) /
                255U);
            std::int32_t column_index = 0;
            std::int32_t line_index = 0;
            for (std::size_t index = 0; index < text->text.size(); ++index) {
                if (text->text[index] == u'\n') {
                    column_index = 0;
                    ++line_index;
                    continue;
                }
                if (text->text[index] == u'\r') continue;
                const auto glyph = GlyphRows(text->text[index]);
                const auto origin_x =
                    text->x + column_index++ *
                                  (6 + bold + italic) * text_metrics.scale;
                for (std::int32_t row = 0; row < 7; ++row) {
                    const auto row_bits = static_cast<unsigned>(glyph[static_cast<std::size_t>(row)]);
                    const auto styled_bits = bold ? (row_bits << 1U) | row_bits : row_bits;
                    for (std::int32_t column = 0; column < 5 + bold; ++column) {
                        if ((styled_bits &
                             (1U << static_cast<unsigned>(4 + bold - column))) == 0) {
                            continue;
                        }
                        const auto skew = italic ? (6 - row) / 3 : 0;
                        const Rect pixel{
                            origin_x + (column + skew) * text_metrics.scale,
                            text->y + (line_index * 8 + row) * text_metrics.scale,
                            origin_x + (column + skew + 1) * text_metrics.scale,
                            text->y + (line_index * 8 + row + 1) * text_metrics.scale};
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
