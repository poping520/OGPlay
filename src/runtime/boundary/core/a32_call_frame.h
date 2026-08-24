#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "ogplay/memory/address.h"

namespace ogplay::cpu {
class A32State;
struct A32HostCallContext;
}

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

inline constexpr std::size_t kMaximumA32CallArguments = 11U;

template <typename T>
class GuestPtr final {
public:
    explicit constexpr GuestPtr(
        const memory::GuestAddress address = memory::GuestAddress{}) noexcept
        : address_(address) {}

    [[nodiscard]] constexpr memory::GuestAddress Address() const noexcept {
        return address_;
    }
    [[nodiscard]] constexpr bool IsNull() const noexcept {
        return address_.IsNull();
    }

    auto operator<=>(const GuestPtr&) const = default;

private:
    memory::GuestAddress address_{};
};

class GuestCString final {
public:
    explicit constexpr GuestCString(
        const memory::GuestAddress address = memory::GuestAddress{}) noexcept
        : address_(address) {}

    [[nodiscard]] constexpr memory::GuestAddress Address() const noexcept {
        return address_;
    }
    [[nodiscard]] constexpr bool IsNull() const noexcept {
        return address_.IsNull();
    }

    auto operator<=>(const GuestCString&) const = default;

private:
    memory::GuestAddress address_{};
};

class A32CallFrame final {
public:
    A32CallFrame(memory::AddressSpace& address_space,
                 const cpu::A32State& state, std::size_t parameter_count);
    A32CallFrame(memory::AddressSpace& address_space,
                 const cpu::A32HostCallContext& context,
                 std::size_t parameter_count);
    explicit A32CallFrame(std::span<const std::uint32_t> arguments,
                          std::uint64_t thread_id = 0,
                          std::uint32_t link_register = 0);

    [[nodiscard]] std::uint32_t Argument(std::size_t index) const;
    template <typename T>
    [[nodiscard]] T Scalar(const std::size_t index) const {
        static_assert(sizeof(T) == sizeof(std::uint32_t));
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        return std::bit_cast<T>(Argument(index));
    }
    template <typename T>
    [[nodiscard]] GuestPtr<T> Pointer(const std::size_t index) const {
        return GuestPtr<T>{memory::GuestAddress{Argument(index)}};
    }
    [[nodiscard]] GuestCString CString(const std::size_t index) const {
        return GuestCString{memory::GuestAddress{Argument(index)}};
    }
    [[nodiscard]] std::span<const std::uint32_t> Arguments() const noexcept;
    [[nodiscard]] std::array<std::uint32_t, 4> RegisterArguments() const noexcept;
    [[nodiscard]] std::uint64_t ThreadId() const noexcept;
    [[nodiscard]] std::uint32_t LinkRegister() const noexcept;

private:
    std::array<std::uint32_t, kMaximumA32CallArguments> arguments_{};
    std::size_t parameter_count_{};
    std::uint64_t thread_id_{};
    std::uint32_t link_register_{};
};

}  // namespace ogplay::runtime
