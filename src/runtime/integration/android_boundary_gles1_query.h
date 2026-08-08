#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime::detail {

class AndroidBoundaryGles1QueryStrings final {
public:
    explicit AndroidBoundaryGles1QueryStrings(memory::AddressSpace& address_space);

    void Validate(std::uint32_t parameter) const;
    [[nodiscard]] std::uint32_t Publish(std::uint32_t parameter,
                                        std::string_view value,
                                        std::uint64_t thread_id);

private:
    memory::AddressSpace* address_space_{};
    bool region_mapped_{};
};

using AndroidBoundaryGles1StringResolver =
    std::function<std::string(std::uint32_t parameter)>;

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string);

}  // namespace ogplay::runtime::detail
