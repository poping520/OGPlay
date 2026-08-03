#pragma once

#include <cstdint>

namespace ogplay::hal {

struct DrawableSize final {
    std::uint32_t width{};
    std::uint32_t height{};

    bool operator==(const DrawableSize&) const = default;
};

class GraphicsPresenter {
public:
    virtual ~GraphicsPresenter() = default;
    [[nodiscard]] virtual DrawableSize Size() const = 0;
    virtual void Resize(DrawableSize size) = 0;
    virtual void Present() = 0;
};

}  // namespace ogplay::hal
