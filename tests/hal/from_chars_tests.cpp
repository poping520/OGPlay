#include "ogplay/hal/from_chars.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include <doctest/doctest.h>

namespace {

using ogplay::hal::FromChars;

template <typename Float>
[[nodiscard]] bool FullyParsed(const std::string_view text, const Float expected) {
    Float value{};
    const auto result =
        FromChars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size() && value == expected;
}

}  // namespace

TEST_CASE("FromChars parses decimal floats without a locale") {
    CHECK(FullyParsed<float>("1.5", 1.5F));
    CHECK(FullyParsed<double>("-2.5e1", -25.0));
    CHECK(FullyParsed<float>("0", 0.0F));
}

TEST_CASE("FromChars rejects leading space and plus like std::from_chars") {
    float value = 7.0F;
    const std::string_view spaced = " 1.5";
    auto result = FromChars(spaced.data(), spaced.data() + spaced.size(), value);
    CHECK(result.ec == std::errc::invalid_argument);
    CHECK(result.ptr == spaced.data());
    CHECK(value == 7.0F);

    const std::string_view plus = "+1.5";
    result = FromChars(plus.data(), plus.data() + plus.size(), value);
    CHECK(result.ec == std::errc::invalid_argument);
    CHECK(result.ptr == plus.data());
}

TEST_CASE("FromChars general format stops before a 0x hex prefix") {
    float value{};
    const std::string_view text = "0x1.0p0";
    const auto result =
        FromChars(text.data(), text.data() + text.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(result.ptr == text.data() + 1);
    CHECK(value == 0.0F);
}

TEST_CASE("FromChars hex format parses significand without a 0x prefix") {
    float value{};
    const std::string_view hex = "1.0p0";
    auto result = FromChars(hex.data(), hex.data() + hex.size(), value,
                            std::chars_format::hex);
    CHECK(result.ec == std::errc{});
    CHECK(result.ptr == hex.data() + hex.size());
    CHECK(value == 1.0F);

    double wide{};
    const std::string_view negative = "-1.8p1";
    result = FromChars(negative.data(), negative.data() + negative.size(), wide,
                       std::chars_format::hex);
    CHECK(result.ec == std::errc{});
    CHECK(result.ptr == negative.data() + negative.size());
    CHECK(wide == -3.0);
}

TEST_CASE("FromChars reports overflow and leftover characters") {
    float value{};
    const std::string_view huge = "1e40";
    auto result = FromChars(huge.data(), huge.data() + huge.size(), value);
    CHECK(result.ec == std::errc::result_out_of_range);
    CHECK(std::isinf(value));

    const std::string_view leftover = "1.5x";
    result = FromChars(leftover.data(), leftover.data() + leftover.size(), value);
    CHECK(result.ec == std::errc{});
    CHECK(result.ptr == leftover.data() + 3);
    CHECK(value == 1.5F);
}

TEST_CASE("FromChars round-trips shortest to_chars decimals") {
    const std::array<float, 5> cases{
        0.123456789F, 0.0F, -0.0F, std::numeric_limits<float>::min(),
        std::numeric_limits<float>::denorm_min()};
    for (const auto original : cases) {
        std::array<char, 64> buffer{};
        const auto rendered = std::to_chars(
            buffer.data(), buffer.data() + buffer.size(), original,
            std::chars_format::general);
        REQUIRE(rendered.ec == std::errc{});
        float parsed{};
        const auto result = FromChars(buffer.data(), rendered.ptr, parsed);
        REQUIRE(result.ec == std::errc{});
        REQUIRE(result.ptr == rendered.ptr);
        CHECK(std::bit_cast<std::uint32_t>(parsed) ==
              std::bit_cast<std::uint32_t>(original));
    }
}
