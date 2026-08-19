#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
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
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_library_lifecycle.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"
#include "ogplay/runtime/framework/framework_lifecycle.h"
#include "ogplay/runtime/framework/framework_locale.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_native_export.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
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
        result.push_back({index, inputs[index].load_bias,
                          loaded.modules[index].lifecycle});
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
    void Initialize() { std::scoped_lock lock(mutex_); state_.Initialize(); }
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
    [[nodiscard]] bool IsLoaded(
        const audio::JavaSoundPoolKind kind, const std::int32_t resource) const {
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
              const float volume, const bool looping = false) {
        std::scoped_lock lock(mutex_);
        if (!state_.IsLoaded(kind, resource)) {
            static_cast<void>(LoadLocked(kind, resource));
        }
        if (!state_.Play(kind, resource, instance, volume, looping)) return;
        if (mixer_ != nullptr &&
            !mixer_->Play(kind, resource, instance, volume, looping)) {
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
        "audio.play_sound_big_looping",
        [controls](const JniInvocation& invocation) {
            const auto resource = std::get<JniInt>(invocation.arguments[0]);
            const auto volume = std::get<JniFloat>(invocation.arguments[1]);
            const auto looping =
                std::get<JniBoolean>(invocation.arguments[2]) != 0U;
            controls->Play(audio::JavaSoundPoolKind::big, resource, 0,
                           volume, looping);
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
    return exit_requested_.load(std::memory_order_acquire); }
std::uint64_t AndroidGuestProcessState::ExitRequestCount() const noexcept {
    return exit_request_count_.load(std::memory_order_relaxed); }

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

namespace {

struct AndroidGuestProcessStartup final {
    std::uint32_t api{};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
    gles::AngleBackend backend;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t maximum_ticks_per_call{};
    std::uint32_t supersample_factor{};
    VirtualFileSystem* filesystem{};
    std::function<void(std::string_view)> progress;
    std::optional<FrameworkDirectAssetImplementations> direct_assets;
    AndroidBoundaryOptions boundary_options;
    audio::JavaSoundPoolMixer::EncodedResourceLoader sound_resource_loader;
    A32GuestCallSliceObserver guest_call_slice_observer;
    AndroidGuestPlatformConfig platform;
    std::size_t application_module_count{};
};

[[nodiscard]] AndroidGuestProcessStartup RootlessStartup(
    const AndroidGuestProcessRequest& request) {
    return {request.api,
            "libc.so",
            request.system_modules,
            request.backend,
            request.width,
            request.height,
            request.maximum_ticks_per_call,
            request.supersample_factor,
            request.filesystem,
            request.progress,
            request.direct_assets,
            request.boundary_options,
            request.sound_resource_loader,
            request.guest_call_slice_observer,
            request.platform,
            0};
}

[[nodiscard]] AndroidGuestProcessStartup LegacyStartup(
    const AndroidGuestCallSessionRequest& request) {
    return {request.api,
            request.root_module,
            request.modules,
            request.backend,
            request.width,
            request.height,
            request.maximum_ticks_per_call,
            request.supersample_factor,
            request.filesystem,
            request.progress,
            request.direct_assets,
            request.boundary_options,
            request.sound_resource_loader,
            request.guest_call_slice_observer,
            request.platform,
            1};
}

}  // namespace

class AndroidGuestProcess::Impl final {
public:
    explicit Impl(const AndroidGuestProcessStartup& request)
        : boundary_(address_space_, request.backend, request.width,
                    request.height, request.supersample_factor,
                    request.boundary_options),
          guest_jni_(address_space_),
          dispatcher_(CreateAndroidArmSyscallDispatcher(ledger_)),
          jni_dispatcher_(ledger_), invocations_(classes_), fields_(classes_),
          objects_(classes_),
          sound_pool_mixer_(request.sound_resource_loader),
          java_vm_(environment_),
          threads_([this] {
              return std::make_unique<cpu::DynarmicCpu>(
                  memory_bus_, execution_context_);
          }),
          filesystem_(request.filesystem),
          root_module_(request.root_module),
          maximum_ticks_(request.maximum_ticks_per_call),
          progress_(request.progress),
          slice_observer_(request.guest_call_slice_observer),
          api_(request.api),
          application_module_count_(request.application_module_count) {
#if !OGPLAY_HAS_DYNARMIC
        static_cast<void>(request);
        throw AndroidGuestProcessError(
            "Android guest call session requires Dynarmic");
#else
        if (request.api != 19 || request.root_module.empty() ||
            request.modules.empty() || request.width == 0 ||
            request.height == 0 || request.maximum_ticks_per_call == 0 ||
            filesystem_ == nullptr) {
            throw AndroidGuestProcessError(
                "Android guest call session request is incomplete");
        }
        BindAndroidGuestJavaAudioHandlers(
            invocations_, sound_pool_,
            sound_pool_mixer_.Enabled() ? &sound_pool_mixer_ : nullptr);
        BindAndroidGuestJavaMediaHandlers(
            invocations_, environment_, strings_, arrays_, movie_state_,
            media_state_, request.sound_resource_loader);
        BindAndroidGuestJavaDisplayHandlers(invocations_, screen_policy_);
        BindAndroidGuestJavaProcessHandlers(invocations_, process_state_);
        BindAndroidGuestJavaLocaleHandlers(invocations_, {});
        BindAndroidGuestJavaPlatformHandlers(
            invocations_, environment_, strings_, arrays_, platform_state_,
            request.platform);
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
        JniGuestBindingContext jni_bindings{
            environment_, classes_, invocations_, fields_, strings_, arrays_,
            java_vm_, objects_, address_space_, &natives_};
        BindJniGuestSlots(jni_dispatcher_, jni_bindings);
        jni_dispatcher_.Seal();
        const auto attached = java_vm_.AttachCurrentThread(
            kRootThreadId, kJniVersion1_6);
        if (attached.status != JniStatus::ok) {
            throw AndroidGuestProcessError(
                "Android guest root JNI thread attachment failed");
        }
        static_cast<void>(InstallAndroidGuestFrameworkPlatform(
            classes_, invocations_, environment_, strings_, fields_, objects_,
            kRootThreadId, request.platform));
        static_cast<void>(InstallAndroidGuestJavaMediaClasses(classes_));
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
            throw AndroidGuestProcessError(
                "Android guest call session has no root CPU");
        }
        std::scoped_lock call_lock(guest_call_mutex_);
        std::unique_ptr<cpu::DynarmicCpu> nested_cpu;
        cpu::Cpu* target = root_cpu_.get();
        auto stack_top = process_memory_.stack_top;
        if (active_guest_call_cpu_ != nullptr) {
            const auto caller = active_guest_call_cpu_->GetState();
            const auto caller_sp =
                caller.Register(cpu::CoreRegister::sp) & ~UINT32_C(7);
            if (caller_sp == 0U) {
                throw AndroidGuestProcessError(
                    "nested Android guest call has no aligned caller stack");
            }
            stack_top = memory::GuestAddress{caller_sp};
            nested_cpu = std::make_unique<cpu::DynarmicCpu>(
                memory_bus_, execution_context_);
            cpu::A32State nested_state;
            nested_state.SetThreadId(caller.ThreadId());
            nested_state.SetThreadPointer(caller.ThreadPointer());
            nested_cpu->SetState(nested_state);
            target = nested_cpu.get();
        }
        auto* const previous = active_guest_call_cpu_;
        active_guest_call_cpu_ = target;
        try {
            auto result = InvokeA32GuestCall(
                *target, dispatcher_, lifecycle_, address_space_, frame,
                stack_top, process_memory_.return_trap, maximum_ticks_,
                [this](cpu::Cpu& cpu, const cpu::RunResult& stopped) {
                    return HandleBoundary(cpu, stopped);
                }, slice_observer_);
            active_guest_call_cpu_ = previous;
            return result;
        } catch (...) {
            active_guest_call_cpu_ = previous;
            throw;
        }
    }

    A32GuestCallResult InvokeRegisteredNative(
        const JniObjectIdentity java_class, const std::string_view name,
        const std::string_view descriptor, const A32GuestCallFrame& frame) {
        return Invoke(ResolveJniRegisteredNativeCall(
            natives_, java_class, name, descriptor, frame));
    }

    bool HandleBoundary(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (jni_dispatcher_.Handle(cpu, stopped)) return true;
        return boundary_.Handle(cpu, stopped);
    }

    void Stop() {
        if (!running_) return;
        auto children = lifecycle_.States();
        std::erase_if(children, [](const GuestThreadRuntimeState& state) {
            return state.thread_id == kRootThreadId;
        });
        for (const auto& child : children) {
            if (child.status == GuestThreadStatus::running) {
                lifecycle_.RequestExit(child.thread_id, 0);
            }
        }
        static_cast<void>(InterruptBlockingWaits());
        std::exception_ptr first_child_failure;
        for (const auto& child : children) {
            try {
                const auto joined = clone_runtime_->Join(child.thread_id);
                if (joined.run.reason != GuestThreadRunStop::guest_exit) {
                    throw AndroidGuestProcessError(
                        "Android guest child did not exit cleanly");
                }
            } catch (...) {
                if (!first_child_failure) {
                    first_child_failure = std::current_exception();
                }
            }
        }
        if (first_child_failure) {
            running_ = false;
            static_cast<void>(environment_.ShutdownMonitors());
            std::rethrow_exception(first_child_failure);
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
        static_cast<void>(environment_.ShutdownMonitors());
        if (detached != JniStatus::ok) {
            throw AndroidGuestProcessError(
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
        BindAndroidFileMetadataSyscalls(
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
        return guest_jni_.Environment(); }
    memory::GuestAddress GuestJavaVm() const noexcept {
        return guest_jni_.JavaVm(); }
    JniEnvironment& Environment() noexcept { return environment_; }
    JniClassRegistry& Classes() noexcept { return classes_; }
    JniInvocationEngine& Invocations() noexcept { return invocations_; }
    JniGuestObjectRegistry& Objects() noexcept { return objects_; }
    JniStringStore& Strings() noexcept { return strings_; }
    JniPrimitiveArrayStore& Arrays() noexcept { return arrays_; }
    audio::JavaSoundPoolState& SoundPoolState() noexcept {
        return sound_pool_; }
    audio::JavaSoundPoolMixer& SoundPoolMixer() noexcept {
        return sound_pool_mixer_; }
    VirtualFileSystem* Filesystem() noexcept { return filesystem_; }
    void InitializeJniLibrary() {
        if (!running_) {
            throw AndroidGuestProcessError(
                "guest JNI library cannot initialize in a stopped session");
        }
        if (jni_library_initialized_) {
            throw AndroidGuestProcessError(
                "guest JNI library is already initialized");
        }
        const auto on_load = BuildJniGuestLibraryOnLoad(
            loaded_.link_namespace, root_module_, guest_jni_.JavaVm());
        if (on_load.has_value()) {
            Progress("guest-jni-onload");
            const auto result = Invoke(on_load->call);
            ValidateJniGuestLibraryOnLoadResult(result.return_value);
        }
        jni_library_initialized_ = true;
        Progress("guest-jni-library-ready");
    }
    void OpenManagedSurface() { boundary_.OpenManagedSurface(); }
    void BindManagedSurfaceOnCallingThread() {
        boundary_.BindManagedSurfaceOnCallingThread();
    }
    void ReleaseManagedSurfaceFromCallingThread() {
        boundary_.ReleaseManagedSurfaceFromCallingThread();
    }
    bool ManagedSurfaceIsOpen() const noexcept {
        return boundary_.ManagedSurfaceIsOpen();
    }
    std::string ManagedGlString(const std::uint32_t parameter) {
        return boundary_.ManagedGlString(parameter);
    }
    void PresentManagedSurface() { boundary_.PresentManagedSurface(); }
    void CloseManagedSurface() { boundary_.CloseManagedSurface(); }
    void PushInput(const AndroidBoundaryInput& input) {
        if (!running_) {
            throw AndroidGuestProcessError(
                "Android guest call session is stopped");
        }
        boundary_.PushInput(input);
    }
    std::optional<AndroidBoundaryFrame> TakeLatestFrame() {
        return boundary_.TakeLatestFrame(); }
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) {
        boundary_.PublishSoftwareFrame(std::move(rgba8));
    }
    void RecycleFrame(AndroidBoundaryFrame&& frame) {
        boundary_.RecycleFrame(std::move(frame));
    }
    std::size_t RenderStereoAudio(const std::span<std::int16_t> output,
                                  const std::uint32_t sample_rate) {
        return sound_pool_mixer_.RenderStereoPcm16(output, sample_rate);
    }
    std::size_t InterruptBlockingWaits() {
        return futex_table_.InterruptAll() +
               environment_.InterruptMonitorWaiters();
    }
    bool Running() const noexcept { return running_; }
    bool ExitRequested() const noexcept { return process_state_.ExitRequested(); }
    std::size_t ApplicationModuleCount() const noexcept {
        return application_module_count_;
    }
    std::size_t LoadedGuestModuleCount() const noexcept {
        return loaded_.modules.size();
    }
    std::size_t AttachedJniThreadCount() const {
        return java_vm_.AttachedThreadCount();
    }
    bool HasLoadedModule(const std::string_view name) const {
        std::scoped_lock lock(dynamic_link_mutex_);
        return HasLoadedModuleLocked(name);
    }
    AndroidGuestApplicationLoad LoadApplicationModules(
        const std::string_view root_module,
        const std::span<const AndroidGuestApplicationModuleSource> sources) {
        if (!running_ || root_module.empty()) {
            throw AndroidGuestProcessError(
                "dynamic application load request is incomplete");
        }

        loader::Elf32LoadedModuleExtension extension;
        std::vector<std::size_t> initialization_order;
        std::vector<std::string> initialization_names;
        std::size_t root_module_index{};
        {
            std::scoped_lock lock(dynamic_link_mutex_);
            std::vector<AndroidGuestApplicationModuleSource> missing_sources;
            missing_sources.reserve(sources.size());
            for (const auto& source : sources) {
                if (!HasLoadedModuleLocked(source.name)) {
                    missing_sources.push_back(source);
                }
            }
            auto inputs = AssignDynamicLoadBiases(missing_sources);
            const auto old_namespace_size =
                loaded_.link_namespace.modules.size();
            const auto& profile = SelectBionicProfile(api_);
            extension = loader::ExtendElf32ModuleNamespace(
                loaded_.link_namespace, root_module, inputs, address_space_,
                [&profile, this](
                    const loader::Elf32LinkNamespace& link_namespace,
                    const std::string_view root,
                    const std::span<const loader::Elf32LinkModule> modules) {
                    return ExtendBionicLinkNamespace(
                        profile, link_namespace, root, modules,
                        boundary_.Symbols());
                });
            root_module_index = extension.link_extension.scope.root_module;
            for (std::size_t index = 0; index < inputs.size(); ++index) {
                const auto namespace_index = old_namespace_size + index;
                lifecycle_modules_.push_back(
                    {namespace_index, inputs[index].load_bias,
                     extension.modules[index].lifecycle});
            }
            for (const auto namespace_index :
                 extension.link_extension.scope.load_order) {
                if (namespace_index < old_namespace_size ||
                    namespace_index >= old_namespace_size + inputs.size()) {
                    continue;
                }
                initialization_order.push_back(namespace_index);
                initialization_names.push_back(
                    extension.link_extension.link_namespace
                        .modules[namespace_index]
                        .name);
            }
            loaded_.link_namespace =
                extension.link_extension.link_namespace;
            dynamic_modules_.insert(
                dynamic_modules_.end(),
                std::make_move_iterator(extension.modules.begin()),
                std::make_move_iterator(extension.modules.end()));
            application_module_count_ += inputs.size();
        }

        for (const auto namespace_index : initialization_order) {
            const std::array one{namespace_index};
            const auto plan = BuildGuestInitializationPlan(
                lifecycle_modules_, one);
            try {
                ExecuteGuestLifecycle(
                    plan, [this](const GuestLifecycleCall& call) {
                        static_cast<void>(Invoke({call.address, {}, {}}));
                    });
            } catch (const std::exception& error) {
                throw AndroidGuestProcessError(
                    "guest constructor failed: " +
                    std::string(error.what()));
            }
            std::scoped_lock lock(dynamic_link_mutex_);
            guest_load_order_.push_back(namespace_index);
        }
        return {root_module_index, std::move(initialization_names)};
    }
    std::optional<std::uint32_t> InitializeExplicitJniLibrary(
        const std::string_view root_module) {
        std::optional<JniGuestLibraryOnLoad> on_load;
        {
            std::scoped_lock lock(dynamic_link_mutex_);
            on_load = BuildJniGuestLibraryOnLoad(
                loaded_.link_namespace, root_module, guest_jni_.JavaVm());
        }
        if (!on_load.has_value()) return std::nullopt;
        const auto result = Invoke(on_load->call).return_value;
        ValidateJniGuestLibraryOnLoadResult(result);
        return result;
    }
    std::optional<AndroidGuestMovieRequest> LatestMovieRequest() const {
        return movie_state_.Latest();
    }
    core::GpuStats Stats() const { return boundary_.Stats(); }
    std::vector<core::GpuRenderTarget> RenderTargets() const {
        return boundary_.RenderTargets(); }
    core::GpuCapabilities Capabilities() const { return boundary_.Capabilities(); }
    std::vector<core::GpuTraceEntry> Trace(
        const std::string_view filter, const std::size_t limit) const {
        return boundary_.Trace(filter, limit);
    }

private:
    [[nodiscard]] bool HasLoadedModuleLocked(
        const std::string_view name) const {
        return std::ranges::any_of(
            loaded_.link_namespace.modules,
            [name](const loader::Elf32LinkModule& module) {
                return module.name == name ||
                       (module.dynamic.soname.has_value() &&
                        *module.dynamic.soname == name);
            });
    }

    [[nodiscard]] std::vector<loader::Elf32ModuleInput>
    AssignDynamicLoadBiases(
        const std::span<const AndroidGuestApplicationModuleSource> sources) {
        constexpr std::uint64_t kFirstDynamicBias = 0x30000000U;
        constexpr std::uint64_t kLoadAlignment = 0x10000U;
        const auto align_up = [](const std::uint64_t value) {
            return (value + kLoadAlignment - 1U) & ~(kLoadAlignment - 1U);
        };
        const auto snapshot = address_space_.CaptureSnapshot();
        std::vector<memory::GuestRange> occupied;
        occupied.reserve(snapshot.mappings.size() + sources.size() * 2U);
        for (const auto& mapping : snapshot.mappings) {
            occupied.push_back(mapping.range);
        }
        std::vector<loader::Elf32ModuleInput> result;
        result.reserve(sources.size());
        std::set<std::string, std::less<>> names;
        std::uint64_t cursor = kFirstDynamicBias;
        for (const auto& source : sources) {
            if (source.name.empty() || source.image.empty() ||
                !names.insert(source.name).second) {
                throw AndroidGuestProcessError(
                    "dynamic application module source is invalid or duplicate: " +
                    source.name);
            }
            const auto image = loader::ParseElf32Arm(source.image);
            if (image.type != loader::Elf32ImageType::shared_object) {
                throw AndroidGuestProcessError(
                    "dynamic application module is not ET_DYN: " +
                    source.name);
            }
            bool assigned{};
            while (!assigned) {
                if (cursor >= kBionicHleThunkBegin ||
                    cursor > std::numeric_limits<std::uint32_t>::max()) {
                    throw AndroidGuestProcessError(
                        "dynamic application module address space is exhausted");
                }
                const auto bias = memory::GuestAddress{
                    static_cast<std::uint32_t>(cursor)};
                const auto plan = loader::BuildElf32LoadPlan(
                    image, bias, address_space_.PageSize());
                std::uint64_t next_cursor = cursor;
                bool collision{};
                for (const auto& region : plan.regions) {
                    next_cursor = std::max(
                        next_cursor, region.range.EndExclusive());
                    for (const auto& used : occupied) {
                        if (!region.range.Overlaps(used)) continue;
                        collision = true;
                        next_cursor = std::max(
                            next_cursor, used.EndExclusive());
                    }
                }
                if (collision) {
                    cursor = align_up(next_cursor + kLoadAlignment);
                    continue;
                }
                result.push_back({source.name, source.image, bias});
                for (const auto& region : plan.regions) {
                    occupied.push_back(region.range);
                }
                cursor = align_up(next_cursor + kLoadAlignment);
                assigned = true;
            }
        }
        return result;
    }

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
    JniFieldStore fields_;
    JniGuestObjectRegistry objects_;
    audio::JavaSoundPoolState sound_pool_;
    audio::JavaSoundPoolMixer sound_pool_mixer_;
    FrameworkScreenPolicyState screen_policy_;
    AndroidGuestProcessState process_state_;
    AndroidGuestPlatformState platform_state_;
    JniStringStore strings_;
    AndroidGuestMovieState movie_state_;
    AndroidGuestLegacyMediaState media_state_;
    JniPrimitiveArrayStore arrays_;
    JniNativeRegistry natives_;
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
    // Serializes use of the root guest executor. Same-thread JNI reentry gets
    // an isolated CPU whose stack begins below the suspended caller SP, so a
    // nested JNI_OnLoad cannot overwrite the outer register/stack frame.
    std::recursive_mutex guest_call_mutex_;
    cpu::Cpu* active_guest_call_cpu_{};
    loader::Elf32LoadedNamespace loaded_;
    std::vector<loader::Elf32LoadedModule> dynamic_modules_;
    std::string root_module_;
    Api19GuestProcessMemory process_memory_;
    std::vector<GuestLifecycleModule> lifecycle_modules_;
    std::vector<std::size_t> guest_load_order_;
    std::uint64_t maximum_ticks_{};
    std::function<void(std::string_view)> progress_;
    A32GuestCallSliceObserver slice_observer_;
    std::uint32_t api_{19};
    std::size_t application_module_count_{};
    bool jni_library_initialized_{};
    bool running_{};
    mutable std::mutex dynamic_link_mutex_;

public:
    [[nodiscard]] std::optional<memory::GuestAddress> FindNativeExport(
        const std::string_view class_name,
        const std::string_view method_name,
        const std::string_view descriptor) const {
        const auto names =
            BuildJniNativeExportNames(class_name, method_name, descriptor);
        for (const auto* candidate : {&names.long_name, &names.short_name}) {
            for (const auto& module : loaded_.link_namespace.modules) {
                for (std::size_t index = 0;
                     index < module.symbols.symbols.size(); ++index) {
                    const auto& symbol = module.symbols.symbols[index];
                    if (symbol.name != *candidate || !symbol.IsExported()) {
                        continue;
                    }
                    return memory::GuestAddress(
                        module.load_bias.Value() + symbol.value.Value());
                }
            }
        }
        return std::nullopt;
    }
};

std::unique_ptr<AndroidGuestProcess> AndroidGuestProcess::Start(
    const AndroidGuestProcessRequest& request) {
    try {
        return std::unique_ptr<AndroidGuestProcess>(
            new AndroidGuestProcess(
                std::make_unique<Impl>(RootlessStartup(request))));
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "Android guest process startup failed: " +
            std::string(error.what()));
    }
}
std::unique_ptr<AndroidGuestProcess> AndroidGuestProcess::StartLegacy(
    const AndroidGuestCallSessionRequest& request) {
    return std::unique_ptr<AndroidGuestProcess>(
        new AndroidGuestProcess(
            std::make_unique<Impl>(LegacyStartup(request))));
}
AndroidGuestProcess::AndroidGuestProcess(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
AndroidGuestProcess::~AndroidGuestProcess() = default;
A32GuestCallResult AndroidGuestProcess::Invoke(
    const A32GuestCallFrame& frame) {
    try {
        return impl_->Invoke(frame);
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "Android guest invocation failed: " +
            std::string(error.what()));
    }
}
A32GuestCallResult AndroidGuestProcess::InvokeRegisteredNative(
    const JniObjectIdentity java_class, const std::string_view name,
    const std::string_view descriptor, const A32GuestCallFrame& frame) {
    try {
        return impl_->InvokeRegisteredNative(java_class, name, descriptor,
                                             frame);
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "registered JNI native invocation failed: " +
            std::string(error.what()));
    }
}
memory::GuestAddress AndroidGuestProcess::GuestEnvironment() const noexcept { return impl_->GuestEnvironment(); }
memory::GuestAddress AndroidGuestProcess::GuestJavaVm() const noexcept { return impl_->GuestJavaVm(); }
JniEnvironment& AndroidGuestProcess::Environment() noexcept { return impl_->Environment(); }
JniClassRegistry& AndroidGuestProcess::Classes() noexcept { return impl_->Classes(); }
JniInvocationEngine& AndroidGuestProcess::Invocations() noexcept {
    return impl_->Invocations();
}
JniGuestObjectRegistry& AndroidGuestProcess::Objects() noexcept {
    return impl_->Objects();
}
JniStringStore& AndroidGuestProcess::Strings() noexcept {
    return impl_->Strings();
}
JniPrimitiveArrayStore& AndroidGuestProcess::Arrays() noexcept {
    return impl_->Arrays();
}
audio::JavaSoundPoolState& AndroidGuestProcess::SoundPoolState() noexcept {
    return impl_->SoundPoolState();
}
audio::JavaSoundPoolMixer& AndroidGuestProcess::SoundPoolMixer() noexcept {
    return impl_->SoundPoolMixer();
}
VirtualFileSystem* AndroidGuestProcess::Filesystem() noexcept {
    return impl_->Filesystem();
}
std::optional<memory::GuestAddress> AndroidGuestProcess::FindNativeExport(
    const std::string_view class_name, const std::string_view method_name,
    const std::string_view descriptor) const {
    return impl_->FindNativeExport(class_name, method_name, descriptor);
}
void AndroidGuestProcess::InitializeJniLibrary() {
    try {
        impl_->InitializeJniLibrary();
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "guest JNI library initialization failed: " +
            std::string(error.what()));
    }
}
void AndroidGuestProcess::OpenManagedSurface() { impl_->OpenManagedSurface(); }
void AndroidGuestProcess::BindManagedSurfaceOnCallingThread() { impl_->BindManagedSurfaceOnCallingThread(); }
void AndroidGuestProcess::ReleaseManagedSurfaceFromCallingThread() { impl_->ReleaseManagedSurfaceFromCallingThread(); }
bool AndroidGuestProcess::ManagedSurfaceIsOpen() const noexcept { return impl_->ManagedSurfaceIsOpen(); }
std::string AndroidGuestProcess::ManagedGlString(const std::uint32_t parameter) { return impl_->ManagedGlString(parameter); }
void AndroidGuestProcess::PresentManagedSurface() { impl_->PresentManagedSurface(); }
void AndroidGuestProcess::CloseManagedSurface() { impl_->CloseManagedSurface(); }
void AndroidGuestProcess::PushInput(const AndroidBoundaryInput& input) { impl_->PushInput(input); }
std::optional<AndroidBoundaryFrame> AndroidGuestProcess::TakeLatestFrame() { return impl_->TakeLatestFrame(); }
void AndroidGuestProcess::PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) { impl_->PublishSoftwareFrame(std::move(rgba8)); }
void AndroidGuestProcess::RecycleFrame(AndroidBoundaryFrame&& frame) { impl_->RecycleFrame(std::move(frame)); }
std::size_t AndroidGuestProcess::RenderStereoAudio(const std::span<std::int16_t> output,
                                                   const std::uint32_t sample_rate) { return impl_->RenderStereoAudio(output, sample_rate); }
std::size_t AndroidGuestProcess::InterruptBlockingWaits() { return impl_->InterruptBlockingWaits(); }
void AndroidGuestProcess::Stop() { impl_->Stop(); }
bool AndroidGuestProcess::Running() const noexcept { return impl_->Running(); }
bool AndroidGuestProcess::ExitRequested() const noexcept { return impl_->ExitRequested(); }
std::size_t AndroidGuestProcess::ApplicationModuleCount() const noexcept { return impl_->ApplicationModuleCount(); }
std::size_t AndroidGuestProcess::LoadedGuestModuleCount() const noexcept { return impl_->LoadedGuestModuleCount(); }
std::size_t AndroidGuestProcess::AttachedJniThreadCount() const { return impl_->AttachedJniThreadCount(); }
bool AndroidGuestProcess::HasLoadedModule(const std::string_view name) const {
    return impl_->HasLoadedModule(name);
}
AndroidGuestApplicationLoad AndroidGuestProcess::LoadApplicationModules(
    const std::string_view root_module,
    const std::span<const AndroidGuestApplicationModuleSource> modules) {
    try {
        return impl_->LoadApplicationModules(root_module, modules);
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "dynamic application ELF load failed: " +
            std::string(error.what()));
    }
}
std::optional<std::uint32_t>
AndroidGuestProcess::InitializeExplicitJniLibrary(
    const std::string_view root_module) {
    try {
        return impl_->InitializeExplicitJniLibrary(root_module);
    } catch (const AndroidGuestProcessError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "explicit JNI library initialization failed: " +
            std::string(error.what()));
    }
}
std::optional<AndroidGuestMovieRequest> AndroidGuestProcess::LatestMovieRequest() const { return impl_->LatestMovieRequest(); }
core::GpuStats AndroidGuestProcess::Stats() const { return impl_->Stats(); }
std::vector<core::GpuRenderTarget> AndroidGuestProcess::RenderTargets() const { return impl_->RenderTargets(); }
core::GpuCapabilities AndroidGuestProcess::Capabilities() const { return impl_->Capabilities(); }
std::vector<core::GpuTraceEntry> AndroidGuestProcess::Trace(
    const std::string_view filter, const std::size_t limit) const { return impl_->Trace(filter, limit); }

std::unique_ptr<AndroidGuestCallSession> AndroidGuestCallSession::Start(
    const AndroidGuestCallSessionRequest& request) {
    if (request.api != 19 || request.root_module.empty() ||
        request.modules.empty() || request.width == 0 ||
        request.height == 0 || request.maximum_ticks_per_call == 0 ||
        request.filesystem == nullptr) {
        throw AndroidGuestCallSessionError(
            "Android guest call session request is incomplete");
    }
    try {
        return std::unique_ptr<AndroidGuestCallSession>(
            new AndroidGuestCallSession(AndroidGuestProcess::StartLegacy(request)));
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(
            "Android guest call session startup failed: " +
            std::string(error.what()));
    }
}

std::unique_ptr<AndroidGuestCallSession>
AndroidGuestCallSession::AdoptProcess(
    std::unique_ptr<AndroidGuestProcess> process) {
    if (!process) {
        throw AndroidGuestCallSessionError(
            "cannot adopt an empty Android guest process");
    }
    return std::unique_ptr<AndroidGuestCallSession>(
        new AndroidGuestCallSession(std::move(process)));
}
AndroidGuestCallSession::AndroidGuestCallSession(
    std::unique_ptr<AndroidGuestProcess> process) noexcept
    : process_(std::move(process)) {}
AndroidGuestCallSession::~AndroidGuestCallSession() = default;
A32GuestCallResult AndroidGuestCallSession::Invoke(
    const A32GuestCallFrame& frame) {
    try {
        return process_->Invoke(frame);
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
A32GuestCallResult AndroidGuestCallSession::InvokeRegisteredNative(
    const JniObjectIdentity java_class, const std::string_view name,
    const std::string_view descriptor, const A32GuestCallFrame& frame) {
    try {
        return process_->InvokeRegisteredNative(
            java_class, name, descriptor, frame);
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
memory::GuestAddress AndroidGuestCallSession::GuestEnvironment() const noexcept { return process_->GuestEnvironment(); }
memory::GuestAddress AndroidGuestCallSession::GuestJavaVm() const noexcept { return process_->GuestJavaVm(); }
JniEnvironment& AndroidGuestCallSession::Environment() noexcept { return process_->Environment(); }
JniClassRegistry& AndroidGuestCallSession::Classes() noexcept { return process_->Classes(); }
JniInvocationEngine& AndroidGuestCallSession::Invocations() noexcept { return process_->Invocations(); }
JniGuestObjectRegistry& AndroidGuestCallSession::Objects() noexcept { return process_->Objects(); }
JniStringStore& AndroidGuestCallSession::Strings() noexcept { return process_->Strings(); }
JniPrimitiveArrayStore& AndroidGuestCallSession::Arrays() noexcept { return process_->Arrays(); }
audio::JavaSoundPoolState& AndroidGuestCallSession::SoundPoolState() noexcept { return process_->SoundPoolState(); }
audio::JavaSoundPoolMixer& AndroidGuestCallSession::SoundPoolMixer() noexcept { return process_->SoundPoolMixer(); }
VirtualFileSystem* AndroidGuestCallSession::Filesystem() noexcept { return process_->Filesystem(); }
AndroidGuestProcess& AndroidGuestCallSession::Process() noexcept { return *process_; }
std::optional<memory::GuestAddress> AndroidGuestCallSession::FindNativeExport(std::string_view class_name, std::string_view method_name, std::string_view descriptor) const { return process_->FindNativeExport(class_name, method_name, descriptor); }
void AndroidGuestCallSession::InitializeJniLibrary() {
    try {
        process_->InitializeJniLibrary();
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
void AndroidGuestCallSession::OpenManagedSurface() { process_->OpenManagedSurface(); }
void AndroidGuestCallSession::BindManagedSurfaceOnCallingThread() { process_->BindManagedSurfaceOnCallingThread(); }
void AndroidGuestCallSession::ReleaseManagedSurfaceFromCallingThread() { process_->ReleaseManagedSurfaceFromCallingThread(); }
bool AndroidGuestCallSession::ManagedSurfaceIsOpen() const noexcept { return process_->ManagedSurfaceIsOpen(); }
std::string AndroidGuestCallSession::ManagedGlString(std::uint32_t parameter) { return process_->ManagedGlString(parameter); }
void AndroidGuestCallSession::PresentManagedSurface() { process_->PresentManagedSurface(); }
void AndroidGuestCallSession::CloseManagedSurface() { process_->CloseManagedSurface(); }
void AndroidGuestCallSession::PushInput(const AndroidBoundaryInput& input) {
    try {
        process_->PushInput(input);
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
std::optional<AndroidBoundaryFrame> AndroidGuestCallSession::TakeLatestFrame() { return process_->TakeLatestFrame(); }
void AndroidGuestCallSession::PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) { process_->PublishSoftwareFrame(std::move(rgba8)); }
void AndroidGuestCallSession::RecycleFrame(AndroidBoundaryFrame&& frame) { process_->RecycleFrame(std::move(frame)); }
std::size_t AndroidGuestCallSession::RenderStereoAudio(std::span<std::int16_t> output, std::uint32_t sample_rate) { return process_->RenderStereoAudio(output, sample_rate); }
std::size_t AndroidGuestCallSession::InterruptBlockingWaits() { return process_->InterruptBlockingWaits(); }
void AndroidGuestCallSession::Stop() {
    try {
        process_->Stop();
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
bool AndroidGuestCallSession::Running() const noexcept { return process_->Running(); }
bool AndroidGuestCallSession::ExitRequested() const noexcept { return process_->ExitRequested(); }
std::optional<AndroidGuestMovieRequest> AndroidGuestCallSession::LatestMovieRequest() const { return process_->LatestMovieRequest(); }
core::GpuStats AndroidGuestCallSession::Stats() const { return process_->Stats(); }
std::vector<core::GpuRenderTarget> AndroidGuestCallSession::RenderTargets() const { return process_->RenderTargets(); }
core::GpuCapabilities AndroidGuestCallSession::Capabilities() const { return process_->Capabilities(); }
std::vector<core::GpuTraceEntry> AndroidGuestCallSession::Trace(std::string_view filter, std::size_t limit) const { return process_->Trace(filter, limit); }

}  // namespace ogplay::runtime
