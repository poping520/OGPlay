#include "run_game.h"
#include "run_game_vfs.h"
#include <algorithm>
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

#include "ogplay/agent/control_editor.h"
#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/agent/mcp_session_control.h"
#include "ogplay/agent/dashboard.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/core/text.h"
#include "ogplay/frontend/control_requests.h"
#include "ogplay/frontend/mcp_http_server.h"
#include "ogplay/frontend/control_overlay.h"
#include "ogplay/frontend/host_shortcuts.h"
#include "ogplay/frontend/mcp_input_dispatch.h"
#include "ogplay/frontend/data_directory.h"
#include "ogplay/frontend/run_apk_progress.h"
#include "ogplay/hal/audio.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/hal/window_input.h"
#include "ogplay/input/control_mapper.h"
#include "ogplay/session/control_config.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/android_link_preflight.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/session/profile_apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/runtime/dexvm/gap_survey.h"
#include "ogplay/runtime/debug/stall_diagnostics.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/session/profile_entry_scope.h"
#include "ogplay/session/profile_vfs.h"
#include "ogplay/video/ffmpeg_video_player.h"
#include "ogplay/video/buffered_video_player.h"
#include "ogplay/session/quirk_registry.h"
#include "ogplay/session/title_profile.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/audio_output_pump.h"
#include "ogplay/session/android_input_mapping.h"

namespace ogplay::frontend {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

constexpr hal::AudioStreamConfig kDesktopAudioOutputSpec{
    48000U, 2U, hal::AudioSampleFormat::signed_16_le};

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open " + PathUtf8(path));
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("input file is empty: " + PathUtf8(path));
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(result.data()), size);
    if (!input) throw std::runtime_error("input file was truncated: " + PathUtf8(path));
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
        throw std::runtime_error("bundled Android library directory is unavailable: " +
                                 PathUtf8(directory));
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

[[nodiscard]] bool ProfileEnablesQuirk(
    const session::TitleProfile& profile, const std::string_view id) {
    return profile.quirks.has_value() &&
           std::ranges::find(profile.quirks->enabled, id) !=
               profile.quirks->enabled.end();
}

std::string Utf16ToUtf8(const std::span<const runtime::JniChar> text) {
    return *core::Utf16ToUtf8(
        text, core::InvalidUtf16Policy::replace, 0xfffdU);
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

int RunGame(GameRunOptions options, core::Logger& logger, bool status_to_stderr) {
    const auto Write = [stream = status_to_stderr ? stderr : stdout](std::string_view text) {
        static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
        static_cast<void>(std::fflush(stream));
    };
    auto [apk_path, external_directory, external_guest_directory, obb_path,
          profiles_directory, exit_after_frames, mcp_port, key_editor_port,
          dexvm_interpreter, diagnostic_directory, diagnostic_teardown_timeout,
          diagnostics_enabled, supersample_factor, cpu_config, mcp_manual_step,
          preflight, survey_gaps_output, sandbox_directory, installation_id,
          ephemeral_sandbox, input_config_path, library_root] = std::move(options);
    const auto bundled_data = HostBundledDataPaths();
    logger.Write(core::LogLevel::info, "frontend.game", "runtime payload selected", {},
                 {{"data_root", PathUtf8(bundled_data.root)}});
    const auto apk_bytes = std::make_shared<const std::vector<std::byte>>(
        ReadBytes(apk_path));
    const auto archive = loader::ParseApkArchive(*apk_bytes);
    const auto quirks =
        session::QuirkRegistry::LoadPackaged(bundled_data.quirk_registry);
    const auto profiles = session::TitleProfileCatalog::LoadDirectory(
        profiles_directory.value_or(bundled_data.profiles_directory), quirks);
    const auto manifest = loader::ReadAndroidManifest(*apk_bytes, archive);
    const auto libraries = loader::ReadApkArmNativeLibraries(*apk_bytes, archive);
    const loader::ApkNativeLibraryInventory native_inventory{libraries};
    const auto compatibility =
        session::SelectApkCompatibilityProfile(manifest, libraries, profiles);
    session::TitleProfile generic_profile;
    generic_profile.schema = 2;
    generic_profile.identity.package = manifest.package;
    generic_profile.runtime.api_level = 19;
    generic_profile.runtime.surface = {800, 480};
    const auto* selected_profile = compatibility.profile;
    const auto& profile = selected_profile != nullptr
                              ? *selected_profile
                              : generic_profile;
    bool has_obb_mount{};
    bool requires_obb{};
    if (profile.data.has_value()) {
        for (const auto& mount : profile.data->mounts) {
            if (mount.source != session::ProfileSource::obb) continue;
            has_obb_mount = true;
            requires_obb |= mount.required;
        }
    }
    if (!obb_path.has_value() && requires_obb) {
        throw std::runtime_error("Profile requires --obb for its OBB mount");
    }
    if (obb_path.has_value()) {
        const auto filename = PathUtf8(obb_path->filename());
        const auto suffix = "." + manifest.package + ".obb";
        const auto prefix_length = filename.starts_with("main.") ? 5U :
                                   filename.starts_with("patch.") ? 6U : 0U;
        if (prefix_length == 0U || !filename.ends_with(suffix) ||
            filename.size() <= prefix_length + suffix.size()) {
            throw std::invalid_argument(
                "--obb filename must be main.<version>.<package>.obb or "
                "patch.<version>.<package>.obb for the APK package");
        }
        const auto version = std::string_view(filename).substr(
            prefix_length, filename.size() - prefix_length - suffix.size());
        if (version.empty() || !std::ranges::all_of(version, [](const char ch) {
                return ch >= '0' && ch <= '9';
            })) {
            throw std::invalid_argument("--obb filename has an invalid version");
        }
    }
    std::shared_ptr<const std::vector<std::byte>> obb_bytes;
    std::optional<loader::ApkArchive> obb_archive;
    if (obb_path.has_value() && has_obb_mount) {
        obb_bytes = std::make_shared<const std::vector<std::byte>>(
            ReadBytes(*obb_path));
        obb_archive = loader::ParseApkArchive(*obb_bytes);
    }
    // Declared before the filesystem so the overlay outlives every node
    // that flushes into it.
    const auto sandbox =
        OpenSandbox({sandbox_directory, ephemeral_sandbox, installation_id}, manifest.package,
                    manifest.version_code);
    // User key layout (ADR-0134): --input-config, else beside the persistent sandbox; an
    // ephemeral sandbox edits in memory only. A missing file is an empty layout.
    if (!input_config_path.has_value() && sandbox.store != nullptr)
        input_config_path = sandbox.root / (sandbox.installation_id + ".input.toml");
    auto user_controls = input_config_path.has_value()
                             ? session::LoadControlOverrideIfPresent(*input_config_path)
                             : session::ControlLayer{};
    // Input configuration errors (unknown template, unplaced or oversized zones) stop the
    // launch before the guest starts.
    const auto control_config = session::BuildControlConfig(
        profile.input.has_value() ? &*profile.input : nullptr, &user_controls,
        bundled_data.input_templates_directory);
    if (profile.runtime.surface.width != 0 && profile.runtime.surface.height != 0) {
        input::ControlMapper probe(control_config);
        probe.SetSurface(profile.runtime.surface.width, profile.runtime.surface.height);
    }
    runtime::VirtualFileSystem filesystem;
    MountApkArchive(profile, apk_bytes, archive, filesystem);
    if (obb_path.has_value()) {
        filesystem.MountHostFile(
            runtime::VfsSource::obb,
            "/sdcard/Android/obb/" + manifest.package, *obb_path);
    }
    if (obb_bytes) {
        MountObbArchive(profile, obb_bytes, *obb_archive, filesystem);
    }
    MountExternalDirectory(profile, external_directory, filesystem,
                           external_guest_directory);
    AttachSandbox(sandbox, profile, filesystem, logger);
    filesystem.SetWorkingDirectory(
        session::ResolveProfileWorkingDirectory(profile));
    const auto& bionic = runtime::SelectBionicProfile(profile.runtime.api_level);
    const auto system_directory =
        bundled_data.root / bionic.data_directory / "lib";
    const auto owned_system = ReadSystemLibraries(system_directory, bionic);
    std::vector<runtime::BionicModuleSource> system_sources;
    system_sources.reserve(owned_system.size());
    for (const auto& source : owned_system) {
        system_sources.push_back({source.name, source.image});
    }
    const auto process_abi = native_inventory.Empty()
                                 ? std::optional<loader::AndroidArmAbi>{}
                                 : std::optional{loader::ResolveApkProcessAbi(
                                       native_inventory)};
    logger.Write(
        core::LogLevel::info, "frontend.run_apk", "APK startup facts selected", {},
        {{"package", manifest.package},
         {"profile", selected_profile != nullptr
                         ? profile.identity.package
                         : "none"},
         {"api_level", static_cast<std::uint64_t>(profile.runtime.api_level)},
         {"lifecycle", std::string(session::ToString(profile.runtime.lifecycle))},
         {"surface_width", static_cast<std::uint64_t>(profile.runtime.surface.width)},
         {"surface_height", static_cast<std::uint64_t>(profile.runtime.surface.height)},
         {"maximum_ticks_per_call", profile.runtime.maximum_ticks_per_call},
         {"native_libraries", static_cast<std::uint64_t>(
                                  native_inventory.Libraries().size())}},
        kUnrestrictedLog);
    if (preflight) {
        runtime::AndroidImportAudit audit;
        if (process_abi) audit = runtime::AuditAndroidNativeImports(
            loader::ApkSelectedNativeLibraries(native_inventory, *process_abi), bionic, system_sources);
        for (const auto& gap : audit.gaps) {
            Write("OGPlay: preflight import: library=" + gap.library + " binding=" +
                  (gap.weak ? "weak" : "strong") + " symbol=" + gap.symbol + "\n");
            logger.Write(gap.weak ? core::LogLevel::warn : core::LogLevel::error,
                "frontend.preflight", "unresolved native import", {},
                {{"library", gap.library}, {"symbol", gap.symbol}, {"weak", gap.weak}}, kUnrestrictedLog);
        }
        Write("OGPlay: preflight imports: strong_missing=" + std::to_string(audit.strong_missing) +
              " weak_missing=" + std::to_string(audit.weak_missing) + "\n");
        logger.Write(audit.strong_missing ? core::LogLevel::error : core::LogLevel::info,
            "frontend.preflight", "native import audit completed", {},
            {{"strong_missing", static_cast<std::uint64_t>(audit.strong_missing)},
             {"weak_missing", static_cast<std::uint64_t>(audit.weak_missing)}}, kUnrestrictedLog);
        if (audit.strong_missing) return 1;
        Write("OGPlay: preflight ready: package=" + manifest.package +
              " rootless=true abi=" +
              (process_abi.has_value()
                   ? std::string(loader::ToString(*process_abi))
                   : std::string("none")) +
              " api=" + std::to_string(profile.runtime.api_level) +
              " lifecycle=" + std::string(session::ToString(profile.runtime.lifecycle)) +
              " native=" + std::to_string(native_inventory.Libraries().size()) +
              " surface=" +
              std::to_string(profile.runtime.surface.width) + "x" +
              std::to_string(profile.runtime.surface.height) + " profile=" +
              (selected_profile != nullptr ? profile.identity.package
                                           : "none") +
              " sandbox=" +
              sandbox.Describe() + "\n");
        return 0;
    }
    std::unique_ptr<agent::FrameSnapshotStore> mcp_frames;
    std::unique_ptr<agent::McpInputQueue> mcp_inputs;
    std::unique_ptr<agent::McpSessionControl> mcp_session;
    auto diagnostic_state = (diagnostics_enabled || mcp_port.has_value())
        ? std::make_shared<runtime::debug::DiagnosticState>()
        : std::shared_ptr<runtime::debug::DiagnosticState>{};
    // The key editor reads presented frames too (control.frame), so frame and input stores
    // exist in every windowed session; only MCP adds the session control.
    mcp_frames = std::make_unique<agent::FrameSnapshotStore>();
    mcp_inputs = std::make_unique<agent::McpInputQueue>();
    if (mcp_port.has_value()) mcp_session = std::make_unique<agent::McpSessionControl>();

    auto window = hal::CreateSdlWindowInput();
    const auto base_title = "OGPlay · " + PathUtf8(apk_path.filename());
    window->Open({.title = base_title + " · FPS --",
                  .width = profile.runtime.surface.width,
                  .height = profile.runtime.surface.height,
                  .hidden = mcp_manual_step, .resizable = true});
    logger.Write(
        core::LogLevel::info, "frontend.run_apk", "SDL window opened", {},
        {{"hidden", mcp_manual_step},
         {"supersample", static_cast<std::uint64_t>(supersample_factor)},
         {"cpu_cores", static_cast<std::uint64_t>(cpu_config.cores)},
         {"cpu_frequency_mhz", static_cast<std::uint64_t>(cpu_config.frequency_mhz)},
         {"cpu_limit_parallelism", cpu_config.execution.limit_parallelism},
         {"cpu_max_mticks_per_second", static_cast<std::uint64_t>(cpu_config.execution.max_mticks_per_second)}},
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
    input::ControlMapper control_mapper(control_config);
    if (guest_width != 0 && guest_height != 0) control_mapper.SetSurface(guest_width, guest_height);
    McpPointerDispatcher mcp_pointer;
    {
        std::shared_ptr<runtime::DexVmAndroidContext> dex_context;
        {
            dex_context = std::make_shared<runtime::DexVmAndroidContext>();
            dex_context->native_output_sample_rate =
                kDesktopAudioOutputSpec.sample_rate;
            dex_context->apk_bytes = *apk_bytes;
            dex_context->archive = archive;
            const auto arsc_bytes =
                loader::ReadApkEntry(*apk_bytes, archive, "resources.arsc");
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
            // Real VideoView playback when this build links ogplay_ffmpeg and
            // its self-check passed; otherwise opening a video records the gap
            // and the guest receives an explicit error (ADR-0136).
            if (video::FfmpegAvailable()) {
                dex_context->video_source_player_factory =
                    [realtime = !mcp_manual_step](auto source) {
                        auto player = video::OpenFfmpegVideo(std::move(source));
                        return realtime ? video::MakeBufferedVideoPlayer(std::move(player))
                                        : std::move(player);
                    };
            } else {
                logger.Write(
                    core::LogLevel::warn, "frontend.run_apk",
                    "video decoding is unavailable: " +
                        video::FfmpegUnavailableReason(),
                    {}, {}, kUnrestrictedLog);
            }

        }
        std::unique_ptr<hal::AudioOutput> audio_output;
        audio_output = hal::CreateSdlAudioOutput(kDesktopAudioOutputSpec);
        audio_output->Start();
        std::uint64_t active_frame{};
        // Pace fast video steps; the lifecycle catches up slow steps using
        // the same unified host Clock. Manual stepping remains deterministic.
        std::uint64_t video_pace_deadline_ns{};
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
        const auto pump_host_events = [&] {
            // DexVM Java threads reuse the native guest-call observer. SDL
            // event pumping and the progress gate it mutates belong only to
            // the host thread that opened and drives the window.
            if (!event_thread_gate.IsOwnerThread()) return;
            if (pump_gate.ShouldPump(frame_rate_clock.Ticks())) {
                window->PumpEvents();
            }
        };
        const auto guest_slice_observer = [&](const std::uint64_t consumed_ticks) {
            if (!event_thread_gate.IsOwnerThread()) return;
            pump_host_events();
            call_progress.Observe(consumed_ticks);
        };
        call_progress.Begin(0U, 0U);

        // Keep the frame/MCP loop independent of lifecycle storage.
        struct LifecycleDriver final {
            std::function<session::LifecycleFrameState()> start;
            std::function<session::LifecycleFrameState()> suspend;
            std::function<session::LifecycleFrameState()> resume;
            std::function<session::LifecycleFrameState()> step;
            std::function<session::LifecycleFrameState(session::DexProcessStopReason)> stop;
            std::function<session::LifecycleFrameState()> state;
            std::function<void(const runtime::AndroidBoundaryInput&)>
                queue_input;
            std::function<void()> cancel_input;
        };
        LifecycleDriver driver;
        core::CapabilityLedger dexvm_ledger;
        const auto dex_entry =
            loader::ReadApkEntry(*apk_bytes, archive, "classes.dex");
        std::vector<std::uint8_t> dex_bytes(dex_entry.size());
        std::memcpy(dex_bytes.data(), dex_entry.data(), dex_entry.size());
        auto boot_archive_bytes = ReadBytes(
            bundled_data.root / bionic.data_directory / "framework" /
            "bootdex.jar");
        const auto boot_archive =
            loader::ParseApkArchive(boot_archive_bytes);
        const auto boot_entry = loader::ReadApkEntry(
            boot_archive_bytes, boot_archive, "classes.dex");
        std::vector<std::uint8_t> boot_dex_bytes(boot_entry.size());
        std::memcpy(boot_dex_bytes.data(), boot_entry.data(),
                    boot_entry.size());
        dex_context->boot_classpath_archive = boot_archive;
        dex_context->boot_classpath_bytes = std::move(boot_archive_bytes);
        runtime::DexVmBridgeConfig bridge_config;
        if (profile.runtime.dexvm.has_value()) {
            const auto& heap = profile.runtime.dexvm->heap;
            bridge_config.heap.initial_heap_target_bytes = heap.initial_target_bytes;
            bridge_config.heap.heap_growth_limit_bytes = heap.growth_limit_bytes;
            bridge_config.heap.maximum_heap_bytes = heap.maximum_bytes;
            bridge_config.heap.target_utilization_percent =
                heap.target_utilization_percent;
            bridge_config.heap.min_free_bytes = heap.min_free_bytes;
            bridge_config.heap.max_free_bytes = heap.max_free_bytes;
            bridge_config.interpreter.max_frames =
                profile.runtime.dexvm->max_frames;
            bridge_config.interpreter.tick_budget =
                profile.runtime.dexvm->ticks_per_call;
            bridge_config.interpreter.backend =
                profile.runtime.dexvm->interpreter ==
                        session::ProfileRuntime::DexVm::Interpreter::threaded
                    ? runtime::dexvm::InterpreterBackend::threaded
                    : runtime::dexvm::InterpreterBackend::switch_dispatch;
        }
        if (dexvm_interpreter.has_value()) {
            bridge_config.interpreter.backend = *dexvm_interpreter == app::InterpreterBackend::threaded
                ? runtime::dexvm::InterpreterBackend::threaded : runtime::dexvm::InterpreterBackend::switch_dispatch;
        }
        if (diagnostic_state &&
            bridge_config.interpreter.diagnostics.trace_capacity == 0U) {
            bridge_config.interpreter.diagnostics.trace_capacity = 256U;
        }
        const std::string backend_source = dexvm_interpreter.has_value()
                                               ? "command_line"
                                           : profile.runtime.dexvm.has_value()
                                               ? "profile"
                                               : "default";
        logger.Write(
            core::LogLevel::info, "frontend.run_apk",
            "DexVM interpreter selected", {},
            {{"backend",
              bridge_config.interpreter.backend ==
                      runtime::dexvm::InterpreterBackend::threaded
                  ? "threaded"
                  : "switch"},
             {"source", backend_source}},
            kUnrestrictedLog);
        session::AndroidAppProcessRequest app_request;
        app_request.proc_facts.cpu = {cpu_config.cores, cpu_config.frequency_mhz,
            {cpu_config.execution.limit_parallelism, cpu_config.execution.max_mticks_per_second}};
        app_request.manifest = manifest;
        app_request.native_libraries = libraries;
        app_request.system_libraries = system_sources;
        app_request.boot_dex_bytes = std::move(boot_dex_bytes);
        app_request.dex_bytes = std::move(dex_bytes);
        app_request.icu_data = ReadBytes(
            bundled_data.root / bionic.data_directory / "icu" /
            "icudt51l.dat");
        app_request.tzdata = ReadBytes(
            bundled_data.root / bionic.data_directory / "zoneinfo" / "tzdata");
        app_request.context = dex_context;
        app_request.ui_fallback_font = ReadBytes(bundled_data.root / bionic.data_directory / "fonts" / "DroidSansFallback.ttf");
        if (profile.runtime.entry.has_value()) {
            app_request.launcher_override =
                profile.runtime.entry->launch_activity;
        }
        app_request.api_level = profile.runtime.api_level;
        app_request.surface_width = profile.runtime.surface.width;
        app_request.surface_height = profile.runtime.surface.height;
        app_request.maximum_ticks_per_call =
            profile.runtime.maximum_ticks_per_call;
        app_request.supersample_factor = supersample_factor;
        app_request.backend = NativeBackend();
        app_request.filesystem = &filesystem;
        app_request.progress = runtime_progress;
        app_request.boundary_options = {
            .allow_gles1_material_single_face = ProfileEnablesQuirk(
                profile, "gles1_material_front_face"),
            .async_present_readback = !mcp_manual_step};
        app_request.guest_call_slice_observer = guest_slice_observer;
        app_request.guest_call_slice_observer_thread = std::this_thread::get_id();
        app_request.platform = {
            .installation_id = sandbox.installation_id,
            .android_id = sandbox.android_id,
            .version_name = manifest.version_name.value_or("unknown")};
        app_request.dexvm = bridge_config;
        app_request.ledger = &dexvm_ledger;
        app_request.logger = &logger;
        app_request.diagnostics = diagnostic_state;
        app_request.configure_dex_vm =
            [&, dex_context](runtime::dexvm::Interpreter& vm) {
                vm.Monitors().SetTimeSource([dex_context] {
                    return dex_context->uptime_millis.load();
                });
                if (survey_gaps_output.has_value()) {
                    vm.Linker().EnableGapSurvey();
                    Write("OGPlay: GAP SURVEY RUN — unresolved platform classes "
                          "and methods answer neutrally and are recorded. This is "
                          "a diagnostic work queue, not a compatibility result.\n");
                    logger.Write(core::LogLevel::warn, "frontend.run_apk",
                                 "gap survey enabled; run is diagnostic only",
                                 {}, {}, kUnrestrictedLog);
                }
                session::ApplyProfileStaticPresets(profile, vm, &logger);
            };
        app_request.host.flush_persistent_state = [&filesystem] {
            session::FlushProfileVfsAtLifecycleBoundary(filesystem);
        };
        app_request.host.pump_host_events = pump_host_events;
        app_request.host.before_process_stop = [&] {
            call_progress.Begin(active_frame, 0U);
        };
        auto app_process = session::AndroidAppProcess::Create(
            std::move(app_request));
        std::shared_ptr<runtime::debug::DiagCoordinator> diagnostic_coordinator;
        if (diagnostics_enabled && diagnostic_state) {
            diagnostic_coordinator = runtime::debug::DiagCoordinator::Start(
                diagnostic_state,
                {.output_directory = diagnostic_directory,
                 .teardown_timeout = diagnostic_teardown_timeout,
                 .logger = &logger});
            Write("OGPlay: diagnostics ready; control=" +
                  PathUtf8(diagnostic_coordinator->ControlFile()) + "\n");
            if (mcp_session) {
                std::weak_ptr<runtime::debug::DiagCoordinator> weak =
                    diagnostic_coordinator;
                mcp_session->SetDiagnosticSnapshotHandler([weak] {
                    const auto coordinator = weak.lock();
                    if (!coordinator) return std::optional<std::string>{};
                    const auto files = coordinator->RequestSnapshotAndWait(
                        "mcp", std::chrono::seconds(5));
                    return files
                        ? std::optional<std::string>{PathUtf8(files->json)}
                        : std::optional<std::string>{};
                });
            }
        }
        // Stop/join the HTTP worker before app_process unbinds diagnostic providers.
        // All Dashboard sources below outlive this scope-owned transport.
        // The channel outlives the server so no worker waits on a destroyed queue.
        auto control_channel = std::make_shared<agent::ControlEditorChannel>();
        bool control_frozen{};
        std::unique_ptr<McpHttpServer> mcp_server;
        // The launcher passes its token through the environment (never argv); a bare CLI run
        // gets a fresh one. The editor page receives it in the URL fragment, which browsers
        // never send to the server.
        std::string control_token;
        const auto inherited_token = hal::HostEnvironmentValue("OGPLAY_CONTROL_TOKEN");
        const bool generated_token = !inherited_token || inherited_token->empty();
        if (generated_token) {
            std::array<std::byte, 16> entropy{};
            hal::FillSecureRandom(entropy);
            control_token = core::EncodeHex(entropy, core::HexCase::lower);
        } else {
            control_token = *inherited_token;
        }
        // The editor page is optional: without built assets /controls/ answers 404 and the RPC stays usable.
        const auto control_assets = bundled_data.root / "webui" / "controls";
        std::error_code control_assets_error;
        const bool control_page = std::filesystem::is_directory(control_assets, control_assets_error);
        const auto control_http = [&] {
            return ControlHttpConfig{control_channel, control_token,
                                     control_page ? control_assets : std::filesystem::path{}};
        };
        std::optional<std::string> editor_url;
        const auto announce_editor = [&] {
            editor_url = "http://127.0.0.1:" + std::to_string(mcp_server->Port()) + "/controls/";
            if (!control_page) Write("OGPlay: key editor page missing (build webui); RPC at " + *editor_url + "rpc" +
                                     (generated_token ? " token=" + control_token : std::string{}) + "\n");
            else if (generated_token) Write("OGPlay: key editor at " + *editor_url + "#token=" + control_token + "\n");
            else Write("OGPlay: key editor at " + *editor_url + " (token from launcher)\n");
        };
        if (mcp_port) {
            agent::DashboardSources sources;
            sources.metadata = {{"package", manifest.package}, {"installation_id", sandbox.installation_id},
                {"profile", selected_profile ? profile.identity.package : "generic"},
                {"api_level", std::to_string(profile.runtime.api_level)}, {"supersample", std::to_string(supersample_factor)},
                {"mode", mcp_manual_step ? "manual-step" : "free-running"},
                {"ffmpeg_available", video::FfmpegAvailable() ? "true" : "false"},
                {"ffmpeg_reason", video::FfmpegUnavailableReason()}};
            sources.diagnostics = diagnostic_state.get(); sources.session = mcp_session.get();
            sources.logger = &logger; sources.ledger = &dexvm_ledger;
            sources.dexvm = [&] { return app_process->DexVm().Vm().TrySnapshot(); };
            sources.jni = [&] { return app_process->NativeProcess().Environment().TrySnapshot(); };
            sources.memory = [&] { return app_process->NativeProcess().TryMemorySnapshot(); };
            sources.cpu = [&] { return app_process->NativeProcess().TryCpuSnapshot(); };
            sources.gpu_errors_available = true; // Per-call boundary-reported GL errors.
            sources.gpu = [&] { return app_process->NativeProcess().TryStats(); };
            sources.filesystem = [&] { return filesystem.TrySnapshot(); };
            sources.audio = [&] { return runtime::TrySnapshotAndroidAudioTracks(app_process->DexVm().Vm(), *app_process->Context()); };
            sources.ui = [&] { return runtime::TrySnapshotAndroidUi(app_process->DexVm().Vm(), *app_process->Context()); };
            sources.video = [&] { return runtime::TrySnapshotAndroidVideo(*app_process->Context()); };
            if (app_process->NativeLibraries()) sources.libraries = [&] { return app_process->NativeLibraries()->TrySnapshot(); };
            DashboardHttpConfig dashboard{
                std::make_shared<agent::DashboardService>(std::move(sources)),
                bundled_data.root / "webui" / "dashboard"};
            if (mcp_manual_step || diagnostics_enabled)
                mcp_server = McpHttpServer::Start(*mcp_port, *mcp_frames, *mcp_inputs, *mcp_session, std::move(dashboard),
                                                  control_http());
            else mcp_server = McpHttpServer::Start(*mcp_port, *mcp_frames, *mcp_inputs, std::move(dashboard),
                                                   control_http());
            Write("OGPlay: MCP ready at " + mcp_server->Endpoint() + "\n");
            Write("OGPlay: Dashboard ready at http://127.0.0.1:" + std::to_string(mcp_server->Port()) + "/dash/\n");
            announce_editor();
        } else if (key_editor_port) {
            mcp_server = McpHttpServer::StartKeyEditor(*key_editor_port, *mcp_frames, *mcp_inputs, control_http());
            announce_editor();
        }
        auto* guest = &app_process->NativeProcess();
        auto* dex_lifecycle = &app_process->ActivityLifecycle();
        dex_lifecycle->SetRealtimeVideoClock(!mcp_manual_step);
        {
            driver = {[&] {
                          app_process->StartApplication();
                          return app_process->StartLauncherActivity();
                      },
                      [&] { return dex_lifecycle->Suspend(); },
                      [&] { return dex_lifecycle->Resume(); },
                      [&] { return dex_lifecycle->StepFrame(); },
                      [&](const session::DexProcessStopReason reason) { return app_process->Stop(reason); },
                      [&] { return dex_lifecycle->State(); },
                      [&](const runtime::AndroidBoundaryInput& input) {
                          dex_lifecycle->QueueInput(input);
                      }, [&] { dex_lifecycle->CancelInput(); }};
        }
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "starting Profile lifecycle", {}, {},
                     kUnrestrictedLog);
        static_cast<void>(driver.start());
        std::unique_ptr<session::AudioOutputPump> audio_pump;
        if (audio_output) {
            audio_pump = std::make_unique<session::AudioOutputPump>(
                [guest](const std::span<std::int16_t> output,
                        const std::uint32_t rate) {
                    return guest->RenderStereoAudio(output, rate);
                },
                audio_output.get(), kDesktopAudioOutputSpec.sample_rate,
                kDesktopAudioOutputSpec.channels,
                [guest] {
                    static_cast<void>(
                        guest->PcmPlayback().InterruptBlockingWaits());
                });
            if (!mcp_manual_step) audio_pump->StartRealtime();
        }
        agent::McpLifecycleState mcp_lifecycle{
            agent::McpLifecycleState::running};
        std::optional<std::string> guest_fault;
        std::exception_ptr failure;
        std::uint64_t permitted_steps{};
        std::uint64_t last_audio_ticks = driver.state().clock_ticks;
        const auto publish_session = [&] {
            if (!mcp_session) return;
            const auto state = driver.state();
            agent::McpSessionSnapshot snapshot{
                .lifecycle = mcp_lifecycle,
                .frame = state.frame,
                .guest_ticks = state.clock_ticks,
                .process_exit = guest->ExitRequested(),
                .guest_fault = guest_fault,
                .successful_presents = presented,
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
        bool host_background = false;
        bool host_suspended_session = false;
        publish_session();
        Write(mcp_manual_step
                  ? "OGPlay: APK guest execution started in MCP manual-step mode.\n"
                  : "OGPlay: APK guest execution started; close the window to stop.\n");
        // F1 hints are drawn on a host copy; the guest frame and MCP/editor snapshots stay clean.
        HostShortcutFilter host_keys;
        bool hints_visible{};
        std::vector<std::uint8_t> hint_frame;
        // F2: a launcher that polled control.status recently opens its own editor window;
        // otherwise the default browser opens the same page.
        constexpr std::uint64_t kLauncherPollWindowNs = 3'000'000'000ULL;
        constexpr std::uint64_t kEditorIdleResumeNs = 8'000'000'000ULL;
        std::uint64_t edit_requests{}, launcher_seen_ns{}, editor_seen_ns{};
        const auto open_key_editor = [&] {
            try {
                if (!mcp_server) {
                    mcp_server = McpHttpServer::StartKeyEditor(0, *mcp_frames, *mcp_inputs, control_http());
                    announce_editor();
                }
                const auto now = hal::Clock::SteadyTimestampNs();
                if (launcher_seen_ns != 0 && now - launcher_seen_ns < kLauncherPollWindowNs) {
                    ++edit_requests;
                    logger.Write(core::LogLevel::info, "frontend.run_apk.key_editor", "key editor requested from the launcher",
                                 {}, {{"requests", edit_requests}}, kUnrestrictedLog);
                    return;
                }
                hal::OpenUrlInBrowser(*editor_url + "#token=" + control_token);
                logger.Write(core::LogLevel::info, "frontend.run_apk.key_editor", "key editor opened in the browser",
                             {}, {}, kUnrestrictedLog);
            } catch (const std::exception& error) {
                logger.Write(core::LogLevel::error, "frontend.run_apk.key_editor", "cannot open the key editor",
                             {}, {{"reason", std::string(error.what())}}, kUnrestrictedLog);
                Write(std::string("OGPlay: cannot open the key editor: ") + error.what() + "\n");
            }
        };
        const auto present_latest = [&] {
            bool did_present = false;
            if (auto frame = guest->TakeLatestFrame(); frame.has_value()) {
                *frame = dex_lifecycle->ComposePresentedFrame(
                    std::move(*frame));
                guest_width = frame->width;
                guest_height = frame->height;
                control_mapper.SetSurface(guest_width, guest_height);
                if (hints_visible) {
                    hint_frame.assign(frame->rgba8.begin(), frame->rgba8.end());
                    DrawControlHints(hint_frame, frame->width, frame->height, control_mapper.Hints(),
                                     control_mapper.Mode());
                    window->PresentRgba8(hint_frame, frame->width, frame->height);
                } else {
                    window->PresentRgba8(frame->rgba8, frame->width, frame->height);
                }
                ++presented;
                did_present = true;
                update_frame_rate();
                PublishPresentedFrame(mcp_frames.get(), std::move(*frame), *guest);
                if (exit_after_frames.has_value() &&
                    presented >= *exit_after_frames) {
                    quit = true;
                }
            }
            return did_present;
        };
        while (!quit && !guest->ExitRequested() &&
               !(dex_context &&
                 runtime::SessionExitRequested(*dex_context))) {
            for (const auto& event : window->PollEvents()) {
                using Type = hal::InputEventType;
                if (event.type == Type::quit) {
                    quit = true;
                } else if (event.type == Type::input_reset || event.type == Type::app_background) {
                    host_keys.Reset();
                    control_mapper.Reset();
                    mcp_pointer.Cancel(mcp_inputs.get());
                    if (!failure) driver.cancel_input();
                    if (event.type == Type::app_background) {
                        host_background = true;
                        if (!failure && mcp_lifecycle == agent::McpLifecycleState::running) {
                            static_cast<void>(driver.suspend());
                            if (audio_pump) audio_pump->SetSuspended(true);
                            mcp_lifecycle = agent::McpLifecycleState::suspended;
                            host_suspended_session = true;
                            publish_session();
                        }
                    }
                } else if (event.type == Type::app_foreground) {
                    host_background = false;
                    if (!failure && host_suspended_session) {
                        static_cast<void>(driver.resume());
                        if (audio_pump) audio_pump->SetSuspended(false);
                        mcp_lifecycle = agent::McpLifecycleState::running;
                        host_suspended_session = false;
                        publish_session();
                    }
                } else if (const auto decision = host_keys.Filter(event); decision.consumed) {
                    if (decision.shortcut == HostShortcut::toggle_hints) {
                        hints_visible = !hints_visible;
                        logger.Write(core::LogLevel::info, "frontend.run_apk.key_hints", "key hints toggled",
                                     {}, {{"visible", hints_visible}}, kUnrestrictedLog);
                    } else if (decision.shortcut == HostShortcut::open_key_editor) {
                        open_key_editor();
                    }
                } else if (mcp_lifecycle == agent::McpLifecycleState::suspended || failure || control_frozen) {
                    continue;
                } else {
                    control_mapper.Accept(event, window->State());
                }
            }
            control_channel->ServePending([&](const std::string_view method, const core::JsonValue params) {
                return HandleControlRequest(method, params, {
                    .mapper = control_mapper,
                    .frozen = control_frozen,
                    .user = user_controls,
                    .profile_input = profile.input.has_value() ? &*profile.input : nullptr,
                    .templates_directory = bundled_data.input_templates_directory,
                    .config_path = input_config_path,
                    .frames = mcp_frames.get(),
                    .surface_width = guest_width,
                    .surface_height = guest_height,
                    .freeze = [&] {
                        control_mapper.Reset();
                        mcp_pointer.Cancel(mcp_inputs.get());
                        if (!failure) driver.cancel_input();
                    },
                    .deliver = [&](const std::vector<input::ControlOutput>& outputs) {
                        for (const auto& output : outputs)
                            if (const auto input = session::MapControlOutput(output)) driver.queue_input(*input);
                    },
                    .edit_requests = edit_requests,
                    .seen = [&](const bool launcher) {
                        (launcher ? launcher_seen_ns : editor_seen_ns) = hal::Clock::SteadyTimestampNs();
                    },
                });
            });
            // An editor page polls control.status while open; a window closed without
            // control.resume (crash, killed browser) must not leave the game frozen.
            if (control_frozen && hal::Clock::SteadyTimestampNs() - editor_seen_ns > kEditorIdleResumeNs) {
                control_frozen = false;
                logger.Write(core::LogLevel::warn, "frontend.run_apk.key_editor",
                             "key editor stopped responding; resuming the game", {}, {}, kUnrestrictedLog);
            }
            bool frame_presented = false;
            try {
                if (audio_pump) audio_pump->RethrowWorkerFailure();
                if (mcp_session) {
                    if (const auto command = mcp_session->TakeNextCommand(); command) {
                        using Command = agent::McpSessionCommand;
                        if (command->type == Command::Type::shutdown) {
                            quit = true;
                        } else if (!failure && command->type == Command::Type::step) {
                            permitted_steps += command->frames;
                        } else if (!failure && command->type == Command::Type::suspend) {
                            control_mapper.Reset();
                            mcp_pointer.Cancel(mcp_inputs.get());
                            if (mcp_lifecycle == agent::McpLifecycleState::running)
                                static_cast<void>(driver.suspend());
                            host_suspended_session = false;
                            if (audio_pump) audio_pump->SetSuspended(true);
                            mcp_lifecycle = agent::McpLifecycleState::suspended;
                            publish_session();
                        } else if (!failure && command->type == Command::Type::resume) {
                            if (host_background) host_suspended_session = true;
                            else {
                                if (mcp_lifecycle == agent::McpLifecycleState::suspended)
                                    static_cast<void>(driver.resume());
                                if (audio_pump) audio_pump->SetSuspended(false);
                                mcp_lifecycle = agent::McpLifecycleState::running;
                            }
                            publish_session();
                        }
                    }
                }
                const bool advance = !failure && !control_frozen &&
                    mcp_lifecycle == agent::McpLifecycleState::running &&
                    (!mcp_manual_step || permitted_steps != 0U);
                if (!advance || quit) {
                    if (!quit && !failure && !mcp_manual_step) {
                        static_cast<void>(present_latest());
                        publish_session();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                mcp_pointer.Pump(mcp_inputs.get(), control_mapper);
                for (const auto& output : control_mapper.Flush()) {
                    if (const auto input = session::MapControlOutput(output)) driver.queue_input(*input);
                }
                active_frame = driver.state().frame + 1U;
                static_cast<void>(driver.step());
                const auto stepped = driver.state();
                if (mcp_manual_step && audio_pump) {
                    const auto tick_delta = stepped.clock_ticks - last_audio_ticks;
                    auto frames = audio_pump->FramesForTicks(
                        tick_delta, dex_lifecycle->TicksPerSecond());
                    std::vector<std::int16_t> offline_audio;
                    while (frames != 0U) {
                        const auto chunk = std::min<std::uint64_t>(frames, 1024U);
                        offline_audio.assign(static_cast<std::size_t>(chunk) * 2U,
                                             0);
                        static_cast<void>(audio_pump->MixOffline(offline_audio));
                        frames -= chunk;
                    }
                    last_audio_ticks = stepped.clock_ticks;
                }
                if (mcp_manual_step) --permitted_steps;
                if (!mcp_manual_step && dex_context &&
                    runtime::AnyVideoPlaying(*dex_context)) {
                    constexpr std::uint64_t kVideoFramePeriodNs = 16'000'000U;
                    const auto now = hal::Clock::SteadyTimestampNs();
                    if (video_pace_deadline_ns + kVideoFramePeriodNs < now) {
                        video_pace_deadline_ns = now;
                    }
                    video_pace_deadline_ns += kVideoFramePeriodNs;
                    if (video_pace_deadline_ns > now) std::this_thread::sleep_for(
                        std::chrono::nanoseconds(video_pace_deadline_ns - now));
                } else {
                    video_pace_deadline_ns = 0;
                }
                frame_presented = present_latest();
                const auto stats = guest->Stats();
                LogProfileFrameProgress(
                    logger, stepped.frame, stepped.clock_ticks,
                    presented, stats.draws, stats.clears);
                publish_session();
            } catch (const std::exception& error) {
                if (!failure) {
                    failure = std::current_exception();
                    try {
                        logger.Write(core::LogLevel::error, "frontend.run_apk.fault",
                                     error.what(), {.frame = driver.state().frame}, {}, kUnrestrictedLog);
                    } catch (...) { /* logging must not replace the original fault */ }
                }
                guest_fault = error.what();
                mcp_lifecycle = agent::McpLifecycleState::failed;
                permitted_steps = 0U;
                publish_session();
                if (!mcp_manual_step) quit = true;
            }
            if (ShouldIdleSleepAfterFrameStep(frame_presented)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        control_channel->Close();
        ReleaseCapturedFrame(mcp_frames.get(), *guest);
        if (audio_pump) audio_pump->Stop();
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "stopping Profile lifecycle",
                     {.frame = driver.state().frame}, {}, kUnrestrictedLog);
        try {
            static_cast<void>(driver.stop(failure ? session::DexProcessStopReason::runtime_failure
                                                 : session::DexProcessStopReason::host_shutdown));
            if (!failure) mcp_lifecycle = agent::McpLifecycleState::stopped;
        } catch (const std::exception& error) {
            logger.Write(core::LogLevel::error, "frontend.run_apk.teardown",
                         error.what(), {}, {}, kUnrestrictedLog);
            if (!failure) failure = std::current_exception();
            if (!guest_fault) guest_fault = error.what();
            mcp_lifecycle = agent::McpLifecycleState::failed;
        }
        if (audio_output) audio_output->Stop();
        publish_session();
        if (survey_gaps_output.has_value()) {
            // Written before rethrowing: a survey run usually ends on the
            // first gap that no stub can paper over, and that harvest is
            // exactly what the next batch needs.
            const auto hits = app_process->DexVm().Linker().GapSurveyHits();
            std::ofstream report(*survey_gaps_output, std::ios::binary);
            report << runtime::dexvm::RenderGapSurveyJson(
                hits, PathUtf8(apk_path.filename()));
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
