#include "ogplay/memory/bus.h"

#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

#include "ogplay/memory/address_space.h"

namespace ogplay::memory {
namespace {

template <typename UInt>
[[nodiscard]] UInt DecodeLittleEndian(const std::array<std::byte, sizeof(UInt)>& bytes) {
    static_assert(std::is_unsigned_v<UInt>);
    UInt value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<UInt>(std::to_integer<std::uint8_t>(bytes[index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

template <typename UInt>
[[nodiscard]] std::array<std::byte, sizeof(UInt)> EncodeLittleEndian(UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    std::array<std::byte, sizeof(UInt)> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(value & static_cast<UInt>(0xffU));
        value >>= 8U;
    }
    return bytes;
}

template <typename UInt>
[[nodiscard]] UInt ReadValue(AddressSpace& address_space, MemoryAccessObserver* observer,
                             const GuestAddress address, const std::uint64_t thread_id) {
    std::array<std::byte, sizeof(UInt)> bytes{};
    address_space.Read(address, bytes, thread_id);
    if (observer != nullptr) {
        observer->OnMemoryAccess({address, static_cast<std::uint32_t>(sizeof(UInt)),
                                  BusAccessType::read, thread_id});
    }
    return DecodeLittleEndian<UInt>(bytes);
}

template <typename UInt>
void WriteValue(AddressSpace& address_space, MemoryAccessObserver* observer,
                const GuestAddress address, const UInt value,
                const std::uint64_t thread_id) {
    const auto bytes = EncodeLittleEndian(value);
    address_space.Write(address, bytes, thread_id);
    if (observer != nullptr) {
        observer->OnMemoryAccess({address, static_cast<std::uint32_t>(sizeof(UInt)),
                                  BusAccessType::write, thread_id});
    }
}

}  // namespace

CheckedMemoryBus::CheckedMemoryBus(AddressSpace& address_space,
                                   MemoryAccessObserver* const observer) noexcept
    : address_space_(address_space), observer_(observer) {}

std::uint8_t CheckedMemoryBus::Read8(const GuestAddress address,
                                     const std::uint64_t thread_id) {
    return ReadValue<std::uint8_t>(address_space_, observer_, address, thread_id);
}
std::uint16_t CheckedMemoryBus::Read16(const GuestAddress address,
                                       const std::uint64_t thread_id) {
    return ReadValue<std::uint16_t>(address_space_, observer_, address, thread_id);
}
std::uint32_t CheckedMemoryBus::Read32(const GuestAddress address,
                                       const std::uint64_t thread_id) {
    return ReadValue<std::uint32_t>(address_space_, observer_, address, thread_id);
}
std::uint64_t CheckedMemoryBus::Read64(const GuestAddress address,
                                       const std::uint64_t thread_id) {
    return ReadValue<std::uint64_t>(address_space_, observer_, address, thread_id);
}
void CheckedMemoryBus::Write8(const GuestAddress address, const std::uint8_t value,
                              const std::uint64_t thread_id) {
    WriteValue(address_space_, observer_, address, value, thread_id);
}
void CheckedMemoryBus::Write16(const GuestAddress address, const std::uint16_t value,
                               const std::uint64_t thread_id) {
    WriteValue(address_space_, observer_, address, value, thread_id);
}
void CheckedMemoryBus::Write32(const GuestAddress address, const std::uint32_t value,
                               const std::uint64_t thread_id) {
    WriteValue(address_space_, observer_, address, value, thread_id);
}
void CheckedMemoryBus::Write64(const GuestAddress address, const std::uint64_t value,
                               const std::uint64_t thread_id) {
    WriteValue(address_space_, observer_, address, value, thread_id);
}

}  // namespace ogplay::memory
