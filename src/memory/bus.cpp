#include "ogplay/memory/bus.h"

#include <array>
#include <cstddef>
#include <type_traits>

#include "ogplay/memory/address_space.h"

namespace ogplay::memory {
namespace {

template <typename UInt>
[[nodiscard]] UInt DecodeLittleEndian(
    const std::array<std::byte, sizeof(UInt)>& bytes) {
    static_assert(std::is_unsigned_v<UInt>);
    std::uint64_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(bytes[index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return static_cast<UInt>(value);
}

template <typename UInt>
[[nodiscard]] UInt ReadValue(AddressSpace& address_space, MemoryAccessObserver* observer,
                             const GuestAddress address, const std::uint64_t thread_id) {
    const auto value = [&] {
        if constexpr (sizeof(UInt) == 1) {
            return address_space.Read8(address, thread_id);
        } else if constexpr (sizeof(UInt) == 2) {
            return address_space.Read16(address, thread_id);
        } else if constexpr (sizeof(UInt) == 4) {
            return address_space.Read32(address, thread_id);
        } else {
            return address_space.Read64(address, thread_id);
        }
    }();
    if (observer != nullptr) {
        observer->OnMemoryAccess({address, static_cast<std::uint32_t>(sizeof(UInt)),
                                  BusAccessType::read, thread_id});
    }
    return value;
}

template <typename UInt>
[[nodiscard]] UInt FetchValue(AddressSpace& address_space,
                              MemoryAccessObserver* observer,
                              const GuestAddress address,
                              const std::uint64_t thread_id) {
    std::array<std::byte, sizeof(UInt)> bytes{};
    address_space.Fetch(address, bytes, thread_id);
    if (observer != nullptr) {
        observer->OnMemoryAccess({address, static_cast<std::uint32_t>(sizeof(UInt)),
                                  BusAccessType::execute, thread_id});
    }
    return DecodeLittleEndian<UInt>(bytes);
}

template <typename UInt>
void WriteValue(AddressSpace& address_space, MemoryAccessObserver* observer,
                const GuestAddress address, const UInt value,
                const std::uint64_t thread_id) {
    if constexpr (sizeof(UInt) == 1) address_space.Write8(address, value, thread_id);
    if constexpr (sizeof(UInt) == 2) address_space.Write16(address, value, thread_id);
    if constexpr (sizeof(UInt) == 4) address_space.Write32(address, value, thread_id);
    if constexpr (sizeof(UInt) == 8) address_space.Write64(address, value, thread_id);
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
std::uint16_t CheckedMemoryBus::Fetch16(const GuestAddress address,
                                        const std::uint64_t thread_id) {
    return FetchValue<std::uint16_t>(address_space_, observer_, address, thread_id);
}
std::uint32_t CheckedMemoryBus::Fetch32(const GuestAddress address,
                                        const std::uint64_t thread_id) {
    return FetchValue<std::uint32_t>(address_space_, observer_, address, thread_id);
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

DirectMemoryPageTable* CheckedMemoryBus::DirectPageTable() noexcept {
    return observer_ == nullptr ? address_space_.DirectPageTable() : nullptr;
}

}  // namespace ogplay::memory
