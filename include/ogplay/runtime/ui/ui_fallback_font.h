#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ogplay::runtime::ui {
struct FallbackGlyph final {
    std::int32_t width{}, height{}, x{}, y{};
    std::vector<std::uint8_t> alpha;
};
// Immutable ordered API19 font chain, injected from the checked runtime payload.
// Simple BMP glyph rasterization; shaping/bidi and arbitrary app fonts are outside this interface.
class UiFallbackFont final {
public:
    UiFallbackFont(std::span<const std::byte> roboto, std::span<const std::byte> droid);
    // First covering font; rejects unsupported scalar/shaping requirements.
    [[nodiscard]] std::size_t SourceIndex(char16_t unit) const;
    ~UiFallbackFont();
    [[nodiscard]] std::shared_ptr<const FallbackGlyph> Glyph(char16_t unit, std::int32_t pixels) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
