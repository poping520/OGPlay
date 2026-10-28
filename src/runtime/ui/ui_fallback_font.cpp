#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ogplay/core/sha256.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_truetype.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ogplay::runtime::ui {
struct UiFallbackFont::Impl {
    struct Face {
        std::vector<unsigned char> bytes;
        stbtt_fontinfo font{};
    };
    std::array<Face, 2> faces;
    mutable std::mutex mutex;
    mutable std::unordered_map<std::uint32_t, std::shared_ptr<const FallbackGlyph>> cache;
};
namespace {
std::string Codepoint(char16_t unit) {
    std::ostringstream out;
    out << "U+" << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << static_cast<unsigned>(unit);
    return out.str();
}
}
UiFallbackFont::UiFallbackFont(const std::span<const std::byte> roboto, const std::span<const std::byte> droid)
    : impl_(std::make_unique<Impl>()) {
    // AOSP android-4.4.4_r2.0.1 frameworks/base/data/fonts/system_fonts.xml
    // selects sans-serif Roboto first; fallback_fonts.xml supplies DroidSansFallback.
    // This bounded subset uses only Regular; bold/italic remain synthetic.
    const std::array payloads{roboto, droid};
    constexpr std::array hashes{
        "f87925e2be4c38abba7920ec54e5634f0538ba6368d758e4da300d9af12c9d27",
        "05d71b179ef97b82cf1bb91cef290c600a510f77f39b4964359e3ef88378c79d"};
    for (std::size_t i = 0; i < payloads.size(); ++i) {
        const auto bytes = payloads[i];
        if (bytes.size() < 12 || bytes.size() > 8U * 1024U * 1024U)
            throw std::runtime_error("fallback font payload size is invalid");
        if (core::Sha256(bytes) != hashes[i])
            throw std::runtime_error("fallback font is not the pinned API19 trusted payload");
        auto& face = impl_->faces[i];
        face.bytes.resize(bytes.size());
        std::transform(bytes.begin(), bytes.end(), face.bytes.begin(), [](const auto b) { return std::to_integer<unsigned char>(b); });
        const auto offset = stbtt_GetFontOffsetForIndex(face.bytes.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&face.font, face.bytes.data(), offset))
            throw std::runtime_error("fallback font payload is not a TrueType font");
    }
}
UiFallbackFont::~UiFallbackFont() = default;
std::size_t UiFallbackFont::SourceIndex(const char16_t unit) const {
    // Punctuation and mathematical symbols have independent glyphs. Script
    // shaping, combining marks and directional controls require a text engine.
    const bool simple_block = unit < 0x0300 || (unit >= 0x0370 && unit < 0x0530) ||
        (unit >= 0x2000 && unit < 0x2c00) || (unit >= 0x2e80 && unit < 0xa000) ||
        (unit >= 0xac00 && unit <= 0xd7a3) || (unit >= 0xf900 && unit <= 0xfaff) ||
        (unit >= 0xff01 && unit <= 0xff9d);
    const bool combining_or_control = (unit >= 0x0483 && unit <= 0x0489) ||
        (unit >= 0x200b && unit <= 0x200f) || (unit >= 0x202a && unit <= 0x202e) ||
        (unit >= 0x2060 && unit <= 0x206f) || (unit >= 0x20d0 && unit <= 0x20ff) ||
        unit == 0x302a || (unit >= 0x302b && unit <= 0x302f) || unit == 0x3099 || unit == 0x309a;
    if (!simple_block || combining_or_control)
        throw std::runtime_error(Codepoint(unit) + " requires unsupported shaping, combining, bidi or scalar decoding");
    for (std::size_t i = 0; i < impl_->faces.size(); ++i)
        if (stbtt_FindGlyphIndex(&impl_->faces[i].font, unit)) return i;
    throw std::runtime_error(Codepoint(unit) + " is absent from API19 font chain");
}
std::shared_ptr<const FallbackGlyph> UiFallbackFont::Glyph(const char16_t unit, const std::int32_t pixels) const {
    const auto& font = impl_->faces[SourceIndex(unit)].font;
    if (pixels < 1 || pixels > 128) throw std::runtime_error("fallback glyph size is outside 1..128 px");
    std::scoped_lock lock(impl_->mutex);
    const auto key = (static_cast<std::uint32_t>(pixels) << 16U) | unit;
    if (const auto found = impl_->cache.find(key); found != impl_->cache.end()) return found->second;
    const auto scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(pixels));
    auto glyph = std::make_shared<FallbackGlyph>();
    stbtt_GetCodepointBitmapBox(&font, unit, scale, scale, &glyph->x, &glyph->y, &glyph->width, &glyph->height);
    glyph->width -= glyph->x; glyph->height -= glyph->y;
    if (glyph->width < 0 || glyph->height < 0 || glyph->width > 256 || glyph->height > 256)
        throw std::runtime_error("fallback glyph bitmap exceeds bounds");
    glyph->alpha.resize(static_cast<std::size_t>(glyph->width) * static_cast<std::size_t>(glyph->height));
    if (!glyph->alpha.empty()) stbtt_MakeCodepointBitmap(&font, glyph->alpha.data(), glyph->width, glyph->height,
        glyph->width, scale, scale, unit);
    int ascent{}, descent{}, gap{};
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
    glyph->y += static_cast<std::int32_t>(std::lround(static_cast<float>(ascent) * scale));
    if (impl_->cache.size() >= 128) impl_->cache.clear();
    impl_->cache.emplace(key, glyph);
    return glyph;
}
}
