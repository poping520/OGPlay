#include "runtime/boundary/core/boundary_thunk_arena.h"

#include <stdexcept>
#include <vector>

#include "ogplay/runtime/boundary/boundary_symbol.h"

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kThunkStride = 4U;
}

BoundaryThunkArena::BoundaryThunkArena(
    memory::AddressSpace& address_space) noexcept
    : address_space_(address_space) {}

void BoundaryThunkArena::Map(const std::size_t slot_count) {
    if (IsMapped()) {
        throw std::logic_error("Android boundary thunks are already mapped");
    }
    const auto page_size = address_space_.PageSize();
    const auto code_bytes = slot_count * kThunkStride;
    const auto arena_bytes =
        ((code_bytes + page_size - 1U) / page_size) * page_size;
    if (arena_bytes == 0U ||
        arena_bytes > kBionicHleThunkEnd - kBionicHleThunkBegin) {
        throw std::length_error("Android boundary thunk arena exceeds its guest range");
    }
    address_space_.Map(
        {memory::GuestAddress{kBionicHleThunkBegin}, arena_bytes},
        memory::PageProtection::read | memory::PageProtection::write);
    std::vector<std::byte> code(arena_bytes, std::byte{});
    for (std::size_t index = 0; index < slot_count; ++index) {
        const auto offset = index * kThunkStride;
        code[offset] = std::byte{0x02};
        code[offset + 1U] = std::byte{0xdf};
        code[offset + 2U] = std::byte{0x70};
        code[offset + 3U] = std::byte{0x47};
    }
    address_space_.Write(memory::GuestAddress{kBionicHleThunkBegin}, code);
    address_space_.Protect(
        {memory::GuestAddress{kBionicHleThunkBegin}, arena_bytes},
        memory::PageProtection::read | memory::PageProtection::execute);
    mapped_bytes_ = arena_bytes;
}

bool BoundaryThunkArena::IsMapped() const noexcept { return mapped_bytes_ != 0U; }

std::size_t BoundaryThunkArena::DecodeSlot(const std::uint64_t pc) const noexcept {
    if (!IsMapped() || pc < kBionicHleThunkBegin ||
        pc >= kBionicHleThunkBegin + mapped_bytes_) {
        return InvalidSlot();
    }
    const auto offset = pc - kBionicHleThunkBegin;
    if ((offset % kThunkStride) != 0U) return InvalidSlot();
    return static_cast<std::size_t>(offset / kThunkStride);
}

std::size_t BoundaryThunkArena::SlotForAddress(
    const std::uint64_t address) noexcept {
    if (address < kBionicHleThunkBegin || address >= kBionicHleThunkEnd) {
        return InvalidSlot();
    }
    const auto offset = address - kBionicHleThunkBegin;
    if ((offset % kThunkStride) != 0U) return InvalidSlot();
    return static_cast<std::size_t>(offset / kThunkStride);
}

}  // namespace ogplay::runtime
