#include "ogplay/runtime/integration/native_activity_runner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/loader/lifecycle.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"
#include "ogplay/runtime/execution/guest_lifecycle.h"
#include "ogplay/runtime/integration/api19_guest_process.h"
#include "ogplay/runtime/syscall/arm_kernel_helpers.h"
#include "ogplay/runtime/syscall/syscall_bridge.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kRootThreadId = 1;
constexpr memory::GuestAddress kActivityAddress{0x6f000000U};
constexpr memory::GuestAddress kCallbacksAddress{0x6f000100U};
constexpr memory::GuestAddress kInternalPathAddress{0x6f000200U};
constexpr memory::GuestAddress kExternalPathAddress{0x6f000240U};
constexpr memory::GuestAddress kObbPathAddress{0x6f000280U};
constexpr std::uint32_t kFakeNativeWindow = 0x6e010000U;
constexpr std::uint32_t kFakeInputQueue = 0x6e020000U;

enum CallbackIndex : std::size_t {
    start = 0, resume = 1, pause = 3, stop = 4, destroy = 5,
    window_focus = 6, window_created = 7, window_destroyed = 10,
    input_created = 11, input_destroyed = 12,
};

void Write32(memory::MemoryBus& bus, const memory::GuestAddress address,
             const std::uint32_t value) {
    bus.Write32(address, value, kRootThreadId);
}

void WriteString(memory::AddressSpace& address_space,
                 const memory::GuestAddress address,
                 const std::string_view value) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1);
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    address_space.Write(address, bytes, kRootThreadId);
}

std::vector<GuestLifecycleModule> MakeLifecycleModules(
    const loader::Elf32LoadedNamespace& loaded,
    const std::span<const loader::Elf32ModuleInput> inputs) {
    std::vector<GuestLifecycleModule> result;
    result.reserve(loaded.modules.size());
    for (std::size_t index = 0; index < loaded.modules.size(); ++index) {
        result.push_back({index, inputs[index].load_bias, loaded.modules[index].lifecycle});
    }
    return result;
}

}  // namespace

class NativeActivitySession::Impl final {
public:
    explicit Impl(const NativeActivityRunRequest& request)
        : boundary_(address_space_, request.backend, request.width, request.height,
                    request.supersample_factor),
          dispatcher_(CreateAndroidArmSyscallDispatcher(ledger_)),
          threads_([this] {
              auto result = std::make_unique<cpu::DynarmicCpu>(
                  memory_bus_, execution_context_);
              result->SetHostCallHook(boundary_.FastHostCallHook());
              return result;
          }), maximum_ticks_(request.maximum_ticks_per_call), progress_(request.progress) {
#if !OGPLAY_HAS_DYNARMIC
        static_cast<void>(request);
        throw NativeActivityRunError("NativeActivity execution requires Dynarmic");
#else
        if (request.api != 19 || request.root_module.empty() || request.modules.empty() ||
            request.maximum_ticks_per_call == 0) {
            throw NativeActivityRunError("NativeActivity request requires API 19 modules and a tick budget");
        }
        const auto& profile = SelectBionicProfile(request.api);
        Progress("mapping-boundaries");
        MapArmKernelHelpers(address_space_);
        boundary_.MapThunks();
        loaded_ = loader::LoadElf32ModuleNamespace(
            request.root_module, request.modules, address_space_,
            [&profile, this](const std::string_view root,
                             const std::span<const loader::Elf32LinkModule> guest) {
                return BuildBionicLinkNamespace(profile, root, guest, boundary_.Symbols());
            });
        Progress("modules-loaded");

        process_memory_ = InitializeApi19GuestProcess(
            address_space_, memory_bus_, loaded_.link_namespace,
            {kRootThreadId, "ogplay-native-activity"});
        InitializeActivityMemory(request.api);
        Progress("process-memory-ready");

        lifecycle_.Register(kRootThreadId, process_memory_.thread_pointer);
        BindSyscalls();
        clone_runtime_ = std::make_unique<GuestCloneThreadRuntime>(
            threads_, dispatcher_, lifecycle_, address_space_, memory_bus_, futex_table_,
            2, 100000, [this](cpu::Cpu& cpu, const cpu::RunResult& stopped) {
                return HandleBoundary(cpu, stopped);
            });
        root_cpu_ = std::make_unique<cpu::DynarmicCpu>(memory_bus_, execution_context_);
        root_cpu_->SetHostCallHook(boundary_.FastHostCallHook());
        cpu::A32State root_state;
        root_state.SetThreadId(kRootThreadId);
        root_state.SetThreadPointer(process_memory_.thread_pointer);
        root_cpu_->SetState(root_state);

        lifecycle_modules_ = MakeLifecycleModules(loaded_, request.modules);
        for (const auto module_index : loaded_.link_namespace.load_order) {
            if (module_index < loaded_.modules.size()) guest_load_order_.push_back(module_index);
        }
        const auto initialization = BuildGuestInitializationPlan(
            lifecycle_modules_, guest_load_order_);
        ExecuteGuestLifecycle(initialization, [this](const GuestLifecycleCall& call) {
            Invoke(call.address, {});
        });
        Progress("guest-initializers-complete");

        const auto entry = loader::LookupElf32Symbol(
            loaded_.link_namespace, "ANativeActivity_onCreate");
        Progress("native-activity-on-create-enter");
        Invoke(entry.address, {kActivityAddress.Value(), 0, 0, 0});
        Progress("native-activity-on-create-return");
        for (std::size_t index = 0; index < callbacks_.size(); ++index) {
            callbacks_[index] = memory_bus_.Read32(kCallbacksAddress.Add(index * 4), kRootThreadId);
        }
        RequireCallback(start); RequireCallback(resume); RequireCallback(destroy);
        RequireCallback(window_focus); RequireCallback(window_created);
        RequireCallback(window_destroyed); RequireCallback(input_created);
        RequireCallback(input_destroyed);
        Progress("callback-start-enter"); InvokeCallback(start, {kActivityAddress.Value()});
        Progress("callback-start-return"); InvokeCallback(resume, {kActivityAddress.Value()});
        Progress("callback-resume-return");
        InvokeCallback(window_created, {kActivityAddress.Value(), kFakeNativeWindow});
        Progress("callback-window-created-return");
        InvokeCallback(input_created, {kActivityAddress.Value(), kFakeInputQueue});
        Progress("callback-input-created-return");
        InvokeCallback(window_focus, {kActivityAddress.Value(), 1});
        running_ = true;
        Progress("native-activity-running");
#endif
    }

    ~Impl() {
        if (running_) {
            try { Stop(); } catch (const std::exception&) {}
        }
    }

    void PushInput(const AndroidBoundaryInput& input) {
        ThrowIfChildFailed();
        if (!running_) throw NativeActivityRunError("NativeActivity session is stopped");
        boundary_.PushInput(input);
    }

    std::optional<AndroidBoundaryFrame> TakeLatestFrame() {
        ThrowIfChildFailed();
        return boundary_.TakeLatestFrame();
    }
    void RecycleFrame(AndroidBoundaryFrame&& frame) {
        ThrowIfChildFailed();
        boundary_.RecycleFrame(std::move(frame));
    }

    void Stop() {
        if (!running_) return;
        InvokeCallback(window_focus, {kActivityAddress.Value(), 0});
        InvokeCallback(input_destroyed, {kActivityAddress.Value(), kFakeInputQueue});
        InvokeCallback(window_destroyed, {kActivityAddress.Value(), kFakeNativeWindow});
        InvokeCallback(pause, {kActivityAddress.Value()});
        InvokeCallback(stop, {kActivityAddress.Value()});
        InvokeCallback(destroy, {kActivityAddress.Value()});

        for (const auto& child : lifecycle_.States()) {
            if (child.thread_id == kRootThreadId) continue;
            const auto joined = clone_runtime_->Join(child.thread_id);
            if (joined.run.reason != GuestThreadRunStop::guest_exit) {
                throw NativeActivityRunError("NativeActivity guest child did not exit cleanly");
            }
        }
        auto fini_order = guest_load_order_;
        std::reverse(fini_order.begin(), fini_order.end());
        const auto finalization = BuildGuestFinalizationPlan(lifecycle_modules_, fini_order);
        ExecuteGuestLifecycle(finalization, [this](const GuestLifecycleCall& call) {
            Invoke(call.address, {});
        });
        running_ = false;
    }

    bool Running() const noexcept { return running_; }

    [[nodiscard]] core::GpuStats Stats() const { return boundary_.Stats(); }
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const {
        return boundary_.RenderTargets();
    }
    [[nodiscard]] core::GpuCapabilities Capabilities() const {
        return boundary_.Capabilities();
    }
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const {
        return boundary_.Trace(filter, limit);
    }

private:
    void ThrowIfChildFailed() const {
        std::exception_ptr failure;
        {
            std::scoped_lock lock(child_failure_mutex_);
            failure = child_failure_;
        }
        if (!failure) return;
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            throw NativeActivityRunError(
                "NativeActivity guest child failed: " + std::string(error.what()));
        } catch (...) {
            throw NativeActivityRunError(
                "NativeActivity guest child failed with a non-standard exception");
        }
    }

    bool HandleBoundary(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        try {
            return boundary_.Handle(cpu, stopped);
        } catch (...) {
            if (cpu.GetState().ThreadId() != kRootThreadId) {
                {
                    std::scoped_lock lock(child_failure_mutex_);
                    if (!child_failure_) child_failure_ = std::current_exception();
                }
                static_cast<void>(futex_table_.WakeAll());
            }
            throw;
        }
    }

    void Progress(const std::string_view stage) const {
        if (progress_) progress_(stage);
    }

    void InitializeActivityMemory(const std::uint32_t api) {
        address_space_.Map({kActivityAddress, address_space_.PageSize()},
                           memory::PageProtection::read | memory::PageProtection::write);
        Write32(memory_bus_, kActivityAddress, kCallbacksAddress.Value());
        Write32(memory_bus_, kActivityAddress.Add(16), kInternalPathAddress.Value());
        Write32(memory_bus_, kActivityAddress.Add(20), kExternalPathAddress.Value());
        Write32(memory_bus_, kActivityAddress.Add(24), api);
        Write32(memory_bus_, kActivityAddress.Add(32), 0x6e030000U);
        Write32(memory_bus_, kActivityAddress.Add(36), kObbPathAddress.Value());
        WriteString(address_space_, kInternalPathAddress, "/data/data/org.ogplay.minimal/files");
        WriteString(address_space_, kExternalPathAddress, "/sdcard/Android/data/org.ogplay.minimal/files");
        WriteString(address_space_, kObbPathAddress, "/sdcard/Android/obb/org.ogplay.minimal");
    }

    void BindSyscalls() {
        BindAndroidTimeSyscalls(dispatcher_, clock_, address_space_);
        BindAndroidMemorySyscalls(dispatcher_, address_space_);
        BindAndroidThreadSyscalls(dispatcher_, futex_table_, memory_bus_);
        BindAndroidSignalSyscalls(dispatcher_, address_space_);
        BindAndroidProcessSyscalls(dispatcher_, address_space_,
            [this](const GuestVmaAnnotation& annotation) {
                std::scoped_lock lock(vma_mutex_); vma_annotations_.push_back(annotation);
            });
        BindAndroidFileSyscalls(dispatcher_, vfs_, address_space_);
        BindAndroidThreadLifecycleSyscalls(dispatcher_, lifecycle_);
        BindAndroidArmPrivateSyscalls(dispatcher_, address_space_,
            [this](const std::uint64_t thread_id, const memory::GuestAddress pointer) {
                lifecycle_.SetThreadPointer(thread_id, pointer); return true;
            });
        dispatcher_.SetObserver([this](const A32SyscallFrame& frame, const std::int32_t result) {
            if (frame.number == 42 && result == 0) {
                pipe_write_descriptor_ = static_cast<std::int32_t>(memory_bus_.Read32(
                    memory::GuestAddress{frame.arguments[0] + 4U}, frame.thread_id));
                Progress("command-pipe-created");
            } else if (frame.number == 4 && result > 0 &&
                       static_cast<std::int32_t>(frame.arguments[0]) == pipe_write_descriptor_) {
                Progress("command-pipe-write");
                boundary_.NotifyFileWrite();
            }
        });
    }

    void RequireCallback(const std::size_t index) const {
        if (callbacks_[index] == 0) {
            throw NativeActivityRunError("NativeActivity required callback was not installed");
        }
    }

    void InvokeCallback(const std::size_t index,
                        const std::initializer_list<std::uint32_t> arguments) {
        if (callbacks_[index] == 0) return;
        Invoke(memory::GuestAddress{callbacks_[index]}, arguments);
    }

    void Invoke(const memory::GuestAddress function,
                const std::initializer_list<std::uint32_t> arguments) {
        if (function.Value() == 0) throw NativeActivityRunError("cannot invoke a null guest function");
        auto state = root_cpu_->GetState();
        state.SetState((function.Value() & 1U) != 0 ? cpu::ExecutionState::thumb
                                                    : cpu::ExecutionState::a32);
        state.SetRegister(cpu::CoreRegister::pc, function.Value() & ~1U);
        state.SetRegister(cpu::CoreRegister::lr,
                          process_memory_.return_trap.Value());
        state.SetRegister(cpu::CoreRegister::sp,
                          process_memory_.stack_top.Value());
        for (std::size_t index = 0; index < 4; ++index) {
            state.SetRegister(static_cast<cpu::CoreRegister>(index), 0);
        }
        std::size_t index{};
        for (const auto argument : arguments) {
            if (index >= 4) throw NativeActivityRunError("guest invocation has too many arguments");
            state.SetRegister(static_cast<cpu::CoreRegister>(index++), argument);
        }
        root_cpu_->SetState(state);

        std::uint64_t consumed{};
        while (consumed < maximum_ticks_) {
            ThrowIfChildFailed();
            const auto stopped = root_cpu_->Run(maximum_ticks_ - consumed);
            consumed += stopped.ticks_consumed;
            if (stopped.reason == cpu::RunStopReason::supervisor_call &&
                stopped.immediate == 1 &&
                stopped.pc == process_memory_.return_trap) {
                return;
            }
            ThrowIfChildFailed();
            if (!ConsumeAndroidArmSupervisorCall(
                    *root_cpu_, stopped, dispatcher_,
                    [this](cpu::Cpu& cpu, const cpu::RunResult& trap) {
                        return HandleBoundary(cpu, trap);
                    })) {
                throw NativeActivityRunError(
                    "NativeActivity guest stopped outside a handled boundary:\n" +
                    DescribeA32GuestStop(stopped, root_cpu_->GetState(),
                                         address_space_));
            }
            ThrowIfChildFailed();
            auto updated = root_cpu_->GetState();
            updated.SetThreadPointer(lifecycle_.State(kRootThreadId).thread_pointer);
            root_cpu_->SetState(updated);
        }
        throw NativeActivityRunError("NativeActivity guest invocation exhausted its tick budget");
    }

    memory::AddressSpace address_space_;
    memory::CheckedMemoryBus memory_bus_{address_space_};
    AndroidBoundaryHle boundary_;
    core::CapabilityLedger ledger_;
    A32SyscallDispatcher dispatcher_;
    hal::RealtimeClock clock_;
    cpu::FutexTable futex_table_;
    VirtualFileSystem vfs_;
    GuestThreadLifecycle lifecycle_;
    std::vector<GuestVmaAnnotation> vma_annotations_;
    std::mutex vma_mutex_;
    mutable std::mutex child_failure_mutex_;
    std::exception_ptr child_failure_;
    std::shared_ptr<cpu::DynarmicExecutionContext> execution_context_ =
        std::make_shared<cpu::DynarmicExecutionContext>(64);
    cpu::GuestThreadGroup threads_;
    std::unique_ptr<GuestCloneThreadRuntime> clone_runtime_;
    std::unique_ptr<cpu::DynarmicCpu> root_cpu_;
    loader::Elf32LoadedNamespace loaded_;
    Api19GuestProcessMemory process_memory_;
    std::vector<GuestLifecycleModule> lifecycle_modules_;
    std::vector<std::size_t> guest_load_order_;
    std::array<std::uint32_t, 16> callbacks_{};
    std::uint64_t maximum_ticks_{};
    std::function<void(std::string_view)> progress_;
    std::int32_t pipe_write_descriptor_{-1};
    bool running_{};
};

std::unique_ptr<NativeActivitySession> NativeActivitySession::Start(
    const NativeActivityRunRequest& request) {
    return std::unique_ptr<NativeActivitySession>(
        new NativeActivitySession(std::make_unique<Impl>(request)));
}
NativeActivitySession::NativeActivitySession(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
NativeActivitySession::~NativeActivitySession() = default;
void NativeActivitySession::PushInput(const AndroidBoundaryInput& input) { impl_->PushInput(input); }
std::optional<AndroidBoundaryFrame> NativeActivitySession::TakeLatestFrame() {
    return impl_->TakeLatestFrame();
}
void NativeActivitySession::RecycleFrame(AndroidBoundaryFrame&& frame) {
    impl_->RecycleFrame(std::move(frame));
}
void NativeActivitySession::Stop() { impl_->Stop(); }
bool NativeActivitySession::Running() const noexcept { return impl_->Running(); }
core::GpuStats NativeActivitySession::Stats() const { return impl_->Stats(); }
std::vector<core::GpuRenderTarget> NativeActivitySession::RenderTargets() const {
    return impl_->RenderTargets();
}
core::GpuCapabilities NativeActivitySession::Capabilities() const {
    return impl_->Capabilities();
}
std::vector<core::GpuTraceEntry> NativeActivitySession::Trace(
    const std::string_view filter, const std::size_t limit) const {
    return impl_->Trace(filter, limit);
}

}  // namespace ogplay::runtime
