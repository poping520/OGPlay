#include "ogplay/core/encoding.h"

#include <algorithm>
#include <array>

namespace ogplay::core {
namespace {

[[nodiscard]] constexpr std::string_view Alphabet(
    const Base64Alphabet alphabet) noexcept {
    return alphabet == Base64Alphabet::url_safe
               ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
               : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

[[nodiscard]] constexpr bool IsAsciiWhitespace(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

}  // namespace

std::string EncodeBase64(const std::span<const std::byte> input,
                         const Base64EncodeOptions options) {
    const auto alphabet = Alphabet(options.alphabet);
    std::string raw;
    raw.reserve(((input.size() + 2U) / 3U) * 4U);
    for (std::size_t offset = 0; offset < input.size(); offset += 3U) {
        const auto remaining = input.size() - offset;
        const auto a = std::to_integer<std::uint32_t>(input[offset]);
        const auto b = remaining > 1U
                           ? std::to_integer<std::uint32_t>(input[offset + 1U])
                           : 0U;
        const auto c = remaining > 2U
                           ? std::to_integer<std::uint32_t>(input[offset + 2U])
                           : 0U;
        const auto word = (a << 16U) | (b << 8U) | c;
        raw.push_back(alphabet[(word >> 18U) & 0x3fU]);
        raw.push_back(alphabet[(word >> 12U) & 0x3fU]);
        if (remaining > 1U) raw.push_back(alphabet[(word >> 6U) & 0x3fU]);
        else if (options.padding) raw.push_back('=');
        if (remaining > 2U) raw.push_back(alphabet[word & 0x3fU]);
        else if (options.padding) raw.push_back('=');
    }
    if (options.line_length == 0U || raw.empty()) return raw;
    std::string wrapped;
    for (std::size_t offset = 0; offset < raw.size(); offset += options.line_length) {
        wrapped.append(raw, offset,
                       std::min(options.line_length, raw.size() - offset));
        wrapped.append(options.newline);
    }
    return wrapped;
}

std::optional<std::vector<std::byte>> DecodeBase64(
    const std::string_view input, const Base64DecodeOptions options) {
    std::array<std::int16_t, 256> decode{};
    decode.fill(-1);
    const auto alphabet = Alphabet(options.alphabet);
    for (std::size_t index = 0; index < alphabet.size(); ++index) {
        decode[static_cast<unsigned char>(alphabet[index])] =
            static_cast<std::int16_t>(index);
    }
    std::vector<std::byte> output;
    std::uint32_t accumulator{};
    std::int32_t bits{};
    bool padded{};
    for (const char character : input) {
        if (options.ignore_ascii_whitespace && IsAsciiWhitespace(character)) continue;
        if (character == '=') {
            padded = true;
            continue;
        }
        const auto value = decode[static_cast<unsigned char>(character)];
        if (padded || value < 0) return std::nullopt;
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::byte>(
                (accumulator >> static_cast<std::uint32_t>(bits)) & 0xffU));
        }
    }
    if (bits == 6 ||
        (bits != 0 && (accumulator & ((1U << bits) - 1U)) != 0U)) {
        return std::nullopt;
    }
    return output;
}

char HexDigit(const std::uint8_t nibble, const HexCase letter_case) noexcept {
    constexpr std::string_view lower = "0123456789abcdef";
    constexpr std::string_view upper = "0123456789ABCDEF";
    return (letter_case == HexCase::upper ? upper : lower)[nibble & 0x0fU];
}

std::optional<std::uint8_t> ParseHexDigit(const char digit) noexcept {
    if (digit >= '0' && digit <= '9') return static_cast<std::uint8_t>(digit - '0');
    if (digit >= 'a' && digit <= 'f') return static_cast<std::uint8_t>(digit - 'a' + 10);
    if (digit >= 'A' && digit <= 'F') return static_cast<std::uint8_t>(digit - 'A' + 10);
    return std::nullopt;
}

std::string EncodeHex(const std::span<const std::byte> input,
                      const HexCase letter_case) {
    std::string output;
    output.reserve(input.size() * 2U);
    for (const auto byte : input) {
        const auto value = std::to_integer<std::uint8_t>(byte);
        output.push_back(HexDigit(value >> 4U, letter_case));
        output.push_back(HexDigit(value, letter_case));
    }
    return output;
}

std::optional<std::vector<std::byte>> DecodeHex(const std::string_view input) {
    if ((input.size() & 1U) != 0U) return std::nullopt;
    std::vector<std::byte> output(input.size() / 2U);
    for (std::size_t index = 0; index < output.size(); ++index) {
        const auto high = ParseHexDigit(input[index * 2U]);
        const auto low = ParseHexDigit(input[index * 2U + 1U]);
        if (!high.has_value() || !low.has_value()) return std::nullopt;
        output[index] = static_cast<std::byte>((*high << 4U) | *low);
    }
    return output;
}

}  // namespace ogplay::core
