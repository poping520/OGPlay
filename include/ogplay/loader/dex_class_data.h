#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ogplay/loader/dex.h"

namespace ogplay::loader {

struct DexEncodedField final {
    std::uint32_t field_index{};
    std::uint32_t access_flags{};
};

struct DexCodeInfo final {
    std::uint32_t offset{};
    std::uint16_t registers_size{};
    std::uint16_t incoming_words{};
    std::uint16_t outgoing_words{};
    std::uint16_t tries_size{};
    std::uint32_t debug_info_offset{};
    std::uint32_t instruction_units{};
};

struct DexEncodedMethod final {
    std::uint32_t method_index{};
    std::uint32_t access_flags{};
    std::optional<DexCodeInfo> code;
};

struct DexClassData final {
    std::uint32_t class_def_index{};
    std::vector<DexEncodedField> static_fields;
    std::vector<DexEncodedField> instance_fields;
    std::vector<DexEncodedMethod> direct_methods;
    std::vector<DexEncodedMethod> virtual_methods;
};

[[nodiscard]] std::vector<DexClassData> ReadDexClassData(
    std::span<const std::uint8_t> bytes, const DexImage& image);

}  // namespace ogplay::loader
