#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "ffmpeg_api.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/hal/shared_library.h"

namespace ogplay::video::ffabi {
namespace {

struct LibrarySpec final {
    const char* base_name;
    unsigned major;
};

constexpr std::array<LibrarySpec, 5> kLibraries = {{
    {"avutil", kAvutilMajor},
    {"swresample", kSwresampleMajor},
    {"swscale", kSwscaleMajor},
    {"avcodec", kAvcodecMajor},
    {"avformat", kAvformatMajor},
}};

[[nodiscard]] std::string PlatformLibraryName(const LibrarySpec& spec) {
    return hal::SharedLibraryFileName(spec.base_name, spec.major);
}

using LibraryHandle = void*;

[[nodiscard]] LibraryHandle OpenLibrary(const std::filesystem::path& path) {
    return hal::OpenSharedLibrary(path);
}

[[nodiscard]] void* ResolveSymbol(LibraryHandle handle, const char* name) {
    return hal::ResolveSharedLibrarySymbol(handle, name);
}

// Directories to try, in order; an empty path means the system default
// search (which on Windows already includes the executable directory).
[[nodiscard]] std::vector<std::filesystem::path> SearchDirectories() {
    std::vector<std::filesystem::path> directories;
    if (const auto env = hal::HostEnvironmentValue("OGPLAY_FFMPEG_DIR");
        env.has_value() && !env->empty()) {
        directories.emplace_back(*env);
    }
    directories.push_back(hal::HostExecutableDirectory());
    directories.emplace_back();
    return directories;
}

// Opens all five libraries from a single directory or fails as a set, so a
// mixed installation can never be half-loaded.
[[nodiscard]] bool OpenAllFrom(const std::filesystem::path& directory,
                               std::array<LibraryHandle, 5>& handles) {
    for (std::size_t i = 0; i < kLibraries.size(); ++i) {
        const std::string name = PlatformLibraryName(kLibraries[i]);
        const auto path =
            directory.empty() ? std::filesystem::path(name) : directory / name;
        handles[i] = OpenLibrary(path);
        if (handles[i] == nullptr) return false;
    }
    return true;
}

struct LoaderState final {
    bool attempted = false;
    const Api* api = nullptr;
    std::string reason;
    Api storage{};
};

template <typename Fn>
[[nodiscard]] bool Resolve(LibraryHandle handle, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(ResolveSymbol(handle, name));
    return out != nullptr;
}

[[nodiscard]] bool ResolveAll(const std::array<LibraryHandle, 5>& handles,
                              Api& api, std::string& missing) {
    const LibraryHandle avutil = handles[0];
    const LibraryHandle swresample = handles[1];
    const LibraryHandle swscale = handles[2];
    const LibraryHandle avcodec = handles[3];
    const LibraryHandle avformat = handles[4];

    struct Symbol final {
        LibraryHandle handle;
        const char* name;
        void** slot;
    };
    const std::array<Symbol, 33> symbols = {{
        {avutil, "avutil_version",
         reinterpret_cast<void**>(&api.avutil_version)},
        {avcodec, "avcodec_version",
         reinterpret_cast<void**>(&api.avcodec_version)},
        {avformat, "avformat_version",
         reinterpret_cast<void**>(&api.avformat_version)},
        {swscale, "swscale_version",
         reinterpret_cast<void**>(&api.swscale_version)},
        {swresample, "swresample_version",
         reinterpret_cast<void**>(&api.swresample_version)},
        {avutil, "av_frame_alloc",
         reinterpret_cast<void**>(&api.av_frame_alloc)},
        {avutil, "av_frame_free", reinterpret_cast<void**>(&api.av_frame_free)},
        {avutil, "av_frame_unref",
         reinterpret_cast<void**>(&api.av_frame_unref)},
        {avutil, "av_opt_set", reinterpret_cast<void**>(&api.av_opt_set)},
        {avutil, "av_opt_set_int",
         reinterpret_cast<void**>(&api.av_opt_set_int)},
        {avutil, "av_get_sample_fmt_name",
         reinterpret_cast<void**>(&api.av_get_sample_fmt_name)},
        {avformat, "avformat_open_input",
         reinterpret_cast<void**>(&api.avformat_open_input)},
        {avformat, "avformat_close_input",
         reinterpret_cast<void**>(&api.avformat_close_input)},
        {avformat, "avformat_find_stream_info",
         reinterpret_cast<void**>(&api.avformat_find_stream_info)},
        {avformat, "av_find_best_stream",
         reinterpret_cast<void**>(&api.av_find_best_stream)},
        {avformat, "av_read_frame",
         reinterpret_cast<void**>(&api.av_read_frame)},
        {avformat, "av_seek_frame",
         reinterpret_cast<void**>(&api.av_seek_frame)},
        {avcodec, "av_packet_alloc",
         reinterpret_cast<void**>(&api.av_packet_alloc)},
        {avcodec, "av_packet_free",
         reinterpret_cast<void**>(&api.av_packet_free)},
        {avcodec, "av_packet_unref",
         reinterpret_cast<void**>(&api.av_packet_unref)},
        {avcodec, "avcodec_alloc_context3",
         reinterpret_cast<void**>(&api.avcodec_alloc_context3)},
        {avcodec, "avcodec_free_context",
         reinterpret_cast<void**>(&api.avcodec_free_context)},
        {avcodec, "avcodec_parameters_to_context",
         reinterpret_cast<void**>(&api.avcodec_parameters_to_context)},
        {avcodec, "avcodec_open2", reinterpret_cast<void**>(&api.avcodec_open2)},
        {avcodec, "avcodec_send_packet",
         reinterpret_cast<void**>(&api.avcodec_send_packet)},
        {avcodec, "avcodec_receive_frame",
         reinterpret_cast<void**>(&api.avcodec_receive_frame)},
        {avcodec, "avcodec_flush_buffers",
         reinterpret_cast<void**>(&api.avcodec_flush_buffers)},
        {swscale, "sws_getContext",
         reinterpret_cast<void**>(&api.sws_getContext)},
        {swscale, "sws_scale", reinterpret_cast<void**>(&api.sws_scale)},
        {swscale, "sws_freeContext",
         reinterpret_cast<void**>(&api.sws_freeContext)},
        {swresample, "swr_alloc", reinterpret_cast<void**>(&api.swr_alloc)},
        {swresample, "swr_init", reinterpret_cast<void**>(&api.swr_init)},
        {swresample, "swr_convert", reinterpret_cast<void**>(&api.swr_convert)},
    }};
    for (const auto& symbol : symbols) {
        *symbol.slot = ResolveSymbol(symbol.handle, symbol.name);
        if (*symbol.slot == nullptr) {
            missing = symbol.name;
            return false;
        }
    }
    if (!Resolve(swresample, "swr_free", api.swr_free)) {
        missing = "swr_free";
        return false;
    }
    return true;
}

[[nodiscard]] bool VersionsMatch(const Api& api, std::string& mismatch) {
    struct Check final {
        const char* name;
        unsigned expected;
        unsigned actual;
    };
    const std::array<Check, 5> checks = {{
        {"avutil", kAvutilMajor, api.avutil_version() >> 16U},
        {"avcodec", kAvcodecMajor, api.avcodec_version() >> 16U},
        {"avformat", kAvformatMajor, api.avformat_version() >> 16U},
        {"swscale", kSwscaleMajor, api.swscale_version() >> 16U},
        {"swresample", kSwresampleMajor, api.swresample_version() >> 16U},
    }};
    for (const auto& check : checks) {
        if (check.expected != check.actual) {
            mismatch = std::string(check.name) + " major " +
                       std::to_string(check.actual) + " != pinned " +
                       std::to_string(check.expected);
            return false;
        }
    }
    return true;
}

void LoadOnce(LoaderState& state) {
    state.attempted = true;
    std::array<LibraryHandle, 5> handles{};
    bool opened = false;
    for (const auto& directory : SearchDirectories()) {
        handles = {};
        if (OpenAllFrom(directory, handles)) {
            opened = true;
            break;
        }
    }
    if (!opened) {
        state.reason = "FFmpeg 7 shared libraries not found (need " +
                       PlatformLibraryName(kLibraries[0]) + " .. " +
                       PlatformLibraryName(kLibraries[4]) + ")";
        return;
    }
    std::string detail;
    if (!ResolveAll(handles, state.storage, detail)) {
        state.reason = "FFmpeg symbol missing: " + detail;
        return;
    }
    if (!VersionsMatch(state.storage, detail)) {
        state.reason = "FFmpeg version mismatch: " + detail;
        return;
    }
    state.api = &state.storage;
}

}  // namespace

const Api* LoadFfmpegApi(std::string* unavailable_reason) {
    static LoaderState state;
    static std::once_flag once;
    std::call_once(once, [] { LoadOnce(state); });
    if (unavailable_reason != nullptr) *unavailable_reason = state.reason;
    return state.api;
}

}  // namespace ogplay::video::ffabi
