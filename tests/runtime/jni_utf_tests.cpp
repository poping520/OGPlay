#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_utf.h"

namespace {

using Bytes = std::vector<std::uint8_t>;
using Utf16 = std::vector<ogplay::runtime::JniChar>;

[[nodiscard]] Bytes BytesOf(
    const std::initializer_list<std::uint8_t> values) {
    return values;
}

}  // namespace

TEST_CASE("JNI modified UTF-8 round trips ASCII NUL and non-ASCII text") {
    const Utf16 input{'O', 'G', 0, 0x4E2D, 0x6587};
    const auto encoded = ogplay::runtime::EncodeJniModifiedUtf8(input);
    CHECK(encoded == BytesOf(
                         {'O', 'G', 0xC0, 0x80, 0xE4, 0xB8, 0xAD,
                          0xE6, 0x96, 0x87}));
    CHECK(ogplay::runtime::JniModifiedUtf8Length(input) == encoded.size());
    CHECK(ogplay::runtime::DecodeJniModifiedUtf8(encoded) == input);
}

TEST_CASE("JNI modified UTF-8 preserves UTF-16 surrogate code units") {
    const Utf16 smile{0xD83D, 0xDE00};
    const auto encoded = ogplay::runtime::EncodeJniModifiedUtf8(smile);
    CHECK(encoded ==
          BytesOf({0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80}));
    CHECK(ogplay::runtime::DecodeJniModifiedUtf8(encoded) == smile);

    const Utf16 unpaired{0xD83D};
    CHECK(ogplay::runtime::DecodeJniModifiedUtf8(
              ogplay::runtime::EncodeJniModifiedUtf8(unpaired)) == unpaired);
}

TEST_CASE("JNI modified UTF-8 rejects malformed and standard four-byte forms") {
    const auto decode = [](const Bytes& bytes) {
        static_cast<void>(ogplay::runtime::DecodeJniModifiedUtf8(bytes));
    };
    CHECK_THROWS_AS(decode(BytesOf({0x00})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xC0, 0x81})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xC1, 0xBF})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xE0, 0x80, 0x80})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xC2})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xE4, 0x20, 0x80})),
                    ogplay::runtime::JniModifiedUtf8Error);
    CHECK_THROWS_AS(decode(BytesOf({0xF0, 0x9F, 0x98, 0x80})),
                    ogplay::runtime::JniModifiedUtf8Error);
}
