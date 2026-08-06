#include "ogplay/gles/gles_call_preparation.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>

#include "ogplay/gles/generated/gles2_catalog.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::gles {
namespace {

namespace catalog = generated::gles2;

[[nodiscard]] std::uint64_t CheckedMultiply(const std::uint64_t left,
                                            const std::uint64_t right) {
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left) {
        throw GlesCallPreparationError("GLES pointer length multiplication overflow");
    }
    return left * right;
}

[[nodiscard]] bool IsSignedCountType(const std::string_view type) noexcept {
    return type == "GLint" || type == "GLsizei" || type == "GLintptr" ||
           type == "GLsizeiptr";
}

[[nodiscard]] std::uint64_t ScalarValue(
    const catalog::FunctionSpec& function,
    const std::span<const GlesGuestValue> arguments,
    const std::string_view name) {
    for (std::size_t index = 0; index < function.parameter_count; ++index) {
        const auto& parameter =
            catalog::kParameters[function.parameter_offset + index];
        if (parameter.name != name) {
            continue;
        }
        if (parameter.indirection != 0) {
            throw GlesCallPreparationError(
                "GLES length expression references a pointer parameter");
        }
        const auto value = arguments[index];
        if (IsSignedCountType(parameter.type) &&
            static_cast<std::int32_t>(value) < 0) {
            throw GlesCallPreparationError(
                "GLES length expression references a negative parameter");
        }
        return value;
    }
    throw GlesCallPreparationError(
        "GLES length expression references an unknown parameter");
}

[[nodiscard]] bool IsIdentifier(const std::string_view value) noexcept {
    if (value.empty() ||
        !(value.front() == '_' ||
          (value.front() >= 'A' && value.front() <= 'Z') ||
          (value.front() >= 'a' && value.front() <= 'z'))) {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return character == '_' ||
               (character >= 'A' && character <= 'Z') ||
               (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9');
    });
}

[[nodiscard]] std::optional<std::uint64_t> ParseUnsigned(
    const std::string_view value) noexcept {
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                        result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] GlesLengthResolution EvaluateLength(
    const catalog::FunctionSpec& function,
    const catalog::ParameterSpec& parameter,
    const std::span<const GlesGuestValue> arguments,
    const GlesLengthResolver* resolver) {
    if (const auto literal = ParseUnsigned(parameter.count)) {
        return {.element_count = *literal};
    }
    if (IsIdentifier(parameter.count)) {
        return {.element_count = ScalarValue(function, arguments,
                                             parameter.count)};
    }
    const auto multiply = parameter.count.find('*');
    if (multiply != std::string_view::npos &&
        parameter.count.find('*', multiply + 1) == std::string_view::npos) {
        const auto name = parameter.count.substr(0, multiply);
        const auto factor = ParseUnsigned(parameter.count.substr(multiply + 1));
        if (IsIdentifier(name) && factor) {
            return {.element_count = CheckedMultiply(
                        ScalarValue(function, arguments, name), *factor)};
        }
    }
    if (resolver != nullptr) {
        if (const auto resolved = resolver->Resolve(
                {.function_name = function.name,
                 .parameter_name = parameter.name,
                 .expression = parameter.count,
                 .arguments = arguments})) {
            return *resolved;
        }
    }
    throw GlesCallPreparationError(
        "GLES pointer length requires an explicit resolver");
}

[[nodiscard]] std::uint64_t ElementSize(
    const catalog::ParameterSpec& parameter) {
    if (parameter.indirection >= 2) {
        return sizeof(GlesGuestValue);
    }
    if (parameter.type == "GLboolean" || parameter.type == "GLbyte" ||
        parameter.type == "GLchar" || parameter.type == "GLubyte" ||
        parameter.type == "void") {
        return 1;
    }
    if (parameter.type == "GLshort" || parameter.type == "GLushort") {
        return 2;
    }
    constexpr std::array<std::string_view, 9> kFourByteTypes{
        "GLbitfield", "GLclampf", "GLenum", "GLfloat", "GLint",
        "GLintptr", "GLsizei", "GLsizeiptr", "GLuint"};
    if (std::ranges::find(kFourByteTypes, parameter.type) !=
        kFourByteTypes.end()) {
        return 4;
    }
    throw GlesCallPreparationError("GLES pointer has an unknown element type");
}

[[nodiscard]] GuestTransferDirection DirectionOf(
    const std::string_view direction) {
    if (direction == "in") {
        return GuestTransferDirection::input;
    }
    if (direction == "out") {
        return GuestTransferDirection::output;
    }
    if (direction == "inout") {
        return GuestTransferDirection::input_output;
    }
    throw GlesCallPreparationError("GLES pointer has an unknown direction");
}

[[nodiscard]] std::uint64_t CStringSize(
    memory::AddressSpace& memory, const memory::GuestAddress address,
    const std::uint64_t thread_id, const std::uint64_t size_limit) {
    if (address.IsNull()) {
        throw GlesCallPreparationError("required GLES string pointer is null");
    }
    for (std::uint64_t offset = 0; offset < size_limit; ++offset) {
        std::byte value{};
        memory.Read(address.Add(offset), std::span(&value, 1), thread_id);
        if (value == std::byte{}) {
            return offset + 1;
        }
    }
    throw GlesCallPreparationError(
        "GLES string is not terminated within the transfer limit");
}

}  // namespace

PreparedGlesCall PrepareGles2Call(
    memory::AddressSpace& memory, const GlesThunkId id,
    const std::span<const GlesGuestValue> arguments,
    const std::uint64_t thread_id, const GlesLengthResolver* resolver,
    const std::uint64_t size_limit) {
    const auto info = GlesDispatchTable::Describe(id);
    if (arguments.size() != info.parameter_count) {
        throw GlesCallPreparationError(
            "GLES call preparation received the wrong argument count");
    }
    const auto& function = catalog::kFunctions[static_cast<std::size_t>(id)];
    PreparedGlesCall prepared{.id = id};
    prepared.pointers.reserve(info.pointer_parameter_count);
    for (std::size_t index = 0; index < function.parameter_count; ++index) {
        const auto& parameter =
            catalog::kParameters[function.parameter_offset + index];
        if (parameter.indirection == 0) {
            continue;
        }
        const memory::GuestAddress address(arguments[index]);
        GlesLengthResolution length{};
        if (parameter.count == "cstring") {
            length.element_count =
                CStringSize(memory, address, thread_id, size_limit);
        } else {
            length = EvaluateLength(function, parameter, arguments, resolver);
        }
        const auto byte_size =
            CheckedMultiply(length.element_count, ElementSize(parameter));
        if (!address.IsNull() && byte_size > size_limit) {
            throw GlesCallPreparationError(
                "GLES pointer exceeds the configured transfer limit");
        }
        const auto direction = DirectionOf(parameter.direction);
        PreparedGlesPointer pointer{.parameter_index = index,
                                    .guest_address = address,
                                    .byte_size = byte_size,
                                    .direction = direction,
                                    .deferred =
                                        length.disposition ==
                                        GlesLengthDisposition::deferred};
        if (!pointer.deferred) {
            pointer.transfer.emplace(GuestBuffer::Prepare(
                memory, address, byte_size, direction, parameter.nullable,
                thread_id, size_limit));
        }
        prepared.pointers.push_back(std::move(pointer));
    }
    return prepared;
}

}  // namespace ogplay::gles
