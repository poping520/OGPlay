#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/loader/elf.h"

namespace ogplay::loader {

inline constexpr std::int32_t kElfDynamicVersionSymbol = 0x6ffffff0;
inline constexpr std::int32_t kElfDynamicVersionDefinition = 0x6ffffffc;
inline constexpr std::int32_t kElfDynamicVersionDefinitionCount = 0x6ffffffd;
inline constexpr std::int32_t kElfDynamicVersionNeeded = 0x6ffffffe;
inline constexpr std::int32_t kElfDynamicVersionNeededCount = 0x6fffffff;

enum class Elf32SymbolVersionKind : std::uint8_t {
    local,
    global,
    definition,
    requirement,
};

struct Elf32SymbolVersion final {
    std::uint16_t index{};
    bool hidden{};
    Elf32SymbolVersionKind kind{Elf32SymbolVersionKind::global};
    std::string name;
    std::string dependency;
};

struct Elf32SymbolVersionTable final {
    std::vector<Elf32SymbolVersion> symbols;
};

[[nodiscard]] std::optional<Elf32SymbolVersionTable> ReadElf32SymbolVersions(
    std::span<const std::byte> bytes, const Elf32Image& image,
    const Elf32DynamicInfo& dynamic, const Elf32SymbolTable& symbols);

}  // namespace ogplay::loader
