#include "ogplay/app/encoding.h"
#include "ogplay/core/encoding.h"
namespace ogplay::app {
std::string EncodeBase64(std::span<const std::byte> input, Base64EncodeOptions options) {
    return core::EncodeBase64(input, {static_cast<core::Base64Alphabet>(options.alphabet), options.padding, options.line_length, options.newline});
}
std::optional<std::vector<std::byte>> DecodeBase64(std::string_view input, Base64DecodeOptions options) {
    return core::DecodeBase64(input, {static_cast<core::Base64Alphabet>(options.alphabet), options.ignore_ascii_whitespace});
}
char HexDigit(std::uint8_t nibble, HexCase letter_case) noexcept { return core::HexDigit(nibble, static_cast<core::HexCase>(letter_case)); }
std::optional<std::uint8_t> ParseHexDigit(char digit) noexcept { return core::ParseHexDigit(digit); }
std::string EncodeHex(std::span<const std::byte> input, HexCase letter_case) { return core::EncodeHex(input, static_cast<core::HexCase>(letter_case)); }
std::optional<std::vector<std::byte>> DecodeHex(std::string_view input) { return core::DecodeHex(input); }
}
