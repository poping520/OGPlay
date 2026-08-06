#include "ogplay/runtime/jni/jni_native_export.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(std::string message) {
    throw JniNativeExportError(std::move(message));
}

[[nodiscard]] std::uint32_t DecodeUtf8(const std::string_view value,
                                       std::size_t& offset) {
    const auto lead = static_cast<std::uint8_t>(value[offset++]);
    if (lead < 0x80U) return lead;
    std::size_t continuation_count{};
    std::uint32_t code_point{};
    if ((lead & 0xE0U) == 0xC0U) {
        continuation_count = 1;
        code_point = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
        continuation_count = 2;
        code_point = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
        continuation_count = 3;
        code_point = lead & 0x07U;
    } else {
        Fail("JNI native name contains invalid UTF-8");
    }
    if (continuation_count > value.size() - offset) {
        Fail("JNI native name contains truncated UTF-8");
    }
    for (std::size_t index = 0; index < continuation_count; ++index) {
        const auto byte = static_cast<std::uint8_t>(value[offset++]);
        if ((byte & 0xC0U) != 0x80U) {
            Fail("JNI native name contains invalid UTF-8 continuation");
        }
        code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    constexpr std::array<std::uint32_t, 4> kMinimum{0, 0x80U, 0x800U, 0x10000U};
    if (code_point < kMinimum[continuation_count] || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
        Fail("JNI native name contains non-scalar UTF-8");
    }
    return code_point;
}

void AppendEscapedCodeUnit(std::string& result, const std::uint16_t value) {
    constexpr std::string_view kHex = "0123456789abcdef";
    result += "_0";
    result.push_back(kHex[(value >> 12U) & 0xFU]);
    result.push_back(kHex[(value >> 8U) & 0xFU]);
    result.push_back(kHex[(value >> 4U) & 0xFU]);
    result.push_back(kHex[value & 0xFU]);
}

[[nodiscard]] std::string Mangle(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        const auto code_point = DecodeUtf8(value, offset);
        if ((code_point >= 'a' && code_point <= 'z') ||
            (code_point >= 'A' && code_point <= 'Z') ||
            (code_point >= '0' && code_point <= '9')) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point == '/') {
            result.push_back('_');
        } else if (code_point == '_') {
            result += "_1";
        } else if (code_point == ';') {
            result += "_2";
        } else if (code_point == '[') {
            result += "_3";
        } else if (code_point <= 0xFFFFU) {
            AppendEscapedCodeUnit(result, static_cast<std::uint16_t>(code_point));
        } else {
            const auto scalar = code_point - 0x10000U;
            AppendEscapedCodeUnit(
                result, static_cast<std::uint16_t>(0xD800U + (scalar >> 10U)));
            AppendEscapedCodeUnit(
                result, static_cast<std::uint16_t>(0xDC00U + (scalar & 0x3FFU)));
        }
    }
    return result;
}

void ValidateNames(const std::string_view class_name,
                   const std::string_view method_name) {
    if (class_name.empty() || class_name.front() == '/' || class_name.back() == '/' ||
        class_name.find("//") != std::string_view::npos ||
        class_name.find_first_of(".;[()") != std::string_view::npos) {
        Fail("JNI native class name is not a valid internal name");
    }
    if (method_name.empty() || method_name.find_first_of("/.;[()") !=
                                   std::string_view::npos) {
        Fail("JNI native method name is not a valid simple name");
    }
}

}  // namespace

JniNativeExportNames BuildJniNativeExportNames(
    const std::string_view class_name, const std::string_view method_name,
    const std::string_view method_descriptor) {
    ValidateNames(class_name, method_name);
    static_cast<void>(ParseJniMethodDescriptor(method_descriptor));
    const auto close = method_descriptor.find(')');
    if (close == std::string_view::npos) {
        throw JniNativeExportError("JNI native method descriptor has no parameter terminator");
    }
    auto short_name = std::string("Java_") + Mangle(class_name) + "_" +
                      Mangle(method_name);
    auto long_name = short_name + "__" + Mangle(method_descriptor.substr(1, close - 1));
    return {std::move(short_name), std::move(long_name)};
}

}  // namespace ogplay::runtime
