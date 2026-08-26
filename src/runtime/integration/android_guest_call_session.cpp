#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

#include "ogplay/audio/java_sound_pool.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/bionic/bionic_tls.h"
#include "ogplay/runtime/execution/guest_clone_thread_runtime.h"
#include "ogplay/runtime/execution/guest_lifecycle.h"
#include "ogplay/runtime/integration/api19_guest_process.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
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
constexpr std::uint32_t kDexVmThreadMaximum = 32;
constexpr std::uint64_t kOpenSlesCallbackThreadId = UINT64_C(0x4f50);
constexpr std::uint32_t kOpenSlesCallbackAllocationSlot =
    kDexVmThreadMaximum - 1U;
constexpr std::uint32_t kDexVmTlsBase = 0x6a100000U;
constexpr std::uint32_t kDexVmStackBase = 0x6c000000U;
constexpr std::uint32_t kDexVmStackSize = 1024U * 1024U;
constexpr std::uint32_t kOpenSlesCallbackTls = 0x71a00000U;
constexpr std::uint32_t kOpenSlesCallbackThreadInfo = 0x71a01000U;
constexpr std::uint32_t kOpenSlesCallbackStack = 0x71b00000U;
constexpr std::uint32_t kNioDirectArenaBegin = 0x74000000U;
constexpr std::uint32_t kNioDirectArenaEnd = 0x78000000U;

[[nodiscard]] std::string IndentDiagnostic(const std::string_view text,
                                           const std::string_view indent) {
    std::string result{indent};
    for (const char character : text) {
        result += character;
        if (character == '\n') result += indent;
    }
    return result;
}

void InstallApi19ProcFiles(VirtualFileSystem& filesystem) {
    constexpr std::string_view kMemInfo =
        "MemTotal:         524288 kB\n"
        "MemFree:          262144 kB\n"
        "Buffers:               0 kB\n"
        "Cached:           131072 kB\n"
        "SwapCached:            0 kB\n"
        "SwapTotal:             0 kB\n"
        "SwapFree:              0 kB\n";
    try {
        const auto existing = filesystem.Stat("/proc/meminfo");
        if (existing.writable) {
            throw AndroidGuestProcessError(
                "API 19 /proc/meminfo must be read only");
        }
        return;
    } catch (const VfsError&) {
    }
    filesystem.PutFile(
        "/proc/meminfo",
        std::as_bytes(std::span{kMemInfo.data(), kMemInfo.size()}), false);
}

thread_local std::unordered_map<const void*, cpu::Cpu*>
    active_guest_call_cpus;

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

bool ReadBoundaryGuestFile(void* const owner, const std::string_view path,
                           std::vector<std::byte>& output) {
    if (owner == nullptr) return false;
    auto& filesystem = *static_cast<VirtualFileSystem*>(owner);
    try {
        const auto info = filesystem.Stat(path);
        if (info.is_directory || info.size > 4U * 1024U * 1024U) return false;
        const auto descriptor = filesystem.Open(path, {.read = true});
        try {
            output.resize(static_cast<std::size_t>(info.size));
            std::size_t offset{};
            while (offset < output.size()) {
                const auto consumed = filesystem.Read(
                    descriptor, std::span(output).subspan(offset));
                if (consumed == 0U) break;
                offset += consumed;
            }
            output.resize(offset);
            filesystem.Close(descriptor);
            return true;
        } catch (...) {
            filesystem.Close(descriptor);
            throw;
        }
    } catch (...) {
        output.clear();
        return false;
    }
}

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
    auto boundary_options = request.boundary_options;
    boundary_options.guest_file_owner = request.filesystem;
    boundary_options.read_guest_file = &ReadBoundaryGuestFile;
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
            boundary_options,
            request.sound_resource_loader,
            request.guest_call_slice_observer,
            request.platform,
            0};
}

[[nodiscard]] AndroidGuestProcessStartup LegacyStartup(
    const AndroidGuestCallSessionRequest& request) {
    auto boundary_options = request.boundary_options;
    boundary_options.guest_file_owner = request.filesystem;
    boundary_options.read_guest_file = &ReadBoundaryGuestFile;
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
            boundary_options,
            request.sound_resource_loader,
            request.guest_call_slice_observer,
            request.platform,
            1};
}

}  // namespace

class AndroidGuestProcess::Impl final {
public:
    struct DexVmThreadContext final {
        DexVmThreadContext(const std::uint64_t id,
                           const memory::GuestRange info,
                           const memory::GuestRange stack_mapping)
            : thread_id(id), thread_info(info), stack(stack_mapping) {}
        std::uint64_t thread_id{};
        std::optional<BionicTlsBlock> tls;
        memory::GuestRange thread_info;
        memory::GuestRange stack;
        memory::GuestAddress stack_top;
        std::unique_ptr<cpu::DynarmicCpu> cpu;
        std::recursive_mutex call_mutex;
        bool jni_attached{};
    };

    explicit Impl(const AndroidGuestProcessStartup& request)
        : boundary_(address_space_, request.backend, request.width,
                    request.height, request.supersample_factor,
                    BindProcessCallbacks(request.boundary_options, this)),
          guest_jni_(address_space_),
          dispatcher_(CreateAndroidArmSyscallDispatcher(ledger_)),
          jni_dispatcher_(ledger_), invocations_(classes_), fields_(classes_),
          objects_(classes_),
          sound_pool_mixer_(request.sound_resource_loader),
          java_vm_(environment_),
          threads_([this] {
              auto result = std::make_unique<cpu::DynarmicCpu>(
                  memory_bus_, execution_context_);
              ConfigureFastHostCalls(*result);
              return result;
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
        InstallApi19ProcFiles(*filesystem_);
        BindAndroidGuestJavaAudioHandlers(
            invocations_, sound_pool_,
            sound_pool_mixer_.Enabled() ? &sound_pool_mixer_ : nullptr);
        media_state_.SetPcmPlayback(&boundary_.PcmPlayback());
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
        ReserveOpenSlesCallbackMemory();
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
        nio_.SetDirectMemoryAccess({
            [this](const std::uint32_t size) {
                if (size == 0U) return memory::GuestAddress{};
                const auto page = address_space_.PageSize();
                const auto mapped = (static_cast<std::uint64_t>(size) + page - 1U) & ~(page - 1U);
                std::scoped_lock lock(nio_direct_mutex_);
                const auto reusable = std::ranges::find_if(
                    nio_direct_free_, [mapped](const memory::GuestRange& range) {
                        return range.Size() >= mapped;
                    });
                if (reusable != nio_direct_free_.end()) {
                    const auto address = reusable->Start();
                    const auto remaining = reusable->Size() - mapped;
                    nio_direct_free_.erase(reusable);
                    if (remaining != 0U) {
                        nio_direct_free_.emplace_back(address.Add(mapped), remaining);
                    }
                    address_space_.Map({address, mapped}, memory::PageProtection::read |
                                                         memory::PageProtection::write);
                    return address;
                }
                if (nio_direct_cursor_ + mapped > kNioDirectArenaEnd)
                    throw std::bad_alloc{};
                const auto address = memory::GuestAddress(nio_direct_cursor_);
                address_space_.Map({address, mapped}, memory::PageProtection::read |
                                                     memory::PageProtection::write);
                nio_direct_cursor_ += static_cast<std::uint32_t>(mapped);
                return address;
            },
            [this](const memory::GuestAddress address, const std::uint32_t size) {
                if (size == 0U) return;
                const auto page = address_space_.PageSize();
                const auto mapped = (static_cast<std::uint64_t>(size) + page - 1U) & ~(page - 1U);
                std::scoped_lock lock(nio_direct_mutex_);
                address_space_.Unmap({address, mapped});
                nio_direct_free_.emplace_back(address, mapped);
                std::ranges::sort(nio_direct_free_, {},
                                  [](const memory::GuestRange& range) {
                                      return range.Start().Value();
                                  });
                std::vector<memory::GuestRange> merged;
                for (const auto& range : nio_direct_free_) {
                    if (!merged.empty() && merged.back().EndExclusive() ==
                                               range.Start().Value()) {
                        const auto start = merged.back().Start();
                        const auto combined = merged.back().Size() + range.Size();
                        merged.pop_back();
                        merged.emplace_back(start, combined);
                    } else {
                        merged.push_back(range);
                    }
                }
                nio_direct_free_ = std::move(merged);
            },
            [this](const memory::GuestAddress address, const std::uint32_t size) {
                if (size == 0U) return address.IsNull();
                try {
                    const memory::GuestRange range(address, size);
                    address_space_.Validate(range, memory::AccessType::read);
                    address_space_.Validate(range, memory::AccessType::write);
                    return true;
                } catch (const std::exception&) { return false; }
            },
            [this](const memory::GuestAddress address, const std::span<std::byte> out) {
                address_space_.Read(address, out);
            },
            [this](const memory::GuestAddress address, const std::span<const std::byte> in) {
                address_space_.Write(address, in);
            }});
        BindSyscalls();
        JniGuestBindingContext jni_bindings{
            environment_, classes_, invocations_, fields_, strings_, arrays_,
            java_vm_, objects_, address_space_, &natives_, &nio_};
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
        ConfigureFastHostCalls(*root_cpu_);
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
        StartOpenSlesCallbackThread();
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
        if (std::this_thread::get_id() != open_sles_callback_thread_.get_id()) {
            RethrowOpenSlesCallbackFailure();
        }
        if (!root_cpu_) {
            throw AndroidGuestProcessError(
                "Android guest call session has no root CPU");
        }
        std::unique_ptr<cpu::DynarmicCpu> nested_cpu;
        cpu::Cpu* target{};
        memory::GuestAddress stack_top;
        std::shared_ptr<DexVmThreadContext> dexvm_context;
        std::unique_lock<std::recursive_mutex> call_lock;
        const auto active = active_guest_call_cpus.find(this);
        if (active != active_guest_call_cpus.end()) {
            const auto caller = active->second->GetState();
            const auto caller_sp =
                caller.Register(cpu::CoreRegister::sp) & ~UINT32_C(7);
            if (caller_sp == 0U) {
                throw AndroidGuestProcessError(
                    "nested Android guest call has no aligned caller stack");
            }
            stack_top = memory::GuestAddress{caller_sp};
            nested_cpu = std::make_unique<cpu::DynarmicCpu>(
                memory_bus_, execution_context_);
            ConfigureFastHostCalls(*nested_cpu);
            cpu::A32State nested_state;
            nested_state.SetThreadId(caller.ThreadId());
            nested_state.SetThreadPointer(caller.ThreadPointer());
            nested_cpu->SetState(nested_state);
            target = nested_cpu.get();
        } else if (frame.thread_id == kRootThreadId) {
            call_lock = std::unique_lock<std::recursive_mutex>(
                guest_call_mutex_);
            target = root_cpu_.get();
            stack_top = process_memory_.stack_top;
        } else {
            {
                const std::scoped_lock lock(dexvm_threads_mutex_);
                const auto found = dexvm_threads_.find(frame.thread_id);
                if (found == dexvm_threads_.end()) {
                    throw AndroidGuestProcessError(
                        "Android guest call thread context is not prepared");
                }
                dexvm_context = found->second;
            }
            call_lock = std::unique_lock<std::recursive_mutex>(
                dexvm_context->call_mutex);
            target = dexvm_context->cpu.get();
            stack_top = dexvm_context->stack_top;
        }
        auto* const previous = active == active_guest_call_cpus.end()
                                   ? nullptr
                                   : active->second;
        active_guest_call_cpus[this] = target;
        try {
            auto result = InvokeA32GuestCall(
                *target, dispatcher_, lifecycle_, address_space_, frame,
                stack_top, process_memory_.return_trap, maximum_ticks_,
                [this](cpu::Cpu& cpu, const cpu::RunResult& stopped) {
                    return HandleBoundary(cpu, stopped);
                }, slice_observer_);
            if (previous != nullptr) active_guest_call_cpus[this] = previous;
            else active_guest_call_cpus.erase(this);
            return result;
        } catch (...) {
            if (previous != nullptr) active_guest_call_cpus[this] = previous;
            else active_guest_call_cpus.erase(this);
            throw;
        }
    }

    void PrepareDexVmThread(const std::uint64_t thread_id,
                            const std::uint32_t allocation_slot,
                            const bool attach_jni = true) {
        constexpr std::uint64_t kBionicPthreadMutexMaximumTid = 0xffffU;
        if (thread_id == 0U || thread_id == kRootThreadId ||
            thread_id > kBionicPthreadMutexMaximumTid ||
            allocation_slot >= kDexVmThreadMaximum) {
            throw AndroidGuestProcessError(
                "DexVM guest thread context request is outside its pool");
        }
        if (allocation_slot == kOpenSlesCallbackAllocationSlot &&
            thread_id != kOpenSlesCallbackThreadId) {
            throw AndroidGuestProcessError(
                "DexVM guest thread requested the reserved OpenSL callback slot");
        }
        const std::scoped_lock contexts_lock(dexvm_threads_mutex_);
        if (dexvm_threads_.contains(thread_id)) return;

        const auto page_size = address_space_.PageSize();
        const auto open_sles_callback =
            thread_id == kOpenSlesCallbackThreadId;
        const memory::GuestAddress tls_address{
            open_sles_callback
                ? kOpenSlesCallbackTls
                : kDexVmTlsBase + allocation_slot *
                      static_cast<std::uint32_t>(2U * page_size)};
        const memory::GuestAddress thread_info_address{
            open_sles_callback
                ? kOpenSlesCallbackThreadInfo
                : tls_address.Value() + static_cast<std::uint32_t>(page_size)};
        const memory::GuestAddress stack_address{
            open_sles_callback
                ? kOpenSlesCallbackStack
                : kDexVmStackBase + allocation_slot * kDexVmStackSize};
        auto context = std::make_shared<DexVmThreadContext>(
            thread_id, memory::GuestRange{thread_info_address, page_size},
            memory::GuestRange{stack_address, kDexVmStackSize});
        bool info_mapped{};
        bool stack_mapped{};
        bool lifecycle_registered{};
        bool jni_attached{};
        try {
            const auto rw = memory::PageProtection::read |
                            memory::PageProtection::write;
            address_space_.Map(context->thread_info, rw);
            info_mapped = true;
            context->tls = CreateBionicTlsBlock(
                address_space_, tls_address, thread_info_address,
                kApi19GuestPreinitAddress);
            address_space_.Map(context->stack, rw);
            stack_mapped = true;
            context->stack_top = stack_address.Add(kDexVmStackSize - 64U);

            const auto tid32 = static_cast<std::uint32_t>(thread_id);
            memory_bus_.Write32(thread_info_address.Add(12),
                                stack_address.Value(), thread_id);
            memory_bus_.Write32(thread_info_address.Add(16),
                                kDexVmStackSize, thread_id);
            memory_bus_.Write32(thread_info_address.Add(20),
                                static_cast<std::uint32_t>(page_size), thread_id);
            memory_bus_.Write32(thread_info_address.Add(32), tid32, thread_id);
            memory_bus_.Write32(thread_info_address.Add(60),
                                tls_address.Value(), thread_id);
            lifecycle_.Register(thread_id, context->tls->thread_pointer);
            lifecycle_registered = true;
            if (attach_jni) {
                const auto attached = java_vm_.AttachCurrentThread(
                    thread_id, kJniVersion1_6);
                if (attached.status != JniStatus::ok) {
                    throw AndroidGuestProcessError(
                        "DexVM guest thread JNI attachment failed");
                }
                jni_attached = true;
                context->jni_attached = true;
            }
            context->cpu = std::make_unique<cpu::DynarmicCpu>(
                memory_bus_, execution_context_);
            ConfigureFastHostCalls(*context->cpu);
            cpu::A32State state;
            state.SetThreadId(thread_id);
            state.SetThreadPointer(context->tls->thread_pointer);
            context->cpu->SetState(state);
            dexvm_threads_.emplace(thread_id, std::move(context));
        } catch (...) {
            if (jni_attached) {
                static_cast<void>(java_vm_.DetachCurrentThread(thread_id));
            }
            if (lifecycle_registered) {
                lifecycle_.RequestExit(thread_id, 0);
                static_cast<void>(lifecycle_.CompleteExit(thread_id));
                static_cast<void>(lifecycle_.Reap(thread_id));
            }
            if (stack_mapped) address_space_.Unmap(context->stack);
            if (context->tls.has_value()) {
                DestroyBionicTlsBlock(address_space_, *context->tls);
            }
            if (info_mapped) address_space_.Unmap(context->thread_info);
            throw;
        }
    }

    void ReleaseDexVmThread(const std::uint64_t thread_id) noexcept {
        std::shared_ptr<DexVmThreadContext> context;
        {
            const std::scoped_lock lock(dexvm_threads_mutex_);
            const auto found = dexvm_threads_.find(thread_id);
            if (found == dexvm_threads_.end()) return;
            context = std::move(found->second);
            dexvm_threads_.erase(found);
        }
        try {
            const std::scoped_lock call_lock(context->call_mutex);
            if (context->jni_attached) {
                static_cast<void>(java_vm_.DetachCurrentThread(thread_id));
            }
            lifecycle_.RequestExit(thread_id, 0);
            static_cast<void>(lifecycle_.CompleteExit(thread_id));
            static_cast<void>(lifecycle_.Reap(thread_id));
            address_space_.Unmap(context->stack);
            DestroyBionicTlsBlock(address_space_, *context->tls);
            address_space_.Unmap(context->thread_info);
        } catch (const std::exception&) {
        }
    }

    [[nodiscard]] std::optional<A32GuestCallResult>
    TryInvokeRegisteredNative(
        const JniObjectIdentity java_class, const std::string_view name,
        const std::string_view descriptor, const A32GuestCallFrame& frame) {
        const auto target = natives_.Resolve(
            java_class, std::string(name), std::string(descriptor));
        if (!target.has_value()) return std::nullopt;
        auto resolved = frame;
        resolved.target = *target;
        return Invoke(resolved);
    }

    bool HandleBoundary(cpu::Cpu& cpu, const cpu::RunResult& stopped) {
        if (jni_dispatcher_.Handle(cpu, stopped)) return true;
        return boundary_.Handle(cpu, stopped);
    }

    void ConfigureFastHostCalls(cpu::Cpu& cpu) {
        if (!slice_observer_) {
            cpu.SetHostCallHook({&DispatchFastHostCall, this});
        }
    }

    static cpu::HostCallResult DispatchFastHostCall(
        void* userdata, const std::uint32_t svc,
        cpu::A32HostCallContext& call) noexcept {
        if (userdata == nullptr) return cpu::HostCallResult::unhandled;
        auto& self = *static_cast<Impl*>(userdata);
        if (svc == 3U) return self.jni_dispatcher_.TryFastCall(call);
        const auto boundary = self.boundary_.FastHostCallHook();
        return boundary.invoke(boundary.userdata, svc, call);
    }

    static AndroidBoundaryOptions BindProcessCallbacks(
        AndroidBoundaryOptions options, Impl* owner) {
        options.dynamic_link = {
            owner,
            +[](void* userdata, const std::string_view name,
                const std::uint32_t flags, const std::uint64_t thread_id) {
                return static_cast<Impl*>(userdata)->DynamicOpen(
                    name, flags, thread_id);
            },
            +[](void* userdata, const std::uint32_t handle,
                const std::string_view name, const std::uint64_t thread_id) {
                return static_cast<Impl*>(userdata)->DynamicSymbol(
                    handle, name, thread_id);
            },
            +[](void* userdata, const std::uint32_t handle,
                const std::uint64_t thread_id) {
                return static_cast<Impl*>(userdata)->DynamicClose(
                    handle, thread_id);
            }};
        options.open_sles_callbacks = {
            owner, +[](void* userdata, const OpenSlesGuestCallback& callback) {
                static_cast<Impl*>(userdata)->EnqueueOpenSlesCallback(callback);
            }};
        return options;
    }

    [[nodiscard]] static std::string DynamicLibraryName(
        const std::string_view path) {
        const auto separator = path.find_last_of("/\\");
        const auto name = separator == std::string_view::npos
                              ? path
                              : path.substr(separator + 1U);
        if (name.empty() || name == "." || name == "..") {
            throw loader::LinkError("dlopen requires a library name");
        }
        return std::string(name);
    }

    // Legacy engine runtimes address the host GL through their own wrapper
    // library name (libhgl.so) and dlsym every GL entry point through that
    // handle. The wrapper ships no ELF here, so it opens the sealed HLE
    // GLES2 boundary instead and keeps such engines on the GL surface.
    [[nodiscard]] static std::string CanonicalDynamicLibrary(
        std::string name) {
        if (name == "libhgl.so") return "libGLESv2.so";
        return name;
    }

    std::uint32_t DynamicOpen(const std::string_view path,
                              const std::uint32_t flags,
                              const std::uint64_t thread_id) {
        static_cast<void>(thread_id);
        constexpr std::uint32_t kSupportedFlags = 0x3U;
        if ((flags & ~kSupportedFlags) != 0U) {
            throw loader::LinkError("dlopen flags are not supported");
        }
        // dlopen(nullptr) is the process-wide handle in bionic, not an open
        // of the root module.
        if (path.empty()) return kRtldDefault;
        const auto library =
            CanonicalDynamicLibrary(DynamicLibraryName(path));
        std::scoped_lock lock(dynamic_link_mutex_);
        const auto open = std::ranges::find_if(
            dynamic_link_handles_, [&](const DynamicLinkHandle& state) {
                return state.open && state.library == library;
            });
        if (open != dynamic_link_handles_.end()) {
            if (open->references ==
                (std::numeric_limits<std::uint32_t>::max)()) {
                throw loader::LinkError("dlopen reference count overflow");
            }
            ++open->references;
            return open->handle;
        }

        DynamicLinkHandle state;
        state.handle = next_dynamic_link_handle_++;
        if (state.handle == 0U || state.handle == kRtldDefault ||
            next_dynamic_link_handle_ == 0U) {
            throw loader::LinkError("dlopen handle space is exhausted");
        }
        state.library = library;
        state.references = 1U;
        state.open = true;
        state.boundary = !boundary_.Symbols().Exports(library).empty();
        if (!state.boundary) {
            state.scope = loader::ExtendElf32LinkNamespace(
                              loaded_.link_namespace, library, {})
                              .scope;
        }
        dynamic_link_handles_.push_back(std::move(state));
        return dynamic_link_handles_.back().handle;
    }

    std::uint32_t DynamicSymbol(const std::uint32_t handle,
                                const std::string_view name,
                                const std::uint64_t thread_id) {
        static_cast<void>(thread_id);
        if (name.empty()) throw loader::LinkError("dlsym requires a symbol name");
        std::scoped_lock lock(dynamic_link_mutex_);
        if (handle == kRtldDefault) {
            // Bionic resolves the default pseudo-handle by walking every
            // loaded library, so sealed HLE modules are searched before the
            // ELF namespace fallback runs.
            const auto hle = boundary_.Symbols().LookupAny(name);
            if (hle.has_value()) return hle->Value();
            return loader::LookupElf32Symbol(loaded_.link_namespace, name)
                .address.Value();
        }
        const auto state = std::ranges::find_if(
            dynamic_link_handles_, [handle](const DynamicLinkHandle& item) {
                return item.open && item.handle == handle;
            });
        if (state == dynamic_link_handles_.end()) {
            throw loader::LinkError("dlsym handle is invalid or closed");
        }
        if (state->boundary) {
            const auto address = boundary_.Symbols().Lookup(state->library, name);
            if (!address.has_value()) {
                throw loader::LinkError("dlsym could not find " +
                                        std::string(name) + " in " +
                                        state->library);
            }
            return address->Value();
        }
        return loader::LookupElf32Symbol(
                   loaded_.link_namespace, *state->scope, name)
            .address.Value();
    }

    std::int32_t DynamicClose(const std::uint32_t handle,
                              const std::uint64_t thread_id) {
        static_cast<void>(thread_id);
        // The process-wide handle is not a reference-counted library.
        if (handle == kRtldDefault) return 0;
        std::scoped_lock lock(dynamic_link_mutex_);
        const auto state = std::ranges::find_if(
            dynamic_link_handles_, [handle](const DynamicLinkHandle& item) {
                return item.open && item.handle == handle;
            });
        if (state == dynamic_link_handles_.end()) {
            throw loader::LinkError("dlclose handle is invalid or closed");
        }
        if (--state->references == 0U) state->open = false;
        return 0;
    }

    void EnqueueOpenSlesCallback(const OpenSlesGuestCallback& callback) {
        if (callback.function == 0U || callback.argument_count == 0U ||
            callback.argument_count > callback.arguments.size()) {
            throw AndroidGuestProcessError("invalid OpenSL guest callback event");
        }
        {
            const std::scoped_lock lock(open_sles_callback_mutex_);
            if (open_sles_callback_stopping_ || open_sles_callback_failure_) return;
            open_sles_callbacks_.push_back(callback);
        }
        open_sles_callback_ready_.notify_one();
    }

    void StartOpenSlesCallbackThread() {
        if (open_sles_callback_memory_reserved_) {
            address_space_.Unmap(
                {memory::GuestAddress{kOpenSlesCallbackTls},
                 address_space_.PageSize()});
            address_space_.Unmap(
                {memory::GuestAddress{kOpenSlesCallbackThreadInfo},
                 address_space_.PageSize()});
            address_space_.Unmap(
                {memory::GuestAddress{kOpenSlesCallbackStack},
                 kDexVmStackSize});
            open_sles_callback_memory_reserved_ = false;
        }
        PrepareDexVmThread(kOpenSlesCallbackThreadId,
                           kOpenSlesCallbackAllocationSlot, false);
        try {
            open_sles_callback_thread_ = std::jthread([this] {
                for (;;) {
                    OpenSlesGuestCallback callback;
                    {
                        std::unique_lock lock(open_sles_callback_mutex_);
                        open_sles_callback_ready_.wait(lock, [this] {
                            return open_sles_callback_stopping_ ||
                                   !open_sles_callbacks_.empty();
                        });
                        if (open_sles_callback_stopping_) return;
                        callback = open_sles_callbacks_.front();
                        open_sles_callbacks_.pop_front();
                    }
                    try {
                        A32GuestCallFrame frame;
                        frame.target = memory::GuestAddress{callback.function};
                        frame.thread_id = kOpenSlesCallbackThreadId;
                        const auto register_count = std::min<std::size_t>(
                            callback.argument_count, frame.registers.size());
                        std::copy_n(callback.arguments.begin(), register_count,
                                    frame.registers.begin());
                        std::array<std::uint32_t, 2> stack{};
                        const auto stack_count = callback.argument_count > 4U
                                                     ? callback.argument_count - 4U
                                                     : 0U;
                        if (stack_count != 0U) {
                            std::copy_n(callback.arguments.begin() + 4U,
                                        stack_count, stack.begin());
                            frame.stack_words =
                                std::span(stack).first(stack_count);
                        }
                        static_cast<void>(Invoke(frame));
                    } catch (...) {
                        const std::scoped_lock lock(open_sles_callback_mutex_);
                        if (!open_sles_callback_failure_) {
                            open_sles_callback_failure_ =
                                std::current_exception();
                        }
                        open_sles_callbacks_.clear();
                    }
                }
            });
        } catch (...) {
            ReleaseDexVmThread(kOpenSlesCallbackThreadId);
            throw;
        }
    }

    void StopOpenSlesCallbackThread() noexcept {
        {
            const std::scoped_lock lock(open_sles_callback_mutex_);
            open_sles_callback_stopping_ = true;
            open_sles_callbacks_.clear();
        }
        open_sles_callback_ready_.notify_all();
        static_cast<void>(futex_table_.InterruptAll());
        static_cast<void>(environment_.InterruptMonitorWaiters());
        if (open_sles_callback_thread_.joinable()) {
            open_sles_callback_thread_.join();
        }
        ReleaseDexVmThread(kOpenSlesCallbackThreadId);
    }

    void ReserveOpenSlesCallbackMemory() {
        const auto rw = memory::PageProtection::read |
                        memory::PageProtection::write;
        address_space_.Map(
            {memory::GuestAddress{kOpenSlesCallbackTls},
             address_space_.PageSize()}, rw);
        try {
            address_space_.Map(
                {memory::GuestAddress{kOpenSlesCallbackThreadInfo},
                 address_space_.PageSize()}, rw);
            try {
                address_space_.Map(
                    {memory::GuestAddress{kOpenSlesCallbackStack},
                     kDexVmStackSize}, rw);
            } catch (...) {
                address_space_.Unmap(
                    {memory::GuestAddress{kOpenSlesCallbackThreadInfo},
                     address_space_.PageSize()});
                throw;
            }
        } catch (...) {
            address_space_.Unmap(
                {memory::GuestAddress{kOpenSlesCallbackTls},
                 address_space_.PageSize()});
            throw;
        }
        open_sles_callback_memory_reserved_ = true;
    }

    void RethrowOpenSlesCallbackFailure() {
        std::exception_ptr failure;
        {
            const std::scoped_lock lock(open_sles_callback_mutex_);
            failure = open_sles_callback_failure_;
        }
        if (failure) std::rethrow_exception(failure);
    }

    void Stop() {
        if (!running_) return;
        StopOpenSlesCallbackThread();
        std::vector<std::uint64_t> dexvm_thread_ids;
        {
            const std::scoped_lock lock(dexvm_threads_mutex_);
            dexvm_thread_ids.reserve(dexvm_threads_.size());
            for (const auto& [thread_id, _] : dexvm_threads_) {
                dexvm_thread_ids.push_back(thread_id);
            }
        }
        for (const auto thread_id : dexvm_thread_ids) {
            ReleaseDexVmThread(thread_id);
        }
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
            dispatcher_, address_space_,
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
    JniFieldStore& Fields() noexcept { return fields_; }
    JniNativeRegistry& Natives() noexcept { return natives_; }
    JniGuestObjectRegistry& Objects() noexcept { return objects_; }
    JniStringStore& Strings() noexcept { return strings_; }
    JniPrimitiveArrayStore& Arrays() noexcept { return arrays_; }
    dexvm::NioRuntime& NIO() noexcept { return nio_; }
    audio::JavaSoundPoolState& SoundPoolState() noexcept {
        return sound_pool_; }
    audio::JavaSoundPoolMixer& SoundPoolMixer() noexcept {
        return sound_pool_mixer_; }
    audio::OpenSlesPcmMixer& PcmPlayback() noexcept {
        return boundary_.PcmPlayback(); }
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
    std::uint32_t InvokeManagedGles(
        const gles::GlesApi api, const std::string_view name,
        const std::span<const std::uint32_t> arguments,
        const std::uint64_t thread_id) {
        return boundary_.InvokeManagedGles(api, name, arguments, thread_id);
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
        RethrowOpenSlesCallbackFailure();
        const auto frames =
            sound_pool_mixer_.RenderStereoPcm16(output, sample_rate);
        static_cast<void>(boundary_.MixOpenSlesPcm16(output, sample_rate));
        return frames;
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
    std::mutex nio_direct_mutex_;
    std::vector<memory::GuestRange> nio_direct_free_;
    std::uint32_t nio_direct_cursor_{kNioDirectArenaBegin};
    dexvm::NioRuntime nio_;
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
    std::mutex open_sles_callback_mutex_;
    std::condition_variable open_sles_callback_ready_;
    std::deque<OpenSlesGuestCallback> open_sles_callbacks_;
    std::exception_ptr open_sles_callback_failure_;
    std::jthread open_sles_callback_thread_;
    bool open_sles_callback_stopping_{};
    bool open_sles_callback_memory_reserved_{};
    std::mutex dexvm_threads_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<DexVmThreadContext>>
        dexvm_threads_;
    loader::Elf32LoadedNamespace loaded_;
    struct DynamicLinkHandle final {
        std::uint32_t handle{};
        std::string library;
        std::optional<loader::Elf32LinkScope> scope;
        std::uint32_t references{};
        bool boundary{};
        bool open{};
    };
    static constexpr std::uint32_t kRtldDefault = 0xffffffffU;
    std::vector<DynamicLinkHandle> dynamic_link_handles_;
    std::uint32_t next_dynamic_link_handle_{1U};
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
std::optional<A32GuestCallResult>
AndroidGuestProcess::TryInvokeRegisteredNative(
    const JniObjectIdentity java_class, const std::string_view name,
    const std::string_view descriptor, const A32GuestCallFrame& frame) {
    const auto class_name = impl_->Classes().ClassName(java_class);
    try {
        return impl_->TryInvokeRegisteredNative(java_class, name, descriptor,
                                                frame);
    } catch (const std::exception& error) {
        throw AndroidGuestProcessError(
            "registered JNI native invocation failed:\n"
            "  class=" +
            class_name + "\n  method=" + std::string(name) +
            "\n  descriptor=" + std::string(descriptor) +
            "\n  guest_thread=" + std::to_string(frame.thread_id) +
            "\n  cause:\n" + IndentDiagnostic(error.what(), "    "));
    }
}
void AndroidGuestProcess::PrepareDexVmThread(
    const std::uint64_t thread_id, const std::uint32_t allocation_slot) {
    impl_->PrepareDexVmThread(thread_id, allocation_slot);
}
void AndroidGuestProcess::ReleaseDexVmThread(
    const std::uint64_t thread_id) noexcept {
    impl_->ReleaseDexVmThread(thread_id);
}
memory::GuestAddress AndroidGuestProcess::GuestEnvironment() const noexcept { return impl_->GuestEnvironment(); }
memory::GuestAddress AndroidGuestProcess::GuestJavaVm() const noexcept { return impl_->GuestJavaVm(); }
JniEnvironment& AndroidGuestProcess::Environment() noexcept { return impl_->Environment(); }
JniClassRegistry& AndroidGuestProcess::Classes() noexcept { return impl_->Classes(); }
JniInvocationEngine& AndroidGuestProcess::Invocations() noexcept {
    return impl_->Invocations();
}
JniFieldStore& AndroidGuestProcess::Fields() noexcept { return impl_->Fields(); }
JniNativeRegistry& AndroidGuestProcess::Natives() noexcept {
    return impl_->Natives();
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
dexvm::NioRuntime& AndroidGuestProcess::NIO() noexcept { return impl_->NIO(); }
audio::JavaSoundPoolState& AndroidGuestProcess::SoundPoolState() noexcept {
    return impl_->SoundPoolState();
}
audio::JavaSoundPoolMixer& AndroidGuestProcess::SoundPoolMixer() noexcept {
    return impl_->SoundPoolMixer();
}
audio::OpenSlesPcmMixer& AndroidGuestProcess::PcmPlayback() noexcept {
    return impl_->PcmPlayback();
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
std::uint32_t AndroidGuestProcess::InvokeManagedGles(
    const gles::GlesApi api, const std::string_view name,
    const std::span<const std::uint32_t> arguments,
    const std::uint64_t thread_id) {
    return impl_->InvokeManagedGles(api, name, arguments, thread_id);
}
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
std::optional<A32GuestCallResult>
AndroidGuestCallSession::TryInvokeRegisteredNative(
    const JniObjectIdentity java_class, const std::string_view name,
    const std::string_view descriptor, const A32GuestCallFrame& frame) {
    try {
        return process_->TryInvokeRegisteredNative(
            java_class, name, descriptor, frame);
    } catch (const AndroidGuestCallSessionError&) {
        throw;
    } catch (const std::exception& error) {
        throw AndroidGuestCallSessionError(error.what());
    }
}
void AndroidGuestCallSession::PrepareDexVmThread(
    const std::uint64_t thread_id, const std::uint32_t allocation_slot) {
    process_->PrepareDexVmThread(thread_id, allocation_slot);
}
void AndroidGuestCallSession::ReleaseDexVmThread(
    const std::uint64_t thread_id) noexcept {
    process_->ReleaseDexVmThread(thread_id);
}
memory::GuestAddress AndroidGuestCallSession::GuestEnvironment() const noexcept { return process_->GuestEnvironment(); }
memory::GuestAddress AndroidGuestCallSession::GuestJavaVm() const noexcept { return process_->GuestJavaVm(); }
JniEnvironment& AndroidGuestCallSession::Environment() noexcept { return process_->Environment(); }
JniClassRegistry& AndroidGuestCallSession::Classes() noexcept { return process_->Classes(); }
JniInvocationEngine& AndroidGuestCallSession::Invocations() noexcept { return process_->Invocations(); }
JniFieldStore& AndroidGuestCallSession::Fields() noexcept { return process_->Fields(); }
JniNativeRegistry& AndroidGuestCallSession::Natives() noexcept { return process_->Natives(); }
JniGuestObjectRegistry& AndroidGuestCallSession::Objects() noexcept { return process_->Objects(); }
JniStringStore& AndroidGuestCallSession::Strings() noexcept { return process_->Strings(); }
JniPrimitiveArrayStore& AndroidGuestCallSession::Arrays() noexcept { return process_->Arrays(); }
dexvm::NioRuntime& AndroidGuestCallSession::NIO() noexcept { return process_->NIO(); }
audio::JavaSoundPoolState& AndroidGuestCallSession::SoundPoolState() noexcept { return process_->SoundPoolState(); }
audio::JavaSoundPoolMixer& AndroidGuestCallSession::SoundPoolMixer() noexcept { return process_->SoundPoolMixer(); }
audio::OpenSlesPcmMixer& AndroidGuestCallSession::PcmPlayback() noexcept { return process_->PcmPlayback(); }
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
std::uint32_t AndroidGuestCallSession::InvokeManagedGles(
    const gles::GlesApi api, const std::string_view name,
    const std::span<const std::uint32_t> arguments,
    const std::uint64_t thread_id) {
    return process_->InvokeManagedGles(api, name, arguments, thread_id);
}
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
