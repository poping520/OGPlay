#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/memory/address.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::loader {

enum class Elf32ImageType : std::uint16_t {
    executable = 2,
    shared_object = 3,
};

inline constexpr std::uint32_t kElfProgramLoad = 1;
inline constexpr std::uint32_t kElfProgramDynamic = 2;
inline constexpr std::int32_t kElfDynamicNull = 0;
inline constexpr std::int32_t kElfDynamicNeeded = 1;
inline constexpr std::int32_t kElfDynamicPltRelSize = 2;
inline constexpr std::int32_t kElfDynamicHash = 4;
inline constexpr std::int32_t kElfDynamicStringTable = 5;
inline constexpr std::int32_t kElfDynamicSymbolTable = 6;
inline constexpr std::int32_t kElfDynamicStringTableSize = 10;
inline constexpr std::int32_t kElfDynamicSymbolEntrySize = 11;
inline constexpr std::int32_t kElfDynamicSoname = 14;
inline constexpr std::int32_t kElfDynamicRel = 17;
inline constexpr std::int32_t kElfDynamicRelSize = 18;
inline constexpr std::int32_t kElfDynamicRelEntrySize = 19;
inline constexpr std::int32_t kElfDynamicPltRel = 20;
inline constexpr std::int32_t kElfDynamicJmpRel = 23;
inline constexpr std::int32_t kElfDynamicRela = 7;
inline constexpr std::int32_t kElfDynamicRelaSize = 8;
inline constexpr std::int32_t kElfDynamicRelaEntrySize = 9;
inline constexpr std::int32_t kElfDynamicGnuHash = 0x6ffffef5;

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

struct Elf32SysvHashInfo final {
    std::uint32_t bucket_count{};
    std::uint32_t chain_count{};
};

struct Elf32GnuHashInfo final {
    std::uint32_t bucket_count{};
    std::uint32_t symbol_offset{};
    std::uint32_t bloom_size{};
    std::uint32_t bloom_shift{};
    std::uint32_t symbol_count{};
};

struct Elf32Symbol final {
    std::string name;
    memory::GuestAddress value;
    std::uint32_t size{};
    std::uint8_t binding{};
    std::uint8_t type{};
    std::uint8_t visibility{};
    std::uint16_t section_index{};

    [[nodiscard]] bool IsExported() const noexcept;
};

struct Elf32SymbolTable final {
    std::optional<Elf32SysvHashInfo> sysv_hash;
    std::optional<Elf32GnuHashInfo> gnu_hash;
    std::vector<Elf32Symbol> symbols;
};

enum class Elf32RelocationTableKind : std::uint8_t {
    dynamic,
    procedure_linkage,
};

struct Elf32Relocation final {
    memory::GuestAddress target;
    std::uint32_t symbol_index{};
    std::uint8_t type{};
    Elf32RelocationTableKind table{Elf32RelocationTableKind::dynamic};
};

struct Elf32RelocationTable final {
    std::vector<Elf32Relocation> relocations;
};

struct Elf32LoadRegion final {
    memory::GuestRange range;
    memory::PageProtection final_protection{memory::PageProtection::none};
};

struct Elf32LoadPlan final {
    memory::GuestAddress load_bias;
    std::optional<memory::GuestAddress> entry;
    std::vector<Elf32LoadRegion> regions;
};

class ElfError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] Elf32Image ParseElf32Arm(std::span<const std::byte> bytes);
[[nodiscard]] Elf32DynamicInfo ReadElf32DynamicInfo(
    std::span<const std::byte> bytes, const Elf32Image& image);
[[nodiscard]] Elf32SymbolTable ReadElf32SymbolTable(
    std::span<const std::byte> bytes, const Elf32Image& image);
[[nodiscard]] Elf32RelocationTable ReadElf32Relocations(
    std::span<const std::byte> bytes, const Elf32Image& image,
    const Elf32SymbolTable& symbols);
[[nodiscard]] Elf32LoadPlan BuildElf32LoadPlan(
    const Elf32Image& image, memory::GuestAddress load_bias,
    std::uint64_t page_size);
[[nodiscard]] Elf32LoadPlan LoadElf32Arm(
    std::span<const std::byte> bytes, const Elf32Image& image,
    memory::GuestAddress load_bias, memory::AddressSpace& address_space);

}  // namespace ogplay::loader
