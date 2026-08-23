#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/boundary/boundary_symbol.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_fault.h"

namespace ogplay::runtime {

struct BoundaryCallServices final {
    memory::AddressSpace& address_space;
    BoundaryFaultStore& faults;
    void* gpu_owner{};
    void (*record_gpu_call)(void*, std::size_t,
                            const std::array<std::uint32_t, 4>&, bool){};

    void RecordFastFault(
        const cpu::A32HostCallContext& context) const noexcept {
        faults.RecordCurrent(context);
    }
    void RecordGpuCall(const std::size_t slot,
                       const std::array<std::uint32_t, 4>& arguments,
                       const bool gpu) const {
        if (record_gpu_call != nullptr) {
            record_gpu_call(gpu_owner, slot, arguments, gpu);
        }
    }
};

using BoundarySlowInvokeFn = std::uint32_t (*)(void*, const A32CallFrame&);

struct BoundaryHotEntry final {
    cpu::HostCallResult (*invoke)(void*, cpu::A32HostCallContext&) noexcept{};
    void* self{};
    BoundarySlowInvokeFn slow{};
    bool gpu{};
};

template <typename Module, auto Method>
std::uint32_t InvokeBoundarySlow(void* userdata, const A32CallFrame& call) {
    return (static_cast<Module*>(userdata)->*Method)(call);
}

template <typename Module, auto Method, std::size_t ParameterCount, bool Gpu>
cpu::HostCallResult InvokeBoundaryFast(
    void* userdata, cpu::A32HostCallContext& context) noexcept {
    if (userdata == nullptr) return cpu::HostCallResult::unhandled;
    auto& module = *static_cast<Module*>(userdata);
    auto& services = module.CallServices();
    try {
        const A32CallFrame call(services.address_space, context, ParameterCount);
        const auto arguments = call.RegisterArguments();
        const auto result = (module.*Method)(call);
        if constexpr (Gpu) {
            constexpr std::uint32_t kThunkStride = 4U;
            const auto slot = static_cast<std::size_t>(
                (context.pc.Value() - kBionicHleThunkBegin) / kThunkStride);
            services.RecordGpuCall(slot, arguments, true);
        }
        context.registers[0] = result;
        return cpu::HostCallResult::handled;
    } catch (...) {
        services.RecordFastFault(context);
        return cpu::HostCallResult::fault;
    }
}

}  // namespace ogplay::runtime
