#include "ogplay/runtime/jni_utf.h"

#include <limits>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const std::size_t offset, const char* message) {
    throw JniModifiedUtf8Error(offset, message);
}

void AddChecked(std::size_t& total, const std::size_t amount) {
    if (amount > std::numeric_limits<std::size_t>::max() - total) {
        throw std::length_error("modified UTF-8 payload is too large");
    }
    total += amount;
}

[[nodiscard]] bool IsContinuation(const std::uint8_t value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

}  // namespace

JniModifiedUtf8Error::JniModifiedUtf8Error(const std::size_t offset,
                                           const char* message)
    : std::runtime_error(message), offset_(offset) {}

std::size_t JniModifiedUtf8Error::Offset() const noexcept {
    return offset_;
}

std::size_t JniModifiedUtf8Length(const std::span<const JniChar> utf16) {
    std::size_t length = 0;
    for (const auto code_unit : utf16) {
        if (code_unit != 0 && code_unit <= 0x7FU) {
            AddChecked(length, 1);
        } else if (code_unit <= 0x7FFU) {
            AddChecked(length, 2);
        } else {
            AddChecked(length, 3);
        }
    }
    return length;
}

std::vector<std::uint8_t> EncodeJniModifiedUtf8(
    const std::span<const JniChar> utf16) {
    std::vector<std::uint8_t> encoded;
    encoded.reserve(JniModifiedUtf8Length(utf16));
    for (const auto code_unit : utf16) {
        if (code_unit != 0 && code_unit <= 0x7FU) {
            encoded.push_back(static_cast<std::uint8_t>(code_unit));
        } else if (code_unit <= 0x7FFU) {
            encoded.push_back(
                static_cast<std::uint8_t>(0xC0U | (code_unit >> 6U)));
            encoded.push_back(
                static_cast<std::uint8_t>(0x80U | (code_unit & 0x3FU)));
        } else {
            encoded.push_back(
                static_cast<std::uint8_t>(0xE0U | (code_unit >> 12U)));
            encoded.push_back(static_cast<std::uint8_t>(
                0x80U | ((code_unit >> 6U) & 0x3FU)));
            encoded.push_back(
                static_cast<std::uint8_t>(0x80U | (code_unit & 0x3FU)));
        }
    }
    return encoded;
}

std::vector<JniChar> DecodeJniModifiedUtf8(
    const std::span<const std::uint8_t> encoded) {
    std::vector<JniChar> utf16;
    utf16.reserve(encoded.size());

    std::size_t offset = 0;
    while (offset < encoded.size()) {
        const auto first = encoded[offset];
        if (first >= 0x01U && first <= 0x7FU) {
            utf16.push_back(first);
            ++offset;
            continue;
        }
        if (first >= 0xC0U && first <= 0xDFU) {
            if (encoded.size() - offset < 2) {
                Fail(offset, "truncated modified UTF-8 sequence");
            }
            const auto second = encoded[offset + 1];
            if (!IsContinuation(second)) {
                Fail(offset + 1, "invalid modified UTF-8 continuation byte");
            }
            const auto code_unit = static_cast<JniChar>(
                ((first & 0x1FU) << 6U) | (second & 0x3FU));
            if (code_unit < 0x80U && code_unit != 0) {
                Fail(offset, "overlong modified UTF-8 sequence");
            }
            if (code_unit == 0 && (first != 0xC0U || second != 0x80U)) {
                Fail(offset, "invalid modified UTF-8 NUL encoding");
            }
            utf16.push_back(code_unit);
            offset += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (encoded.size() - offset < 3) {
                Fail(offset, "truncated modified UTF-8 sequence");
            }
            const auto second = encoded[offset + 1];
            const auto third = encoded[offset + 2];
            if (!IsContinuation(second)) {
                Fail(offset + 1, "invalid modified UTF-8 continuation byte");
            }
            if (!IsContinuation(third)) {
                Fail(offset + 2, "invalid modified UTF-8 continuation byte");
            }
            const auto code_unit = static_cast<JniChar>(
                ((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) |
                (third & 0x3FU));
            if (code_unit < 0x800U) {
                Fail(offset, "overlong modified UTF-8 sequence");
            }
            utf16.push_back(code_unit);
            offset += 3;
            continue;
        }
        if (first == 0) {
            Fail(offset, "raw NUL is not a modified UTF-8 payload byte");
        }
        Fail(offset, "invalid modified UTF-8 leading byte");
    }
    return utf16;
}

}  // namespace ogplay::runtime
