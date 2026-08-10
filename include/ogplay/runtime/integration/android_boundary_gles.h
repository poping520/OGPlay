#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace ogplay::cpu {
class A32State;
}

namespace ogplay::gles {
class AngleFrame;
}

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class AndroidBoundaryGles final {
public:
    explicit AndroidBoundaryGles(memory::AddressSpace& address_space);
    ~AndroidBoundaryGles();
    AndroidBoundaryGles(const AndroidBoundaryGles&) = delete;
    AndroidBoundaryGles& operator=(const AndroidBoundaryGles&) = delete;

    [[nodiscard]] std::optional<std::uint32_t> Dispatch(
        std::string_view symbol, const std::array<std::uint32_t, 4>& arguments,
        const cpu::A32State& state, gles::AngleFrame* frame);
    [[nodiscard]] bool HasEnabledVertexAttribute() const noexcept;
    void Reset() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
