#pragma once
#include <cstdint>
#include <vector>
#include "ogplay/runtime/dexvm/nio_runtime.h"

namespace ogplay::runtime {
// One authoritative pixel value: logical colors while unlocked, guest bytes
// while locked. Concurrent unsynchronized pixel writers are a caller error.
class BitmapPixels final {
public:
    BitmapPixels(std::int32_t width, std::int32_t height, std::int32_t config,
                 std::vector<std::uint32_t> colors = {});
    std::uint32_t Get(std::size_t index) const;
    void Set(std::size_t index, std::uint32_t color);
    std::vector<std::uint32_t> Snapshot() const;
    std::size_t Size() const noexcept { return count_; }
    std::uint32_t Stride() const noexcept { return stride_; }
    std::int32_t Format() const noexcept { return format_; }
    std::uint32_t Locks() const noexcept { return locks_; }
    std::uint64_t Revision() const noexcept { return revision_; }
    memory::GuestAddress Lock(const dexvm::NioDirectMemoryAccess& access);
    void Unlock();
    void Retire();
    void AbortLocks();
private:
    std::uint32_t Offset(std::size_t index) const;
    void Encode(std::uint32_t color, std::span<std::byte> bytes) const;
    std::uint32_t Decode(std::span<const std::byte> bytes) const;
    std::uint32_t width_, height_, stride_, bytes_per_pixel_, byte_count_, locks_{};
    std::int32_t format_;
    std::size_t count_;
    std::vector<std::uint32_t> colors_;
    dexvm::NioDirectMemoryAccess access_;
    memory::GuestAddress address_;
    bool retired_{};
    std::uint64_t revision_{1};
};
}
