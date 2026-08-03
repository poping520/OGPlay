#include "ogplay/loader/symbol_version.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::loader {
namespace {

struct VersionName final {
    Elf32SymbolVersionKind kind{Elf32SymbolVersionKind::global};
    std::string name;
    std::string dependency;
};

void Require(const bool condition, const std::string_view message) {
    if (!condition) throw ElfError(std::string(message));
}

[[nodiscard]] std::uint16_t Read16(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 2,
            "truncated ELF version half");
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 1]))
         << 8U));
}

[[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4,
            "truncated ELF version word");
    std::uint32_t result{};
    for (std::size_t index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[offset + index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> UniqueValue(
    const Elf32Image& image, const std::int32_t tag) {
    std::optional<std::uint32_t> result;
    for (const auto& entry : image.dynamic_entries) {
        if (entry.tag != tag) continue;
        Require(!result.has_value(), "ELF version tag appears more than once");
        result = entry.value;
    }
    return result;
}

[[nodiscard]] std::size_t VirtualFileOffset(
    const Elf32Image& image, const std::uint64_t address,
    const std::uint32_t size, const std::size_t file_size) {
    Require(address + size <= (UINT64_C(1) << 32U),
            "ELF version range wraps the guest address space");
    std::optional<std::size_t> result;
    for (const auto& load : image.program_headers) {
        if (load.type != kElfProgramLoad) continue;
        const auto start =
            static_cast<std::uint64_t>(load.virtual_address.Value());
        if (address < start || address + size > start + load.file_size) continue;
        Require(!result.has_value(), "ELF version range has ambiguous mappings");
        const auto offset = static_cast<std::uint64_t>(load.file_offset) +
                            address - start;
        Require(offset <= std::numeric_limits<std::size_t>::max() &&
                    offset <= file_size && size <= file_size - offset,
                "ELF version range is outside the image");
        result = static_cast<std::size_t>(offset);
    }
    Require(result.has_value(), "ELF version range is not file-backed");
    return *result;
}

[[nodiscard]] std::uint64_t AddAddress(const std::uint32_t base,
                                       const std::uint32_t offset) {
    const auto result = static_cast<std::uint64_t>(base) + offset;
    Require(result < (UINT64_C(1) << 32U),
            "ELF version link offset wraps the address space");
    return result;
}

void Advance(std::uint32_t& offset, const std::uint32_t next,
             const std::string_view message) {
    Require(next <= std::numeric_limits<std::uint32_t>::max() - offset,
            message);
    offset += next;
}

[[nodiscard]] std::string ReadString(
    const std::span<const std::byte> bytes, const Elf32Image& image,
    const Elf32DynamicInfo& dynamic, const std::uint32_t string_offset) {
    Require(string_offset < dynamic.string_table_size,
            "ELF version string offset is outside DT_STRTAB");
    const auto remaining = dynamic.string_table_size - string_offset;
    const auto address = static_cast<std::uint64_t>(
                             dynamic.string_table.Value()) +
                         string_offset;
    const auto offset = VirtualFileOffset(image, address, remaining, bytes.size());
    std::string result;
    for (std::uint32_t index = 0; index < remaining; ++index) {
        const auto character = std::to_integer<unsigned char>(
            bytes[offset + static_cast<std::size_t>(index)]);
        if (character == 0) return result;
        result.push_back(static_cast<char>(character));
    }
    throw ElfError("ELF version string is not terminated");
}

void AddVersion(std::map<std::uint16_t, VersionName>& versions,
                const std::uint16_t index, VersionName version) {
    Require(index > 1, "ELF named version index must exceed one");
    Require(versions.emplace(index, std::move(version)).second,
            "ELF version index is defined more than once");
}

void ReadDefinitions(const std::span<const std::byte> bytes,
                     const Elf32Image& image,
                     const Elf32DynamicInfo& dynamic,
                     const std::uint32_t base, const std::uint32_t count,
                     std::map<std::uint16_t, VersionName>& versions) {
    std::uint32_t relative{};
    for (std::uint32_t entry = 0; entry < count; ++entry) {
        const auto address = AddAddress(base, relative);
        const auto offset = VirtualFileOffset(image, address, 20, bytes.size());
        Require(Read16(bytes, offset) == 1, "unsupported ELF verdef revision");
        const auto index = static_cast<std::uint16_t>(
            Read16(bytes, offset + 4) & UINT16_C(0x7fff));
        const auto auxiliary_count = Read16(bytes, offset + 6);
        auto auxiliary = Read32(bytes, offset + 12);
        Require(auxiliary_count != 0 && auxiliary != 0,
                "ELF verdef has no auxiliary name");
        std::string name;
        for (std::uint16_t item = 0; item < auxiliary_count; ++item) {
            const auto auxiliary_offset = VirtualFileOffset(
                image,
                AddAddress(static_cast<std::uint32_t>(address), auxiliary),
                8, bytes.size());
            if (item == 0) {
                name = ReadString(bytes, image, dynamic,
                                  Read32(bytes, auxiliary_offset));
            }
            const auto next = Read32(bytes, auxiliary_offset + 4);
            Require((item + 1U == auxiliary_count) == (next == 0),
                    "ELF verdaux chain length is inconsistent");
            Advance(auxiliary, next, "ELF verdaux chain offset wraps");
        }
        AddVersion(versions, index,
                   {Elf32SymbolVersionKind::definition,
                    std::move(name), {}});
        const auto next = Read32(bytes, offset + 16);
        Require((entry + 1U == count) == (next == 0),
                "ELF verdef chain length disagrees with DT_VERDEFNUM");
        Advance(relative, next, "ELF verdef chain offset wraps");
    }
}

void ReadRequirements(const std::span<const std::byte> bytes,
                      const Elf32Image& image,
                      const Elf32DynamicInfo& dynamic,
                      const std::uint32_t base, const std::uint32_t count,
                      std::map<std::uint16_t, VersionName>& versions) {
    std::uint32_t relative{};
    for (std::uint32_t entry = 0; entry < count; ++entry) {
        const auto address = AddAddress(base, relative);
        const auto offset = VirtualFileOffset(image, address, 16, bytes.size());
        Require(Read16(bytes, offset) == 1, "unsupported ELF verneed revision");
        const auto auxiliary_count = Read16(bytes, offset + 2);
        const auto dependency =
            ReadString(bytes, image, dynamic, Read32(bytes, offset + 4));
        auto auxiliary = Read32(bytes, offset + 8);
        Require(auxiliary_count != 0 && auxiliary != 0,
                "ELF verneed has no auxiliary entries");
        for (std::uint16_t item = 0; item < auxiliary_count; ++item) {
            const auto auxiliary_offset = VirtualFileOffset(
                image, AddAddress(static_cast<std::uint32_t>(address), auxiliary),
                16, bytes.size());
            const auto index = static_cast<std::uint16_t>(
                Read16(bytes, auxiliary_offset + 6) & UINT16_C(0x7fff));
            AddVersion(versions, index,
                       {Elf32SymbolVersionKind::requirement,
                        ReadString(bytes, image, dynamic,
                                   Read32(bytes, auxiliary_offset + 8)),
                        dependency});
            const auto next = Read32(bytes, auxiliary_offset + 12);
            Require((item + 1U == auxiliary_count) == (next == 0),
                    "ELF vernaux chain length is inconsistent");
            Advance(auxiliary, next, "ELF vernaux chain offset wraps");
        }
        const auto next = Read32(bytes, offset + 12);
        Require((entry + 1U == count) == (next == 0),
                "ELF verneed chain length disagrees with DT_VERNEEDNUM");
        Advance(relative, next, "ELF verneed chain offset wraps");
    }
}

}  // namespace

std::optional<Elf32SymbolVersionTable> ReadElf32SymbolVersions(
    const std::span<const std::byte> bytes, const Elf32Image& image,
    const Elf32DynamicInfo& dynamic, const Elf32SymbolTable& symbols) {
    const auto versym = UniqueValue(image, kElfDynamicVersionSymbol);
    const auto verdef = UniqueValue(image, kElfDynamicVersionDefinition);
    const auto verdef_count =
        UniqueValue(image, kElfDynamicVersionDefinitionCount);
    const auto verneed = UniqueValue(image, kElfDynamicVersionNeeded);
    const auto verneed_count = UniqueValue(image, kElfDynamicVersionNeededCount);
    Require(verdef.has_value() == verdef_count.has_value(),
            "ELF verdef address/count metadata is incomplete");
    Require(verneed.has_value() == verneed_count.has_value(),
            "ELF verneed address/count metadata is incomplete");
    if (!versym.has_value()) {
        Require(!verdef.has_value() && !verneed.has_value(),
                "ELF version definitions require DT_VERSYM");
        return std::nullopt;
    }

    std::map<std::uint16_t, VersionName> versions;
    if (verdef.has_value()) {
        Require(*verdef_count != 0, "DT_VERDEFNUM must not be zero");
        ReadDefinitions(bytes, image, dynamic, *verdef, *verdef_count, versions);
    }
    if (verneed.has_value()) {
        Require(*verneed_count != 0, "DT_VERNEEDNUM must not be zero");
        ReadRequirements(bytes, image, dynamic, *verneed, *verneed_count,
                         versions);
    }

    const auto table_size = static_cast<std::uint64_t>(symbols.symbols.size()) * 2U;
    Require(table_size <= std::numeric_limits<std::uint32_t>::max(),
            "ELF versym table is too large");
    const auto offset = VirtualFileOffset(
        image, *versym, static_cast<std::uint32_t>(table_size), bytes.size());
    Elf32SymbolVersionTable result;
    result.symbols.reserve(symbols.symbols.size());
    for (std::size_t symbol = 0; symbol < symbols.symbols.size(); ++symbol) {
        const auto raw = Read16(bytes, offset + symbol * 2U);
        const auto index = static_cast<std::uint16_t>(raw & UINT16_C(0x7fff));
        Elf32SymbolVersion version;
        version.index = index;
        version.hidden = (raw & UINT16_C(0x8000)) != 0;
        if (index == 0) {
            version.kind = Elf32SymbolVersionKind::local;
        } else if (index == 1) {
            version.kind = Elf32SymbolVersionKind::global;
        } else {
            const auto found = versions.find(index);
            Require(found != versions.end(),
                    "ELF versym references an unknown version index");
            version.kind = found->second.kind;
            version.name = found->second.name;
            version.dependency = found->second.dependency;
        }
        result.symbols.push_back(std::move(version));
    }
    return result;
}

}  // namespace ogplay::loader
