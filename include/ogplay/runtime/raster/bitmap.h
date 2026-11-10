#pragma once

#include "ogplay/memory/address.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class SkBitmap;

namespace ogplay::runtime::raster {

// Stores no independent pixel copy. The lease outlives every Skia reference
// to the supplied pixels, including images produced from the bitmap.
class Bitmap final {
public:
    Bitmap(int width, int height, int config, bool premultiplied, bool alpha, std::span<std::byte> pixels,
           std::shared_ptr<void> lease, memory::GuestAddress guest_address);
    ~Bitmap();

    static std::size_t AllocationSize(int width, int height, int config);
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] std::uint32_t Stride() const noexcept;
    [[nodiscard]] int Format() const noexcept;
    [[nodiscard]] int Config() const noexcept;
    [[nodiscard]] std::uint32_t Locks() const noexcept;
    [[nodiscard]] std::uint32_t Revision() const;
    [[nodiscard]] std::uint32_t Get(std::size_t index) const;
    void Set(std::size_t index, std::uint32_t argb);
    [[nodiscard]] std::vector<std::uint32_t> Snapshot() const;
    void Read(int x, int y, int width, int height, std::span<std::uint32_t> argb) const;
    void Write(int x, int y, int width, int height, std::span<const std::uint32_t> argb);
    [[nodiscard]] std::span<std::byte> RawPixels() const;

    [[nodiscard]] bool Recycled() const noexcept;
    [[nodiscard]] bool HasAlpha() const;
    [[nodiscard]] bool Premultiplied() const;
    void SetAlpha(bool alpha, bool premultiplied);
    void Reconfigure(int width, int height, int config, bool alpha, bool premultiplied);
    void Retire();
    void AbortLocks();
    [[nodiscard]] memory::GuestAddress Lock();
    void Unlock();
    [[nodiscard]] SkBitmap& SkiaBitmap() noexcept;

    bool mipmap{};

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::raster
