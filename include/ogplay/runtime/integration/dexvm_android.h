#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
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

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/framework/preferences_xml.h"
#include "ogplay/runtime/ui/ui_tree.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/video/video_player.h"

namespace ogplay::runtime {

class AndroidGuestCallSession;
class NativeLibraryLoader;
class VirtualFileSystem;

// android.* intrinsic surface for the dex_activity lifecycle
// (docs/design/dexvm/03-platform-intrinsics.md §4). The catalog is a
// code-defined immutable list; handlers bind to the running guest session
// (sound mixer, VFS, platform identity) through this shared context.

struct DexVmAndroidContext final {
    AndroidGuestCallSession* session{};
    // Process-wide APK native loader used by java.lang.System.load*.
    // The application ClassLoader has one stable non-zero identity for the
    // lifetime of this context; APS-5 intentionally does not invent a second
    // loader namespace.
    NativeLibraryLoader* native_libraries{};
    std::uint64_t application_class_loader_token{1U};
    loader::ArscTable arsc;
    std::vector<std::byte> apk_bytes;
    loader::ApkArchive archive;
    std::string package_name;
    std::uint32_t surface_width{};
    std::uint32_t surface_height{};
    // API19 UI fallback until a title exposes reliable DisplayMetrics.
    float ui_density{1.0F};
    float ui_scaled_density{1.0F};
    std::int32_t api_level{19};
    std::string iso3_language{"eng"};
    std::string iso3_country{"USA"};
    std::string iso_country{"US"};
    std::string device_id{"000000000000000"};
    std::string device_software_version{"00"};
    std::string line_number;
    std::string network_operator{"00000"};

    // External storage facts: the guest-visible root and the real free
    // space of the backing host volume (filled by the frontend; 0 when no
    // external mount exists, which StatFs then truthfully reports).
    std::string external_storage_root{"/sdcard"};
    std::uint64_t external_free_bytes{};

    // Deterministic time published by the lifecycle driver (unified Clock).
    std::atomic<std::int64_t> uptime_millis{0};
    // System.exit(): the guest asked for the process, not an activity.
    std::atomic<bool> exit_requested{false};
    // Activity.finish() retires one activity. An installer shell finishing
    // itself right after startActivity is a handoff, and its run() keeps
    // executing on its own host thread, so the request is recorded per
    // activity handle: a finish() from an already retired activity can no
    // longer be mistaken for the session ending. Ask SessionExitRequested.
    std::atomic<std::uint32_t> finishing_activity{0};
    // Raised with pending_activity_descriptor so the exit predicate can see
    // an in-flight handoff without reading that string across threads.
    std::atomic<bool> activity_switch_pending{false};

    // Captured lifecycle facts.
    dexvm::VmObjectRef activity;
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
    dexvm::VmObjectRef content_view;

    // Host-side stream table for InputStream-backed intrinsics.
    struct Stream final {
        std::vector<std::byte> bytes;
        std::size_t cursor{};
        bool closed{};
    };
    std::unordered_map<std::uint32_t, Stream> streams;
    struct OutputStream final {
        std::string path;
        std::vector<std::byte> bytes;
        bool closed{};
    };
    std::unordered_map<std::uint32_t, OutputStream> output_streams;

    // ZipInputStream state: the adopted source bytes go through the strict
    // ZIP reader once; entries inflate one at a time as the title walks
    // getNextEntry/read.
    struct ZipStream final {
        std::vector<std::byte> raw;
        loader::ApkArchive archive;
        std::size_t next_entry{};
        std::vector<std::byte> entry_bytes;
        std::size_t cursor{};
        bool entry_open{};
        bool closed{};
    };
    std::unordered_map<std::uint32_t, ZipStream> zip_streams;

    // Shared guest filesystem: the single world view for Java File I/O and
    // native fopen alike (external mounts, APK assets, and the per-title
    // save sandbox when one is attached). ADR-0020 retired the separate
    // session-memory overlay that used to shadow it.
    VirtualFileSystem* vfs{};

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

  // Bundle values retain their Java kind and object identity. This store
  // is shared by interpreted and native JNI callers through the DexVM
  // intrinsic bridge.
  using BundleValue =
      std::variant<std::int32_t, std::int64_t, std::string, dexvm::VmObjectRef>;
  std::unordered_map<std::uint32_t,
                     std::unordered_map<std::string, BundleValue>>
      bundles;

    // Editable instance handle -> owning EditText handle; the text itself
    // lives in the interpreter's builder buffer of the owner.
    std::unordered_map<std::uint32_t, std::uint32_t> editable_owner;

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

    struct EglFacadeState final {
        dexvm::VmObjectRef display;
        dexvm::VmObjectRef config;
        dexvm::VmObjectRef no_display;
        dexvm::VmObjectRef no_context;
        dexvm::VmObjectRef no_surface;
        dexvm::VmObjectRef window_surface;
        std::unordered_map<std::uint32_t, std::int32_t> contexts;
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

    // Bitmap pixel stores by instance handle: real host-side ARGB8888
    // buffers so getPixels/createBitmap round-trip actual data.
    struct BitmapState final {
        std::int32_t width{};
        std::int32_t height{};
        std::vector<std::uint32_t> argb;
        bool recycled{};
    };
    std::unordered_map<std::uint32_t, BitmapState> bitmaps;

    // MediaPlayer playing flags by instance handle.
    std::unordered_map<std::uint32_t, std::int32_t> media_resources;
    std::unordered_map<std::uint32_t, bool> media_playing;
    std::unordered_map<std::uint32_t, bool> media_looping;

    // Cooperative java.util.Timer task state. java.lang.Thread itself is a
    // dexvm core intrinsic and keeps Java-visible facts in declared fields;
    // this legacy-shaped record is only used by the frame-boundary Timer pump.
    struct JavaThreadState final {
    dexvm::VmObjectRef runnable{};
        bool started{};
        bool finished{};
        // Timer does not schedule by priority; retained only as neutral state
        // for the bounded cooperative record.
        std::int32_t priority{5};
    // Diagnostic name for the queued TimerTask.
    std::string name;
    };
    std::unordered_map<std::uint32_t, JavaThreadState> java_threads;
    // java.util.Timer tasks stay cooperative: schedule() queues the task and
    // the delay collapses to the next lifecycle frame boundary, so timers
    // remain deterministic and never outlive the frame loop.
    std::vector<dexvm::VmObjectRef> java_thread_queue;
    // Owned by the DexVm bridge; set once the interpreter exists.
    dexvm::VmThreadRuntime* threads{};

    // Explicit intent component targets (class descriptors) and the pending
    // activity switch consumed by the dex_activity lifecycle.
    std::unordered_map<std::uint32_t, std::string> intent_components;
    std::unordered_map<std::uint32_t,
                       std::unordered_map<std::string, std::string>>
        intent_string_extras;
    std::unordered_map<std::uint32_t,
                       std::unordered_map<std::string, std::int32_t>>
        intent_int_extras;
    std::string pending_activity_descriptor;
    // Intent that launched the current/pending activity (getIntent()).
    dexvm::VmObjectRef current_intent;
    // One live guest View object <-> one UiTree node. Runtime UI owns all
    // hierarchy/state/geometry; this integration layer alone owns guest refs
    // and callbacks keyed by UiNodeId.
    ui::UiTree ui_tree;
    ui::UiBitmapCache ui_bitmaps;
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
    std::unordered_map<std::uint32_t, ui::LayoutParams> ui_layout_params;
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef>
        ui_view_layout_params;
    std::unordered_map<std::uint32_t, ui::ImageScaleType>
        ui_image_scale_types;
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
};

struct UiWidgetDescriptor final {
    std::string_view xml_tag;
    std::string_view dex_descriptor;
    ui::UiClass kind;
};

[[nodiscard]] std::span<const UiWidgetDescriptor> UiWidgetRegistry();
using UiLayoutLoader = std::function<std::vector<loader::BinaryXmlElement>(
    std::uint32_t)>;
[[nodiscard]] std::vector<loader::BinaryXmlElement> ExpandUiIncludes(
    std::span<const loader::BinaryXmlElement> elements,
    const UiLayoutLoader& loader,
    std::optional<std::uint32_t> root_layout_id = std::nullopt);
[[nodiscard]] dexvm::VmObjectRef InflateUiElements(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::span<const loader::BinaryXmlElement> elements);
[[nodiscard]] dexvm::VmObjectRef InflateUiLayoutResource(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::uint32_t layout_id);
[[nodiscard]] std::u16string ResolveUiString(
    const DexVmAndroidContext& context, std::uint32_t resource_id);
[[nodiscard]] std::uint32_t ResolveUiColor(
    const DexVmAndroidContext& context, std::uint32_t resource_id);
[[nodiscard]] std::int32_t ResolveUiDimension(
    const DexVmAndroidContext& context, std::uint32_t resource_id,
    bool scaled);
[[nodiscard]] std::shared_ptr<const ui::UiBitmap> ResolveUiDrawable(
    DexVmAndroidContext& context, std::uint32_t resource_id);

// Installs/removes the generic VmExecutionLock blocking observer for the
// lifecycle thread. The observer filters by the registered host thread id;
// a GLThread's own blocking release never changes driver state.
void AttachEglSwapPacer(DexVmAndroidContext& context,
                        dexvm::VmExecutionLock& execution_lock);
void DetachEglSwapPacer(DexVmAndroidContext& context,
                        dexvm::VmExecutionLock& execution_lock);
void AdvanceEglSwapPacer(DexVmAndroidContext& context);
void ShutdownEglSwapPacer(DexVmAndroidContext& context);
void PaceEglSwap(DexVmAndroidContext& context,
                 dexvm::VmExecutionLock& execution_lock);

// Advances every playing VideoView to the shared uptime clock: publishes new
// frames through publish (letterboxed to the surface size) and fires the
// registered onCompletion exactly once per playback at end of stream.
// Returns a rendered message when a guest callback raised.
[[nodiscard]] std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish);

// True while at least one VideoView is actively playing decoded video. The
// frontend uses this to pace the free-running frame loop to real time so
// the deterministic per-frame uptime clock matches the wall clock during
// playback (manual stepping stays unpaced and reproducible).
[[nodiscard]] bool AnyVideoPlaying(const DexVmAndroidContext& context);

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

// DexVM/View binding helpers. BindViewToUiNode enforces the one-to-one live
// identity invariant; EnsureViewUiNode creates a detached generic node for a
// Java-created View until ViewGroup/content attachment is implemented.
void BindViewToUiNode(DexVmAndroidContext& context,
                      dexvm::VmObjectRef view, ui::UiNodeId node);
[[nodiscard]] ui::UiNodeId EnsureViewUiNode(DexVmAndroidContext& context,
                                            dexvm::VmObjectRef view,
                                            ui::UiClass kind);
[[nodiscard]] std::optional<ui::UiNodeId> FindViewUiNode(
    const DexVmAndroidContext& context, std::uint64_t view_handle);
[[nodiscard]] dexvm::VmObjectRef ViewObjectForUiNode(
    const DexVmAndroidContext& context, ui::UiNodeId node);
void ResetViewUiState(DexVmAndroidContext& context);

// True when the view's derived bounds contain the point (up-inside check of
// a click gesture).
[[nodiscard]] bool ViewContainsPoint(DexVmAndroidContext& context,
                                     std::uint64_t handle, float x, float y);

struct ViewTouchResult final {
    bool handled{};
    std::optional<std::string> error;
};
[[nodiscard]] ViewTouchResult InvokeViewOnTouch(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
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
// a click listener may retain capture without consuming OnTouchListener.
[[nodiscard]] ViewGestureDispatchResult DispatchViewGestureEvent(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::uint64_t handle, std::int32_t action, float x, float y,
    bool click_eligible, bool touch_consumed);

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

[[nodiscard]] std::optional<std::string> DispatchSurfaceHolderCallbacks(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    SurfaceHolderPhase phase);

// Delivers surfaceDestroyed to the active holder generation, then forgets
// its holders/callbacks so a replacement Activity starts a fresh generation.
[[nodiscard]] std::optional<std::string> RetireSurfaceHolderGeneration(
    dexvm::Interpreter& vm, DexVmAndroidContext& context);

// Frame-boundary service for Java threads: runs queued cooperative Timer
// tasks on the calling (VM host) thread and drains any uncaught failure the
// real host threads recorded. Returns a rendered message when a thread died,
// matching the process-fatal default handler on device.
[[nodiscard]] std::optional<std::string>
PumpJavaThreads(dexvm::Interpreter &vm, DexVmAndroidContext &context);

// True when the guest asked for the session to end: System.exit(), or the
// activity that currently owns the screen finished itself with no successor
// on the way in. A finish() aimed at any other activity handle is a retired
// or departing activity and never ends the session.
[[nodiscard]] bool SessionExitRequested(const DexVmAndroidContext& context);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog(
    const std::shared_ptr<DexVmAndroidContext>& context);

// Builds a MotionEvent intrinsic instance for input dispatch.
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm,
                                                 std::int32_t action, float x,
                                                 float y, std::int32_t pointer);

}  // namespace ogplay::runtime
