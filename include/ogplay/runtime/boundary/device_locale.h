#pragma once

#include <array>
#include <stdexcept>
#include <string_view>

namespace ogplay::runtime {

// A validated projection of the session's device facts, never host locale.
struct DeviceLocale final {
    std::array<char, 2> language;
    std::array<char, 2> region;

    DeviceLocale(std::string_view language_code, std::string_view region_code) {
        if (language_code.size() != 2 || region_code.size() != 2 || language_code[0] < 'a' || language_code[0] > 'z' ||
            language_code[1] < 'a' || language_code[1] > 'z' || region_code[0] < 'A' || region_code[0] > 'Z' ||
            region_code[1] < 'A' || region_code[1] > 'Z')
            throw std::invalid_argument(
                "device locale requires a lowercase two-letter language and uppercase two-letter region");
        language = {language_code[0], language_code[1]};
        region = {region_code[0], region_code[1]};
    }
};

}  // namespace ogplay::runtime
