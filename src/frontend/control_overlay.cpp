#include "ogplay/frontend/control_overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace ogplay::frontend {
namespace {

using Rows = std::array<std::uint8_t, 7>;
// 5x7 ASCII glyphs; bit 4 is the left column. \x01..\x04 are up, down, left, right arrows.
constexpr std::array<std::pair<char, Rows>, 54> kGlyphs{{
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}}, {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'\\', {0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00}}, {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}}, {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}}, {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}}, {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'\x01', {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04}}, {'\x02', {0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04}},
    {'\x03', {0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00}}, {'\x04', {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00}},
}};

using Color = std::array<std::uint8_t, 3>;
constexpr Color kZone{124, 156, 255};
constexpr Color kText{255, 255, 255};
constexpr Color kShade{0, 0, 0};

struct Canvas final {
    std::span<std::uint8_t> pixels;
    std::uint32_t width, height;

    void Blend(const std::int64_t x, const std::int64_t y, const Color color, const std::uint8_t alpha) const {
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width) || y >= static_cast<std::int64_t>(height)) return;
        const auto offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4U;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto mixed = static_cast<unsigned>(pixels[offset + channel]) * (255U - alpha) +
                               static_cast<unsigned>(color[channel]) * alpha + 127U;
            pixels[offset + channel] = static_cast<std::uint8_t>(mixed / 255U);
        }
    }
    void Fill(const std::int64_t x, const std::int64_t y, const std::int64_t w, const std::int64_t h,
              const Color color, const std::uint8_t alpha) const {
        for (auto row = std::max<std::int64_t>(y, 0); row < std::min<std::int64_t>(y + h, static_cast<std::int64_t>(height)); ++row)
            for (auto column = std::max<std::int64_t>(x, 0); column < std::min<std::int64_t>(x + w, static_cast<std::int64_t>(width)); ++column)
                Blend(column, row, color, alpha);
    }
};

[[nodiscard]] const Rows* Glyph(const char character) {
    const auto upper = character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : character;
    for (const auto& [name, rows] : kGlyphs) if (name == upper) return &rows;
    return nullptr;
}

[[nodiscard]] std::int64_t TextWidth(const std::string_view text, const std::int64_t scale) {
    return text.empty() ? 0 : static_cast<std::int64_t>(text.size()) * 6 * scale - scale;
}

// Centered text on a dark plate so labels stay readable over any game art.
void Label(const Canvas& canvas, const std::string_view text, const float center_x, const float center_y,
           const std::int64_t scale) {
    if (text.empty()) return;
    const auto width = TextWidth(text, scale), height = 7 * scale;
    const auto left = static_cast<std::int64_t>(std::lround(center_x)) - width / 2;
    const auto top = static_cast<std::int64_t>(std::lround(center_y)) - height / 2;
    canvas.Fill(left - 2 * scale, top - 2 * scale, width + 4 * scale, height + 4 * scale, kShade, 150);
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto* rows = Glyph(text[index]);
        if (rows == nullptr) continue;
        const auto x0 = left + static_cast<std::int64_t>(index) * 6 * scale;
        for (std::int64_t row = 0; row < 7; ++row)
            for (std::int64_t column = 0; column < 5; ++column)
                if (((*rows)[static_cast<std::size_t>(row)] >> (4 - column)) & 1U)
                    canvas.Fill(x0 + column * scale, top + row * scale, scale, scale, kText, 255);
    }
}

void Zone(const Canvas& canvas, const input::ResolvedZoneView& zone, const std::int64_t stroke) {
    const auto left = static_cast<std::int64_t>(std::floor(zone.center_x - zone.half_width));
    const auto right = static_cast<std::int64_t>(std::ceil(zone.center_x + zone.half_width));
    const auto top = static_cast<std::int64_t>(std::floor(zone.center_y - zone.half_height));
    const auto bottom = static_cast<std::int64_t>(std::ceil(zone.center_y + zone.half_height));
    for (auto y = std::max<std::int64_t>(top, 0); y < std::min<std::int64_t>(bottom, static_cast<std::int64_t>(canvas.height)); ++y) {
        for (auto x = std::max<std::int64_t>(left, 0); x < std::min<std::int64_t>(right, static_cast<std::int64_t>(canvas.width)); ++x) {
            const float dx = static_cast<float>(x) + 0.5F - zone.center_x;
            const float dy = static_cast<float>(y) + 0.5F - zone.center_y;
            float inside{};  // distance to the edge, positive inside
            if (zone.circle) {
                inside = zone.half_width - std::hypot(dx, dy);
            } else {
                inside = std::min(zone.half_width - std::abs(dx), zone.half_height - std::abs(dy));
            }
            if (inside < 0) continue;
            canvas.Blend(x, y, kZone, inside < static_cast<float>(stroke) ? 220 : 56);
        }
    }
}

}  // namespace

std::string HintLabel(const input::InputSource& source) {
    const auto text = input::FormatInputSource(source);
    const auto name = std::string_view(text).substr(text.find(':') + 1);
    if (source.kind == input::SourceKind::mouse_button) {
        if (name == "left") return "LMB";
        if (name == "right") return "RMB";
        if (name == "middle") return "MMB";
        std::string upper(name);
        std::ranges::transform(upper, upper.begin(), [](const char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
        return upper;
    }
    constexpr std::array<std::pair<std::string_view, std::string_view>, 31> kShort{{
        {"up", "\x01"}, {"down", "\x02"}, {"left", "\x03"}, {"right", "\x04"}, {"space", "SPC"},
        {"enter", "ENT"}, {"escape", "ESC"}, {"backspace", "BS"}, {"tab", "TAB"}, {"caps_lock", "CAP"},
        {"left_shift", "LSH"}, {"right_shift", "RSH"}, {"left_ctrl", "LCT"}, {"right_ctrl", "RCT"},
        {"left_alt", "LAL"}, {"right_alt", "RAL"}, {"page_up", "PGU"}, {"page_down", "PGD"},
        {"home", "HOM"}, {"end", "END"}, {"insert", "INS"}, {"delete", "DEL"}, {"minus", "-"},
        {"equals", "="}, {"left_bracket", "["}, {"right_bracket", "]"}, {"backslash", "\\"},
        {"semicolon", ";"}, {"apostrophe", "'"}, {"grave", "`"}, {"comma", ","},
    }};
    for (const auto& [key, label] : kShort) if (key == name) return std::string(label);
    if (name == "period") return ".";
    if (name == "slash") return "/";
    std::string upper(name);
    std::ranges::transform(upper, upper.begin(), [](const char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
    return upper;
}

void DrawControlHints(const std::span<std::uint8_t> rgba8, const std::uint32_t width, const std::uint32_t height,
                      const std::span<const input::ControlHint> hints, const std::string_view mode) {
    if (rgba8.size() < static_cast<std::size_t>(width) * height * 4U) return;
    const Canvas canvas{rgba8, width, height};
    const std::int64_t scale = std::max<std::int64_t>(1, static_cast<std::int64_t>(height) / 280);
    for (const auto& hint : hints) Zone(canvas, hint.zone, scale + 1);
    for (const auto& hint : hints) {
        const auto& zone = hint.zone;
        if (hint.stick && hint.sources.size() == 4) {
            constexpr std::array<std::pair<float, float>, 4> kDirections{{{0, -1}, {-1, 0}, {0, 1}, {1, 0}}};
            for (std::size_t i = 0; i < 4; ++i)
                Label(canvas, HintLabel(hint.sources[i]), zone.center_x + kDirections[i].first * zone.half_width * 0.6F,
                      zone.center_y + kDirections[i].second * zone.half_height * 0.6F, scale);
            continue;
        }
        std::string text;
        for (const auto& source : hint.sources) text += (text.empty() ? "" : "/") + HintLabel(source);
        Label(canvas, text, zone.center_x, zone.center_y, scale);
    }
    std::string banner = "F1 HIDE KEYS  F2 EDIT KEYS";
    if (mode != input::kDefaultMode) banner += "  MODE " + std::string(mode);
    Label(canvas, banner, static_cast<float>(TextWidth(banner, scale)) / 2.0F + 6.0F * static_cast<float>(scale),
          8.0F * static_cast<float>(scale), scale);
}

}  // namespace ogplay::frontend
