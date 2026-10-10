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
// Immutable API19 fallback font, injected from the checked runtime payload.
// Simple BMP glyph rasterization; shaping/bidi and arbitrary app fonts are outside this interface.
class UiFallbackFont final {
public:
    explicit UiFallbackFont(std::span<const std::byte> bytes);
    ~UiFallbackFont();
    [[nodiscard]] std::shared_ptr<const FallbackGlyph> Glyph(char16_t unit, std::int32_t pixels) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
