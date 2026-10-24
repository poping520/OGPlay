#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ui_fixed_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ogplay::runtime::ui {
namespace {

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
                const bool parent_clips_children, UiRenderPlane plane) {
    const auto& node = *tree.Get(id);
    if (!node.window_visible || node.visibility != Visibility::Visible) return;
    // Parent clipChildren clips this view to its bounds (AOSP View.draw).
    const bool external = tree.GeometryOwner() == UiGeometryOwner::ExternalProjection;
    const bool clip_to_bounds = external || parent_clips_children;
    if (clip_to_bounds) {
        out.emplace_back(PushClip{external ? node.screen_clip : node.screen_frame});
    }
    if (node.surface_has_frame && node.surface_layer != SurfaceLayer::on_top) {
        out.emplace_back(ClearSurfaceRect{node.screen_frame});
    }
    if (node.background_color.has_value() && !(id == tree.Root() && plane == UiRenderPlane::modal)) {
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
    if (node.image_color) {
        const Rect content{node.screen_frame.left + node.padding.left,
                           node.screen_frame.top + node.padding.top,
                           node.screen_frame.right - node.padding.right,
                           node.screen_frame.bottom - node.padding.bottom};
        out.emplace_back(DrawSolidRect{content, *node.image_color, node.alpha * node.image_alpha});
    } else if (node.image_resource_id != 0 || node.image_bitmap) {
        const auto found = bitmaps.find(node.image_resource_id);
        const auto image = node.image_bitmap ? node.image_bitmap :
            found != bitmaps.end() ? found->second : nullptr;
        if (image) {
            const auto& bitmap = *image;
            out.emplace_back(DrawBitmap{ImageDestination(node, bitmap),
                                        image, node.alpha * node.image_alpha});
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
        if (!node.text_line_plan && node.max_lines > 1 && !rendered_text.empty()) {
            rendered_text = WrapFixedText(
                rendered_text, node.text_size_px, node.text_style,
                text_content.right - text_content.left, node.max_lines, node.fallback_font.get());
        }
        if (node.text_line_plan) rendered_text = node.text_line_plan->text;
        const auto metrics = node.text_line_plan ? node.text_line_plan->metrics
                                                 : MeasureFixedText(rendered_text, node.text_size_px, node.text_style,
                                                                    node.fallback_font.get());
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
            out.emplace_back(DrawText{x, y, rendered_text, node.text_color, node.text_size_px, node.alpha,
                                      node.text_style, node.fallback_font, node.text_line_plan});
        }
    }
    // clipToPadding clips descendants to the padding box (AOSP dispatchDraw),
    // not this node's background or onDraw content.
    const bool clip_children_to_padding = !external && node.clip_to_padding && HasPadding(node.padding);
    if (clip_children_to_padding) {
        out.emplace_back(PushClip{ContentBox(node)});
    }
    for (const auto child : node.children) {
        if (id == tree.Root() && plane != UiRenderPlane::all &&
            tree.Get(child)->modal_layer != (plane == UiRenderPlane::modal)) continue;
        AppendNode(tree, child, bitmaps, out, node.clip_children, plane);
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
            if (frame.rgba8[offset + 3U] != 0) frame.fully_transparent = false;
        }
    }
}

}  // namespace

UiRenderList BuildUiRenderList(const UiTree& tree,
                               const UiBitmapCache& bitmaps, UiRenderPlane plane) {
    if (tree.GeometryOwner() == UiGeometryOwner::ExternalProjection && !tree.IsProjectionSnapshot())
        throw std::runtime_error("UI rendering requires a complete projection snapshot");
    UiRenderList commands;
    // The window clips the content root to its bounds, matching default
    // parent clipChildren=true.
    AppendNode(tree, tree.Root(), bitmaps, commands, true, plane);
    return commands;
}

UiOverlayFrame RasterizeUiOverlay(const UiRenderList& commands,
                                  const UiMetrics metrics) {
    if (metrics.width < 0 || metrics.height < 0) {
        throw std::runtime_error("UI overlay metrics must be non-negative");
    }
    UiOverlayFrame frame{.width = static_cast<std::uint32_t>(metrics.width),
                         .height = static_cast<std::uint32_t>(metrics.height),
                         .rgba8 = {}, .fully_transparent = true};
    frame.rgba8.resize(static_cast<std::size_t>(frame.width) * frame.height * 4U);
    std::vector<Rect> clips{{0, 0, metrics.width, metrics.height}};
    for (const auto& command : commands) {
        if (const auto* push = std::get_if<PushClip>(&command)) {
            clips.push_back(Intersect(clips.back(), push->rect));
        } else if (std::holds_alternative<PopClip>(command)) {
            if (clips.size() <= 1) throw std::runtime_error("UI clip stack underflow");
            clips.pop_back();
        } else if (const auto* clear = std::get_if<ClearSurfaceRect>(&command)) {
            PaintRect(frame, clear->rect, clips.back(), [](auto, auto, auto* dst) {
                std::fill_n(dst, 4, std::uint8_t{});
            });
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
            const auto& value = text->line_plan ? text->line_plan->text : text->text;
            const auto style = text->line_plan ? text->line_plan->style : text->style;
            const auto& fallback = text->line_plan ? text->line_plan->fallback_font : text->fallback_font;
            const auto text_metrics = text->line_plan ? text->line_plan->metrics
                                                      : MeasureFixedText(value, text->size_px, style, fallback.get());
            const int bold = (style & 1U) ? 1 : 0;
            const int italic = (style & 2U) ? 2 : 0;
            const auto alpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(text->rgba & 0xffU) *
                     AlphaByte(text->alpha) +
                 127U) /
                255U);
            std::int32_t column_index = 0;
            std::int32_t line_index = 0;
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (value[index] == u'\n') {
                    column_index = 0;
                    ++line_index;
                    continue;
                }
                if (value[index] == u'\r') continue;
                const auto unit = value[index];
                const auto origin_x = text->x + column_index * text_metrics.scale;
                column_index += (unit > 127 ? 8 : 6) + bold + italic;
                if (unit > 127 && fallback) {
                    const auto glyph = fallback->Glyph(unit, text_metrics.scale * 8 - 1);
                    for (std::int32_t row = 0; row < glyph->height; ++row) {
                        const auto skew = italic ? std::max(0, (glyph->height - row) / 3) : 0;
                        for (std::int32_t col = 0; col < glyph->width; ++col) {
                            const auto coverage = glyph->alpha[static_cast<std::size_t>(row) * static_cast<std::size_t>(glyph->width) +
                                                               static_cast<std::size_t>(col)];
                            if (!coverage) continue;
                            const auto x = origin_x + glyph->x + col + skew;
                            const auto y = text->y + line_index * 8 * text_metrics.scale + glyph->y + row;
                            const auto a = static_cast<std::uint8_t>((static_cast<unsigned>(alpha) * coverage + 127U) / 255U);
                            PaintRect(frame, Rect{x, y, x + 1 + bold, y + 1}, clips.back(),
                                [text, a](auto, auto, auto* dst) { Blend(dst,
                                    static_cast<std::uint8_t>(text->rgba >> 24U), static_cast<std::uint8_t>(text->rgba >> 16U),
                                    static_cast<std::uint8_t>(text->rgba >> 8U), a); });
                        }
                    }
                    continue;
                }
                const auto glyph = GlyphRows(value[index]);

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
    // Clear commands may erase earlier opaque pixels. Certify the final result.
    frame.fully_transparent = true;
    for (std::size_t offset = 3; offset < frame.rgba8.size(); offset += 4) {
        if (frame.rgba8[offset] != 0) { frame.fully_transparent = false; break; }
    }
    return frame;
}

const UiOverlayFrame& UiOverlayRenderer::Render(UiTree& tree,
                                                const UiBitmapCache& bitmaps,
                                                const UiMetrics metrics,
                                                UiRenderPlane plane, bool force_rebuild) {
    const auto* root = tree.Get(tree.Root());
    const bool rebuild = force_rebuild || plane_ != plane || generation_ != tree.Generation() ||
                         metrics_.width != metrics.width ||
                         metrics_.height != metrics.height || root->draw_dirty;
    if (rebuild) {
        cached_ = RasterizeUiOverlay(BuildUiRenderList(tree, bitmaps, plane), metrics);
        generation_ = tree.Generation();
        metrics_ = metrics;
        plane_ = plane;
        ++build_count_;
        tree.ClearDrawDirty();
    }
    return cached_;
}

}  // namespace ogplay::runtime::ui
