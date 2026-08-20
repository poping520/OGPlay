#pragma once

// Locale-free floating from_chars. Apple's libc++ only exports the dylib
// symbol from macOS 16, so the macOS HAL uses strtof_l/strtod_l; other
// hosts call std::from_chars. The result contract matches std::from_chars
// for general and hex formats.

#include <charconv>

namespace ogplay::hal {

[[nodiscard]] std::from_chars_result FromChars(
    const char* first, const char* last, float& value,
    std::chars_format fmt = std::chars_format::general);

[[nodiscard]] std::from_chars_result FromChars(
    const char* first, const char* last, double& value,
    std::chars_format fmt = std::chars_format::general);

}  // namespace ogplay::hal
