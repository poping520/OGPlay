#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime {

enum class JniTypeKind : std::uint8_t {
    boolean,
    byte,
    character,
    short_integer,
    integer,
    long_integer,
    float_value,
    double_value,
    void_value,
    object,
    array,
};

struct JniTypeDescriptor final {
    JniTypeKind kind{JniTypeKind::void_value};
    JniTypeKind element_kind{JniTypeKind::void_value};
    std::uint8_t array_dimensions{};
    std::string object_class;

    [[nodiscard]] bool IsReference() const noexcept;
    [[nodiscard]] std::size_t ParameterSlots() const noexcept;

    bool operator==(const JniTypeDescriptor&) const = default;
};

struct JniMethodDescriptor final {
    std::vector<JniTypeDescriptor> parameters;
    JniTypeDescriptor result;

    [[nodiscard]] std::size_t ParameterSlots() const noexcept;

    bool operator==(const JniMethodDescriptor&) const = default;
};

class JniSignatureError final : public std::runtime_error {
public:
    JniSignatureError(std::size_t offset, std::string message);

    [[nodiscard]] std::size_t Offset() const noexcept { return offset_; }

private:
    std::size_t offset_{};
};

[[nodiscard]] bool IsValidJniObjectClassName(
    std::string_view name) noexcept;
[[nodiscard]] JniTypeDescriptor ParseJniFieldDescriptor(
    std::string_view descriptor);
[[nodiscard]] JniMethodDescriptor ParseJniMethodDescriptor(
    std::string_view descriptor);

}  // namespace ogplay::runtime
