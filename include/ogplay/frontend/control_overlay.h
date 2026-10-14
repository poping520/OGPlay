#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/input/control_mapper.h"

namespace ogplay::frontend {

// Short ASCII label for the overlay: "J", "SPC", "LMB", arrows as \x01..\x04.
[[nodiscard]] std::string HintLabel(const input::InputSource& source);

// F1 key hints (ADR-0134), drawn on a host-side copy of the presented guest frame: zones,
// their keys, and a banner with the host shortcuts and any non-default mode. Coordinates are
// guest frame pixels. The guest frame and MCP captures never carry the overlay.
void DrawControlHints(std::span<std::uint8_t> rgba8, std::uint32_t width, std::uint32_t height,
                      std::span<const input::ControlHint> hints, std::string_view mode);

}  // namespace ogplay::frontend
