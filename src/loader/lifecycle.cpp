#include "ogplay/loader/lifecycle.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/byte_order.h"

#include "elf_internal.h"

namespace ogplay::loader {
namespace {

void Require(const bool condition, const std::string_view message) {
    if (!condition) throw ElfError(std::string(message));
}

void RequireRange(const std::size_t offset, const std::uint64_t size,
                  const std::size_t total, const std::string_view message) {
    Require(size <= total &&
                core::RangeFits(total, offset, static_cast<std::size_t>(size)),
            message);
}

[[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    RequireRange(offset, 4, bytes.size(), "truncated lifecycle field");
    return core::ReadLittleEndian<std::uint32_t>(bytes, offset);
}

[[nodiscard]] std::optional<std::uint32_t> UniqueDynamicValue(
    const Elf32Image& image, const std::int32_t tag) {
    return elf_detail::UniqueDynamicValue(
        image, tag, "lifecycle dynamic tag appears more than once");
}

[[nodiscard]] std::size_t VirtualFileOffset(
    const Elf32Image& image, const memory::GuestAddress address,
    const std::uint32_t size, const std::size_t file_size) {
    return elf_detail::VirtualFileOffset(
        image, address, size, file_size,
        {"lifecycle virtual range wraps the address space",
         "lifecycle range has ambiguous PT_LOAD mappings",
         "lifecycle file offset is not representable",
         "lifecycle range is outside the image",
         "lifecycle range is not file-backed by PT_LOAD"});
}

[[nodiscard]] std::vector<memory::GuestAddress> ReadFunctionArray(
    const std::span<const std::byte> bytes, const Elf32Image& image,
    const std::int32_t address_tag, const std::int32_t size_tag) {
    const auto address = UniqueDynamicValue(image, address_tag);
    const auto size = UniqueDynamicValue(image, size_tag);
    Require(address.has_value() == size.has_value(),
            "lifecycle array metadata is incomplete");
    if (!address.has_value()) return {};
    Require(*size % 4U == 0, "lifecycle array size is not word aligned");
    if (*size == 0) return {};
    const auto offset = VirtualFileOffset(
        image, memory::GuestAddress{*address}, *size, bytes.size());
    std::vector<memory::GuestAddress> result;
    result.reserve(*size / 4U);
    for (std::uint32_t index = 0; index < *size / 4U; ++index) {
        result.push_back(memory::GuestAddress{
            Read32(bytes, offset + static_cast<std::size_t>(index) * 4U)});
    }
    return result;
}

}  // namespace

Elf32LifecycleInfo ReadElf32LifecycleInfo(
    const std::span<const std::byte> bytes, const Elf32Image& image) {
    Elf32LifecycleInfo result;
    const auto init = UniqueDynamicValue(image, kElfDynamicInit);
    const auto fini = UniqueDynamicValue(image, kElfDynamicFini);
    if (init.has_value()) result.init = memory::GuestAddress{*init};
    if (fini.has_value()) result.fini = memory::GuestAddress{*fini};
    result.init_array = ReadFunctionArray(
        bytes, image, kElfDynamicInitArray, kElfDynamicInitArraySize);
    result.fini_array = ReadFunctionArray(
        bytes, image, kElfDynamicFiniArray, kElfDynamicFiniArraySize);

    for (const auto& segment : image.program_headers) {
        if (segment.type != kElfProgramArmExidx) continue;
        Require(!result.arm_exidx.has_value(),
                "ELF contains multiple PT_ARM_EXIDX segments");
        Require(segment.file_size == segment.memory_size &&
                    segment.file_size % 8U == 0,
                "PT_ARM_EXIDX has an invalid size");
        if (segment.file_size != 0) {
            static_cast<void>(VirtualFileOffset(
                image, segment.virtual_address, segment.file_size, bytes.size()));
        }
        result.arm_exidx =
            Elf32ArmExidx{segment.virtual_address, segment.file_size / 8U};
    }
    return result;
}

}  // namespace ogplay::loader
