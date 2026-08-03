#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <optional>
#include <vector>

#include "ogplay/memory/address.h"

namespace ogplay::loader {

enum class Elf32ImageType : std::uint16_t {
    executable = 2,
    shared_object = 3,
};

inline constexpr std::uint32_t kElfProgramLoad = 1;
inline constexpr std::uint32_t kElfProgramDynamic = 2;
inline constexpr std::int32_t kElfDynamicNull = 0;
inline constexpr std::int32_t kElfDynamicNeeded = 1;
inline constexpr std::int32_t kElfDynamicStringTable = 5;
inline constexpr std::int32_t kElfDynamicStringTableSize = 10;
inline constexpr std::int32_t kElfDynamicSoname = 14;

struct Elf32ProgramHeader final {
    std::uint32_t type{};
    std::uint32_t file_offset{};
    memory::GuestAddress virtual_address;
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t flags{};
    std::uint32_t alignment{};
};

struct Elf32DynamicEntry final {
    std::int32_t tag{};
    std::uint32_t value{};
};

struct Elf32Image final {
    Elf32ImageType type{Elf32ImageType::shared_object};
    memory::GuestAddress entry;
    std::uint32_t arm_flags{};
    bool has_dynamic_segment{};
    std::vector<Elf32ProgramHeader> program_headers;
    std::vector<Elf32DynamicEntry> dynamic_entries;
};

struct Elf32DynamicInfo final {
    memory::GuestAddress string_table;
    std::uint32_t string_table_size{};
    std::vector<std::string> needed;
    std::optional<std::string> soname;
};

class ElfError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Elf32Image ParseElf32Arm(std::span<const std::byte> bytes);
[[nodiscard]] Elf32DynamicInfo ReadElf32DynamicInfo(
    std::span<const std::byte> bytes, const Elf32Image& image);

}  // namespace ogplay::loader
