#include "ogplay/core/software_surface.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ogplay::core {

SoftwareSurface::SoftwareSurface(const std::size_t width, const std::size_t height)
    : width_(width), height_(height) {
    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument("software surface dimensions must be non-zero");
    }
    if (width_ > std::numeric_limits<std::size_t>::max() / height_ / 4U) {
        throw std::overflow_error("software surface dimensions overflow");
    }
    pixels_.resize(width_ * height_ * 4U);
}

void SoftwareSurface::Clear(const Rgba8 color) {
    for (std::size_t pixel = 0; pixel < width_ * height_; ++pixel) Store(pixel, color);
}

void SoftwareSurface::FillRect(const std::size_t x,
                               const std::size_t y,
                               const std::size_t width,
                               const std::size_t height,
                               const Rgba8 color) {
    const auto right = x >= width_ ? width_ : x + std::min(width, width_ - x);
    const auto bottom = y >= height_ ? height_ : y + std::min(height, height_ - y);
    for (auto row = y; row < bottom; ++row) {
        for (auto column = x; column < right; ++column) {
            Store(row * width_ + column, color);
        }
    }
}

ImageView SoftwareSurface::View() const noexcept {
    return {pixels_, width_, height_};
}

std::size_t SoftwareSurface::Width() const noexcept { return width_; }
std::size_t SoftwareSurface::Height() const noexcept { return height_; }

void SoftwareSurface::Store(const std::size_t pixel, const Rgba8 color) {
    const auto offset = pixel * 4U;
    pixels_[offset] = color.red;
    pixels_[offset + 1U] = color.green;
    pixels_[offset + 2U] = color.blue;
    pixels_[offset + 3U] = color.alpha;
}

}  // namespace ogplay::core
