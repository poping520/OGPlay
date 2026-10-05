#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ogplay/runtime/ui/ui_renderer.h"

namespace ogplay::session {

void ComposeUiOverlayInPlace(std::span<std::uint8_t> base_rgba8,
                            const runtime::ui::UiOverlayFrame& overlay);

[[nodiscard]] std::vector<std::uint8_t> ComposeUiOverlay(
    std::span<const std::uint8_t> base_rgba8,
    const runtime::ui::UiOverlayFrame& overlay);

}  // namespace ogplay::session
