#pragma once
#include "ogplay/runtime/integration/surface_stream.h"
#include "ogplay/gles/external_texture.h"
#include "ogplay/runtime/common/guest_memory_facts.h"
#include "ogplay/runtime/boundary/sensor_directory.h"

#include "ogplay/core/logger.h"
#include "ogplay/hal/clock.h"

#include <deque>

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
#include "ogplay/loader/arsc.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/integration/xml_cursor.h"
#include "ogplay/runtime/integration/theme_resources.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/framework/preferences_xml.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/ui/ui_tree.h"
#include "ogplay/video/video_player.h"

namespace ogplay::audio {
class OpenSlesPcmMixer;
class EncodedMusicMixer;
class EncodedAudioDataSource;
}  // namespace ogplay::audio

namespace ogplay::runtime {
// Shared application-window/holder alpha rule (PixelFormat.formatHasAlpha, API19).
[[nodiscard]] constexpr bool AndroidPixelFormatHasAlpha(std::int32_t format) {
    return format == -3 || format == -2 || format == 1 || format == 6 || format == 7 || format == 8 || format == 10;
}

class BitmapPixels;
class AndroidAssetManagerSource;
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

// Fixed before VM startup; JavaTraversal requires a sealed paired payload.
enum class AndroidUiTraversalOwner : std::uint8_t { JavaTraversal };

// Assembly facts, independent of host service discovery and test overrides.
struct AndroidPlatformServices final {
    bool accessibility_service_absent{};
};

// Validates mode/version and payload identities before constructing any context,
// registering catalogs or creating a VM. Unmarked experimental payloads fail closed.
[[nodiscard]] std::shared_ptr<struct DexVmAndroidContext> AssembleAndroidContext(std::vector<std::byte> boot_archive,
                                                                                 AndroidPlatformServices platform);

// Java colors are ARGB; UiTree and solid draw commands consume RGBA.
[[nodiscard]] constexpr std::uint32_t AndroidColorToRgba(std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

struct DexVmAndroidContext final {
    explicit DexVmAndroidContext(AndroidUiTraversalOwner owner = AndroidUiTraversalOwner::JavaTraversal,
                                 AndroidPlatformServices platform = {})
        : ui_traversal_owner(owner), platform_services(platform),
          ui_tree(CreateUiTree(ui::UiGeometryOwner::ExternalProjection, nullptr)) {}

    [[nodiscard]] static ui::UiTree CreateUiTree(ui::UiGeometryOwner owner,
                                                 std::shared_ptr<const ui::UiFallbackFont> font) {
        return ui::UiTree(owner, std::move(font));
    }

    [[nodiscard]] ui::UiTree CreateHostUiTree() const {
        return CreateUiTree(ui::UiGeometryOwner::HostLayout, ui_tree.FallbackFont());
    }

    const AndroidUiTraversalOwner ui_traversal_owner;
    const AndroidPlatformServices platform_services;

    [[nodiscard]] bool UiAccessibilityUnavailable() const noexcept {
        return platform_services.accessibility_service_absent || ui_test_accessibility_disabled;
    }

    std::uint64_t ui_measure_epoch{};
    float ui_projection_density{};  // Density used by the last completed owner traversal.

    // Weak Java identities; each stable Window content root owns its scheduling budget.
    struct UiWindowSchedule final {
        bool draw_requested{};
        std::set<dexvm::VmObjectRef> requests;
        std::uint64_t deferred_until_tick{};
        std::uint64_t watchdog_tick{};
        unsigned deferred_ticks{};
        std::uint64_t generation{}, content_revision{};
        dexvm::VmObjectRef window_token{};
        ui::Size metrics{};
        float density{};
        std::shared_ptr<const ui::UiTree> published;
    };

    std::map<dexvm::VmObjectRef, UiWindowSchedule> ui_window_schedules;
    std::uint64_t ui_owner_tick{};
    std::optional<std::thread::id> ui_java_owner_thread;
    // 0 outside layout, 1 first pass, 2 deferred-request pass (API19 ViewRootImpl).
    unsigned ui_java_layout_pass{};
    std::optional<ui::Size> ui_java_active_metrics;
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
    ThemeResources theme_resources;
    std::set<dexvm::VmObjectRef> theme_asset_owners;
    std::map<ThemeToken, dexvm::VmObjectRef> theme_owners;
    bool theme_projection_loaded{};
    bool theme_gc_registered{};
    XmlStringPoolToken resource_string_pool;
    XmlStringPoolToken rom_string_pool;
    XmlCursorRegistry xml_cursors;
    bool xml_gc_registered{};
    loader::ArscTable arsc;
    std::vector<std::byte> apk_bytes;
    loader::ApkArchive archive;
    std::vector<std::byte> boot_classpath_bytes;
    loader::ApkArchive boot_classpath_archive;
    std::string package_name;
    // Guest-visible path of the installed APK. Context code/resource paths and
    // ApplicationInfo source paths expose this instead of leaking the
    // frontend's host path.
    std::string package_resource_path;
    // The installed guest inventory is sealed and contains only this APK.
    // Missing standalone context facts are not an empty installation list.
    bool package_inventory_known{};
    // Internal sealed APK source, initialized only by ApkAssets().
    std::shared_ptr<AndroidAssetManagerSource> apk_asset_source;
    std::uint32_t package_version_code{};
    std::string package_version_name;
    std::uint32_t target_sdk_version{};
    bool strict_webview_errors{};
    std::uint32_t application_uid{10000U};
    std::string application_class_name;
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
    // API19 registers a dispatcher before an absent bind returns false.
    // Connections are strong edges of the Context, removed on unbind/sweep.
    std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>> service_connections;

    struct LocalServiceBinding final {
        std::uint64_t generation{};
        dexvm::VmObjectRef owner{}, connection{}, intent{}, component{};
        std::string name;
        bool delivered{};
    };

    struct LocalService final {
        dexvm::VmObjectRef instance{}, binder{}, intent{};
    };

    std::vector<LocalServiceBinding> local_service_bindings;
    std::unordered_map<std::string, LocalService> local_services;
    std::uint64_t next_service_generation{1};
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
    // Activity.finish() retires one activity. An installer shell finishing
    // itself right after startActivity is a handoff, and its run() keeps
    // executing on its own host thread, so the request is recorded per
    // activity handle: a finish() from an already retired activity can no
    // longer be mistaken for the session ending. Ask SessionExitRequested.
    std::atomic<std::uint32_t> finishing_activity{0};
    // Raised with a queued transition so the exit predicate can observe work
    // without reading the queue or stack across threads.
    std::atomic<bool> activity_switch_pending{false};

    // Captured lifecycle facts.
    dexvm::VmObjectRef activity;

    struct ActivityResult final {
        std::int32_t request_code{}, result_code{};
        dexvm::VmObjectRef data{};
    };

    struct ActivityRecord final {
        dexvm::VmObjectRef object{}, intent{}, content{}, caller{};
        std::int32_t request_code{-1};
        bool started{};
        std::vector<ActivityResult> results{};

        std::optional<ui::UiNodeId> window_root{};
        bool resumed{}, visible{}, stopped{};
        bool window_attached{};  // Window.Callback only; owned by lifecycle.
        dexvm::VmObjectRef window_manager{}, app_window_token{};
        dexvm::VmObjectRef render_view{};
        std::int32_t task_id{1};
        std::uint32_t launch_mode{};
        bool package_affinity{true};
        // Last complete partition survives foreground changes and failed traversals.
        dexvm::VmObjectRef java_root{};
        std::shared_ptr<const ui::UiTree> projection{};
    };

    struct ApplicationWindow final {
        dexvm::VmObjectRef owner{}, root{}, params{};
        std::uint64_t generation{};
        std::int32_t flags{}, format{};
        bool removed{};
        std::shared_ptr<const ui::UiTree> projection;
    };

    std::vector<ApplicationWindow> application_windows;
    std::uint64_t application_window_revision{}, next_application_window_generation{1};
    // Scoped owner traversal/input selector, never a Java parent or a second tree.
    dexvm::VmObjectRef application_window_root_override{};
    dexvm::VmObjectRef focused_application_window{};  // lifecycle is the only writer.
    std::optional<dexvm::IntrinsicFieldHandle> window_manager_owner_field;
    bool application_pointer_active{}, application_pointer_tail{};
    std::uint64_t application_pointer_revision{};
    dexvm::VmObjectRef application_pointer_root{};
    std::shared_ptr<AndroidBoundaryInput> application_pointer_last_input;
    std::shared_ptr<const ui::UiTree> application_pointer_projection, application_input_projection;

    struct ActivityCommand final {
        enum class Kind { launch, finish } kind{Kind::launch};
        dexvm::VmObjectRef owner{}, intent{};
        std::string descriptor, component;
        std::int32_t request_code{-1}, result_code{};
        std::uint32_t launch_mode{};
        bool package_affinity{true};
    };

    std::vector<ActivityRecord> activity_stack;
    // Contiguous task groups ordered by foreground history, back() is current.
    // One default-affinity group and independent singleInstance groups.
    std::int32_t next_task_id{2};
    std::atomic<std::size_t> activity_stack_depth{};
    std::deque<ActivityCommand> activity_commands;
    // DexActivityLifecycle is the sole writer of main-window focus. Queries
    // and callbacks read the same fact; the owner token keeps retired
    // Activity instances from observing the replacement window's focus.
    std::atomic<bool> window_has_focus{false};
    std::atomic<std::uint32_t> window_focus_activity{0};
    // Process-lifetime Application root and its attached package Context.
    // application_descriptor is published only after onCreate succeeds;
    // the object itself is provisionally visible during onCreate, matching
    // ActivityThread's initial-application identity.
    dexvm::VmObjectRef application;
    dexvm::VmObjectRef application_base_context;
    std::string application_descriptor;
    dexvm::VmObjectRef renderer;

    struct GlSurfaceRuntime final {
        dexvm::VmObjectRef view{}, owner{}, renderer{}, egl_context_factory{}, egl_config_chooser{};
        dexvm::VmObjectRef renderer_egl{}, renderer_display{}, renderer_config{};
        dexvm::VmObjectRef renderer_context{}, renderer_surface{}, renderer_gl{};
        std::vector<dexvm::VmObjectRef> events;
        std::function<void()> wake;
        std::function<void(bool)> pause;
        bool paused{}, stopped{}, ready{}, surface_available{};
    };

    std::unordered_map<std::uint32_t, std::shared_ptr<GlSurfaceRuntime>> gl_surface_runtimes;
    std::function<void(dexvm::VmObjectRef)> retire_gl_surface_view;
    dexvm::VmObjectRef egl_context_factory;
    dexvm::VmObjectRef egl_config_chooser;
    // Owned by the intrinsic renderer, backed by the normal EGL registry.
    dexvm::VmObjectRef renderer_egl, renderer_display, renderer_config;
    dexvm::VmObjectRef renderer_context, renderer_surface, renderer_gl;
    // GLSurfaceView render mode is guest-visible per view. Frame production
    // remains owned by the lifecycle/managed-surface boundary.
    std::unordered_map<std::uint32_t, std::int32_t> gl_surface_render_modes;
    dexvm::VmObjectRef gl_surface_renderer_view;
    std::unordered_map<std::uint32_t, bool> gl_surface_render_requests;
    std::unordered_map<std::uint32_t, std::int32_t> gl_surface_client_versions;
    std::unordered_map<std::uint32_t, std::vector<std::int32_t>> gl_surface_config_specs;
    std::vector<dexvm::VmObjectRef> gl_surface_events;
    // Session-owned intrinsic GLThread driver, reached through explicit hooks.
    std::function<void(dexvm::VmObjectRef)> run_gl_surface_thread;
    std::function<void()> wake_gl_surface_thread;
    bool gl_surface_thread_stopped{};
    dexvm::VmObjectRef content_view;
    std::optional<dexvm::IntrinsicFieldHandle> ui_java_activity_window, ui_java_window_content;
    // Compatibility projection of the first installed View; traversal uses the
    // window root, including its stable guest content container and all children.
    std::uint64_t ui_content_revision{};
    std::uint32_t ui_content_mutation_depth{};

    struct DialogPresentation final {
        dexvm::VmObjectRef dialog, owner;
        ui::UiNodeId root;
        std::array<std::optional<ui::UiNodeId>, 3> buttons;
        std::optional<ui::UiNodeId> prior_focus;
        bool visible{true};
        // All modal node IDs belong to this required independent HostLayout tree.
        std::shared_ptr<ui::UiTree> host_tree;
        std::uint64_t partition{}, generation{}, revision{};
        ui::Size viewport{};
        float density{1.0F};
        ui::UiNodeId panel;

        struct GuestFocus final {
            dexvm::VmObjectRef view, window_root;
            std::uint64_t window_generation{};
        };

        std::optional<GuestFocus> prior_guest_focus;
    };

    struct UiOwnerIdentity final {
        dexvm::VmObjectRef activity, root, view;
        std::uint64_t generation{}, revision{};
        std::vector<std::pair<dexvm::VmObjectRef, std::uint64_t>> mounts;
    };

    // Incremented by the ROM removal adapter, including detach/re-attach ABA.
    std::map<dexvm::VmObjectRef, std::uint64_t> ui_mount_generations;
    std::optional<UiOwnerIdentity> input_capture_identity, input_scroll_identity;
    bool input_pointer_tail{};
    bool surface_event_dispatching{};
    bool input_deep_dispatching{};
    std::map<std::uint32_t, UiOwnerIdentity> surface_owner_identities;
    std::uint64_t active_window_holder_generation{};
    std::unordered_set<std::uint32_t> window_surface_holders;
    std::uint64_t window_holder_generation{};

    // Advances only after an isolated host tree has passed layout/fit checks.
    std::uint64_t dialog_host_revision{};
    std::vector<DialogPresentation> dialog_presentations;

    struct PresentationSnapshot final {
        std::uint64_t presentation_epoch{};
        std::vector<std::shared_ptr<const ui::UiTree>> guest_window_epoch_refs;
        std::shared_ptr<const ui::UiTree> main_window;

        struct ApplicationWindowSnapshot final {
            dexvm::VmObjectRef root, owner;
            std::uint64_t generation{};
            std::int32_t flags{}, format{};
            std::shared_ptr<const ui::UiTree> projection;
        };

        std::vector<ApplicationWindowSnapshot> application_windows;
        std::uint64_t host_modal_revision{}, metrics_revision{};
        ui::Size viewport{};
        dexvm::VmObjectRef activity{};
        ui::UiBitmapCache bitmaps;
        std::map<ui::UiNodeId, video::VideoFrame> video_frames;
        std::unordered_set<ui::UiNodeId, ui::UiNodeIdHash> scaled_surface_frames;
        std::unordered_set<ui::UiNodeId, ui::UiNodeIdHash> alpha_surface_frames;

        struct ModalSnapshot final {
            dexvm::VmObjectRef dialog, owner;
            std::uint64_t partition{}, generation{}, revision{};
            std::shared_ptr<const ui::UiTree> tree;
            std::array<std::optional<ui::UiNodeId>, 3> buttons;
        };

        std::vector<ModalSnapshot> modal_stack;
    };

    enum class PresentationRejectReason : std::uint8_t {
        none,
        input_lease,
        missing_guest,
        pending_metrics_without_snapshot,
        active_window,
        application_window_metrics,
        background_window_metrics,
        producer_pixels,
        modal_tree,
        modal_viewport,
        modal_density,
        pending_metrics_without_modal,
        pending_metrics_unchanged,
        count
    };
    PresentationRejectReason presentation_last_reject{};
    std::array<std::uint64_t, static_cast<std::size_t>(PresentationRejectReason::count)> presentation_reject_counts{};

    // Owner builds partitions; render/input acquire only this immutable sequence.
    std::shared_ptr<const PresentationSnapshot> presentation_snapshot;
    std::shared_ptr<const PresentationSnapshot> input_presentation;  // Owner-scoped event lease.

    struct ModalCapture final {
        std::uint64_t partition{}, generation{};
        dexvm::VmObjectRef dialog;
    };

    std::optional<ModalCapture> dialog_capture;
    // Cancelled sequences still consume their terminal event, without retaining an owner.
    bool dialog_pointer_tail{};
    std::unordered_set<std::int32_t> dialog_key_tails;
    bool dialog_pointer_active{};
    std::optional<ui::UiNodeId> dialog_pressed_button;
    std::unordered_set<std::int32_t> dialog_keys_down;
    // A managed lifecycle sizes a newly installed content root or dynamically
    // added subtree before exposing its SurfaceHolder callbacks at the next
    // owner-thread traversal (API19 creates Surfaces in updateWindow, after the
    // attaching message returns).
    bool defer_content_surface_callbacks{};
    // Attached subtree roots whose SurfaceViews await their owner traversal.
    // getHolder only returns the stable object; traversal owns activation.
    std::unordered_set<ui::UiNodeId, ui::UiNodeIdHash> surface_traversal_pending;

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

    // SharedPreferences, keyed by prefs file name. Loaded from and written
    // back to /data/data/<pkg>/shared_prefs/<name>.xml through the VFS, so
    // they persist with the sandbox and a title that reads the file
    // directly sees the same fact (ADR-0020). Values keep their Java type
    // so mismatched getters can throw the real ClassCastException.
    std::unordered_map<std::string, PreferenceMap> preferences;
    // Names already loaded, so a reopen does not re-read the file and lose
    // uncommitted edits.
    std::unordered_map<std::string, bool> preferences_loaded;
    // SharedPreferences/Editor instance handle -> preference file name.
    std::unordered_map<std::uint32_t, std::string> preference_names;

    // Per-Editor pending changes; nullopt removes a key. Only commit/apply
    // publishes these into the unique committed preference map. No guest refs.
    struct PreferenceEditorState final {
        std::unordered_map<std::string, std::optional<PreferenceValue>> modified;
        bool clear{};
    };

    std::unordered_map<std::uint32_t, PreferenceEditorState> preference_editors;

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

    struct PathState final {
        enum class Verb : std::uint8_t { move, line, close, rect };

        struct Command final {
            Verb verb{};
            float x1{};
            float y1{};
            float x2{};
            float y2{};
            std::int32_t direction{};
        };

        std::vector<Command> commands;
    };

    std::unordered_map<std::uint32_t, PathState> paths;

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
    std::unordered_map<std::uint32_t, std::uint32_t> parcel_owner_tokens;

    struct BinderThreadState final {
        std::int32_t strict_mode_policy{};
        std::uint64_t identity_token{};
    };

    std::unordered_map<std::uint64_t, BinderThreadState> binder_threads;

    struct WakeLockState final {
        std::int32_t level_and_flags{};
        std::string tag;
        std::int32_t count{};
        bool reference_counted{true};
    };

    std::unordered_map<std::uint32_t, WakeLockState> wake_locks;
    // Shared WifiLock/MulticastLock offline leases; counters/held state live in Java.
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> wifi_lock_leases;
    std::int64_t last_vibration_millis{};

    // Editable instance handle -> owning EditText handle; the text itself
    // lives in the interpreter's builder buffer of the owner.
    std::unordered_map<std::uint32_t, std::uint32_t> editable_owner;
    std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>> text_watchers;

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

    // ActivityManager owns this on Android. The bounded process has no
    // Binder, so retain the API19 observable request per Activity identity;
    // -1 is SCREEN_ORIENTATION_UNSPECIFIED.
    std::unordered_map<std::uint32_t, std::int32_t> requested_orientations;

    // SurfaceView owns one stable SurfaceHolder. addCallback appends, as on
    // the platform: these titles register both the view and the activity on
    // the same holder and both have to receive the surface lifecycle.
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> surface_holders;
    std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>> surface_callbacks;
    // Lifecycle-owned fact: the one host window surface is open. Subtree
    // attach/detach must never mutate it. Each holder independently enters or
    // leaves active_surface_holders, mirroring SurfaceView.mSurfaceCreated.
    bool managed_host_surface_open{};
    dexvm::VmObjectRef window_surface_callback;
    dexvm::VmObjectRef window_surface_holder;
    dexvm::VmObjectRef window_input_callback;
    dexvm::VmObjectRef window_input_queue;
    std::shared_ptr<const AndroidSensorDirectory> sensor_directory = std::make_shared<const AndroidSensorDirectory>();
    // Lifecycle notification only; window state remains owned by the holder.
    std::function<void(dexvm::VmObjectRef)> retire_native_surface;
    std::function<void(dexvm::VmObjectRef, std::uint32_t, std::uint32_t)> update_native_surface;
    // Producer pixels contain no Java references; boundary workers never acquire the VM lock.
    std::mutex surface_pixels_mutex;
    std::unordered_map<std::uint32_t, video::VideoFrame> surface_pixels;
    // Protected by surface_pixels_mutex, including boundary-worker notifications.
    // An entry belongs to one created lifetime; missing/retired holders cannot be revived.
    std::unordered_map<std::uint32_t, bool> surface_buffer_queued;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> holder_surfaces;
    // Requested format survives Surface lifetimes; active format is frozen at creation/change.
    std::unordered_map<std::uint32_t, std::int32_t> surface_requested_formats;
    std::unordered_map<std::uint32_t, std::int32_t> surface_active_formats;
    std::unordered_map<std::uint32_t, std::int32_t> surface_callback_formats;
    std::int32_t window_format{1};
    std::unordered_set<std::uint32_t> active_surface_holders;
    std::unordered_set<std::uint32_t> destroying_surface_holders;
    std::unordered_map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> surface_callback_sizes;

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
    std::unordered_map<std::uint32_t, std::shared_ptr<SurfaceStream>> stream_surfaces;
    std::unordered_map<std::uint32_t, std::uint32_t> managed_surface_tokens;
    std::unordered_map<std::uint32_t, std::uint32_t> stream_surface_owners;
    std::uint32_t next_surface_token{1};
    std::function<gles::ExternalTextureContext(const gles::ExternalTextureCall&)> surface_texture_graphics;

    struct EglFacadeState final {
        dexvm::VmObjectRef display;
        dexvm::VmObjectRef no_display;
        dexvm::VmObjectRef no_context;
        dexvm::VmObjectRef no_surface;
        dexvm::VmObjectRef window_surface;
        std::uint32_t native_display{};
        std::unordered_map<std::uint32_t, std::uint32_t> configs;
        std::unordered_map<std::uint32_t, std::uint32_t> contexts;
        std::unordered_map<std::uint32_t, std::uint32_t> surfaces;
        dexvm::VmObjectRef egl14_display;
        dexvm::VmObjectRef egl14_no_display;
        dexvm::VmObjectRef egl14_no_context;
        dexvm::VmObjectRef egl14_no_surface;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_contexts;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_surfaces;
        std::unordered_map<std::uint32_t, std::uint32_t> egl14_configs;
        dexvm::VmObjectRef current_display;
        dexvm::VmObjectRef current_surface;
        dexvm::VmObjectRef current_context;
        std::optional<std::thread::id> current_thread;
        std::int32_t last_error{0x3000};
        bool initialized{};
        // Conditional display pacing for guest-owned GLSurfaceView threads.
        // Normally one swap is released per lifecycle frame. If the frame
        // driver itself enters a guest blocking primitive, swaps pass until
        // it wakes so monitor handshakes cannot depend on their own signal.
        std::mutex pace_mutex;
        std::condition_variable pace_changed;
        std::optional<std::thread::id> pace_driver;
        std::uint64_t pace_generation{};
        bool pace_driver_blocked{};
        bool pace_shutdown{};
        std::atomic<bool> surface_retired{false};
    };

    EglFacadeState egl;

    // Each View exposes one stable observer. Listener identity is retained
    // until a managed main-thread traversal. Detached observers do not receive
    // global layout events; registration itself does not invent an event.
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> view_tree_observers;

    struct ManagedTokenState final {
        dexvm::VmObjectRef root{};
        std::uint64_t generation{};
    };

    std::unordered_map<std::uint32_t, ManagedTokenState> managed_window_tokens;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> global_layout_listeners;

    // SAX reader setup is real local state. Parsing remains an explicit gap
    // until the bounded XML callback pipeline is implemented.
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> sax_content_handlers;

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
        std::shared_ptr<BitmapPixels> pixels;
        bool recycled{};
        // android.graphics.Bitmap.Config nativeInt (API 19 values 1/3/4/5).
        std::int32_t config{5};
        std::int32_t density{160};
        bool mutable_pixels{true};
    };

    std::unordered_map<std::uint32_t, BitmapState> bitmaps;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> bitmap_lock_roots;

    // SurfaceHolder software rendering owns a Canvas only while locked. The
    // Canvas buffer uses straight (not premultiplied) Android ARGB words and is converted to boundary RGBA
    // exactly once by unlockCanvasAndPost.
    struct CanvasState final {
        std::uint32_t holder{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> argb;
        bool locked{};
    };

    std::unordered_map<std::uint32_t, CanvasState> canvases;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> holder_canvases;

    struct MediaPlayerState final {
        enum class Phase { idle, initialized, preparing, prepared, started, paused, stopped, completed, error };
        Phase phase{Phase::idle};
        std::uint32_t music{};
        audio::EncodedAudioSource source;
        dexvm::VmObjectRef jni_weak;
        std::shared_ptr<const audio::EncodedAudioDataSource> source_data;
        dexvm::VmObjectRef display_surface{}, display_view{};
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

    enum class ScheduledWorkKind : std::uint8_t {
        local_service_bind,
        handler_message,
        handler_runnable,
        async_post,
        async_progress,
        display_vsync,
    };

    struct ScheduledWork final {
        std::int64_t deadline_millis{};
        std::uint64_t sequence{};
        ScheduledWorkKind kind{ScheduledWorkKind::handler_message};
        dexvm::VmObjectRef looper{};
        dexvm::VmObjectRef owner{};
        dexvm::VmObjectRef target{};
        dexvm::VmObjectRef payload{};
        dexvm::VmObjectRef token{};
        std::int32_t what{};
        std::uint64_t generation{};
        std::int64_t timestamp_nanos{};
    };

    struct PendingViewAction final {
        std::uint64_t context_token{};
        dexvm::VmObjectRef runnable{};
        std::int64_t delay_millis{};
    };

    std::function<void(std::uint64_t)> prepare_native_looper;
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
    // Injected logical display fact; independent of host wall-clock pacing.
    float display_refresh_rate_hz{60.0F};
    enum class AsyncStatus : std::uint8_t { pending, running, finished };

    struct AsyncTaskState final {
        AsyncStatus status{AsyncStatus::pending};
        dexvm::VmObjectRef params{};
        dexvm::VmObjectRef result{};
        dexvm::VmObjectRef worker{};
        dexvm::VmObjectRef thread{};
        bool cancelled{};
    };

    struct AsyncWorkerState final {
        dexvm::VmObjectRef task{};
        dexvm::VmObjectRef params{};
    };

    // One scheduler owns every Android/JRE delayed callback. Deadline
    // decisions use uptime_millis; the condition variable only wakes a
    // HandlerThread when work or Clock state changes.
    mutable std::mutex scheduler_mutex;
    std::condition_variable scheduler_changed;
    std::vector<ScheduledWork> scheduled_work;
    std::uint64_t next_scheduler_sequence{1};
    bool scheduler_shutdown{};
    dexvm::VmObjectRef main_looper;
    // OGPlay has one managed ViewRoot. Its private Handler identity is shared
    // by every attached View, matching API 19 AttachInfo dispatch ownership.
    dexvm::VmObjectRef view_root_handler;
    // API 19 ViewRootImpl keeps posts made by detached Views in a per-thread
    // RunQueue until that thread next traverses a live ViewRoot.
    std::vector<PendingViewAction> pending_view_actions;
    std::unordered_map<std::uint32_t, LooperState> loopers;
    std::unordered_map<std::uint64_t, dexvm::VmObjectRef> thread_loopers;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> handler_loopers;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> handler_callbacks;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> handler_threads;
    std::unordered_map<std::uint32_t, AsyncTaskState> async_tasks;
    std::unordered_map<std::uint32_t, AsyncWorkerState> async_workers;
    // Owned by the DexVm bridge; set once the interpreter exists.
    dexvm::VmThreadRuntime* threads{};

    // Intent component/extras live in ordinary mComponent/mExtras fields.
    // Compatibility query snapshots only; the FIFO queue owns execution.
    std::string pending_activity_descriptor;
    // Manifest component identity may differ from the instantiated class for
    // an activity-alias.
    std::string pending_activity_component_name;
    // Launch/handoff root; attached Activity.getIntent() reads its own mIntent.
    dexvm::VmObjectRef current_intent;
    // One live guest View object <-> one UiTree node. Runtime UI owns all
    // hierarchy/state/geometry; this integration layer alone owns guest refs
    // and callbacks keyed by UiNodeId.
    ui::UiTree ui_tree;
    ui::UiBitmapCache ui_bitmaps;
    std::uint32_t ui_bitmap_config_width{};
    std::uint32_t ui_bitmap_config_height{};
    std::uint32_t ui_bitmap_config_density_bits{};
    ui::UiOverlayRenderer ui_overlay_renderer;
    ui::UiOverlayRenderer ui_modal_renderer;

    std::optional<ui::Size> ui_layout_metrics;
    bool ui_layout_dispatching{};
    std::unordered_map<std::uint64_t, ui::UiNodeId> object_to_ui_node;
    std::unordered_map<ui::UiNodeId, dexvm::VmObjectRef, ui::UiNodeIdHash> ui_node_to_object;
    // One non-split gesture shared by host and guest dispatch. View bindings
    // retain receivers; capture is validated against attachment and Activity.
    bool ui_bounded_scroll_dispatch{};  // Scoped host non-split dispatch; never a position owner.
    std::uint64_t ui_touch_capture{};
    dexvm::VmObjectRef ui_touch_activity;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> ui_touch_targets;

    struct TouchClickState {
        bool eligible{};
        bool consumed{};
    };

    std::unordered_map<std::uint32_t, TouchClickState> ui_touch_clicks;

    // Guest LayoutParams objects retain their typed host value independently
    // of attachment. A View points at its current params object; attaching or
    // updateViewLayout copies that value into the UiTree's sole layout fact.
    // Temporary legacy adapter references; never part of the ROM View shape.
    // Traced from the owning View and swept with it, including detached Views.
    struct ViewAdapterReferences final {
        dexvm::VmObjectRef window_owner, focus_listener, system_ui_listener;
        // Text/style live in UiNode. Keep the distinct constructor line policy
        // and owner-traced identities here; no Java text/layout mirror.
        bool text_single_line{};
        // Bounded edit service; no second text buffer or ROM Editor/spans.
        dexvm::VmObjectRef text_editable{}, text_movement{};
        std::int32_t selection_start{}, selection_end{};
        std::optional<ui::LeafMeasureResult> text_measure;
        std::unordered_map<std::int32_t, ui::LeafMeasureResult> text_measure_cache;
        std::uint64_t text_revision{};
        dexvm::VmObjectRef text_typeface{};
        std::array<dexvm::VmObjectRef, 4> text_compounds{dexvm::VmObjectRef{}, dexvm::VmObjectRef{},
                                                         dexvm::VmObjectRef{}, dexvm::VmObjectRef{}};
    };

    std::unordered_map<std::uint32_t, ViewAdapterReferences> view_adapter_references;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> ui_view_layout_params;
    std::unordered_map<std::uint32_t, ui::ImageScaleType> ui_image_scale_types;

    struct UiDrawableState final {
        std::uint32_t resource_id{};
        std::uint8_t alpha{255};
        ui::Rect bounds;
        std::optional<ui::UiNodeId> callback_node;
        dexvm::VmObjectRef bitmap{};
        bool bitmap_drawable{false};
        std::shared_ptr<const ui::UiBitmap> image_projection;
        std::uint64_t bitmap_revision{};
        std::shared_ptr<const ui::UiBitmap> leaf_pixels;
        bool rom_skin{};
        std::array<std::int32_t, 4> skin_padding{}, skin_optical{};
        std::int32_t skin_width{-1}, skin_height{-1};
        std::vector<dexvm::VmObjectRef> skin_children;
        std::vector<std::vector<std::int32_t>> skin_states;
        std::int32_t skin_current{-1};
    };

    // Immutable leaf-resource backing belongs to its Java Drawable, never to a cache.
    // View owns the guest Drawable reference; immutable decoded pixels remain
    // shared in ui_bitmaps while alpha/bounds stay per Drawable instance.
    std::unordered_map<std::uint64_t, dexvm::VmObjectRef> ui_view_backgrounds;
    std::unordered_map<std::uint64_t, dexvm::VmObjectRef> ui_view_images;
    std::unordered_map<std::uint64_t, UiDrawableState> ui_drawables;

    // Experimental bounded measurement state; Typeface remains a Java strong field.
    std::unordered_set<std::uint32_t> identity_matrices;

    struct PaintMeasureState final {
        std::int32_t flags{0x500};  // API19 DEV_KERN_TEXT | EMBEDDED_BITMAP_TEXT.
        float text_size{12.0F};     // API19 SkPaint default.
        std::uint64_t measurements{};
        std::u16string last_text;
        float last_width{};
    };

    std::unordered_map<std::uint64_t, PaintMeasureState> paint_measure_states;
    // Real VideoView playback (ADR-0021). The factory is injected by the
    // frontend. Preparation and errors are deferred to the guest main pump;
    // listeners and the bound MediaPlayer identity are ordinary Java fields.
    video::VideoSourcePlayerFactory video_source_player_factory;
    std::optional<dexvm::IntrinsicFieldHandle> video_view_generation_field;
    mutable std::recursive_mutex video_views_mutex;

    struct VideoViewState final {
        std::unique_ptr<video::VideoPlayer> player;
        std::string guest_path;
        std::int32_t generation{};
        std::int32_t pending_event{};
        bool was_attached{};
        // Zero for VideoView (map key is the view); ordinary MediaPlayer has a
        // separate output View and keeps decoding while the output is detached.
        bool media_player{};
        dexvm::VmObjectRef display_view{};
        bool looping{};
        std::int32_t audio_stream{3};
        std::int64_t loop_index{};
        bool frame_requested{};
        float left_volume{1.0F};
        float right_volume{1.0F};
        std::optional<video::VideoFrame> latest_frame;
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

    // Bound View.mContext token. Guest object identity lives in the field slot
    // and is traced by GC; this is not a host-side Context table.
    std::optional<dexvm::IntrinsicFieldHandle> view_context_field;
    std::optional<std::array<dexvm::IntrinsicFieldHandle, 3>> view_compatibility_fields;

    struct ViewJavaLayoutFields final {
        dexvm::IntrinsicFieldHandle resources;
        std::array<dexvm::IntrinsicFieldHandle, 4> padding;
        std::array<dexvm::IntrinsicFieldHandle, 2> minimum;
        // left, right, bottom, start, end, initial left, initial right.
        std::array<dexvm::IntrinsicFieldHandle, 7> user_padding;
        std::array<dexvm::IntrinsicFieldHandle, 2> padding_defined;
    };

    std::optional<ViewJavaLayoutFields> view_java_layout_fields;

    struct ViewJavaMeasureFields final {
        std::array<dexvm::IntrinsicFieldHandle, 2> measured;
        std::array<dexvm::IntrinsicFieldHandle, 2> old_specs;
        std::array<dexvm::IntrinsicFieldHandle, 3> flags;
        dexvm::IntrinsicFieldHandle cache;
        dexvm::IntrinsicFieldHandle view_flags;
        // Read-only geometry bindings: ROM alone writes these fields.
        std::array<dexvm::IntrinsicFieldHandle, 4> frame;
        std::array<dexvm::IntrinsicFieldHandle, 2> scroll;
        std::array<dexvm::IntrinsicFieldHandle, 6> unsupported_state;
    };

    std::optional<ViewJavaMeasureFields> view_java_measure_fields;

    struct ViewJavaHierarchyFields final {
        dexvm::IntrinsicFieldHandle parent;
        dexvm::IntrinsicFieldHandle params;
        dexvm::IntrinsicFieldHandle id;
        dexvm::IntrinsicFieldHandle label_for_id;
    };

    struct ViewGroupJavaFields final {
        dexvm::IntrinsicFieldHandle children;
        dexvm::IntrinsicFieldHandle count;
        dexvm::IntrinsicFieldHandle flags;
        dexvm::IntrinsicFieldHandle mode;
        dexvm::IntrinsicFieldHandle last_touch_index;
        dexvm::IntrinsicFieldHandle touch_target;
        dexvm::IntrinsicFieldHandle hover_target;
    };

    std::optional<ViewJavaHierarchyFields> view_java_hierarchy_fields;
    std::optional<ViewGroupJavaFields> view_group_java_fields;
    bool ui_java_propagating_request{};
    // Test-container input: explicitly disabled accessibility, never a production default.
    bool ui_test_accessibility_disabled{};
    // Declaration tokens only, never guest listener identities or semantic state.
    std::optional<std::array<dexvm::IntrinsicFieldHandle, 2>> view_java_input_fields;
    std::optional<std::array<dexvm::IntrinsicFieldHandle, 4>> view_java_listener_fields;

    // Binding only; the stable Rect is a normal GC-traced field on each holder.
    std::optional<dexvm::IntrinsicFieldHandle> surface_holder_frame_field;
    std::optional<dexvm::IntrinsicFieldHandle> surface_view_requested_width_field;
    std::optional<dexvm::IntrinsicFieldHandle> surface_view_requested_height_field;
    // ROM booleans; window visibility is refreshed from the existing owner/window authority.
    std::optional<std::array<dexvm::IntrinsicFieldHandle, 3>> surface_view_visibility_fields;
    // Weak window identities; counters survive schedule invalidation, never root guest objects.
    std::map<dexvm::VmObjectRef, std::map<std::string, std::uint64_t>> ui_traversal_diagnostics;

    // Process diagnostics contain values only: survive window retirement without GC roots.
    struct UiDiagnosticCount final {
        std::uint64_t count{}, epoch{}, tick{};
    };

    std::map<std::string, UiDiagnosticCount> ui_diagnostic_totals;
    std::map<std::uint32_t, std::string> ui_producer_diagnostics;
    bool ui_diagnostic_summary_emitted{};
};

void RegisterAndroidAudioTrackStateTable(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);

// Called after guest workers are joined; retires media without Java callbacks.
void ReleaseAndroidMediaResources(DexVmAndroidContext& context);

void RegisterAndroidValueStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);

// Called after Activity base Context attachment, before onCreate. The component
// name is the launch identity (possibly an alias), not necessarily the Java
// class.
void AttachAndroidActivityIdentity(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context,
                                   dexvm::VmObjectRef activity, const std::string& component_name);
void RegisterAndroidDatabaseStateTables(dexvm::Interpreter& vm, const std::shared_ptr<DexVmAndroidContext>& context);
void ReleaseAndroidDatabaseResources(const std::shared_ptr<DexVmAndroidContext>& context);

using UiLayoutLoader = std::function<std::vector<loader::BinaryXmlElement>(std::uint32_t)>;
[[nodiscard]] std::vector<loader::BinaryXmlElement>
ExpandUiIncludes(std::span<const loader::BinaryXmlElement> elements, const UiLayoutLoader& loader,
                 std::optional<std::uint32_t> root_layout_id = std::nullopt);
void AssignViewContext(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                       dexvm::VmObjectRef owner);
void InitializeAndroidViewJavaLayout(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view);
// Test roots only: publish the current Java measured size and local frame.
// Reads Java authority; never writes measured/bounds or invokes LayoutUiTree.
// Owner-only bounded input seam: Java scrollBy/clamp, then projection-only publication.
void ScrollAndroidViewBy(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                         std::int32_t delta_y, std::int32_t delta_x = 0);

void RecordAndroidSurfaceDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context, std::string_view category,
                                    const std::string& reason, std::vector<core::LogField> fields = {},
                                    const core::RateLimitPolicy& rate_limit = {});
void RecordAndroidProducerDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                                     const std::string& reason);
// Owner stop boundary, before callbacks/teardown; idempotent, never executes guest code.
void SummarizeAndroidUiDiagnostics(dexvm::Interpreter& vm, DexVmAndroidContext& context);

void RecordAndroidTraversalDiagnostic(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef root,
                                      const std::string& reason);
void CommitAndroidMeasuredRootProjection(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef root,
                                         const ui::UiProjectionTarget& target, std::uint64_t epoch);
[[nodiscard]] ui::Insets ReadAndroidViewPadding(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                dexvm::VmObjectRef view);
[[nodiscard]] ui::Size ReadAndroidViewMinimum(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                              dexvm::VmObjectRef view);
void WriteAndroidViewPadding(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                             ui::Insets padding, std::optional<bool> explicit_padding = true);
void WriteAndroidViewMinimum(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view,
                             ui::Size minimum);
[[nodiscard]] std::u16string ResolveUiString(const DexVmAndroidContext& context, std::uint32_t resource_id);
[[nodiscard]] std::string ResolveResourceString(const DexVmAndroidContext& context, std::uint32_t resource_id);
[[nodiscard]] std::uint32_t ResolveUiColor(const DexVmAndroidContext& context, std::uint32_t resource_id);
[[nodiscard]] std::int32_t ResolveUiDimension(const DexVmAndroidContext& context, std::uint32_t resource_id,
                                              bool scaled);
[[nodiscard]] std::shared_ptr<const ui::UiBitmap> ResolveUiDrawable(DexVmAndroidContext& context,
                                                                    std::uint32_t resource_id);
[[nodiscard]] const loader::ArscEntry* SelectUiResourceEntry(const DexVmAndroidContext& context,
                                                             std::uint32_t resource_id);
[[nodiscard]] std::shared_ptr<const ui::UiBitmap> DecodeLeafBitmap(const DexVmAndroidContext& context,
                                                                   const loader::ArscEntry& entry);
[[nodiscard]] const loader::ArscEntry& ResolveUiResourceEntry(const DexVmAndroidContext& context,
                                                              std::uint32_t resource_id);

// Installs/removes the generic VmExecutionLock blocking observer for the
// lifecycle thread. The observer filters by the registered host thread id;
// a GLThread's own blocking release never changes driver state.
void AttachAndroidClockObserver(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
void AttachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
void DetachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);
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
void PaceEglSwap(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock);

// Composes attached decoded video over the current base or UI according to
// the SurfaceView layer fact; uses the same UiTree geometry and visibility.
void ComposeVideoViews(DexVmAndroidContext& context, std::vector<std::uint8_t>& rgba8, std::uint32_t width,
                       std::uint32_t height, bool on_top);
// Explicit lease: never acquires again or reads mutable producer state.
void ComposeVideoViews(const DexVmAndroidContext::PresentationSnapshot& presentation, std::vector<std::uint8_t>& rgba8,
                       std::uint32_t width, std::uint32_t height, bool on_top);
// Owner-thread helpers; video state is shared with the existing video/PCM pump.
[[nodiscard]] bool PrepareAndroidMediaVideo(DexVmAndroidContext& context, std::uint32_t owner);
void ReleaseAndroidMediaVideo(DexVmAndroidContext& context, std::uint32_t owner);
void BindAndroidMediaSurface(DexVmAndroidContext& context, std::uint32_t owner, dexvm::VmObjectRef surface);
void DetachAndroidMediaSurface(DexVmAndroidContext& context, std::uint32_t holder);
void RefreshAndroidSurfaceFrames(DexVmAndroidContext& context);

// Pumps attached generations at the shared uptime clock; delivers prepared,
// error and single-shot completion on the guest main thread. Listener code may
// release or replace its owner. publish signals each decoded frame to the session.
// Returns a rendered message when a guest callback raised.
[[nodiscard]] std::optional<std::string>
PumpVideoViews(dexvm::Interpreter& vm, DexVmAndroidContext& context,
               const std::function<void(std::vector<std::uint8_t> rgba8)>& publish, bool compose_canvas = true);

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
    std::uint64_t generation{}, nodes{}, layout_dirty{}, draw_dirty{};
    std::optional<std::uint64_t> focus;
    std::string presentation_last_reject;
    std::map<std::string, std::uint64_t> presentation_reject_counts;

    struct Modal final {
        std::uint64_t partition{}, revision{}, live_modifications{}, frozen_modifications{};
        bool visible{}, owner_visible{}, live_root_visible{}, frozen_root_visible{};
    };

    std::vector<Modal> modals;
};

void SnapshotAndroidPresentationDiagnostics(const DexVmAndroidContext& context, AndroidUiSnapshot& result);

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
[[nodiscard]] bool HasOpaqueFullscreenVideo(const DexVmAndroidContext& context);

// Mixes decoded audio of every playing VideoView into the interleaved
// stereo S16 buffer (saturating add on top of the existing content), pulling
// PCM from each player's audio cursor with deterministic nearest-neighbour
// resampling to output_rate. Mono duplicates to both channels, wider
// layouts take the first two. Returns the number of views that contributed.
// Paused, stopped and completed views contribute silence.
[[nodiscard]] std::size_t MixVideoPcmIntoAccumulator(DexVmAndroidContext& context,
                                                     std::span<std::int64_t> interleaved_stereo,
                                                     std::uint32_t output_rate);
[[nodiscard]] std::size_t MixVideoPcmIntoStereo(DexVmAndroidContext& context,
                                                std::span<std::int16_t> interleaved_stereo, std::uint32_t output_rate);

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

// Widget click dispatch uses the UiTree's resolved geometry and refreshes a
// dirty traversal before hit-test. The full gesture contract lands in LUI-9.
[[nodiscard]] std::optional<std::uint64_t> FindClickableViewAt(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                               float x, float y);

// Visible, enabled, attached View receivers under a point in Android touch
// traversal order: reverse child Z, deepest first, then the parent.
[[nodiscard]] std::vector<std::uint64_t> FindTouchReceiversAt(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                              float x, float y);

struct DeepTouchDispatchResult final {
    bool handled{};
    std::uint64_t captured_view{};
    std::optional<std::string> error;
};

[[nodiscard]] DeepTouchDispatchResult DispatchDeepTouchEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                             std::int32_t action, float x, float y,
                                                             std::uint64_t captured_view,
                                                             const AndroidBoundaryInput* snapshot = nullptr);
[[nodiscard]] bool DispatchContentTouchEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                             dexvm::VmObjectRef event);
[[nodiscard]] bool DispatchAndroidViewTouch(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                            dexvm::VmObjectRef view, dexvm::VmObjectRef event, bool group);

// DexVM/View binding helpers. BindViewToUiNode enforces the one-to-one live
// identity invariant; EnsureViewUiNode creates a detached generic node for a
// Java-created View until ViewGroup/content attachment is implemented.
void BindViewToUiNode(DexVmAndroidContext& context, dexvm::VmObjectRef view, ui::UiNodeId node);
[[nodiscard]] ui::UiNodeId EnsureViewUiNode(DexVmAndroidContext& context, dexvm::VmObjectRef view, ui::UiClass kind);
[[nodiscard]] std::optional<ui::UiNodeId> FindViewUiNode(const DexVmAndroidContext& context, std::uint64_t view_handle);
[[nodiscard]] dexvm::VmObjectRef ViewObjectForUiNode(const DexVmAndroidContext& context, ui::UiNodeId node);
void ResetViewUiState(DexVmAndroidContext& context);
[[nodiscard]] bool HasAndroidModalDialog(const DexVmAndroidContext& context);
void SynchronizeAndroidDialogs(DexVmAndroidContext& context);
[[nodiscard]] std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot>
AcquireAndroidPresentation(const DexVmAndroidContext& context);
// Owner only. No guest calls; false until a complete matching guest partition exists.
bool PublishAndroidPresentation(DexVmAndroidContext& context);
// Owner traversal prepares unpublished host partitions for changed metrics.
void ResizeAndroidDialogs(dexvm::Interpreter& vm, DexVmAndroidContext& context);
void ReleaseAndroidDialogs(DexVmAndroidContext& context);
void DismissAndroidActivityDialogs(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef owner);
[[nodiscard]] bool DispatchAndroidDialogInput(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                              const AndroidBoundaryInput& input);
[[nodiscard]] dexvm::VmObjectRef MakeAndroidKeyEvent(dexvm::Interpreter& vm, const AndroidBoundaryInput& input);
void RetireViewUiSubtree(DexVmAndroidContext& context, ui::UiNodeId node);
[[nodiscard]] ui::UiNodeId ActivityContentRoot(DexVmAndroidContext& context,
                                               dexvm::VmObjectRef owner = dexvm::VmObjectRef{});
void DetachGlSurfaceSubtree(DexVmAndroidContext& context, ui::UiNodeId subtree);

// Applies one filtered text replacement and synchronously delivers the API 19
// TextWatcher transaction on the guest thread.
namespace android_intrinsics {
bool ApplyTextEdit(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef view, std::int32_t start,
                   std::int32_t before_count, const std::u16string& replacement, bool collapse_selection = false);
}  // namespace android_intrinsics

// Bounded experimental editing consumes input against Java findFocus.
[[nodiscard]] bool DispatchAndroidBoundedEditInput(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                   const AndroidBoundaryInput& input);

// True when the view's derived bounds contain the point (up-inside check of
// a click gesture).
[[nodiscard]] bool ViewContainsPoint(dexvm::Interpreter& vm, DexVmAndroidContext& context, std::uint64_t handle,
                                     float x, float y);

struct ViewTouchResult final {
    bool handled{};
    std::optional<std::string> error;
};

[[nodiscard]] ViewTouchResult InvokeViewOnTouch(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                std::uint64_t handle, std::int32_t action, float x, float y,
                                                const AndroidBoundaryInput* snapshot = nullptr);

struct ViewGestureDispatchResult final {
    bool handled{};
    bool keep_capture{};
    bool click_eligible{};
    bool touch_consumed{};
    std::optional<std::string> error;
};

// Dispatches one event to a captured View. Gesture ownership and click
// eligibility are independent: a false touch-only DOWN falls through, while
// a clickable view with a click listener may retain capture without consuming
// OnTouchListener. Touch clicks require clickable; programmatic
// InvokeViewOnClick does not.
[[nodiscard]] ViewGestureDispatchResult DispatchViewGestureEvent(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                                 std::uint64_t handle, std::int32_t action, float x,
                                                                 float y, bool click_eligible, bool touch_consumed,
                                                                 const AndroidBoundaryInput* snapshot = nullptr);

// Invokes the registered OnClickListener.onClick(view) on the guest thread.
// Returns a rendered message when the guest callback raised.
[[nodiscard]] std::optional<std::string> InvokeViewOnClick(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                           std::uint64_t handle);

// OwnerTick begins one logical tick; BeforeInput is a read barrier in that tick.
// JavaTraversal only accepts ResolveOnly for those two production safe points.
// Legacy ResolveOnly never dispatches guest size callbacks;
// only GeometryRead refreshes Java LayoutParams. viewport preserves presentation metrics.
enum class AndroidUiTraversalReason : std::uint8_t {
    GeometryRead,
    BeforeInput,
    OwnerTick,
    BeforeSurfaceCallbacks,
    BeforePresent,
    HostDialog
};
enum class AndroidUiTraversalPhase : std::uint8_t { ResolveOnly, ResolveAndNotifySizes };
// Returns whether layout resolved or a size notification was delivered.
bool EnsureAndroidUiTraversal(dexvm::Interpreter&, DexVmAndroidContext&, AndroidUiTraversalReason,
                              AndroidUiTraversalPhase, std::optional<ui::Size> viewport = std::nullopt);

// Owner-only transient geometry, including while callbacks are executing. Never publishes,
// mutates the live tree, dispatches Surface, or enters guest code.
std::shared_ptr<const ui::UiTree> ReadAndroidUiGeometry(dexvm::Interpreter&, DexVmAndroidContext&);
std::set<dexvm::VmObjectRef>& AndroidUiMeasureRequests(dexvm::Interpreter&, DexVmAndroidContext&);

// Compatibility wrapper for BeforeSurfaceCallbacks / ResolveAndNotifySizes.
bool DispatchAndroidViewSizes(dexvm::Interpreter&, DexVmAndroidContext&);
void DispatchAndroidGlobalLayout(dexvm::Interpreter&, DexVmAndroidContext&);
void SetWindowSurfaceCallback(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef);
void SetWindowInputCallback(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef);
void DispatchWindowInputQueue(dexvm::Interpreter&, DexVmAndroidContext&, bool created);

enum class SurfaceHolderPhase : std::uint8_t { created, changed, destroyed };

// Retires only this holder, through the normal destruction transaction. Unsafe
// callback/layout/input or non-owner contexts leave retirement to the owner pump.
void ReconcileSurfaceViewVisibility(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef, bool visible);

// SurfaceHolder callbacks follow size delivery; failures return a rendered guest message.
[[nodiscard]] std::optional<std::string>
DispatchSurfaceHolderCallbacks(dexvm::Interpreter& vm, DexVmAndroidContext& context, SurfaceHolderPhase phase);

// Mirrors ViewGroup's conditional dispatchAttachedToWindow/
// dispatchDetachedFromWindow for a subtree that has just become attached to,
// or is about to leave, the live UiTree. Only SurfaceViews that already own a
// holder participate; callbacks are delivered once per holder generation.
[[nodiscard]] std::optional<std::string> AttachSurfaceViewSubtree(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                                  ui::UiNodeId subtree);
// True while node or an ancestor was attached after the last Surface traversal.
[[nodiscard]] bool SurfaceTraversalPending(const DexVmAndroidContext& context, ui::UiNodeId node);
[[nodiscard]] std::optional<std::string> DetachSurfaceViewSubtree(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                                  ui::UiNodeId subtree);

// Delivers surfaceDestroyed to the active holder generation, then forgets
// its holders/callbacks so a replacement Activity starts a fresh generation.
[[nodiscard]] std::optional<std::string> RetireSurfaceHolderGeneration(dexvm::Interpreter& vm,
                                                                       DexVmAndroidContext& context);

// Advances the only Android monotonic Clock and wakes scheduler waiters.
void AdvanceAndroidClock(DexVmAndroidContext& context, std::int64_t delta_millis);

// Stops all loopers and wakes HandlerThread teardown. Idempotent.
void ShutdownAndroidScheduler(DexVmAndroidContext& context);
void ShutdownLocalServices(dexvm::Interpreter&, DexVmAndroidContext&);
void ShutdownPendingIntents(dexvm::Interpreter&, DexVmAndroidContext&);

// Frame-boundary service for the main Looper and Java threads. Runs all due
// main work in (deadline, sequence) order and drains uncaught thread failure.
[[nodiscard]] std::optional<std::string> PumpJavaThreads(dexvm::Interpreter& vm, DexVmAndroidContext& context);

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

void SetAndroidKeyEventUnicode(dexvm::Interpreter& vm, dexvm::VmObjectRef event, std::int32_t unicode_char);

// Builds a MotionEvent intrinsic instance for input dispatch.
struct AndroidBoundaryInput;
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm, const AndroidBoundaryInput& input);
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm, std::int32_t action, float x, float y,
                                                 std::int32_t pointer);

}  // namespace ogplay::runtime

namespace ogplay::runtime {
void RegisterAndroidSurfaceStreamStateTable(dexvm::Interpreter&, const std::shared_ptr<DexVmAndroidContext>&);
void DispatchAndroidSurfaceStreamEvents(dexvm::Interpreter&, DexVmAndroidContext&);
void ReleaseAndroidSurfaceStreams(DexVmAndroidContext&);
void InitializeManagedJavaSurface(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef, std::uint32_t holder);
std::shared_ptr<SurfaceStream> ResolveAndroidSurfaceStream(dexvm::Interpreter&, DexVmAndroidContext&,
                                                           dexvm::VmObjectRef);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
[[nodiscard]] dexvm::VmObjectRef AndroidUiTraversalRoot(dexvm::Interpreter& vm, DexVmAndroidContext& context);
[[nodiscard]] bool AndroidViewBelongsToRoot(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                            dexvm::VmObjectRef view, bool visible = false);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
enum class AndroidViewListener : std::size_t { Click, Touch, LongClick, FocusChange };
[[nodiscard]] dexvm::VmObjectRef ReadAndroidViewListener(dexvm::Interpreter& vm, DexVmAndroidContext& context,
                                                         dexvm::VmObjectRef view, AndroidViewListener listener);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
[[nodiscard]] DexVmAndroidContext::UiOwnerIdentity CaptureAndroidUiOwner(dexvm::Interpreter&, DexVmAndroidContext&,
                                                                         dexvm::VmObjectRef);
[[nodiscard]] bool IsAndroidUiOwnerCurrent(dexvm::Interpreter&, DexVmAndroidContext&,
                                           const DexVmAndroidContext::UiOwnerIdentity&, bool require_enabled = true);

// One public owner event, including modal, native queue, editing and Activity fallback.
// Nested public events fail; internal View dispatch borrows the established lease.
class AndroidInputEvent final {
public:
    AndroidInputEvent(dexvm::Interpreter&, DexVmAndroidContext&);
    ~AndroidInputEvent();
    AndroidInputEvent(const AndroidInputEvent&) = delete;
    AndroidInputEvent& operator=(const AndroidInputEvent&) = delete;

private:
    DexVmAndroidContext& context_;
    bool java_;
};

[[nodiscard]] std::optional<std::string> PumpAndroidSurfaceEvents(dexvm::Interpreter&, DexVmAndroidContext&);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
[[nodiscard]] dexvm::VmObjectRef AndroidActivityWindowManager(dexvm::Interpreter&, DexVmAndroidContext&,
                                                              dexvm::VmObjectRef);
[[nodiscard]] dexvm::VmObjectRef AndroidApplicationWindowRoot(dexvm::Interpreter&, DexVmAndroidContext&,
                                                              dexvm::VmObjectRef);
void SynchronizeAndroidApplicationWindows(dexvm::Interpreter&, DexVmAndroidContext&);
void CloseAndroidApplicationWindows(dexvm::Interpreter&, DexVmAndroidContext&, dexvm::VmObjectRef owner);
[[nodiscard]] bool DispatchAndroidApplicationWindowInput(dexvm::Interpreter&, DexVmAndroidContext&,
                                                         const AndroidBoundaryInput&);
}  // namespace ogplay::runtime

namespace ogplay::runtime {
void DispatchAndroidDisplayFrame(dexvm::Interpreter&, DexVmAndroidContext&);
}
