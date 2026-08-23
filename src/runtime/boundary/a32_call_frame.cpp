#include "ogplay/runtime/boundary/a32_call_frame.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "ogplay/cpu/cpu.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

A32CallFrame::A32CallFrame(memory::AddressSpace& address_space,
                           const cpu::A32State& state,
                           const std::size_t parameter_count)
    : parameter_count_(parameter_count), thread_id_(state.ThreadId()),
      link_register_(state.Register(cpu::CoreRegister::lr)) {
    if (parameter_count > arguments_.size()) {
        throw std::length_error("A32 HLE call exceeds the supported argument count");
    }
    constexpr std::array registers{
        cpu::CoreRegister::r0, cpu::CoreRegister::r1,
        cpu::CoreRegister::r2, cpu::CoreRegister::r3};
    for (std::size_t index = 0;
         index < std::min(parameter_count, registers.size()); ++index) {
        arguments_[index] = state.Register(registers[index]);
    }
    if (parameter_count <= registers.size()) return;

    const auto stack_words = parameter_count - registers.size();
    std::array<std::byte,
               (kMaximumA32CallArguments - registers.size()) *
                   sizeof(std::uint32_t)> bytes{};
    const auto stack_bytes = std::span(bytes).first(
        stack_words * sizeof(std::uint32_t));
    address_space.Read(
        memory::GuestAddress{state.Register(cpu::CoreRegister::sp)}, stack_bytes,
        thread_id_);
    for (std::size_t word = 0; word < stack_words; ++word) {
        std::uint32_t value{};
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(
                             bytes[word * sizeof(value) + byte]))
                     << (byte * 8U);
        }
        arguments_[registers.size() + word] = value;
    }
}

A32CallFrame::A32CallFrame(memory::AddressSpace& address_space,
                           const cpu::A32HostCallContext& context,
                           const std::size_t parameter_count)
    : parameter_count_(parameter_count), thread_id_(context.thread_id),
      link_register_(context.registers[14]) {
    if (parameter_count > arguments_.size()) {
        throw std::length_error("A32 HLE call exceeds the supported argument count");
    }
    const auto register_count = std::min(parameter_count, std::size_t{4});
    std::copy_n(context.registers.begin(), register_count, arguments_.begin());
    if (parameter_count <= 4U) return;

    const auto stack_words = parameter_count - 4U;
    std::array<std::byte, (kMaximumA32CallArguments - 4U) *
                              sizeof(std::uint32_t)>
        bytes{};
    const auto stack_bytes =
        std::span(bytes).first(stack_words * sizeof(std::uint32_t));
    address_space.Read(memory::GuestAddress{context.registers[13]}, stack_bytes,
                       thread_id_);
    for (std::size_t word = 0; word < stack_words; ++word) {
        std::uint32_t value{};
        for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(
                             bytes[word * sizeof(value) + byte]))
                     << (byte * 8U);
        }
        arguments_[4U + word] = value;
    }
}

std::uint32_t A32CallFrame::Argument(const std::size_t index) const {
    if (index >= parameter_count_) {
        throw std::out_of_range("A32 HLE argument index is outside the call frame");
    }
    return arguments_[index];
}

std::span<const std::uint32_t> A32CallFrame::Arguments() const noexcept {
    return std::span(arguments_).first(parameter_count_);
}

std::array<std::uint32_t, 4> A32CallFrame::RegisterArguments() const noexcept {
    std::array<std::uint32_t, 4> result{};
    std::copy_n(arguments_.begin(), std::min(parameter_count_, result.size()),
                result.begin());
    return result;
}

std::uint64_t A32CallFrame::ThreadId() const noexcept { return thread_id_; }

std::uint32_t A32CallFrame::LinkRegister() const noexcept {
    return link_register_;
}

}  // namespace ogplay::runtime
