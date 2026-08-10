#include "run_apk.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/agent/mcp_protocol.h"
#include "ogplay/frontend/mcp_http_server.h"
#include "ogplay/hal/audio.h"
#include "ogplay/hal/clock.h"
#include "ogplay/hal/window_input.h"
#include "ogplay/input/mouse_touch_mapper.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/integration/android_link_preflight.h"
#include "ogplay/runtime/integration/native_activity_runner.h"
#include "ogplay/session/profile_apk.h"
#include "ogplay/session/profile_audio.h"
#include "ogplay/session/profile_guest_lifecycle.h"
#include "ogplay/session/quirk_registry.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::frontend {
namespace {

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

[[nodiscard]] std::optional<runtime::FrameworkDirectAssetImplementations>
DirectAssetImplementations(const session::TitleProfile& profile) {
    runtime::FrameworkDirectAssetImplementations result;
    for (const auto& java_class : profile.java_classes) {
        for (const auto& method : java_class.methods) {
            if (method.implementation == "resource.load_full") {
                result.load_full = method.implementation;
            } else if (method.implementation == "resource.load_range") {
                result.load_range = method.implementation;
            } else if (method.implementation == "resource.length") {
                result.length = method.implementation;
            }
        }
    }
    const auto count = static_cast<unsigned>(!result.load_full.empty()) +
                       static_cast<unsigned>(!result.load_range.empty()) +
                       static_cast<unsigned>(!result.length.empty());
    if (count == 0) return std::nullopt;
    if (count != 3) {
        throw std::runtime_error(
            "Profile direct asset implementation set is incomplete");
    }
    return result;
}

[[nodiscard]] bool ProfileEnablesQuirk(
    const session::TitleProfile& profile, const std::string_view id) {
    return profile.quirks.has_value() &&
           std::ranges::find(profile.quirks->enabled, id) !=
               profile.quirks->enabled.end();
}

[[nodiscard]] const session::ProfileMount* ExternalMount(
    const session::TitleProfile& profile) {
    if (!profile.data.has_value()) return nullptr;
    const session::ProfileMount* result{};
    for (const auto& mount : profile.data->mounts) {
        if (mount.source != session::ProfileSource::external) continue;
        if (result != nullptr) {
            throw std::runtime_error(
                "run-apk supports one external Profile mount per session");
        }
        result = &mount;
    }
    return result;
}

[[nodiscard]] std::string JoinGuestPath(const std::string_view root,
                                        const std::string_view relative) {
    auto result = std::string(root);
    if (result != "/") result.push_back('/');
    result.append(relative);
    return result;
}

void MountExternalDirectory(
    const session::TitleProfile& profile,
    const std::optional<std::filesystem::path>& directory,
    runtime::VirtualFileSystem& filesystem) {
    const auto* mount = ExternalMount(profile);
    if (mount == nullptr) {
        if (directory.has_value()) {
            throw std::runtime_error(
                "--external-dir was supplied but Profile declares no external mount");
        }
        return;
    }
    if (!directory.has_value()) {
        if (mount->required) {
            throw std::runtime_error(
                "Profile requires --external-dir for guest mount " +
                mount->guest);
        }
        return;
    }
    filesystem.MountHostDirectory(mount->guest, *directory);
    for (const auto& entry : profile.data->manifest) {
        if (!entry.required) continue;
        try {
            static_cast<void>(filesystem.Stat(
                JoinGuestPath(mount->guest, entry.path)));
        } catch (const runtime::VfsError& error) {
            if (error.ErrorNumber() != 2) throw;
            throw std::runtime_error(
                "required Profile manifest file is missing: " + entry.path);
        }
    }
}

[[nodiscard]] std::vector<runtime::VfsLazyMountEntry> IndexApkAssets(
    const std::span<const std::byte> apk_bytes,
    const loader::ApkArchive& archive) {
    std::vector<runtime::VfsLazyMountEntry> result;
    for (const auto& entry : archive.entries) {
        if (!entry.name.starts_with("assets/") ||
            entry.name.size() == std::string_view{"assets/"}.size() ||
            entry.name.ends_with('/')) {
            continue;
        }
        result.push_back({
            entry.name,
            entry.uncompressed_size,
            [apk_bytes, &archive, name = entry.name] {
                return loader::ReadApkEntry(apk_bytes, archive, name);
            },
        });
    }
    if (result.empty()) {
        throw std::runtime_error(
            "Profile direct asset HLE requires APK assets");
    }
    return result;
}

[[nodiscard]] audio::JavaSoundPoolMixer::EncodedResourceLoader
SoundResourceLoader(const session::TitleProfile& profile,
                    const std::span<const std::byte> apk_bytes,
                    const loader::ApkArchive& archive) {
    if (!profile.audio.has_value() ||
        !profile.audio->sound_pool.has_value()) {
        return {};
    }
    return [&profile, apk_bytes, &archive](const std::int32_t resource) {
        const auto resolved =
            session::ResolveProfileSoundPoolPath(profile, resource);
        if (!resolved.has_value()) return std::vector<std::byte>{};
        if (resolved->source != session::ProfileSource::apk) {
            throw std::runtime_error(
                "run-apk has no mounted SoundPool source: " +
                std::string(session::ToString(resolved->source)));
        }
        return loader::ReadApkEntry(apk_bytes, archive, resolved->path);
    };
}

void PumpAudio(runtime::AndroidGuestCallSession& guest,
               hal::AudioOutput& output,
               std::vector<std::int16_t>& samples) {
    constexpr std::uint32_t kSampleRate = 48000U;
    constexpr std::uint64_t kTargetQueuedFrames = 4096U;
    constexpr std::size_t kMaximumChunksPerPump = 4U;
    for (std::size_t chunk = 0;
         chunk < kMaximumChunksPerPump &&
         output.QueuedFrames() < kTargetQueuedFrames;
         ++chunk) {
        const auto frames = guest.RenderStereoAudio(samples, kSampleRate);
        const auto sample_count = frames * 2U;
        output.Submit(std::as_bytes(
            std::span{samples}.first(sample_count)));
    }
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

int RunApkCommand(const int argc, const char* const argv[]) {
    if (argc < 5) {
        throw std::invalid_argument(
            "run-apk requires <apk> --system-dir <api19-lib-dir>");
    }
    const std::filesystem::path apk_path{argv[2]};
    std::optional<std::filesystem::path> system_directory;
    std::optional<std::filesystem::path> external_directory;
    auto profiles_directory =
        std::filesystem::path(OGPLAY_SOURCE_DIR) / "data" / "profiles";
    std::optional<std::uint64_t> exit_after_frames;
    std::optional<std::uint16_t> mcp_port;
    std::uint32_t supersample_factor{1};
    bool preflight{};
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
        } else if (option == "--preflight") {
            preflight = true;
        } else {
            throw std::invalid_argument("unknown or incomplete run-apk option: " +
                                        std::string(option));
        }
    }
    if (!system_directory.has_value()) {
        throw std::invalid_argument("run-apk requires --system-dir with API 19 Bionic libraries");
    }
    if (preflight && mcp_port.has_value()) {
        throw std::invalid_argument("--mcp-port cannot be combined with --preflight");
    }

    const auto apk_bytes = ReadBytes(apk_path);
    const auto archive = loader::ParseApkArchive(apk_bytes);
    const auto source_root = std::filesystem::path(OGPLAY_SOURCE_DIR);
    const auto quirks = session::QuirkRegistry::Load(
        source_root / "data" / "quirks.toml", source_root);
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
    runtime::VirtualFileSystem filesystem;
    MountExternalDirectory(profile, external_directory, filesystem);
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
              std::to_string(linked.relocations) + " native_calls=" +
              std::to_string(launch->native_calls.size()) + "\n");
        return 0;
    }
    if (profile.runtime.lifecycle != session::ProfileLifecycle::native_activity &&
        profile.runtime.lifecycle != session::ProfileLifecycle::gl_surface_view) {
        throw std::runtime_error(
            "profile lifecycle runner is not implemented by run-apk: " +
            std::string(session::ToString(profile.runtime.lifecycle)));
    }

    std::unique_ptr<agent::FrameSnapshotStore> mcp_frames;
    std::unique_ptr<McpHttpServer> mcp_server;
    if (mcp_port.has_value()) {
        mcp_frames = std::make_unique<agent::FrameSnapshotStore>();
        mcp_server = McpHttpServer::Start(*mcp_port, *mcp_frames);
        Write("OGPlay: MCP ready at " + mcp_server->Endpoint() + "\n");
    }

    auto window = hal::CreateSdlWindowInput();
    const auto base_title = "OGPlay · " + apk_path.filename().string();
    window->Open({.title = base_title + " · FPS --",
                  .width = profile.runtime.surface.width,
                  .height = profile.runtime.surface.height,
                  .hidden = false, .resizable = true});
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
    const auto direct_assets = DirectAssetImplementations(profile);
    input::MouseTouchMapper mouse_touch;
    if (profile.runtime.lifecycle == session::ProfileLifecycle::native_activity) {
        auto guest = runtime::NativeActivitySession::Start(
            {profile.runtime.api_level, root_name, module_inputs, NativeBackend(),
             profile.runtime.surface.width, profile.runtime.surface.height,
             profile.runtime.maximum_ticks_per_call, {}, supersample_factor});
        Write("OGPlay: NativeActivity started; close the window to stop.\n");
        while (!quit) {
            const auto window_state = window->State();
            for (const auto& event : window->PollEvents()) {
                if (event.type == hal::InputEventType::quit) {
                    quit = true;
                } else if (const auto mapped = mouse_touch.Map(
                               event, window_state, guest_width, guest_height);
                           mapped.has_value()) {
                    if (const auto input = MapInput(*mapped); input.has_value()) {
                        guest->PushInput(*input);
                    }
                }
            }
            if (auto frame = guest->TakeLatestFrame(); frame.has_value()) {
                window->PresentRgba8(frame->rgba8, frame->width, frame->height);
                guest_width = frame->width;
                guest_height = frame->height;
                ++presented;
                update_frame_rate();
                PublishPresentedFrame(mcp_frames.get(), std::move(*frame), *guest);
                if (exit_after_frames.has_value() &&
                    presented >= *exit_after_frames) {
                    quit = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ReleaseCapturedFrame(mcp_frames.get(), *guest);
        guest->Stop();
    } else {
        if (direct_assets.has_value()) {
            const auto assets = IndexApkAssets(apk_bytes, archive);
            filesystem.MountLazyReadOnly(
                runtime::VfsSource::apk, "/apk", assets);
        }
        auto sound_loader = SoundResourceLoader(profile, apk_bytes, archive);
        std::unique_ptr<hal::AudioOutput> audio_output;
        std::vector<std::int16_t> audio_samples(1024U * 2U);
        if (sound_loader) {
            audio_output = hal::CreateSdlAudioOutput(
                {48000U, 2U, hal::AudioSampleFormat::signed_16_le});
            audio_output->Start();
        }
        auto guest = runtime::AndroidGuestCallSession::Start(
            {profile.runtime.api_level, root_name, module_inputs,
             NativeBackend(), profile.runtime.surface.width,
             profile.runtime.surface.height,
             profile.runtime.maximum_ticks_per_call,
             supersample_factor, &filesystem, {}, direct_assets,
             {.allow_gles1_material_single_face = ProfileEnablesQuirk(
                  profile, "gles1_material_front_face")},
             std::move(sound_loader),
             [&window] { window->PumpEvents(); }});
        auto lifecycle = session::ProfileGuestLifecycle::Create(
            profile, launch->native_calls,
            {
                guest->GuestEnvironment(),
                &guest->Environment(),
                &guest->Classes(),
                [&guest](const runtime::A32GuestCallFrame& frame) {
                    return guest->Invoke(frame);
                },
                [&guest] { guest->OpenManagedSurface(); },
                [&guest] { guest->PresentManagedSurface(); },
                [&guest] {
                    static_cast<void>(guest->InterruptBlockingWaits());
                },
                [&guest] { guest->Stop(); },
                [&guest] { guest->CloseManagedSurface(); },
                [&guest](const runtime::AndroidBoundaryInput& input) {
                    guest->PushInput(input);
                },
            });
        static_cast<void>(lifecycle->Start());
        Write("OGPlay: Profile GLSurfaceView started; close the window to stop.\n");
        while (!quit && !guest->ExitRequested()) {
            const auto window_state = window->State();
            for (const auto& event : window->PollEvents()) {
                if (event.type == hal::InputEventType::quit) {
                    quit = true;
                } else if (const auto mapped = mouse_touch.Map(
                               event, window_state, guest_width, guest_height);
                           mapped.has_value()) {
                    if (const auto input = MapInput(*mapped); input.has_value()) {
                        lifecycle->QueueInput(*input);
                    }
                }
            }
            static_cast<void>(lifecycle->StepFrame());
            if (audio_output) {
                PumpAudio(*guest, *audio_output, audio_samples);
            }
            if (auto frame = guest->TakeLatestFrame(); frame.has_value()) {
                window->PresentRgba8(frame->rgba8, frame->width, frame->height);
                guest_width = frame->width;
                guest_height = frame->height;
                ++presented;
                update_frame_rate();
                PublishPresentedFrame(mcp_frames.get(), std::move(*frame), *guest);
                if (exit_after_frames.has_value() &&
                    presented >= *exit_after_frames) {
                    quit = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ReleaseCapturedFrame(mcp_frames.get(), *guest);
        static_cast<void>(lifecycle->Stop());
        if (audio_output) audio_output->Stop();
    }
    window->Close();
    Write("OGPlay: stopped after " + std::to_string(presented) + " presented frames.\n");
    return 0;
}

}  // namespace ogplay::frontend
