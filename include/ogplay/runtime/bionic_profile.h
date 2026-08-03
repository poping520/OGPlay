#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace ogplay::runtime {

enum class AndroidApi : std::uint8_t { api19 = 19, api22 = 22, api23 = 23 };

enum class BionicSymbolRoute : std::uint8_t {
    guest_execution,
    host_intercept,
    host_boundary,
};

struct BionicProfile final {
    AndroidApi api{};
    std::string_view android_release;
    std::string_view data_directory;
    std::span<const std::string_view> guest_libraries;
    std::span<const std::string_view> boundary_libraries;
};

class BionicProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] const BionicProfile& SelectBionicProfile(std::uint32_t api);
[[nodiscard]] BionicSymbolRoute RouteBionicSymbol(
    const BionicProfile& profile, std::string_view library,
    std::string_view symbol);

}  // namespace ogplay::runtime
