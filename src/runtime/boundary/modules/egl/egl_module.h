#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>

#include "ogplay/runtime/boundary/boundary_symbol.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/egl/egl_exports.h"
#include "runtime/boundary/services/graphics_boundary_context.h"

namespace ogplay::runtime {

struct EglBoundaryContext final {
    GraphicsBoundaryContext& graphics;
    std::span<const BionicHleSymbol> symbols;
};

class EglModule final {
public:
    EglModule(BoundaryCallServices& calls,
              EglBoundaryContext& context) noexcept;
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void RetireGuestGraphics() noexcept;

#define OGPLAY_DECLARE_EGL(name, id, count, method) \
    std::uint32_t method(const A32CallFrame& call);
    OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DECLARE_EGL)
#undef OGPLAY_DECLARE_EGL

private:
    struct ThreadState final {
        std::uint32_t error{0x3000U};
        std::uint32_t bound_api{0x30A0U};
        std::uint32_t display{};
        std::uint32_t draw_surface{};
        std::uint32_t read_surface{};
        std::uint32_t context{};
    };

    struct ContextState final {
        std::uint32_t display{};
        std::uint32_t config{};
        std::uint32_t client_version{2U};
        std::uint32_t share_context{};
        std::optional<std::uint64_t> current_thread;
        bool destroy_pending{};
    };

    enum class SurfaceKind : std::uint8_t { window, pbuffer };
    struct SurfaceState final {
        std::uint32_t display{};
        std::uint32_t config{};
        SurfaceKind kind{SurfaceKind::window};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t swap_interval{1U};
        std::uint32_t current_count{};
        bool destroy_pending{};
    };

    template <std::uint16_t FunctionId>
    std::uint32_t ExecuteExport(const A32CallFrame& call);

    void SetError(std::uint64_t thread_id, std::uint32_t error);
    [[nodiscard]] std::uint32_t TakeError(std::uint64_t thread_id);
    [[nodiscard]] std::uint32_t PublishQueryString(
        std::uint32_t name, std::uint64_t thread_id);
    [[nodiscard]] std::uint32_t ResolveProcAddress(
        GuestCString name, std::uint64_t thread_id) const;
    void CollectRetiredObjectsLocked();

    BoundaryCallServices& calls_;
    EglBoundaryContext& context_;
    std::mutex mutex_;
    std::map<std::uint64_t, ThreadState> threads_;
    std::map<std::uint32_t, ContextState> contexts_;
    std::map<std::uint32_t, SurfaceState> surfaces_;
    bool initialized_{};
    bool strings_mapped_{};
    std::uint32_t next_surface_{3U};
    std::uint32_t next_context_{4U};
    std::atomic<bool> guest_graphics_retired_{false};
};

}  // namespace ogplay::runtime
