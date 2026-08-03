#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ogplay/loader/elf.h"

namespace ogplay::loader {

inline constexpr std::uint32_t kElfProgramTls = 7;

struct Elf32TlsInfo final {
    memory::GuestAddress address;
    std::uint32_t file_size{};
    std::uint32_t memory_size{};
    std::uint32_t alignment{};
    std::vector<std::byte> initial_image;
};

[[nodiscard]] std::optional<Elf32TlsInfo> ReadElf32TlsInfo(
    std::span<const std::byte> bytes, const Elf32Image& image);

}  // namespace ogplay::loader
