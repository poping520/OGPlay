#include "ogplay/runtime/ui/ui_leaf_measure.h"
#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ui_fixed_font.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace ogplay::runtime::ui {
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
    case u'\'': return {4, 4, 8, 0, 0, 0, 0};
    case u'"': return {10, 10, 0, 0, 0, 0, 0};
    case u'#': return {10, 31, 10, 10, 31, 10, 0};
    case u'$': return {4, 15, 20, 14, 5, 30, 4};
    case u'%': return {24, 25, 2, 4, 8, 19, 3};
    case u'&': return {12, 18, 20, 8, 21, 18, 13};
    case u'*': return {0, 21, 14, 31, 14, 21, 0};
    case u'<': return {2, 4, 8, 16, 8, 4, 2};
    case u'>': return {8, 4, 2, 1, 2, 4, 8};
    case u'@': return {14, 17, 23, 21, 23, 16, 14};
    case u'[': return {14, 8, 8, 8, 8, 8, 14};
    case u'\\': return {16, 8, 8, 4, 2, 2, 1};
    case u']': return {14, 2, 2, 2, 2, 2, 14};
    case u'^': return {4, 10, 17, 0, 0, 0, 0};
    case u'`': return {8, 4, 0, 0, 0, 0, 0};
    case u'{': return {2, 4, 4, 8, 4, 4, 2};
    case u'|': return {4, 4, 4, 4, 4, 4, 4};
    case u'}': return {8, 4, 4, 2, 4, 4, 8};
    case u'~': return {0, 0, 8, 21, 2, 0, 0};
    default: {
        std::ostringstream error;
        error << "U+" << std::uppercase << std::hex << std::setfill('0') << std::setw(4)
              << static_cast<unsigned>(value) << " is absent from 5x7 fixed font";
        throw std::runtime_error(error.str());
    }
    }
}

void RequireTextGlyph(const char16_t unit, const UiFallbackFont* fallback) {
    if (unit > 127 && fallback) static_cast<void>(fallback->SourceIndex(unit));
    else static_cast<void>(GlyphRows(unit));
}

FixedTextMetrics MeasureFixedText(const std::u16string_view text, const float size_px, const std::uint32_t style,
                                  const UiFallbackFont* fallback) {
    if (style > 3U) throw std::runtime_error("invalid built-in font style");
    if (!std::isfinite(size_px) || size_px < 1.0F || size_px > 128.0F) {
        throw std::runtime_error("fixed-font text size is outside 1..128 px");
    }
    if (text.size() > 1024U) {
        throw std::runtime_error("fixed-font text exceeds 1024 code units");
    }
    for (const auto unit : text) {
        if (unit == u'\n' || unit == u'\r') continue;
        RequireTextGlyph(unit, fallback);
    }
    const auto scale = std::max(1, static_cast<std::int32_t>(std::lround(size_px / 8.0F)));
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
            columns += (unit > 127 ? 8 : 6) + extra;
        }
    }
    max_columns = std::max(max_columns, columns);
    const auto width = max_columns == 0 ? 0 : (max_columns - 1) * scale;
    if (width > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("fixed-font measured width overflows");
    }
    return {static_cast<std::int32_t>(width), lines == 0 ? 0 : (lines * 8 - 1) * scale, scale};
}

std::u16string WrapFixedText(const std::u16string_view text, const float size_px, const std::uint32_t style,
                             const std::int32_t width_px, const std::int32_t max_lines,
                             const UiFallbackFont* fallback) {
    if (fallback && std::any_of(text.begin(), text.end(), [](const auto c) { return c > 127; })) {
        const auto metrics = MeasureFixedText(text, size_px, style, fallback);
        std::u16string result;
        std::int32_t x{}, line = 1;
        for (const auto unit : text) {
            if (unit == u'\r') continue;
            if (unit == u'\n') {
                result.push_back(unit);
                x = 0;
                ++line;
                continue;
            }
            const auto advance =
                ((unit > 127 ? 8 : 6) + ((style & 1U) ? 1 : 0) + ((style & 2U) ? 2 : 0)) * metrics.scale;
            if (width_px > 0 && x > 0 && x + advance > width_px && line < max_lines) {
                result.push_back(u'\n');
                x = 0;
                ++line;
            }
            result.push_back(unit);
            x += advance;
        }
        return result;
    }
    if (text.empty() || max_lines <= 1 || width_px <= 0) {
        return std::u16string(text);
    }
    const auto probe = MeasureFixedText(u"M", size_px, style);
    const auto cell = std::max(1, probe.width + probe.scale);
    const auto columns = std::max(1, width_px / cell);
    std::u16string result;
    result.reserve(text.size() + text.size() / static_cast<std::size_t>(columns));
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
        while (end < text.size() && text[end] != u' ' && text[end] != u'\n') {
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

namespace {
std::int32_t Constrain(std::int32_t desired, MeasureSpec spec) {
    const auto safe = std::max(0, desired);
    switch (spec.mode) {
    case MeasureMode::Exactly: return std::max(0, spec.size);
    case MeasureMode::AtMost: return std::min(safe, std::max(0, spec.size));
    case MeasureMode::Unspecified: return safe;
    }
    throw std::runtime_error("invalid UI measure mode");
}

LeafMeasureResult Resolve(Size desired, MeasureSpec width, MeasureSpec height) {
    LeafMeasureResult result;
    result.desired = desired;
    result.constrained = {Constrain(desired.width, width), Constrain(desired.height, height)};
    result.width_state =
        width.mode == MeasureMode::AtMost && desired.width > result.constrained.width ? 0x01000000U : 0;
    result.height_state =
        height.mode == MeasureMode::AtMost && desired.height > result.constrained.height ? 0x01000000U : 0;
    return result;
}
}  // namespace

LeafMeasureResult MeasureImageLeaf(const ImageLeafInput& input, MeasureSpec width, MeasureSpec height) {
    return Resolve({std::max(input.minimum.width, input.intrinsic.width + input.padding.left + input.padding.right),
                    std::max(input.minimum.height, input.intrinsic.height + input.padding.top + input.padding.bottom)},
                   width, height);
}

LeafMeasureResult MeasureTextLeaf(const TextLeafInput& input, MeasureSpec width, MeasureSpec height) {
    if (input.java_text_view) {
        const auto& c = input.compound;
        const auto raw = MeasureFixedText(input.text, input.size_px, input.style, input.fallback_font.get());
        const auto horizontal = input.padding.left + input.padding.right + c[0].width + c[2].width;
        const auto vertical = input.padding.top + input.padding.bottom + c[1].height + c[3].height;
        const auto desired_width =
            std::max(input.minimum.width, std::max({raw.width, c[1].width, c[3].width}) + horizontal);
        const auto actual_width = Constrain(desired_width, width);
        auto text = input.single_line
                        ? input.text
                        : WrapFixedText(input.text, input.size_px, input.style,
                                        std::max(1, actual_width - horizontal + raw.scale),
                                        std::numeric_limits<std::int32_t>::max(), input.fallback_font.get());
        // maxLines caps visible layout height, including explicit newlines.
        std::int32_t lines = 1;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == u'\n' && ++lines > input.max_lines) {
                text.resize(i);
                break;
            }
        }
        const auto metrics = MeasureFixedText(text, input.size_px, input.style, input.fallback_font.get());
        LeafMeasureResult result;
        result.desired = {desired_width, std::max(input.minimum.height,
                                                  std::max({metrics.height, c[0].height, c[2].height}) + vertical)};
        result.constrained = {actual_width, Constrain(result.desired.height, height)};
        // TextView.java:6278-6454 uses min, not resolveSizeAndState.
        result.baseline = input.padding.top + c[1].height + 7 * metrics.scale;
        result.line_plan = std::make_shared<const TextLinePlan>(
            TextLinePlan{text, metrics, input.size_px, input.style, input.fallback_font});
        return result;
    }
    auto result = MeasureImageLeaf({input.intrinsic, input.padding, input.minimum}, width, height);
    auto measured_text = input.text.empty() ? std::u16string(u"M") : input.text;
    const auto& c = input.compound;
    if (input.max_lines > 1 && width.mode != MeasureMode::Unspecified) {
        const auto compound_width = input.wrap_width == TextWrapWidth::CompoundPadding ? c[0].width + c[2].width : 0;
        const auto available = std::max(1, width.size - input.padding.left - input.padding.right - compound_width);
        measured_text = WrapFixedText(measured_text, input.size_px, input.style, available, input.max_lines,
                                      input.fallback_font.get());
    }
    const auto metrics = MeasureFixedText(measured_text, input.size_px, input.style, input.fallback_font.get());
    result =
        Resolve({std::max(result.desired.width, std::max({metrics.width, c[1].width, c[3].width}) + c[0].width +
                                                    c[2].width + input.padding.left + input.padding.right),
                 std::max(result.desired.height, std::max({metrics.height, c[0].height, c[2].height}) + c[1].height +
                                                     c[3].height + input.padding.top + input.padding.bottom)},
                width, height);
    // Empty text reserves the historical M cell, but rasterizes no placeholder.
    const auto rendered = input.text.empty() ? std::u16string{} : measured_text;
    result.line_plan = std::make_shared<const TextLinePlan>(
        TextLinePlan{rendered, MeasureFixedText(rendered, input.size_px, input.style, input.fallback_font.get()),
                     input.size_px, input.style, input.fallback_font});
    return result;
}
}  // namespace ogplay::runtime::ui
