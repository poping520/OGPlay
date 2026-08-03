#include "ogplay/loader/elf.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::loader {
namespace {

inline constexpr std::size_t kElf32HeaderSize = 52;
inline constexpr std::size_t kElf32ProgramHeaderSize = 32;
inline constexpr std::size_t kElf32DynamicEntrySize = 8;
inline constexpr std::size_t kElf32SymbolSize = 16;
inline constexpr std::uint16_t kElfMachineArm = 40;
inline constexpr std::uint32_t kElfVersionCurrent = 1;
inline constexpr std::uint32_t kElfFlagExecute = 1;
inline constexpr std::uint32_t kElfFlagWrite = 2;
inline constexpr std::uint32_t kElfFlagRead = 4;

void Require(const bool condition, const std::string_view message) {
    if (!condition) throw ElfError(std::string(message));
}

void RequireRange(const std::size_t offset, const std::uint64_t size,
                  const std::size_t total, const std::string_view message) {
    Require(size <= total && offset <= total - static_cast<std::size_t>(size),
            message);
}

[[nodiscard]] std::uint16_t Read16(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    RequireRange(offset, 2, bytes.size(), "truncated ELF field");
    const auto low = static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(bytes[offset]));
    const auto high = static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(bytes[offset + 1]));
    return static_cast<std::uint16_t>(low | (high << 8U));
}

[[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    RequireRange(offset, 4, bytes.size(), "truncated ELF field");
    std::uint32_t result{};
    for (std::size_t index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

[[nodiscard]] Elf32ProgramHeader ParseProgramHeader(
    const std::span<const std::byte> bytes, const std::size_t offset) {
    const auto type = Read32(bytes, offset);
    const auto file_offset = Read32(bytes, offset + 4);
    const auto virtual_address = Read32(bytes, offset + 8);
    const auto file_size = Read32(bytes, offset + 16);
    const auto memory_size = Read32(bytes, offset + 20);
    const auto flags = Read32(bytes, offset + 24);
    const auto alignment = Read32(bytes, offset + 28);

    if (type == kElfProgramLoad) {
        Require(file_size <= memory_size,
                "PT_LOAD file size exceeds memory size");
        RequireRange(file_offset, file_size, bytes.size(),
                     "PT_LOAD file range is outside the image");
        const auto guest_end = static_cast<std::uint64_t>(virtual_address) +
                               static_cast<std::uint64_t>(memory_size);
        Require(guest_end <= (UINT64_C(1) << 32U),
                "PT_LOAD guest range wraps the address space");
        Require(alignment == 0 || alignment == 1 || std::has_single_bit(alignment),
                "PT_LOAD alignment is not a power of two");
        if (alignment > 1) {
            Require((file_offset % alignment) == (virtual_address % alignment),
                    "PT_LOAD file and virtual addresses are incongruent");
        }
    }
    return {type,
            file_offset,
            memory::GuestAddress{virtual_address},
            file_size,
            memory_size,
            flags,
            alignment};
}

[[nodiscard]] std::vector<Elf32DynamicEntry> ParseDynamic(
    const std::span<const std::byte> bytes,
    const Elf32ProgramHeader& dynamic) {
    RequireRange(dynamic.file_offset, dynamic.file_size, bytes.size(),
                 "PT_DYNAMIC file range is outside the image");
    Require(dynamic.file_size >= kElf32DynamicEntrySize &&
                dynamic.file_size % kElf32DynamicEntrySize == 0,
            "PT_DYNAMIC has an invalid size");
    std::vector<Elf32DynamicEntry> result;
    const auto end = static_cast<std::size_t>(dynamic.file_offset) +
                     dynamic.file_size;
    for (auto offset = static_cast<std::size_t>(dynamic.file_offset);
         offset < end; offset += kElf32DynamicEntrySize) {
        const auto raw_tag = Read32(bytes, offset);
        const auto tag = std::bit_cast<std::int32_t>(raw_tag);
        if (tag == kElfDynamicNull) return result;
        result.push_back({tag, Read32(bytes, offset + 4)});
    }
    throw ElfError("PT_DYNAMIC is not terminated by DT_NULL");
}

[[nodiscard]] std::optional<std::uint32_t> UniqueDynamicValue(
    const Elf32Image& image, const std::int32_t tag) {
    std::optional<std::uint32_t> result;
    for (const auto& entry : image.dynamic_entries) {
        if (entry.tag != tag) continue;
        Require(!result.has_value(), "dynamic tag appears more than once");
        result = entry.value;
    }
    return result;
}

[[nodiscard]] std::size_t VirtualFileOffset(
    const Elf32Image& image, const memory::GuestAddress address,
    const std::uint32_t size, const std::size_t file_size) {
    const auto start = static_cast<std::uint64_t>(address.Value());
    const auto end = start + size;
    Require(end <= (UINT64_C(1) << 32U),
            "dynamic virtual range wraps the address space");
    std::optional<std::size_t> result;
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramLoad) continue;
        const auto segment_start =
            static_cast<std::uint64_t>(segment.virtual_address.Value());
        const auto segment_file_end = segment_start + segment.file_size;
        if (start < segment_start || end > segment_file_end) continue;
        Require(!result.has_value(),
                "dynamic virtual range has ambiguous PT_LOAD mappings");
        const auto delta = start - segment_start;
        const auto offset = static_cast<std::uint64_t>(segment.file_offset) + delta;
        Require(offset <= std::numeric_limits<std::size_t>::max(),
                "dynamic file offset is not representable");
        RequireRange(static_cast<std::size_t>(offset), size, file_size,
                     "dynamic virtual range is outside the image");
        result = static_cast<std::size_t>(offset);
    }
    Require(result.has_value(),
            "dynamic virtual range is not file-backed by PT_LOAD");
    return *result;
}

struct FileBackedLocation final {
    std::size_t offset{};
    std::uint64_t available{};
};

[[nodiscard]] FileBackedLocation LocateFileBacked(
    const Elf32Image& image, const memory::GuestAddress address,
    const std::size_t file_size) {
    const auto value = static_cast<std::uint64_t>(address.Value());
    std::optional<FileBackedLocation> result;
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramLoad) continue;
        const auto start =
            static_cast<std::uint64_t>(segment.virtual_address.Value());
        const auto end = start + segment.file_size;
        if (value < start || value >= end) continue;
        Require(!result.has_value(),
                "dynamic virtual address has ambiguous PT_LOAD mappings");
        const auto delta = value - start;
        const auto offset = static_cast<std::uint64_t>(segment.file_offset) + delta;
        Require(offset <= std::numeric_limits<std::size_t>::max(),
                "dynamic file offset is not representable");
        RequireRange(static_cast<std::size_t>(offset), end - value, file_size,
                     "file-backed PT_LOAD range is outside the image");
        result = FileBackedLocation{static_cast<std::size_t>(offset), end - value};
    }
    Require(result.has_value(),
            "dynamic virtual address is not file-backed by PT_LOAD");
    return *result;
}

[[nodiscard]] memory::GuestAddress AddGuestAddress(
    const memory::GuestAddress address, const std::uint64_t offset,
    const std::string_view message) {
    const auto value = static_cast<std::uint64_t>(address.Value()) + offset;
    Require(value <= std::numeric_limits<std::uint32_t>::max(), message);
    return memory::GuestAddress{static_cast<std::uint32_t>(value)};
}

[[nodiscard]] Elf32SysvHashInfo ReadSysvHash(
    const std::span<const std::byte> bytes, const Elf32Image& image,
    const memory::GuestAddress address) {
    const auto header = VirtualFileOffset(image, address, 8, bytes.size());
    const auto bucket_count = Read32(bytes, header);
    const auto chain_count = Read32(bytes, header + 4);
    Require(bucket_count != 0 && chain_count != 0,
            "DT_HASH has an empty bucket or chain table");
    const auto words = UINT64_C(2) + bucket_count + chain_count;
    Require(words <= std::numeric_limits<std::uint32_t>::max() / 4U,
            "DT_HASH size overflows");
    static_cast<void>(VirtualFileOffset(
        image, address, static_cast<std::uint32_t>(words * 4U), bytes.size()));
    return {bucket_count, chain_count};
}

[[nodiscard]] Elf32GnuHashInfo ReadGnuHash(
    const std::span<const std::byte> bytes, const Elf32Image& image,
    const memory::GuestAddress address) {
    const auto header = VirtualFileOffset(image, address, 16, bytes.size());
    const auto bucket_count = Read32(bytes, header);
    const auto symbol_offset = Read32(bytes, header + 4);
    const auto bloom_size = Read32(bytes, header + 8);
    const auto bloom_shift = Read32(bytes, header + 12);
    Require(bucket_count != 0 && bloom_size != 0,
            "DT_GNU_HASH has an empty bucket or bloom table");
    const auto prefix_size = UINT64_C(16) +
                             static_cast<std::uint64_t>(bloom_size) * 4U +
                             static_cast<std::uint64_t>(bucket_count) * 4U;
    Require(prefix_size <= std::numeric_limits<std::uint32_t>::max(),
            "DT_GNU_HASH prefix size overflows");
    const auto table = VirtualFileOffset(
        image, address, static_cast<std::uint32_t>(prefix_size), bytes.size());
    const auto buckets = table + 16 + static_cast<std::size_t>(bloom_size) * 4U;
    std::uint32_t maximum_bucket{};
    for (std::uint32_t index = 0; index < bucket_count; ++index) {
        const auto bucket = Read32(bytes, buckets + static_cast<std::size_t>(index) * 4U);
        if (bucket == 0) continue;
        Require(bucket >= symbol_offset,
                "DT_GNU_HASH bucket precedes its symbol offset");
        maximum_bucket = std::max(maximum_bucket, bucket);
    }

    auto symbol_count = symbol_offset;
    if (maximum_bucket != 0) {
        const auto chains_address = AddGuestAddress(
            address, prefix_size, "DT_GNU_HASH chain address wraps");
        const auto chains = LocateFileBacked(image, chains_address, bytes.size());
        const auto first = static_cast<std::uint64_t>(maximum_bucket) - symbol_offset;
        Require(first < chains.available / 4U,
                "DT_GNU_HASH bucket is outside its chain table");
        auto index = maximum_bucket;
        for (auto chain = first; chain < chains.available / 4U; ++chain, ++index) {
            const auto value = Read32(
                bytes, chains.offset + static_cast<std::size_t>(chain) * 4U);
            if ((value & 1U) != 0) {
                Require(index != std::numeric_limits<std::uint32_t>::max(),
                        "DT_GNU_HASH symbol count overflows");
                symbol_count = index + 1U;
                break;
            }
        }
        Require(symbol_count > maximum_bucket,
                "DT_GNU_HASH chain is not terminated");
    }
    return {bucket_count, symbol_offset, bloom_size, bloom_shift, symbol_count};
}

[[nodiscard]] std::string ReadDynamicString(
    const std::span<const std::byte> bytes, const std::size_t table_offset,
    const std::uint32_t table_size, const std::uint32_t string_offset) {
    Require(string_offset < table_size,
            "dynamic string offset is outside DT_STRTAB");
    const auto begin = table_offset + string_offset;
    const auto end = table_offset + table_size;
    auto cursor = begin;
    while (cursor < end && bytes[cursor] != std::byte{}) ++cursor;
    Require(cursor < end, "dynamic string is not null-terminated");
    std::string result;
    result.reserve(cursor - begin);
    for (auto offset = begin; offset < cursor; ++offset) {
        result.push_back(
            static_cast<char>(std::to_integer<std::uint8_t>(bytes[offset])));
    }
    return result;
}

[[nodiscard]] bool HasProtection(const memory::PageProtection value,
                                 const memory::PageProtection flag) {
    return (static_cast<std::uint8_t>(value) &
            static_cast<std::uint8_t>(flag)) != 0;
}

[[nodiscard]] memory::PageProtection SegmentProtection(
    const std::uint32_t flags) {
    auto protection = memory::PageProtection::none;
    if ((flags & kElfFlagRead) != 0) {
        protection = protection | memory::PageProtection::read;
    }
    if ((flags & kElfFlagWrite) != 0) {
        protection = protection | memory::PageProtection::write;
    }
    if ((flags & kElfFlagExecute) != 0) {
        protection = protection | memory::PageProtection::execute;
    }
    return protection;
}

[[nodiscard]] std::uint64_t BiasedAddress(
    const memory::GuestAddress address, const memory::GuestAddress load_bias,
    const std::uint64_t size, const std::string_view message) {
    const auto result = static_cast<std::uint64_t>(address.Value()) +
                        load_bias.Value();
    Require(result + size <= (UINT64_C(1) << 32U), message);
    return result;
}

[[nodiscard]] std::optional<memory::GuestAddress> ResolveEntry(
    const Elf32Image& image, const memory::GuestAddress load_bias) {
    if (image.entry.Value() == 0) return std::nullopt;
    const auto entry = BiasedAddress(image.entry, load_bias, 1,
                                     "ELF entry wraps the address space");
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramLoad ||
            (segment.flags & kElfFlagExecute) == 0) {
            continue;
        }
        const auto start = BiasedAddress(segment.virtual_address, load_bias, 0,
                                         "PT_LOAD start wraps the address space");
        const auto end = start + segment.memory_size;
        if (entry >= start && entry < end) {
            return memory::GuestAddress{static_cast<std::uint32_t>(entry)};
        }
    }
    throw ElfError("ELF entry is not inside an executable PT_LOAD");
}

void WriteZeros(memory::AddressSpace& address_space,
                memory::GuestAddress address, std::uint64_t size,
                const std::uint64_t page_size) {
    const std::vector<std::byte> zeros(static_cast<std::size_t>(page_size),
                                       std::byte{});
    while (size != 0) {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(size, zeros.size()));
        address_space.Write(address, std::span<const std::byte>(zeros).first(count));
        address = address.Add(count);
        size -= count;
    }
}

}  // namespace

bool Elf32Symbol::IsExported() const noexcept {
    const bool externally_bound = binding == 1 || binding == 2;
    const bool externally_visible = visibility == 0 || visibility == 3;
    return section_index != 0 && externally_bound && externally_visible;
}

Elf32Image ParseElf32Arm(const std::span<const std::byte> bytes) {
    RequireRange(0, kElf32HeaderSize, bytes.size(), "ELF header is truncated");
    Require(std::to_integer<std::uint8_t>(bytes[0]) == 0x7f &&
                std::to_integer<std::uint8_t>(bytes[1]) == 'E' &&
                std::to_integer<std::uint8_t>(bytes[2]) == 'L' &&
                std::to_integer<std::uint8_t>(bytes[3]) == 'F',
            "ELF magic is invalid");
    Require(std::to_integer<std::uint8_t>(bytes[4]) == 1,
            "ELF is not 32-bit");
    Require(std::to_integer<std::uint8_t>(bytes[5]) == 1,
            "ELF is not little-endian");
    Require(std::to_integer<std::uint8_t>(bytes[6]) == kElfVersionCurrent,
            "ELF identification version is invalid");

    const auto raw_type = Read16(bytes, 16);
    Require(raw_type == static_cast<std::uint16_t>(Elf32ImageType::executable) ||
                raw_type == static_cast<std::uint16_t>(Elf32ImageType::shared_object),
            "ELF image type is unsupported");
    Require(Read16(bytes, 18) == kElfMachineArm, "ELF machine is not ARM");
    Require(Read32(bytes, 20) == kElfVersionCurrent,
            "ELF header version is invalid");
    Require(Read16(bytes, 40) == kElf32HeaderSize,
            "ELF header size is invalid");

    const auto program_offset = Read32(bytes, 28);
    const auto program_entry_size = Read16(bytes, 42);
    const auto program_count = Read16(bytes, 44);
    Require(program_count == 0 || program_entry_size == kElf32ProgramHeaderSize,
            "ELF program header size is invalid");
    const auto table_size = static_cast<std::uint64_t>(program_entry_size) *
                            program_count;
    RequireRange(program_offset, table_size, bytes.size(),
                 "ELF program header table is outside the image");

    Elf32Image image;
    image.type = static_cast<Elf32ImageType>(raw_type);
    image.entry = memory::GuestAddress{Read32(bytes, 24)};
    image.arm_flags = Read32(bytes, 36);
    image.program_headers.reserve(program_count);
    std::optional<std::size_t> dynamic_index;
    for (std::size_t index = 0; index < program_count; ++index) {
        const auto offset = static_cast<std::size_t>(program_offset) +
                            index * kElf32ProgramHeaderSize;
        auto header = ParseProgramHeader(bytes, offset);
        if (header.type == kElfProgramDynamic) {
            Require(!dynamic_index.has_value(),
                    "ELF contains multiple PT_DYNAMIC segments");
            dynamic_index = index;
        }
        image.program_headers.push_back(header);
    }
    if (dynamic_index.has_value()) {
        image.has_dynamic_segment = true;
        image.dynamic_entries =
            ParseDynamic(bytes, image.program_headers[*dynamic_index]);
    }
    return image;
}

Elf32DynamicInfo ReadElf32DynamicInfo(
    const std::span<const std::byte> bytes, const Elf32Image& image) {
    Require(image.has_dynamic_segment, "ELF has no PT_DYNAMIC segment");
    const auto string_table = UniqueDynamicValue(image, kElfDynamicStringTable);
    const auto string_table_size =
        UniqueDynamicValue(image, kElfDynamicStringTableSize);
    Require(string_table.has_value() && string_table_size.has_value(),
            "dynamic string table metadata is incomplete");
    Require(*string_table_size != 0, "dynamic string table is empty");
    const memory::GuestAddress table_address{*string_table};
    const auto table_offset = VirtualFileOffset(
        image, table_address, *string_table_size, bytes.size());

    Elf32DynamicInfo info;
    info.string_table = table_address;
    info.string_table_size = *string_table_size;
    for (const auto& entry : image.dynamic_entries) {
        if (entry.tag == kElfDynamicNeeded) {
            info.needed.push_back(ReadDynamicString(
                bytes, table_offset, *string_table_size, entry.value));
        }
    }
    const auto soname = UniqueDynamicValue(image, kElfDynamicSoname);
    if (soname.has_value()) {
        info.soname = ReadDynamicString(
            bytes, table_offset, *string_table_size, *soname);
    }
    return info;
}

Elf32SymbolTable ReadElf32SymbolTable(
    const std::span<const std::byte> bytes, const Elf32Image& image) {
    const auto dynamic = ReadElf32DynamicInfo(bytes, image);
    const auto symbol_table = UniqueDynamicValue(image, kElfDynamicSymbolTable);
    const auto symbol_size = UniqueDynamicValue(image, kElfDynamicSymbolEntrySize);
    Require(symbol_table.has_value() && symbol_size.has_value(),
            "dynamic symbol table metadata is incomplete");
    Require(*symbol_size == kElf32SymbolSize,
            "DT_SYMENT is not the ELF32 symbol size");

    Elf32SymbolTable result;
    const auto sysv_hash = UniqueDynamicValue(image, kElfDynamicHash);
    if (sysv_hash.has_value()) {
        result.sysv_hash = ReadSysvHash(
            bytes, image, memory::GuestAddress{*sysv_hash});
    }
    const auto gnu_hash = UniqueDynamicValue(image, kElfDynamicGnuHash);
    if (gnu_hash.has_value()) {
        result.gnu_hash = ReadGnuHash(
            bytes, image, memory::GuestAddress{*gnu_hash});
    }
    Require(result.sysv_hash.has_value() || result.gnu_hash.has_value(),
            "dynamic symbol table has no supported hash table");
    std::uint32_t symbol_count = result.sysv_hash.has_value()
                                     ? result.sysv_hash->chain_count
                                     : result.gnu_hash->symbol_count;
    if (result.sysv_hash.has_value() && result.gnu_hash.has_value()) {
        Require(result.sysv_hash->chain_count == result.gnu_hash->symbol_count,
                "SysV and GNU hash symbol counts disagree");
    }
    Require(symbol_count != 0, "dynamic symbol table is empty");
    const auto table_size = static_cast<std::uint64_t>(symbol_count) *
                            kElf32SymbolSize;
    Require(table_size <= std::numeric_limits<std::uint32_t>::max(),
            "dynamic symbol table size overflows");
    const auto symbols_offset = VirtualFileOffset(
        image, memory::GuestAddress{*symbol_table},
        static_cast<std::uint32_t>(table_size), bytes.size());
    const auto strings_offset = VirtualFileOffset(
        image, dynamic.string_table, dynamic.string_table_size, bytes.size());

    result.symbols.reserve(symbol_count);
    for (std::uint32_t index = 0; index < symbol_count; ++index) {
        const auto offset = symbols_offset +
                            static_cast<std::size_t>(index) * kElf32SymbolSize;
        const auto name_offset = Read32(bytes, offset);
        const auto value = Read32(bytes, offset + 4);
        const auto size = Read32(bytes, offset + 8);
        const auto info = std::to_integer<std::uint8_t>(bytes[offset + 12]);
        const auto other = std::to_integer<std::uint8_t>(bytes[offset + 13]);
        const auto section = Read16(bytes, offset + 14);
        result.symbols.push_back({
            ReadDynamicString(bytes, strings_offset,
                              dynamic.string_table_size, name_offset),
            memory::GuestAddress{value},
            size,
            static_cast<std::uint8_t>(info >> 4U),
            static_cast<std::uint8_t>(info & 0x0fU),
            static_cast<std::uint8_t>(other & 0x03U),
            section});
    }
    const auto& null_symbol = result.symbols.front();
    Require(null_symbol.name.empty() && null_symbol.value.Value() == 0 &&
                null_symbol.size == 0 && null_symbol.binding == 0 &&
                null_symbol.type == 0 && null_symbol.visibility == 0 &&
                null_symbol.section_index == 0,
            "dynamic symbol zero is not the null symbol");
    return result;
}

Elf32LoadPlan BuildElf32LoadPlan(const Elf32Image& image,
                                 const memory::GuestAddress load_bias,
                                 const std::uint64_t page_size) {
    Require(page_size != 0 && std::has_single_bit(page_size) &&
                page_size <= std::numeric_limits<std::uint32_t>::max(),
            "host page size is invalid");
    Require(load_bias.Value() % page_size == 0,
            "ELF load bias is not page aligned");
    if (image.type == Elf32ImageType::executable) {
        Require(load_bias.Value() == 0, "ET_EXEC cannot use a load bias");
    }

    std::map<std::uint32_t, memory::PageProtection> pages;
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramLoad || segment.memory_size == 0) continue;
        const auto protection = SegmentProtection(segment.flags);
        Require(protection != memory::PageProtection::none,
                "PT_LOAD has no usable permissions");
        Require(HasProtection(protection, memory::PageProtection::read),
                "PT_LOAD must be readable");
        const auto start = BiasedAddress(segment.virtual_address, load_bias,
                                         segment.memory_size,
                                         "PT_LOAD bias wraps the address space");
        const auto end = start + segment.memory_size;
        const auto page_start = start & ~(page_size - 1U);
        const auto page_end = (end + page_size - 1U) & ~(page_size - 1U);
        Require(page_start >= memory::LowAddressGuard().EndExclusive(),
                "PT_LOAD overlaps the low address guard");
        for (auto page = page_start; page < page_end; page += page_size) {
            const auto key = static_cast<std::uint32_t>(page);
            pages[key] = pages[key] | protection;
            Require(!(HasProtection(pages[key], memory::PageProtection::write) &&
                      HasProtection(pages[key], memory::PageProtection::execute)),
                    "PT_LOAD page would require write and execute permission");
        }
    }
    Require(!pages.empty(), "ELF has no non-empty PT_LOAD segments");

    Elf32LoadPlan plan;
    plan.load_bias = load_bias;
    plan.entry = ResolveEntry(image, load_bias);
    auto iterator = pages.begin();
    auto region_start = static_cast<std::uint64_t>(iterator->first);
    auto region_end = region_start + page_size;
    auto protection = iterator->second;
    ++iterator;
    for (; iterator != pages.end(); ++iterator) {
        if (iterator->first == region_end && iterator->second == protection) {
            region_end += page_size;
            continue;
        }
        plan.regions.push_back({
            memory::GuestRange(memory::GuestAddress{
                                   static_cast<std::uint32_t>(region_start)},
                               region_end - region_start),
            protection});
        region_start = iterator->first;
        region_end = region_start + page_size;
        protection = iterator->second;
    }
    plan.regions.push_back({
        memory::GuestRange(
            memory::GuestAddress{static_cast<std::uint32_t>(region_start)},
            region_end - region_start),
        protection});
    return plan;
}

Elf32LoadPlan LoadElf32Arm(const std::span<const std::byte> bytes,
                           const Elf32Image& image,
                           const memory::GuestAddress load_bias,
                           memory::AddressSpace& address_space) {
    const auto page_size = address_space.PageSize();
    auto plan = BuildElf32LoadPlan(image, load_bias, page_size);
    std::vector<memory::GuestRange> mapped;
    try {
        for (const auto& region : plan.regions) {
            address_space.Map(region.range, memory::PageProtection::read |
                                               memory::PageProtection::write);
            mapped.push_back(region.range);
        }
        for (const auto& segment : image.program_headers) {
            if (segment.type != kElfProgramLoad || segment.memory_size == 0) continue;
            const auto destination_value = BiasedAddress(
                segment.virtual_address, load_bias, segment.memory_size,
                "PT_LOAD bias wraps the address space");
            auto destination = memory::GuestAddress{
                static_cast<std::uint32_t>(destination_value)};
            if (segment.file_size != 0) {
                RequireRange(segment.file_offset, segment.file_size, bytes.size(),
                             "PT_LOAD file range is outside the image");
                address_space.Write(
                    destination,
                    bytes.subspan(segment.file_offset, segment.file_size));
            }
            const auto zero_size = segment.memory_size - segment.file_size;
            if (zero_size != 0) {
                WriteZeros(address_space, destination.Add(segment.file_size),
                           zero_size, page_size);
            }
        }
        for (const auto& region : plan.regions) {
            address_space.Protect(region.range, region.final_protection);
        }
    } catch (...) {
        for (auto iterator = mapped.rbegin(); iterator != mapped.rend(); ++iterator) {
            address_space.Unmap(*iterator);
        }
        throw;
    }
    return plan;
}

}  // namespace ogplay::loader
