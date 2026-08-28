#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"
#include "ogplay/runtime/framework/framework_asset.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {

namespace debug { class DiagnosticState; }

namespace dexvm { class NioRuntime; }

class JniInvocationEngine;
class JniNativeRegistry;
class JniStringStore;
class JniFieldStore;
class JniGuestObjectRegistry;
class FrameworkScreenPolicyState;
struct FrameworkLocaleConfig;

struct AndroidGuestPlatformConfig final {
    std::string installation_id{"ogplay-installation"};
    std::string version_name{"unknown"};
    std::string operator_name{"offline"};
    std::string line_number;
    std::string host_name{"generic"};
    std::string user_agent{"OGPlay"};
    std::string mac_address{"00:00:00:00:00:00"};
};

class AndroidGuestPlatformState final {
public:
    void SetUniqueCode(std::int32_t value);
    void RequestBackground();
    void SetFullyLoaded();
    void SetKeyboard(bool visible, std::span<const JniChar> text);
    void RequestManagedSwap();
    void RecordOfflineTracking();
    void IncreaseLaunchCount();
    [[nodiscard]] std::optional<std::int32_t> UniqueCode() const;
    [[nodiscard]] bool BackgroundRequested() const;
    [[nodiscard]] bool FullyLoaded() const;
    [[nodiscard]] bool KeyboardVisible() const;
    [[nodiscard]] std::vector<JniChar> KeyboardText() const;
    [[nodiscard]] std::uint64_t ManagedSwapRequests() const;
    [[nodiscard]] std::uint64_t OfflineTrackingCount() const;
    [[nodiscard]] std::int32_t LaunchCount() const;

private:
    mutable std::mutex mutex_;
    std::optional<std::int32_t> unique_code_;
    std::vector<JniChar> keyboard_text_;
    std::uint64_t managed_swap_requests_{};
    std::uint64_t offline_tracking_count_{};
    std::int32_t launch_count_{};
    bool background_requested_{};
    bool fully_loaded_{};
    bool keyboard_visible_{};
};

struct AndroidGuestMovieRequest final {
    std::uint64_t sequence{};
    std::uint64_t thread_id{};
    std::vector<JniChar> name;
};

class AndroidGuestMovieState final {
public:
    void Request(std::uint64_t thread_id, std::span<const JniChar> name);
    [[nodiscard]] std::optional<AndroidGuestMovieRequest> Latest() const;
    [[nodiscard]] std::uint64_t RequestCount() const;

private:
    mutable std::mutex mutex_;
    std::optional<AndroidGuestMovieRequest> latest_;
    std::uint64_t request_count_{};
};

class AndroidGuestLegacyMediaState final {
public:
    struct AudioTrackSnapshot final {
        std::int32_t sample_rate{};
        std::int32_t channels{};
        std::int32_t buffer_size{};
        std::uint64_t bytes_written{};
        bool playing{};
        bool paused{};
        bool released{};
    };

    void SetPcmPlayback(audio::OpenSlesPcmMixer* playback);

    void Record(std::string_view method);
    void SetMasterVolume(float volume);
    void SetMusicVolume(std::int32_t resource, float volume);
    [[nodiscard]] float MasterVolume() const;
    [[nodiscard]] float MusicVolume(std::int32_t resource) const;
    [[nodiscard]] std::uint64_t CallbackCount(std::string_view method) const;
    [[nodiscard]] static std::int32_t MinimumAudioTrackBuffer(
        std::int32_t sample_rate, std::int32_t channel_config,
        std::int32_t encoding);
    void ConfigureAudioTrack(JniObjectIdentity track, std::int32_t sample_rate,
                             std::int32_t channel_config,
                             std::int32_t encoding, std::int32_t buffer_size,
                             std::int32_t mode);
    void PauseAudioTrack(JniObjectIdentity track);
    void PlayAudioTrack(JniObjectIdentity track);
    void StopAudioTrack(JniObjectIdentity track);
    void ReleaseAudioTrack(JniObjectIdentity track);
    void WriteAudioTrack(JniObjectIdentity track,
                         std::span<const JniByte> bytes);
    [[nodiscard]] AudioTrackSnapshot AudioTrack(
        JniObjectIdentity track) const;

private:
    struct AudioTrackRecord final {
        AudioTrackSnapshot snapshot;
        audio::OpenSlesPcmMixer::PlayerId player{};
    };
    mutable std::mutex mutex_;
    std::map<std::string, std::uint64_t, std::less<>> callback_counts_;
    std::map<std::int32_t, float> music_volumes_;
    std::map<std::uint64_t, AudioTrackRecord> audio_tracks_;
    audio::OpenSlesPcmMixer* pcm_playback_{};
    float master_volume_{1.0F};
};

class AndroidGuestProcessState final {
public:
    void RequestExit() noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] std::uint64_t ExitRequestCount() const noexcept;

private:
    std::atomic<bool> exit_requested_{};
    std::atomic<std::uint64_t> exit_request_count_{};
};

// Installs the JNI-visible legacy media classes whose handlers are bound by
// BindAndroidGuestJavaMediaHandlers. Idempotent so standalone tests and the
// full call session share exactly one declaration.
[[nodiscard]] JniObjectIdentity InstallAndroidGuestJavaMediaClasses(
    JniClassRegistry& classes);

void BindAndroidGuestJavaAudioHandlers(
    JniInvocationEngine& invocations,
    audio::JavaSoundPoolState& sound_pool,
    audio::JavaSoundPoolMixer* mixer = nullptr);

void BindAndroidGuestJavaMediaHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, JniPrimitiveArrayStore& arrays,
    AndroidGuestMovieState& movie_state,
    AndroidGuestLegacyMediaState& media_state,
    const audio::JavaSoundPoolMixer::EncodedResourceLoader& resource_loader);

void BindAndroidGuestJavaMovieHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, AndroidGuestMovieState& movie_state);

void BindAndroidGuestJavaDisplayHandlers(
    JniInvocationEngine& invocations,
    FrameworkScreenPolicyState& screen_policy);

void BindAndroidGuestJavaProcessHandlers(
    JniInvocationEngine& invocations,
    AndroidGuestProcessState& process_state);

void BindAndroidGuestJavaLocaleHandlers(
    JniInvocationEngine& invocations,
    const FrameworkLocaleConfig& locale);

void BindAndroidGuestJavaPlatformHandlers(
    JniInvocationEngine& invocations, JniEnvironment& environment,
    JniStringStore& strings, JniPrimitiveArrayStore& arrays,
    AndroidGuestPlatformState& state,
    const AndroidGuestPlatformConfig& config);

struct AndroidGuestFrameworkPlatformSet final {
    JniObjectIdentity context_class;
    JniObjectIdentity content_resolver_class;
    JniObjectIdentity telephony_class;
    JniObjectIdentity uuid_class;
    JniObjectIdentity context;
    JniObjectIdentity content_resolver;
    JniObjectIdentity telephony;
    JniObjectIdentity uuid;
};

[[nodiscard]] AndroidGuestFrameworkPlatformSet
InstallAndroidGuestFrameworkPlatform(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings,
    JniFieldStore& fields, JniGuestObjectRegistry& objects,
    std::uint64_t thread_id, const AndroidGuestPlatformConfig& config);

// Virtual-device facts published to API 19 guest /proc files. Values are
// explicit session configuration and never observations of the host machine.
struct GuestProcFacts final {
    std::uint32_t memory_total_kb{524288};
    std::uint32_t memory_free_kb{262144};
};

struct AndroidGuestCallSessionRequest final {
    std::uint32_t api{19};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
    gles::AngleBackend backend;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t maximum_ticks_per_call{UINT64_C(10000000000)};
    std::uint32_t supersample_factor{1};
    VirtualFileSystem* filesystem{};
    std::function<void(std::string_view)> progress;
    std::optional<FrameworkDirectAssetImplementations> direct_assets{};
    AndroidBoundaryOptions boundary_options{};
    audio::JavaSoundPoolMixer::EncodedResourceLoader sound_resource_loader{};
    A32GuestCallSliceObserver guest_call_slice_observer{};
    AndroidGuestPlatformConfig platform{};
    GuestProcFacts proc_facts{};
    std::shared_ptr<debug::DiagnosticState> diagnostics;
};

// Creates the Android-native process substrate before any APK application
// module is admitted. system_modules must contain libc.so and its already
// selected API-level dependency closure; APK application ELFs are deliberately
// outside this request and are added by the later app-loading layer.
struct AndroidGuestProcessRequest final {
    std::uint32_t api{19};
    std::span<const loader::Elf32ModuleInput> system_modules;
    gles::AngleBackend backend;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t maximum_ticks_per_call{UINT64_C(10000000000)};
    std::uint32_t supersample_factor{1};
    VirtualFileSystem* filesystem{};
    std::function<void(std::string_view)> progress;
    std::optional<FrameworkDirectAssetImplementations> direct_assets{};
    AndroidBoundaryOptions boundary_options{};
    audio::JavaSoundPoolMixer::EncodedResourceLoader sound_resource_loader{};
    A32GuestCallSliceObserver guest_call_slice_observer{};
    AndroidGuestPlatformConfig platform{};
    GuestProcFacts proc_facts{};
    std::shared_ptr<debug::DiagnosticState> diagnostics;
};

class AndroidGuestProcessError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AndroidGuestApplicationModuleSource final {
    std::string name;
    std::span<const std::byte> image;
};

struct AndroidGuestApplicationLoad final {
    std::size_t root_module_index{};
    std::vector<std::string> initialized_modules;
};

class AndroidGuestProcess final : public core::GpuStateProvider {
public:
    [[nodiscard]] static std::unique_ptr<AndroidGuestProcess> Start(
        const AndroidGuestProcessRequest& request);
    ~AndroidGuestProcess();
    AndroidGuestProcess(const AndroidGuestProcess&) = delete;
    AndroidGuestProcess& operator=(const AndroidGuestProcess&) = delete;

    [[nodiscard]] A32GuestCallResult Invoke(const A32GuestCallFrame& frame);
    [[nodiscard]] std::optional<A32GuestCallResult>
    TryInvokeRegisteredNative(
        JniObjectIdentity java_class, std::string_view name,
        std::string_view descriptor, const A32GuestCallFrame& frame);
    void PrepareDexVmThread(std::uint64_t thread_id,
                            std::uint32_t allocation_slot);
    void ReleaseDexVmThread(std::uint64_t thread_id) noexcept;
    [[nodiscard]] memory::GuestAddress GuestEnvironment() const noexcept;
    [[nodiscard]] memory::GuestAddress GuestJavaVm() const noexcept;
    [[nodiscard]] JniEnvironment& Environment() noexcept;
    [[nodiscard]] JniClassRegistry& Classes() noexcept;
    [[nodiscard]] JniInvocationEngine& Invocations() noexcept;
    [[nodiscard]] JniFieldStore& Fields() noexcept;
    [[nodiscard]] JniNativeRegistry& Natives() noexcept;
    [[nodiscard]] JniGuestObjectRegistry& Objects() noexcept;
    [[nodiscard]] JniStringStore& Strings() noexcept;
    [[nodiscard]] JniPrimitiveArrayStore& Arrays() noexcept;
    [[nodiscard]] dexvm::NioRuntime& NIO() noexcept;
    [[nodiscard]] audio::JavaSoundPoolState& SoundPoolState() noexcept;
    [[nodiscard]] audio::JavaSoundPoolMixer& SoundPoolMixer() noexcept;
    [[nodiscard]] audio::OpenSlesPcmMixer& PcmPlayback() noexcept;
    [[nodiscard]] VirtualFileSystem* Filesystem() noexcept;
    [[nodiscard]] std::optional<memory::GuestAddress> FindNativeExport(
        std::string_view class_name, std::string_view method_name,
        std::string_view descriptor) const;
    void OpenManagedSurface();
    void BindManagedSurfaceOnCallingThread();
    void ReleaseManagedSurfaceFromCallingThread();
    [[nodiscard]] bool ManagedSurfaceIsOpen() const noexcept;
    [[nodiscard]] std::string ManagedGlString(std::uint32_t parameter);
    [[nodiscard]] std::uint32_t InvokeManagedGles(
        gles::GlesApi api, std::string_view name,
        std::span<const std::uint32_t> arguments,
        std::uint64_t thread_id = 0);
    void PresentManagedSurface();
    void CloseManagedSurface();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8);
    void RecycleFrame(AndroidBoundaryFrame&& frame);
    [[nodiscard]] std::size_t RenderStereoAudio(
        std::span<std::int16_t> output, std::uint32_t sample_rate);
    [[nodiscard]] std::size_t InterruptBlockingWaits();
    // Idempotently enters process teardown: native waits are interrupted,
    // guest graphics are retired, and renewable JNI frames become cancellable.
    void BeginTeardown() noexcept;
    void Stop();
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] std::size_t ApplicationModuleCount() const noexcept;
    [[nodiscard]] std::size_t LoadedGuestModuleCount() const noexcept;
    [[nodiscard]] std::size_t AttachedJniThreadCount() const;
    [[nodiscard]] bool HasLoadedModule(std::string_view name) const;
    [[nodiscard]] AndroidGuestApplicationLoad LoadApplicationModules(
        std::string_view root_module,
        std::span<const AndroidGuestApplicationModuleSource> modules);
    [[nodiscard]] std::optional<std::uint32_t> InitializeExplicitJniLibrary(
        std::string_view root_module);
    [[nodiscard]] std::optional<AndroidGuestMovieRequest>
    LatestMovieRequest() const;
    [[nodiscard]] std::shared_ptr<debug::DiagnosticState> Diagnostics() const;

    [[nodiscard]] core::GpuStats Stats() const override;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const override;
    [[nodiscard]] core::GpuCapabilities Capabilities() const override;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const override;
    [[nodiscard]] std::optional<std::vector<core::GpuTraceEntry>>
    TryGlesTrace(std::size_t limit) const;

private:
    class Impl;
    explicit AndroidGuestProcess(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] static std::unique_ptr<AndroidGuestProcess> StartLegacy(
        const AndroidGuestCallSessionRequest& request);
    void InitializeJniLibrary();
    std::unique_ptr<Impl> impl_;
    friend class AndroidGuestCallSession;
};

class AndroidGuestCallSessionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AndroidGuestCallSession final : public core::GpuStateProvider {
public:
    [[nodiscard]] static std::unique_ptr<AndroidGuestCallSession> Start(
        const AndroidGuestCallSessionRequest& request);
    // Compatibility facade for Java/DexVM services that still consume the
    // call-session interface while application startup owns a rootless shell.
    [[nodiscard]] static std::unique_ptr<AndroidGuestCallSession> AdoptProcess(
        std::unique_ptr<AndroidGuestProcess> process);
    ~AndroidGuestCallSession();
    AndroidGuestCallSession(const AndroidGuestCallSession&) = delete;
    AndroidGuestCallSession& operator=(const AndroidGuestCallSession&) = delete;

    [[nodiscard]] A32GuestCallResult Invoke(const A32GuestCallFrame& frame);
    // Empty means no RegisterNatives mapping. Once a target is resolved, any
    // execution failure is propagated and must not be treated as a miss.
    [[nodiscard]] std::optional<A32GuestCallResult>
    TryInvokeRegisteredNative(
        JniObjectIdentity java_class, std::string_view name,
        std::string_view descriptor, const A32GuestCallFrame& frame);
    void PrepareDexVmThread(std::uint64_t thread_id,
                            std::uint32_t allocation_slot);
    void ReleaseDexVmThread(std::uint64_t thread_id) noexcept;
    [[nodiscard]] memory::GuestAddress GuestEnvironment() const noexcept;
    [[nodiscard]] memory::GuestAddress GuestJavaVm() const noexcept;
    [[nodiscard]] JniEnvironment& Environment() noexcept;
    [[nodiscard]] JniClassRegistry& Classes() noexcept;
    [[nodiscard]] JniInvocationEngine& Invocations() noexcept;
    [[nodiscard]] JniFieldStore& Fields() noexcept;
    [[nodiscard]] JniNativeRegistry& Natives() noexcept;
    [[nodiscard]] JniGuestObjectRegistry& Objects() noexcept;
    [[nodiscard]] JniStringStore& Strings() noexcept;
    [[nodiscard]] JniPrimitiveArrayStore& Arrays() noexcept;
    [[nodiscard]] dexvm::NioRuntime& NIO() noexcept;
    [[nodiscard]] audio::JavaSoundPoolState& SoundPoolState() noexcept;
    [[nodiscard]] audio::JavaSoundPoolMixer& SoundPoolMixer() noexcept;
    [[nodiscard]] audio::OpenSlesPcmMixer& PcmPlayback() noexcept;
    [[nodiscard]] VirtualFileSystem* Filesystem() noexcept;
    [[nodiscard]] AndroidGuestProcess& Process() noexcept;
    // Resolves a Java_ native export (short then long JNI name) across the
    // loaded guest namespace; empty when the method is not exported.
    [[nodiscard]] std::optional<memory::GuestAddress> FindNativeExport(
        std::string_view class_name, std::string_view method_name,
        std::string_view descriptor) const;
    void InitializeJniLibrary();
    void OpenManagedSurface();
    void BindManagedSurfaceOnCallingThread();
    void ReleaseManagedSurfaceFromCallingThread();
    [[nodiscard]] bool ManagedSurfaceIsOpen() const noexcept;
    [[nodiscard]] std::string ManagedGlString(std::uint32_t parameter);
    [[nodiscard]] std::uint32_t InvokeManagedGles(
        gles::GlesApi api, std::string_view name,
        std::span<const std::uint32_t> arguments,
        std::uint64_t thread_id = 0);
    void PresentManagedSurface();
    void CloseManagedSurface();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8);
    void RecycleFrame(AndroidBoundaryFrame&& frame);
    [[nodiscard]] std::size_t RenderStereoAudio(
        std::span<std::int16_t> output, std::uint32_t sample_rate);
    [[nodiscard]] std::size_t InterruptBlockingWaits();
    void BeginTeardown() noexcept;
    void Stop();
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] std::optional<AndroidGuestMovieRequest>
    LatestMovieRequest() const;
    [[nodiscard]] std::shared_ptr<debug::DiagnosticState> Diagnostics() const;

    [[nodiscard]] core::GpuStats Stats() const override;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const override;
    [[nodiscard]] core::GpuCapabilities Capabilities() const override;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const override;

private:
    explicit AndroidGuestCallSession(
        std::unique_ptr<AndroidGuestProcess> process) noexcept;
    std::unique_ptr<AndroidGuestProcess> process_;
};

}  // namespace ogplay::runtime
