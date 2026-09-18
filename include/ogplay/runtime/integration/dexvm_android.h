#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
}

namespace ogplay::runtime {

namespace database {
class Connection;
}

class AndroidGuestCallSession;
class NativeLibraryLoader;
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

// android.* intrinsic surface for the dex_activity lifecycle
// (docs/design/dexvm/03-platform-intrinsics.md §4). The catalog is a
// code-defined immutable list; handlers bind to the running guest session
// (sound mixer, VFS, platform identity) through this shared context.

struct DexVmAndroidContext final {
  AndroidGuestCallSession *session{};
  // One process-owned PCM backend shared by OpenSL ES and AudioTrack.
  audio::OpenSlesPcmMixer *pcm_playback{};
  // Documented shared desktop output fact. Frontends inject their session's
  // actual mixer rate when it differs from the default desktop configuration.
  std::uint32_t native_output_sample_rate{48000U};
  // Process-owned encoded-audio mixer used by SoundPool and MediaPlayer.
  audio::JavaSoundPoolMixer *encoded_audio_playback{};
  // Process-wide APK native loader used by java.lang.System.load*.
  // The application ClassLoader has one stable non-zero identity for the
  // lifetime of this context; APS-5 intentionally does not invent a second
  // loader namespace.
  NativeLibraryLoader *native_libraries{};
  std::uint64_t application_class_loader_token{1U};
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
  std::uint32_t package_version_code{};
  std::string package_version_name;
  std::uint32_t target_sdk_version{};
  std::uint32_t application_uid{10000U};
  std::string application_class_name;
  std::optional<loader::AndroidManifestLabel> application_label;
  std::uint32_t application_icon{};
  std::uint32_t application_theme{};
  std::unordered_map<std::string, std::uint32_t> activity_themes;
  std::unordered_map<std::string, loader::AndroidManifestMetaDataValue>
      application_meta_data;
  std::vector<std::string> requested_permissions;
  // Installed service discovery is limited to the sealed current-APK facts.
  // An unconfigured standalone VM must not confuse missing metadata with
  // absence.
  bool service_inventory_known{};
  bool activity_inventory_known{};
  bool application_enabled{true};
  std::vector<loader::AndroidManifestActivityComponent> activity_components;
  std::vector<loader::AndroidManifestServiceComponent> service_components;
  // API19 registers a dispatcher before an absent bind returns false.
  // Connections are strong edges of the Context, removed on unbind/sweep.
  std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>>
      service_connections;
  std::unordered_set<std::string> granted_permissions;
  std::unordered_set<std::string> system_features;
  std::uint32_t surface_width{};
  std::uint32_t surface_height{};
  // API19 UI fallback until a title exposes reliable DisplayMetrics.
  float ui_density{1.0F};
  float ui_scaled_density{1.0F};
  std::int32_t api_level{19};
  std::string language{"en"};
  std::string iso3_language{"eng"};
  std::string iso3_country{"USA"};
  std::string iso_country{"US"};
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

  // External storage facts: the guest-visible root and the real free
  // space of the backing host volume (filled by the frontend; 0 when no
  // external mount exists, which StatFs then truthfully reports).
  std::string external_storage_root{"/sdcard"};
  std::uint64_t external_free_bytes{};

  // Deterministic time published by the lifecycle driver (unified Clock).
  std::atomic<std::int64_t> uptime_millis{kDefaultAndroidDeviceUptimeMillis};
  // System.exit(): the guest asked for the process, not an activity.
  std::atomic<bool> exit_requested{false};
  // Activity.finish() retires one activity. An installer shell finishing
  // itself right after startActivity is a handoff, and its run() keeps
  // executing on its own host thread, so the request is recorded per
  // activity handle: a finish() from an already retired activity can no
  // longer be mistaken for the session ending. Ask SessionExitRequested.
  std::atomic<std::uint32_t> finishing_activity{0};
  // Raised with the pending activity target so the exit predicate can see
  // an in-flight handoff without reading that string across threads.
  std::atomic<bool> activity_switch_pending{false};

  // Captured lifecycle facts.
  dexvm::VmObjectRef activity;
  // DexActivityLifecycle is the sole writer of main-window focus. Queries
  // and callbacks read the same fact; the owner token keeps retired
  // Activity instances from observing the replacement window's focus.
  std::atomic<bool> window_has_focus{false};
  std::atomic<std::uint32_t> window_focus_activity{0};
  // Handle of the launcher activity that opened the process's single
  // task; Activity.isTaskRoot() answers against it. Stored as a plain
  // handle (not the ref) so a retired handoff shell keeps answering
  // for its own token, matching the platform.
  std::uint32_t task_root_activity{0};
  // Process-lifetime Application root and its attached package Context.
  // application_descriptor is published only after onCreate succeeds;
  // the object itself is provisionally visible during onCreate, matching
  // ActivityThread's initial-application identity.
  dexvm::VmObjectRef application;
  dexvm::VmObjectRef application_base_context;
  std::string application_descriptor;
  dexvm::VmObjectRef renderer;
  dexvm::VmObjectRef egl_context_factory;
  dexvm::VmObjectRef egl_config_chooser;
  // GLSurfaceView render mode is guest-visible per view. Frame production
  // remains owned by the lifecycle/managed-surface boundary.
  std::unordered_map<std::uint32_t, std::int32_t> gl_surface_render_modes;
  dexvm::VmObjectRef gl_surface_renderer_view;
  std::unordered_map<std::uint32_t, bool> gl_surface_render_requests;
  std::unordered_map<std::uint32_t, std::int32_t> gl_surface_client_versions;
  std::unordered_map<std::uint32_t, std::vector<std::int32_t>>
      gl_surface_config_specs;
  std::vector<dexvm::VmObjectRef> gl_surface_events;
  dexvm::VmObjectRef content_view;

  // Shared guest filesystem: the single world view for Java File I/O and
  // native fopen alike (external mounts, APK assets, and the per-title
  // save sandbox when one is attached). ADR-0020 retired the separate
  // session-memory overlay that used to shadow it.
  VirtualFileSystem *vfs{};
  // Default-offline network capability. Production code can only enable it
  // by publishing both an explicit policy and a narrow injected transport.
  dexvm::NetworkPolicy network_policy;
  dexvm::NetworkTransport *network_transport{};

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

  // DVM-88 bounded SQLite value store. The serialized database image is
  // written through the process VFS; no host path or SQLite connection is
  // exposed to platform handlers.
  using DatabaseValue = std::variant<std::monostate, std::int64_t, double,
                                     std::string, std::vector<std::byte>>;
  using DatabaseRow = std::unordered_map<std::string, DatabaseValue>;
  struct DatabaseTable final {
    std::vector<std::string> columns;
    std::vector<DatabaseRow> rows;
    std::int64_t next_row_id{1};
  };
  struct DatabaseState final {
    std::string path;
    bool open{true};
    std::int32_t version{};
    std::unordered_map<std::string, DatabaseTable> tables;
    std::shared_ptr<database::Connection> connection;
    std::vector<bool> transaction_success;
  };
  struct CursorState final {
    std::vector<std::string> columns;
    std::vector<DatabaseRow> rows;
    std::int32_t position{-1};
    bool closed{};
  };
  std::unordered_map<std::uint32_t, DatabaseRow> content_values;
  std::unordered_map<std::uint32_t, DatabaseState> databases;
  std::unordered_map<std::string, std::uint32_t> database_by_path;
  std::unordered_map<std::uint32_t, CursorState> database_cursors;
  struct CursorWindowState final {
    std::string name;
    std::size_t capacity{};
    std::size_t used{};
    std::int32_t columns{};
    std::vector<std::vector<DatabaseValue>> rows;
  };
  std::uint32_t next_cursor_window_token{1};
  std::unordered_map<std::uint32_t, CursorWindowState> cursor_windows;
  std::uint32_t next_sqlite_connection_token{1};
  std::unordered_map<std::uint32_t, std::shared_ptr<database::Connection>>
      sqlite_connections;
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
  std::int64_t last_vibration_millis{};

  // Editable instance handle -> owning EditText handle; the text itself
  // lives in the interpreter's builder buffer of the owner.
  std::unordered_map<std::uint32_t, std::uint32_t> editable_owner;
  std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>>
      text_watchers;
  dexvm::VmObjectRef focused_edit_text;

  // Cached service/singleton intrinsic instances by handler-defined key.
  std::unordered_map<std::string, dexvm::VmObjectRef> singletons;

  // Offline telephony listener registrations. A non-zero mask records the
  // requested observation; LISTEN_NONE removes it. No host radio means no
  // callbacks are generated.
  std::unordered_map<std::uint32_t, std::int32_t> telephony_listeners;

  // Dynamic receiver registrations are owned by the Context instance that
  // performed registerReceiver(), matching LoadedApk's per-context
  // dispatcher map. Broadcast delivery remains outside this bounded model.
  std::unordered_map<std::uint32_t, std::unordered_set<std::uint32_t>>
      broadcast_receivers;

  // IntentFilter keeps data schemes in insertion order and de-duplicates
  // with the framework's case-sensitive String equality.
  std::unordered_map<std::uint32_t, std::vector<std::string>>
      intent_filter_actions;
  std::unordered_map<std::uint32_t, std::vector<std::string>>
      intent_filter_categories;
  // Exact MIME strings are retained as-is. API 19 represents "major/*"
  // internally as "major" and "*/*" as "*"; matching reconstructs that
  // partial-type rule without a second flag table.
  std::unordered_map<std::uint32_t, std::vector<std::string>>
      intent_filter_types;
  std::unordered_map<std::uint32_t, std::vector<std::string>>
      intent_filter_schemes;
  struct IntentFilterAuthority final {
    std::string original_host;
    std::string match_host;
    bool wildcard{};
    std::int32_t port{-1};
  };
  std::unordered_map<std::uint32_t, std::vector<IntentFilterAuthority>>
      intent_filter_authorities;

  // ActivityManager owns this on Android. The bounded process has no
  // Binder, so retain the API19 observable request per Activity identity;
  // -1 is SCREEN_ORIENTATION_UNSPECIFIED.
  std::unordered_map<std::uint32_t, std::int32_t> requested_orientations;

  // SurfaceView owns one stable SurfaceHolder. addCallback appends, as on
  // the platform: these titles register both the view and the activity on
  // the same holder and both have to receive the surface lifecycle.
  std::unordered_map<std::uint32_t, dexvm::VmObjectRef> surface_holders;
  std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>>
      surface_callbacks;
  // Lifecycle-owned fact: the one host window surface is open. Subtree
  // attach/detach must never mutate it. Each holder independently enters or
  // leaves active_surface_holders, mirroring SurfaceView.mSurfaceCreated.
  bool managed_host_surface_open{};
  std::unordered_set<std::uint32_t> active_surface_holders;

  struct EglFacadeState final {
    dexvm::VmObjectRef display;
    dexvm::VmObjectRef no_display;
    dexvm::VmObjectRef no_context;
    dexvm::VmObjectRef no_surface;
    dexvm::VmObjectRef window_surface;
    std::uint32_t native_display{};
    std::unordered_map<std::uint32_t, std::uint32_t> configs;
    std::unordered_map<std::uint32_t,
                       std::unordered_map<std::int32_t, std::int32_t>>
        config_attribute_overrides;
    std::unordered_map<std::uint32_t, std::uint32_t> contexts;
    std::unordered_map<std::uint32_t, std::uint32_t> surfaces;
    dexvm::VmObjectRef egl14_display;
    dexvm::VmObjectRef egl14_no_display;
    dexvm::VmObjectRef egl14_no_context;
    dexvm::VmObjectRef egl14_no_surface;
    std::unordered_map<std::uint32_t, std::uint32_t> egl14_contexts;
    std::unordered_map<std::uint32_t, std::uint32_t> egl14_surfaces;
    std::unordered_map<std::uint32_t, std::uint32_t> egl14_configs;
    std::unordered_map<std::uint32_t,
                       std::unordered_map<std::int32_t, std::int32_t>>
        egl14_config_attribute_overrides;
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
  // for a future managed layout pass; registration itself does not invent
  // an event.
  std::unordered_map<std::uint32_t, dexvm::VmObjectRef> view_tree_observers;
  std::unordered_map<std::uint32_t, dexvm::VmObjectRef> global_layout_listeners;

  // SAX reader setup is real local state. Parsing remains an explicit gap
  // until the bounded XML callback pipeline is implemented.
  std::unordered_map<std::uint32_t, dexvm::VmObjectRef> sax_content_handlers;

  // SoundPool stream id -> (resource id) mapping for voice controls.
  std::unordered_map<std::int32_t, std::int32_t> sound_streams;
  std::int32_t next_sound_stream{1};

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
    dexvm::VmObjectRef position_listener;
  };
  std::unordered_map<std::uint32_t, AudioTrackState> audio_tracks;

  // Bitmap pixel stores by instance handle: real host-side ARGB8888
  // buffers so getPixels/createBitmap round-trip actual data.
  struct BitmapState final {
    std::int32_t width{};
    std::int32_t height{};
    std::vector<std::uint32_t> argb;
    bool recycled{};
    // android.graphics.Bitmap.Config nativeInt (API 19 values 1/3/4/5).
    std::int32_t config{5};
  };
  std::unordered_map<std::uint32_t, BitmapState> bitmaps;

  // SurfaceHolder software rendering owns a Canvas only while locked. The
  // Canvas buffer uses Android ARGB words and is converted to boundary RGBA
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

  // MediaPlayer playing flags by instance handle.
  std::unordered_map<std::uint32_t, audio::EncodedAudioSource> media_resources;
  std::unordered_map<std::uint32_t, bool> media_playing;
  std::unordered_map<std::uint32_t, bool> media_looping;

  enum class ScheduledWorkKind : std::uint8_t {
    handler_message,
    handler_runnable,
    timer_task,
    countdown,
    async_post,
    async_progress,
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
  };

  struct PendingViewAction final {
    std::uint64_t context_token{};
    dexvm::VmObjectRef runnable{};
    std::int64_t delay_millis{};
  };

  struct LooperState final {
    std::uint64_t context_token{};
    dexvm::VmObjectRef thread{};
    bool main{};
    bool quitting{};
  };

  struct TimerTaskState final {
    dexvm::VmObjectRef timer{};
    std::int64_t scheduled_time{};
    std::int64_t period_millis{};
    std::uint64_t generation{};
    bool fixed_rate{};
    bool scheduled{};
    bool cancelled{};
  };

  struct CountDownState final {
    std::int64_t duration_millis{};
    std::int64_t interval_millis{};
    std::int64_t stop_time_millis{};
    std::uint64_t generation{};
    bool cancelled{};
  };

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
  std::unordered_map<std::uint32_t, TimerTaskState> timer_tasks;
  std::unordered_map<std::uint32_t, bool> cancelled_timers;
  std::unordered_map<std::uint32_t, CountDownState> countdown_timers;
  std::unordered_map<std::uint32_t, AsyncTaskState> async_tasks;
  std::unordered_map<std::uint32_t, AsyncWorkerState> async_workers;
  // Owned by the DexVm bridge; set once the interpreter exists.
  dexvm::VmThreadRuntime *threads{};

  // Intent component/extras live in ordinary mComponent/mExtras fields.
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
  std::unordered_map<std::uint64_t, ui::UiNodeId> object_to_ui_node;
  std::unordered_map<ui::UiNodeId, dexvm::VmObjectRef, ui::UiNodeIdHash>
      ui_node_to_object;
  std::unordered_map<ui::UiNodeId, dexvm::VmObjectRef, ui::UiNodeIdHash>
      ui_click_listeners;
  std::unordered_map<ui::UiNodeId, dexvm::VmObjectRef, ui::UiNodeIdHash>
      ui_touch_listeners;
  // Guest LayoutParams objects retain their typed host value independently
  // of attachment. A View points at its current params object; attaching or
  // updateViewLayout copies that value into the UiTree's sole layout fact.
  std::unordered_map<std::uint32_t, dexvm::VmObjectRef> ui_view_layout_params;
  std::unordered_map<std::uint32_t, ui::ImageScaleType> ui_image_scale_types;
  struct UiDrawableState final {
    std::uint32_t resource_id{};
    std::optional<std::uint32_t> color;
    std::uint8_t alpha{255};
    ui::Rect bounds;
    std::optional<ui::UiNodeId> callback_node;
  };
  // View owns the guest Drawable reference; immutable decoded pixels remain
  // shared in ui_bitmaps while alpha/bounds stay per Drawable instance.
  std::unordered_map<std::uint64_t, dexvm::VmObjectRef> ui_view_backgrounds;
  std::unordered_map<std::uint64_t, UiDrawableState> ui_drawables;
  // VideoView -> OnCompletionListener. The guest video pump fires it once
  // at end of stream; fallback completion is also deferred to that boundary
  // so callbacks never run re-entrantly inside start().
  std::unordered_map<std::uint64_t, dexvm::VmObjectRef> video_completion;
  std::unordered_set<std::uint64_t> pending_video_completion;
  // VideoView -> OnErrorListener. Registration is real; callbacks are only
  // eligible once the host video path publishes a concrete async error.
  std::unordered_map<std::uint64_t, dexvm::VmObjectRef> video_errors;

  // Real VideoView playback (ADR-0021). The factory is injected by the
  // frontend; when it is missing or open fails, setVideoPath records the
  // gap and start() schedules the deferred-completion fallback.
  video::VideoPlayerFactory video_player_factory;
  struct VideoViewState final {
    std::unique_ptr<video::VideoPlayer> player;
    std::string guest_path;
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
};

void RegisterAndroidAudioTrackStateTable(
    dexvm::Interpreter &vm,
    const std::shared_ptr<DexVmAndroidContext> &context);

void RegisterAndroidValueStateTables(
    dexvm::Interpreter &vm,
    const std::shared_ptr<DexVmAndroidContext> &context);

// Called after Activity base Context attachment, before onCreate. The component
// name is the launch identity (possibly an alias), not necessarily the Java
// class.
void AttachAndroidActivityIdentity(
    dexvm::Interpreter &vm, const std::shared_ptr<DexVmAndroidContext> &context,
    dexvm::VmObjectRef activity, const std::string &component_name);
void RegisterAndroidDatabaseStateTables(
    dexvm::Interpreter &vm,
    const std::shared_ptr<DexVmAndroidContext> &context);
void ReleaseAndroidDatabaseResources(
    const std::shared_ptr<DexVmAndroidContext> &context);

struct UiWidgetDescriptor final {
  std::string_view xml_tag;
  std::string_view dex_descriptor;
  ui::UiClass kind;
};

[[nodiscard]] std::span<const UiWidgetDescriptor> UiWidgetRegistry();
using UiLayoutLoader =
    std::function<std::vector<loader::BinaryXmlElement>(std::uint32_t)>;
[[nodiscard]] std::vector<loader::BinaryXmlElement>
ExpandUiIncludes(std::span<const loader::BinaryXmlElement> elements,
                 const UiLayoutLoader &loader,
                 std::optional<std::uint32_t> root_layout_id = std::nullopt);
void AssignViewContext(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                       dexvm::VmObjectRef view, dexvm::VmObjectRef owner);
[[nodiscard]] dexvm::VmObjectRef
InflateUiElements(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                  std::span<const loader::BinaryXmlElement> elements,
                  dexvm::VmObjectRef inflater_context = dexvm::VmObjectRef{});
[[nodiscard]] dexvm::VmObjectRef InflateUiLayoutResource(
    dexvm::Interpreter &vm, DexVmAndroidContext &context,
    std::uint32_t layout_id,
    dexvm::VmObjectRef inflater_context = dexvm::VmObjectRef{});
[[nodiscard]] std::u16string ResolveUiString(const DexVmAndroidContext &context,
                                             std::uint32_t resource_id);
[[nodiscard]] std::string
ResolveResourceString(const DexVmAndroidContext &context,
                      std::uint32_t resource_id);
[[nodiscard]] std::uint32_t ResolveUiColor(const DexVmAndroidContext &context,
                                           std::uint32_t resource_id);
[[nodiscard]] std::int32_t
ResolveUiDimension(const DexVmAndroidContext &context,
                   std::uint32_t resource_id, bool scaled);
[[nodiscard]] std::shared_ptr<const ui::UiBitmap>
ResolveUiDrawable(DexVmAndroidContext &context, std::uint32_t resource_id);
[[nodiscard]] const loader::ArscEntry &
ResolveUiResourceEntry(const DexVmAndroidContext &context,
                       std::uint32_t resource_id);

// Installs/removes the generic VmExecutionLock blocking observer for the
// lifecycle thread. The observer filters by the registered host thread id;
// a GLThread's own blocking release never changes driver state.
void AttachEglSwapPacer(DexVmAndroidContext &context,
                        dexvm::VmExecutionLock &execution_lock);
void DetachEglSwapPacer(DexVmAndroidContext &context,
                        dexvm::VmExecutionLock &execution_lock);
void AdvanceEglSwapPacer(DexVmAndroidContext &context);
void ShutdownEglSwapPacer(DexVmAndroidContext &context);
struct EglSwapPacerSnapshot final {
  bool attached{};
  bool driver_blocked{};
  bool shutdown{};
  bool surface_retired{};
  std::uint64_t generation{};
};
[[nodiscard]] std::optional<EglSwapPacerSnapshot>
TryEglSwapPacerSnapshot(DexVmAndroidContext &context);
// Permanently invalidates the Java EGL facade and releases any guest swap
// pacer wait. Used only when process teardown begins.
void RetireGuestEglSurface(DexVmAndroidContext &context);
void PaceEglSwap(DexVmAndroidContext &context,
                 dexvm::VmExecutionLock &execution_lock);

// Advances every playing VideoView to the shared uptime clock: publishes new
// frames through publish (letterboxed to the surface size) and fires the
// registered onCompletion exactly once per playback at end of stream.
// Returns a rendered message when a guest callback raised.
[[nodiscard]] std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter &vm, DexVmAndroidContext &context,
    const std::function<void(std::vector<std::uint8_t> rgba8)> &publish);

// Delivers AudioTrack marker/period callbacks from the lifecycle frame thread
// according to the shared PCM mixer's real playback head. Guest callbacks
// never run on the host audio thread.
[[nodiscard]] std::optional<std::string>
PumpAndroidAudioTracks(dexvm::Interpreter &vm, DexVmAndroidContext &context);

// True while at least one VideoView is actively playing decoded video. The
// frontend uses this to pace the free-running frame loop to real time so
// the deterministic per-frame uptime clock matches the wall clock during
// playback (manual stepping stays unpaced and reproducible).
[[nodiscard]] bool AnyVideoPlaying(const DexVmAndroidContext &context);

// Mixes decoded audio of every playing VideoView into the interleaved
// stereo S16 buffer (saturating add on top of the existing content), pulling
// PCM from each player's audio cursor with deterministic nearest-neighbour
// resampling to output_rate. Mono duplicates to both channels, wider
// layouts take the first two. Returns the number of views that contributed.
// Paused, stopped and completed views contribute silence.
[[nodiscard]] std::size_t
MixVideoPcmIntoStereo(DexVmAndroidContext &context,
                      std::span<std::int16_t> interleaved_stereo,
                      std::uint32_t output_rate);

// Widget click dispatch uses the UiTree's resolved geometry and refreshes a
// dirty traversal before hit-test. The full gesture contract lands in LUI-9.
[[nodiscard]] std::optional<std::uint64_t>
FindClickableViewAt(DexVmAndroidContext &context, float x, float y);

// Visible, enabled, attached View receivers under a point in Android touch
// traversal order: reverse child Z, deepest first, then the parent.
[[nodiscard]] std::vector<std::uint64_t>
FindTouchReceiversAt(DexVmAndroidContext &context, float x, float y);

// DexVM/View binding helpers. BindViewToUiNode enforces the one-to-one live
// identity invariant; EnsureViewUiNode creates a detached generic node for a
// Java-created View until ViewGroup/content attachment is implemented.
void BindViewToUiNode(DexVmAndroidContext &context, dexvm::VmObjectRef view,
                      ui::UiNodeId node);
[[nodiscard]] ui::UiNodeId EnsureViewUiNode(DexVmAndroidContext &context,
                                            dexvm::VmObjectRef view,
                                            ui::UiClass kind);
[[nodiscard]] std::optional<ui::UiNodeId>
FindViewUiNode(const DexVmAndroidContext &context, std::uint64_t view_handle);
[[nodiscard]] dexvm::VmObjectRef
ViewObjectForUiNode(const DexVmAndroidContext &context, ui::UiNodeId node);
void ResetViewUiState(DexVmAndroidContext &context);
void InitializeDefaultViewBackground(dexvm::Interpreter &vm,
                                     DexVmAndroidContext &context,
                                     dexvm::VmObjectRef view,
                                     ui::UiNodeId node);

// Applies one filtered text replacement and synchronously delivers the API 19
// TextWatcher transaction on the guest thread.
namespace android_intrinsics {
bool ApplyTextEdit(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                   dexvm::VmObjectRef view, std::int32_t start,
                   std::int32_t before_count,
                   const std::u16string &replacement);
} // namespace android_intrinsics

// True when the view's derived bounds contain the point (up-inside check of
// a click gesture).
[[nodiscard]] bool ViewContainsPoint(DexVmAndroidContext &context,
                                     std::uint64_t handle, float x, float y);

struct ViewTouchResult final {
  bool handled{};
  std::optional<std::string> error;
};
[[nodiscard]] ViewTouchResult
InvokeViewOnTouch(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                  std::uint64_t handle, std::int32_t action, float x, float y);

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
[[nodiscard]] ViewGestureDispatchResult
DispatchViewGestureEvent(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                         std::uint64_t handle, std::int32_t action, float x,
                         float y, bool click_eligible, bool touch_consumed);

// Invokes the registered OnClickListener.onClick(view) on the guest thread.
// Returns a rendered message when the guest callback raised.
[[nodiscard]] std::optional<std::string>
InvokeViewOnClick(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                  std::uint64_t handle);

// SurfaceHolder.Callback delivery for guest-implemented SurfaceViews. A
// title that brings its own GLSurfaceView registers itself on the holder in
// its constructor and then waits for these callbacks before it will touch
// EGL, so the managed surface lifecycle has to deliver them. There is one
// managed surface, so every registered holder callback gets the same event.
// Returns a rendered message when a guest callback raised.
enum class SurfaceHolderPhase : std::uint8_t { created, changed, destroyed };

[[nodiscard]] std::optional<std::string>
DispatchSurfaceHolderCallbacks(dexvm::Interpreter &vm,
                               DexVmAndroidContext &context,
                               SurfaceHolderPhase phase);

// Mirrors ViewGroup's conditional dispatchAttachedToWindow/
// dispatchDetachedFromWindow for a subtree that has just become attached to,
// or is about to leave, the live UiTree. Only SurfaceViews that already own a
// holder participate; callbacks are delivered once per holder generation.
[[nodiscard]] std::optional<std::string>
AttachSurfaceViewSubtree(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                         ui::UiNodeId subtree);
[[nodiscard]] std::optional<std::string>
DetachSurfaceViewSubtree(dexvm::Interpreter &vm, DexVmAndroidContext &context,
                         ui::UiNodeId subtree);

// Delivers surfaceDestroyed to the active holder generation, then forgets
// its holders/callbacks so a replacement Activity starts a fresh generation.
[[nodiscard]] std::optional<std::string>
RetireSurfaceHolderGeneration(dexvm::Interpreter &vm,
                              DexVmAndroidContext &context);

// Advances the only Android monotonic Clock and wakes scheduler waiters.
void AdvanceAndroidClock(DexVmAndroidContext &context,
                         std::int64_t delta_millis);

// Stops all loopers and wakes HandlerThread teardown. Idempotent.
void ShutdownAndroidScheduler(DexVmAndroidContext &context);

// Frame-boundary service for the main Looper and Java threads. Runs all due
// main work in (deadline, sequence) order and drains uncaught thread failure.
[[nodiscard]] std::optional<std::string>
PumpJavaThreads(dexvm::Interpreter &vm, DexVmAndroidContext &context);

void RegisterAndroidSchedulerStateTable(
    dexvm::Interpreter &vm,
    const std::shared_ptr<DexVmAndroidContext> &context);
void RegisterAndroidOwnerAttachedStateTable(
    dexvm::Interpreter &vm,
    const std::shared_ptr<DexVmAndroidContext> &context);
void VisitAndroidSessionRoots(const DexVmAndroidContext &context,
                              const dexvm::VmRootVisitor &visit);

// True when the guest asked for the session to end: System.exit(), or the
// activity that currently owns the screen finished itself with no successor
// on the way in. A finish() aimed at any other activity handle is a retired
// or departing activity and never ends the session.
[[nodiscard]] bool SessionExitRequested(const DexVmAndroidContext &context);

[[nodiscard]] dexvm::CoreIntrinsicServices AndroidCoreIntrinsicServices(
    const std::shared_ptr<DexVmAndroidContext> &context);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl>
AndroidIntrinsicCatalog(const std::shared_ptr<DexVmAndroidContext> &context);

void SetAndroidKeyEventUnicode(dexvm::Interpreter &vm, dexvm::VmObjectRef event,
                               std::int32_t unicode_char);

// Builds a MotionEvent intrinsic instance for input dispatch.
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter &vm,
                                                 std::int32_t action, float x,
                                                 float y, std::int32_t pointer);

} // namespace ogplay::runtime
