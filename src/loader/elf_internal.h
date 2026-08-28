#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "ogplay/core/arithmetic.h"
#include "ogplay/loader/elf.h"

namespace ogplay::loader::elf_detail {

struct VirtualFileOffsetErrors final {
    std::string_view wraps;
    std::string_view ambiguous;
    std::string_view unrepresentable;
    std::string_view outside_image;
    std::string_view not_file_backed;
};

[[nodiscard]] inline std::optional<std::uint32_t> UniqueDynamicValue(
    const Elf32Image& image, const std::int32_t tag,
    const std::string_view duplicate_error) {
    std::optional<std::uint32_t> result;
    for (const auto& entry : image.dynamic_entries) {
        if (entry.tag != tag) continue;
        if (result.has_value()) throw ElfError(std::string(duplicate_error));
        result = entry.value;
    }
    return result;
}

[[nodiscard]] inline std::size_t VirtualFileOffset(
    const Elf32Image& image, const memory::GuestAddress address,
    const std::uint32_t size, const std::size_t file_size,
    const VirtualFileOffsetErrors& errors) {
    const auto start = static_cast<std::uint64_t>(address.Value());
    const auto end = start + size;
    if (end > (UINT64_C(1) << 32U)) {
        throw ElfError(std::string(errors.wraps));
    }
    std::optional<std::size_t> result;
    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramLoad) continue;
        const auto segment_start =
            static_cast<std::uint64_t>(segment.virtual_address.Value());
        const auto segment_file_end = segment_start + segment.file_size;
        if (start < segment_start || end > segment_file_end) continue;
        if (result.has_value()) throw ElfError(std::string(errors.ambiguous));
        const auto offset = static_cast<std::uint64_t>(segment.file_offset) +
                            start - segment_start;
        if (offset > std::numeric_limits<std::size_t>::max()) {
            throw ElfError(std::string(errors.unrepresentable));
        }
        const auto host_offset = static_cast<std::size_t>(offset);
        if (!core::RangeFits(file_size, host_offset, size)) {
            throw ElfError(std::string(errors.outside_image));
        }
        result = host_offset;
    }
    if (!result.has_value()) {
        throw ElfError(std::string(errors.not_file_backed));
    }
    return *result;
}

}  // namespace ogplay::loader::elf_detail
