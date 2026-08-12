#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/runtime/dexvm/interpreter.h"
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

    // Session-lifetime in-memory files (v1 storage semantics: writes are
    // visible within the session; cross-session persistence is not claimed).
    // Overlays the shared guest VFS below: Java-side writes land here first
    // and shadow same-path VFS entries on read.
    std::unordered_map<std::string, std::vector<std::byte>> memory_files;

    // Shared guest filesystem (same world view as native fopen): external
    // mounts, APK assets, runtime files. Optional; memory_files still works
    // standalone in unit tests.
    VirtualFileSystem* vfs{};

    // SharedPreferences stores (v1 storage semantics like memory_files:
    // session-lifetime, immediately visible, no cross-session persistence
    // claim). Values keep their Java type so mismatched getters can throw
    // the real ClassCastException.
    using PreferenceValue =
        std::variant<bool, std::int32_t, std::int64_t, std::string>;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, PreferenceValue>>
        preferences;
    // SharedPreferences/Editor instance handle -> preference file name.
    std::unordered_map<std::uint32_t, std::string> preference_names;

    // Editable instance handle -> owning EditText handle; the text itself
    // lives in the interpreter's builder buffer of the owner.
    std::unordered_map<std::uint32_t, std::uint32_t> editable_owner;

    // Cached service/singleton intrinsic instances by handler-defined key.
    std::unordered_map<std::string, dexvm::VmObjectRef> singletons;

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

    // Cooperative Java threads (interim form of 04 §3): start() queues the
    // thread, execution happens on the single VM host thread at lifecycle
    // frame boundaries, or synchronously at join(). A run() body that never
    // terminates exhausts the tick budget and fails explicitly; nothing
    // pretends to be concurrent.
    struct JavaThreadState final {
        dexvm::VmObjectRef runnable;
        bool started{};
        bool finished{};
    };
    std::unordered_map<std::uint32_t, JavaThreadState> java_threads;
    std::vector<dexvm::VmObjectRef> java_thread_queue;

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

// Widget click dispatch (device semantics for the layout subset the bounds
// derivation supports). FindClickableViewAt answers the topmost visible view
// with a registered OnClickListener whose derived bounds contain the point;
// views without derivable bounds never match (recorded gap, the touch falls
// through to Activity.onTouchEvent).
[[nodiscard]] std::optional<std::uint64_t> FindClickableViewAt(
    const DexVmAndroidContext& context, float x, float y);

// True when the view's derived bounds contain the point (up-inside check of
// a click gesture).
[[nodiscard]] bool ViewContainsPoint(const DexVmAndroidContext& context,
                                     std::uint64_t handle, float x, float y);

// Invokes the registered OnClickListener.onClick(view) on the guest thread.
// Returns a rendered message when the guest callback raised.
[[nodiscard]] std::optional<std::string> InvokeViewOnClick(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    std::uint64_t handle);

// Runs every queued cooperative Java thread to completion on the calling
// (VM host) thread. Returns a rendered uncaught-exception message when a
// thread died, matching the process-fatal default handler on device.
[[nodiscard]] std::optional<std::string> PumpJavaThreads(
    dexvm::Interpreter& vm, DexVmAndroidContext& context);

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog();

void RegisterAndroidBuiltins(dexvm::IntrinsicRegistry& registry,
                             std::shared_ptr<DexVmAndroidContext> context);

// Builds a MotionEvent intrinsic instance for input dispatch.
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm,
                                                 std::int32_t action,
                                                 float x, float y,
                                                 std::int32_t pointer);

}  // namespace ogplay::runtime
