#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "ogplay/gles/gles_dispatch.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/services/graphics_boundary_context.h"

namespace ogplay::runtime {

class Gles2Module final {
public:
    Gles2Module(BoundaryCallServices& calls,
                GraphicsBoundaryContext& graphics) noexcept
        : calls_(calls), graphics_(graphics) {}

    [[nodiscard]] BoundaryCallServices& CallServices() noexcept { return calls_; }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t Invoke(const A32CallFrame& call) {
        const auto args = call.RegisterArguments();
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles2, FunctionId).name;
        if (const auto shader_program = DispatchShaderProgram<FunctionId>(call);
            shader_program.has_value()) {
            return *shader_program;
        }
        if (const auto resources = graphics_.gles_dispatch.Dispatch(
                FunctionId, call,
                graphics_.angle_frame.has_value()
                    ? &*graphics_.angle_frame : nullptr);
            resources.has_value()) {
            if constexpr (FunctionId == 40U || FunctionId == 41U) {
                graphics_.frames.RecordDraw();
            }
            return *resources;
        }
        if constexpr (FunctionId == 141U || FunctionId == 96U) {
            const std::array logical{
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]),
                std::bit_cast<std::int32_t>(args[2]),
                std::bit_cast<std::int32_t>(args[3])};
            const auto x = detail::ScaleAndroidBoundaryViewportComponent(
                std::bit_cast<std::int32_t>(args[0]), graphics_.layout.factor);
            const auto y = detail::ScaleAndroidBoundaryViewportComponent(
                std::bit_cast<std::int32_t>(args[1]), graphics_.layout.factor);
            const auto width = detail::ScaleAndroidBoundaryViewportComponent(
                std::bit_cast<std::int32_t>(args[2]), graphics_.layout.factor);
            const auto height = detail::ScaleAndroidBoundaryViewportComponent(
                std::bit_cast<std::int32_t>(args[3]), graphics_.layout.factor);
            if constexpr (FunctionId == 141U) {
                graphics_.RequireFrame(symbol).Viewport(x, y, width, height);
                graphics_.gl_context.Shared().SetViewport(logical);
            } else {
                graphics_.RequireFrame(symbol).Scissor(x, y, width, height);
                graphics_.gl_context.Shared().SetScissor(logical);
            }
            return 0;
        }
        if constexpr (FunctionId == 16U) {
            const std::array color{std::bit_cast<float>(args[0]),
                                   std::bit_cast<float>(args[1]),
                                   std::bit_cast<float>(args[2]),
                                   std::bit_cast<float>(args[3])};
            graphics_.RequireFrame(symbol).ClearColor(
                color[0], color[1], color[2], color[3]);
            graphics_.gl_context.Shared().SetClearColor(color);
            return 0;
        }
        if constexpr (FunctionId == 15U) {
            graphics_.RequireFrame(symbol).Clear(args[0]);
            graphics_.frames.RecordClear();
            return 0;
        }
        throw std::runtime_error(
            "Android boundary HLE is not implemented: " + std::string(symbol));
    }

private:
    static constexpr std::size_t kMaximumGlesNameBytes = 4096;

    template <gles::GlesThunkId FunctionId>
    std::optional<std::uint32_t> DispatchShaderProgram(
        const A32CallFrame& call) {
        const auto args = call.RegisterArguments();
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles2, FunctionId).name;
        const auto tid = call.ThreadId();
        if constexpr (FunctionId == 26U) {
            return graphics_.RequireFrame(symbol).CreateShader(args[0]);
        }
        if constexpr (FunctionId == 98U) {
            graphics_.RequireFrame(symbol).ShaderSource(
                args[0], graphics_.ReadShaderSources(args, tid));
            return 0;
        }
        if constexpr (FunctionId == 20U) {
            graphics_.RequireFrame(symbol).CompileShader(args[0]);
            graphics_.frames.RecordShaderCompile();
            return 0;
        }
        if constexpr (FunctionId == 70U) {
            const auto value = graphics_.RequireFrame(symbol)
                                   .GetShaderParameter(args[0], args[1]);
            graphics_.WriteRequired32(
                args[2], std::bit_cast<std::uint32_t>(value), tid, symbol);
            return 0;
        }
        if constexpr (FunctionId == 32U) {
            graphics_.RequireFrame(symbol).DeleteShader(args[0]);
            return 0;
        }
        if constexpr (FunctionId == 25U) {
            return graphics_.RequireFrame(symbol).CreateProgram();
        }
        if constexpr (FunctionId == 1U) {
            graphics_.RequireFrame(symbol).AttachShader(args[0], args[1]);
            return 0;
        }
        if constexpr (FunctionId == 89U) {
            graphics_.RequireFrame(symbol).LinkProgram(args[0]);
            graphics_.frames.RecordProgramLink();
            return 0;
        }
        if constexpr (FunctionId == 65U) {
            const auto value = graphics_.RequireFrame(symbol)
                                   .GetProgramParameter(args[0], args[1]);
            graphics_.WriteRequired32(
                args[2], std::bit_cast<std::uint32_t>(value), tid, symbol);
            return 0;
        }
        if constexpr (FunctionId == 57U) {
            return std::bit_cast<std::uint32_t>(graphics_.RequireFrame(symbol)
                .GetAttribLocation(args[0], graphics_.ReadCString(
                    args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if constexpr (FunctionId == 74U) {
            return std::bit_cast<std::uint32_t>(graphics_.RequireFrame(symbol)
                .GetUniformLocation(args[0], graphics_.ReadCString(
                    args[1], kMaximumGlesNameBytes, tid, symbol)));
        }
        if constexpr (FunctionId == 130U) {
            graphics_.RequireFrame(symbol).UseProgram(args[0]);
            graphics_.gl_context.Shared().SetCurrentProgram(args[0]);
            return 0;
        }
        if constexpr (FunctionId == 30U) {
            graphics_.RequireFrame(symbol).DeleteProgram(args[0]);
            return 0;
        }
        return std::nullopt;
    }

    BoundaryCallServices& calls_;
    GraphicsBoundaryContext& graphics_;
};

}  // namespace ogplay::runtime
