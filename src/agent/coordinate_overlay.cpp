#include "ogplay/agent/coordinate_overlay.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>

namespace ogplay::agent {
namespace {

constexpr std::uint32_t kMajorStep = 100U;
constexpr std::uint32_t kMinorStep = 25U;
constexpr std::uint32_t kMinorTickLength = 7U;
constexpr std::uint32_t kGlyphScale = 2U;
constexpr std::uint8_t kGridAlpha = 112U;
constexpr std::uint8_t kTickAlpha = 176U;

constexpr std::array<std::array<std::uint8_t, 5>, 10> kDigits{{
    {{0b111U, 0b101U, 0b101U, 0b101U, 0b111U}},
    {{0b010U, 0b110U, 0b010U, 0b010U, 0b111U}},
    {{0b111U, 0b001U, 0b111U, 0b100U, 0b111U}},
    {{0b111U, 0b001U, 0b111U, 0b001U, 0b111U}},
    {{0b101U, 0b101U, 0b111U, 0b001U, 0b001U}},
    {{0b111U, 0b100U, 0b111U, 0b001U, 0b111U}},
    {{0b111U, 0b100U, 0b111U, 0b101U, 0b111U}},
    {{0b111U, 0b001U, 0b001U, 0b001U, 0b001U}},
    {{0b111U, 0b101U, 0b111U, 0b101U, 0b111U}},
    {{0b111U, 0b101U, 0b111U, 0b001U, 0b111U}},
}};

struct Canvas final {
    std::uint32_t width;
    std::uint32_t height;
    std::span<std::uint8_t> pixels;

    void Blend(const std::uint32_t x, const std::uint32_t y,
               const std::array<std::uint8_t, 3> color,
               const std::uint8_t alpha) const {
        if (x >= width || y >= height) return;
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
        for (std::size_t channel = 0; channel < color.size(); ++channel) {
            const auto mixed = static_cast<unsigned>(pixels[offset + channel]) *
                                   (255U - alpha) +
                               static_cast<unsigned>(color[channel]) * alpha + 127U;
            pixels[offset + channel] = static_cast<std::uint8_t>(mixed / 255U);
        }
    }

    void Rectangle(const std::uint32_t x, const std::uint32_t y,
                   const std::uint32_t rectangle_width,
                   const std::uint32_t rectangle_height,
                   const std::array<std::uint8_t, 3> color,
                   const std::uint8_t alpha) const {
        const auto x_end = std::min<std::uint64_t>(width,
                                                   static_cast<std::uint64_t>(x) + rectangle_width);
        const auto y_end = std::min<std::uint64_t>(height,
                                                   static_cast<std::uint64_t>(y) + rectangle_height);
        for (auto row = y; row < y_end; ++row) {
            for (auto column = x; column < x_end; ++column) {
                Blend(column, row, color, alpha);
            }
        }
    }
};

void DrawNumber(const Canvas& canvas, const std::uint32_t requested_x,
                const std::uint32_t requested_y, const std::uint32_t value) {
    std::array<char, 10> text{};
    const auto converted = std::to_chars(text.data(), text.data() + text.size(), value);
    if (converted.ec != std::errc{}) return;
    const auto digits = static_cast<std::uint32_t>(converted.ptr - text.data());
    const auto label_width = digits * 8U;
    const auto label_height = 12U;
    if (label_width > canvas.width || label_height > canvas.height) return;
    const auto x = std::min(requested_x, canvas.width - label_width);
    const auto y = std::min(requested_y, canvas.height - label_height);
    canvas.Rectangle(x, y, label_width, label_height, {0U, 0U, 0U}, 208U);

    for (std::uint32_t index = 0; index < digits; ++index) {
        const auto digit = static_cast<unsigned>(text[index] - '0');
        if (digit >= kDigits.size()) continue;
        for (std::uint32_t row = 0; row < 5U; ++row) {
            for (std::uint32_t column = 0; column < 3U; ++column) {
                if ((kDigits[digit][row] & (1U << (2U - column))) == 0U) continue;
                canvas.Rectangle(x + index * 8U + column * kGlyphScale + 1U,
                                 y + row * kGlyphScale + 1U,
                                 kGlyphScale, kGlyphScale,
                                 {255U, 255U, 255U}, 255U);
            }
        }
    }
}

}  // namespace

void DrawCoordinateOverlay(const std::uint32_t width, const std::uint32_t height,
                           const std::span<std::uint8_t> rgba8) {
    const auto expected = static_cast<std::uint64_t>(width) * height * 4U;
    if (width == 0U || height == 0U || expected != rgba8.size()) {
        throw std::invalid_argument("coordinate overlay requires exact RGBA8 dimensions");
    }
    const Canvas canvas{width, height, rgba8};
    for (std::uint32_t x = 0; x < width; x += kMajorStep) {
        canvas.Rectangle(x, 0U, 1U, height, {0U, 255U, 255U}, kGridAlpha);
        DrawNumber(canvas, x + 2U, 2U, x);
    }
    for (std::uint32_t y = 0; y < height; y += kMajorStep) {
        canvas.Rectangle(0U, y, width, 1U, {0U, 255U, 255U}, kGridAlpha);
        if (y != 0U) DrawNumber(canvas, 2U, y + 2U, y);
    }
    for (std::uint32_t x = kMinorStep; x < width; x += kMinorStep) {
        if (x % kMajorStep == 0U) continue;
        canvas.Rectangle(x, 0U, 1U, kMinorTickLength, {255U, 224U, 0U}, kTickAlpha);
        if (height > kMinorTickLength) {
            canvas.Rectangle(x, height - kMinorTickLength, 1U, kMinorTickLength,
                             {255U, 224U, 0U}, kTickAlpha);
        }
    }
    for (std::uint32_t y = kMinorStep; y < height; y += kMinorStep) {
        if (y % kMajorStep == 0U) continue;
        canvas.Rectangle(0U, y, kMinorTickLength, 1U, {255U, 224U, 0U}, kTickAlpha);
        if (width > kMinorTickLength) {
            canvas.Rectangle(width - kMinorTickLength, y, kMinorTickLength, 1U,
                             {255U, 224U, 0U}, kTickAlpha);
        }
    }
}

}  // namespace ogplay::agent
