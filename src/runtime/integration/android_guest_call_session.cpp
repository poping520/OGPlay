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

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"
#include "ogplay/runtime/execution/guest_lifecycle.h"
#include "ogplay/runtime/integration/api19_guest_process.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_bindings.h"
#include "ogplay/runtime/integration/jni_guest_dispatch.h"
#include "ogplay/runtime/framework/framework_lifecycle.h"
#include "ogplay/runtime/framework/framework_locale.h"
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

class JavaSoundPoolControls final {
public:
    JavaSoundPoolControls(audio::JavaSoundPoolState& state,
                          audio::JavaSoundPoolMixer* mixer)
        : state_(state), mixer_(mixer) {}

    void Destroy() {
        std::scoped_lock lock(mutex_);
        state_.Destroy();
        if (mixer_ != nullptr) mixer_->Destroy();
    }
    void Initialize() {
        std::scoped_lock lock(mutex_);
        state_.Initialize();
    }
    void StopAllSounds() {
        std::scoped_lock lock(mutex_);
        static_cast<void>(state_.StopAllSounds());
        if (mixer_ != nullptr) mixer_->StopAllSounds();
    }
    void StopAll(const audio::JavaSoundPoolKind kind,
                 const std::int32_t except_resource) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(state_.StopAll(kind, except_resource));
        if (mixer_ != nullptr) mixer_->StopAll(kind, except_resource);
    }
    void PauseAll(const audio::JavaSoundPoolKind kind) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(state_.PauseAll(kind));
        if (mixer_ != nullptr) mixer_->PauseAll(kind);
    }
    void ResumeAll(const audio::JavaSoundPoolKind kind) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(state_.ResumeAll(kind));
        if (mixer_ != nullptr) mixer_->ResumeAll(kind);
    }
    [[nodiscard]] bool IsLoaded(const audio::JavaSoundPoolKind kind,
                                const std::int32_t resource) const {
        std::scoped_lock lock(mutex_);
        return state_.IsLoaded(kind, resource);
    }
    void Unload(const audio::JavaSoundPoolKind kind,
                const std::int32_t resource) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(state_.Unload(kind, resource));
        if (mixer_ != nullptr &&
            !state_.IsLoaded(audio::JavaSoundPoolKind::pool, resource) &&
            !state_.IsLoaded(audio::JavaSoundPoolKind::big, resource)) {
            mixer_->Unload(resource);
        }
    }
    void Load(const audio::JavaSoundPoolKind kind,
              const std::int32_t resource) {
        std::scoped_lock lock(mutex_);
        static_cast<void>(LoadLocked(kind, resource));
    }
    void Play(const audio::JavaSoundPoolKind kind,
              const std::int32_t resource, const std::int32_t instance,
              const float volume) {
        std::scoped_lock lock(mutex_);
        if (!state_.IsLoaded(kind, resource)) {
            static_cast<void>(LoadLocked(kind, resource));
        }
        if (!state_.Play(kind, resource, instance, volume)) return;
        if (mixer_ != nullptr &&
            !mixer_->Play(kind, resource, instance, volume)) {
            static_cast<void>(state_.Stop(kind, resource, instance));
        }
    }
    void Pause(const audio::JavaSoundPoolKind kind,
               const std::int32_t resource, const std::int32_t instance) {
        std::scoped_lock lock(mutex_);
        if (state_.Pause(kind, resource, instance) && mixer_ != nullptr) {
            mixer_->Pause(kind, resource, instance);
        }
    }
    void Resume(const audio::JavaSoundPoolKind kind,
                const std::int32_t resource, const std::int32_t instance) {
        std::scoped_lock lock(mutex_);
        if (state_.Resume(kind, resource, instance) && mixer_ != nullptr) {
            mixer_->Resume(kind, resource, instance);
        }
    }
    void Stop(const audio::JavaSoundPoolKind kind,
              const std::int32_t resource, const std::int32_t instance) {
        std::scoped_lock lock(mutex_);
        if (state_.Stop(kind, resource, instance) && mixer_ != nullptr) {
            mixer_->Stop(kind, resource, instance);
        }
    }
    void SetVolume(const audio::JavaSoundPoolKind kind,
                   const std::int32_t resource, const std::int32_t instance,
                   const float volume) {
        std::scoped_lock lock(mutex_);
        if (state_.SetVolume(kind, resource, instance, volume) &&
            mixer_ != nullptr) {
            mixer_->SetVolume(kind, resource, instance, volume);
        }
    }
    void SetPitch(const audio::JavaSoundPoolKind kind,
                  const std::int32_t resource, const std::int32_t instance,
                  const float pitch) {
        std::scoped_lock lock(mutex_);
        if (state_.SetPitch(kind, resource, instance, pitch) &&
            mixer_ != nullptr) {
            mixer_->SetPitch(kind, resource, instance, pitch);
        }
    }
    void Reset(const audio::JavaSoundPoolKind kind,
               const std::int32_t resource, const std::int32_t instance) {
        std::scoped_lock lock(mutex_);
        if (state_.Reset(kind, resource, instance) && mixer_ != nullptr) {
            mixer_->Reset(kind, resource, instance);
        }
    }

private:
    [[nodiscard]] bool LoadLocked(const audio::JavaSoundPoolKind kind,
                                  const std::int32_t resource) {
        if (!state_.RequestLoad(kind, resource)) return false;
        if (state_.IsLoaded(kind, resource)) return true;
        if (mixer_ == nullptr || !mixer_->Load(resource)) return false;
        return state_.MarkLoaded(kind, resource);
    }

    audio::JavaSoundPoolState& state_;
    audio::JavaSoundPoolMixer* mixer_{};
    mutable std::mutex mutex_;
};

}  // namespace

void BindAndroidGuestJavaAudioHandlers(
    JniInvocationEngine& invocations,
    audio::JavaSoundPoolState& sound_pool,
    audio::JavaSoundPoolMixer* mixer) {
    const auto controls =
        std::make_shared<JavaSoundPoolControls>(sound_pool, mixer);
    invocations.RegisterHandler(
        "audio.destroy_sound_pool",
        [controls](const JniInvocation&) {
            controls->Destroy();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.init_sound_pool_array",
        [controls](const JniInvocation&) {
            controls->Initialize();
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.stop_all_sounds",
        [controls](const JniInvocation&) {
            controls->StopAllSounds();
            return JniValue{std::monostate{}};
        });
    const auto stop_kind = [&invocations, controls](
                               const char* implementation,
                               const audio::JavaSoundPoolKind kind) {
        invocations.RegisterHandler(
            implementation,
            [controls, kind](const JniInvocation& invocation) {
                const auto except_resource =
                    std::get<JniInt>(invocation.arguments[0]);
                controls->StopAll(kind, except_resource);
                return JniValue{std::monostate{}};
            });
    };
    stop_kind("audio.stop_all_pool", audio::JavaSoundPoolKind::pool);
    stop_kind("audio.stop_all_big", audio::JavaSoundPoolKind::big);
    invocations.RegisterHandler(
        "audio.pause_all_big",
        [controls](const JniInvocation&) {
            controls->PauseAll(audio::JavaSoundPoolKind::big);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.resume_all_big",
        [controls](const JniInvocation&) {
            controls->ResumeAll(audio::JavaSoundPoolKind::big);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.is_sound_loaded",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto loaded = controls->IsLoaded(
                audio::JavaSoundPoolKind::pool, resource);
            return JniValue{JniInt{loaded ? 0 : -1}};
        });
    invocations.RegisterHandler(
        "audio.is_sound_loaded_big",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto loaded = controls->IsLoaded(
                audio::JavaSoundPoolKind::big, resource);
            return JniValue{JniInt{loaded ? 0 : -1}};
        });
    invocations.RegisterHandler(
        "audio.unload_sound",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            controls->Unload(audio::JavaSoundPoolKind::pool, resource);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.unload_sound_big",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            controls->Unload(audio::JavaSoundPoolKind::big, resource);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.play_sound",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto instance = std::get<JniInt>(invocation.arguments[1]);
            const auto volume = std::get<JniFloat>(invocation.arguments[2]);
            controls->Play(audio::JavaSoundPoolKind::pool, resource,
                           instance, volume);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.play_sound_big",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto volume = std::get<JniFloat>(invocation.arguments[1]);
            controls->Play(audio::JavaSoundPoolKind::big, resource, 0,
                           volume);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.load_sound",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            controls->Load(audio::JavaSoundPoolKind::pool, resource);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.load_sound_big",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            controls->Load(audio::JavaSoundPoolKind::big, resource);
            return JniValue{std::monostate{}};
        });
    const auto bind_voice_pair = [&invocations, controls](
                                     const char* pool_implementation,
                                     const char* big_implementation,
                                     const auto operation) {
        invocations.RegisterHandler(
            pool_implementation,
            [controls, operation](const JniInvocation& invocation) {
                const auto resource =
                    std::get<JniInt>(invocation.arguments[0]);
                const auto instance =
                    std::get<JniInt>(invocation.arguments[1]);
                (controls.get()->*operation)(
                    audio::JavaSoundPoolKind::pool, resource, instance);
                return JniValue{std::monostate{}};
            });
        invocations.RegisterHandler(
            big_implementation,
            [controls, operation](const JniInvocation& invocation) {
                const auto resource =
                    std::get<JniInt>(invocation.arguments[0]);
                (controls.get()->*operation)(
                    audio::JavaSoundPoolKind::big, resource, 0);
                return JniValue{std::monostate{}};
            });
    };
    bind_voice_pair("audio.pause_sound", "audio.pause_sound_big",
                    &JavaSoundPoolControls::Pause);
    bind_voice_pair("audio.resume_sound", "audio.resume_sound_big",
                    &JavaSoundPoolControls::Resume);
    bind_voice_pair("audio.stop_sound", "audio.stop_sound_big",
                    &JavaSoundPoolControls::Stop);
    const auto bind_volume_pair = [&invocations, controls](
                                      const char* pool_implementation,
                                      const char* big_implementation) {
        invocations.RegisterHandler(
            pool_implementation,
            [controls](const JniInvocation& invocation) {
                const auto resource =
                    std::get<JniInt>(invocation.arguments[0]);
                const auto instance =
                    std::get<JniInt>(invocation.arguments[1]);
                const auto volume =
                    std::get<JniFloat>(invocation.arguments[2]);
                controls->SetVolume(audio::JavaSoundPoolKind::pool, resource,
                                    instance, volume);
                return JniValue{std::monostate{}};
            });
        invocations.RegisterHandler(
            big_implementation,
            [controls](const JniInvocation& invocation) {
                const auto resource =
                    std::get<JniInt>(invocation.arguments[0]);
                const auto volume =
                    std::get<JniFloat>(invocation.arguments[1]);
                controls->SetVolume(audio::JavaSoundPoolKind::big, resource,
                                    0, volume);
                return JniValue{std::monostate{}};
            });
    };
    bind_volume_pair("audio.set_volume", "audio.set_volume_big");
    invocations.RegisterHandler(
        "audio.set_pitch",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto instance = std::get<JniInt>(invocation.arguments[1]);
            const auto pitch = std::get<JniFloat>(invocation.arguments[2]);
            controls->SetPitch(audio::JavaSoundPoolKind::pool, resource,
                               instance, pitch);
            return JniValue{std::monostate{}};
        });
    invocations.RegisterHandler(
        "audio.reset_sound",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            controls->Reset(audio::JavaSoundPoolKind::big, resource, 0);
            return JniValue{std::monostate{}};
        });
}

void BindAndroidGuestJavaDisplayHandlers(
    JniInvocationEngine& invocations,
    FrameworkScreenPolicyState& screen_policy) {
    invocations.RegisterHandler(
        "display.change_mode",
        [&screen_policy](const JniInvocation& invocation) {
            const auto mode = std::get<JniInt>(invocation.arguments[0]);
            screen_policy.SetSleepAllowed(mode == 1);
            return JniValue{std::monostate{}};
        });
}

void AndroidGuestProcessState::RequestExit() noexcept {
    exit_request_count_.fetch_add(1U, std::memory_order_relaxed);
    exit_requested_.store(true, std::memory_order_release);
}

bool AndroidGuestProcessState::ExitRequested() const noexcept {
    return exit_requested_.load(std::memory_order_acquire);
}

std::uint64_t AndroidGuestProcessState::ExitRequestCount() const noexcept {
    return exit_request_count_.load(std::memory_order_relaxed);
}

void BindAndroidGuestJavaProcessHandlers(
    JniInvocationEngine& invocations,
    AndroidGuestProcessState& process_state) {
    invocations.RegisterHandler(
        "process.exit",
        [&process_state](const JniInvocation&) {
            process_state.RequestExit();
            return JniValue{std::monostate{}};
        });
}

void BindAndroidGuestJavaLocaleHandlers(
    JniInvocationEngine& invocations,
    const FrameworkLocaleConfig& locale) {
    const auto language = LegacyPhoneLanguageIndex(locale);
    invocations.RegisterHandler(
        "locale.detect_phone_language",
        [language](const JniInvocation&) {
            return JniValue{JniInt{language}};
        });
}

class AndroidGuestCallSession::Impl final {
public:
    explicit Impl(const AndroidGuestCallSessionRequest& request)
        : boundary_(address_space_, request.backend, request.width,
                    request.height, request.supersample_factor,
                    request.boundary_options),
          guest_jni_(address_space_),
          dispatcher_(CreateAndroidArmSyscallDispatcher(ledger_)),
          jni_dispatcher_(ledger_), invocations_(classes_),
          sound_pool_mixer_(request.sound_resource_loader),
          java_vm_(environment_),
          threads_([this] {
              return std::make_unique<cpu::DynarmicCpu>(
                  memory_bus_, execution_context_);
          }),
          filesystem_(request.filesystem),
          maximum_ticks_(request.maximum_ticks_per_call),
          progress_(request.progress),
          slice_observer_(request.guest_call_slice_observer) {
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
        BindAndroidGuestJavaAudioHandlers(
            invocations_, sound_pool_,
            sound_pool_mixer_.Enabled() ? &sound_pool_mixer_ : nullptr);
        BindAndroidGuestJavaMovieHandlers(
            invocations_, environment_, strings_, movie_state_);
        BindAndroidGuestJavaDisplayHandlers(invocations_, screen_policy_);
        BindAndroidGuestJavaProcessHandlers(invocations_, process_state_);
        BindAndroidGuestJavaLocaleHandlers(invocations_, {});
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
            arrays_, java_vm_, address_space_);
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
            }, slice_observer_);
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
    void RecycleFrame(AndroidBoundaryFrame&& frame) {
        boundary_.RecycleFrame(std::move(frame));
    }
    std::size_t RenderStereoAudio(const std::span<std::int16_t> output,
                                  const std::uint32_t sample_rate) {
        return sound_pool_mixer_.RenderStereoPcm16(output, sample_rate);
    }
    bool Running() const noexcept { return running_; }
    bool ExitRequested() const noexcept {
        return process_state_.ExitRequested();
    }
    std::optional<AndroidGuestMovieRequest> LatestMovieRequest() const {
        return movie_state_.Latest();
    }
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
    audio::JavaSoundPoolState sound_pool_;
    audio::JavaSoundPoolMixer sound_pool_mixer_;
    FrameworkScreenPolicyState screen_policy_;
    AndroidGuestProcessState process_state_;
    JniStringStore strings_;
    AndroidGuestMovieState movie_state_;
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
    A32GuestCallSliceObserver slice_observer_;
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
void AndroidGuestCallSession::RecycleFrame(AndroidBoundaryFrame&& frame) {
    impl_->RecycleFrame(std::move(frame));
}
std::size_t AndroidGuestCallSession::RenderStereoAudio(
    const std::span<std::int16_t> output,
    const std::uint32_t sample_rate) {
    return impl_->RenderStereoAudio(output, sample_rate);
}
void AndroidGuestCallSession::Stop() { impl_->Stop(); }
bool AndroidGuestCallSession::Running() const noexcept {
    return impl_->Running();
}
bool AndroidGuestCallSession::ExitRequested() const noexcept {
    return impl_->ExitRequested();
}
std::optional<AndroidGuestMovieRequest>
AndroidGuestCallSession::LatestMovieRequest() const {
    return impl_->LatestMovieRequest();
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
