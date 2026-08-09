#pragma once

#include <cstdint>

#include "ogplay/memory/address.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::memory {

enum class BusAccessType : std::uint8_t { read, write, execute };

struct BusAccess {
    GuestAddress address;
    std::uint32_t size{};
    BusAccessType type{BusAccessType::read};
    std::uint64_t thread_id{};
};

class MemoryAccessObserver {
public:
    virtual ~MemoryAccessObserver() = default;
    virtual void OnMemoryAccess(const BusAccess& access) = 0;
};

class MemoryBus {
public:
    virtual ~MemoryBus() = default;
    [[nodiscard]] virtual std::uint8_t Read8(GuestAddress address,
                                             std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual std::uint16_t Read16(GuestAddress address,
                                               std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual std::uint32_t Read32(GuestAddress address,
                                               std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual std::uint64_t Read64(GuestAddress address,
                                               std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual std::uint16_t Fetch16(GuestAddress address,
                                                std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual std::uint32_t Fetch32(GuestAddress address,
                                                std::uint64_t thread_id = 0) = 0;
    virtual void Write8(GuestAddress address, std::uint8_t value,
                        std::uint64_t thread_id = 0) = 0;
    virtual void Write16(GuestAddress address, std::uint16_t value,
                         std::uint64_t thread_id = 0) = 0;
    virtual void Write32(GuestAddress address, std::uint32_t value,
                         std::uint64_t thread_id = 0) = 0;
    virtual void Write64(GuestAddress address, std::uint64_t value,
                         std::uint64_t thread_id = 0) = 0;
    [[nodiscard]] virtual DirectMemoryPageTable* DirectPageTable() noexcept {
        return nullptr;
    }
};

class CheckedMemoryBus final : public MemoryBus {
public:
    explicit CheckedMemoryBus(AddressSpace& address_space,
                              MemoryAccessObserver* observer = nullptr) noexcept;

    [[nodiscard]] std::uint8_t Read8(GuestAddress address,
                                     std::uint64_t thread_id = 0) override;
    [[nodiscard]] std::uint16_t Read16(GuestAddress address,
                                       std::uint64_t thread_id = 0) override;
    [[nodiscard]] std::uint32_t Read32(GuestAddress address,
                                       std::uint64_t thread_id = 0) override;
    [[nodiscard]] std::uint64_t Read64(GuestAddress address,
                                       std::uint64_t thread_id = 0) override;
    [[nodiscard]] std::uint16_t Fetch16(GuestAddress address,
                                        std::uint64_t thread_id = 0) override;
    [[nodiscard]] std::uint32_t Fetch32(GuestAddress address,
                                        std::uint64_t thread_id = 0) override;
    void Write8(GuestAddress address, std::uint8_t value,
                std::uint64_t thread_id = 0) override;
    void Write16(GuestAddress address, std::uint16_t value,
                 std::uint64_t thread_id = 0) override;
    void Write32(GuestAddress address, std::uint32_t value,
                 std::uint64_t thread_id = 0) override;
    void Write64(GuestAddress address, std::uint64_t value,
                 std::uint64_t thread_id = 0) override;
    [[nodiscard]] DirectMemoryPageTable* DirectPageTable() noexcept override;

private:
    AddressSpace& address_space_;
    MemoryAccessObserver* observer_;
};

}  // namespace ogplay::memory
