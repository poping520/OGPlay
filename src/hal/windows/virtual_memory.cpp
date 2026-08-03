#include "ogplay/hal/virtual_memory.h"

#include <limits>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ogplay::hal {
namespace {

DWORD NativeProtection(const MemoryProtection protection) {
    const auto read = HasProtection(protection, MemoryProtection::read);
    const auto write = HasProtection(protection, MemoryProtection::write);
    const auto execute = HasProtection(protection, MemoryProtection::execute);
    if (write && !read) throw std::invalid_argument("writable memory must also be readable");
    if (execute) {
        if (write) return PAGE_EXECUTE_READWRITE;
        if (read) return PAGE_EXECUTE_READ;
        return PAGE_EXECUTE;
    }
    if (write) return PAGE_READWRITE;
    if (read) return PAGE_READONLY;
    return PAGE_NOACCESS;
}

class WindowsVirtualMemory final : public VirtualMemoryReservation {
public:
    explicit WindowsVirtualMemory(const std::uint64_t size) : size_(size) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page_size_ = info.dwPageSize;
        if (size_ == 0 || size_ % page_size_ != 0 ||
            size_ > std::numeric_limits<SIZE_T>::max()) {
            throw std::invalid_argument("reservation size must be page aligned");
        }
        base_ = static_cast<std::byte*>(
            VirtualAlloc(nullptr, static_cast<SIZE_T>(size_), MEM_RESERVE, PAGE_NOACCESS));
        if (base_ == nullptr) throw std::runtime_error("VirtualAlloc reserve failed");
    }

    ~WindowsVirtualMemory() override {
        if (base_ != nullptr) static_cast<void>(VirtualFree(base_, 0, MEM_RELEASE));
    }

    std::byte* Base() const noexcept override { return base_; }
    std::uint64_t Size() const noexcept override { return size_; }
    std::uint64_t PageSize() const noexcept override { return page_size_; }

    void Commit(const std::uint64_t offset, const std::uint64_t size,
                const MemoryProtection protection) override {
        ValidateRange(offset, size);
        auto* const address = base_ + static_cast<SIZE_T>(offset);
        const auto result = VirtualAlloc(address, static_cast<SIZE_T>(size), MEM_COMMIT,
                                         NativeProtection(protection));
        if (result != address) throw std::runtime_error("VirtualAlloc commit failed");
    }

    void Protect(const std::uint64_t offset, const std::uint64_t size,
                 const MemoryProtection protection) override {
        ValidateRange(offset, size);
        DWORD previous{};
        if (!VirtualProtect(base_ + static_cast<SIZE_T>(offset), static_cast<SIZE_T>(size),
                            NativeProtection(protection), &previous)) {
            throw std::runtime_error("VirtualProtect failed");
        }
    }

    void Decommit(const std::uint64_t offset, const std::uint64_t size) override {
        ValidateRange(offset, size);
        if (!VirtualFree(base_ + static_cast<SIZE_T>(offset), static_cast<SIZE_T>(size),
                         MEM_DECOMMIT)) {
            throw std::runtime_error("VirtualFree decommit failed");
        }
    }

private:
    void ValidateRange(const std::uint64_t offset, const std::uint64_t size) const {
        if (size == 0 || offset % page_size_ != 0 || size % page_size_ != 0 ||
            offset > size_ || size > size_ - offset) {
            throw std::invalid_argument("virtual memory range is invalid or unaligned");
        }
    }

    std::byte* base_{};
    std::uint64_t size_{};
    std::uint64_t page_size_{};
};

}  // namespace

std::unique_ptr<VirtualMemoryReservation> ReserveVirtualMemory(const std::uint64_t size) {
    return std::make_unique<WindowsVirtualMemory>(size);
}

}  // namespace ogplay::hal
