#include "ogplay/loader/tls.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ogplay::loader {
namespace {

void Require(const bool condition, const std::string_view message) {
    if (!condition) throw ElfError(std::string(message));
}

[[nodiscard]] bool IsPowerOfTwo(const std::uint32_t value) {
    return value != 0 && (value & (value - 1U)) == 0;
}

void ValidateLoadCoverage(const Elf32Image& image,
                          const Elf32ProgramHeader& tls) {
    const auto start = static_cast<std::uint64_t>(tls.virtual_address.Value());
    const auto file_end = start + tls.file_size;
    const auto memory_end = start + tls.memory_size;
    std::size_t file_matches{};
    std::size_t memory_matches{};
    for (const auto& load : image.program_headers) {
        if (load.type != kElfProgramLoad) continue;
        const auto load_start =
            static_cast<std::uint64_t>(load.virtual_address.Value());
        if (start >= load_start &&
            memory_end <= load_start + load.memory_size) {
            ++memory_matches;
        }
        if (tls.file_size != 0 && start >= load_start &&
            file_end <= load_start + load.file_size) {
            ++file_matches;
        }
    }
    Require(memory_matches == 1,
            "PT_TLS memory is not uniquely covered by PT_LOAD");
    Require(tls.file_size == 0 || file_matches == 1,
            "PT_TLS initial image is not uniquely file-backed by PT_LOAD");
}

}  // namespace

std::optional<Elf32TlsInfo> ReadElf32TlsInfo(
    const std::span<const std::byte> bytes, const Elf32Image& image) {
    std::optional<Elf32ProgramHeader> tls;
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramTls) continue;
        Require(!tls.has_value(), "ELF contains multiple PT_TLS segments");
        tls = segment;
    }
    if (!tls.has_value()) return std::nullopt;

    Require(tls->file_size <= tls->memory_size,
            "PT_TLS file size exceeds memory size");
    const auto end = static_cast<std::uint64_t>(tls->virtual_address.Value()) +
                     tls->memory_size;
    Require(end <= (UINT64_C(1) << 32U),
            "PT_TLS memory wraps the guest address space");
    Require(tls->alignment == 0 || IsPowerOfTwo(tls->alignment),
            "PT_TLS alignment is not zero or a power of two");
    if (tls->alignment > 1) {
        Require(tls->file_offset % tls->alignment ==
                    tls->virtual_address.Value() % tls->alignment,
                "PT_TLS file and virtual addresses are incongruent");
    }
    const auto file_end = static_cast<std::uint64_t>(tls->file_offset) +
                          tls->file_size;
    Require(file_end <= bytes.size(), "PT_TLS initial image is truncated");
    ValidateLoadCoverage(image, *tls);

    Elf32TlsInfo result;
    result.address = tls->virtual_address;
    result.file_size = tls->file_size;
    result.memory_size = tls->memory_size;
    result.alignment = tls->alignment;
    const auto first = bytes.begin() + tls->file_offset;
    result.initial_image.assign(first, first + tls->file_size);
    return result;
}

}  // namespace ogplay::loader
