#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ogplay::cpu {
class A32State;
struct A32HostCallContext;
}

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

inline constexpr std::size_t kMaximumA32CallArguments = 9U;

class A32CallFrame final {
public:
    A32CallFrame(memory::AddressSpace& address_space,
                 const cpu::A32State& state, std::size_t parameter_count);
    A32CallFrame(memory::AddressSpace& address_space,
                 const cpu::A32HostCallContext& context,
                 std::size_t parameter_count);

    [[nodiscard]] std::uint32_t Argument(std::size_t index) const;
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
