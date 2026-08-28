#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ogplay::core {

enum class Utf8Error {
    none,
    invalid_lead,
    truncated,
    invalid_continuation,
    non_canonical,
};

struct Utf8Validation final {
    Utf8Error error{Utf8Error::none};
    std::size_t utf16_code_units{};

    [[nodiscard]] bool IsValid() const noexcept {
        return error == Utf8Error::none;
    }
};

enum class InvalidUtf16Policy {
    reject,
    replace,
};

[[nodiscard]] std::string_view TrimAsciiWhitespace(std::string_view text) noexcept;
[[nodiscard]] Utf8Validation ValidateUtf8(std::string_view text) noexcept;
[[nodiscard]] bool IsValidUtf8(std::string_view text) noexcept;
[[nodiscard]] bool AppendUtf8(std::string& output, std::uint32_t code_point);

[[nodiscard]] std::optional<std::string> Utf16ToUtf8(
    std::span<const char16_t> text, InvalidUtf16Policy policy,
    std::uint32_t replacement = 0xfffdU);
[[nodiscard]] std::optional<std::string> Utf16ToUtf8(
    std::span<const std::uint16_t> text, InvalidUtf16Policy policy,
    std::uint32_t replacement = 0xfffdU);

}  // namespace ogplay::core
