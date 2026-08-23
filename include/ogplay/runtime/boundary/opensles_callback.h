#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ogplay::runtime {

struct OpenSlesGuestCallback final {
    std::uint32_t function{};
    std::array<std::uint32_t, 6> arguments{};
    std::uint8_t argument_count{};
};

struct OpenSlesCallbackSink final {
    void* owner{};
    void (*enqueue)(void*, const OpenSlesGuestCallback&){};

    void Enqueue(const OpenSlesGuestCallback& callback) const {
        if (enqueue != nullptr) enqueue(owner, callback);
    }
};

}  // namespace ogplay::runtime
