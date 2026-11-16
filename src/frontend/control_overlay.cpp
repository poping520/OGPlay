#include "ogplay/frontend/control_overlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <utility>

#define STB_TRUETYPE_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace ogplay::frontend {
namespace {

using Rows = std::array<std::uint8_t, 7>;
// 5x7 ASCII glyphs; bit 4 is the left column. \x01..\x04 are up, down, left, right arrows.
constexpr std::array<std::pair<char, Rows>, 55> kGlyphs{{
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'\\', {0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00}},   {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},   {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'~', {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00}},    {'\x01', {0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04}},
    {'\x02', {0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04}}, {'\x03', {0x00, 0x04, 0x08, 0x1F, 0x08, 0x04, 0x00}},
    {'\x04', {0x00, 0x04, 0x02, 0x1F, 0x02, 0x04, 0x00}},
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

void Text(const Canvas& canvas, const std::string_view text, const std::int64_t left, const std::int64_t top,
          const std::int64_t scale) {
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

// Centered text on a dark plate so labels stay readable over any game art.
void Label(const Canvas& canvas, const std::string_view text, const float center_x, const float center_y,
           const std::int64_t scale) {
    if (text.empty()) return;
    const auto width = TextWidth(text, scale), height = 7 * scale;
    const auto left = static_cast<std::int64_t>(std::lround(center_x)) - width / 2;
    const auto top = static_cast<std::int64_t>(std::lround(center_y)) - height / 2;
    canvas.Fill(left - 2 * scale, top - 2 * scale, width + 4 * scale, height + 4 * scale, kShade, 150);
    Text(canvas, text, left, top, scale);
}

// The frame-rate counter is set in SN Pro SemiBold (the rounded face the launcher uses where SF Pro Rounded is not
// available), embedded as a digits-only subset. Glyphs are rasterized once per size as signed distance fields, which
// give both the anti-aliased figure and its outline.
constexpr unsigned char kCounterFont[] = {
#include "counter_font.inc"
};
constexpr std::string_view kCounterCharacters = "0123456789-";
// Lime figures with a dark outline on a dark pill stay readable over bright, white or busy game art.
constexpr Color kCounterText{163, 230, 53};

[[nodiscard]] const stbtt_fontinfo* CounterFont() {
    static const auto font = [] {
        std::optional<stbtt_fontinfo> info{stbtt_fontinfo{}};
        if (stbtt_InitFont(&*info, kCounterFont, stbtt_GetFontOffsetForIndex(kCounterFont, 0)) == 0) info.reset();
        return info;
    }();
    return font ? &*font : nullptr;
}

struct CounterGlyph final {
    std::int32_t width{}, height{}, x_offset{}, y_offset{};
    float advance{};
    std::vector<std::uint8_t> field;  // 128 on the edge, larger inside
};

// Every size derives from the figure height: about 1/30 of the frame's short side, at least 12 pixels.
struct CounterFace final {
    std::int64_t text_height{};
    float cell{};      // tabular advance: the widest figure (SN Pro's 3 and 9 are a little narrower)
    float ascent{};    // baseline below the top of the figures
    float outline{};   // pixels of dark edge around each figure
    float field_scale{};
    std::array<CounterGlyph, kCounterCharacters.size()> glyphs;
};

[[nodiscard]] const CounterFace* Face(const std::int64_t text_height) {
    thread_local std::optional<CounterFace> cached;
    if (cached && cached->text_height == text_height) return &*cached;
    const auto* font = CounterFont();
    if (font == nullptr) return nullptr;
    int x0{}, y0{}, x1{}, y1{};
    if (stbtt_GetCodepointBox(font, '0', &x0, &y0, &x1, &y1) == 0 || y1 <= y0) return nullptr;
    CounterFace face;
    face.text_height = text_height;
    const float scale = static_cast<float>(text_height) / static_cast<float>(y1 - y0);
    face.ascent = static_cast<float>(y1) * scale;
    face.outline = std::max(1.0F, static_cast<float>(text_height) * 0.08F);
    const int padding = static_cast<int>(std::ceil(face.outline)) + 2;
    face.field_scale = 127.0F / static_cast<float>(padding);
    for (std::size_t index = 0; index < kCounterCharacters.size(); ++index) {
        const int code = kCounterCharacters[index];
        auto& glyph = face.glyphs[index];
        int advance{}, bearing{};
        stbtt_GetCodepointHMetrics(font, code, &advance, &bearing);
        glyph.advance = static_cast<float>(advance) * scale;
        if (code != '-') face.cell = std::max(face.cell, glyph.advance);
        auto* field = stbtt_GetCodepointSDF(font, scale, code, padding, 128, face.field_scale, &glyph.width,
                                            &glyph.height, &glyph.x_offset, &glyph.y_offset);
        if (field != nullptr) {
            glyph.field.assign(field, field + static_cast<std::ptrdiff_t>(glyph.width) * glyph.height);
            stbtt_FreeSDF(field, nullptr);
        }
    }
    cached = std::move(face);
    return &*cached;
}

struct CounterLayout final {
    const CounterFace* face;
    std::int64_t text_width, text_height, pad_x, pad_y, inset;
};
[[nodiscard]] CounterLayout Layout(const std::uint32_t width, const std::uint32_t height, const std::string_view text) {
    const auto short_side = static_cast<std::int64_t>(std::min(width, height));
    const auto text_height = std::max<std::int64_t>(12, short_side / 30);
    const auto* face = Face(text_height);
    float text_width{};
    if (face != nullptr)
        for (const char character : text) {
            const auto at = kCounterCharacters.find(character);
            text_width += at == std::string_view::npos ? 0.0F
                          : character == '-'           ? face->glyphs[at].advance
                                                       : face->cell;
        }
    // Rounded phone corners and cutouts clip a few percent of the short side.
    return {face, std::lround(text_width), text_height, std::lround(static_cast<float>(text_height) * 0.55F),
            std::lround(static_cast<float>(text_height) * 0.35F), std::max<std::int64_t>(4, short_side / 30)};
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
    if (source.kind == input::SourceKind::mouse_motion) return "MOUSE";
    if (source.kind == input::SourceKind::wheel) return source.code > 0 ? "WH\x01" : "WH\x02";
    if (source.kind == input::SourceKind::mouse_button) {
        if (name == "left") return "LMB";
        if (name == "right") return "RMB";
        if (name == "middle") return "MMB";
        std::string upper(name);
        std::ranges::transform(upper, upper.begin(), [](const char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
        return upper;
    }
    constexpr std::array<std::pair<std::string_view, std::string_view>, 31> kShort{{
        {"up", "\x01"},        {"down", "\x02"},       {"left", "\x03"},      {"right", "\x04"},
        {"space", "SPC"},      {"enter", "ENT"},       {"escape", "ESC"},     {"backspace", "BS"},
        {"tab", "TAB"},        {"caps_lock", "CAP"},   {"left_shift", "LSH"}, {"right_shift", "RSH"},
        {"left_ctrl", "LCT"},  {"right_ctrl", "RCT"},  {"left_alt", "LAL"},   {"right_alt", "RAL"},
        {"page_up", "PGU"},    {"page_down", "PGD"},   {"home", "HOM"},       {"end", "END"},
        {"insert", "INS"},     {"delete", "DEL"},      {"minus", "-"},        {"equals", "="},
        {"left_bracket", "["}, {"right_bracket", "]"}, {"backslash", "\\"},   {"semicolon", ";"},
        {"apostrophe", "'"},   {"grave", "~"},         {"comma", ","},
    }};
    for (const auto& [key, label] : kShort) if (key == name) return std::string(label);
    if (name == "period") return ".";
    if (name == "slash") return "/";
    std::string upper(name);
    std::ranges::transform(upper, upper.begin(), [](const char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
    return upper;
}

std::string PointerHintLine(const input::ControlMapper& mapper) {
    if (!mapper.LookActive()) return {};
    std::string keys;
    for (const auto& source : mapper.LockSources())
        keys += (keys.empty() ? "" : "/") + HintLabel(source);
    if (keys.empty()) return "NO MOUSE LOCK KEY";
    return keys + (mapper.WantsRelativePointer() ? " FREE MOUSE" : " LOCK MOUSE");
}

void DrawControlHints(const std::span<std::uint8_t> rgba8, const std::uint32_t width, const std::uint32_t height,
                      const std::span<const input::ControlHint> hints, const std::string_view mode,
                      const std::string_view pointer_line, const std::uint32_t top) {
    if (rgba8.size() < static_cast<std::size_t>(width) * height * 4U) return;
    const Canvas canvas{rgba8, width, height};
    const std::int64_t scale = std::max<std::int64_t>(1, static_cast<std::int64_t>(height) / 280);
    for (const auto& hint : hints) Zone(canvas, hint.zone, scale + 1);
    for (const auto& hint : hints) {
        const auto& zone = hint.zone;
        if (hint.stick && hint.sources.size() == 4) {
            constexpr std::array<std::pair<float, float>, 4> kDirections{{{0.0F, -1.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 0.0F}}};
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
          static_cast<float>(top) + 8.0F * static_cast<float>(scale), scale);
    Label(canvas, pointer_line,
          static_cast<float>(TextWidth(pointer_line, scale)) / 2.0F + 6.0F * static_cast<float>(scale),
          static_cast<float>(top) + 20.0F * static_cast<float>(scale), scale);
}

std::string FrameRateText(const std::optional<double> fps) {
    if (!fps.has_value() || !std::isfinite(*fps) || *fps < 0) return "--";
    return std::to_string(std::lround(std::min(*fps, 9999.0)));
}

OverlayArea FrameRateArea(const std::uint32_t width, const std::uint32_t height, const std::string_view text,
                          const ScreenCorner corner) {
    const auto layout = Layout(width, height, text);
    const auto plate_width = layout.text_width + 2 * layout.pad_x;
    const auto plate_height = layout.text_height + 2 * layout.pad_y;
    if (layout.face == nullptr || layout.text_width == 0 || plate_width + 2 * layout.inset > width ||
        plate_height + 2 * layout.inset > height)
        return {};
    const bool right = corner == ScreenCorner::top_right || corner == ScreenCorner::bottom_right;
    const bool bottom = corner == ScreenCorner::bottom_left || corner == ScreenCorner::bottom_right;
    return {static_cast<std::uint32_t>(right ? width - layout.inset - plate_width : layout.inset),
            static_cast<std::uint32_t>(bottom ? height - layout.inset - plate_height : layout.inset),
            static_cast<std::uint32_t>(plate_width), static_cast<std::uint32_t>(plate_height)};
}

void DrawFrameRate(const std::span<std::uint8_t> rgba8, const std::uint32_t width, const std::uint32_t height,
                   const std::string_view text, const ScreenCorner corner) {
    if (rgba8.size() < static_cast<std::size_t>(width) * height * 4U) return;
    const auto area = FrameRateArea(width, height, text, corner);
    if (area.width == 0) return;
    const Canvas canvas{rgba8, width, height};
    const auto layout = Layout(width, height, text);
    const auto& face = *layout.face;
    // A translucent pill with anti-aliased ends.
    const float radius = static_cast<float>(std::min(area.width, area.height)) / 2.0F;
    for (std::uint32_t row = 0; row < area.height; ++row)
        for (std::uint32_t column = 0; column < area.width; ++column) {
            const float px = static_cast<float>(column) + 0.5F, py = static_cast<float>(row) + 0.5F;
            const float cx = std::clamp(px, radius, static_cast<float>(area.width) - radius);
            const float coverage = std::clamp(radius - std::hypot(px - cx, py - radius) + 0.5F, 0.0F, 1.0F);
            if (coverage > 0)
                canvas.Blend(area.x + column, area.y + row, kShade, static_cast<std::uint8_t>(std::lround(coverage * 190.0F)));
        }
    // Figures sit centred in equal cells; everything stays inside the plate so SavedArea covers it.
    const auto right = static_cast<std::int64_t>(area.x + area.width), bottom = static_cast<std::int64_t>(area.y + area.height);
    const float baseline = static_cast<float>(area.y + layout.pad_y) + face.ascent;
    float pen = static_cast<float>(area.x + layout.pad_x);
    for (const char character : text) {
        const auto at = kCounterCharacters.find(character);
        if (at == std::string_view::npos) continue;
        const auto& glyph = face.glyphs[at];
        const float cell = character == '-' ? glyph.advance : face.cell;
        const auto left = static_cast<std::int64_t>(std::lround(pen + (cell - glyph.advance) / 2.0F)) + glyph.x_offset;
        const auto top = static_cast<std::int64_t>(std::lround(baseline)) + glyph.y_offset;
        pen += cell;
        for (std::int32_t row = 0; row < glyph.height; ++row)
            for (std::int32_t column = 0; column < glyph.width; ++column) {
                const auto x = left + column, y = top + row;
                if (x < static_cast<std::int64_t>(area.x) || x >= right || y < static_cast<std::int64_t>(area.y) || y >= bottom)
                    continue;
                const float distance =
                    (static_cast<float>(glyph.field[static_cast<std::size_t>(row * glyph.width + column)]) - 128.0F) /
                    face.field_scale;  // pixels, positive inside the figure
                const float outline = std::clamp(distance + face.outline + 0.5F, 0.0F, 1.0F);
                if (outline <= 0) continue;
                canvas.Blend(x, y, kShade, static_cast<std::uint8_t>(std::lround(outline * 200.0F)));
                const float coverage = std::clamp(distance + 0.5F, 0.0F, 1.0F);
                if (coverage > 0)
                    canvas.Blend(x, y, kCounterText, static_cast<std::uint8_t>(std::lround(coverage * 255.0F)));
            }
    }
}

void SavedArea::Save(const std::span<const std::uint8_t> rgba8, const std::uint32_t width, const OverlayArea area) {
    const auto end = (static_cast<std::size_t>(area.y) + area.height) * width * 4U;
    area_ = area.width != 0 && area.height != 0 && area.x + area.width <= width && end <= rgba8.size()
                ? area
                : OverlayArea{};
    const auto row_bytes = static_cast<std::size_t>(area_.width) * 4U;
    pixels_.resize(row_bytes * area_.height);
    for (std::uint32_t row = 0; row < area_.height; ++row)
        std::memcpy(pixels_.data() + row * row_bytes,
                    rgba8.data() + ((static_cast<std::size_t>(area_.y) + row) * width + area_.x) * 4U, row_bytes);
}

void SavedArea::Restore(const std::span<std::uint8_t> rgba8, const std::uint32_t width) const {
    const auto row_bytes = static_cast<std::size_t>(area_.width) * 4U;
    for (std::uint32_t row = 0; row < area_.height; ++row)
        std::memcpy(rgba8.data() + ((static_cast<std::size_t>(area_.y) + row) * width + area_.x) * 4U,
                    pixels_.data() + row * row_bytes, row_bytes);
}

}  // namespace ogplay::frontend
