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
        if (const auto completion = DispatchCompletion<FunctionId>(call);
            completion.has_value()) {
            return *completion;
        }
        if (const auto transfer = DispatchTransfer<FunctionId>(call);
            transfer.has_value()) {
            return *transfer;
        }
        if (const auto state = DispatchLowTransferState<FunctionId>(call);
            state.has_value()) {
            return *state;
        }
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
        throw std::logic_error(
            "sealed GLES2 export escaped concrete handler coverage: " +
            std::string(symbol));
    }

private:
    static constexpr std::size_t kMaximumGlesNameBytes = 4096;

    template <gles::GlesThunkId FunctionId>
    std::optional<std::uint32_t> DispatchCompletion(
        const A32CallFrame& call) {
        if constexpr (FunctionId == 2U) return BindAttribLocation(call);
        if constexpr (FunctionId == 37U) return DetachShader(call);
        if constexpr (FunctionId == 56U) return GetAttachedShaders(call);
        if constexpr (FunctionId == 68U) return GetShaderPrecisionFormat(call);
        if constexpr (FunctionId == 69U) return GetShaderSource(call);
        if constexpr (FunctionId == 75U) return GetUniformfv(call);
        if constexpr (FunctionId == 76U) return GetUniformiv(call);
        if constexpr (FunctionId == 77U) return GetVertexAttribPointerv(call);
        if constexpr (FunctionId == 78U) return GetVertexAttribfv(call);
        if constexpr (FunctionId == 79U) return GetVertexAttribiv(call);
        if constexpr (FunctionId == 93U) return ReleaseShaderCompiler(call);
        if constexpr (FunctionId == 97U) return ShaderBinary(call);
        if constexpr (FunctionId == 115U) return Uniform2f(call);
        if constexpr (FunctionId == 117U) return Uniform2i(call);
        if constexpr (FunctionId == 119U) return Uniform3f(call);
        if constexpr (FunctionId == 121U) return Uniform3i(call);
        if constexpr (FunctionId == 125U) return Uniform4i(call);
        if constexpr (FunctionId == 127U) return UniformMatrix2fv(call);
        if constexpr (FunctionId == 131U) return ValidateProgram(call);
        if constexpr (FunctionId == 132U) return VertexAttrib1f(call);
        if constexpr (FunctionId == 133U) return VertexAttrib1fv(call);
        if constexpr (FunctionId == 134U) return VertexAttrib2f(call);
        if constexpr (FunctionId == 135U) return VertexAttrib2fv(call);
        if constexpr (FunctionId == 136U) return VertexAttrib3f(call);
        if constexpr (FunctionId == 137U) return VertexAttrib3fv(call);
        if constexpr (FunctionId == 139U) return VertexAttrib4fv(call);
        return std::nullopt;
    }

    std::uint32_t BindAttribLocation(const A32CallFrame& call);
    std::uint32_t DetachShader(const A32CallFrame& call);
    std::uint32_t GetAttachedShaders(const A32CallFrame& call);
    std::uint32_t GetShaderPrecisionFormat(const A32CallFrame& call);
    std::uint32_t GetShaderSource(const A32CallFrame& call);
    std::uint32_t GetUniformfv(const A32CallFrame& call);
    std::uint32_t GetUniformiv(const A32CallFrame& call);
    std::uint32_t GetVertexAttribPointerv(const A32CallFrame& call);
    std::uint32_t GetVertexAttribfv(const A32CallFrame& call);
    std::uint32_t GetVertexAttribiv(const A32CallFrame& call);
    std::uint32_t ReleaseShaderCompiler(const A32CallFrame& call);
    std::uint32_t ShaderBinary(const A32CallFrame& call);
    std::uint32_t Uniform2f(const A32CallFrame& call);
    std::uint32_t Uniform2i(const A32CallFrame& call);
    std::uint32_t Uniform3f(const A32CallFrame& call);
    std::uint32_t Uniform3i(const A32CallFrame& call);
    std::uint32_t Uniform4i(const A32CallFrame& call);
    std::uint32_t UniformMatrix2fv(const A32CallFrame& call);
    std::uint32_t ValidateProgram(const A32CallFrame& call);
    std::uint32_t VertexAttrib1f(const A32CallFrame& call);
    std::uint32_t VertexAttrib1fv(const A32CallFrame& call);
    std::uint32_t VertexAttrib2f(const A32CallFrame& call);
    std::uint32_t VertexAttrib2fv(const A32CallFrame& call);
    std::uint32_t VertexAttrib3f(const A32CallFrame& call);
    std::uint32_t VertexAttrib3fv(const A32CallFrame& call);
    std::uint32_t VertexAttrib4fv(const A32CallFrame& call);

    template <gles::GlesThunkId FunctionId>
    std::optional<std::uint32_t> DispatchTransfer(const A32CallFrame& call) {
        if constexpr (FunctionId == 13U) return BufferSubData(call);
        if constexpr (FunctionId == 21U) return CompressedTexImage2D(call);
        if constexpr (FunctionId == 22U) return CompressedTexSubImage2D(call);
        if constexpr (FunctionId == 23U) return CopyTexImage2D(call);
        if constexpr (FunctionId == 24U) return CopyTexSubImage2D(call);
        if constexpr (FunctionId == 58U) return GetBooleanv(call);
        if constexpr (FunctionId == 59U) return GetBufferParameteriv(call);
        if constexpr (FunctionId == 61U) return GetFloatv(call);
        if constexpr (FunctionId == 62U)
            return GetFramebufferAttachmentParameteriv(call);
        if constexpr (FunctionId == 66U) return GetRenderbufferParameteriv(call);
        if constexpr (FunctionId == 72U) return GetTexParameterfv(call);
        if constexpr (FunctionId == 73U) return GetTexParameteriv(call);
        if constexpr (FunctionId == 106U) return TexParameterf(call);
        if constexpr (FunctionId == 107U) return TexParameterfv(call);
        if constexpr (FunctionId == 109U) return TexParameteriv(call);
        return std::nullopt;
    }

    std::uint32_t BufferSubData(const A32CallFrame& call);
    std::uint32_t CompressedTexImage2D(const A32CallFrame& call);
    std::uint32_t CompressedTexSubImage2D(const A32CallFrame& call);
    std::uint32_t CopyTexImage2D(const A32CallFrame& call);
    std::uint32_t CopyTexSubImage2D(const A32CallFrame& call);
    std::uint32_t GetBooleanv(const A32CallFrame& call);
    std::uint32_t GetBufferParameteriv(const A32CallFrame& call);
    std::uint32_t GetFloatv(const A32CallFrame& call);
    std::uint32_t GetFramebufferAttachmentParameteriv(
        const A32CallFrame& call);
    std::uint32_t GetRenderbufferParameteriv(const A32CallFrame& call);
    std::uint32_t GetTexParameterfv(const A32CallFrame& call);
    std::uint32_t GetTexParameteriv(const A32CallFrame& call);
    std::uint32_t TexParameterf(const A32CallFrame& call);
    std::uint32_t TexParameterfv(const A32CallFrame& call);
    std::uint32_t TexParameteriv(const A32CallFrame& call);

    template <gles::GlesThunkId FunctionId>
    std::optional<std::uint32_t> DispatchLowTransferState(
        const A32CallFrame& call) {
        const auto args = call.RegisterArguments();
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles2, FunctionId).name;
        if constexpr (FunctionId == 9U || FunctionId == 11U ||
                      (FunctionId >= 17U && FunctionId <= 19U) ||
                      FunctionId == 27U || (FunctionId >= 34U && FunctionId <= 36U) ||
                      FunctionId == 44U || FunctionId == 48U ||
                      FunctionId == 80U || FunctionId == 81U ||
                      (FunctionId >= 83U && FunctionId <= 88U) ||
                      FunctionId == 91U || (FunctionId >= 99U && FunctionId <= 104U)) {
        auto& frame = graphics_.RequireFrame(symbol);
        if constexpr (FunctionId == 9U) {
            frame.BlendEquationSeparate(args[0], args[1]); return 0U;
        }
        if constexpr (FunctionId == 11U) {
            frame.BlendFunctionSeparate(args[0], args[1], args[2], args[3]); return 0U;
        }
        if constexpr (FunctionId == 17U) {
            const auto value = std::bit_cast<float>(args[0]);
            frame.ClearDepth(value);
            graphics_.gl_context.Shared().SetClearDepth(value);
            return 0U;
        }
        if constexpr (FunctionId == 18U) {
            const auto value = std::bit_cast<std::int32_t>(args[0]);
            frame.ClearStencil(value);
            graphics_.gl_context.Shared().SetClearStencil(value);
            return 0U;
        }
        if constexpr (FunctionId == 19U) {
            frame.ColorMask(args[0] != 0U, args[1] != 0U,
                            args[2] != 0U, args[3] != 0U); return 0U;
        }
        if constexpr (FunctionId == 27U) { frame.CullFace(args[0]); return 0U; }
        if constexpr (FunctionId == 34U) { frame.DepthFunction(args[0]); return 0U; }
        if constexpr (FunctionId == 35U) { frame.DepthMask(args[0] != 0U); return 0U; }
        if constexpr (FunctionId == 36U) {
            frame.DepthRange(std::bit_cast<float>(args[0]),
                             std::bit_cast<float>(args[1])); return 0U;
        }
        if constexpr (FunctionId == 44U) { frame.Finish(); return 0U; }
        if constexpr (FunctionId == 48U) { frame.FrontFace(args[0]); return 0U; }
        if constexpr (FunctionId == 80U) { frame.Hint(args[0], args[1]); return 0U; }
        if constexpr (FunctionId == 88U) {
            frame.LineWidth(std::bit_cast<float>(args[0])); return 0U;
        }
        if constexpr (FunctionId == 91U) {
            frame.PolygonOffset(std::bit_cast<float>(args[0]),
                                std::bit_cast<float>(args[1])); return 0U;
        }
        if constexpr (FunctionId == 99U) {
            frame.StencilFunction(args[0], std::bit_cast<std::int32_t>(args[1]),
                                  args[2]); return 0U;
        }
        if constexpr (FunctionId == 100U) {
            frame.StencilFunctionSeparate(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]), args[3]);
            return 0U;
        }
        if constexpr (FunctionId == 101U) { frame.StencilMask(args[0]); return 0U; }
        if constexpr (FunctionId == 102U) {
            frame.StencilMaskSeparate(args[0], args[1]); return 0U;
        }
        if constexpr (FunctionId == 103U) {
            frame.StencilOperation(args[0], args[1], args[2]); return 0U;
        }
        if constexpr (FunctionId == 104U) {
            frame.StencilOperationSeparate(args[0], args[1], args[2], args[3]);
            return 0U;
        }
        if constexpr (FunctionId == 81U) return frame.IsBuffer(args[0]) ? 1U : 0U;
        if constexpr (FunctionId == 83U) return frame.IsFramebuffer(args[0]) ? 1U : 0U;
        if constexpr (FunctionId == 84U) return frame.IsProgram(args[0]) ? 1U : 0U;
        if constexpr (FunctionId == 85U) return frame.IsRenderbuffer(args[0]) ? 1U : 0U;
        if constexpr (FunctionId == 86U) return frame.IsShader(args[0]) ? 1U : 0U;
        if constexpr (FunctionId == 87U) return frame.IsTexture(args[0]) ? 1U : 0U;
        }
        return std::nullopt;
    }

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
            auto& frame = graphics_.RequireFrame(symbol);
            frame.LinkProgram(args[0]);
            auto& transfer = graphics_.gl_context.Shared().transfer;
            transfer.ClearUniformElementCounts(args[0]);
            if (frame.GetProgramParameter(args[0], 0x8B82U) != 0) {
                for (const auto& uniform :
                     frame.DiscoverUniformValueCounts(args[0])) {
                    transfer.SetUniformElementCount(
                        args[0], uniform.location, uniform.value_count);
                }
            }
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
            graphics_.gl_context.Shared().transfer.ClearUniformElementCounts(
                args[0]);
            return 0;
        }
        return std::nullopt;
    }

    BoundaryCallServices& calls_;
    GraphicsBoundaryContext& graphics_;
};

}  // namespace ogplay::runtime
