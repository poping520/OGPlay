#include "run_apk.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/hal/window_input.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/integration/native_activity_runner.h"

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

std::uint64_t ParsePositive(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument(std::string(option) + " requires a positive integer");
    }
    return value;
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

void ForwardInput(runtime::NativeActivitySession& guest, const hal::InputEvent& event) {
    std::optional<runtime::AndroidBoundaryInputType> type;
    if (event.type == hal::InputEventType::key) type = runtime::AndroidBoundaryInputType::key;
    if (event.type == hal::InputEventType::pointer_motion) {
        type = runtime::AndroidBoundaryInputType::pointer_motion;
    }
    if (event.type == hal::InputEventType::pointer_button) {
        type = runtime::AndroidBoundaryInputType::pointer_button;
    }
    if (type.has_value()) {
        guest.PushInput({*type, event.code, event.x, event.y, event.pressed});
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
    std::optional<std::uint64_t> exit_after_frames;
    for (int index = 3; index < argc; ++index) {
        const std::string_view option{argv[index]};
        if (option == "--system-dir" && index + 1 < argc) {
            system_directory = std::filesystem::path{argv[++index]};
        } else if (option == "--exit-after-frames" && index + 1 < argc) {
            exit_after_frames = ParsePositive(argv[++index], option);
        } else {
            throw std::invalid_argument("unknown or incomplete run-apk option: " +
                                        std::string(option));
        }
    }
    if (!system_directory.has_value()) {
        throw std::invalid_argument("run-apk requires --system-dir with API 19 Bionic libraries");
    }

    const auto apk_bytes = ReadBytes(apk_path);
    const auto archive = loader::ParseApkArchive(apk_bytes);
    const loader::ApkEntry* native_entry{};
    for (const auto& entry : archive.entries) {
        if (!entry.name.starts_with("lib/armeabi-v7a/") || !entry.name.ends_with(".so")) continue;
        if (native_entry != nullptr) {
            throw std::runtime_error(
                "APK has multiple armeabi-v7a libraries; explicit selection is not implemented");
        }
        native_entry = &entry;
    }
    if (native_entry == nullptr) throw std::runtime_error("APK has no armeabi-v7a native library");
    const auto payload = loader::ReadStoredApkEntry(apk_bytes, archive, native_entry->name);
    const auto libm = ReadBytes(*system_directory / "libm.so");
    const auto libdl = ReadBytes(*system_directory / "libdl.so");
    const auto libc = ReadBytes(*system_directory / "libc.so");
    const auto root_name = std::filesystem::path(native_entry->name).filename().string();
    const loader::Elf32ModuleInput modules[]{
        {root_name, payload, memory::GuestAddress{0x10000000U}},
        {"libm.so", libm, memory::GuestAddress{0x20000000U}},
        {"libdl.so", libdl, memory::GuestAddress{0x30000000U}},
        {"libc.so", libc, memory::GuestAddress{0x40000000U}},
    };

    auto window = hal::CreateSdlWindowInput();
    window->Open({.title = "OGPlay · " + apk_path.filename().string(),
                  .width = 640, .height = 360, .hidden = false, .resizable = true});
    auto guest = runtime::NativeActivitySession::Start(
        {19, root_name, modules, NativeBackend(), 640, 360, UINT64_C(200000000), {}});
    Write("OGPlay: NativeActivity started; close the window to stop.\n");

    bool quit{};
    std::uint64_t presented{};
    while (!quit) {
        for (const auto& event : window->PollEvents()) {
            if (event.type == hal::InputEventType::quit) quit = true;
            else ForwardInput(*guest, event);
        }
        if (auto frame = guest->TakeLatestFrame(); frame.has_value()) {
            window->PresentRgba8(frame->rgba8, frame->width, frame->height);
            ++presented;
            if (exit_after_frames.has_value() && presented >= *exit_after_frames) quit = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    guest->Stop(); window->Close();
    Write("OGPlay: stopped after " + std::to_string(presented) + " presented frames.\n");
    return 0;
}

}  // namespace ogplay::frontend
