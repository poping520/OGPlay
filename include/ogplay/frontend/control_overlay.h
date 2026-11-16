#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/input/control_mapper.h"

namespace ogplay::frontend {

// Short ASCII label for the overlay: "J", "SPC", "LMB", arrows as \x01..\x04.
[[nodiscard]] std::string HintLabel(const input::InputSource& source);

// F1 key hints (ADR-0134), drawn on a host-side copy of the presented guest frame: zones,
// their keys, and a banner with the host shortcuts and any non-default mode; pointer_line, when
// not empty, goes on a second line (the mouse lock keys). Coordinates are guest frame pixels.
// The guest frame and MCP captures never carry the overlay. top moves the banner lines down by that
// many pixels (below a frame-rate counter in the same corner).
void DrawControlHints(std::span<std::uint8_t> rgba8, std::uint32_t width, std::uint32_t height,
                      std::span<const input::ControlHint> hints, std::string_view mode,
                      std::string_view pointer_line = {}, std::uint32_t top = 0);

enum class ScreenCorner : std::uint8_t { top_left, top_right, bottom_left, bottom_right };

// A rectangle of frame pixels; empty when nothing fits.
struct OverlayArea final {
    std::uint32_t x{}, y{}, width{}, height{};
    bool operator==(const OverlayArea&) const = default;
};
// "60 FPS" for a measured rate (rounded), "-- FPS" before the first measurement.
[[nodiscard]] std::string FrameRateText(std::optional<double> fps);
// Where the frame-rate counter for `text` goes: a dark plate in `corner`, inset from the edges so phone corners
// and cutouts leave it readable, sized from the frame's short side like the key hints. Empty if the frame is too
// small to hold it.
[[nodiscard]] OverlayArea FrameRateArea(std::uint32_t width, std::uint32_t height, std::string_view text,
                                        ScreenCorner corner);
// Draws the counter into FrameRateArea and changes no pixel outside it.
void DrawFrameRate(std::span<std::uint8_t> rgba8, std::uint32_t width, std::uint32_t height, std::string_view text,
                   ScreenCorner corner);

// The pixels of one area kept aside, for drawing on a frame in place and putting the frame back afterwards
// (a whole-frame copy per present costs far more than the few rows the counter covers).
class SavedArea final {
public:
    void Save(std::span<const std::uint8_t> rgba8, std::uint32_t width, OverlayArea area);
    void Restore(std::span<std::uint8_t> rgba8, std::uint32_t width) const;

private:
    OverlayArea area_{};
    std::vector<std::uint8_t> pixels_;
};
// The mouse lock line for a crosshair setup: "~ LOCK MOUSE" while free, "~ FREE MOUSE" while
// locked, "NO MOUSE LOCK KEY" without a lock binding; empty without an active look.
[[nodiscard]] std::string PointerHintLine(const input::ControlMapper& mapper);

}  // namespace ogplay::frontend
