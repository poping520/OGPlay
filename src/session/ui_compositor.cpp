#include "ogplay/session/ui_compositor.h"

#include <stdexcept>

namespace ogplay::session {

std::vector<std::uint8_t> ComposeUiOverlay(
    const std::span<const std::uint8_t> base_rgba8,
    const runtime::ui::UiOverlayFrame& overlay) {
    if (base_rgba8.size() != overlay.rgba8.size() ||
        overlay.rgba8.size() != static_cast<std::size_t>(overlay.width) *
                                    overlay.height * 4U) {
        throw std::invalid_argument("UI composition frame layouts differ");
    }
    std::vector<std::uint8_t> result(base_rgba8.begin(), base_rgba8.end());
    for (std::size_t offset = 0; offset < result.size(); offset += 4U) {
        const auto alpha = overlay.rgba8[offset + 3U];
        if (alpha == 0) continue;
        const auto inverse = 255U - alpha;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            result[offset + channel] = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(overlay.rgba8[offset + channel]) *
                     alpha +
                 static_cast<std::uint32_t>(result[offset + channel]) * inverse +
                 127U) /
                255U);
        }
        result[offset + 3U] = static_cast<std::uint8_t>(
            alpha + (static_cast<std::uint32_t>(result[offset + 3U]) * inverse +
                     127U) /
                        255U);
    }
    return result;
}

}  // namespace ogplay::session
