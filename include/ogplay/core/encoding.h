#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::core {

enum class Base64Alphabet { standard, url_safe };

struct Base64EncodeOptions final {
    Base64Alphabet alphabet{Base64Alphabet::standard};
    bool padding{true};
    std::size_t line_length{};
    std::string_view newline{"\n"};
};

struct Base64DecodeOptions final {
    Base64Alphabet alphabet{Base64Alphabet::standard};
    bool ignore_ascii_whitespace{true};
};

[[nodiscard]] std::string EncodeBase64(
    std::span<const std::byte> input,
    Base64EncodeOptions options = {});
[[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64(
    std::string_view input, Base64DecodeOptions options = {});

enum class HexCase { lower, upper };

[[nodiscard]] char HexDigit(std::uint8_t nibble, HexCase letter_case) noexcept;
[[nodiscard]] std::optional<std::uint8_t> ParseHexDigit(char digit) noexcept;
[[nodiscard]] std::string EncodeHex(std::span<const std::byte> input,
                                    HexCase letter_case = HexCase::lower);
[[nodiscard]] std::optional<std::vector<std::byte>> DecodeHex(
    std::string_view input);

}  // namespace ogplay::core
