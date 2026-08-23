#pragma once

#include <cstdint>

#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/egl/egl_exports.h"
#include "runtime/boundary/services/graphics_boundary_context.h"

namespace ogplay::runtime {

class EglModule final {
public:
    EglModule(BoundaryCallServices& calls,
              GraphicsBoundaryContext& graphics) noexcept;
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;

#define OGPLAY_DECLARE_EGL(name, id, count, method) \
    std::uint32_t method(const A32CallFrame& call);
    OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DECLARE_EGL)
#undef OGPLAY_DECLARE_EGL

private:
    template <std::uint16_t FunctionId>
    std::uint32_t ExecuteExport(const A32CallFrame& call);

    BoundaryCallServices& calls_;
    GraphicsBoundaryContext& graphics_;
};

}  // namespace ogplay::runtime
