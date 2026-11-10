#include "paint.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/core/SkSamplingOptions.h"

#include <algorithm>
#include <utility>

namespace ogplay::runtime::raster {
namespace {

struct Scalar {
    SkUnichar code;
    std::size_t units;
};

Scalar ReadScalar(std::u16string_view text, std::size_t i) {
    const auto hi = text[i];
    if (hi >= 0xd800 && hi <= 0xdbff && i + 1 < text.size()) {
        const auto lo = text[i + 1];
        if (lo >= 0xdc00 && lo <= 0xdfff) return {0x10000 + ((hi - 0xd800) << 10) + lo - 0xdc00, 2};
    }
    return {hi >= 0xd800 && hi <= 0xdfff ? 0xfffd : hi, 1};
}

struct UnicodeRange {
    SkUnichar first, last;
};

template <std::size_t N>
bool InRanges(SkUnichar code, const UnicodeRange (&ranges)[N]) {
    const auto it = std::ranges::lower_bound(ranges, code, {}, &UnicodeRange::last);
    return it != std::end(ranges) && code >= it->first;
}

// Unicode 13.0.0 Bidi_Class R/AL and General_Category Mark.
constexpr UnicodeRange kStrongRtl[] = {
    {0x5be, 0x5be},     {0x5c0, 0x5c0},     {0x5c3, 0x5c3},     {0x5c6, 0x5c6},     {0x5d0, 0x5ea},
    {0x5ef, 0x5f4},     {0x608, 0x608},     {0x60b, 0x60b},     {0x60d, 0x60d},     {0x61b, 0x61c},
    {0x61e, 0x64a},     {0x66d, 0x66f},     {0x671, 0x6d5},     {0x6e5, 0x6e6},     {0x6ee, 0x6ef},
    {0x6fa, 0x70d},     {0x70f, 0x710},     {0x712, 0x72f},     {0x74d, 0x7a5},     {0x7b1, 0x7b1},
    {0x7c0, 0x7ea},     {0x7f4, 0x7f5},     {0x7fa, 0x7fa},     {0x7fe, 0x815},     {0x81a, 0x81a},
    {0x824, 0x824},     {0x828, 0x828},     {0x830, 0x83e},     {0x840, 0x858},     {0x85e, 0x85e},
    {0x860, 0x86a},     {0x8a0, 0x8b4},     {0x8b6, 0x8c7},     {0x200f, 0x200f},   {0xfb1d, 0xfb1d},
    {0xfb1f, 0xfb28},   {0xfb2a, 0xfb36},   {0xfb38, 0xfb3c},   {0xfb3e, 0xfb3e},   {0xfb40, 0xfb41},
    {0xfb43, 0xfb44},   {0xfb46, 0xfbc1},   {0xfbd3, 0xfd3d},   {0xfd50, 0xfd8f},   {0xfd92, 0xfdc7},
    {0xfdf0, 0xfdfc},   {0xfe70, 0xfe74},   {0xfe76, 0xfefc},   {0x10800, 0x10805}, {0x10808, 0x10808},
    {0x1080a, 0x10835}, {0x10837, 0x10838}, {0x1083c, 0x1083c}, {0x1083f, 0x10855}, {0x10857, 0x1089e},
    {0x108a7, 0x108af}, {0x108e0, 0x108f2}, {0x108f4, 0x108f5}, {0x108fb, 0x1091b}, {0x10920, 0x10939},
    {0x1093f, 0x1093f}, {0x10980, 0x109b7}, {0x109bc, 0x109cf}, {0x109d2, 0x10a00}, {0x10a10, 0x10a13},
    {0x10a15, 0x10a17}, {0x10a19, 0x10a35}, {0x10a40, 0x10a48}, {0x10a50, 0x10a58}, {0x10a60, 0x10a9f},
    {0x10ac0, 0x10ae4}, {0x10aeb, 0x10af6}, {0x10b00, 0x10b35}, {0x10b40, 0x10b55}, {0x10b58, 0x10b72},
    {0x10b78, 0x10b91}, {0x10b99, 0x10b9c}, {0x10ba9, 0x10baf}, {0x10c00, 0x10c48}, {0x10c80, 0x10cb2},
    {0x10cc0, 0x10cf2}, {0x10cfa, 0x10d23}, {0x10e80, 0x10ea9}, {0x10ead, 0x10ead}, {0x10eb0, 0x10eb1},
    {0x10f00, 0x10f27}, {0x10f30, 0x10f45}, {0x10f51, 0x10f59}, {0x10fb0, 0x10fcb}, {0x10fe0, 0x10ff6},
    {0x1e800, 0x1e8c4}, {0x1e8c7, 0x1e8cf}, {0x1e900, 0x1e943}, {0x1e94b, 0x1e94b}, {0x1e950, 0x1e959},
    {0x1e95e, 0x1e95f}, {0x1ec71, 0x1ecb4}, {0x1ed01, 0x1ed3d}, {0x1ee00, 0x1ee03}, {0x1ee05, 0x1ee1f},
    {0x1ee21, 0x1ee22}, {0x1ee24, 0x1ee24}, {0x1ee27, 0x1ee27}, {0x1ee29, 0x1ee32}, {0x1ee34, 0x1ee37},
    {0x1ee39, 0x1ee39}, {0x1ee3b, 0x1ee3b}, {0x1ee42, 0x1ee42}, {0x1ee47, 0x1ee47}, {0x1ee49, 0x1ee49},
    {0x1ee4b, 0x1ee4b}, {0x1ee4d, 0x1ee4f}, {0x1ee51, 0x1ee52}, {0x1ee54, 0x1ee54}, {0x1ee57, 0x1ee57},
    {0x1ee59, 0x1ee59}, {0x1ee5b, 0x1ee5b}, {0x1ee5d, 0x1ee5d}, {0x1ee5f, 0x1ee5f}, {0x1ee61, 0x1ee62},
    {0x1ee64, 0x1ee64}, {0x1ee67, 0x1ee6a}, {0x1ee6c, 0x1ee72}, {0x1ee74, 0x1ee77}, {0x1ee79, 0x1ee7c},
    {0x1ee7e, 0x1ee7e}, {0x1ee80, 0x1ee89}, {0x1ee8b, 0x1ee9b}, {0x1eea1, 0x1eea3}, {0x1eea5, 0x1eea9},
    {0x1eeab, 0x1eebb},
};
constexpr UnicodeRange kMarks[] = {
    {0x300, 0x36f},     {0x483, 0x489},     {0x591, 0x5bd},     {0x5bf, 0x5bf},     {0x5c1, 0x5c2},
    {0x5c4, 0x5c5},     {0x5c7, 0x5c7},     {0x610, 0x61a},     {0x64b, 0x65f},     {0x670, 0x670},
    {0x6d6, 0x6dc},     {0x6df, 0x6e4},     {0x6e7, 0x6e8},     {0x6ea, 0x6ed},     {0x711, 0x711},
    {0x730, 0x74a},     {0x7a6, 0x7b0},     {0x7eb, 0x7f3},     {0x7fd, 0x7fd},     {0x816, 0x819},
    {0x81b, 0x823},     {0x825, 0x827},     {0x829, 0x82d},     {0x859, 0x85b},     {0x8d3, 0x8e1},
    {0x8e3, 0x903},     {0x93a, 0x93c},     {0x93e, 0x94f},     {0x951, 0x957},     {0x962, 0x963},
    {0x981, 0x983},     {0x9bc, 0x9bc},     {0x9be, 0x9c4},     {0x9c7, 0x9c8},     {0x9cb, 0x9cd},
    {0x9d7, 0x9d7},     {0x9e2, 0x9e3},     {0x9fe, 0x9fe},     {0xa01, 0xa03},     {0xa3c, 0xa3c},
    {0xa3e, 0xa42},     {0xa47, 0xa48},     {0xa4b, 0xa4d},     {0xa51, 0xa51},     {0xa70, 0xa71},
    {0xa75, 0xa75},     {0xa81, 0xa83},     {0xabc, 0xabc},     {0xabe, 0xac5},     {0xac7, 0xac9},
    {0xacb, 0xacd},     {0xae2, 0xae3},     {0xafa, 0xaff},     {0xb01, 0xb03},     {0xb3c, 0xb3c},
    {0xb3e, 0xb44},     {0xb47, 0xb48},     {0xb4b, 0xb4d},     {0xb55, 0xb57},     {0xb62, 0xb63},
    {0xb82, 0xb82},     {0xbbe, 0xbc2},     {0xbc6, 0xbc8},     {0xbca, 0xbcd},     {0xbd7, 0xbd7},
    {0xc00, 0xc04},     {0xc3e, 0xc44},     {0xc46, 0xc48},     {0xc4a, 0xc4d},     {0xc55, 0xc56},
    {0xc62, 0xc63},     {0xc81, 0xc83},     {0xcbc, 0xcbc},     {0xcbe, 0xcc4},     {0xcc6, 0xcc8},
    {0xcca, 0xccd},     {0xcd5, 0xcd6},     {0xce2, 0xce3},     {0xd00, 0xd03},     {0xd3b, 0xd3c},
    {0xd3e, 0xd44},     {0xd46, 0xd48},     {0xd4a, 0xd4d},     {0xd57, 0xd57},     {0xd62, 0xd63},
    {0xd81, 0xd83},     {0xdca, 0xdca},     {0xdcf, 0xdd4},     {0xdd6, 0xdd6},     {0xdd8, 0xddf},
    {0xdf2, 0xdf3},     {0xe31, 0xe31},     {0xe34, 0xe3a},     {0xe47, 0xe4e},     {0xeb1, 0xeb1},
    {0xeb4, 0xebc},     {0xec8, 0xecd},     {0xf18, 0xf19},     {0xf35, 0xf35},     {0xf37, 0xf37},
    {0xf39, 0xf39},     {0xf3e, 0xf3f},     {0xf71, 0xf84},     {0xf86, 0xf87},     {0xf8d, 0xf97},
    {0xf99, 0xfbc},     {0xfc6, 0xfc6},     {0x102b, 0x103e},   {0x1056, 0x1059},   {0x105e, 0x1060},
    {0x1062, 0x1064},   {0x1067, 0x106d},   {0x1071, 0x1074},   {0x1082, 0x108d},   {0x108f, 0x108f},
    {0x109a, 0x109d},   {0x135d, 0x135f},   {0x1712, 0x1714},   {0x1732, 0x1734},   {0x1752, 0x1753},
    {0x1772, 0x1773},   {0x17b4, 0x17d3},   {0x17dd, 0x17dd},   {0x180b, 0x180d},   {0x1885, 0x1886},
    {0x18a9, 0x18a9},   {0x1920, 0x192b},   {0x1930, 0x193b},   {0x1a17, 0x1a1b},   {0x1a55, 0x1a5e},
    {0x1a60, 0x1a7c},   {0x1a7f, 0x1a7f},   {0x1ab0, 0x1ac0},   {0x1b00, 0x1b04},   {0x1b34, 0x1b44},
    {0x1b6b, 0x1b73},   {0x1b80, 0x1b82},   {0x1ba1, 0x1bad},   {0x1be6, 0x1bf3},   {0x1c24, 0x1c37},
    {0x1cd0, 0x1cd2},   {0x1cd4, 0x1ce8},   {0x1ced, 0x1ced},   {0x1cf4, 0x1cf4},   {0x1cf7, 0x1cf9},
    {0x1dc0, 0x1df9},   {0x1dfb, 0x1dff},   {0x20d0, 0x20f0},   {0x2cef, 0x2cf1},   {0x2d7f, 0x2d7f},
    {0x2de0, 0x2dff},   {0x302a, 0x302f},   {0x3099, 0x309a},   {0xa66f, 0xa672},   {0xa674, 0xa67d},
    {0xa69e, 0xa69f},   {0xa6f0, 0xa6f1},   {0xa802, 0xa802},   {0xa806, 0xa806},   {0xa80b, 0xa80b},
    {0xa823, 0xa827},   {0xa82c, 0xa82c},   {0xa880, 0xa881},   {0xa8b4, 0xa8c5},   {0xa8e0, 0xa8f1},
    {0xa8ff, 0xa8ff},   {0xa926, 0xa92d},   {0xa947, 0xa953},   {0xa980, 0xa983},   {0xa9b3, 0xa9c0},
    {0xa9e5, 0xa9e5},   {0xaa29, 0xaa36},   {0xaa43, 0xaa43},   {0xaa4c, 0xaa4d},   {0xaa7b, 0xaa7d},
    {0xaab0, 0xaab0},   {0xaab2, 0xaab4},   {0xaab7, 0xaab8},   {0xaabe, 0xaabf},   {0xaac1, 0xaac1},
    {0xaaeb, 0xaaef},   {0xaaf5, 0xaaf6},   {0xabe3, 0xabea},   {0xabec, 0xabed},   {0xfb1e, 0xfb1e},
    {0xfe00, 0xfe0f},   {0xfe20, 0xfe2f},   {0x101fd, 0x101fd}, {0x102e0, 0x102e0}, {0x10376, 0x1037a},
    {0x10a01, 0x10a03}, {0x10a05, 0x10a06}, {0x10a0c, 0x10a0f}, {0x10a38, 0x10a3a}, {0x10a3f, 0x10a3f},
    {0x10ae5, 0x10ae6}, {0x10d24, 0x10d27}, {0x10eab, 0x10eac}, {0x10f46, 0x10f50}, {0x11000, 0x11002},
    {0x11038, 0x11046}, {0x1107f, 0x11082}, {0x110b0, 0x110ba}, {0x11100, 0x11102}, {0x11127, 0x11134},
    {0x11145, 0x11146}, {0x11173, 0x11173}, {0x11180, 0x11182}, {0x111b3, 0x111c0}, {0x111c9, 0x111cc},
    {0x111ce, 0x111cf}, {0x1122c, 0x11237}, {0x1123e, 0x1123e}, {0x112df, 0x112ea}, {0x11300, 0x11303},
    {0x1133b, 0x1133c}, {0x1133e, 0x11344}, {0x11347, 0x11348}, {0x1134b, 0x1134d}, {0x11357, 0x11357},
    {0x11362, 0x11363}, {0x11366, 0x1136c}, {0x11370, 0x11374}, {0x11435, 0x11446}, {0x1145e, 0x1145e},
    {0x114b0, 0x114c3}, {0x115af, 0x115b5}, {0x115b8, 0x115c0}, {0x115dc, 0x115dd}, {0x11630, 0x11640},
    {0x116ab, 0x116b7}, {0x1171d, 0x1172b}, {0x1182c, 0x1183a}, {0x11930, 0x11935}, {0x11937, 0x11938},
    {0x1193b, 0x1193e}, {0x11940, 0x11940}, {0x11942, 0x11943}, {0x119d1, 0x119d7}, {0x119da, 0x119e0},
    {0x119e4, 0x119e4}, {0x11a01, 0x11a0a}, {0x11a33, 0x11a39}, {0x11a3b, 0x11a3e}, {0x11a47, 0x11a47},
    {0x11a51, 0x11a5b}, {0x11a8a, 0x11a99}, {0x11c2f, 0x11c36}, {0x11c38, 0x11c3f}, {0x11c92, 0x11ca7},
    {0x11ca9, 0x11cb6}, {0x11d31, 0x11d36}, {0x11d3a, 0x11d3a}, {0x11d3c, 0x11d3d}, {0x11d3f, 0x11d45},
    {0x11d47, 0x11d47}, {0x11d8a, 0x11d8e}, {0x11d90, 0x11d91}, {0x11d93, 0x11d97}, {0x11ef3, 0x11ef6},
    {0x16af0, 0x16af4}, {0x16b30, 0x16b36}, {0x16f4f, 0x16f4f}, {0x16f51, 0x16f87}, {0x16f8f, 0x16f92},
    {0x16fe4, 0x16fe4}, {0x16ff0, 0x16ff1}, {0x1bc9d, 0x1bc9e}, {0x1d165, 0x1d169}, {0x1d16d, 0x1d172},
    {0x1d17b, 0x1d182}, {0x1d185, 0x1d18b}, {0x1d1aa, 0x1d1ad}, {0x1d242, 0x1d244}, {0x1da00, 0x1da36},
    {0x1da3b, 0x1da6c}, {0x1da75, 0x1da75}, {0x1da84, 0x1da84}, {0x1da9b, 0x1da9f}, {0x1daa1, 0x1daaf},
    {0x1e000, 0x1e006}, {0x1e008, 0x1e018}, {0x1e01b, 0x1e021}, {0x1e023, 0x1e024}, {0x1e026, 0x1e02a},
    {0x1e130, 0x1e136}, {0x1e2ec, 0x1e2ef}, {0x1e8d0, 0x1e8d6}, {0x1e944, 0x1e94a}, {0xe0100, 0xe01ef},
};

bool NeedsLayout(SkUnichar code) {
    return InRanges(code, kStrongRtl) || InRanges(code, kMarks) ||
           (code >= 0x0900 && code <= 0x109f) ||  // Indic, Thai, Lao, Tibetan, Myanmar.
           (code >= 0x1780 && code <= 0x17ff) ||  // Khmer.
           (code >= 0x1800 && code <= 0x18af) ||  // Mongolian.
           (code >= 0x1a00 && code <= 0x1cff) || (code >= 0xa800 && code <= 0xab2f) ||
           (code >= 0x11000 && code <= 0x11fff) || (code >= 0x200c && code <= 0x200f) ||
           (code >= 0x202a && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069) ||
           (code >= 0x1f3fb && code <= 0x1f3ff);
}

const SkFont& GlyphFont(const Paint& paint, const TextRun& run, const TextGlyph& glyph) {
    return glyph.fallback ? run.fallback : paint.font;
}

void Decorations(SkCanvas& canvas, const Paint& paint, float x, float y, float width) {
    const auto size = paint.font.getSize();
    const auto half = std::max(1.F, size / 18.F) * 0.5F;
    for (const auto& [flag, offset] : {std::pair{Paint::underline, 1.F / 9}, {Paint::strike_thru, -6.F / 21}}) {
        if (paint.flags & flag) {
            const auto middle = y + size * offset;
            canvas.drawRect(SkRect::MakeLTRB(x, middle - half, x + width, middle + half), paint.paint);
        }
    }
}

}  // namespace

Paint::Paint(const FontSource& source) : default_typeface(SystemTypeface(source, 0, 0)) {
    font.setTypeface(default_typeface->face);
    SetFlags(0x500);
}

void Paint::SetFlags(int value) {
    flags = value & 0xffff;
    paint.setAntiAlias(flags & anti_alias);
    paint.setDither(flags & dither);
    font.setEdging(!(flags & anti_alias) ? SkFont::Edging::kAlias
                   : flags & lcd         ? SkFont::Edging::kSubpixelAntiAlias
                                         : SkFont::Edging::kAntiAlias);
    font.setLinearMetrics(flags & linear_text);
    font.setSubpixel(flags & subpixel);
    font.setEmbeddedBitmaps(flags & embedded_bitmap);
    font.setForceAutoHinting(flags & auto_hinting);
    UpdateFont();
}

void Paint::SetTypeface(std::shared_ptr<const Typeface> face) {
    typeface = std::move(face);
    font.setTypeface((typeface ? typeface : default_typeface)->face);
    UpdateFont();
}

void Paint::UpdateFont() {
    const int missing = typeface ? typeface->style & ~typeface->intrinsic_style : 0;
    font.setEmbolden((flags & fake_bold) || (missing & 1));
    font.setSkewX(skew - ((missing & 2) ? 0.25F : 0.F));
}

SkFont Paint::FallbackFont(sk_sp<SkTypeface> face) const {
    auto fallback = font;
    fallback.setTypeface(std::move(face));
    const auto style = typeface ? typeface->style : 0;
    fallback.setEmbolden((flags & fake_bold) || (style & 1));
    fallback.setSkewX(skew - ((style & 2) ? 0.25F : 0.F));
    return fallback;
}

float Paint::AlignOffset(float width) const {
    return align == 1 ? width * 0.5F : align == 2 ? width : 0.F;
}

SkSamplingOptions Paint::Sampling(bool force_filter) const {
    return SkSamplingOptions((flags & filter_bitmap) || force_filter ? SkFilterMode::kLinear : SkFilterMode::kNearest);
}

bool ContainsStrongRtl(std::u16string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto [code, units] = ReadScalar(text, i);
        if (InRanges(code, kStrongRtl)) return true;
        i += units;
    }
    return false;
}

TextRun MapText(const Paint& paint, const FontSource& source, std::u16string_view text) {
    TextRun run;
    bool fallback_loaded = false;
    run.glyphs.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto [code, units] = ReadScalar(text, i);
        run.needs_layout |= NeedsLayout(code);
        auto glyph = paint.font.unicharToGlyph(code);
        bool fallback = false;
        if (!glyph && source) {
            if (!fallback_loaded) {
                run.fallback = paint.FallbackFont(SystemFallback(source));
                fallback_loaded = true;
            }
            glyph = run.fallback.unicharToGlyph(code);
            fallback = glyph != 0;
        }
        const auto& font = fallback ? run.fallback : paint.font;
        const auto advance = font.measureText(&glyph, sizeof(glyph), SkTextEncoding::kGlyphID);
        run.glyphs.push_back({glyph, i, units, run.width, advance, fallback});
        run.width += advance;
        i += units;
    }
    return run;
}

SkRect TextBounds(const Paint& paint, const TextRun& run) {
    auto bounds = SkRect::MakeEmpty();
    for (const auto& glyph : run.glyphs) {
        SkRect rect;
        GlyphFont(paint, run, glyph).measureText(&glyph.id, sizeof(glyph.id), SkTextEncoding::kGlyphID, &rect);
        bounds.join(rect.makeOffset(glyph.x, 0));
    }
    return bounds;
}

void TextPath(const Paint& paint, const TextRun& run, float x, float y, SkPathBuilder& path) {
    path.reset();
    x -= paint.AlignOffset(run.width);
    for (const auto& glyph : run.glyphs)
        if (const auto outline = GlyphFont(paint, run, glyph).getPath(glyph.id)) path.addPath(*outline, x + glyph.x, y);
}

void DrawText(SkCanvas& canvas, const Paint& paint, const TextRun& run, float x, float y,
              std::span<const SkPoint> positions) {
    if (positions.empty()) x -= paint.AlignOffset(run.width);
    std::vector<SkGlyphID> ids;
    std::vector<SkPoint> points;
    ids.reserve(run.glyphs.size());
    points.reserve(run.glyphs.size());
    for (std::size_t begin = 0; begin < run.glyphs.size();) {
        const auto& first = run.glyphs[begin];
        auto end = begin;
        while (end < run.glyphs.size() && run.glyphs[end].fallback == first.fallback) {
            const auto index = end++;
            const auto& glyph = run.glyphs[index];
            ids.push_back(glyph.id);
            if (positions.empty()) {
                points.push_back({glyph.x, 0});
            } else {
                auto point = positions[index];
                point.fX -= paint.AlignOffset(glyph.advance);
                points.push_back(point);
                Decorations(canvas, paint, point.x(), point.y(), glyph.advance);
            }
        }
        canvas.drawGlyphs(ids, points, {x, y}, GlyphFont(paint, run, first), paint.paint);
        ids.clear();
        points.clear();
        begin = end;
    }
    if (positions.empty() && !run.glyphs.empty()) Decorations(canvas, paint, x, y, run.width);
}

void DrawTextOnPath(SkCanvas& canvas, const Paint& paint, const TextRun& run, const SkPath& path, float horizontal,
                    float vertical) {
    SkPathMeasure measure(path, false);
    const auto length = measure.getLength();
    if (length <= 0) return;
    horizontal += paint.AlignOffset(length - run.width);
    for (const auto& glyph : run.glyphs) {
        const auto outline = GlyphFont(paint, run, glyph).getPath(glyph.id);
        if (!outline) continue;
        const auto offset = horizontal + glyph.x;
        const auto morph = [&](SkPoint point) {
            SkPoint position;
            SkVector tangent;
            if (!measure.getPosTan(point.x() + offset, &position, &tangent)) return point + SkVector{offset, vertical};
            return position + SkVector{-tangent.y(), tangent.x()} * (point.y() + vertical);
        };
        SkPathBuilder bent(outline->getFillType());
        // API19 maps each control point onto the curve and inserts a
        // midpoint on straight segments so glyph strokes can bend.
        SkPath::RawIter iterator(*outline);
        SkPoint points[4];
        for (auto verb = iterator.next(points); verb != SkPath::kDone_Verb; verb = iterator.next(points)) {
            switch (verb) {
            case SkPath::kMove_Verb: bent.moveTo(morph(points[0])); break;
            case SkPath::kLine_Verb: bent.quadTo(morph((points[0] + points[1]) * 0.5F), morph(points[1])); break;
            case SkPath::kQuad_Verb: bent.quadTo(morph(points[1]), morph(points[2])); break;
            case SkPath::kConic_Verb: bent.conicTo(morph(points[1]), morph(points[2]), iterator.conicWeight()); break;
            case SkPath::kCubic_Verb: bent.cubicTo(morph(points[1]), morph(points[2]), morph(points[3])); break;
            case SkPath::kClose_Verb: bent.close(); break;
            case SkPath::kDone_Verb: break;
            }
        }
        canvas.drawPath(bent.detach(), paint.paint);
    }
}

}  // namespace ogplay::runtime::raster
