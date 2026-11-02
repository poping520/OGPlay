#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/app/export.h"

namespace ogplay::app {

// How a running game's window/Dock icon combines the game icon with the OGPlay mark:
// badge = the game icon fills the macOS plate with a round OGPlay badge bottom-right (default);
// cradle = the game icon is the cartridge, inserted in the OGPlay blue base.
enum class RunningIconStyle : std::uint8_t { cradle, badge };

[[nodiscard]] OGPLAYCORE_API std::optional<RunningIconStyle> ParseRunningIconStyle(std::string_view name);
[[nodiscard]] OGPLAYCORE_API std::string_view RunningIconStyleName(RunningIconStyle style);

// Straight (non-premultiplied) RGBA8, row-major, no padding.
struct RgbaImage final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

// Square icon of `size` pixels (16..1024). The game icon is cropped to fill the macOS rounded square:
// transparent margins and soft drop shadows trimmed, then zoomed (at most 20%) until its own corners
// leave no transparent pixel inside the squircle; no backing plate is added. `cartridge_png` is the
// bundled OGPlay cartridge (data/branding/ogplay-cartridge.png). Throws std::invalid_argument when
// either PNG cannot be decoded or the game icon is fully transparent.
[[nodiscard]] OGPLAYCORE_API RgbaImage ComposeRunningIcon(std::span<const std::byte> game_icon_png,
                                                          std::span<const std::byte> cartridge_png,
                                                          RunningIconStyle style, std::uint32_t size);
// The same icon at several sizes (each 16..1024), decoding and cropping the game icon once: window icons
// hand the platform one picture per size instead of letting it shrink the largest.
[[nodiscard]] OGPLAYCORE_API std::vector<RgbaImage> ComposeRunningIcons(std::span<const std::byte> game_icon_png,
                                                                        std::span<const std::byte> cartridge_png,
                                                                        RunningIconStyle style,
                                                                        std::span<const std::uint32_t> sizes);
// PNG bytes of an RGBA image; empty when encoding fails.
[[nodiscard]] OGPLAYCORE_API std::vector<std::byte> EncodeRgbaPng(const RgbaImage& image);

}  // namespace ogplay::app
