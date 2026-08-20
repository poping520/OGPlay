#include "ogplay/hal/from_chars.h"

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#include <xlocale.h>

namespace ogplay::hal {
namespace {

[[nodiscard]] bool IsAsciiSpace(const unsigned char unit) {
    return unit == ' ' || unit == '\t' || unit == '\n' || unit == '\r' ||
           unit == '\v' || unit == '\f';
}

template <typename Float>
[[nodiscard]] std::from_chars_result FromCharsApple(const char* first,
                                                    const char* last,
                                                    Float& value,
                                                    std::chars_format fmt) {
    if (first == nullptr || last == nullptr || first >= last) {
        return {first, std::errc::invalid_argument};
    }
    if (IsAsciiSpace(static_cast<unsigned char>(*first)) || *first == '+') {
        return {first, std::errc::invalid_argument};
    }

    const bool hex = fmt == std::chars_format::hex;
    if (!hex) {
        const char* digits = first;
        if (*digits == '-') ++digits;
        if (digits + 1 < last && *digits == '0' &&
            (digits[1] == 'x' || digits[1] == 'X')) {
            value = *first == '-' ? static_cast<Float>(-0.0)
                                  : static_cast<Float>(0.0);
            return {digits + 1, std::errc{}};
        }
    }

    std::string buffer;
    std::size_t prefix = 0;
    if (hex) {
        if (*first == '-') {
            buffer.assign("-0x");
            buffer.append(first + 1, last);
        } else {
            buffer.assign("0x");
            buffer.append(first, last);
        }
        prefix = 2;
    } else {
        buffer.assign(first, last);
    }

    char* end = nullptr;
    const int previous_errno = errno;
    errno = 0;
    Float parsed{};
    if constexpr (std::is_same_v<Float, float>) {
        parsed = strtof_l(buffer.c_str(), &end, LC_C_LOCALE);
    } else {
        parsed = strtod_l(buffer.c_str(), &end, LC_C_LOCALE);
    }
    const int parsed_errno = errno;
    errno = previous_errno;

    if (end == nullptr || end < buffer.data()) {
        return {first, std::errc::invalid_argument};
    }
    const auto consumed = static_cast<std::size_t>(end - buffer.data());
    if (consumed <= prefix) {
        return {first, std::errc::invalid_argument};
    }
    value = parsed;
    const char* ptr = first + (consumed - prefix);
    if (parsed_errno == ERANGE) {
        // strto*_l flags subnormals as underflow; from_chars accepts them.
        if (std::isfinite(parsed) && parsed != static_cast<Float>(0)) {
            return {ptr, std::errc{}};
        }
        return {ptr, std::errc::result_out_of_range};
    }
    return {ptr, std::errc{}};
}

}  // namespace

std::from_chars_result FromChars(const char* first, const char* last,
                                 float& value, std::chars_format fmt) {
    return FromCharsApple(first, last, value, fmt);
}

std::from_chars_result FromChars(const char* first, const char* last,
                                 double& value, std::chars_format fmt) {
    return FromCharsApple(first, last, value, fmt);
}

}  // namespace ogplay::hal
