#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
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

    template <std::uint16_t FunctionId>
    std::uint32_t ExecuteExport(const A32CallFrame& call);

    void SetError(std::uint64_t thread_id, std::uint32_t error);
    [[nodiscard]] std::uint32_t TakeError(std::uint64_t thread_id);
    [[nodiscard]] std::uint32_t PublishQueryString(
        std::uint32_t name, std::uint64_t thread_id);
    [[nodiscard]] std::uint32_t ResolveProcAddress(
        GuestCString name, std::uint64_t thread_id) const;

    BoundaryCallServices& calls_;
    EglBoundaryContext& context_;
    std::mutex mutex_;
    std::map<std::uint64_t, ThreadState> threads_;
    bool initialized_{};
    bool strings_mapped_{};
    std::int32_t swap_interval_{1};
    std::uint32_t pbuffer_width_{};
    std::uint32_t pbuffer_height_{};
    std::atomic<bool> guest_graphics_retired_{false};
};

}  // namespace ogplay::runtime
