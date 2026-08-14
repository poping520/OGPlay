#include "run_apk.h"
#include "run_apk_vfs.h"
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/agent/mcp_session_control.h"
#include "ogplay/core/logger.h"
#include "ogplay/frontend/mcp_http_server.h"
#include "ogplay/frontend/mcp_input_dispatch.h"
#include "ogplay/frontend/data_directory.h"
#include "ogplay/frontend/run_apk_progress.h"
#include "ogplay/hal/audio.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/window_input.h"
#include "ogplay/input/mouse_touch_mapper.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/android_link_preflight.h"
#include "ogplay/session/profile_apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/runtime/dexvm/gap_survey.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/session/profile_entry_scope.h"
#include "ogplay/session/profile_vfs.h"
#include "ogplay/video/ffmpeg_video_player.h"
#include "ogplay/session/quirk_registry.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::frontend {
namespace {

constexpr std::uint16_t kDefaultMcpPort = 15971U;

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};

void Write(const std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stdout));
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("input file is empty: " + path.string());
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(result.data()), size);
    if (!input) throw std::runtime_error("input file was truncated: " + path.string());
    return result;
}

struct OwnedSystemLibrary final {
    std::string name;
    std::vector<std::byte> image;
};

std::vector<OwnedSystemLibrary> ReadSystemLibraries(
    const std::filesystem::path& directory,
    const runtime::BionicProfile& profile) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        throw std::runtime_error("Bionic system library directory is unavailable: " +
                                 directory.string());
    }
    std::vector<OwnedSystemLibrary> result;
    for (const auto name : profile.guest_libraries) {
        const auto path = directory / name;
        if (std::filesystem::is_regular_file(path, error) && !error) {
            result.push_back({std::string(name), ReadBytes(path)});
        } else {
            error.clear();
        }
    }
    return result;
}

std::uint64_t ParsePositive(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return value;
}

std::uint32_t ParseSupersampleFactor(const std::string_view text) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value < 1 || value > 4) {
        throw std::invalid_argument("--supersample requires an integer in 1..4");
    }
    return value;
}

std::uint16_t ParseMcpPort(const std::string_view text) {
    std::uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value == 0 || value > 65535U) {
        throw std::invalid_argument("--mcp-port requires an integer in 1..65535");
    }
    return static_cast<std::uint16_t>(value);
}

std::string FrameRateTitle(const std::string_view base, const double fps) {
    std::array<char, 32> text{};
    const auto result = std::to_chars(
        text.data(), text.data() + text.size(), fps,
        std::chars_format::fixed, 1);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("cannot format window frame rate");
    }
    return std::string{base} + " · FPS " +
           std::string{text.data(), result.ptr};
}

gles::AngleBackend NativeBackend() {
    constexpr std::string_view renderer{OGPLAY_NATIVE_ANGLE_RENDERER};
    if (renderer == "d3d11") {
        return {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
    }
    if (renderer == "metal") {
        return {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
    }
    if (renderer == "vulkan") {
        return {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
    }
    throw std::logic_error("unknown configured ANGLE renderer");
}

[[nodiscard]] std::optional<runtime::AndroidBoundaryInput> MapInput(
    const hal::InputEvent& event) {
    std::optional<runtime::AndroidBoundaryInputType> type;
    if (event.type == hal::InputEventType::key) type = runtime::AndroidBoundaryInputType::key;
    if (event.type == hal::InputEventType::pointer_motion) {
        type = runtime::AndroidBoundaryInputType::pointer_motion;
    }
    if (event.type == hal::InputEventType::pointer_button) {
        type = runtime::AndroidBoundaryInputType::pointer_button;
    }
    if (type.has_value()) {
        return runtime::AndroidBoundaryInput{
            *type, event.code, event.x, event.y, event.pressed};
    }
    return std::nullopt;
}

[[nodiscard]] bool ProfileEnablesQuirk(
    const session::TitleProfile& profile, const std::string_view id) {
    return profile.quirks.has_value() &&
           std::ranges::find(profile.quirks->enabled, id) !=
               profile.quirks->enabled.end();
}

void PumpAudio(runtime::AndroidGuestCallSession& guest,
               hal::AudioOutput& output,
               std::vector<std::int16_t>& samples,
               runtime::DexVmAndroidContext* dex_context) {
    constexpr std::uint32_t kSampleRate = 48000U;
    constexpr std::uint64_t kTargetQueuedFrames = 4096U;
    constexpr std::size_t kMaximumChunksPerPump = 4U;
    for (std::size_t chunk = 0;
         chunk < kMaximumChunksPerPump &&
         output.QueuedFrames() < kTargetQueuedFrames;
         ++chunk) {
        auto frames = guest.RenderStereoAudio(samples, kSampleRate);
        if (dex_context != nullptr && !dex_context->video_views.empty()) {
            // The mixer zero-fills the whole buffer, so decoded VideoView
            // audio mixes over the full chunk even when no sound is queued.
            frames = samples.size() / 2U;
            static_cast<void>(runtime::MixVideoPcmIntoStereo(
                *dex_context, samples, kSampleRate));
        }
        const auto sample_count = frames * 2U;
        output.Submit(std::as_bytes(
            std::span{samples}.first(sample_count)));
    }
}

std::string Utf16ToUtf8(const std::span<const runtime::JniChar> text) {
    std::string result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        std::uint32_t code_point = text[index];
        if (code_point >= 0xd800U && code_point <= 0xdbffU) {
            if (index + 1U < text.size() && text[index + 1U] >= 0xdc00U &&
                text[index + 1U] <= 0xdfffU) {
                ++index;
                code_point = 0x10000U + ((code_point - 0xd800U) << 10U) +
                             (text[index] - 0xdc00U);
            } else {
                code_point = 0xfffdU;
            }
        } else if (code_point >= 0xdc00U && code_point <= 0xdfffU) {
            code_point = 0xfffdU;
        }
        if (code_point <= 0x7fU) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ffU) {
            result.push_back(static_cast<char>(0xc0U | code_point >> 6U));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else if (code_point <= 0xffffU) {
            result.push_back(static_cast<char>(0xe0U | code_point >> 12U));
            result.push_back(static_cast<char>(0x80U | (code_point >> 6U & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else {
            result.push_back(static_cast<char>(0xf0U | code_point >> 18U));
            result.push_back(static_cast<char>(0x80U | (code_point >> 12U & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (code_point >> 6U & 0x3fU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
    }
    return result;
}

template <typename Guest>
void PublishPresentedFrame(agent::FrameSnapshotStore* frames,
                           runtime::AndroidBoundaryFrame frame,
                           Guest& guest) {
    if (frames == nullptr) {
        guest.RecycleFrame(std::move(frame));
        return;
    }
    auto previous = frames->Publish(
        {frame.width, frame.height, frame.sequence, std::move(frame.rgba8)});
    if (previous.has_value()) {
        guest.RecycleFrame(
            {previous->width, previous->height, previous->sequence,
             std::move(previous->rgba8)});
    }
}

template <typename Guest>
void ReleaseCapturedFrame(agent::FrameSnapshotStore* frames, Guest& guest) {
    if (frames == nullptr) return;
    if (auto frame = frames->Take(); frame.has_value()) {
        guest.RecycleFrame(
            {frame->width, frame->height, frame->sequence, std::move(frame->rgba8)});
    }
}

}  // namespace

int RunApkCommand(const int argc, const char* const argv[],
                  core::Logger& logger) {
    if (argc < 5) {
        throw std::invalid_argument(
            "run-apk requires <apk> --system-dir <api19-lib-dir>");
    }
    const std::filesystem::path apk_path{argv[2]};
    std::optional<std::filesystem::path> system_directory;
    std::optional<std::filesystem::path> external_directory;
    const auto bundled_data = HostBundledDataPaths();
    auto profiles_directory = bundled_data.profiles_directory;
    std::optional<std::uint64_t> exit_after_frames;
    std::optional<std::uint16_t> mcp_port;
    std::uint32_t supersample_factor{1};
    bool default_mcp{};
    bool mcp_manual_step{};
    bool preflight{};
    // Diagnostic gap survey (dex_activity only): substitutes recorded neutral
    // stubs for unresolved platform surfaces so one run harvests the whole
    // work queue. Never a compatibility result.
    std::optional<std::filesystem::path> survey_gaps_output;
    // Saves persist by default (ADR-0020); automation opts out so golden
    // frames stay reproducible.
    std::optional<std::filesystem::path> sandbox_directory;
    bool ephemeral_sandbox{};
    for (int index = 3; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (option == "--system-dir" && index + 1 < argc) {
            system_directory = std::filesystem::path{argv[++index]};
        } else if (option == "--profiles-dir" && index + 1 < argc) {
            profiles_directory = std::filesystem::path{argv[++index]};
        } else if (option == "--external-dir" && index + 1 < argc) {
            if (external_directory.has_value()) {
                throw std::invalid_argument(
                    "run-apk accepts --external-dir only once");
            }
            external_directory = std::filesystem::path{argv[++index]};
            if (external_directory->empty()) {
                throw std::invalid_argument(
                    "--external-dir requires a non-empty host directory");
            }
        } else if (option == "--exit-after-frames" && index + 1 < argc) {
            exit_after_frames = ParsePositive(argv[++index], option);
        } else if (option == "--supersample" && index + 1 < argc) {
            supersample_factor = ParseSupersampleFactor(argv[++index]);
        } else if (option == "--mcp-port" && index + 1 < argc) {
            if (mcp_port.has_value()) {
                throw std::invalid_argument("run-apk accepts --mcp-port only once");
            }
            mcp_port = ParseMcpPort(argv[++index]);
        } else if (option == "--mcp") {
            if (default_mcp) {
                throw std::invalid_argument("run-apk accepts --mcp only once");
            }
            default_mcp = true;
        } else if (option == "--mcp-manual-step") {
            if (mcp_manual_step) {
                throw std::invalid_argument(
                    "run-apk accepts --mcp-manual-step only once");
            }
            mcp_manual_step = true;
        } else if (option == "--sandbox-dir" && index + 1 < argc) {
            const std::string_view value{argv[++index]};
            if (value.empty()) {
                throw std::invalid_argument(
                    "--sandbox-dir requires a non-empty host directory");
            }
            sandbox_directory = std::filesystem::path{value};
        } else if (option == "--ephemeral-sandbox") {
            ephemeral_sandbox = true;
        } else if (option == "--preflight") {
            preflight = true;
        } else if (option == "--survey-gaps" && index + 1 < argc) {
            if (survey_gaps_output.has_value()) {
                throw std::invalid_argument(
                    "run-apk accepts --survey-gaps only once");
            }
            survey_gaps_output = std::filesystem::path{argv[++index]};
        } else {
            throw std::invalid_argument("unknown or incomplete run-apk option: " +
                                        std::string(option));
        }
    }
    if (!system_directory.has_value()) {
        throw std::invalid_argument("run-apk requires --system-dir with API 19 Bionic libraries");
    }
    if (ephemeral_sandbox && sandbox_directory.has_value()) {
        throw std::invalid_argument(
            "--ephemeral-sandbox and --sandbox-dir cannot be combined");
    }
    if (default_mcp && mcp_port.has_value()) {
        throw std::invalid_argument("--mcp and --mcp-port cannot be combined");
    }
    if (preflight && default_mcp) {
        throw std::invalid_argument("--mcp cannot be combined with --preflight");
    }
    if (preflight && mcp_port.has_value()) {
        throw std::invalid_argument("--mcp-port cannot be combined with --preflight");
    }
    if (mcp_manual_step && !default_mcp && !mcp_port.has_value()) {
        throw std::invalid_argument(
            "--mcp-manual-step requires --mcp or --mcp-port");
    }
    if (mcp_manual_step && preflight) {
        throw std::invalid_argument(
            "--mcp-manual-step cannot be combined with --preflight");
    }
    if (default_mcp) mcp_port = kDefaultMcpPort;

    const auto apk_bytes = ReadBytes(apk_path);
    const auto archive = loader::ParseApkArchive(apk_bytes);
    const auto quirks =
        session::QuirkRegistry::LoadPackaged(bundled_data.quirk_registry);
    const auto profiles = session::TitleProfileCatalog::LoadDirectory(
        profiles_directory, quirks);
    const auto manifest = loader::ReadAndroidManifest(apk_bytes, archive);
    const auto libraries = loader::ReadApkArmNativeLibraries(apk_bytes, archive);
    const auto match = session::MatchApkTitleProfile(manifest, libraries, profiles);
    if (!match.has_value() || match->profile == nullptr) {
        throw std::runtime_error("APK has no exact Title Profile: " +
                                 manifest.package);
    }
    const auto& profile = *match->profile;
    // Declared before the filesystem so the overlay outlives every node
    // that flushes into it.
    const auto sandbox =
        OpenSandbox({sandbox_directory, ephemeral_sandbox}, manifest.package,
                    manifest.version_code);
    runtime::VirtualFileSystem filesystem;
    MountExternalDirectory(profile, external_directory, filesystem);
    AttachSandbox(sandbox, profile, filesystem, logger);
    if (profile.data.has_value() &&
        profile.data->working_directory.has_value()) {
        filesystem.SetWorkingDirectory(*profile.data->working_directory);
    }
    const auto& bionic = runtime::SelectBionicProfile(profile.runtime.api_level);
    const auto owned_system = ReadSystemLibraries(*system_directory, bionic);
    std::vector<runtime::BionicModuleSource> system_sources;
    system_sources.reserve(owned_system.size());
    for (const auto& source : owned_system) {
        system_sources.push_back({source.name, source.image});
    }
    auto launch = session::PrepareApkProfileLaunch(
        manifest, libraries, profiles, system_sources);
    if (!launch.has_value()) {
        throw std::logic_error("exact APK Profile disappeared during launch planning");
    }
    const auto module_inputs = launch->modules.Inputs();
    const auto root_name = std::string(launch->modules.RootName());
    logger.Write(
        core::LogLevel::info, "frontend.run_apk", "exact Profile selected", {},
        {{"package", manifest.package},
         {"root_module", root_name},
         {"api_level", static_cast<std::uint64_t>(profile.runtime.api_level)},
         {"lifecycle", std::string(session::ToString(profile.runtime.lifecycle))},
         {"surface_width", static_cast<std::uint64_t>(profile.runtime.surface.width)},
         {"surface_height", static_cast<std::uint64_t>(profile.runtime.surface.height)},
         {"maximum_ticks_per_call", profile.runtime.maximum_ticks_per_call},
         {"guest_modules", static_cast<std::uint64_t>(module_inputs.size())}},
        kUnrestrictedLog);
    if (preflight) {
        const auto linked = runtime::PreflightAndroidGuestLink(
            {profile.runtime.api_level, root_name, module_inputs, NativeBackend(),
             profile.runtime.surface.width, profile.runtime.surface.height,
             supersample_factor});
        Write("OGPlay: preflight ready: package=" + manifest.package +
              " root=" + root_name + " abi=" +
              std::string(loader::ToString(launch->match.library.abi)) +
              " api=" + std::to_string(profile.runtime.api_level) +
              " lifecycle=" + std::string(session::ToString(profile.runtime.lifecycle)) +
              " modules=" + std::to_string(module_inputs.size()) + " surface=" +
              std::to_string(profile.runtime.surface.width) + "x" +
              std::to_string(profile.runtime.surface.height) + " linked=" +
              std::to_string(linked.guest_modules) + "+" +
              std::to_string(linked.boundary_modules) + " relocations=" +
              std::to_string(linked.relocations) + " sandbox=" +
              sandbox.Describe() + "\n");
        return 0;
    }
    if (profile.runtime.lifecycle != session::ProfileLifecycle::dex_activity) {
        throw std::runtime_error(
            "run-apk requires a Profile v2 dex_activity lifecycle: " +
            std::string(session::ToString(profile.runtime.lifecycle)));
    }

    std::unique_ptr<agent::FrameSnapshotStore> mcp_frames;
    std::unique_ptr<agent::McpInputQueue> mcp_inputs;
    std::unique_ptr<agent::McpSessionControl> mcp_session;
    std::unique_ptr<McpHttpServer> mcp_server;
    if (mcp_port.has_value()) {
        mcp_frames = std::make_unique<agent::FrameSnapshotStore>();
        mcp_inputs = std::make_unique<agent::McpInputQueue>();
        if (mcp_manual_step) {
            mcp_session = std::make_unique<agent::McpSessionControl>();
            mcp_server = McpHttpServer::Start(
                *mcp_port, *mcp_frames, *mcp_inputs, *mcp_session);
        } else {
            mcp_server = McpHttpServer::Start(
                *mcp_port, *mcp_frames, *mcp_inputs);
        }
        Write("OGPlay: MCP ready at " + mcp_server->Endpoint() + "\n");
    }

    auto window = hal::CreateSdlWindowInput();
    const auto base_title = "OGPlay · " + apk_path.filename().string();
    window->Open({.title = base_title + " · FPS --",
                  .width = profile.runtime.surface.width,
                  .height = profile.runtime.surface.height,
                  .hidden = mcp_manual_step, .resizable = true});
    logger.Write(
        core::LogLevel::info, "frontend.run_apk", "SDL window opened", {},
        {{"hidden", mcp_manual_step},
         {"supersample", static_cast<std::uint64_t>(supersample_factor)}},
        kUnrestrictedLog);
    bool quit{};
    std::uint64_t presented{};
    std::uint32_t guest_width = profile.runtime.surface.width;
    std::uint32_t guest_height = profile.runtime.surface.height;
    hal::RealtimeClock frame_rate_clock;
    hal::FrameRateSampler frame_rate{
        frame_rate_clock.TicksPerSecond(),
        frame_rate_clock.TicksPerSecond() / 2U};
    const auto update_frame_rate = [&] {
        if (const auto fps = frame_rate.Observe(
                presented, frame_rate_clock.Ticks());
            fps.has_value()) {
            window->SetTitle(FrameRateTitle(base_title, *fps));
        }
    };
    input::MouseTouchMapper mouse_touch;
    McpPointerDispatcher mcp_pointer;
    {
        std::shared_ptr<runtime::DexVmAndroidContext> dex_context;
        {
            dex_context = std::make_shared<runtime::DexVmAndroidContext>();
            dex_context->apk_bytes = apk_bytes;
            dex_context->archive = archive;
            const auto arsc_bytes =
                loader::ReadApkEntry(apk_bytes, archive, "resources.arsc");
            dex_context->arsc = loader::ParseArsc(std::span(
                reinterpret_cast<const std::uint8_t*>(arsc_bytes.data()),
                arsc_bytes.size()));
            dex_context->package_name = manifest.package;
            dex_context->surface_width = profile.runtime.surface.width;
            dex_context->surface_height = profile.runtime.surface.height;
            dex_context->api_level =
                static_cast<std::int32_t>(profile.runtime.api_level);
            // Java File/streams resolve through the same guest VFS as
            // native fopen (external mounts, /apk assets).
            dex_context->vfs = &filesystem;
            // Real VideoView playback when the FFmpeg shared libraries are
            // loadable; otherwise setVideoPath records the gap and start()
            // keeps the immediate-completion fallback (ADR-0021).
            if (video::FfmpegAvailable()) {
                dex_context->video_player_factory =
                    video::MakeFfmpegVideoPlayerFactory();
            } else {
                logger.Write(
                    core::LogLevel::warn, "frontend.run_apk",
                    "video decoding is unavailable: " +
                        video::FfmpegUnavailableReason(),
                    {}, {}, kUnrestrictedLog);
            }
            if (external_directory.has_value()) {
                std::error_code space_error;
                const auto space = std::filesystem::space(
                    *external_directory, space_error);
                if (!space_error) {
                    dex_context->external_free_bytes = space.available;
                }
            }
        }
        auto sound_loader =
            audio::JavaSoundPoolMixer::EncodedResourceLoader(
                [dex_context](const std::int32_t resource)
                    -> std::vector<std::byte> {
                    const auto* entry = dex_context->arsc.FindById(
                        static_cast<std::uint32_t>(resource));
                    if (entry == nullptr || !entry->string_value.has_value()) {
                        return {};
                    }
                    return loader::ReadApkEntry(
                        dex_context->apk_bytes, dex_context->archive,
                        *entry->string_value);
                });
        std::unique_ptr<hal::AudioOutput> audio_output;
        std::vector<std::int16_t> audio_samples(1024U * 2U);
        if (sound_loader) {
            audio_output = hal::CreateSdlAudioOutput(
                {48000U, 2U, hal::AudioSampleFormat::signed_16_le});
            audio_output->Start();
        }
        std::uint64_t active_frame{};
        // Real-time pacing while a VideoView plays: the dex lifecycle
        // advances its deterministic uptime by 16 ms per frame, so during
        // free-running playback each frame must also cost 16 ms of wall
        // time or the video (and its audio) races ahead. Games without
        // active playback keep the unthrottled loop; manual stepping is
        // never paced.
        std::chrono::steady_clock::time_point video_pace_deadline{};
        RunApkGuestCallProgress call_progress{logger};
        HostEventThreadGate event_thread_gate;
        constexpr std::uint64_t kGuestCallEventPumpHz = 250U;
        HostEventPumpGate pump_gate{
            frame_rate_clock.TicksPerSecond() / kGuestCallEventPumpHz};
        const auto runtime_progress = [&logger](const std::string_view stage) {
            logger.Write(
                core::LogLevel::info, "runtime.guest_session", stage, {}, {},
                kUnrestrictedLog);
        };
        const auto guest_slice_observer = [&](const std::uint64_t consumed_ticks) {
            // DexVM Java threads reuse the native guest-call observer. SDL
            // event pumping and the progress gate it mutates belong only to
            // the host thread that opened and drives the window.
            if (!event_thread_gate.IsOwnerThread()) return;
            if (pump_gate.ShouldPump(frame_rate_clock.Ticks())) {
                window->PumpEvents();
            }
            call_progress.Observe(consumed_ticks);
        };
        call_progress.Begin(0U, 0U);
        auto guest = runtime::AndroidGuestCallSession::Start(
            {profile.runtime.api_level, root_name, module_inputs,
             NativeBackend(), profile.runtime.surface.width,
             profile.runtime.surface.height,
             profile.runtime.maximum_ticks_per_call,
             supersample_factor, &filesystem, runtime_progress, std::nullopt,
             {.allow_gles1_material_single_face = ProfileEnablesQuirk(
                  profile, "gles1_material_front_face")},
             std::move(sound_loader),
             guest_slice_observer,
             {.installation_id = "ogplay-" + manifest.package,
              .version_name = manifest.version_name.value_or("unknown")}});

        // Keep the frame/MCP loop independent of lifecycle storage.
        struct LifecycleDriver final {
            std::function<session::LifecycleFrameState()> start;
            std::function<session::LifecycleFrameState()> suspend;
            std::function<session::LifecycleFrameState()> resume;
            std::function<session::LifecycleFrameState()> step;
            std::function<session::LifecycleFrameState()> stop;
            std::function<session::LifecycleFrameState()> state;
            std::function<void(const runtime::AndroidBoundaryInput&)>
                queue_input;
        };
        LifecycleDriver driver;
        std::unique_ptr<runtime::DexVmGuestBridge> dex_bridge;
        std::unique_ptr<session::DexActivityLifecycle> dex_lifecycle;
        core::CapabilityLedger dexvm_ledger;
        {
            dex_context->session = guest.get();
            const auto dex_entry =
                loader::ReadApkEntry(apk_bytes, archive, "classes.dex");
            std::vector<std::uint8_t> dex_bytes(dex_entry.size());
            std::memcpy(dex_bytes.data(), dex_entry.data(),
                        dex_entry.size());
            runtime::DexVmBridgeConfig bridge_config;
            if (profile.runtime.dexvm.has_value()) {
                bridge_config.heap.heap_budget_bytes =
                    profile.runtime.dexvm->heap_budget_bytes;
                bridge_config.interpreter.max_frames =
                    profile.runtime.dexvm->max_frames;
                bridge_config.interpreter.tick_budget =
                    profile.runtime.dexvm->ticks_per_call;
            }
            const auto android_catalog =
                runtime::AndroidIntrinsicCatalog(dex_context);
            dex_bridge = std::make_unique<runtime::DexVmGuestBridge>(
                *guest, std::move(dex_bytes), android_catalog,
                dex_context, dexvm_ledger, &logger, bridge_config);
            dex_context->threads = &dex_bridge->Threads();
            // Object.wait deadlines come from the deterministic uptime the
            // lifecycle driver publishes from the unified Clock, never from
            // a host wall clock.
            dex_bridge->Vm().Monitors().SetTimeSource([dex_context] {
                return dex_context->uptime_millis.load();
            });
            if (survey_gaps_output.has_value()) {
                dex_bridge->Linker().EnableGapSurvey();
                Write("OGPlay: GAP SURVEY RUN — unresolved platform classes "
                      "and methods answer neutrally and are recorded. This is "
                      "a diagnostic work queue, not a compatibility result.\n");
                logger.Write(core::LogLevel::warn, "frontend.run_apk",
                             "gap survey enabled; run is diagnostic only",
                             {}, {}, kUnrestrictedLog);
            }
            const auto descriptor = session::ResolveProfileLaunchDescriptor(
                profile, manifest.launcher_activity);
            session::ApplyProfileStaticPresets(
                profile, dex_bridge->Vm(), &logger);
            dex_lifecycle = std::make_unique<session::DexActivityLifecycle>(
                session::DexActivityLifecycleBindings{
                    dex_bridge.get(), dex_context, descriptor,
                    [&guest] { guest->OpenManagedSurface(); },
                    [&guest] { guest->PresentManagedSurface(); },
                    [&guest](std::vector<std::uint8_t> rgba8) {
                        guest->PublishSoftwareFrame(std::move(rgba8));
                    },
                    [&guest] {
                        static_cast<void>(guest->InterruptBlockingWaits());
                    },
                    [&] {
                        call_progress.Begin(active_frame, 0U);
                        guest->Stop();
                    },
                    [&guest] { guest->CloseManagedSurface(); },
                    [&filesystem] { session::FlushProfileVfsAtLifecycleBoundary(filesystem); },
                    [&guest] { guest->ReleaseManagedSurfaceFromCallingThread(); }});
            driver = {[&] { return dex_lifecycle->Start(); },
                      [&] { return dex_lifecycle->Suspend(); },
                      [&] { return dex_lifecycle->Resume(); },
                      [&] { return dex_lifecycle->StepFrame(); },
                      [&] { return dex_lifecycle->Stop(); },
                      [&] { return dex_lifecycle->State(); },
                      [&](const runtime::AndroidBoundaryInput& input) {
                          dex_lifecycle->QueueInput(input);
                      }};
        }
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "initializing root JNI library", {}, {},
                     kUnrestrictedLog);
        call_progress.Begin(0U, 0U);
        guest->InitializeJniLibrary();
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "starting Profile lifecycle", {}, {},
                     kUnrestrictedLog);
        static_cast<void>(driver.start());
        agent::McpLifecycleState mcp_lifecycle{
            agent::McpLifecycleState::running};
        std::optional<std::string> guest_fault;
        std::exception_ptr failure;
        std::uint64_t permitted_steps{};
        const auto publish_session = [&] {
            if (!mcp_session) return;
            const auto state = driver.state();
            agent::McpSessionSnapshot snapshot{
                .lifecycle = mcp_lifecycle,
                .frame = state.frame,
                .guest_ticks = state.clock_ticks,
                .process_exit = guest->ExitRequested(),
                .guest_fault = guest_fault,
            };
            if (const auto frame = mcp_frames->LatestMetadata(); frame) {
                snapshot.presented_frame = frame->sequence;
            }
            if (const auto movie = guest->LatestMovieRequest(); movie) {
                snapshot.movie_request = agent::McpMovieRequestSnapshot{
                    movie->sequence, Utf16ToUtf8(movie->name)};
            }
            mcp_session->Publish(std::move(snapshot));
        };
        publish_session();
        Write(mcp_manual_step
                  ? "OGPlay: Profile GLSurfaceView started in MCP manual-step mode.\n"
                  : "OGPlay: Profile GLSurfaceView started; close the window to stop.\n");
        while (!quit && !guest->ExitRequested() &&
               !(dex_context &&
                 runtime::SessionExitRequested(*dex_context))) {
            const auto window_state = window->State();
            for (const auto& event : window->PollEvents()) {
                if (event.type == hal::InputEventType::quit) {
                    quit = true;
                } else if (mcp_pointer.SuppressWindowEvent(event.type)) {
                    continue;
                } else if (mcp_lifecycle == agent::McpLifecycleState::suspended ||
                           failure) {
                    continue;
                } else if (const auto mapped = mouse_touch.Map(
                               event, window_state, guest_width, guest_height);
                           mapped.has_value()) {
                    if (const auto input = MapInput(*mapped); input.has_value()) {
                        driver.queue_input(*input);
                    }
                }
            }
            bool frame_presented = false;
            try {
                if (mcp_session) {
                    if (const auto command = mcp_session->TakeNextCommand(); command) {
                        using Command = agent::McpSessionCommand;
                        if (command->type == Command::Type::shutdown) {
                            quit = true;
                        } else if (!failure && command->type == Command::Type::step) {
                            permitted_steps += command->frames;
                        } else if (!failure && command->type == Command::Type::suspend) {
                            static_cast<void>(driver.suspend());
                            mcp_lifecycle = agent::McpLifecycleState::suspended;
                            publish_session();
                        } else if (!failure && command->type == Command::Type::resume) {
                            static_cast<void>(driver.resume());
                            mcp_lifecycle = agent::McpLifecycleState::running;
                            publish_session();
                        }
                    }
                }
                const bool advance = !failure &&
                    mcp_lifecycle == agent::McpLifecycleState::running &&
                    (!mcp_manual_step || permitted_steps != 0U);
                if (!advance || quit) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (const auto input = mcp_pointer.TakeNext(
                        mcp_inputs.get(), mouse_touch); input.has_value()) {
                    driver.queue_input(*input);
                }
                active_frame = driver.state().frame + 1U;
                static_cast<void>(driver.step());
                const auto stepped = driver.state();
                if (mcp_manual_step) --permitted_steps;
                if (!mcp_manual_step && dex_context &&
                    runtime::AnyVideoPlaying(*dex_context)) {
                    constexpr auto kVideoFramePeriod =
                        std::chrono::milliseconds(16);
                    const auto now = std::chrono::steady_clock::now();
                    if (video_pace_deadline < now - kVideoFramePeriod) {
                        video_pace_deadline = now;  // playback (re)started
                    }
                    video_pace_deadline += kVideoFramePeriod;
                    std::this_thread::sleep_until(video_pace_deadline);
                } else {
                    video_pace_deadline = {};
                }
                if (audio_output) {
                    PumpAudio(*guest, *audio_output, audio_samples,
                              dex_context.get());
                }
                if (auto frame = guest->TakeLatestFrame(); frame.has_value()) {
                    window->PresentRgba8(frame->rgba8, frame->width, frame->height);
                    guest_width = frame->width;
                    guest_height = frame->height;
                    ++presented;
                    frame_presented = true;
                    update_frame_rate();
                    PublishPresentedFrame(mcp_frames.get(), std::move(*frame), *guest);
                    if (exit_after_frames.has_value() &&
                        presented >= *exit_after_frames) {
                        quit = true;
                    }
                }
                const auto stats = guest->Stats();
                LogProfileFrameProgress(
                    logger, stepped.frame, stepped.clock_ticks,
                    presented, stats.draws, stats.clears);
                publish_session();
            } catch (const std::exception& error) {
                if (!failure) failure = std::current_exception();
                guest_fault = error.what();
                mcp_lifecycle = agent::McpLifecycleState::failed;
                permitted_steps = 0U;
                publish_session();
                logger.Write(
                    core::LogLevel::error, "frontend.run_apk",
                    "Profile GLSurfaceView failed",
                    {.frame = driver.state().frame},
                    {{"reason", std::string(error.what())}});
                if (!mcp_manual_step) quit = true;
            }
            if (ShouldIdleSleepAfterFrameStep(frame_presented)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        ReleaseCapturedFrame(mcp_frames.get(), *guest);
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "stopping Profile lifecycle",
                     {.frame = driver.state().frame}, {}, kUnrestrictedLog);
        try {
            static_cast<void>(driver.stop());
            if (!failure) mcp_lifecycle = agent::McpLifecycleState::stopped;
        } catch (const std::exception& error) {
            if (!failure) failure = std::current_exception();
            if (!guest_fault) guest_fault = error.what();
            mcp_lifecycle = agent::McpLifecycleState::failed;
        }
        if (audio_output) audio_output->Stop();
        publish_session();
        if (survey_gaps_output.has_value() && dex_bridge) {
            // Written before rethrowing: a survey run usually ends on the
            // first gap that no stub can paper over, and that harvest is
            // exactly what the next batch needs.
            const auto hits = dex_bridge->Linker().GapSurveyHits();
            std::ofstream report(*survey_gaps_output, std::ios::binary);
            report << runtime::dexvm::RenderGapSurveyJson(
                hits, apk_path.filename().string());
            Write("OGPlay: gap survey wrote " + std::to_string(hits.size()) +
                  " entries to " + survey_gaps_output->string() + "\n");
        }
        if (failure) std::rethrow_exception(failure);
    }
    window->Close();
    logger.Write(core::LogLevel::info, "frontend.run_apk",
                 "APK session stopped", {}, {{"presented", presented}},
                 kUnrestrictedLog);
    Write("OGPlay: stopped after " + std::to_string(presented) + " presented frames.\n");
    return 0;
}
}  // namespace ogplay::frontend
