#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ogplay::memory {

inline constexpr std::uint64_t kGuestAddressSpaceSize = UINT64_C(1) << 32U;
inline constexpr std::uint32_t kGuestLowGuardSize = UINT32_C(0x10000);

class GuestAddress final {
public:
    explicit constexpr GuestAddress(const std::uint32_t value = 0) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool IsNull() const noexcept { return value_ == 0; }

    [[nodiscard]] constexpr GuestAddress Add(const std::uint64_t offset) const {
        if (offset > std::numeric_limits<std::uint32_t>::max() -
                         static_cast<std::uint64_t>(value_)) {
            throw std::overflow_error("guest address addition overflow");
        }
        return GuestAddress(value_ + static_cast<std::uint32_t>(offset));
    }

    [[nodiscard]] constexpr GuestAddress Subtract(const std::uint64_t offset) const {
        if (offset > value_) throw std::overflow_error("guest address subtraction underflow");
        return GuestAddress(value_ - static_cast<std::uint32_t>(offset));
    }

    [[nodiscard]] constexpr std::uint64_t OffsetTo(const GuestAddress other) const {
        if (other < *this) throw std::invalid_argument("guest address order is reversed");
        return static_cast<std::uint64_t>(other.value_) - value_;
    }

    [[nodiscard]] constexpr bool IsAligned(const std::uint64_t alignment) const {
        ValidateAlignment(alignment);
        return (value_ & (alignment - 1U)) == 0;
    }

    [[nodiscard]] constexpr GuestAddress AlignDown(const std::uint64_t alignment) const {
        ValidateAlignment(alignment);
        return GuestAddress(static_cast<std::uint32_t>(value_ & ~(alignment - 1U)));
    }

    [[nodiscard]] constexpr GuestAddress AlignUp(const std::uint64_t alignment) const {
        ValidateAlignment(alignment);
        const auto mask = alignment - 1U;
        const auto aligned = (static_cast<std::uint64_t>(value_) + mask) & ~mask;
        if (aligned >= kGuestAddressSpaceSize) {
            throw std::overflow_error("aligned guest address is outside the address space");
        }
        return GuestAddress(static_cast<std::uint32_t>(aligned));
    }

    auto operator<=>(const GuestAddress&) const = default;

private:
    static constexpr void ValidateAlignment(const std::uint64_t alignment) {
        if (alignment == 0 || (alignment & (alignment - 1U)) != 0 ||
            alignment > kGuestAddressSpaceSize) {
            throw std::invalid_argument("guest alignment must be a power of two up to 4 GiB");
        }
    }

    std::uint32_t value_{};
};

class GuestRange final {
public:
    constexpr GuestRange(const GuestAddress start, const std::uint64_t size)
        : start_(start), size_(size) {
        if (size_ == 0) throw std::invalid_argument("guest range must not be empty");
        if (size_ > kGuestAddressSpaceSize - start_.Value()) {
            throw std::overflow_error("guest range exceeds the address space");
        }
    }

    [[nodiscard]] constexpr GuestAddress Start() const noexcept { return start_; }
    [[nodiscard]] constexpr std::uint64_t Size() const noexcept { return size_; }
    [[nodiscard]] constexpr std::uint64_t EndExclusive() const noexcept {
        return static_cast<std::uint64_t>(start_.Value()) + size_;
    }

    [[nodiscard]] constexpr bool Contains(const GuestAddress address) const noexcept {
        return address >= start_ &&
               static_cast<std::uint64_t>(address.Value()) < EndExclusive();
    }

    [[nodiscard]] constexpr bool Contains(const GuestRange& other) const noexcept {
        return other.start_ >= start_ && other.EndExclusive() <= EndExclusive();
    }

    [[nodiscard]] constexpr bool Overlaps(const GuestRange& other) const noexcept {
        return static_cast<std::uint64_t>(start_.Value()) < other.EndExclusive() &&
               static_cast<std::uint64_t>(other.start_.Value()) < EndExclusive();
    }

    [[nodiscard]] constexpr bool IsAligned(const std::uint64_t alignment) const {
        return start_.IsAligned(alignment) && (size_ & (alignment - 1U)) == 0;
    }

    bool operator==(const GuestRange&) const = default;

private:
    GuestAddress start_;
    std::uint64_t size_;
};

[[nodiscard]] constexpr GuestRange LowAddressGuard() {
    return GuestRange(GuestAddress{}, kGuestLowGuardSize);
}

}  // namespace ogplay::memory
