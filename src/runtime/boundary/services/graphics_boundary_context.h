#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/supersample.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/services/guest_gl_context.h"
#include "runtime/boundary/services/frame_service.h"

namespace ogplay::runtime {

struct GraphicsBoundaryContext final {
    gles::AngleBackend& backend;
    gles::SupersampleLayout& layout;
    GuestGlContext& gl_context;
    AndroidBoundaryGles& gles_dispatch;
    std::optional<gles::AngleFrame>& angle_frame;
    std::optional<std::thread::id>& gl_owner;
    bool& managed_surface;
    FrameService& frames;
    void* owner{};
    gles::AngleFrame& (*require_frame)(void*, std::string_view){};
    void (*initialize_defaults)(void*){};
    void (*release_surface)(void*){};
    void (*publish_frame)(void*){};
    void (*reset_guest_graphics)(void*){};
    void (*write32)(void*, std::uint32_t, std::uint32_t, std::uint64_t){};
    void (*write_required32)(void*, std::uint32_t, std::uint32_t,
                             std::uint64_t, std::string_view){};
    std::string (*read_cstring)(void*, std::uint32_t, std::size_t,
                                std::uint64_t, std::string_view){};
    std::vector<std::string> (*read_shader_sources)(
        void*, const std::array<std::uint32_t, 4>&, std::uint64_t){};

    [[nodiscard]] gles::AngleFrame& RequireFrame(
        const std::string_view operation) const {
        return require_frame(owner, operation);
    }
    void InitializeGuestGlDefaults() const { initialize_defaults(owner); }
    void ReleaseManagedSurfaceFromCallingThread() const { release_surface(owner); }
    void PublishFrame() const { publish_frame(owner); }
    void ResetGuestGraphics() const { reset_guest_graphics(owner); }
    void Write32(const std::uint32_t address, const std::uint32_t value,
                 const std::uint64_t thread_id) const {
        write32(owner, address, value, thread_id);
    }
    void WriteRequired32(const std::uint32_t address,
                         const std::uint32_t value,
                         const std::uint64_t thread_id,
                         const std::string_view operation) const {
        write_required32(owner, address, value, thread_id, operation);
    }
    [[nodiscard]] std::string ReadCString(
        const std::uint32_t address, const std::size_t maximum_bytes,
        const std::uint64_t thread_id,
        const std::string_view operation) const {
        return read_cstring(owner, address, maximum_bytes, thread_id, operation);
    }
    [[nodiscard]] std::vector<std::string> ReadShaderSources(
        const std::array<std::uint32_t, 4>& arguments,
        const std::uint64_t thread_id) const {
        return read_shader_sources(owner, arguments, thread_id);
    }
};

}  // namespace ogplay::runtime
