#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/runtime/integration/native_activity_runner.h"

namespace {

std::optional<std::filesystem::path> EnvironmentPath(const char* name) {
#if defined(_WIN32)
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return std::nullopt;
    const std::filesystem::path result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::filesystem::path{value};
#endif
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot read NativeActivity test input");
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("NativeActivity test input is empty");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0); input.read(reinterpret_cast<char*>(result.data()), size);
    if (!input) throw std::runtime_error("NativeActivity test input was truncated");
    return result;
}

std::optional<ogplay::runtime::AndroidBoundaryFrame> WaitFrame(
    ogplay::runtime::NativeActivitySession& session, const std::uint64_t after = 0) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        auto frame = session.TakeLatestFrame();
        if (frame.has_value() && frame->sequence > after) return frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("minimal APK NativeActivity renders and responds to guest input") {
    const auto oracle = EnvironmentPath("OGPLAY_BIONIC_ORACLE_ROOT");
    const auto apk_path = EnvironmentPath("OGPLAY_MINIMAL_NDK_APK");
    if (!oracle.has_value() || !apk_path.has_value()) return;

    const auto apk_bytes = ReadBytes(*apk_path);
    const auto archive = ogplay::loader::ParseApkArchive(apk_bytes);
    const auto payload = ogplay::loader::ReadStoredApkEntry(
        apk_bytes, archive, "lib/armeabi-v7a/libogplay_minimal_ndk.so");
    const auto directory = *oracle / "api19" / "lib";
    const auto libm = ReadBytes(directory / "libm.so");
    const auto libdl = ReadBytes(directory / "libdl.so");
    const auto libc = ReadBytes(directory / "libc.so");
    const ogplay::loader::Elf32ModuleInput modules[]{
        {"libogplay_minimal_ndk.so", payload, ogplay::memory::GuestAddress{0x10000000U}},
        {"libm.so", libm, ogplay::memory::GuestAddress{0x20000000U}},
        {"libdl.so", libdl, ogplay::memory::GuestAddress{0x30000000U}},
        {"libc.so", libc, ogplay::memory::GuestAddress{0x40000000U}},
    };
    auto session = ogplay::runtime::NativeActivitySession::Start({
        19, "libogplay_minimal_ndk.so", modules,
        {ogplay::gles::AngleRenderer::d3d11, ogplay::gles::AngleDevice::hardware},
        64, 36, UINT64_C(200000000), {}});
    REQUIRE(session->Running());
    const auto initial = WaitFrame(*session);
    REQUIRE(initial.has_value());
    session->PushInput({ogplay::runtime::AndroidBoundaryInputType::key,
                        29, 0, 0, true});
    std::optional<ogplay::runtime::AndroidBoundaryFrame> changed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !changed.has_value()) {
        auto candidate = WaitFrame(*session, initial->sequence);
        if (candidate.has_value() && candidate->rgba8 != initial->rgba8) {
            changed = std::move(candidate);
        }
    }
    REQUIRE(changed.has_value());
    CHECK(changed->rgba8 != initial->rgba8);

    const ogplay::core::GpuStateProvider& gpu = *session;
    const auto stats = gpu.Stats();
    CHECK(stats.draws == 0);
    CHECK(stats.clears >= 2);
    CHECK(stats.shader_compiles == 0);
    CHECK(stats.program_links == 0);
    CHECK(stats.gl_errors == 0);
    REQUIRE(stats.draw_targets.size() == 1);
    CHECK(stats.draw_targets[0].fbo == 0);
    CHECK(stats.draw_targets[0].attachment == "color0");

    const auto targets = gpu.RenderTargets();
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].fbo == 0);
    CHECK(targets[0].width == 64);
    CHECK(targets[0].height == 36);
    CHECK(targets[0].format == "RGBA8");
    CHECK_FALSE(targets[0].created_by_guest);

    const auto capabilities = gpu.Capabilities();
    CHECK(capabilities.host_backend == "d3d11/hardware");
    CHECK(capabilities.reported_extensions.empty());
    CHECK(capabilities.reported_limits.empty());

    const auto clear_trace = gpu.Trace("glClear", 4);
    REQUIRE_FALSE(clear_trace.empty());
    CHECK(clear_trace.size() <= 4);
    CHECK(clear_trace.back().call == "glClear");
    CHECK(clear_trace.back().arguments.contains("r0"));
    const auto bounded_trace = gpu.Trace("", 3);
    CHECK(bounded_trace.size() == 3);
    session->Stop();
    CHECK_FALSE(session->Running());
    CHECK(session->RenderTargets().empty());
}
