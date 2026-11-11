#pragma once
#include "ogplay/runtime/integration/surface_stream.h"
#include "ogplay/gles/external_texture.h"
#include "ogplay/runtime/common/guest_memory_facts.h"
#include "ogplay/runtime/boundary/sensor_directory.h"

#include "ogplay/core/atomic_shared_ptr.h"
#include "ogplay/core/logger.h"
#include "ogplay/hal/clock.h"

#include <deque>
#include <filesystem>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_manifest.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/framework/preferences_xml.h"
#include "ogplay/video/video_player.h"

namespace ogplay::audio {
class OpenSlesPcmMixer;
class EncodedMusicMixer;
class EncodedAudioDataSource;
}  // namespace ogplay::audio

namespace ogplay::runtime {
struct GraphicsStateStore;
struct RomWindowState;
struct RomActivityState;
struct RomInputState;

namespace raster {
struct GraphicsObjects;
[[nodiscard]] std::shared_ptr<GraphicsObjects> NewGraphicsObjects();
}  // namespace raster

// Shared application-window/holder alpha rule (PixelFormat.formatHasAlpha, API19).
[[nodiscard]] constexpr bool AndroidPixelFormatHasAlpha(std::int32_t format) {
    return format == -3 || format == -2 || format == 1 || format == 6 || format == 7 || format == 8 || format == 10;
}

namespace raster {
class Bitmap;
}
struct ResourceNativeState;
class ResourceAssetManager;
struct AndroidBoundaryInput;

namespace database {
class Connection;
}

class AndroidGuestCallSession;
class NativeLibraryLoader;
class NativeActivityRuntime;
class VirtualFileSystem;

// The Android device exists before an APK process starts. Keep a deterministic
// boot-age baseline so SystemClock is device-relative rather than
// process-relative.
inline constexpr std::int64_t kDefaultAndroidDeviceUptimeMillis = 60'000;

struct AndroidKeyguardState final {
    bool locked{};
    bool secure{};
    bool restricted_input{};
};

using AndroidKeyguardStateProvider = std::function<AndroidKeyguardState()>;

// Explicit bounded guest topology: a disabled logical WiFi network type,
// independent of host adapters. Online states require a separate capability.
class GuestNetworkEnvironment;

struct AndroidOfflineConnectivity final {
    bool wifi_type_supported{true};
};

// Process-visible logical devices, independent of SDL's host device ids.
inline constexpr std::int32_t kAndroidKeyboardDeviceId = -1;
inline constexpr std::int32_t kAndroidTouchDeviceId = 0;
inline constexpr std::int32_t kAndroidKeyboardSource = 0x101;
inline constexpr std::int32_t kAndroidTouchSource = 0x1002;

struct AndroidInputMotionRange final {
    std::int32_t axis{}, source{};
    float minimum{}, maximum{}, flat{}, fuzz{}, resolution{};
};

struct AndroidInputDevice final {
    std::int32_t id{};
    std::string name, descriptor;
    std::int32_t sources{}, keyboard_type{};
    std::vector<AndroidInputMotionRange> ranges;
};

// android.* intrinsic surface for the dex_activity lifecycle
// (docs/design/dexvm/03-platform-intrinsics.md §4). The catalog is a
// code-defined immutable list; handlers bind to the running guest session
// (sound mixer, VFS, platform identity) through this shared context.

// Fixed ROM classpath for the process.
struct BootClasspathJar final {
    std::string guest_path;
    std::vector<std::byte> bytes;
    loader::ApkArchive archive;
};

// Boot classpath order. OGPlay's own classes come first so that they replace
// the ROM class of the same name; the ROM jars follow unmodified.
inline constexpr std::array<std::string_view, 8> kBootClasspathJars{
    "ogplay.jar",    "core.jar",       "conscrypt.jar",        "ext.jar",
    "framework.jar", "framework2.jar", "telephony-common.jar", "android.policy.jar"};
inline constexpr std::string_view kBootClasspathDirectory = "/system/framework/";

// Reads every kBootClasspathJars archive from a payload framework directory.
[[nodiscard]] std::vector<BootClasspathJar> ReadBootClasspath(const std::filesystem::path& framework_directory);

// Parses the archives and builds the context; fails before any VM exists if a jar is unusable.
[[nodiscard]] std::shared_ptr<struct DexVmAndroidContext>
AssembleAndroidContext(std::vector<BootClasspathJar> boot_classpath);

// classes.dex, classes2.dex, ... of every boot jar, in classpath order.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> BootClasspathDexFiles(std::span<const BootClasspathJar> jars);

// First boot jar, in classpath order, that contains the entry.
struct DexVmAndroidContext final {
    std::atomic_bool process_stopping{};
    // Set from the owning native process before linking/startup. No host query.
    std::optional<GuestMemorySnapshot> memory_snapshot;
    std::optional<GuestProcessMemorySnapshot> process_memory_snapshot;
    std::function<std::optional<GuestProcessMemorySnapshot>()> read_process_memory;
    // Installed before Application startup; no host device or Binder discovery.
    std::vector<AndroidInputDevice> input_devices;
    AndroidGuestCallSession* session{};
    // One process-owned PCM backend shared by OpenSL ES and AudioTrack.
    audio::OpenSlesPcmMixer* pcm_playback{};
    // Documented shared desktop output fact. Frontends inject their session's
    // actual mixer rate when it differs from the default desktop configuration.
    std::uint32_t native_output_sample_rate{48000U};
    // Process-owned encoded-audio mixer used by SoundPool and MediaPlayer.
    audio::JavaSoundPoolMixer* encoded_audio_playback{};
    audio::EncodedMusicMixer* encoded_music{};
    mutable std::mutex audio_policy_mutex;
    std::array<std::int32_t, 10> stream_volume{7, 7, 7, 15, 7, 7, 7, 7, 7, 7};
    std::array<bool, 10> stream_mute{};
    // Process-wide APK native loader used by java.lang.System.load*.
    // The application ClassLoader has one stable non-zero identity for the
    // lifetime of this context; APS-5 intentionally does not invent a second
    // loader namespace.
    NativeLibraryLoader* native_libraries{};
    std::shared_ptr<NativeActivityRuntime> native_activity;
    std::uint64_t application_class_loader_token{1U};
    std::shared_ptr<ResourceNativeState> resource_natives;
    std::weak_ptr<ResourceAssetManager> application_resource_manager;
    std::filesystem::path framework_resource_apk;
    std::shared_ptr<const std::vector<std::byte>> sealed_apk_bytes;
    std::vector<std::byte> apk_bytes;

    [[nodiscard]] std::span<const std::byte> ApkBytes() const {
        return sealed_apk_bytes ? std::span<const std::byte>(*sealed_apk_bytes) : std::span<const std::byte>(apk_bytes);
    }

    loader::ApkArchive archive;
    std::vector<BootClasspathJar> boot_classpath;
    std::string package_name;
    // Guest-visible path of the installed APK. Context code/resource paths and
    // ApplicationInfo source paths expose this instead of leaking the
    // frontend's host path.
    std::string package_resource_path;
    // The installed guest inventory is sealed and contains only this APK.
    // Missing standalone context facts are not an empty installation list.
    bool package_inventory_known{};
    std::uint32_t package_version_code{};
    std::string package_version_name;
    std::uint32_t target_sdk_version{};
    bool strict_webview_errors{};
    std::uint32_t application_uid{10000U};
    std::string application_class_name;
    std::string guest_process_name;
    std::string application_process_name;
    std::optional<loader::AndroidManifestLabel> application_label;
    std::uint32_t application_icon{};
    std::uint32_t application_theme{};
    std::unordered_map<std::string, std::uint32_t> activity_themes;
    std::unordered_map<std::string, loader::AndroidManifestMetaDataValue> application_meta_data;
    std::vector<std::string> requested_permissions;
    bool permission_inventory_known{};
    std::vector<loader::AndroidManifestPermissionDefinition> defined_permissions;
    // Installed service discovery is limited to the sealed current-APK facts.
    // An unconfigured standalone VM must not confuse missing metadata with
    // absence.
    bool service_inventory_known{};
    bool activity_inventory_known{};
    bool receiver_inventory_known{};
    bool application_enabled{true};
    bool application_large_heap{false};
    std::vector<loader::AndroidManifestActivityComponent> activity_components;
    std::vector<loader::AndroidManifestServiceComponent> service_components;
    std::vector<loader::AndroidManifestReceiverComponent> receiver_components;
    std::vector<loader::AndroidManifestProviderComponent> provider_components;
    std::map<std::string, dexvm::VmObjectRef, std::less<>> local_provider_holders;

    struct LocalContentObserver final {
        dexvm::VmObjectRef transport{};
        std::vector<std::string> path;
        bool descendants{};
    };

    std::vector<LocalContentObserver> local_content_observers;
    std::uint64_t settings_system_version{};
    std::vector<std::pair<dexvm::VmObjectRef, dexvm::VmObjectRef>> local_broadcast_filters;
    std::map<std::string, dexvm::VmObjectRef, std::less<>> local_sticky_broadcasts;

    struct RomServiceRecord final {
        dexvm::VmObjectRef thread{}, token{}, component{}, bind_intent{}, binder{};
        std::vector<dexvm::VmObjectRef> connections{};
        std::int32_t start_id{};
        bool started{}, bound{}, published{}, rebind{};
    };

    std::map<std::string, RomServiceRecord, std::less<>> rom_services;
    bool local_services_stopping{};

    // VM execution lock serializes token lookup, mutation and GC. Registry
    // owners are weak; live wrappers trace their Intent snapshot. Alarm entries
    // (when present) strongly retain wrappers until removal or process teardown.
    struct PendingIntentRecord final {
        dexvm::VmObjectRef object{}, intent{};
        std::string creator_package;
        std::int32_t kind{}, request_code{}, flags{};
        bool canceled{};
    };

    std::unordered_map<std::uint32_t, PendingIntentRecord> pending_intents;
    std::vector<dexvm::VmObjectRef> alarm_operations;
    bool pending_intents_stopping{};
    std::unordered_set<std::string> granted_permissions;
    std::unordered_set<std::string> system_features;
    std::uint32_t surface_width{};
    std::uint32_t surface_height{};
    // Host window fact: display sleep is already inhibited for the session,
    // which is the only effect SurfaceHolder.setKeepScreenOn(true) can request.
    bool host_display_sleep_inhibited{};
    // API19 UI fallback until a title exposes reliable DisplayMetrics.
    float ui_density{1.0F};
    float ui_scaled_density{1.0F};
    std::int32_t api_level{19};
    std::string locale_language{"en"};
    std::string locale_region{"US"};
    std::string device_id{"000000000000000"};
    std::string device_software_version{"00"};
    std::string line_number;
    std::string network_operator{"00000"};
    // Host-independent default: OGPlay has no lock screen. A frontend may
    // replace this process-lifetime provider with a host-backed snapshot
    // without changing the Android facade or introducing Binder/system_server.
    AndroidKeyguardStateProvider keyguard_state_provider;
    // Read-only subset of the API 19 secure settings table. This is distinct
    // from app SharedPreferences and from telephony/serial identities.
    std::map<std::string, std::string, std::less<>> secure_settings;

    // Guest-visible external root. Filesystem capacity comes from guest VFS.
    std::string external_storage_root{"/sdcard"};

    // Deterministic time published by the lifecycle driver (unified Clock).
    // Source can be replaced with RealtimeClock(NowFunction) by deterministic tests.
    std::shared_ptr<hal::Clock> stall_clock = std::make_shared<hal::RealtimeClock>(1000);
    dexvm::VmMonitorTable* clock_monitors{};
    std::thread::id clock_driver = std::this_thread::get_id();
    std::atomic<std::int64_t> uptime_millis{kDefaultAndroidDeviceUptimeMillis};
    // System.exit(): the guest asked for the process, not an activity.
    std::atomic<bool> exit_requested{false};

    struct ActivityCommand final {
        dexvm::VmObjectRef owner{}, intent{};
        std::string descriptor, component;
        std::int32_t request_code{-1};
        std::uint32_t launch_mode{};
        bool package_affinity{true};
    };

    // Process-lifetime Application root and its attached package Context.
    // application_descriptor is published only after onCreate succeeds;
    // the object itself is provisionally visible during onCreate, matching
    // ActivityThread's initial-application identity.
    dexvm::VmObjectRef foreground_activity;
    dexvm::VmObjectRef application;
    dexvm::VmObjectRef application_base_context;
    std::string application_descriptor;
    // Shared guest filesystem: the single world view for Java File I/O and
    // native fopen alike (external mounts, APK assets, and the per-title
    // save sandbox when one is attached). ADR-0020 retired the separate
    // session-memory overlay that used to shadow it.
    VirtualFileSystem* vfs{};
    // Default-offline network capability. Production code can only enable it
    // by publishing both an explicit policy and a narrow injected transport.
    dexvm::NetworkPolicy network_policy;
    dexvm::NetworkTransport* network_transport{};
    std::optional<AndroidOfflineConnectivity> offline_connectivity{AndroidOfflineConnectivity{}};
    std::shared_ptr<GuestNetworkEnvironment> network_environment;

    // Process-local Settings provider values; application preferences live in ROM objects and VFS XML.
    std::unordered_map<std::string, PreferenceMap> preferences;

    using DatabaseValue = std::variant<std::monostate, std::int64_t, double, std::string, std::vector<std::byte>>;

    struct CursorWindowState final {
        std::string name;
        std::size_t capacity{};
        std::size_t used{};
        std::int32_t columns{};
        std::vector<std::vector<DatabaseValue>> rows{};
    };

    std::uint32_t next_cursor_window_token{1};
    std::unordered_map<std::uint32_t, CursorWindowState> cursor_windows;
    std::uint32_t next_sqlite_connection_token{1};
    std::unordered_map<std::uint32_t, std::shared_ptr<database::Connection>> sqlite_connections;

    struct SQLiteHelperState final {
        std::string name;
        std::int32_t version{};
        dexvm::VmObjectRef database{0};
    };

    std::unordered_map<std::uint32_t, SQLiteHelperState> sqlite_helpers;

    struct ParcelBinderRecord final {
        std::size_t offset{};
        std::size_t span{};
        dexvm::VmObjectRef binder;
    };

    struct ParcelBacking final {
        std::vector<std::byte> bytes;
        std::size_t position{};
        std::vector<ParcelBinderRecord> binders;
        bool allow_fds{true};
    };

    std::uint32_t next_parcel_token{1U};
    std::unordered_map<std::uint32_t, ParcelBacking> parcel_backings;

    struct BinderThreadState final {
        std::int32_t strict_mode_policy{};
    };

    std::unordered_map<std::uint64_t, BinderThreadState> binder_threads;
    // Runtime-installed local Binder objects, protected by the VM execution lock.
    std::map<std::string, dexvm::VmObjectRef, std::less<>> local_binder_services;
    // Staged ROM WMS; the projection path remains active until the P3 switch.
    std::shared_ptr<RomWindowState> rom_windows;
    std::shared_ptr<RomActivityState> rom_activities;
    std::shared_ptr<RomInputState> rom_input;

    std::int64_t last_vibration_millis{};

    // Cached service/singleton intrinsic instances by handler-defined key.
    std::unordered_map<std::string, dexvm::VmObjectRef> singletons;

    // Offline telephony listener registrations. A non-zero mask records the
    // requested observation; LISTEN_NONE removes it. No host radio means no
    // callbacks are generated.
    std::unordered_map<std::uint32_t, std::int32_t> telephony_listeners;

    // Dynamic receiver registrations are owned by the Context instance that
    // performed registerReceiver(), matching LoadedApk's per-context
    // dispatcher map. Broadcast delivery remains outside this bounded model.
    std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>> broadcast_receivers;

    std::shared_ptr<const AndroidSensorDirectory> sensor_directory = std::make_shared<const AndroidSensorDirectory>();

    struct SurfaceTextureState final {
        std::shared_ptr<SurfaceStream> stream;
        dexvm::VmObjectRef weak_self;
        std::uint32_t token{}, texture{};
        gles::ExternalTextureContext binding;
        bool attached{true};
        bool released_image{};
        std::int64_t timestamp_ns{};
        std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    };

    std::shared_ptr<SurfaceStreamBudget> surface_stream_budget = std::make_shared<SurfaceStreamBudget>();
    std::unordered_map<std::uint32_t, SurfaceTextureState> surface_textures;
    std::uint32_t next_surface_token{1};
    std::function<gles::ExternalTextureContext(const gles::ExternalTextureCall&)> surface_texture_graphics;

    struct EglFacadeState final {
        std::uint32_t native_display{};
        dexvm::VmObjectRef egl14_display;
        dexvm::VmObjectRef egl14_no_display;
        dexvm::VmObjectRef egl14_no_context;
        dexvm::VmObjectRef egl14_no_surface;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_contexts;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_surfaces;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_configs;
        // Conditional display pacing for guest-owned GLSurfaceView threads.
        // Normally one swap is released per lifecycle frame. If the frame
        // driver itself enters a guest blocking primitive, swaps pass until
        // it wakes so monitor handshakes cannot depend on their own signal.
        std::mutex pace_mutex;
        std::condition_variable pace_changed;
        std::optional<std::thread::id> pace_driver;
        std::uint64_t pace_generation{};

        struct Producer {
            bool waiting{}, blocked{};
            std::uint64_t native_thread{}, context_token{};
            std::uint32_t window_surface{};
            bool wait_timed_out{};
        };

        std::unordered_set<std::uint32_t> window_surfaces;
        std::map<std::thread::id, Producer> producers;
        bool pace_driver_blocked{};
        bool pace_shutdown{};
        std::atomic<bool> surface_retired{false};
    };

    EglFacadeState egl;

    // SAX reader setup is real local state. Parsing remains an explicit gap
    // until the bounded XML callback pipeline is implemented.

    // SoundPoolImpl native tokens map the Java instance to a mixer pool.
    struct SoundPoolState final {
        std::uint32_t pool{};
        dexvm::VmObjectRef jni_weak;

        struct PendingLoad final {
            std::int32_t sound{};
            std::int32_t status{};
        };

        std::vector<PendingLoad> pending_loads;
    };

    std::unordered_map<std::uint32_t, SoundPoolState> sound_pools;

    struct AudioTrackState final {
        std::uint64_t player{};
        std::int32_t sample_rate{};
        std::int32_t channel_count{};
        std::int32_t bytes_per_sample{};
        std::int32_t buffer_size{};
        std::int32_t mode{};
        std::int32_t state{};
        std::int32_t notification_period{};
        std::int32_t marker_position{};
        bool marker_fired{};
        std::uint32_t last_notified_head{};
        std::uint32_t last_observed_head{};
        std::uint64_t written_frames{};
        std::uint64_t periodic_callbacks_generated{};
        std::uint64_t periodic_callbacks_delivered{};
        std::uint64_t periodic_callbacks_retired{};
        dexvm::VmObjectRef jni_weak;

        [[nodiscard]] std::uint64_t PendingPeriodicCallbacks() const noexcept {
            const auto accounted = periodic_callbacks_delivered + periodic_callbacks_retired;
            return periodic_callbacks_generated > accounted ? periodic_callbacks_generated - accounted : 0U;
        }

        void RetirePendingPeriodicCallbacks() noexcept { periodic_callbacks_retired += PendingPeriodicCallbacks(); }
    };

    std::unordered_map<std::uint32_t, AudioTrackState> audio_tracks;

    // Bitmap backing by instance handle: logical colors when unlocked and
    // a stable guest-format authority during active NDK pixel leases.
    struct BitmapState final {
        std::int32_t width{};
        std::int32_t height{};
        std::shared_ptr<raster::Bitmap> pixels;
        bool recycled{};
        // android.graphics.Bitmap.Config nativeInt (API 19 values 1/3/4/5).
        std::int32_t config{5};
        std::int32_t density{160};
        bool mutable_pixels{true};
        // Explicit Bitmap.setHasAlpha value; unset derives from config and pixels.
        std::optional<bool> has_alpha{};
    };

    std::unordered_map<std::uint32_t, BitmapState> bitmaps;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> bitmap_lock_roots;

    // The last canvas keeps the holder on the software presentation path.
    struct MediaPlayerState final {
        enum class Phase { idle, initialized, preparing, prepared, started, paused, stopped, completed, error };
        Phase phase{Phase::idle};
        std::uint32_t music{};
        audio::EncodedAudioSource source;
        dexvm::VmObjectRef jni_weak;
        std::shared_ptr<const audio::EncodedAudioDataSource> source_data;
        dexvm::VmObjectRef display_surface{};
        memory::GuestAddress display_window;
        std::int32_t video_generation{};
        std::int32_t audio_stream{3};
        bool looping{};
        float left_volume{1.0F}, right_volume{1.0F};
        bool prepared_event_pending{};
        bool seek_event_pending{};
        bool error_event_pending{};
    };

    std::unordered_map<std::uint32_t, MediaPlayerState> media_players;
    std::uint64_t next_encoded_audio_lease{1U};
    std::unordered_map<std::uint64_t, std::shared_ptr<const audio::EncodedAudioDataSource>> encoded_audio_leases;

    struct ToastRecord {
        dexvm::VmObjectRef callback;
        int duration{};
    };

    std::deque<ToastRecord> toasts;
    std::atomic<std::int64_t> toast_deadline_millis{std::numeric_limits<std::int64_t>::max()};

    enum class ScheduledWorkKind : std::uint8_t {
        display_vsync,
    };

    struct ScheduledWork final {
        std::int64_t deadline_millis{};
        std::uint64_t sequence{};
        ScheduledWorkKind kind{ScheduledWorkKind::display_vsync};
        dexvm::VmObjectRef looper{};
        dexvm::VmObjectRef owner{};
        dexvm::VmObjectRef target{};
        dexvm::VmObjectRef payload{};
        dexvm::VmObjectRef token{};
        std::int32_t what{};
        std::uint64_t generation{};
        std::int64_t timestamp_nanos{};
    };

    std::function<void(std::uint64_t)> prepare_native_looper;
    std::function<void(std::uint64_t, const std::function<bool()>&)> poll_native_looper;
    std::function<void(std::uint64_t)> wake_native_looper;
    std::function<void()> notify_native_loopers;
    // Resolve the VM execution token to the same process TID used by JNI.
    std::function<std::uint64_t(std::uint64_t)> native_thread_for_context;

    struct LooperState final {
        std::uint64_t context_token{};
        dexvm::VmObjectRef thread{};
        bool main{};
        bool quitting{};
        dexvm::VmObjectRef queue{};
    };

    struct PropertySetter final {
        dexvm::DexClassId target_class;
        dexvm::VmMethodId method;
        std::string name, signature;
    };

    // Stable process-local 1-based method IDs, never host addresses.
    std::vector<PropertySetter> property_setters;

    struct DisplayReceiverState final {
        dexvm::VmObjectRef receiver{}, looper{}, queue{};  // Weak receiver; Java owns its queue.
        bool requested{};
    };

    std::map<std::uint32_t, DisplayReceiverState> display_receivers;
    std::uint32_t next_display_receiver_token{1};
    std::uint32_t display_vsync_frame{};
    bool trace_enabled{true};
    // Injected logical display fact; independent of host wall-clock pacing.
    float display_refresh_rate_hz{60.0F};
    // Host frame-rate cap period (0 = none). A SurfaceHolder canvas posted from a thread other
    // than the paced lifecycle driver waits per holder, like a real buffer queue.
    std::uint64_t frame_period_ns{};

    struct HostActivityRequest {
        enum class Kind { launch, suspend, resume, shutdown } kind;
        std::string descriptor, component;
    };

    std::deque<HostActivityRequest> host_activity_requests;
    std::deque<std::pair<std::string, bool>> host_text_events;
    bool media_tick_pending{};
    std::atomic<bool> host_ime_visible{};
    std::atomic<bool> activities_destroyed{};
    std::atomic<int> requested_activity_orientation{-1};

    struct MessageQueueState final {
        std::uint64_t context_token{};
        bool wake{}, idling{}, destroyed{};
        std::int64_t poll_deadline_millis{};
    };

    std::uint32_t next_message_queue_token{1};
    std::map<std::uint32_t, std::shared_ptr<MessageQueueState>> message_queues;
    // Temporary session adapter: enqueue host commands through the ROM Handler
    // from the actual main thread when its native queue is polled.
    std::function<void(dexvm::Interpreter&)> on_first_main_idle;

    // One scheduler owns every Android/JRE delayed callback. Deadline
    // decisions use uptime_millis; the condition variable only wakes a
    // HandlerThread when work or Clock state changes.
    mutable std::mutex scheduler_mutex;
    std::condition_variable scheduler_changed;
    std::vector<ScheduledWork> scheduled_work;
    std::uint64_t next_scheduler_sequence{1};
    bool scheduler_shutdown{};
    dexvm::VmObjectRef main_looper;
    std::unordered_map<std::uint32_t, LooperState> loopers;
    std::unordered_map<std::uint64_t, dexvm::VmObjectRef> thread_loopers;
    // Owned by the DexVm bridge; set once the interpreter exists.
    dexvm::VmThreadRuntime* threads{};

    std::function<std::vector<std::byte>(std::string_view)> system_font_source;
    const std::shared_ptr<raster::GraphicsObjects> skia_objects = raster::NewGraphicsObjects();

    std::shared_ptr<GraphicsStateStore> graphics_state_store;
    // Real VideoView playback (ADR-0021). The factory is injected by the
    // frontend. Preparation and errors are deferred to the guest main pump;
    // listeners and the bound MediaPlayer identity are ordinary Java fields.
    video::VideoSourcePlayerFactory video_source_player_factory;
    mutable std::recursive_mutex video_views_mutex;

    struct VideoViewState final {
        std::unique_ptr<video::VideoPlayer> player;
        std::string guest_path;
        std::int32_t generation{};
        std::int32_t pending_event{};
        memory::GuestAddress display_window;
        bool looping{};
        std::int32_t audio_stream{3};
        std::int64_t loop_index{};
        bool frame_requested{};
        float left_volume{1.0F};
        float right_volume{1.0F};
        std::shared_ptr<const video::VideoFrame> latest_frame;
        std::int64_t duration_ms{};
        // Playback position = base_position_ms + (uptime - start_uptime)
        // while playing; frozen at base_position_ms otherwise.
        std::int64_t base_position_ms{};
        std::int64_t start_uptime_ms{};
        bool playing{};
        bool completed{};
        // Nearest-neighbour resampler state for the audio pump: the phase
        // remainder (< output rate) and the source frame that stays current
        // across pump batches while upsampling.
        std::uint32_t pcm_phase{};
        std::vector<std::int16_t> pcm_carry;
    };

    std::unordered_map<std::uint64_t, VideoViewState> video_views;
};

void RegisterAndroidAudioTrackStateTable(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);

// Called after guest workers are joined; retires media without Java callbacks.
void ReleaseAndroidRasterResources(DexVmAndroidContext& context);
void ReleaseAndroidMediaResources(DexVmAndroidContext& context);

void RegisterAndroidValueStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);

void RegisterAndroidDatabaseStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);
void ReleaseAndroidDatabaseResources(const std::shared_ptr<DexVmAndroidContext>& context);

void AttachAndroidClockObserver(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
void AttachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
void DetachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
bool EglProducersSettled(DexVmAndroidContext& context, bool wait = true);
void ReleaseOverdueEglProducers(DexVmAndroidContext& context, std::uint64_t waited_ns, core::Logger* logger);
void AdvanceEglSwapPacer(DexVmAndroidContext& context);
void ShutdownEglSwapPacer(DexVmAndroidContext& context);

struct EglSwapPacerSnapshot final {
    bool attached{};
    bool driver_blocked{};
    bool shutdown{};
    bool surface_retired{};
    std::uint64_t generation{};
};

[[nodiscard]] std::optional<EglSwapPacerSnapshot> TryEglSwapPacerSnapshot(DexVmAndroidContext& context);
// Permanently invalidates the Java EGL facade and releases any guest swap
// pacer wait. Used only when process teardown begins.
void RetireGuestEglSurface(DexVmAndroidContext& context);
void PaceEglSwap(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock, std::uint64_t native_thread = 0,
                 std::uint32_t window_surface = 0, std::uint64_t context_token = 0);

// Owner-thread helpers; video state is shared with the existing video/PCM pump.
[[nodiscard]] bool PrepareAndroidMediaVideo(DexVmAndroidContext& context, std::uint32_t owner);
void ReleaseAndroidMediaVideo(DexVmAndroidContext& context, std::uint32_t owner);
void BindAndroidMediaSurface(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context,
                             std::uint32_t owner, dexvm::VmObjectRef surface);

// Pumps attached generations at the shared uptime clock; delivers prepared,
// error and single-shot completion on the guest main thread. Listener code may
// release or replace its owner. publish signals each decoded frame to the session.
// Returns a rendered message when a guest callback raised.
[[nodiscard]] std::optional<std::string> PumpVideoViews(dexvm::Interpreter& vm, DexVmAndroidContext& context);

// Delivers AudioTrack marker/period callbacks from the lifecycle frame thread
// according to the shared PCM mixer's real playback head. Guest callbacks
// never run on the host audio thread.
[[nodiscard]] std::optional<std::string> PumpAndroidAudioTracks(dexvm::Interpreter& vm, DexVmAndroidContext& context);

struct AndroidAudioTrackDiagnosticSnapshot final {
    std::uint32_t receiver{};
    std::uint64_t player{};
    std::uint64_t written_frames{};
    std::size_t queued_bytes{};
    std::uint64_t consumed_frames{};
    std::uint64_t underrun_output_frames{};
    std::uint64_t underrun_count{};
    std::uint64_t periodic_callbacks_generated{};
    std::uint64_t periodic_callbacks_delivered{};
    std::uint64_t periodic_callbacks_deferred{};
};

struct AndroidUiSnapshot final {
    std::uint64_t windows{};
    std::optional<std::uint64_t> focus;
};

struct AndroidVideoSnapshot final {
    std::uint64_t receiver{};
    std::int64_t duration_ms{}, base_position_ms{};
    bool playing{}, completed{}, decoder_attached{};
};

[[nodiscard]] std::optional<AndroidUiSnapshot> TrySnapshotAndroidUi(dexvm::Interpreter& vm,
                                                                    const DexVmAndroidContext& context);
[[nodiscard]] std::optional<std::vector<AndroidVideoSnapshot>>
TrySnapshotAndroidVideo(const DexVmAndroidContext& context);
[[nodiscard]] std::optional<std::vector<AndroidAudioTrackDiagnosticSnapshot>>
TrySnapshotAndroidAudioTracks(dexvm::Interpreter& vm, const DexVmAndroidContext& context);

[[nodiscard]] std::vector<AndroidAudioTrackDiagnosticSnapshot>
SnapshotAndroidAudioTracks(const DexVmAndroidContext& context);
// Caller holds audio_policy_mutex; update native stream gains without touching
// Java state from the output worker.
void ApplyAudioStreamPolicy(DexVmAndroidContext& context, std::int32_t stream);

// True while at least one VideoView is actively playing decoded video. The
// frontend uses this to pace the free-running frame loop to real time so
// the deterministic per-frame uptime clock matches the wall clock during
// playback (manual stepping stays unpaced and reproducible).
[[nodiscard]] bool AnyVideoPlaying(const DexVmAndroidContext& context);
// Caller holds the VM execution lock; dirty layout cannot certify coverage.

// Mixes decoded audio of every playing VideoView into the interleaved
// stereo S16 buffer (saturating add on top of the existing content), pulling
// PCM from each player's audio cursor with deterministic nearest-neighbour
// resampling to output_rate. Mono duplicates to both channels, wider
// layouts take the first two. Returns the number of views that contributed.
// Paused, stopped and completed views contribute silence.
[[nodiscard]] std::size_t MixVideoPcmIntoAccumulator(DexVmAndroidContext& context,
                                                     std::span<std::int64_t> interleaved_stereo,
                                                     std::uint32_t output_rate);
// Reads a bounded encoded-audio window from resid, APK stored/deflate, VFS, or
// a captured FD lease. UINT64_MAX/0 length means the remainder of the source.
[[nodiscard]] audio::JavaSoundPoolMixer::EncodedResource
LoadEncodedAudioWindow(DexVmAndroidContext& context, const audio::EncodedAudioSource& source);
[[nodiscard]] std::shared_ptr<const audio::EncodedAudioDataSource>
LoadEncodedAudioSource(DexVmAndroidContext& context, const audio::EncodedAudioSource& source);
[[nodiscard]] std::uint64_t CaptureEncodedAudioWindow(DexVmAndroidContext& context, audio::EncodedAudioSource& source);
// API19 MediaPlayerService FD source over a guest VFS descriptor: dup-like
// lease of [offset, offset+length), length clipped to the file size. Returns
// the failure reason instead of a lease when the descriptor cannot be read.
[[nodiscard]] std::optional<std::string> CaptureDescriptorAudioWindow(DexVmAndroidContext& context,
                                                                      std::int32_t descriptor, std::uint64_t offset,
                                                                      std::uint64_t length,
                                                                      audio::EncodedAudioSource& source);

void PrepareAndroidMainLooper(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);

// Advances the only Android monotonic Clock and wakes scheduler waiters.
void QueueAndroidActivityRequest(DexVmAndroidContext& context, DexVmAndroidContext::HostActivityRequest request);
void QueueAndroidTextEvent(DexVmAndroidContext& context, std::string text, bool composing);
void QueueAndroidMediaTick(DexVmAndroidContext& context);
[[nodiscard]] bool AndroidMainQueueIdle(DexVmAndroidContext& context);
void NotifyAndroidScheduler(DexVmAndroidContext& context);
void AdvanceAndroidClock(DexVmAndroidContext& context, std::int64_t delta_millis);

// Stops all loopers and wakes HandlerThread teardown. Idempotent.
void ShutdownAndroidScheduler(DexVmAndroidContext& context);
void ShutdownLocalServices(dexvm::Interpreter&, DexVmAndroidContext&);
void ShutdownPendingIntents(dexvm::Interpreter&, DexVmAndroidContext&);

void RegisterAndroidSchedulerStateTable(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);
void RegisterAndroidOwnerAttachedStateTable(dexvm::Interpreter& vm,
                                            const std::shared_ptr<DexVmAndroidContext>& context);
void VisitAndroidSessionRoots(const DexVmAndroidContext& context, const dexvm::VmRootVisitor& visit);

// True when the guest asked for the session to end: System.exit(), or the
// activity that currently owns the screen finished itself with no successor
// on the way in. A finish() aimed at any other activity handle is a retired
// or departing activity and never ends the session.
[[nodiscard]] bool SessionExitRequested(const DexVmAndroidContext& context);

[[nodiscard]] dexvm::CoreIntrinsicServices
AndroidCoreIntrinsicServices(const std::shared_ptr<DexVmAndroidContext>& context);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl>
BoundCoreIntrinsicCatalog(const std::shared_ptr<DexVmAndroidContext>& context, std::uint32_t cpu_cores = 1);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl>
AndroidIntrinsicCatalog(const std::shared_ptr<DexVmAndroidContext>& context);

}  // namespace ogplay::runtime

namespace ogplay::runtime {
void RegisterAndroidSurfaceStreamStateTable(dexvm::Interpreter&, const std::shared_ptr<DexVmAndroidContext>&);
void DispatchAndroidSurfaceStreamEvents(dexvm::Interpreter&, DexVmAndroidContext&);
void ReleaseAndroidSurfaceStreams(DexVmAndroidContext&);
std::shared_ptr<SurfaceStream> ResolveAndroidSurfaceStream(dexvm::Interpreter&, DexVmAndroidContext&,
                                                           dexvm::VmObjectRef);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
void QueueAndroidDisplayFrame(dexvm::VmExecutionLock& execution_lock, DexVmAndroidContext& context);
}  // namespace ogplay::runtime
