#include <doctest/doctest.h>

#include <array>
#include <ostream>

#include "ogplay/core/byte_order.h"
#include "ogplay/core/arithmetic.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/text.h"

TEST_CASE("core byte order and range helpers are overflow safe") {
    const std::array bytes{std::byte{0x78}, std::byte{0x56}, std::byte{0x34},
                           std::byte{0x12}};
    CHECK(ogplay::core::RangeFits(std::span{bytes}, 0U, 4U));
    CHECK_FALSE(ogplay::core::RangeFits(std::span{bytes}, 3U, 2U));
    CHECK(ogplay::core::ReadLittleEndian<std::uint32_t>(std::span{bytes}, 0U) ==
          0x12345678U);
    CHECK(ogplay::core::AlignUp(17U, 8U) == 24U);
    CHECK_FALSE(ogplay::core::AlignUp(UINT64_MAX, 8U).has_value());
}

TEST_CASE("core UTF helpers validate canonical text and preserve policies") {
    const std::string text = "A\xf0\x9f\x98\x80";
    const auto valid = ogplay::core::ValidateUtf8(text);
    CHECK(valid.IsValid());
    CHECK(valid.utf16_code_units == 3U);
    CHECK_FALSE(ogplay::core::IsValidUtf8("\xc0\x80"));
    CHECK(ogplay::core::TrimAsciiWhitespace(" \tvalue\v") == "value");

    const std::array utf16{char16_t{0xd800}, u'A'};
    CHECK_FALSE(ogplay::core::Utf16ToUtf8(
                    std::span{utf16}, ogplay::core::InvalidUtf16Policy::reject)
                    .has_value());
    CHECK(*ogplay::core::Utf16ToUtf8(
              std::span{utf16}, ogplay::core::InvalidUtf16Policy::replace, '?') ==
          "?A");
}

TEST_CASE("core Base64 and hex codecs expose policy without duplicating loops") {
    const std::array bytes{std::byte{'f'}, std::byte{'o'}, std::byte{'o'}};
    CHECK(ogplay::core::EncodeBase64(bytes) == "Zm9v");
    const std::array short_bytes{std::byte{0xfb}, std::byte{0xff}};
    CHECK(ogplay::core::EncodeBase64(
              short_bytes,
              {.alphabet = ogplay::core::Base64Alphabet::url_safe,
               .padding = false}) == "-_8");
    const auto decoded = ogplay::core::DecodeBase64(
        "-_8", {.alphabet = ogplay::core::Base64Alphabet::url_safe});
    REQUIRE(decoded.has_value());
    CHECK(*decoded == std::vector<std::byte>(short_bytes.begin(), short_bytes.end()));
    CHECK(ogplay::core::EncodeHex(short_bytes, ogplay::core::HexCase::upper) ==
          "FBFF");
    CHECK(ogplay::core::DecodeHex("fbFF") ==
          std::vector<std::byte>(short_bytes.begin(), short_bytes.end()));
}
