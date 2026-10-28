#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::runtime::ui {
class UiFallbackFont;

struct Insets final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
    constexpr auto operator<=>(const Insets&) const = default;
};

struct Size final {
    std::int32_t width{};
    std::int32_t height{};
    constexpr auto operator<=>(const Size&) const = default;
};

enum class MeasureMode : std::uint8_t {
    Exactly,
    AtMost,
    Unspecified,
};

struct MeasureSpec final {
    MeasureMode mode{MeasureMode::Unspecified};
    std::int32_t size{};
};

struct FixedTextMetrics final {
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t scale{};

    constexpr auto operator<=>(const FixedTextMetrics&) const = default;
};

// Shared coverage check. Throws with the codepoint and the unsupported requirement.
void RequireTextGlyph(char16_t unit, const UiFallbackFont* fallback = nullptr);

// Deterministic built-in 5x7 ASCII font, with optional trusted API19 BMP
// fallback. Unsupported glyphs and invalid sizes fail explicitly.
[[nodiscard]] FixedTextMetrics MeasureFixedText(std::u16string_view text, float size_px, std::uint32_t style = 0,
                                                const UiFallbackFont* fallback = nullptr);
[[nodiscard]] std::u16string WrapFixedText(std::u16string_view text, float size_px, std::uint32_t style,
                                           std::int32_t width_px, std::int32_t max_lines,
                                           const UiFallbackFont* fallback = nullptr);

// These are the bounded fixed-font service's lines, not Android Layout.
struct TextLinePlan final {
    std::u16string text;
    FixedTextMetrics metrics;
    float size_px{8.0F};
    std::uint32_t style{};
    std::shared_ptr<const UiFallbackFont> fallback_font;
};

enum class TextWrapWidth : std::uint8_t { CompoundPadding, LegacyPadding };

struct ImageLeafInput final {
    Size intrinsic;
    Insets padding;
    Size minimum;
};

struct TextLeafInput final {
    std::u16string text;
    float size_px{8.0F};
    std::uint32_t style{};
    std::shared_ptr<const UiFallbackFont> fallback_font;
    std::int32_t max_lines{1};
    std::array<Size, 4> compound{};  // left, top, right, bottom
    Insets padding;
    Size minimum;
    Size intrinsic;
    TextWrapWidth wrap_width{TextWrapWidth::CompoundPadding};
    // API19 TextView chooses width before layout and clamps without TOO_SMALL.
    bool java_text_view{};
    bool single_line{};
};

struct LeafMeasureResult final {
    Size desired;
    Size constrained;
    // Per-axis Android TOO_SMALL bit for AT_MOST; Legacy ignores state.
    std::uint32_t width_state{};
    std::uint32_t height_state{};
    // Fixed-cell baseline for the bounded Java TextView service; absent for Legacy.
    std::optional<std::int32_t> baseline;
    std::shared_ptr<const TextLinePlan> line_plan;
};

[[nodiscard]] LeafMeasureResult MeasureTextLeaf(const TextLeafInput& input, MeasureSpec width, MeasureSpec height);
[[nodiscard]] LeafMeasureResult MeasureImageLeaf(const ImageLeafInput& input, MeasureSpec width, MeasureSpec height);
}  // namespace ogplay::runtime::ui
