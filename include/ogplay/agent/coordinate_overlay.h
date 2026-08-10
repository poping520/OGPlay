#pragma once

#include <cstdint>
#include <span>

namespace ogplay::agent {

void DrawCoordinateOverlay(std::uint32_t width, std::uint32_t height,
                           std::span<std::uint8_t> rgba8);

}  // namespace ogplay::agent
