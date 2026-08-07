#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"
#include "ogplay/runtime/execution/guest_lifecycle.h"
#include "ogplay/runtime/integration/api19_guest_process.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_bindings.h"
#include "ogplay/runtime/integration/jni_guest_dispatch.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/syscall/arm_kernel_helpers.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kRootThreadId = 1;

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

}  // namespace

class AndroidGuestCallSession::Impl final {
public:
    explicit Impl(const AndroidGuestCallSessionRequest& request)
        : boundary_(address_space_, request.backend, request.width,
                    request.height, request.supersample_factor),
          guest_jni_(address_space_),
          dispatcher_(CreateAndroidArmSyscallDispatcher(ledger_)),
          jni_dispatcher_(ledger_), invocations_(classes_),
          java_vm_(environment_),
          threads_([this] {
              return std::make_unique<cpu::DynarmicCpu>(
                  memory_bus_, execution_context_);
          }),
          filesystem_(request.filesystem),
          maximum_ticks_(request.maximum_ticks_per_call),
          progress_(request.progress) {
#if !OGPLAY_HAS_DYNARMIC
        static_cast<void>(request);
        throw AndroidGuestCallSessionError(
            "Android guest call session requires Dynarmic");
#else
        if (request.api != 19 || request.root_module.empty() ||
            request.modules.empty() || request.width == 0 ||
            request.height == 0 || request.maximum_ticks_per_call == 0 ||
            filesystem_ == nullptr) {
            throw AndroidGuestCallSessionError(
                "Android guest call session request is incomplete");
        }
        if (request.direct_assets.has_value()) {
            direct_assets_ = std::make_unique<FrameworkDirectAssetHle>(
                invocations_, environment_, strings_, arrays_, *filesystem_);
            direct_assets_->Install(*request.direct_assets);
        }
        const auto& profile = SelectBionicProfile(request.api);
        Progress("mapping-boundaries");
        MapArmKernelHelpers(address_space_);
        boundary_.MapThunks();
        loaded_ = loader::LoadElf32ModuleNamespace(
            request.root_module, request.modules, address_space_,
            [&profile, this](
                const std::string_view root,
                const std::span<const loader::Elf32LinkModule> guest) {
                return BuildBionicLinkNamespace(
                    profile, root, guest, boundary_.Symbols());
            });
        Progress("modules-loaded");

        process_memory_ = InitializeApi19GuestProcess(
            address_space_, memory_bus_, loaded_.link_namespace,
            {kRootThreadId, "ogplay-profile"});
        lifecycle_.Register(kRootThreadId, process_memory_.thread_pointer);
        BindSyscalls();
        BindJniGuestCoreSlots(
            jni_dispatcher_, environment_, classes_, invocations_, strings_,
            java_vm_, address_space_);
        jni_dispatcher_.Seal();
        const auto attached = java_vm_.AttachCurrentThread(
            kRootThreadId, kJniVersion1_6);
        if (attached.status != JniStatus::ok) {
            throw AndroidGuestCallSessionError(
                "Android guest root JNI thread attachment failed");
        }
        Progress("process-memory-ready");

        clone_runtime_ = std::make_unique<GuestCloneThreadRuntime>(
            threads_, dispatcher_, lifecycle_, address_space_, memory_bus_,
            futex_table_, 2, 100000,
            [this](cpu::Cpu& cpu, const cpu::RunResult& stopped) {
                return HandleBoundary(cpu, stopped);
            });
        root_cpu_ = std::make_unique<cpu::DynarmicCpu>(
            memory_bus_, execution_context_);
        cpu::A32State root_state;
        root_state.SetThreadId(kRootThreadId);
        root_state.SetThreadPointer(process_memory_.thread_pointer);
        root_cpu_->SetState(root_state);

        lifecycle_modules_ = LifecycleModules(loaded_, request.modules);
        for (const auto module_index : loaded_.link_namespace.load_order) {
            if (module_index < loaded_.modules.size()) {
                guest_load_order_.push_back(module_index);
            }
        }
        const auto initialization = BuildGuestInitializationPlan(
            lifecycle_modules_, guest_load_order_);
        ExecuteGuestLifecycle(
            initialization, [this](const GuestLifecycleCall& call) {
                static_cast<void>(Invoke(
                    {call.address, {}, {}}));
            });
        running_ = true;
        Progress("guest-initializers-complete");
#endif
    }

    ~Impl() {
        if (!running_) return;
        try {
            Stop();
        } catch (const std::exception&) {
        }
    }

    A32GuestCallResult Invoke(const A32GuestCallFrame& frame) {
        if (!root_cpu_) {
            throw AndroidGuestCallSessionError(
                "Android guest call session has no root CPU");
        }
        return InvokeA32GuestCall(
            *root_cpu_, dispatcher_, lifecycle_, address_space_, frame,
            process_memory_.stack_top, process_memory_.return_trap,
            maximum_ticks_,
            [this](cpu::Cpu& cpu, const cpu::RunResult& stopped) {
                return HandleBoundary(cpu, stopped);
            });
    }

    bool HandleBoundary(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (jni_dispatcher_.Handle(cpu, stopped)) return true;
        return boundary_.Handle(cpu, stopped);
    }

    void Stop() {
        if (!running_) return;
        for (const auto& child : lifecycle_.States()) {
            if (child.thread_id == kRootThreadId) continue;
            const auto joined = clone_runtime_->Join(child.thread_id);
            if (joined.run.reason != GuestThreadRunStop::guest_exit) {
                throw AndroidGuestCallSessionError(
                    "Android guest child did not exit cleanly");
            }
        }
        auto fini_order = guest_load_order_;
        std::reverse(fini_order.begin(), fini_order.end());
        const auto finalization = BuildGuestFinalizationPlan(
            lifecycle_modules_, fini_order);
        ExecuteGuestLifecycle(
            finalization, [this](const GuestLifecycleCall& call) {
                static_cast<void>(Invoke(
                    {call.address, {}, {}}));
            });
        const auto detached = java_vm_.DetachCurrentThread(kRootThreadId);
        if (detached != JniStatus::ok) {
            throw AndroidGuestCallSessionError(
                "Android guest root JNI thread detachment failed");
        }
        running_ = false;
    }

    void BindSyscalls() {
        BindAndroidTimeSyscalls(dispatcher_, clock_, address_space_);
        BindAndroidMemorySyscalls(dispatcher_, address_space_);
        BindAndroidThreadSyscalls(
            dispatcher_, futex_table_, memory_bus_);
        BindAndroidSignalSyscalls(dispatcher_, address_space_);
        BindAndroidProcessSyscalls(
            dispatcher_, address_space_,
            [this](const GuestVmaAnnotation& annotation) {
                std::scoped_lock lock(vma_mutex_);
                vma_annotations_.push_back(annotation);
            });
        BindAndroidFileSyscalls(
            dispatcher_, *filesystem_, address_space_);
        BindAndroidThreadLifecycleSyscalls(dispatcher_, lifecycle_);
        BindAndroidArmPrivateSyscalls(
            dispatcher_,
            [this](const std::uint64_t thread_id,
                   const memory::GuestAddress pointer) {
                lifecycle_.SetThreadPointer(thread_id, pointer);
                return true;
            });
        dispatcher_.SetObserver(
            [this](const A32SyscallFrame& frame,
                   const std::int32_t result) {
                if (frame.number == 4 && result > 0) {
                    boundary_.NotifyFileWrite();
                }
            });
    }

    void Progress(const std::string_view stage) const {
        if (progress_) progress_(stage);
    }

    memory::GuestAddress GuestEnvironment() const noexcept {
        return guest_jni_.Environment();
    }
    memory::GuestAddress GuestJavaVm() const noexcept {
        return guest_jni_.JavaVm();
    }
    JniEnvironment& Environment() noexcept { return environment_; }
    JniClassRegistry& Classes() noexcept { return classes_; }
    void OpenManagedSurface() { boundary_.OpenManagedSurface(); }
    void PresentManagedSurface() { boundary_.PresentManagedSurface(); }
    void CloseManagedSurface() { boundary_.CloseManagedSurface(); }
    void PushInput(const AndroidBoundaryInput& input) {
        if (!running_) {
            throw AndroidGuestCallSessionError(
                "Android guest call session is stopped");
        }
        boundary_.PushInput(input);
    }
    std::optional<AndroidBoundaryFrame> TakeLatestFrame() {
        return boundary_.TakeLatestFrame();
    }
    bool Running() const noexcept { return running_; }
    core::GpuStats Stats() const { return boundary_.Stats(); }
    std::vector<core::GpuRenderTarget> RenderTargets() const {
        return boundary_.RenderTargets();
    }
    core::GpuCapabilities Capabilities() const {
        return boundary_.Capabilities();
    }
    std::vector<core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const {
        return boundary_.Trace(filter, limit);
    }

private:
    memory::AddressSpace address_space_;
    memory::CheckedMemoryBus memory_bus_{address_space_};
    AndroidBoundaryHle boundary_;
    GuestJniAbi guest_jni_;
    core::CapabilityLedger ledger_;
    A32SyscallDispatcher dispatcher_;
    JniGuestCallDispatcher jni_dispatcher_;
    JniEnvironment environment_;
    JniClassRegistry classes_;
    JniInvocationEngine invocations_;
    JniStringStore strings_;
    JniPrimitiveArrayStore arrays_;
    JniJavaVm java_vm_;
    hal::RealtimeClock clock_;
    cpu::FutexTable futex_table_;
    GuestThreadLifecycle lifecycle_;
    std::vector<GuestVmaAnnotation> vma_annotations_;
    std::mutex vma_mutex_;
    std::shared_ptr<cpu::DynarmicExecutionContext> execution_context_ =
        std::make_shared<cpu::DynarmicExecutionContext>(64);
    cpu::GuestThreadGroup threads_;
    VirtualFileSystem* filesystem_{};
    std::unique_ptr<FrameworkDirectAssetHle> direct_assets_;
    std::unique_ptr<GuestCloneThreadRuntime> clone_runtime_;
    std::unique_ptr<cpu::DynarmicCpu> root_cpu_;
    loader::Elf32LoadedNamespace loaded_;
    Api19GuestProcessMemory process_memory_;
    std::vector<GuestLifecycleModule> lifecycle_modules_;
    std::vector<std::size_t> guest_load_order_;
    std::uint64_t maximum_ticks_{};
    std::function<void(std::string_view)> progress_;
    bool running_{};
};

std::unique_ptr<AndroidGuestCallSession> AndroidGuestCallSession::Start(
    const AndroidGuestCallSessionRequest& request) {
    try {
        return std::unique_ptr<AndroidGuestCallSession>(
            new AndroidGuestCallSession(std::make_unique<Impl>(request)));
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(
            "Android guest call session startup failed: " +
            std::string(error.what()));
    }
}

AndroidGuestCallSession::AndroidGuestCallSession(
    std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
AndroidGuestCallSession::~AndroidGuestCallSession() = default;
A32GuestCallResult AndroidGuestCallSession::Invoke(
    const A32GuestCallFrame& frame) {
    try {
        return impl_->Invoke(frame);
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(
            "Android guest invocation failed: " +
            std::string(error.what()));
    }
}
memory::GuestAddress AndroidGuestCallSession::GuestEnvironment() const noexcept {
    return impl_->GuestEnvironment();
}
memory::GuestAddress AndroidGuestCallSession::GuestJavaVm() const noexcept {
    return impl_->GuestJavaVm();
}
JniEnvironment& AndroidGuestCallSession::Environment() noexcept {
    return impl_->Environment();
}
JniClassRegistry& AndroidGuestCallSession::Classes() noexcept {
    return impl_->Classes();
}
void AndroidGuestCallSession::OpenManagedSurface() {
    impl_->OpenManagedSurface();
}
void AndroidGuestCallSession::PresentManagedSurface() {
    impl_->PresentManagedSurface();
}
void AndroidGuestCallSession::CloseManagedSurface() {
    impl_->CloseManagedSurface();
}
void AndroidGuestCallSession::PushInput(const AndroidBoundaryInput& input) {
    impl_->PushInput(input);
}
std::optional<AndroidBoundaryFrame>
AndroidGuestCallSession::TakeLatestFrame() {
    return impl_->TakeLatestFrame();
}
void AndroidGuestCallSession::Stop() { impl_->Stop(); }
bool AndroidGuestCallSession::Running() const noexcept {
    return impl_->Running();
}
core::GpuStats AndroidGuestCallSession::Stats() const {
    return impl_->Stats();
}
std::vector<core::GpuRenderTarget>
AndroidGuestCallSession::RenderTargets() const {
    return impl_->RenderTargets();
}
core::GpuCapabilities AndroidGuestCallSession::Capabilities() const {
    return impl_->Capabilities();
}
std::vector<core::GpuTraceEntry> AndroidGuestCallSession::Trace(
    const std::string_view filter, const std::size_t limit) const {
    return impl_->Trace(filter, limit);
}

}  // namespace ogplay::runtime
