#include "ogplay/gles/guest_transfer.h"

#include <limits>
#include <utility>

#include "ogplay/memory/address_space.h"

namespace ogplay::gles {
namespace {

[[nodiscard]] bool ReadsGuest(const GuestTransferDirection direction) noexcept {
    return direction == GuestTransferDirection::input ||
           direction == GuestTransferDirection::input_output;
}

[[nodiscard]] bool WritesGuest(const GuestTransferDirection direction) noexcept {
    return direction == GuestTransferDirection::output ||
           direction == GuestTransferDirection::input_output;
}

}  // namespace

std::span<const std::byte> PrepareGuestInput(
    memory::AddressSpace& memory, const memory::GuestAddress address,
    const std::uint64_t size, const bool nullable,
    std::vector<std::byte>& storage, const std::uint64_t thread_id,
    const std::uint64_t size_limit) {
    if (address.IsNull()) {
        if (!nullable) {
            throw GuestTransferError("required guest pointer is null");
        }
        return std::span<const std::byte>{};
    }
    if (size > size_limit || size > std::numeric_limits<std::size_t>::max()) {
        throw GuestTransferError("guest transfer exceeds configured size limit");
    }
    if (size == 0) {
        return std::span<const std::byte>{};
    }

    const memory::GuestRange range(address, size);
    memory.Validate(range, memory::AccessType::read, thread_id);
    const auto host_size = static_cast<std::size_t>(size);
    if (storage.size() < host_size) {
        storage.resize(host_size);
    }
    const auto bytes = std::span(storage).first(host_size);
    memory.Read(address, bytes, thread_id);
    return bytes;
}

GuestBuffer GuestBuffer::Prepare(memory::AddressSpace& memory,
                                 const memory::GuestAddress address,
                                 const std::uint64_t size,
                                 const GuestTransferDirection direction,
                                 const bool nullable,
                                 const std::uint64_t thread_id,
                                 const std::uint64_t size_limit) {
    if (address.IsNull()) {
        if (!nullable) {
            throw GuestTransferError("required guest pointer is null");
        }
        return GuestBuffer(memory, address, size, direction, thread_id, true, {});
    }
    if (size > size_limit || size > std::numeric_limits<std::size_t>::max()) {
        throw GuestTransferError("guest transfer exceeds configured size limit");
    }
    if (size == 0) {
        return GuestBuffer(memory, address, size, direction, thread_id, false, {});
    }

    const memory::GuestRange range(address, size);
    if (ReadsGuest(direction)) {
        memory.Validate(range, memory::AccessType::read, thread_id);
    }
    if (WritesGuest(direction)) {
        memory.Validate(range, memory::AccessType::write, thread_id);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (ReadsGuest(direction)) {
        memory.Read(address, bytes, thread_id);
    }
    return GuestBuffer(memory, address, size, direction, thread_id, false,
                       std::move(bytes));
}

GuestBuffer::GuestBuffer(memory::AddressSpace& memory,
                         const memory::GuestAddress address,
                         const std::uint64_t size,
                         const GuestTransferDirection direction,
                         const std::uint64_t thread_id, const bool is_null,
                         std::vector<std::byte> bytes) noexcept
    : memory_(&memory), address_(address), size_(size), direction_(direction),
      thread_id_(thread_id), is_null_(is_null), bytes_(std::move(bytes)) {}

bool GuestBuffer::IsNull() const noexcept {
    return is_null_;
}

std::uint64_t GuestBuffer::Size() const noexcept {
    return size_;
}

GuestTransferDirection GuestBuffer::Direction() const noexcept {
    return direction_;
}

std::span<const std::byte> GuestBuffer::Bytes() const noexcept {
    return bytes_;
}

std::span<std::byte> GuestBuffer::WritableBytes() {
    if (!WritesGuest(direction_) || is_null_) {
        throw GuestTransferError("guest buffer has no writable host transfer");
    }
    return bytes_;
}

bool GuestBuffer::IsCommitted() const noexcept {
    return committed_;
}

void GuestBuffer::Commit() {
    if (!WritesGuest(direction_)) {
        throw GuestTransferError("input-only guest buffer cannot be committed");
    }
    if (committed_) {
        throw GuestTransferError("guest buffer was already committed");
    }
    if (!is_null_ && size_ != 0) {
        const memory::GuestRange range(address_, size_);
        memory_->Validate(range, memory::AccessType::write, thread_id_);
        memory_->Write(address_, bytes_, thread_id_);
    }
    committed_ = true;
}

}  // namespace ogplay::gles
