#include "runtime/boundary/modules/gles2/gles2_dispatch.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/services/guest_gl_context.h"
#include "runtime/boundary/core/a32_call_frame.h"

namespace ogplay::runtime {
namespace {

constexpr memory::GuestAddress kQueryStringPage{0x70001000U};
constexpr std::uint32_t kQueryStringSlotBytes = 1024;
constexpr std::uint32_t kArrayBuffer = 0x8892U;
constexpr std::uint32_t kElementArrayBuffer = 0x8893U;
constexpr std::uint32_t kStreamDraw = 0x88E0U;
constexpr std::uint32_t kByte = 0x1400U;
constexpr std::uint32_t kUnsignedByte = 0x1401U;
constexpr std::uint32_t kShort = 0x1402U;
constexpr std::uint32_t kUnsignedShort = 0x1403U;
constexpr std::uint32_t kFloat = 0x1406U;
constexpr std::uint32_t kFixed = 0x140CU;
constexpr std::size_t kMaximumVertexAttributes = 16U;

// These values are generated-catalog slots. ValidateGles2FunctionIds() makes
// catalog drift fail during boundary construction instead of misrouting a call.
enum class Gles2Function : gles::GlesThunkId {
    active_texture = 0, attach_shader = 1, bind_buffer = 3,
    bind_framebuffer = 4, bind_renderbuffer = 5, bind_texture = 6,
    blend_color = 7, blend_equation = 8, blend_func = 10,
    buffer_data = 12, check_framebuffer_status = 14, clear = 15,
    clear_color = 16, compile_shader = 20, create_program = 25,
    create_shader = 26, delete_buffers = 28, delete_framebuffers = 29,
    delete_program = 30, delete_renderbuffers = 31, delete_shader = 32,
    delete_textures = 33, disable = 38, disable_vertex_attrib_array = 39,
    draw_arrays = 40, draw_elements = 41, enable = 42,
    enable_vertex_attrib_array = 43, flush = 45,
    framebuffer_renderbuffer = 46, framebuffer_texture_2d = 47,
    gen_buffers = 49, gen_framebuffers = 50, gen_renderbuffers = 51,
    gen_textures = 52, generate_mipmap = 53, get_active_attrib = 54,
    get_active_uniform = 55, get_attrib_location = 57, get_error = 60,
    get_integerv = 63, get_program_info_log = 64, get_program_iv = 65,
    get_shader_info_log = 67, get_shader_iv = 70, get_string = 71,
    get_uniform_location = 74, is_enabled = 82, link_program = 89,
    pixel_store_i = 90, read_pixels = 92, renderbuffer_storage = 94,
    sample_coverage = 95, scissor = 96, shader_source = 98,
    tex_image_2d = 105, tex_parameter_i = 108, tex_sub_image_2d = 110,
    uniform_1f = 111, uniform_1fv = 112, uniform_1i = 113,
    uniform_1iv = 114, uniform_2fv = 116, uniform_2iv = 118,
    uniform_3fv = 120, uniform_3iv = 122, uniform_4f = 123,
    uniform_4fv = 124, uniform_4iv = 126, uniform_matrix_3fv = 128,
    uniform_matrix_4fv = 129, use_program = 130, vertex_attrib_4f = 138,
    vertex_attrib_pointer = 140, viewport = 141,
};

[[nodiscard]] constexpr gles::GlesThunkId Id(const Gles2Function function) {
    return static_cast<gles::GlesThunkId>(function);
}

void ValidateGles2FunctionIds() {
    static constexpr std::array expected{
        std::pair{Gles2Function::active_texture, std::string_view{"glActiveTexture"}},
        std::pair{Gles2Function::draw_arrays, std::string_view{"glDrawArrays"}},
        std::pair{Gles2Function::draw_elements, std::string_view{"glDrawElements"}},
        std::pair{Gles2Function::viewport, std::string_view{"glViewport"}},
    };
    for (const auto& [id, name] : expected) {
        if (gles::DescribeGlesFunction(gles::GlesApi::gles2, Id(id)).name != name) {
            throw std::logic_error("generated GLES2 function ids changed");
        }
    }
}

std::uint32_t QueryStringOffset(const std::uint32_t parameter) {
    switch (parameter) {
    case 0x1f00U: return 0;
    case 0x1f01U: return kQueryStringSlotBytes;
    case 0x1f02U: return kQueryStringSlotBytes * 2U;
    case 0x8b8cU: return kQueryStringSlotBytes * 3U;
    default: throw std::invalid_argument("unsupported GLES string query");
    }
}
[[nodiscard]] std::size_t VertexAttribScalarBytes(const std::uint32_t type) {
    switch (type) {
    case kByte: case kUnsignedByte: return 1U;
    case kShort: case kUnsignedShort: return 2U;
    case kFloat: case kFixed: return 4U;
    default:
        throw std::invalid_argument("GLES2 vertex attribute type is unsupported");
    }
}
[[nodiscard]] std::uint64_t ClientArrayBytes(const std::int32_t size,
                                             const std::uint32_t type,
                                             const std::int32_t stride,
                                             const std::uint32_t maximum_index) {
    const auto packed =
        static_cast<std::uint64_t>(size) * VertexAttribScalarBytes(type);
    const auto step = stride == 0 ? packed : static_cast<std::uint64_t>(stride);
    if (maximum_index != 0U &&
        step > ((std::numeric_limits<std::uint64_t>::max)() - packed) /
                   maximum_index) {
        throw std::length_error("GLES2 client array byte range overflows");
    }
    return step * maximum_index + packed;
}
[[nodiscard]] std::uint32_t MaximumGuestIndex(const std::span<const std::byte> bytes,
                                              const std::uint32_t type) {
    std::uint32_t maximum{};
    if (type == kUnsignedByte) {
        for (const auto value : bytes) {
            maximum = std::max(maximum, static_cast<std::uint32_t>(
                                            std::to_integer<std::uint8_t>(value)));
        }
        return maximum;
    }
    if (type != kUnsignedShort || bytes.size() % 2U != 0U) {
        throw std::invalid_argument("GLES2 draw index type is unsupported");
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
        maximum = std::max(
            maximum,
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
    }
    return maximum;
}

}  // namespace

class AndroidBoundaryGles::Impl final {
public:
    Impl(memory::AddressSpace& address_space, GuestGlContext& context)
        : address_space_(address_space), context_(context) {
        ValidateGles2FunctionIds();
    }

    std::optional<std::uint32_t> Dispatch(
        const gles::GlesThunkId function_id,
        const A32CallFrame& a32_call, gles::AngleFrame* const frame) {
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles2, function_id).name;
        const auto args = a32_call.RegisterArguments();
        const auto tid = a32_call.ThreadId();
        if (function_id == Id(Gles2Function::gen_buffers) ||
            function_id == Id(Gles2Function::gen_textures) ||
            function_id == Id(Gles2Function::gen_framebuffers) ||
            function_id == Id(Gles2Function::gen_renderbuffers)) {
            auto call = PrepareCall(function_id, std::span(args).first<2>(), tid);
            auto& current = RequireFrame(frame, symbol);
            auto names = function_id == Id(Gles2Function::gen_buffers)
                             ? current.GenerateBuffers(args[0])
                         : function_id == Id(Gles2Function::gen_textures)
                             ? current.GenerateTextures(args[0])
                         : function_id == Id(Gles2Function::gen_framebuffers)
                             ? current.GenerateFramebuffers(args[0])
                             : current.GenerateRenderbuffers(args[0]);
            WriteWords(Pointer(call), names);
            return 0;
        }
        if (function_id == Id(Gles2Function::delete_buffers) ||
            function_id == Id(Gles2Function::delete_textures) ||
            function_id == Id(Gles2Function::delete_framebuffers) ||
            function_id == Id(Gles2Function::delete_renderbuffers)) {
            auto call = PrepareCall(function_id, std::span(args).first<2>(), tid);
            const auto names = ReadWords(Pointer(call));
            if (function_id == Id(Gles2Function::delete_buffers)) {
                RequireFrame(frame, symbol).DeleteBuffers(names);
                const auto bound = context_.Shared().transfer.Snapshot();
                if (std::ranges::find(names, bound.array_buffer) != names.end()) {
                    context_.Shared().transfer.BindBuffer(0x8892U, 0);
                }
                if (std::ranges::find(names, bound.element_array_buffer) != names.end()) {
                    context_.Shared().transfer.BindBuffer(0x8893U, 0);
                }
            } else if (function_id == Id(Gles2Function::delete_textures)) {
                RequireFrame(frame, symbol).DeleteTextures(names);
                context_.Shared().DeleteTextures(names);
            } else if (function_id == Id(Gles2Function::delete_framebuffers)) {
                RequireFrame(frame, symbol).DeleteFramebuffers(names);
                context_.Shared().DeleteFramebuffers(names);
            } else {
                RequireFrame(frame, symbol).DeleteRenderbuffers(names);
                context_.Shared().DeleteRenderbuffers(names);
            }
            return 0;
        }
        if (function_id == Id(Gles2Function::bind_buffer)) {
            auto next = context_.Shared().transfer;
            next.BindBuffer(args[0], args[1]);
            RequireFrame(frame, symbol).BindBuffer(args[0], args[1]);
            context_.Shared().transfer = std::move(next);
            return 0;
        }
        if (function_id == Id(Gles2Function::buffer_data)) {
            auto call = PrepareCall(function_id, args, tid);
            const auto& data = Pointer(call);
            RequireFrame(frame, symbol).BufferData(
                args[0], args[1], OptionalBytes(data), args[3]);
            return 0;
        }
        if (function_id == Id(Gles2Function::active_texture)) {
            context_.Shared().ValidateActiveTexture(args[0]);
            RequireFrame(frame, symbol).ActiveTexture(args[0]);
            context_.Shared().SetActiveTexture(args[0]);
            return 0;
        }
        if (function_id == Id(Gles2Function::bind_texture)) {
            context_.Shared().ValidateTextureTarget(args[0]);
            RequireFrame(frame, symbol).BindTexture(args[0], args[1]);
            context_.Shared().BindTexture(args[0], args[1]);
            return 0;
        }
        if (function_id == Id(Gles2Function::bind_framebuffer)) {
            context_.Shared().ValidateFramebufferTarget(args[0]);
            RequireFrame(frame, symbol).BindFramebuffer(args[0], args[1]);
            context_.Shared().BindFramebuffer(args[0], args[1]);
            return 0;
        }
        if (function_id == Id(Gles2Function::bind_renderbuffer)) {
            context_.Shared().ValidateRenderbufferTarget(args[0]);
            RequireFrame(frame, symbol).BindRenderbuffer(args[0], args[1]);
            context_.Shared().BindRenderbuffer(args[0], args[1]);
            return 0;
        }
        if (function_id == Id(Gles2Function::check_framebuffer_status)) {
            return RequireFrame(frame, symbol).CheckFramebufferStatus(args[0]);
        }
        if (function_id == Id(Gles2Function::renderbuffer_storage)) {
            RequireFrame(frame, symbol).RenderbufferStorage(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]),
                std::bit_cast<std::int32_t>(args[3]));
            return 0;
        }
        if (function_id == Id(Gles2Function::framebuffer_texture_2d)) {
            RequireFrame(frame, symbol).FramebufferTexture2D(
                args[0], args[1], args[2], args[3],
                std::bit_cast<std::int32_t>(a32_call.Argument(4)));
            return 0;
        }
        if (function_id == Id(Gles2Function::framebuffer_renderbuffer)) {
            RequireFrame(frame, symbol).FramebufferRenderbuffer(
                args[0], args[1], args[2], args[3]);
            return 0;
        }
        if (function_id == Id(Gles2Function::generate_mipmap)) {
            RequireFrame(frame, symbol).GenerateMipmap(args[0]);
            return 0;
        }
        if (function_id == Id(Gles2Function::pixel_store_i)) {
            const auto value = std::bit_cast<std::int32_t>(args[1]);
            auto next = context_.Shared().transfer;
            next.PixelStore(args[0], value);
            RequireFrame(frame, symbol).PixelStore(args[0], value);
            context_.Shared().transfer = std::move(next);
            return 0;
        }
        if (function_id == Id(Gles2Function::tex_parameter_i)) {
            RequireFrame(frame, symbol).TextureParameter(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]));
            return 0;
        }
        if (function_id == Id(Gles2Function::tex_image_2d)) {
            std::array<std::uint32_t, 9> all{args[0], args[1], args[2], args[3]};
            for (std::size_t index = 4; index < all.size(); ++index) {
                all[index] = a32_call.Argument(index);
            }
            auto call = PrepareCall(function_id, all, tid);
            const auto& pixels = Pointer(call);
            RequireFrame(frame, symbol).TextureImage2D(
                all[0], std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]),
                std::bit_cast<std::int32_t>(all[4]),
                std::bit_cast<std::int32_t>(all[5]), all[6], all[7],
                OptionalBytes(pixels));
            if (std::bit_cast<std::int32_t>(all[1]) == 0) {
                context_.Shared().SetTextureBaseFormat(all[0], all[2]);
            }
            return 0;
        }
        if (function_id == Id(Gles2Function::tex_sub_image_2d)) {
            std::array<std::uint32_t, 9> all{args[0], args[1], args[2], args[3]};
            for (std::size_t index = 4; index < all.size(); ++index) {
                all[index] = a32_call.Argument(index);
            }
            auto call = PrepareCall(function_id, all, tid);
            RequireFrame(frame, symbol).TextureSubImage2D(
                all[0], std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]),
                std::bit_cast<std::int32_t>(all[4]),
                std::bit_cast<std::int32_t>(all[5]), all[6], all[7],
                Pointer(call).Bytes());
            return 0;
        }
        if (function_id == Id(Gles2Function::enable_vertex_attrib_array) ||
            function_id == Id(Gles2Function::disable_vertex_attrib_array)) {
            const auto index = args[0];
            RequireAttributeIndex(index);
            attributes_[index].enabled =
                function_id == Id(Gles2Function::enable_vertex_attrib_array);
            attributes_[index].enable_lr =
                a32_call.LinkRegister();
            RequireFrame(frame, symbol).SetVertexAttributeEnabled(
                index, attributes_[index].enabled);
            return 0;
        }
        if (function_id == Id(Gles2Function::vertex_attrib_pointer)) {
            const auto index = args[0];
            RequireAttributeIndex(index);
            const auto size = std::bit_cast<std::int32_t>(args[1]);
            const auto type = args[2];
            const auto normalized = args[3] != 0;
            const auto stride = std::bit_cast<std::int32_t>(a32_call.Argument(4));
            const auto pointer = a32_call.Argument(5);
            if (size < 1 || size > 4) {
                throw std::invalid_argument(
                    "GLES2 vertex attribute size is outside 1..4");
            }
            if (stride < 0) {
                throw std::invalid_argument(
                    "GLES2 vertex attribute stride is negative");
            }
            static_cast<void>(VertexAttribScalarBytes(type));
            std::array<std::uint32_t, 6> all{
                args[0], args[1], args[2], args[3],
                static_cast<std::uint32_t>(stride), pointer};
            auto call = PrepareCall(function_id, all, tid);
            if (call.pointers.size() != 1 || !call.pointers.front().deferred) {
                throw std::logic_error(
                    "glVertexAttribPointer expected a deferred pointer");
            }
            const auto buffer = context_.Shared().transfer.Snapshot().array_buffer;
            attributes_[index] = {
                .size = size,
                .type = type,
                .normalized = normalized,
                .stride = stride,
                .pointer = pointer,
                .buffer = buffer,
                .enabled = attributes_[index].enabled,
                .defined = true,
                .definition_lr = a32_call.LinkRegister(),
                .enable_lr = attributes_[index].enable_lr,
            };
            if (buffer != 0U) {
                RequireFrame(frame, symbol).VertexAttributePointer(
                    index, size, type, normalized, stride, pointer);
            }
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_1f)) {
            RequireFrame(frame, symbol).Uniform1f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]));
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_1i)) {
            RequireFrame(frame, symbol).Uniform1i(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]));
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_4f)) {
            RequireFrame(frame, symbol).Uniform4f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]), std::bit_cast<float>(args[2]),
                std::bit_cast<float>(args[3]),
                std::bit_cast<float>(a32_call.Argument(4)));
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_matrix_3fv)) {
            auto call = PrepareCall(function_id, args, tid);
            const auto values = ReadFloats(Pointer(call));
            RequireFrame(frame, symbol).UniformMatrix3(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]), args[2] != 0, values);
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_1fv) ||
            function_id == Id(Gles2Function::uniform_2fv) ||
            function_id == Id(Gles2Function::uniform_3fv) ||
            function_id == Id(Gles2Function::uniform_4fv) ||
            function_id == Id(Gles2Function::uniform_1iv) ||
            function_id == Id(Gles2Function::uniform_2iv) ||
            function_id == Id(Gles2Function::uniform_3iv) ||
            function_id == Id(Gles2Function::uniform_4iv)) {
            auto call = PrepareCall(function_id, std::span(args).first<3>(), tid);
            const auto components =
                function_id == Id(Gles2Function::uniform_1fv) ||
                        function_id == Id(Gles2Function::uniform_1iv) ? 1U :
                function_id == Id(Gles2Function::uniform_2fv) ||
                        function_id == Id(Gles2Function::uniform_2iv) ? 2U :
                function_id == Id(Gles2Function::uniform_3fv) ||
                        function_id == Id(Gles2Function::uniform_3iv) ? 3U : 4U;
            const auto count = std::bit_cast<std::int32_t>(args[1]);
            const auto floating =
                function_id == Id(Gles2Function::uniform_1fv) ||
                function_id == Id(Gles2Function::uniform_2fv) ||
                function_id == Id(Gles2Function::uniform_3fv) ||
                function_id == Id(Gles2Function::uniform_4fv);
            if (floating) {
                RequireFrame(frame, symbol).UniformFloats(
                    std::bit_cast<std::int32_t>(args[0]), count, components,
                    ReadFloats(Pointer(call)));
            } else {
                RequireFrame(frame, symbol).UniformIntegers(
                    std::bit_cast<std::int32_t>(args[0]), count, components,
                    ReadIntegers(Pointer(call)));
            }
            return 0;
        }
        if (function_id == Id(Gles2Function::uniform_matrix_4fv)) {
            auto call = PrepareCall(function_id, args, tid);
            RequireFrame(frame, symbol).UniformMatrix4(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]), args[2] != 0,
                ReadFloats(Pointer(call)));
            return 0;
        }
        if (function_id == Id(Gles2Function::vertex_attrib_4f)) {
            RequireFrame(frame, symbol).VertexAttribute4f(
                args[0], std::bit_cast<float>(args[1]),
                std::bit_cast<float>(args[2]), std::bit_cast<float>(args[3]),
                std::bit_cast<float>(a32_call.Argument(4)));
            return 0;
        }
        if (function_id == Id(Gles2Function::get_active_attrib) ||
            function_id == Id(Gles2Function::get_active_uniform)) {
            std::array<std::uint32_t, 7> all{
                args[0], args[1], args[2], args[3], a32_call.Argument(4),
                a32_call.Argument(5), a32_call.Argument(6)};
            auto call = PrepareCall(function_id, all, tid);
            const auto result = function_id == Id(Gles2Function::get_active_attrib)
                ? RequireFrame(frame, symbol).GetActiveAttribute(all[0], all[1])
                : RequireFrame(frame, symbol).GetActiveUniform(all[0], all[1]);
            const auto length = WriteText(Pointer(call, 6), result.name);
            WriteWord(Pointer(call, 3), length);
            WriteWord(Pointer(call, 4),
                      std::bit_cast<std::uint32_t>(result.size));
            WriteWord(Pointer(call, 5), result.type);
            return 0;
        }
        if (function_id == Id(Gles2Function::get_program_info_log) ||
            function_id == Id(Gles2Function::get_shader_info_log)) {
            auto call = PrepareCall(function_id, args, tid);
            const auto log = function_id == Id(Gles2Function::get_program_info_log)
                ? RequireFrame(frame, symbol).GetProgramInfoLog(args[0])
                : RequireFrame(frame, symbol).GetShaderInfoLog(args[0]);
            const auto length = WriteText(Pointer(call, 3), log);
            WriteWord(Pointer(call, 2), length);
            return 0;
        }
        if (function_id == Id(Gles2Function::get_integerv)) {
            auto call = PrepareCall(function_id, std::span(args).first<2>(), tid);
            auto& output = Pointer(call);
            std::vector<std::uint32_t> words;
            if (args[0] == 0x0BA2U || args[0] == 0x0C10U) {
                static_cast<void>(RequireFrame(frame, symbol));
                const auto& logical = args[0] == 0x0BA2U
                                          ? context_.Shared().Viewport()
                                          : context_.Shared().Scissor();
                words.reserve(logical.size());
                for (const auto value : logical) {
                    words.push_back(std::bit_cast<std::uint32_t>(value));
                }
            } else if (args[0] == 0x8069U || args[0] == 0x8514U) {
                static_cast<void>(RequireFrame(frame, symbol));
                const auto target = args[0] == 0x8069U ? 0x0DE1U : 0x8513U;
                words.push_back(context_.Shared().BoundTexture(target));
            } else {
                const auto values = RequireFrame(frame, symbol).GetIntegers(
                    args[0], output.WritableBytes().size() / 4U);
                words.reserve(values.size());
                for (const auto value : values) {
                    words.push_back(std::bit_cast<std::uint32_t>(value));
                }
            }
            WriteWords(output, words);
            return 0;
        }
        if (function_id == Id(Gles2Function::get_string)) {
            return WriteQueryString(args[0],
                                    RequireFrame(frame, symbol).GetString(args[0]), tid);
        }
        if (function_id == Id(Gles2Function::get_error)) {
            return RequireFrame(frame, symbol).GetError();
        }
        if (function_id == Id(Gles2Function::is_enabled)) {
            static_cast<void>(RequireFrame(frame, symbol));
            return context_.Shared().Capability(args[0]) ? 1U : 0U;
        }
        if (function_id == Id(Gles2Function::enable) ||
            function_id == Id(Gles2Function::disable)) {
            const auto enabled = function_id == Id(Gles2Function::enable);
            context_.Shared().ValidateCapability(args[0]);
            RequireFrame(frame, symbol).SetCapability(args[0], enabled);
            context_.Shared().SetCapability(args[0], enabled);
            return 0;
        }
        if (function_id == Id(Gles2Function::blend_func)) {
            RequireFrame(frame, symbol).BlendFunction(args[0], args[1]);
            return 0;
        }
        if (function_id == Id(Gles2Function::blend_color)) {
            RequireFrame(frame, symbol).BlendColor(
                std::bit_cast<float>(args[0]), std::bit_cast<float>(args[1]),
                std::bit_cast<float>(args[2]), std::bit_cast<float>(args[3]));
            return 0;
        }
        if (function_id == Id(Gles2Function::blend_equation)) {
            RequireFrame(frame, symbol).BlendEquation(args[0]);
            return 0;
        }
        if (function_id == Id(Gles2Function::sample_coverage)) {
            RequireFrame(frame, symbol).SampleCoverage(
                std::bit_cast<float>(args[0]), args[1] != 0U);
            return 0;
        }
        if (function_id == Id(Gles2Function::flush)) {
            RequireFrame(frame, symbol).Flush();
            return 0;
        }
        if (function_id == Id(Gles2Function::draw_arrays)) {
            auto& current = RequireFrame(frame, symbol);
            const auto first = std::bit_cast<std::int32_t>(args[1]);
            const auto count = std::bit_cast<std::int32_t>(args[2]);
            if (first < 0 || count < 0) {
                throw std::invalid_argument("GLES2 draw array range is negative");
            }
            if (count == 0) return 0;
            const auto maximum = static_cast<std::uint64_t>(first) +
                                 static_cast<std::uint64_t>(count) - 1U;
            if (maximum > (std::numeric_limits<std::uint32_t>::max)()) {
                throw std::length_error("GLES2 draw array index overflows");
            }
            StageClientAttributes(current, static_cast<std::uint32_t>(maximum),
                                  tid);
            current.DrawArrays(args[0], first, count);
            RestoreBufferBindings(current);
            return 0;
        }
        if (function_id == Id(Gles2Function::draw_elements)) {
            auto& current = RequireFrame(frame, symbol);
            const auto count = std::bit_cast<std::int32_t>(args[1]);
            const auto type = args[2];
            if (count < 0) {
                throw std::invalid_argument("GLES2 draw element count is negative");
            }
            if (count == 0) return 0;
            auto call = PrepareCall(function_id, args, tid);
            if (call.pointers.size() != 1) {
                throw std::logic_error("glDrawElements expected one pointer");
            }
            const auto element_buffer =
                context_.Shared().transfer.Snapshot().element_array_buffer;
            if (element_buffer != 0U) {
                if (!call.pointers.front().deferred) {
                    throw std::logic_error(
                        "glDrawElements expected a deferred index offset");
                }
                if (HasEnabledClientAttribute()) {
                    throw std::runtime_error(
                        "GLES2 cannot stage client arrays from an opaque element buffer");
                }
                current.DrawElements(args[0], count, type, args[3]);
                return 0;
            }
            if (call.pointers.front().deferred ||
                !call.pointers.front().transfer.has_value()) {
                throw std::logic_error(
                    "glDrawElements expected transferred client indices");
            }
            const auto indices = call.pointers.front().transfer->Bytes();
            const auto maximum = MaximumGuestIndex(indices, type);
            StageClientAttributes(current, maximum, tid);
            EnsureIndexStagingBuffer(current);
            current.BindBuffer(kElementArrayBuffer, index_staging_buffer_);
            current.BufferData(kElementArrayBuffer,
                               static_cast<std::uint32_t>(indices.size()),
                               indices, kStreamDraw);
            current.DrawElements(args[0], count, type, 0U);
            RestoreBufferBindings(current);
            return 0;
        }
        if (function_id == Id(Gles2Function::read_pixels)) {
            std::array<std::uint32_t, 7> all{
                args[0], args[1], args[2], args[3],
                a32_call.Argument(4), a32_call.Argument(5),
                a32_call.Argument(6)};
            auto call = PrepareCall(function_id, all, tid);
            auto& output = Pointer(call);
            RequireFrame(frame, symbol).ReadPixels(
                std::bit_cast<std::int32_t>(all[0]),
                std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]), all[4], all[5],
                output.WritableBytes());
            output.Commit();
            return 0;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool HasEnabledVertexAttribute() const noexcept {
        return std::ranges::any_of(attributes_, [](const Attribute& attribute) {
            return attribute.enabled && attribute.defined;
        });
    }

    [[nodiscard]] gles::GlesTransferStateSnapshot TransferState() const noexcept {
        return context_.Shared().transfer.Snapshot();
    }

    void RestoreNativeState(gles::AngleFrame& frame) {
        const auto& shared = context_.Shared();
        frame.UseProgram(shared.CurrentProgram());
        for (const auto& [binding, texture] : shared.TextureBindings()) {
            frame.ActiveTexture(binding.texture_unit);
            frame.BindTexture(binding.target, texture);
        }
        frame.ActiveTexture(shared.active_texture);
        frame.BindFramebuffer(0x8D40U, shared.Framebuffer());
        for (std::size_t index = 0; index < attributes_.size(); ++index) {
            const auto& attribute = attributes_[index];
            if (attribute.defined && attribute.buffer != 0U) {
                frame.BindBuffer(kArrayBuffer, attribute.buffer);
                frame.VertexAttributePointer(
                    static_cast<std::uint32_t>(index), attribute.size,
                    attribute.type, attribute.normalized, attribute.stride,
                    attribute.pointer);
            }
            frame.SetVertexAttributeEnabled(static_cast<std::uint32_t>(index),
                                            attribute.enabled);
        }
        const auto buffers = shared.transfer.Snapshot();
        frame.BindBuffer(kArrayBuffer, buffers.array_buffer);
        frame.BindBuffer(kElementArrayBuffer, buffers.element_array_buffer);
    }

    void Reset() noexcept {
        context_.Reset();
        attributes_ = {};
        staging_buffers_.clear();
        client_array_staging_.clear();
        index_staging_buffer_ = 0;
    }

private:
    struct Attribute final {
        std::int32_t size{};
        std::uint32_t type{};
        bool normalized{};
        std::int32_t stride{};
        std::uint32_t pointer{};
        std::uint32_t buffer{};
        bool enabled{};
        bool defined{};
        std::uint32_t definition_lr{};
        std::uint32_t enable_lr{};
    };

    static void RequireAttributeIndex(const std::uint32_t index) {
        if (index >= kMaximumVertexAttributes) {
            throw std::invalid_argument(
                "GLES2 vertex attribute index is outside 0..15");
        }
    }

    [[nodiscard]] bool HasEnabledClientAttribute() const noexcept {
        return std::ranges::any_of(attributes_, [](const Attribute& attribute) {
            return attribute.enabled && attribute.defined &&
                   attribute.buffer == 0U;
        });
    }

    void EnsureStagingBuffers(gles::AngleFrame& frame) {
        if (!staging_buffers_.empty()) return;
        staging_buffers_ = frame.GenerateBuffers(kMaximumVertexAttributes);
        client_array_staging_.resize(kMaximumVertexAttributes);
    }

    void EnsureIndexStagingBuffer(gles::AngleFrame& frame) {
        if (index_staging_buffer_ != 0U) return;
        const auto buffers = frame.GenerateBuffers(1);
        index_staging_buffer_ = buffers.front();
    }

    void StageClientAttributes(gles::AngleFrame& frame,
                               const std::uint32_t maximum_index,
                               const std::uint64_t thread_id) {
        if (!HasEnabledClientAttribute()) return;
        EnsureStagingBuffers(frame);
        for (std::size_t index = 0; index < attributes_.size(); ++index) {
            const auto& attribute = attributes_[index];
            if (!attribute.enabled || !attribute.defined ||
                attribute.buffer != 0U) {
                continue;
            }
            const auto bytes = ClientArrayBytes(
                attribute.size, attribute.type, attribute.stride, maximum_index);
            std::span<const std::byte> transfer;
            try {
                transfer = gles::PrepareGuestInput(
                    address_space_, memory::GuestAddress{attribute.pointer},
                    bytes, false, client_array_staging_[index], thread_id);
            } catch (const gles::GuestTransferError& error) {
                throw gles::GuestTransferError(
                    "GLES2 client attribute " + std::to_string(index) +
                    " transfer failed: " + error.what() +
                    "; pointer=" + std::to_string(attribute.pointer) +
                    " bytes=" + std::to_string(bytes) +
                    " size=" + std::to_string(attribute.size) +
                    " type=" + std::to_string(attribute.type) +
                    " stride=" + std::to_string(attribute.stride) +
                    " definition_lr=" +
                    std::to_string(attribute.definition_lr) +
                    " enable_lr=" + std::to_string(attribute.enable_lr));
            }
            frame.BindBuffer(kArrayBuffer, staging_buffers_[index]);
            frame.BufferData(kArrayBuffer,
                             static_cast<std::uint32_t>(transfer.size()),
                             transfer, kStreamDraw);
            frame.VertexAttributePointer(
                static_cast<std::uint32_t>(index), attribute.size,
                attribute.type, attribute.normalized, attribute.stride, 0U);
        }
    }

    void RestoreBufferBindings(gles::AngleFrame& frame) {
        const auto bound = context_.Shared().transfer.Snapshot();
        frame.BindBuffer(kArrayBuffer, bound.array_buffer);
        frame.BindBuffer(kElementArrayBuffer, bound.element_array_buffer);
    }

    gles::PreparedGlesCall PrepareCall(
        const gles::GlesThunkId function_id,
        const std::span<const std::uint32_t> args,
        const std::uint64_t tid) {
        return gles::PrepareGles2Call(
            address_space_, function_id, args, tid, &context_.Shared().transfer);
    }

    static gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call) {
        if (call.pointers.size() != 1 ||
            !call.pointers.front().transfer.has_value()) {
            throw std::logic_error(
                "GLES resource handler expected one transferred pointer");
        }
        return *call.pointers.front().transfer;
    }

    static gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call,
                                      const std::size_t parameter_index) {
        const auto found = std::ranges::find_if(
            call.pointers, [parameter_index](const auto& pointer) {
                return pointer.parameter_index == parameter_index;
            });
        if (found == call.pointers.end() || !found->transfer.has_value()) {
            throw std::logic_error(
                "GLES resource handler expected a transferred parameter");
        }
        return *found->transfer;
    }

    static std::vector<std::uint32_t> ReadWords(
        const gles::GuestBuffer& transfer) {
        const auto bytes = transfer.Bytes();
        if (bytes.size() % 4U != 0) {
            throw std::logic_error("GLES name array is misaligned");
        }
        std::vector<std::uint32_t> words(bytes.size() / 4U);
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                words[word] |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[word * 4U + byte]))
                    << (byte * 8U);
            }
        }
        return words;
    }

    static std::vector<float> ReadFloats(const gles::GuestBuffer& transfer) {
        const auto words = ReadWords(transfer);
        std::vector<float> values;
        values.reserve(words.size());
        for (const auto word : words) {
            values.push_back(std::bit_cast<float>(word));
        }
        return values;
    }

    static std::vector<std::int32_t> ReadIntegers(
        const gles::GuestBuffer& transfer) {
        const auto words = ReadWords(transfer);
        std::vector<std::int32_t> values;
        values.reserve(words.size());
        for (const auto word : words) {
            values.push_back(std::bit_cast<std::int32_t>(word));
        }
        return values;
    }

    static void WriteWord(gles::GuestBuffer& transfer,
                          const std::uint32_t word) {
        if (transfer.IsNull()) return;
        WriteWords(transfer, std::span(&word, 1));
    }

    static std::uint32_t WriteText(gles::GuestBuffer& transfer,
                                   const std::string_view text) {
        auto bytes = transfer.WritableBytes();
        const auto length = bytes.empty()
            ? 0U
            : std::min(text.size(), bytes.size() - 1U);
        std::ranges::fill(bytes, std::byte{});
        for (std::size_t index = 0; index < length; ++index) {
            bytes[index] = static_cast<std::byte>(
                static_cast<unsigned char>(text[index]));
        }
        transfer.Commit();
        return static_cast<std::uint32_t>(length);
    }

    static void WriteWords(gles::GuestBuffer& transfer,
                           const std::span<const std::uint32_t> words) {
        auto bytes = transfer.WritableBytes();
        if (bytes.size() != words.size() * 4U) {
            throw std::logic_error("GLES name output size differs");
        }
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                bytes[word * 4U + byte] =
                    static_cast<std::byte>(words[word] >> (byte * 8U));
            }
        }
        transfer.Commit();
    }

    static std::optional<std::span<const std::byte>> OptionalBytes(
        const gles::GuestBuffer& transfer) {
        if (transfer.IsNull()) return std::nullopt;
        return transfer.Bytes();
    }

    static gles::AngleFrame& RequireFrame(
        gles::AngleFrame* const frame, const std::string_view operation) {
        if (frame == nullptr) {
            throw std::runtime_error(
                std::string(operation) + " has no current ANGLE frame");
        }
        return *frame;
    }

    void EnsureQueryStringPage() {
        if (query_string_page_mapped_) return;
        if (address_space_.PageSize() < kQueryStringSlotBytes * 4U) {
            throw std::length_error("guest page is too small for GLES query strings");
        }
        address_space_.Map({kQueryStringPage, address_space_.PageSize()},
                           memory::PageProtection::read |
                               memory::PageProtection::write);
        address_space_.Protect({kQueryStringPage, address_space_.PageSize()},
                               memory::PageProtection::read);
        query_string_page_mapped_ = true;
    }

    std::uint32_t WriteQueryString(const std::uint32_t parameter,
                                   const std::string_view value,
                                   const std::uint64_t tid) {
        const auto offset = QueryStringOffset(parameter);
        if (value.size() >= kQueryStringSlotBytes) {
            throw std::length_error("ANGLE query string exceeds its guest slot");
        }
        EnsureQueryStringPage();
        std::vector<std::byte> bytes;
        bytes.reserve(value.size() + 1U);
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        const auto page = memory::GuestRange{kQueryStringPage,
                                              address_space_.PageSize()};
        address_space_.Protect(page, memory::PageProtection::read |
                                         memory::PageProtection::write);
        try {
            address_space_.Write(kQueryStringPage.Add(offset), bytes, tid);
        } catch (...) {
            address_space_.Protect(page, memory::PageProtection::read);
            throw;
        }
        address_space_.Protect(page, memory::PageProtection::read);
        return kQueryStringPage.Add(offset).Value();
    }

    memory::AddressSpace& address_space_;
    GuestGlContext& context_;
    std::array<Attribute, kMaximumVertexAttributes> attributes_{};
    std::vector<std::uint32_t> staging_buffers_;
    std::vector<std::vector<std::byte>> client_array_staging_;
    std::uint32_t index_staging_buffer_{};
    bool query_string_page_mapped_{};
};

AndroidBoundaryGles::AndroidBoundaryGles(memory::AddressSpace& address_space)
    : owned_context_(std::make_unique<GuestGlContext>()),
      impl_(std::make_unique<Impl>(address_space, *owned_context_)) {}
AndroidBoundaryGles::AndroidBoundaryGles(memory::AddressSpace& address_space,
                                         GuestGlContext& context)
    : impl_(std::make_unique<Impl>(address_space, context)) {}
AndroidBoundaryGles::~AndroidBoundaryGles() = default;

std::optional<std::uint32_t> AndroidBoundaryGles::Dispatch(
    const std::string_view symbol,
    const A32CallFrame& call, gles::AngleFrame* const frame) {
    const auto function_id = gles::FindGlesFunction(
        gles::GlesApi::gles2, symbol);
    if (!function_id.has_value()) return std::nullopt;
    return Dispatch(*function_id, call, frame);
}

std::optional<std::uint32_t> AndroidBoundaryGles::Dispatch(
    const gles::GlesThunkId function_id,
    const A32CallFrame& call, gles::AngleFrame* const frame) {
    return impl_->Dispatch(function_id, call, frame);
}

bool AndroidBoundaryGles::HasEnabledVertexAttribute() const noexcept {
    return impl_->HasEnabledVertexAttribute();
}

gles::GlesTransferStateSnapshot
AndroidBoundaryGles::TransferState() const noexcept {
    return impl_->TransferState();
}

void AndroidBoundaryGles::RestoreNativeState(gles::AngleFrame& frame) {
    impl_->RestoreNativeState(frame);
}

void AndroidBoundaryGles::Reset() noexcept { impl_->Reset(); }

}  // namespace ogplay::runtime
