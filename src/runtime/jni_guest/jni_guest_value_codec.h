#pragma once

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"

namespace ogplay::runtime::jni_guest_detail {

enum class VoidResultPolicy { allow, reject };

[[nodiscard]] inline JniGuestCallResult EncodeValueResult(
    const JniValue& value, const JniTypeKind kind,
    const VoidResultPolicy void_policy,
    const std::string_view invalid_type_error) {
    const auto word = [](const std::uint32_t result) {
        return JniGuestCallResult{JniGuestReturnWidth::word, {result, 0U}};
    };
    const auto pair = [](const std::uint64_t result) {
        return JniGuestCallResult{
            JniGuestReturnWidth::double_word,
            {static_cast<std::uint32_t>(result),
             static_cast<std::uint32_t>(result >> 32U)}};
    };
    switch (kind) {
    case JniTypeKind::object:
    case JniTypeKind::array:
        return word(std::get<JniReference>(value).Value());
    case JniTypeKind::boolean: return word(std::get<JniBoolean>(value));
    case JniTypeKind::byte:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniByte>(value))));
    case JniTypeKind::character: return word(std::get<JniChar>(value));
    case JniTypeKind::short_integer:
        return word(std::bit_cast<std::uint32_t>(
            static_cast<JniInt>(std::get<JniShort>(value))));
    case JniTypeKind::integer:
        return word(std::bit_cast<std::uint32_t>(std::get<JniInt>(value)));
    case JniTypeKind::long_integer:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniLong>(value)));
    case JniTypeKind::float_value:
        return word(std::bit_cast<std::uint32_t>(std::get<JniFloat>(value)));
    case JniTypeKind::double_value:
        return pair(std::bit_cast<std::uint64_t>(std::get<JniDouble>(value)));
    case JniTypeKind::void_value:
        if (void_policy == VoidResultPolicy::allow) {
            static_cast<void>(std::get<std::monostate>(value));
            return {};
        }
        break;
    }
    throw JniGuestBindingError(std::string(invalid_type_error));
}

}  // namespace ogplay::runtime::jni_guest_detail
