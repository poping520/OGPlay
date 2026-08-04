#include "ogplay/runtime/jni/jni_signature.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::runtime {
namespace {

inline constexpr std::size_t kMaximumArrayDimensions = 255;
inline constexpr std::size_t kMaximumMethodParameterSlots = 255;

[[noreturn]] void Fail(const std::size_t offset, std::string message) {
    throw JniSignatureError(offset, std::move(message));
}

[[nodiscard]] bool IsValidObjectClass(const std::string_view name) {
    if (name.empty() || name.front() == '/' || name.back() == '/' ||
        name.find("//") != std::string_view::npos) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](const char value) {
        return value == '.' || value == '[' || value == ';' || value == '(' ||
               value == ')';
    });
}

[[nodiscard]] JniTypeKind PrimitiveKind(const char marker,
                                        const std::size_t offset) {
    switch (marker) {
    case 'Z': return JniTypeKind::boolean;
    case 'B': return JniTypeKind::byte;
    case 'C': return JniTypeKind::character;
    case 'S': return JniTypeKind::short_integer;
    case 'I': return JniTypeKind::integer;
    case 'J': return JniTypeKind::long_integer;
    case 'F': return JniTypeKind::float_value;
    case 'D': return JniTypeKind::double_value;
    case 'V': return JniTypeKind::void_value;
    default: Fail(offset, "unknown JNI descriptor type marker");
    }
}

[[nodiscard]] JniTypeDescriptor ParseType(const std::string_view descriptor,
                                          std::size_t& offset,
                                          const bool allow_void) {
    const auto start = offset;
    std::size_t dimensions = 0;
    while (offset < descriptor.size() && descriptor[offset] == '[') {
        ++dimensions;
        ++offset;
        if (dimensions > kMaximumArrayDimensions) {
            Fail(start, "JNI array descriptor exceeds 255 dimensions");
        }
    }
    if (offset >= descriptor.size()) {
        Fail(offset, "JNI descriptor ends before its element type");
    }

    JniTypeKind element_kind{};
    std::string object_class;
    if (descriptor[offset] == 'L') {
        const auto name_begin = ++offset;
        const auto terminator = descriptor.find(';', name_begin);
        if (terminator == std::string_view::npos) {
            Fail(name_begin, "JNI object descriptor has no terminator");
        }
        const auto name = descriptor.substr(name_begin, terminator - name_begin);
        if (!IsValidObjectClass(name)) {
            Fail(name_begin, "JNI object descriptor has an invalid class name");
        }
        object_class = std::string(name);
        element_kind = JniTypeKind::object;
        offset = terminator + 1;
    } else {
        element_kind = PrimitiveKind(descriptor[offset], offset);
        ++offset;
    }

    if (element_kind == JniTypeKind::void_value &&
        (!allow_void || dimensions != 0)) {
        Fail(start, "void is only valid as a non-array method result");
    }
    return {
        dimensions == 0 ? element_kind : JniTypeKind::array,
        element_kind,
        static_cast<std::uint8_t>(dimensions),
        std::move(object_class),
    };
}

}  // namespace

bool JniTypeDescriptor::IsReference() const noexcept {
    return kind == JniTypeKind::object || kind == JniTypeKind::array;
}

std::size_t JniTypeDescriptor::ParameterSlots() const noexcept {
    if (kind == JniTypeKind::void_value) return 0;
    if (kind == JniTypeKind::long_integer || kind == JniTypeKind::double_value) {
        return 2;
    }
    return 1;
}

std::size_t JniMethodDescriptor::ParameterSlots() const noexcept {
    std::size_t slots = 0;
    for (const auto& parameter : parameters) {
        slots += parameter.ParameterSlots();
    }
    return slots;
}

JniSignatureError::JniSignatureError(const std::size_t offset,
                                     std::string message)
    : std::runtime_error(std::move(message)), offset_(offset) {}

JniTypeDescriptor ParseJniFieldDescriptor(const std::string_view descriptor) {
    if (descriptor.empty()) Fail(0, "JNI field descriptor is empty");
    std::size_t offset = 0;
    auto type = ParseType(descriptor, offset, false);
    if (offset != descriptor.size()) {
        Fail(offset, "JNI field descriptor has trailing bytes");
    }
    return type;
}

JniMethodDescriptor ParseJniMethodDescriptor(const std::string_view descriptor) {
    if (descriptor.empty() || descriptor.front() != '(') {
        Fail(0, "JNI method descriptor must start with '('");
    }
    std::size_t offset = 1;
    JniMethodDescriptor method;
    std::size_t parameter_slots = 0;
    while (offset < descriptor.size() && descriptor[offset] != ')') {
        auto parameter = ParseType(descriptor, offset, false);
        parameter_slots += parameter.ParameterSlots();
        if (parameter_slots > kMaximumMethodParameterSlots) {
            Fail(offset, "JNI method descriptor exceeds 255 parameter slots");
        }
        method.parameters.push_back(std::move(parameter));
    }
    if (offset >= descriptor.size()) {
        Fail(offset, "JNI method descriptor has no closing ')'");
    }
    ++offset;
    if (offset >= descriptor.size()) {
        Fail(offset, "JNI method descriptor has no result type");
    }
    method.result = ParseType(descriptor, offset, true);
    if (offset != descriptor.size()) {
        Fail(offset, "JNI method descriptor has trailing bytes");
    }
    return method;
}

}  // namespace ogplay::runtime
