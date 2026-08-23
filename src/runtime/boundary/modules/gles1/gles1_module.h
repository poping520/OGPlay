#pragma once

#include <cstdint>
#include <mutex>
#include <stdexcept>

#include "ogplay/gles/gles_dispatch.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_fixed.h"
#include "runtime/boundary/services/graphics_boundary_context.h"

namespace ogplay::runtime {

class Gles1Module final {
public:
    Gles1Module(BoundaryCallServices& calls,
                GraphicsBoundaryContext& graphics,
                detail::AndroidBoundaryGles1DrawState& draw_state,
                gles::GlesDispatchTable& core_dispatch,
                gles::GlesDispatchTable& extension_dispatch) noexcept
        : calls_(calls), graphics_(graphics), draw_state_(draw_state),
          core_dispatch_(core_dispatch),
          extension_dispatch_(extension_dispatch) {}

    [[nodiscard]] BoundaryCallServices& CallServices() noexcept { return calls_; }

    template <gles::GlesApi Api, gles::GlesThunkId Id>
    std::uint32_t Invoke(const A32CallFrame& call) {
        constexpr bool draw_call = Api == gles::GlesApi::gles1 &&
                                   (Id == 35U || Id == 36U);
        const auto symbol = gles::DescribeGlesFunction(Api, Id).name;
        if constexpr (draw_call) {
            if (graphics_.gl_context.SelectDrawRenderer(
                    draw_state_
                        .Array(detail::kGles1VertexArray, 0x84C0U).enabled,
                    graphics_.gles_dispatch.HasEnabledVertexAttribute()) ==
                GuestGlRenderer::programmable) {
                const auto result = graphics_.gles_dispatch.Dispatch(
                    Id == 35U ? 40U : 41U, call,
                    graphics_.angle_frame.has_value()
                        ? &*graphics_.angle_frame : nullptr);
                if (!result.has_value()) {
                    throw std::logic_error(
                        "selected programmable draw has no GLES2 handler");
                }
                graphics_.frames.RecordDraw();
                return *result;
            }
        }
        auto& dispatch = Api == gles::GlesApi::gles1
                             ? core_dispatch_ : extension_dispatch_;
        if constexpr (!draw_call) {
            return dispatch.Invoke(Id, call.Arguments(), call.ThreadId());
        }
        graphics_.gl_context.Native().BeginFixedDraw();
        try {
            const auto result = dispatch.Invoke(
                Id, call.Arguments(), call.ThreadId());
            graphics_.gles_dispatch.RestoreNativeState(
                graphics_.RequireFrame(symbol));
            graphics_.gl_context.Native().EndFixedDraw();
            return result;
        } catch (...) {
            try {
                graphics_.gles_dispatch.RestoreNativeState(
                    graphics_.RequireFrame(symbol));
                graphics_.gl_context.Native().EndFixedDraw();
            } catch (...) {
                graphics_.gl_context.Native().Reset();
            }
            throw;
        }
    }

private:
    BoundaryCallServices& calls_;
    GraphicsBoundaryContext& graphics_;
    detail::AndroidBoundaryGles1DrawState& draw_state_;
    gles::GlesDispatchTable& core_dispatch_;
    gles::GlesDispatchTable& extension_dispatch_;
};

}  // namespace ogplay::runtime
