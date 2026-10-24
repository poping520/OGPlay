#pragma once
#include <array>
#include <cstdint>

namespace ogplay::runtime::ui {
using Glyph = std::array<std::uint8_t, 7>;
[[nodiscard]] Glyph GlyphRows(char16_t value);
}  // namespace ogplay::runtime::ui
