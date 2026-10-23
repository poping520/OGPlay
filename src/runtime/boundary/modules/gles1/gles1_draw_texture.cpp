#include "runtime/boundary/modules/gles1/gles1_module.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>

#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/services/gles_transfer_io.h"

namespace ogplay::runtime {

std::uint32_t Gles1Module::Api19Extension(const A32CallFrame& call, const std::string_view symbol) {
    if (symbol != "glBlendEquationOES" && symbol != "glBlendEquationSeparateOES" &&
        symbol != "glBlendFuncSeparateOES" && !symbol.starts_with("glDrawTex"))
        return UnimplementedGl(calls_, call, "libGLESv1_CM.so", symbol);
    std::scoped_lock execution_lock(graphics_.execution_mutex);
    graphics_.ActivateCurrentContext();
    try {
        auto& frame = graphics_.RequireFrame(symbol);
        if (symbol == "glBlendEquationOES")
            frame.BlendEquation(call.Argument(0));
        else if (symbol == "glBlendEquationSeparateOES")
            frame.BlendEquationSeparate(call.Argument(0), call.Argument(1));
        else if (symbol == "glBlendFuncSeparateOES")
            frame.BlendFunctionSeparate(call.Argument(0), call.Argument(1), call.Argument(2), call.Argument(3));
        else {
            std::array<float, 5> rectangle{};
            const auto kind = symbol[9];  // glDrawTex{s,i,x,f}[v]OES
            const bool vector = symbol[10] == 'v';
            std::vector<std::uint32_t> words;
            std::array<std::byte, 10> shorts{};
            if (vector && kind == 's')
                calls_.address_space.Read(call.Pointer<std::int16_t>(0).Address(), shorts, call.ThreadId());
            else if (vector)
                words = gles_io::LoadGuestWordsLE(
                    calls_.address_space, call.Pointer<std::uint32_t>(0).Address().Value(), 5U, call.ThreadId());
            for (std::size_t i = 0; i < rectangle.size(); ++i) {
                const auto word = vector ? (kind == 's' ? 0U : words[i]) : call.Argument(i);
                if (kind == 'f')
                    rectangle[i] = std::bit_cast<float>(word);
                else if (kind == 'x')
                    rectangle[i] = static_cast<float>(std::bit_cast<std::int32_t>(word)) / 65536.0F;
                else if (kind == 's') {
                    const auto bits =
                        vector ? static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(shorts[i * 2U]) |
                                                            (std::to_integer<std::uint16_t>(shorts[i * 2U + 1U]) << 8U))
                               : static_cast<std::uint16_t>(word);
                    rectangle[i] = static_cast<float>(std::bit_cast<std::int16_t>(bits));
                } else
                    rectangle[i] = static_cast<float>(std::bit_cast<std::int32_t>(word));
                if (!std::isfinite(rectangle[i])) throw gles::GlesApiError(symbol, 0x0501U);
            }
            if (rectangle[3] <= 0.0F || rectangle[4] <= 0.0F) throw gles::GlesApiError(symbol, 0x0501U);
            graphics_.gl_context.Native().BeginFixedDraw();
            try {
                if (frame.ClientVersion() >= 3) {
                    auto& state = graphics_.gl_context.Programmable();
                    if (state.fixed_vertex_array == 0U) {
                        std::array<std::uint32_t, 1> names{};
                        frame.InvokeGles3Names(34U, names);
                        state.fixed_vertex_array = names[0];
                    }
                    const std::array args{state.fixed_vertex_array};
                    static_cast<void>(frame.InvokeGles3Scalar(6U, args));
                }
                draw_state_.DrawTexture(frame, core_state_, legacy_state_, rectangle);
                graphics_.gles_dispatch.RestoreNativeState(frame);
                graphics_.gl_context.Native().EndFixedDraw();
                graphics_.frames.RecordDraw();
            } catch (...) {
                try {
                    graphics_.gles_dispatch.RestoreNativeState(frame);
                    graphics_.gl_context.Native().EndFixedDraw();
                } catch (...) {
                    graphics_.gl_context.Native().Reset();
                }
                throw;
            }
        }
    } catch (const gles::GlesApiError& error) {
        graphics_.SetGuestError(call, error.Code());
    }
    return 0U;
}

namespace detail {

void AndroidBoundaryGles1DrawState::DrawTexture(gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
                                                const AndroidBoundaryGles1LegacyState& legacy,
                                                const std::array<float, 5>& rectangle) {
    const auto& viewport = core.Shared().Viewport();
    if (viewport[2] == 0 || viewport[3] == 0) return;
    const float x0 = 2.0F * (rectangle[0] - static_cast<float>(viewport[0])) / static_cast<float>(viewport[2]) - 1.0F;
    const float y0 = 2.0F * (rectangle[1] - static_cast<float>(viewport[1])) / static_cast<float>(viewport[3]) - 1.0F;
    const float x1 = x0 + 2.0F * rectangle[3] / static_cast<float>(viewport[2]);
    const float y1 = y0 + 2.0F * rectangle[4] / static_cast<float>(viewport[3]);
    const std::array positions{x0, y0, 0.0F, 1.0F, x1, y0, 0.0F, 1.0F, x0, y1, 0.0F, 1.0F, x1, y1, 0.0F, 1.0F};
    auto units = core.EnabledTextureUnits();
    std::erase_if(units, [&core](const auto unit) { return !core.Capability(unit, 0x0DE1U); });
    if (units.size() > kGles1MaximumDrawTextureUnits)
        throw std::runtime_error("GLES1 draw texture exceeds fixed texture stage limit");
    // Prepare every crop before changing native draw state.
    std::array<std::array<float, 16>, kGles1MaximumDrawTextureUnits> coordinates{};
    for (std::size_t stage = 0; stage < units.size(); ++stage) {
        const auto size = core.Shared().TextureSize(units[stage]);
        const auto crop = core.Shared().TextureCrop(units[stage]);
        if (size[0] <= 0 || size[1] <= 0) throw gles::GlesApiError("glDrawTexOES texture size", 0x0502U);
        const float s0 = static_cast<float>(crop[0]) / static_cast<float>(size[0]);
        const float t0 = static_cast<float>(crop[1]) / static_cast<float>(size[1]);
        const float s1 = (static_cast<float>(crop[0]) + static_cast<float>(crop[2])) / static_cast<float>(size[0]);
        const float t1 = (static_cast<float>(crop[1]) + static_cast<float>(crop[3])) / static_cast<float>(size[1]);
        coordinates[stage] = {s0, t0, 0.0F, 1.0F, s1, t0, 0.0F, 1.0F, s0, t1, 0.0F, 1.0F, s1, t1, 0.0F, 1.0F};
    }
    constexpr std::array<std::uint32_t, kGles1MaximumDrawTextureUnits> targets{0x0DE1U, 0x0DE1U};
    auto& program = EnsureProgram(frame, targets);
    frame.UseProgram(program.name);
    ApplyUniforms(frame, program, core, legacy, units, targets, 2.0F * std::clamp(rectangle[2], 0.0F, 1.0F) - 1.0F);
    for (const auto attribute : program.attributes)
        if (attribute >= 0) frame.SetVertexAttributeEnabled(static_cast<std::uint32_t>(attribute), false);
    const auto upload = [&frame, &program](const std::size_t index, const auto& values) {
        if (program.attributes[index] < 0) return;
        frame.BindBuffer(0x8892U, program.buffers[index]);
        const auto bytes = std::as_bytes(std::span(values));
        frame.BufferData(0x8892U, bytes.size(), bytes, 0x88E0U);
        const auto attribute = static_cast<std::uint32_t>(program.attributes[index]);
        frame.VertexAttributePointer(attribute, 4, 0x1406U, false, 0, 0);
        frame.SetVertexAttributeEnabled(attribute, true);
    };
    upload(0U, positions);
    for (std::size_t stage = 0; stage < units.size(); ++stage)
        upload(4U + stage, coordinates[stage]);
    const bool cull = frame.GetBooleans(0x0B44U, 1U)[0] != 0U;
    const bool offset = frame.GetBooleans(0x8037U, 1U)[0] != 0U;
    try {
        frame.SetCapability(0x0B44U, false);
        frame.SetCapability(0x8037U, false);
        frame.DrawArrays(0x0005U, 0, 4);
        frame.SetCapability(0x0B44U, cull);
        frame.SetCapability(0x8037U, offset);
    } catch (...) {
        frame.SetCapability(0x0B44U, cull);
        frame.SetCapability(0x8037U, offset);
        throw;
    }
}

}  // namespace detail
}  // namespace ogplay::runtime
