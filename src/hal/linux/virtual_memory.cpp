#include "ogplay/hal/virtual_memory.h"

#include <limits>
#include <stdexcept>

#include <sys/mman.h>
#include <unistd.h>

namespace ogplay::hal {
namespace {

int NativeProtection(const MemoryProtection protection) {
    if (HasProtection(protection, MemoryProtection::write) &&
        !HasProtection(protection, MemoryProtection::read)) {
        throw std::invalid_argument("writable memory must also be readable");
    }
    int result = PROT_NONE;
    if (HasProtection(protection, MemoryProtection::read)) result |= PROT_READ;
    if (HasProtection(protection, MemoryProtection::write)) result |= PROT_WRITE;
    if (HasProtection(protection, MemoryProtection::execute)) result |= PROT_EXEC;
    return result;
}

class LinuxVirtualMemory final : public VirtualMemoryReservation {
public:
    explicit LinuxVirtualMemory(const std::uint64_t size) : size_(size) {
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) throw std::runtime_error("sysconf(_SC_PAGESIZE) failed");
        page_size_ = static_cast<std::uint64_t>(page_size);
        if (size_ == 0 || size_ % page_size_ != 0 ||
            size_ > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument("reservation size must be page aligned");
        }
        const auto result = mmap(nullptr, static_cast<std::size_t>(size_), PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result == MAP_FAILED) throw std::runtime_error("mmap reserve failed");
        base_ = static_cast<std::byte*>(result);
    }

    ~LinuxVirtualMemory() override {
        if (base_ != nullptr) static_cast<void>(munmap(base_, static_cast<std::size_t>(size_)));
    }

    std::byte* Base() const noexcept override { return base_; }
    std::uint64_t Size() const noexcept override { return size_; }
    std::uint64_t PageSize() const noexcept override { return page_size_; }

    void Commit(const std::uint64_t offset, const std::uint64_t size,
                const MemoryProtection protection) override {
        SetProtection(offset, size, protection);
    }

    void Protect(const std::uint64_t offset, const std::uint64_t size,
                 const MemoryProtection protection) override {
        SetProtection(offset, size, protection);
    }

    void Decommit(const std::uint64_t offset, const std::uint64_t size) override {
        ValidateRange(offset, size);
        auto* const address = base_ + static_cast<std::size_t>(offset);
        const auto result = mmap(address, static_cast<std::size_t>(size), PROT_NONE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (result != address) throw std::runtime_error("mmap decommit failed");
    }

private:
    void ValidateRange(const std::uint64_t offset, const std::uint64_t size) const {
        if (size == 0 || offset % page_size_ != 0 || size % page_size_ != 0 ||
            offset > size_ || size > size_ - offset) {
            throw std::invalid_argument("virtual memory range is invalid or unaligned");
        }
    }

    void SetProtection(const std::uint64_t offset, const std::uint64_t size,
                       const MemoryProtection protection) {
        ValidateRange(offset, size);
        if (mprotect(base_ + static_cast<std::size_t>(offset), static_cast<std::size_t>(size),
                     NativeProtection(protection)) != 0) {
            throw std::runtime_error("mprotect failed");
        }
    }

    std::byte* base_{};
    std::uint64_t size_{};
    std::uint64_t page_size_{};
};

}  // namespace

std::unique_ptr<VirtualMemoryReservation> ReserveVirtualMemory(const std::uint64_t size) {
    return std::make_unique<LinuxVirtualMemory>(size);
}

}  // namespace ogplay::hal
