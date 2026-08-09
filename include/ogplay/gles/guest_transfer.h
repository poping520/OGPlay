#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/memory/address.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::gles {

inline constexpr std::uint64_t kDefaultGuestTransferLimit = 64U * 1024U * 1024U;

enum class GuestTransferDirection { input, output, input_output };

class GuestTransferError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::span<const std::byte> PrepareGuestInput(
    memory::AddressSpace& memory, memory::GuestAddress address,
    std::uint64_t size, bool nullable, std::vector<std::byte>& storage,
    std::uint64_t thread_id = 0,
    std::uint64_t size_limit = kDefaultGuestTransferLimit);

class GuestBuffer final {
public:
    GuestBuffer(const GuestBuffer&) = delete;
    GuestBuffer& operator=(const GuestBuffer&) = delete;
    GuestBuffer(GuestBuffer&&) noexcept = default;
    GuestBuffer& operator=(GuestBuffer&&) noexcept = default;

    static GuestBuffer Prepare(memory::AddressSpace& memory,
                               memory::GuestAddress address,
                               std::uint64_t size,
                               GuestTransferDirection direction,
                               bool nullable, std::uint64_t thread_id = 0,
                               std::uint64_t size_limit =
                                   kDefaultGuestTransferLimit);

    [[nodiscard]] bool IsNull() const noexcept;
    [[nodiscard]] std::uint64_t Size() const noexcept;
    [[nodiscard]] GuestTransferDirection Direction() const noexcept;
    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept;
    [[nodiscard]] std::span<std::byte> WritableBytes();
    [[nodiscard]] bool IsCommitted() const noexcept;
    void Commit();

private:
    GuestBuffer(memory::AddressSpace& memory, memory::GuestAddress address,
                std::uint64_t size, GuestTransferDirection direction,
                std::uint64_t thread_id, bool is_null,
                std::vector<std::byte> bytes) noexcept;

    memory::AddressSpace* memory_{};
    memory::GuestAddress address_{};
    std::uint64_t size_{};
    GuestTransferDirection direction_{};
    std::uint64_t thread_id_{};
    bool is_null_{};
    bool committed_{};
    std::vector<std::byte> bytes_;
};

}  // namespace ogplay::gles
