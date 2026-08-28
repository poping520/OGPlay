#include "ogplay/core/text.h"

#include <algorithm>
#include <cctype>
#include <type_traits>

namespace ogplay::core {
namespace {

[[nodiscard]] constexpr bool IsAsciiWhitespace(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\f' || value == '\v';
}

template <typename Unit>
[[nodiscard]] std::optional<std::string> ConvertUtf16(
    const std::span<const Unit> text, const InvalidUtf16Policy policy,
    const std::uint32_t replacement) {
    static_assert(sizeof(Unit) == sizeof(std::uint16_t));
    if (policy == InvalidUtf16Policy::replace &&
        (replacement > 0x10ffffU ||
         (replacement >= 0xd800U && replacement <= 0xdfffU))) {
        return std::nullopt;
    }
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        std::uint32_t code_point = static_cast<std::uint16_t>(text[index]);
        if (code_point >= 0xd800U && code_point <= 0xdbffU &&
            index + 1U < text.size()) {
            const auto low = static_cast<std::uint16_t>(text[index + 1U]);
            if (low >= 0xdc00U && low <= 0xdfffU) {
                code_point = 0x10000U + ((code_point - 0xd800U) << 10U) +
                             (low - 0xdc00U);
                ++index;
            } else if (policy == InvalidUtf16Policy::replace) {
                code_point = replacement;
            } else {
                return std::nullopt;
            }
        } else if (code_point >= 0xd800U && code_point <= 0xdfffU) {
            if (policy == InvalidUtf16Policy::reject) return std::nullopt;
            code_point = replacement;
        }
        if (!AppendUtf8(result, code_point)) return std::nullopt;
    }
    return result;
}

}  // namespace

std::string_view TrimAsciiWhitespace(const std::string_view text) noexcept {
    std::size_t begin{};
    while (begin < text.size() && IsAsciiWhitespace(text[begin])) ++begin;
    auto end = text.size();
    while (end > begin && IsAsciiWhitespace(text[end - 1U])) --end;
    return text.substr(begin, end - begin);
}

Utf8Validation ValidateUtf8(const std::string_view text) noexcept {
    Utf8Validation result;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        std::uint32_t code_point{};
        std::size_t count{};
        if (first <= 0x7fU) {
            code_point = first;
            count = 1;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            code_point = first & 0x1fU;
            count = 2;
        } else if (first >= 0xe0U && first <= 0xefU) {
            code_point = first & 0x0fU;
            count = 3;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = first & 0x07U;
            count = 4;
        } else {
            result.error = Utf8Error::invalid_lead;
            return result;
        }
        if (count > text.size() - index) {
            result.error = Utf8Error::truncated;
            return result;
        }
        for (std::size_t continuation = 1; continuation < count; ++continuation) {
            const auto byte = static_cast<std::uint8_t>(text[index + continuation]);
            if ((byte & 0xc0U) != 0x80U) {
                result.error = Utf8Error::invalid_continuation;
                return result;
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        const auto minimum = count == 1U ? 0U : count == 2U ? 0x80U
                                               : count == 3U ? 0x800U
                                                             : 0x10000U;
        if (code_point < minimum || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            result.error = Utf8Error::non_canonical;
            return result;
        }
        result.utf16_code_units += code_point > 0xffffU ? 2U : 1U;
        index += count;
    }
    return result;
}

bool IsValidUtf8(const std::string_view text) noexcept {
    return ValidateUtf8(text).IsValid();
}

bool AppendUtf8(std::string& output, const std::uint32_t code_point) {
    if (code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
        return false;
    }
    if (code_point <= 0x7fU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if (code_point <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
    return true;
}

std::optional<std::string> Utf16ToUtf8(
    const std::span<const char16_t> text, const InvalidUtf16Policy policy,
    const std::uint32_t replacement) {
    return ConvertUtf16(text, policy, replacement);
}

std::optional<std::string> Utf16ToUtf8(
    const std::span<const std::uint16_t> text, const InvalidUtf16Policy policy,
    const std::uint32_t replacement) {
    return ConvertUtf16(text, policy, replacement);
}

bool IsValidPackageName(const std::string_view package) {
    bool component_start = true;
    std::size_t components = 1;
    for (const char character : package) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '.') {
            if (component_start) return false;
            component_start = true;
            ++components;
        } else if (component_start) {
            if (std::isalpha(byte) == 0) return false;
            component_start = false;
        } else if (std::isalnum(byte) == 0 && character != '_') {
            return false;
        }
    }
    return !component_start && components >= 2;
}

bool IsValidLowercaseIdentifier(const std::string_view value) {
    if (value.empty() ||
        std::islower(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::islower(byte) != 0 || std::isdigit(byte) != 0 ||
               character == '_';
    });
}

}  // namespace ogplay::core
