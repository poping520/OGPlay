#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::runtime::dexvm {

struct MethodTypeDescriptor final {
    std::vector<std::string> parameters;
    std::string return_type;

    bool operator==(const MethodTypeDescriptor&) const = default;
};

class ClassNameCodecError final : public std::runtime_error {
public:
    ClassNameCodecError(std::size_t offset, std::string message);

    [[nodiscard]] std::size_t Offset() const noexcept { return offset_; }

private:
    std::size_t offset_{};
};

class ClassNameCodec final {
public:
    [[nodiscard]] static MethodTypeDescriptor ParseMethod(
        std::string_view descriptor);
    [[nodiscard]] static bool IsPrimitive(std::string_view descriptor) noexcept;
    [[nodiscard]] static bool IsReference(std::string_view descriptor) noexcept;
    [[nodiscard]] static bool IsArray(std::string_view descriptor) noexcept;
    [[nodiscard]] static std::string ClassGetName(
        std::string_view descriptor);
    [[nodiscard]] static std::string BinaryNameToDescriptor(
        std::string_view name);
};

}  // namespace ogplay::runtime::dexvm
