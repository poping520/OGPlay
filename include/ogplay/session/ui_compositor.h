#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ogplay/runtime/ui/ui_renderer.h"

namespace ogplay::session {

[[nodiscard]] std::vector<std::uint8_t> ComposeUiOverlay(
    std::span<const std::uint8_t> base_rgba8,
    const runtime::ui::UiOverlayFrame& overlay);

}  // namespace ogplay::session
