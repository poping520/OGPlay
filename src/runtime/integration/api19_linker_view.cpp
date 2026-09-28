#include "runtime/integration/api19_linker_view.h"

#include <bit>
#include <vector>

#include "ogplay/core/byte_order.h"
#include "ogplay/loader/link_namespace.h"

namespace ogplay::runtime::detail {
namespace {

std::uint32_t ElfHash(const std::string_view name) {
    std::uint32_t hash{};
    for (const char character : name) {
        hash = (hash << 4U) + static_cast<unsigned char>(character);
        const auto high = hash & 0xf0000000U;
        hash ^= high >> 24U;
        hash &= ~high;
    }
    return hash;
}

}  // namespace

Api19LinkerView CreateApi19LinkerView(
    memory::AddressSpace& memory, const memory::GuestRange& arena,
    const std::string_view library, const memory::GuestAddress base,
    const memory::GuestAddress load_bias, const std::uint32_t image_size,
    const std::span<const Api19LinkerSymbol> symbols) {
    constexpr std::size_t kMaximumSymbols = 65536;
    constexpr std::size_t kMaximumBytes = 4 * 1024 * 1024;
    if (library.empty() || library.size() >= 128 || library.find('\0') != std::string_view::npos ||
        symbols.size() > kMaximumSymbols) {
        throw loader::LinkError("API19 linker view has invalid name or too many symbols");
    }
    const auto count = static_cast<std::uint32_t>(symbols.size()) + 1U;
    const auto buckets = std::bit_ceil(count);
    const auto symtab_offset = kApi19SoinfoSize;
    const auto bucket_offset = symtab_offset + count * 16U;
    const auto chain_offset = bucket_offset + buckets * 4U;
    const auto strtab_offset = chain_offset + count * 4U;
    std::vector<std::byte> bytes(strtab_offset + 1U, std::byte{});
    std::vector<std::uint32_t> heads(buckets), chains(count);
    for (std::uint32_t index = 1; index < count; ++index) {
        const auto& symbol = symbols[index - 1U];
        if (symbol.name.empty() || symbol.name.find('\0') != std::string::npos ||
            symbol.name.size() >= kMaximumBytes || bytes.size() + symbol.name.size() + 1U > kMaximumBytes ||
            (symbol.binding != 1U && symbol.binding != 2U) || symbol.type > 15U) {
            throw loader::LinkError("API19 linker view has invalid symbol metadata");
        }
        const auto name_offset = static_cast<std::uint32_t>(bytes.size() - strtab_offset);
        for (const char character : symbol.name) {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        const auto offset = symtab_offset + index * 16U;
        core::WriteLittleEndian(std::span{bytes}, offset, name_offset);
        // Project resolved addresses (also SHN_ABS/intercepts) into this view's
        // load bias. ARM32 address addition wraps modulo 2^32 by definition.
        core::WriteLittleEndian(std::span{bytes}, offset + 4U,
                                symbol.address.Value() - load_bias.Value());
        core::WriteLittleEndian(std::span{bytes}, offset + 8U, symbol.size);
        bytes[offset + 12U] = static_cast<std::byte>((symbol.binding << 4U) | symbol.type);
        core::WriteLittleEndian(std::span{bytes}, offset + 14U, std::uint16_t{1});
        const auto bucket = ElfHash(symbol.name) % buckets;
        chains[index] = heads[bucket];
        heads[bucket] = index;
    }
    const auto page = memory.PageSize();
    const auto size = (bytes.size() + page - 1U) & ~(page - 1U);
    const auto address = memory.MapAnywhere(arena, size,
        memory::PageProtection::read | memory::PageProtection::write);
    const Api19LinkerView view{{address, size}};
    try {
        for (std::size_t index = 0; index < library.size(); ++index) {
            bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(library[index]));
        }
        const auto put = [&](const std::size_t offset, const std::uint32_t value) {
            core::WriteLittleEndian(std::span{bytes}, offset, value);
        };
        put(0x8c, base.Value());
        put(0x90, image_size);
        put(0xa8, 1);  // FLAG_LINKED; lifecycle still belongs to the process.
        put(0xac, address.Add(strtab_offset).Value());
        put(0xb0, address.Add(symtab_offset).Value());
        put(0xb4, buckets);
        put(0xb8, count);
        put(0xbc, address.Add(bucket_offset).Value());
        put(0xc0, address.Add(chain_offset).Value());
        put(kApi19SoinfoReferences, 1);
        put(0x104, load_bias.Value());  // link_map.l_addr
        put(0x108, address.Value());   // link_map.l_name
        put(0x11c, load_bias.Value());
        for (std::uint32_t index = 0; index < buckets; ++index) put(bucket_offset + index * 4U, heads[index]);
        for (std::uint32_t index = 0; index < count; ++index) put(chain_offset + index * 4U, chains[index]);
        memory.Write(address, bytes);
        memory.Protect(view.backing, memory::PageProtection::read);
    } catch (...) {
        memory.Unmap(view.backing);
        throw;
    }
    return view;
}

void SetApi19LinkerReferences(memory::AddressSpace& memory,
                             const Api19LinkerView& view, const std::uint32_t references) {
    const memory::GuestRange record{view.Handle(), memory.PageSize()};
    memory.Protect(record, memory::PageProtection::read | memory::PageProtection::write);
    try {
        memory.Write32(view.Handle().Add(kApi19SoinfoReferences), references);
    } catch (...) {
        memory.Protect(record, memory::PageProtection::read);
        throw;
    }
    memory.Protect(record, memory::PageProtection::read);
}

}  // namespace ogplay::runtime::detail
