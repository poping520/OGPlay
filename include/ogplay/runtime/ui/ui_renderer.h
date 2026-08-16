#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ogplay/runtime/ui/ui_tree.h"

namespace ogplay::runtime::ui {

struct UiBitmap final {
    std::int32_t width{};
    std::int32_t height{};
    std::vector<std::uint8_t> rgba8;
};

using UiBitmapCache =
    std::unordered_map<std::uint32_t, std::shared_ptr<const UiBitmap>>;

struct DrawSolidRect final {
    Rect rect;
    std::uint32_t rgba{};
    float alpha{1.0F};
};

struct DrawBitmap final {
    Rect rect;
    std::shared_ptr<const UiBitmap> bitmap;
    float alpha{1.0F};
};

struct DrawText final {
    std::int32_t x{};
    std::int32_t y{};
    std::u16string text;
    std::uint32_t rgba{};
    float size_px{8.0F};
    float alpha{1.0F};
};

struct PushClip final { Rect rect; };
struct PopClip final {};
using UiDrawCommand =
    std::variant<DrawSolidRect, DrawBitmap, DrawText, PushClip, PopClip>;
using UiRenderList = std::vector<UiDrawCommand>;

struct UiOverlayFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> rgba8;
};

struct FixedTextMetrics final {
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t scale{};

    constexpr auto operator<=>(const FixedTextMetrics&) const = default;
};

// Deterministic built-in 5x7 uppercase/digit font. Lowercase folds to
// uppercase; unsupported glyphs, multiline text and invalid sizes fail.
[[nodiscard]] FixedTextMetrics MeasureFixedText(std::u16string_view text,
                                                float size_px);

[[nodiscard]] UiRenderList BuildUiRenderList(const UiTree& tree,
                                             const UiBitmapCache& bitmaps);
[[nodiscard]] UiOverlayFrame RasterizeUiOverlay(
    const UiRenderList& commands, UiMetrics metrics);

class UiOverlayRenderer final {
public:
    [[nodiscard]] const UiOverlayFrame& Render(UiTree& tree,
                                               const UiBitmapCache& bitmaps,
                                               UiMetrics metrics);
    [[nodiscard]] std::uint64_t BuildCount() const { return build_count_; }

private:
    UiOverlayFrame cached_;
    std::uint32_t generation_{};
    UiMetrics metrics_{};
    std::uint64_t build_count_{};
};

}  // namespace ogplay::runtime::ui
