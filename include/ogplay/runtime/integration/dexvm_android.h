#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime {

class AndroidGuestCallSession;

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
    std::string device_id{"000000000000000"};
    std::string device_software_version{"00"};
    std::string line_number;
    std::string network_operator{"00000"};

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
    std::unordered_map<std::string, std::vector<std::byte>> memory_files;

    // Cached service/singleton intrinsic instances by handler-defined key.
    std::unordered_map<std::string, dexvm::VmObjectRef> singletons;

    // SoundPool stream id -> (resource id) mapping for voice controls.
    std::unordered_map<std::int32_t, std::int32_t> sound_streams;
    std::int32_t next_sound_stream{1};

    // MediaPlayer playing flags by instance handle.
    std::unordered_map<std::uint32_t, std::int32_t> media_resources;
    std::unordered_map<std::uint32_t, bool> media_playing;
};

[[nodiscard]] std::vector<dexvm::IntrinsicClassDecl> AndroidIntrinsicCatalog();

void RegisterAndroidBuiltins(dexvm::IntrinsicRegistry& registry,
                             std::shared_ptr<DexVmAndroidContext> context);

// Builds a MotionEvent intrinsic instance for input dispatch.
[[nodiscard]] dexvm::VmObjectRef MakeMotionEvent(dexvm::Interpreter& vm,
                                                 std::int32_t action,
                                                 float x, float y,
                                                 std::int32_t pointer);

}  // namespace ogplay::runtime
