#include "ogplay/video/rgba_canvas.h"

#include <algorithm>
#include <cstring>

namespace ogplay::video {

std::vector<std::uint8_t> ComposeRgbaOnCanvas(
    const VideoFrame& frame, const std::uint32_t canvas_width,
    const std::uint32_t canvas_height) {
    constexpr std::uint32_t kMaxDimension = 4096U;
    if (canvas_width == 0U || canvas_height == 0U ||
        canvas_width > kMaxDimension || canvas_height > kMaxDimension) {
        throw VideoPlayerError("canvas dimensions are out of bounds");
    }
    if (frame.width == 0U || frame.height == 0U ||
        frame.rgba8.size() != static_cast<std::size_t>(frame.width) *
                                  frame.height * 4U) {
        throw VideoPlayerError("frame pixel buffer does not match its size");
    }

    std::vector<std::uint8_t> canvas(static_cast<std::size_t>(canvas_width) *
                                     canvas_height * 4U);
    // Opaque black background.
    for (std::size_t offset = 3; offset < canvas.size(); offset += 4U) {
        canvas[offset] = 0xFFU;
    }

    // Aspect-preserving target rectangle, centered. 64-bit math keeps the
    // comparison exact: fit by width when frame_w/frame_h >= canvas_w/canvas_h.
    std::uint32_t target_width = canvas_width;
    std::uint32_t target_height = canvas_height;
    const std::uint64_t frame_wide = static_cast<std::uint64_t>(frame.width) *
                                     canvas_height;
    const std::uint64_t canvas_wide = static_cast<std::uint64_t>(canvas_width) *
                                      frame.height;
    if (frame_wide >= canvas_wide) {
        target_height = static_cast<std::uint32_t>(std::max<std::uint64_t>(
            canvas_wide / frame.width, 1U));
    } else {
        target_width = static_cast<std::uint32_t>(std::max<std::uint64_t>(
            frame_wide / frame.height, 1U));
    }
    const std::uint32_t origin_x = (canvas_width - target_width) / 2U;
    const std::uint32_t origin_y = (canvas_height - target_height) / 2U;

    for (std::uint32_t row = 0; row < target_height; ++row) {
        const std::uint32_t source_row = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(row) * frame.height / target_height);
        const std::uint8_t* source_line =
            frame.rgba8.data() +
            static_cast<std::size_t>(source_row) * frame.width * 4U;
        std::uint8_t* target_line =
            canvas.data() +
            (static_cast<std::size_t>(origin_y + row) * canvas_width +
             origin_x) * 4U;
        if (target_width == frame.width) {
            std::memcpy(target_line, source_line,
                        static_cast<std::size_t>(target_width) * 4U);
            continue;
        }
        for (std::uint32_t column = 0; column < target_width; ++column) {
            const std::uint32_t source_column = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(column) * frame.width /
                target_width);
            std::memcpy(target_line + static_cast<std::size_t>(column) * 4U,
                        source_line + static_cast<std::size_t>(source_column) *
                                          4U,
                        4U);
        }
    }
    return canvas;
}

}  // namespace ogplay::video
