#pragma once

#include <cstdlib>
#include <string_view>

namespace ogplay::tests {

inline bool M4ExitVerificationRequired() {
#if defined(_WIN32)
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, "OGPLAY_REQUIRE_M4_EXIT") != 0 || value == nullptr) {
        return false;
    }
    const bool required = std::string_view{value} == "1";
    std::free(value);
    return required;
#else
    const auto* value = std::getenv("OGPLAY_REQUIRE_M4_EXIT");
    return value != nullptr && std::string_view{value} == "1";
#endif
}

}  // namespace ogplay::tests
