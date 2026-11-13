#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>
#include <utility>
#include "ogplay/memory/address.h"

namespace ogplay::runtime::dexvm {
struct GuestMemoryAccess final {
    std::function<memory::GuestAddress(std::uint32_t)> allocate;
    std::function<void(memory::GuestAddress, std::uint32_t)> release;
    std::function<bool(memory::GuestAddress, std::uint32_t)> validate;
    std::function<void(memory::GuestAddress, std::span<std::byte>)> read;
    std::function<void(memory::GuestAddress, std::span<const std::byte>)> write;
    // Synchronous native access only; caller owns reachability and mapping lifetime.
    std::function<std::byte*(memory::GuestAddress, std::uint32_t, bool)> resolve;
};

class GuestMemoryRuntime final {
public:
    using GuestMemoryOperation = std::function<std::uint32_t(memory::GuestAddress)>;

    void SetAccess(GuestMemoryAccess access) { access_ = std::move(access); }

    [[nodiscard]] std::byte* Resolve(memory::GuestAddress address, std::uint32_t size, bool write) const;
    [[nodiscard]] std::uint32_t WithTemporaryGuestMemory(std::span<const std::byte> bytes, bool copy_back,
                                                         const GuestMemoryOperation& operation,
                                                         std::vector<std::byte>* copied_back = nullptr);

private:
    GuestMemoryAccess access_;
};
}  // namespace ogplay::runtime::dexvm
