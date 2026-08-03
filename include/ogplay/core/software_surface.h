#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ogplay/core/frame_compare.h"

namespace ogplay::core {

struct Rgba8 {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
};

class SoftwareSurface final {
public:
    SoftwareSurface(std::size_t width, std::size_t height);

    void Clear(Rgba8 color);
    void FillRect(std::size_t x,
                  std::size_t y,
                  std::size_t width,
                  std::size_t height,
                  Rgba8 color);

    [[nodiscard]] ImageView View() const noexcept;
    [[nodiscard]] std::size_t Width() const noexcept;
    [[nodiscard]] std::size_t Height() const noexcept;

private:
    void Store(std::size_t pixel, Rgba8 color);

    std::size_t width_;
    std::size_t height_;
    std::vector<std::uint8_t> pixels_;
};

}  // namespace ogplay::core
