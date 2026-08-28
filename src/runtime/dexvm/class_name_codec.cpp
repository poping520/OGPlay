#include "ogplay/runtime/dexvm/class_name_codec.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::runtime::dexvm {
namespace {

inline constexpr std::size_t kMaximumArrayDimensions = 255;
inline constexpr std::size_t kMaximumMethodParameterSlots = 255;

struct ParsedType final {
    std::string_view descriptor;
    bool primitive{};
    bool array{};
    std::size_t parameter_slots{1};
};

[[noreturn]] void Fail(const std::size_t offset, std::string message) {
    throw ClassNameCodecError(offset, std::move(message));
}

[[nodiscard]] bool IsPrimitiveMarker(const char marker) noexcept {
    switch (marker) {
    case 'Z':
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'J':
    case 'F':
    case 'D':
    case 'V': return true;
    default: return false;
    }
}

[[nodiscard]] bool IsValidBinaryClassName(const std::string_view name) {
    if (name.empty() || name.front() == '.' || name.back() == '.' ||
        name.find("..") != std::string_view::npos) {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](const char value) {
        return value == '/' || value == '[' || value == ';' || value == '(' ||
               value == ')';
    });
}

[[nodiscard]] ParsedType ParseType(const std::string_view input,
                                   std::size_t& offset,
                                   const bool allow_void) {
    const auto start = offset;
    std::size_t dimensions = 0;
    while (offset < input.size() && input[offset] == '[') {
        ++dimensions;
        ++offset;
        if (dimensions > kMaximumArrayDimensions) {
            Fail(start, "array descriptor exceeds 255 dimensions");
        }
    }
    if (offset >= input.size()) {
        Fail(offset, "descriptor ends before its element type");
    }

    bool primitive = false;
    std::size_t parameter_slots = 1;
    if (input[offset] == 'L') {
        const auto name_begin = ++offset;
        const auto terminator = input.find(';', name_begin);
        if (terminator == std::string_view::npos) {
            Fail(name_begin, "object descriptor has no terminator");
        }
        if (!IsValidJniObjectClassName(
                input.substr(name_begin, terminator - name_begin))) {
            Fail(name_begin, "object descriptor has an invalid class name");
        }
        offset = terminator + 1;
    } else {
        const auto marker = input[offset];
        if (!IsPrimitiveMarker(marker)) {
            Fail(offset, "unknown descriptor type marker");
        }
        primitive = dimensions == 0;
        ++offset;
        if (marker == 'V' && (!allow_void || dimensions != 0)) {
            Fail(start, "void is only valid as a non-array method result");
        }
        if (dimensions == 0 && (marker == 'J' || marker == 'D')) {
            parameter_slots = 2;
        } else if (marker == 'V') {
            parameter_slots = 0;
        }
    }

    return {
        input.substr(start, offset - start),
        primitive,
        dimensions != 0,
        parameter_slots,
    };
}

[[nodiscard]] ParsedType ParseWholeType(const std::string_view descriptor,
                                        const bool allow_void) {
    if (descriptor.empty()) Fail(0, "descriptor is empty");
    std::size_t offset = 0;
    auto parsed = ParseType(descriptor, offset, allow_void);
    if (offset != descriptor.size()) {
        Fail(offset, "descriptor has trailing bytes");
    }
    return parsed;
}

[[nodiscard]] std::string PrimitiveName(const char marker) {
    switch (marker) {
    case 'Z': return "boolean";
    case 'B': return "byte";
    case 'C': return "char";
    case 'S': return "short";
    case 'I': return "int";
    case 'J': return "long";
    case 'F': return "float";
    case 'D': return "double";
    case 'V': return "void";
    default: Fail(0, "unknown primitive descriptor type marker");
    }
}

[[nodiscard]] bool IsPrimitiveKeyword(const std::string_view name) noexcept {
    return name == "boolean" || name == "byte" || name == "char" ||
           name == "short" || name == "int" || name == "long" ||
           name == "float" || name == "double" || name == "void";
}

}  // namespace

ClassNameCodecError::ClassNameCodecError(const std::size_t offset,
                                         std::string message)
    : std::runtime_error(std::move(message)), offset_(offset) {}

MethodTypeDescriptor ClassNameCodec::ParseMethod(
    const std::string_view descriptor) {
    if (descriptor.empty() || descriptor.front() != '(') {
        Fail(0, "method descriptor must start with '('");
    }

    MethodTypeDescriptor method;
    std::size_t offset = 1;
    std::size_t parameter_slots = 0;
    while (offset < descriptor.size() && descriptor[offset] != ')') {
        const auto parameter = ParseType(descriptor, offset, false);
        parameter_slots += parameter.parameter_slots;
        if (parameter_slots > kMaximumMethodParameterSlots) {
            Fail(offset, "method descriptor exceeds 255 parameter slots");
        }
        method.parameters.emplace_back(parameter.descriptor);
    }
    if (offset >= descriptor.size()) {
        Fail(offset, "method descriptor has no closing ')'");
    }
    ++offset;
    if (offset >= descriptor.size()) {
        Fail(offset, "method descriptor has no result type");
    }
    method.return_type = ParseType(descriptor, offset, true).descriptor;
    if (offset != descriptor.size()) {
        Fail(offset, "method descriptor has trailing bytes");
    }
    return method;
}

bool ClassNameCodec::IsPrimitive(
    const std::string_view descriptor) noexcept {
    return descriptor.size() == 1 && IsPrimitiveMarker(descriptor.front());
}

bool ClassNameCodec::IsReference(
    const std::string_view descriptor) noexcept {
    try {
        const auto type = ParseWholeType(descriptor, false);
        return !type.primitive;
    } catch (const ClassNameCodecError&) {
        return false;
    }
}

bool ClassNameCodec::IsArray(const std::string_view descriptor) noexcept {
    try {
        return ParseWholeType(descriptor, false).array;
    } catch (const ClassNameCodecError&) {
        return false;
    }
}

std::string ClassNameCodec::ClassGetName(
    const std::string_view descriptor) {
    const auto type = ParseWholeType(descriptor, true);
    if (type.primitive) return PrimitiveName(descriptor.front());

    if (type.array) {
        auto name = std::string(descriptor);
        std::replace(name.begin(), name.end(), '/', '.');
        return name;
    }

    auto name = std::string(descriptor.substr(1, descriptor.size() - 2));
    std::replace(name.begin(), name.end(), '/', '.');
    return name;
}

std::string ClassNameCodec::BinaryNameToDescriptor(
    const std::string_view name) {
    if (name.empty()) Fail(0, "binary class name is empty");
    if (IsPrimitiveKeyword(name)) {
        Fail(0, "primitive keywords are not binary class names");
    }

    if (name.front() == '[') {
        std::size_t dimensions = 0;
        while (dimensions < name.size() && name[dimensions] == '[') {
            ++dimensions;
        }
        if (dimensions > kMaximumArrayDimensions) {
            Fail(0, "array binary name exceeds 255 dimensions");
        }
        if (dimensions >= name.size()) {
            Fail(dimensions, "array binary name has no element type");
        }

        const auto element = name.substr(dimensions);
        if (element.size() == 1 && IsPrimitiveMarker(element.front()) &&
            element.front() != 'V') {
            return std::string(name);
        }
        if (element.size() < 3 || element.front() != 'L' ||
            element.back() != ';') {
            Fail(dimensions, "array binary name has an invalid element type");
        }
        const auto class_name = element.substr(1, element.size() - 2);
        if (!IsValidBinaryClassName(class_name)) {
            Fail(dimensions + 1, "array binary name has an invalid class name");
        }
        auto descriptor = std::string(name);
        std::replace(descriptor.begin() + static_cast<std::ptrdiff_t>(dimensions + 1),
                     descriptor.end() - 1, '.', '/');
        return descriptor;
    }

    if (!IsValidBinaryClassName(name)) {
        Fail(0, "binary class name is invalid");
    }
    auto descriptor = std::string("L") + std::string(name) + ";";
    std::replace(descriptor.begin() + 1, descriptor.end() - 1, '.', '/');
    return descriptor;
}

}  // namespace ogplay::runtime::dexvm
