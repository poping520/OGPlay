#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime {

class JniModifiedUtf8Error final : public std::runtime_error {
public:
    JniModifiedUtf8Error(std::size_t offset, const char* message);

    [[nodiscard]] std::size_t Offset() const noexcept;

private:
    std::size_t offset_{};
};

// The encoded payload does not contain the trailing NUL required by JNI C APIs.
[[nodiscard]] std::size_t JniModifiedUtf8Length(
    std::span<const JniChar> utf16);
[[nodiscard]] std::vector<std::uint8_t> EncodeJniModifiedUtf8(
    std::span<const JniChar> utf16);
[[nodiscard]] std::vector<JniChar> DecodeJniModifiedUtf8(
    std::span<const std::uint8_t> encoded);

}  // namespace ogplay::runtime
