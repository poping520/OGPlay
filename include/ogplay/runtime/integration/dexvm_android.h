#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/framework/preferences_xml.h"
#include "ogplay/video/video_player.h"

namespace ogplay::runtime {

class AndroidGuestCallSession;
class VirtualFileSystem;

// android.* intrinsic surface for the dex_activity lifecycle
// (docs/design/dexvm/03-platform-intrinsics.md §4). The catalog is a
// code-defined immutable list; handlers bind to the running guest session
// (sound mixer, VFS, platform identity) through this shared context.

struct DexVmAndroidContext final {
    AndroidGuestCallSession* session{};
    loader::ArscTable arsc;
    std::vector<std::byte> apk_bytes;
    loader::ApkArchive archive;
    std::string package_name;
    std::uint32_t surface_width{};
    std::uint32_t surface_height{};
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
    std::atomic<bool> exit_requested{false};

    // Captured lifecycle facts.
    dexvm::VmObjectRef activity;
    dexvm::VmObjectRef renderer;
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

    // SurfaceView owns one stable SurfaceHolder. addCallback appends, as on
    // the platform: these titles register both the view and the activity on
    // the same holder and both have to receive the surface lifecycle.
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> surface_holders;
    std::unordered_map<std::uint32_t, std::vector<dexvm::VmObjectRef>>
        surface_callbacks;

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

    // Java threads (04 §3). Thread.start() hands the target to the DexVM
    // thread runtime, which gives it a real host thread and its own
    // execution context; this table only keeps the guest-visible Thread
    // facts (target, name, priority) that the intrinsics answer from.
    struct JavaThreadState final {
    dexvm::VmObjectRef runnable{};
        bool started{};
        bool finished{};
        // java.lang.Thread priority (MIN_PRIORITY=1, NORM_PRIORITY=5,
        // MAX_PRIORITY=10). Recorded as a guest fact; host scheduler
        // priority is deliberately not claimed.
        std::int32_t priority{5};
    // Stable deterministic identity and guest-visible name. The id is
    // derived from the VM object handle and is never reused in-session.
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
    // Views inflated from the last setContentView(layout id), keyed by
    // android:id resource id (findViewById source of truth).
    std::unordered_map<std::uint32_t, dexvm::VmObjectRef> view_registry;
    // Per-view interaction state (real storage behind setOnClickListener /
    // setVisibility; keyed by intrinsic instance handle). Android constants:
    // VISIBLE=0, INVISIBLE=4, GONE=8.
    struct WidgetState final {
        dexvm::VmObjectRef click_listener;
        std::int32_t visibility{0};
    };
    std::unordered_map<std::uint64_t, WidgetState> widget_states;
    // Layout facts captured at inflation (document order, parents before
    // children) that click hit-testing derives bounds from. measured_* is
    // the wrap_content size taken from the android:src drawable (0 when
    // unknown). Bounds outside the supported derivation subset stay
    // unresolved and touches fall through to Activity.onTouchEvent.
    struct LayoutViewFact final {
        dexvm::VmObjectRef view;
        std::int32_t parent{-1};  // index into layout_views, -1 for roots
        std::string tag;
        std::int32_t layout_width{};
        std::int32_t layout_height{};
        std::uint32_t gravity{};
        std::uint32_t layout_gravity{};
        std::int32_t padding_top{};
        std::int32_t measured_width{};
        std::int32_t measured_height{};
    };
    std::vector<LayoutViewFact> layout_views;
    // VideoView -> OnCompletionListener. With a player attached the guest
    // video pump fires it once at end of stream; without one start() still
    // completes synchronously (honest fallback).
    std::unordered_map<std::uint64_t, dexvm::VmObjectRef> video_completion;

    // Real VideoView playback (ADR-0021). The factory is injected by the
    // frontend; when it is missing or open fails, setVideoPath records the
    // gap and start() keeps the immediate-completion fallback.
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

// Widget click dispatch (device semantics for the layout subset the bounds
// derivation supports). FindClickableViewAt answers the topmost visible view
// with a registered OnClickListener whose derived bounds contain the point;
// views without derivable bounds never match (recorded gap, the touch falls
// through to Activity.onTouchEvent).
[[nodiscard]] std::optional<std::uint64_t>
FindClickableViewAt(const DexVmAndroidContext &context, float x, float y);

// True when the view's derived bounds contain the point (up-inside check of
// a click gesture).
[[nodiscard]] bool ViewContainsPoint(const DexVmAndroidContext& context,
                                     std::uint64_t handle, float x, float y);

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

// Frame-boundary service for Java threads: runs queued cooperative Timer
// tasks on the calling (VM host) thread and drains any uncaught failure the
// real host threads recorded. Returns a rendered message when a thread died,
// matching the process-fatal default handler on device.
[[nodiscard]] std::optional<std::string>
PumpJavaThreads(dexvm::Interpreter &vm, DexVmAndroidContext &context);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog(
    const std::shared_ptr<DexVmAndroidContext>& context);

// Builds a MotionEvent intrinsic instance for input dispatch.
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm,
                                                 std::int32_t action, float x,
                                                 float y, std::int32_t pointer);

}  // namespace ogplay::runtime
