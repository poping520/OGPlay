#pragma once
#include <cstddef>
#include <memory>

namespace ogplay::runtime {
// One immutable process device catalog. Neither Java nor NDK probes hardware.
class AndroidSensorDirectory {
public:
    virtual ~AndroidSensorDirectory() = default;
    [[nodiscard]] virtual std::size_t Count() const noexcept { return 0; }
};
} // namespace ogplay::runtime
