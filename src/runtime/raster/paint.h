#pragma once

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkTypeface.h"

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

class SkCanvas;
class SkPathBuilder;

namespace ogplay::runtime::raster {

using FontSource = std::function<std::vector<std::byte>(std::string_view)>;

struct Typeface {
    sk_sp<SkTypeface> face;
    int family{-1};  // External font, with no installed sibling faces.
    int style{};
    int intrinsic_style{};
};

int SystemFontFamily(std::string_view name);
std::shared_ptr<const Typeface> SystemTypeface(const FontSource& source, int family, int style);
sk_sp<SkTypeface> SystemFallback(const FontSource& source);
std::shared_ptr<const Typeface> DecodeTypeface(std::span<const std::byte> bytes);
std::shared_ptr<const Typeface> RestyleTypeface(const FontSource& source, const Typeface& face, int style);

struct Shader;

struct Paint {
    enum Flag {
        anti_alias = 0x001,
        filter_bitmap = 0x002,
        dither = 0x004,
        underline = 0x008,
        strike_thru = 0x010,
        fake_bold = 0x020,
        linear_text = 0x040,
        subpixel = 0x080,
        lcd = 0x200,
        embedded_bitmap = 0x400,
        auto_hinting = 0x800
    };

    explicit Paint(const FontSource& source = {});
    void SetFlags(int value);
    void SetTypeface(std::shared_ptr<const Typeface> face);
    [[nodiscard]] SkFont FallbackFont(sk_sp<SkTypeface> face) const;
    [[nodiscard]] float AlignOffset(float width) const;
    [[nodiscard]] SkSamplingOptions Sampling(bool force_filter = false) const;

    SkPaint paint;
    SkFont font;
    std::shared_ptr<const Typeface> typeface;
    std::shared_ptr<const Typeface> default_typeface;

    std::shared_ptr<Shader> shader;
    std::string locale;
    int flags{};
    int align{};
    int hinting{1};
    float skew{};

private:
    void UpdateFont();
};

struct TextGlyph {
    SkGlyphID id{};
    std::size_t unit{}, units{};
    float x{}, advance{};
    bool fallback{};
};

struct TextRun {
    std::vector<TextGlyph> glyphs;
    SkFont fallback;
    float width{};
    bool needs_layout{};
};

TextRun MapText(const Paint& paint, const FontSource& source, std::u16string_view text);
bool ContainsStrongRtl(std::u16string_view text);
SkRect TextBounds(const Paint& paint, const TextRun& run);
void TextPath(const Paint& paint, const TextRun& run, float x, float y, SkPathBuilder& path);
void DrawText(SkCanvas& canvas, const Paint& paint, const TextRun& run, float x, float y,
              std::span<const SkPoint> positions = {});
void DrawTextOnPath(SkCanvas& canvas, const Paint& paint, const TextRun& run, const SkPath& path, float horizontal,
                    float vertical);

}  // namespace ogplay::runtime::raster
