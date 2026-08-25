#include "ogplay/runtime/integration/headless_bionic_runner.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/bionic/bionic_tls.h"
#include "ogplay/runtime/syscall/arm_kernel_helpers.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"
#include "ogplay/runtime/execution/guest_lifecycle.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

inline constexpr std::uint64_t kRootThreadId = 1;
inline constexpr memory::GuestAddress kTlsAddress{0x70000000U};
inline constexpr memory::GuestAddress kThreadInfoAddress{0x70001000U};
inline constexpr memory::GuestAddress kPreinitAddress{0x70002000U};
inline constexpr memory::GuestAddress kStackAddress{0x71000000U};
inline constexpr std::uint64_t kStackSize = UINT64_C(4) * 1024U * 1024U;
inline constexpr memory::GuestAddress kReturnAddress{0x7ff00000U};
inline constexpr memory::GuestAddress kArgumentAddress{0x7ff01000U};
inline constexpr memory::GuestAddress kPropertyAreaAddress{0x73000000U};

void WriteString(memory::AddressSpace& address_space,
                 const memory::GuestAddress address,
                 const std::string& value) {
    if (value.size() >= address_space.PageSize()) {
        throw HeadlessBionicRunError("headless guest argument is too long");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1U);
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    address_space.Write(address, bytes, kRootThreadId);
}

void WriteWord(memory::MemoryBus& memory_bus,
               const memory::GuestAddress address,
               const std::uint32_t value) {
    memory_bus.Write32(address, value, kRootThreadId);
}

void InitializeApi19Startup(memory::AddressSpace& address_space,
                            memory::MemoryBus& memory_bus) {
    const auto page_size = static_cast<std::uint32_t>(address_space.PageSize());
    const auto read_write = memory::PageProtection::read |
                            memory::PageProtection::write;
    address_space.Map({kThreadInfoAddress, page_size}, read_write);
    address_space.Map({kPreinitAddress, page_size}, read_write);

    // API 19 pthread_internal_t: next/prev, six-word pthread_attr_t,
    // tid/allocated, cond/join/return/flags/cleanup, then the TLS pointer.
    WriteWord(memory_bus, kThreadInfoAddress.Add(12), kStackAddress.Value());
    WriteWord(memory_bus, kThreadInfoAddress.Add(16),
              static_cast<std::uint32_t>(kStackSize));
    WriteWord(memory_bus, kThreadInfoAddress.Add(20), page_size);
    WriteWord(memory_bus, kThreadInfoAddress.Add(32),
              static_cast<std::uint32_t>(kRootThreadId));
    WriteWord(memory_bus, kThreadInfoAddress.Add(60), kTlsAddress.Value());

    const auto argv = kPreinitAddress.Add(0x40);
    const auto envp = kPreinitAddress.Add(0x50);
    const auto auxv = kPreinitAddress.Add(0x60);
    const auto abort_message = kPreinitAddress.Add(0x80);
    const auto program_name = kPreinitAddress.Add(0xa0);
    const auto random_bytes = kPreinitAddress.Add(0xc0);
    WriteWord(memory_bus, kPreinitAddress, 1);
    WriteWord(memory_bus, kPreinitAddress.Add(4), argv.Value());
    WriteWord(memory_bus, kPreinitAddress.Add(8), envp.Value());
    WriteWord(memory_bus, kPreinitAddress.Add(12), auxv.Value());
    WriteWord(memory_bus, kPreinitAddress.Add(16), abort_message.Value());
    WriteWord(memory_bus, argv, program_name.Value());
    WriteWord(memory_bus, argv.Add(4), 0);
    WriteWord(memory_bus, envp, 0);
    WriteWord(memory_bus, auxv, 25);  // AT_RANDOM
    WriteWord(memory_bus, auxv.Add(4), random_bytes.Value());
    WriteWord(memory_bus, auxv.Add(8), 6);  // AT_PAGESZ
    WriteWord(memory_bus, auxv.Add(12), page_size);
    WriteWord(memory_bus, auxv.Add(16), 0);  // AT_NULL
    WriteWord(memory_bus, auxv.Add(20), 0);
    WriteWord(memory_bus, abort_message, 0);
    WriteString(address_space, program_name, "ogplay-headless");
    WriteWord(memory_bus, random_bytes, 0x4f47504cU);
    WriteWord(memory_bus, random_bytes.Add(4), 0x41594d32U);
    WriteWord(memory_bus, random_bytes.Add(8), 0x10293847U);
    WriteWord(memory_bus, random_bytes.Add(12), 0x56473829U);
}

void InitializeEmptyPropertyArea(
    memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
    const loader::Elf32LinkNamespace& link_namespace) {
    address_space.Map({kPropertyAreaAddress, address_space.PageSize()},
                      memory::PageProtection::read |
                          memory::PageProtection::write);
    WriteWord(memory_bus, kPropertyAreaAddress, 20);  // empty root prop_bt
    WriteWord(memory_bus, kPropertyAreaAddress.Add(4), 0);
    WriteWord(memory_bus, kPropertyAreaAddress.Add(8), 0x504f5250U);
    WriteWord(memory_bus, kPropertyAreaAddress.Add(12), 0xfc6ed0abU);
    const auto exported = loader::LookupElf32Symbol(
        link_namespace, "__system_property_area__");
    WriteWord(memory_bus, exported.address, kPropertyAreaAddress.Value());
}

[[nodiscard]] std::vector<GuestLifecycleModule> LifecycleModules(
    const loader::Elf32LoadedNamespace& loaded,
    const std::span<const loader::Elf32ModuleInput> inputs) {
    std::vector<GuestLifecycleModule> result;
    result.reserve(loaded.modules.size());
    for (std::size_t index = 0; index < loaded.modules.size(); ++index) {
        result.push_back(
            {index, inputs[index].load_bias, loaded.modules[index].lifecycle});
    }
    return result;
}

[[nodiscard]] std::uint64_t Invoke(
    cpu::Cpu& cpu, A32SyscallDispatcher& dispatcher,
    GuestThreadLifecycle& lifecycle, memory::GuestAddress function,
    const std::uint32_t argument, const std::uint64_t remaining_ticks,
    std::vector<HeadlessSyscallCall>& syscall_calls) {
    if (function.Value() == 0 || remaining_ticks == 0) {
        throw HeadlessBionicRunError("invalid headless guest invocation");
    }
    auto state = cpu.GetState();
    const auto thumb = (function.Value() & 1U) != 0;
    state.SetState(thumb ? cpu::ExecutionState::thumb
                         : cpu::ExecutionState::a32);
    state.SetRegister(cpu::CoreRegister::pc, function.Value() & ~1U);
    state.SetRegister(cpu::CoreRegister::lr, kReturnAddress.Value());
    state.SetRegister(cpu::CoreRegister::sp,
                      kStackAddress.Add(kStackSize - 16U).Value());
    state.SetRegister(cpu::CoreRegister::r0, argument);
    state.SetRegister(cpu::CoreRegister::r1, 0);
    state.SetRegister(cpu::CoreRegister::r2, 0);
    state.SetRegister(cpu::CoreRegister::r3, 0);
    cpu.SetState(state);

    std::uint64_t consumed{};
    std::string syscall_trace;
    while (consumed < remaining_ticks) {
        const auto stopped = cpu.Run(remaining_ticks - consumed);
        consumed += stopped.ticks_consumed;
        if (stopped.reason == cpu::RunStopReason::budget_exhausted) break;
        if (stopped.reason == cpu::RunStopReason::supervisor_call &&
            stopped.immediate == 1 && stopped.pc == kReturnAddress) {
            return consumed;
        }
        if (stopped.reason != cpu::RunStopReason::supervisor_call ||
            stopped.immediate != 0) {
            const auto stopped_state = cpu.GetState();
            const auto fault = stopped.fault.has_value()
                                   ? " fault=" + std::to_string(
                                         stopped.fault->address.Value()) +
                                         " access=" + std::to_string(
                                         static_cast<std::uint8_t>(
                                             stopped.fault->access))
                                   : std::string{};
            throw HeadlessBionicRunError(
                "headless guest stopped outside the syscall boundary: reason=" +
                std::to_string(static_cast<std::uint8_t>(stopped.reason)) +
                " pc=" + std::to_string(stopped.pc.Value()) +
                " instruction=" + std::to_string(stopped.instruction) +
                " function=" + std::to_string(function.Value()) + fault +
                " r0=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r0)) +
                " r1=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r1)) +
                " r2=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r2)) +
                " r3=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r3)) +
                " r4=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r4)) +
                " r5=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r5)) +
                " r6=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::r6)) +
                " lr=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::lr)) +
                " sp=" + std::to_string(stopped_state.Register(
                    cpu::CoreRegister::sp)) + " syscalls=" + syscall_trace);
        }
        const auto syscall_number =
            cpu.GetState().Register(cpu::CoreRegister::r7);
        std::array<std::uint32_t, 7> syscall_arguments{};
        for (std::size_t index = 0; index < syscall_arguments.size(); ++index) {
            syscall_arguments[index] = cpu.GetState().Register(
                static_cast<cpu::CoreRegister>(index));
        }
        const auto dispatched =
            DispatchAndroidArmSupervisorCall(cpu, stopped, dispatcher);
        if (!dispatched.has_value()) {
            throw HeadlessBionicRunError("headless guest SVC was not handled");
        }
        syscall_calls.push_back({cpu.GetState().ThreadId(), syscall_number,
                                 syscall_arguments,
                                 dispatched->return_value});
        syscall_trace += "," + std::to_string(syscall_number) + "(";
        for (std::size_t index = 0; index < 6; ++index) {
            if (index != 0) syscall_trace += ":";
            syscall_trace += std::to_string(syscall_arguments[index]);
        }
        syscall_trace += ")=" + std::to_string(dispatched->return_value);
        if (syscall_trace.size() > 512) syscall_trace.erase(0, 256);
        auto updated = cpu.GetState();
        updated.SetThreadPointer(
            lifecycle.State(updated.ThreadId()).thread_pointer);
        cpu.SetState(updated);
    }
    throw HeadlessBionicRunError(
        "headless guest exhausted its tick budget at pc=" +
        std::to_string(
            cpu.GetState().Register(cpu::CoreRegister::pc)) +
        " function=" + std::to_string(function.Value()));
}

[[nodiscard]] std::vector<std::byte> ReadOutput(
    VirtualFileSystem& vfs, const std::string& path) {
    VfsFileInfo info;
    try {
        info = vfs.Stat(path);
    } catch (const VfsError& error) {
        if (error.ErrorNumber() == 2) return {};
        throw;
    }
    if (info.size > std::numeric_limits<std::size_t>::max()) {
        throw HeadlessBionicRunError("headless guest output is too large");
    }
    std::vector<std::byte> result(static_cast<std::size_t>(info.size));
    const auto descriptor = vfs.Open(path, {true, false, false, false});
    try {
        const auto count = vfs.Read(descriptor, result);
        if (count != result.size()) {
            throw HeadlessBionicRunError("headless guest output was truncated");
        }
        vfs.Close(descriptor);
    } catch (...) {
        try {
            vfs.Close(descriptor);
        } catch (const VfsError&) {
        }
        throw;
    }
    return result;
}

}  // namespace

HeadlessBionicRunReport RunHeadlessBionicEntry(
    const HeadlessBionicRunRequest& request) {
#if !OGPLAY_HAS_DYNARMIC
    static_cast<void>(request);
    throw HeadlessBionicRunError(
        "headless Bionic execution requires the Dynarmic backend");
#else
    static_cast<void>(SelectBionicProfile(request.api));
    if (request.root_module.empty() || request.entry_symbol.empty() ||
        request.modules.empty() || request.vfs_path.empty() ||
        request.maximum_ticks == 0) {
        throw HeadlessBionicRunError("headless Bionic request is incomplete");
    }

    memory::AddressSpace address_space;
    memory::CheckedMemoryBus memory_bus(address_space);
    MapArmKernelHelpers(address_space);
    auto loaded = loader::LoadElf32ModuleNamespace(
        request.root_module, request.modules, address_space);
    const auto entry = loader::LookupElf32Symbol(
        loaded.link_namespace, request.entry_symbol);

    const auto read_write = memory::PageProtection::read |
                            memory::PageProtection::write;
    InitializeApi19Startup(address_space, memory_bus);
    const auto tls = CreateBionicTlsBlock(
        address_space, kTlsAddress, kThreadInfoAddress, kPreinitAddress);
    address_space.Map({kStackAddress, kStackSize}, read_write);
    address_space.Map({kReturnAddress, address_space.PageSize()}, read_write);
    memory_bus.Write32(kReturnAddress, 0xef000001U, kRootThreadId);
    address_space.Protect({kReturnAddress, address_space.PageSize()},
                          memory::PageProtection::read |
                              memory::PageProtection::execute);
    address_space.Map({kArgumentAddress, address_space.PageSize()}, read_write);
    WriteString(address_space, kArgumentAddress, request.vfs_path);
    InitializeEmptyPropertyArea(address_space, memory_bus,
                                loaded.link_namespace);

    core::CapabilityLedger ledger;
    auto dispatcher = CreateAndroidArmSyscallDispatcher(ledger);
    if (request.syscall_observer) {
        dispatcher.SetObserver(
            [&observer = request.syscall_observer](
                const A32SyscallFrame& frame, const std::int32_t result) {
                observer({frame.thread_id, frame.number, frame.arguments,
                          result});
            });
    }
    hal::RealtimeClock clock;
    cpu::FutexTable futex_table;
    VirtualFileSystem vfs;
    std::vector<GuestVmaAnnotation> vma_annotations;
    std::mutex vma_annotations_mutex;
    GuestThreadLifecycle lifecycle;
    lifecycle.Register(kRootThreadId, tls.thread_pointer);
    BindAndroidTimeSyscalls(dispatcher, clock, address_space);
    BindAndroidMemorySyscalls(dispatcher, address_space);
    BindAndroidThreadSyscalls(dispatcher, futex_table, memory_bus);
    BindAndroidSignalSyscalls(dispatcher, address_space);
    BindAndroidProcessSyscalls(
        dispatcher, address_space,
        [&vma_annotations, &vma_annotations_mutex](
            const GuestVmaAnnotation& annotation) {
            std::scoped_lock lock(vma_annotations_mutex);
            vma_annotations.push_back(annotation);
        });
    BindAndroidFileSyscalls(dispatcher, vfs, address_space);
    BindAndroidThreadLifecycleSyscalls(dispatcher, lifecycle);
    BindAndroidArmPrivateSyscalls(
        dispatcher, address_space, [&lifecycle](const std::uint64_t thread_id,
                                 const memory::GuestAddress pointer) {
            lifecycle.SetThreadPointer(thread_id, pointer);
            return true;
        });

    auto execution_context =
        std::make_shared<cpu::DynarmicExecutionContext>(64);
    cpu::GuestThreadGroup threads{[&memory_bus, execution_context] {
        return std::make_unique<cpu::DynarmicCpu>(memory_bus,
                                                  execution_context);
    }};
    GuestCloneThreadRuntime clone_runtime{
        threads, dispatcher, lifecycle, address_space, memory_bus,
        futex_table};
    cpu::DynarmicCpu root_cpu(memory_bus, execution_context);
    cpu::A32State root_state;
    root_state.SetThreadId(kRootThreadId);
    root_state.SetThreadPointer(tls.thread_pointer);
    root_cpu.SetState(root_state);

    const auto lifecycle_modules = LifecycleModules(loaded, request.modules);
    const auto init = BuildGuestInitializationPlan(
        lifecycle_modules, loaded.link_namespace.load_order);
    std::uint64_t ticks{};
    std::vector<HeadlessSyscallCall> root_syscalls;
    try {
        ExecuteGuestLifecycle(init, [&](const GuestLifecycleCall& call) {
            const auto used = Invoke(root_cpu, dispatcher, lifecycle,
                                     call.address, 0,
                                     request.maximum_ticks - ticks,
                                     root_syscalls);
            ticks += used;
        });
    } catch (const HeadlessBionicRunError& error) {
        auto message = std::string(error.what());
        try {
            const auto guest_environment = loader::LookupElf32Symbol(
                loaded.link_namespace, "environ");
            const auto environment_address = memory_bus.Read32(
                guest_environment.address, kRootThreadId);
            message += " environ=" + std::to_string(environment_address) +
                       " environ0=" + std::to_string(memory_bus.Read32(
                           memory::GuestAddress{environment_address},
                           kRootThreadId));
            const auto property_area = loader::LookupElf32Symbol(
                loaded.link_namespace, "__system_property_area__");
            message += " property_area=" + std::to_string(memory_bus.Read32(
                property_area.address, kRootThreadId));
        } catch (const std::exception&) {
        }
        for (const auto& hit : ledger.Unimplemented()) {
            message += " unimplemented=" + hit.id;
        }
        throw HeadlessBionicRunError(std::move(message));
    }

    ticks += Invoke(root_cpu, dispatcher, lifecycle, entry.address,
                    kArgumentAddress.Value(), request.maximum_ticks - ticks,
                    root_syscalls);
    const auto entry_result = std::bit_cast<std::int32_t>(
        root_cpu.GetState().Register(cpu::CoreRegister::r0));

    std::size_t child_count{};
    for (const auto& child : lifecycle.States()) {
        if (child.thread_id == kRootThreadId) continue;
        const auto joined = clone_runtime.Join(child.thread_id);
        if (joined.run.reason != GuestThreadRunStop::guest_exit ||
            !joined.run.exit.has_value()) {
            auto message =
                "guest child did not exit cleanly: run=" +
                std::to_string(static_cast<std::uint8_t>(joined.run.reason)) +
                " cpu=" + std::to_string(static_cast<std::uint8_t>(
                              joined.run.cpu_stop.reason)) +
                " pc=" +
                std::to_string(joined.run.cpu_stop.pc.Value());
            if (joined.run.cpu_stop.fault.has_value()) {
                message += " fault=" + std::to_string(
                    joined.run.cpu_stop.fault->address.Value());
            }
            for (const auto& hit : ledger.Unimplemented()) {
                message += " unimplemented=" + hit.id;
            }
            throw HeadlessBionicRunError(std::move(message));
        }
        ++child_count;
    }

    auto fini_order = loaded.link_namespace.load_order;
    std::reverse(fini_order.begin(), fini_order.end());
    const auto fini = BuildGuestFinalizationPlan(
        lifecycle_modules, fini_order);
    ExecuteGuestLifecycle(fini, [&](const GuestLifecycleCall& call) {
        const auto used = Invoke(root_cpu, dispatcher, lifecycle, call.address,
                                 0, request.maximum_ticks - ticks,
                                 root_syscalls);
        ticks += used;
    });

    return {entry_result, ticks, child_count, ReadOutput(vfs, request.vfs_path),
            ledger.Unimplemented(), std::move(root_syscalls),
            std::move(vma_annotations)};
#endif
}

}  // namespace ogplay::runtime
