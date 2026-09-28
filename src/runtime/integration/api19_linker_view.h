#pragma once

#include <span>
#include <string>
#include <string_view>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {

// Read-only projection of android-4.4.4_r2 ARM32 soinfo. These are guest ABI
// offsets, independent of the host compiler's struct layout.
inline constexpr std::uint32_t kApi19SoinfoSize = 0x124;
inline constexpr std::uint32_t kApi19SoinfoReferences = 0x100;

struct Api19LinkerSymbol final {
    std::string name;
    memory::GuestAddress address;
    std::uint32_t size{};
    std::uint8_t binding{};
    std::uint8_t type{};
};

struct Api19LinkerView final {
    memory::GuestRange backing;
    [[nodiscard]] memory::GuestAddress Handle() const { return backing.Start(); }
};

[[nodiscard]] Api19LinkerView CreateApi19LinkerView(
    memory::AddressSpace& memory, const memory::GuestRange& arena,
    std::string_view library, memory::GuestAddress base,
    memory::GuestAddress load_bias, std::uint32_t image_size,
    std::span<const Api19LinkerSymbol> symbols);
void SetApi19LinkerReferences(memory::AddressSpace& memory,
                             const Api19LinkerView& view, std::uint32_t references);

}  // namespace ogplay::runtime::detail
