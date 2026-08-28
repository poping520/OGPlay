#include "gles1_query.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/services/gles_transfer_io.h"
#include "gles1_draw.h"
#include "gles1_support.h"

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kTexture0 = 0x84C0U;

[[nodiscard]] bool IsScalarTextureEnvironmentPname(
    const std::uint32_t pname) noexcept {
    return pname == kGles1TextureEnvironmentMode ||
           pname == kGles1CombineRgb || pname == kGles1CombineAlpha ||
           pname == kGles1RgbScale || pname == kGles1AlphaScale ||
           (pname >= kGles1Source0Rgb && pname <= kGles1Source2Rgb) ||
           (pname >= kGles1Source0Alpha && pname <= kGles1Source2Alpha) ||
           (pname >= kGles1Operand0Rgb && pname <= kGles1Operand2Rgb) ||
           (pname >= kGles1Operand0Alpha && pname <= kGles1Operand2Alpha);
}

[[nodiscard]] std::vector<float> ReadGuestFloats(
    const memory::AddressSpace& address_space, const std::uint32_t address,
    const std::size_t count, const std::uint64_t thread_id) {
    return gles_io::ValuesFromWords<float>(gles_io::LoadGuestWordsLE(
        address_space, address, count, thread_id));
}

void WriteGuestFloats(
    gles::GuestBuffer& output, const std::span<const float> values) {
    gles_io::WriteValuesExact(output, values,
                              "GLES1 float query output size differs");
}

void WriteGuestIntegers(
    gles::GuestBuffer& output, const std::span<const std::int32_t> values) {
    gles_io::WriteValuesExact(output, values,
                              "GLES1 integer query output size differs");
}

void WriteGuestBooleans(
    gles::GuestBuffer& output, const std::span<const std::int32_t> values) {
    auto bytes = output.WritableBytes();
    if (bytes.size() != values.size()) {
        throw std::logic_error("GLES1 boolean query output size differs");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        bytes[index] = values[index] == 0 ? std::byte{} : std::byte{1};
    }
    output.Commit();
}

[[nodiscard]] std::size_t Gles1ResourceCount(
    const std::uint32_t word, const std::string_view operation) {
    const auto count = std::bit_cast<std::int32_t>(word);
    if (count < 0) {
        throw std::invalid_argument(std::string(operation) +
                                    " count cannot be negative");
    }
    return static_cast<std::size_t>(count);
}

[[nodiscard]] std::vector<std::uint32_t> ReadGuestNames(
    const gles::GuestBuffer& input) {
    return gles_io::LoadWordsLE(input.Bytes());
}

void WriteGuestNames(gles::GuestBuffer& output,
                     const std::span<const std::uint32_t> names) {
    auto bytes = output.WritableBytes();
    gles_io::StoreWordsLE(bytes, names);
    output.Commit();
}

[[nodiscard]] std::optional<std::int32_t> Gles1OwnedInteger(
    const std::uint32_t pname, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy) {
    const auto transfer = core.TransferState().Snapshot();
    switch (pname) {
    case 0x0BA0U: return static_cast<std::int32_t>(core.Matrices().Mode());
    case 0x0BA3U:
        return static_cast<std::int32_t>(
            core.Matrices().StackDepth(kGles1Modelview));
    case 0x0BA4U:
        return static_cast<std::int32_t>(
            core.Matrices().StackDepth(kGles1Projection));
    case 0x0BA5U:
        return static_cast<std::int32_t>(
            core.Matrices().StackDepth(kGles1Texture));
    case 0x0B54U: return static_cast<std::int32_t>(core.ShadeModel());
    case 0x0D36U:  // GL_MAX_MODELVIEW_STACK_DEPTH
    case 0x0D38U:  // GL_MAX_PROJECTION_STACK_DEPTH
    case 0x0D39U: return 32;  // GL_MAX_TEXTURE_STACK_DEPTH
    case 0x0D31U: return 1;   // GL_MAX_LIGHTS; renderer consumes light zero
    case 0x0D32U: return 6;   // GL_MAX_CLIP_PLANES
    case 0x0CF5U: return static_cast<std::int32_t>(transfer.unpack_alignment);
    case 0x0D05U: return static_cast<std::int32_t>(transfer.pack_alignment);
    case 0x8069U:
        return static_cast<std::int32_t>(core.BoundTexture(0x0DE1U));
    case 0x84E0U: return static_cast<std::int32_t>(core.ActiveTexture());
    case 0x84E1U:
        return static_cast<std::int32_t>(legacy.ClientActiveTexture());
    case 0x84E2U: return 2;  // GL_MAX_TEXTURE_UNITS; fixed renderer limit
    case 0x8894U: return static_cast<std::int32_t>(transfer.array_buffer);
    case 0x8895U:
        return static_cast<std::int32_t>(transfer.element_array_buffer);
    default: return std::nullopt;
    }
}

[[nodiscard]] std::size_t NativeIntegerQueryCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x0D3AU:  // GL_MAX_VIEWPORT_DIMS
    case 0x846DU:  // GL_ALIASED_POINT_SIZE_RANGE
    case 0x846EU: return 2U;  // GL_ALIASED_LINE_WIDTH_RANGE
    // GL_VIEWPORT, GL_SCISSOR_BOX, GL_COLOR_CLEAR_VALUE/WRITEMASK.
    case 0x0BA2U: case 0x0C10U: case 0x0C22U: case 0x0C23U: return 4U;
    // Shared scalar raster/depth/stencil/blend state.
    case 0x0B45U: case 0x0B46U: case 0x0B74U: case 0x0B92U:
    case 0x0B93U: case 0x0B94U: case 0x0B95U: case 0x0B96U:
    case 0x0B97U: case 0x0B98U: case 0x0BE0U: case 0x0BE1U:
    case 0x0D33U:  // GL_MAX_TEXTURE_SIZE
    case 0x0D50U:  // GL_SUBPIXEL_BITS
    case 0x0D52U:  // GL_RED_BITS
    case 0x0D53U:  // GL_GREEN_BITS
    case 0x0D54U:  // GL_BLUE_BITS
    case 0x0D55U:  // GL_ALPHA_BITS
    case 0x0D56U:  // GL_DEPTH_BITS
    case 0x0D57U:  // GL_STENCIL_BITS
    case 0x0B72U:  // GL_DEPTH_WRITEMASK
    case 0x80A8U:  // GL_SAMPLE_BUFFERS
    case 0x80A9U:  // GL_SAMPLES
    case 0x80ABU:  // GL_SAMPLE_COVERAGE_INVERT
    case 0x84E8U:  // GL_MAX_RENDERBUFFER_SIZE
    case 0x851CU:  // GL_MAX_CUBE_MAP_TEXTURE_SIZE
    case 0x86A2U:  // GL_NUM_COMPRESSED_TEXTURE_FORMATS
    // Mixed GLES1/GLES2 binaries resolve shared glGetIntegerv through one
    // boundary symbol while still querying GLES2 capability limits.
    case 0x8869U:  // GL_MAX_VERTEX_ATTRIBS
    case 0x8872U:  // GL_MAX_TEXTURE_IMAGE_UNITS
    case 0x8B4CU:  // GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
    case 0x8B4DU:  // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
    case 0x8DF9U:  // GL_NUM_SHADER_BINARY_FORMATS (hybrid GLES1/GLES2 binary)
    case 0x8DFBU:  // GL_MAX_VERTEX_UNIFORM_VECTORS
    case 0x8DFCU:  // GL_MAX_VARYING_VECTORS
    case 0x8DFDU:  // GL_MAX_FRAGMENT_UNIFORM_VECTORS
    case 0x8B8DU:  // GL_CURRENT_PROGRAM
    case 0x8CA6U:  // GL_FRAMEBUFFER_BINDING
    case 0x8CA7U:  // GL_RENDERBUFFER_BINDING
    case 0x8B9AU:  // GL_IMPLEMENTATION_COLOR_READ_TYPE
    case 0x8B9BU: return 1U;  // GL_IMPLEMENTATION_COLOR_READ_FORMAT
    default:
        throw std::invalid_argument("GLES1 integer query is unsupported: " + std::to_string(pname));
    }
}

[[nodiscard]] std::int32_t SignedTextureValue(const std::uint32_t value) noexcept { return std::bit_cast<std::int32_t>(value); }
[[nodiscard]] gles::GuestBuffer PrepareTexturePixels(
    memory::AddressSpace& address_space, const AndroidBoundaryGles1State& state,
    const std::string_view function_name,
    const std::span<const std::uint32_t> arguments,
    const std::size_t pointer_index, const bool nullable,
    const std::uint64_t thread_id) {
    const auto resolution = state.TransferState().Resolve(
        {.function_name = function_name,
         .parameter_name = "pixels",
         .expression = "pixel_bytes(width,height,format,type)",
         .arguments = arguments});
    if (!resolution.has_value() ||
        resolution->disposition != gles::GlesLengthDisposition::transfer) {
        throw std::logic_error("GLES1 pixel transfer did not resolve to guest bytes");
    }
    return gles::GuestBuffer::Prepare(
        address_space, memory::GuestAddress{arguments[pointer_index]},
        resolution->element_count, gles::GuestTransferDirection::input,
        nullable, thread_id);
}
void GenerateAutomaticMipmap(AndroidBoundaryGles1State& state,
                             gles::AngleFrame& frame,
                             const std::uint32_t target,
                             const std::int32_t level) {
    if (level == 0 && state.GenerateMipmapEnabled(target)) {
        frame.GenerateMipmap(target);
    }
}
}  // namespace

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string) {
    if (!resolve_string) {
        throw std::invalid_argument("GLES1 string resolver is missing");
    }
    dispatch.Bind(
        "glGetString",
        [&strings, resolve_string = std::move(resolve_string)](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            strings.Validate(arguments[0]);
            return strings.Publish(arguments[0], resolve_string(arguments[0]),
                                   thread_id);
        });
}

void BindAndroidBoundaryGles1Legacy(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1LegacyState& legacy,
    AndroidBoundaryGles1State& core,
    AndroidBoundaryGles1DrawState& draw,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 legacy state frame resolver is missing");
    }
    dispatch.Bind(
        "glAlphaFunc",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const auto reference = std::bit_cast<float>(arguments[1]);
            legacy.ValidateAlphaFunction(arguments[0], reference);
            static_cast<void>(require_frame("glAlphaFunc"));
            legacy.SetAlphaFunction(arguments[0], reference);
            return 0U;
        });
    dispatch.Bind(
        "glClientActiveTexture",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            legacy.ValidateClientActiveTexture(arguments[0]);
            static_cast<void>(require_frame("glClientActiveTexture"));
            legacy.SetClientActiveTexture(arguments[0]);
            return 0U;
        });
    dispatch.Bind(
        "glColor4f",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array color{
                std::bit_cast<float>(arguments[0]),
                std::bit_cast<float>(arguments[1]),
                std::bit_cast<float>(arguments[2]),
                std::bit_cast<float>(arguments[3])};
            legacy.ValidateColor(color);
            static_cast<void>(require_frame("glColor4f"));
            legacy.SetColor(color);
            return 0U;
        });
    dispatch.Bind(
        "glColor4ub",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            constexpr float kMaximum = 255.0F;
            const std::array color{
                static_cast<float>(arguments[0] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[1] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[2] & 0xFFU) / kMaximum,
                static_cast<float>(arguments[3] & 0xFFU) / kMaximum};
            legacy.ValidateColor(color);
            static_cast<void>(require_frame("glColor4ub"));
            legacy.SetColor(color);
            return 0U;
        });
    dispatch.Bind(
        "glMultMatrixf",
        [&core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto matrix = ReadGuestGles1Matrix(
                address_space, arguments[0], thread_id);
            const auto next = Gles1MultiplyMatrices(
                core.Matrices().Current(), matrix);
            RequireFiniteGles1MatrixValues(next);
            static_cast<void>(require_frame("glMultMatrixf"));
            core.Matrices().Load(next);
            return 0U;
        });
    dispatch.Bind(
        "glNormal3f",
        [&legacy, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array normal{std::bit_cast<float>(arguments[0]),
                                    std::bit_cast<float>(arguments[1]),
                                    std::bit_cast<float>(arguments[2])};
            legacy.ValidateNormal(normal);
            static_cast<void>(require_frame("glNormal3f"));
            legacy.SetNormal(normal);
            return 0U;
        });
    dispatch.Bind(
        "glClipPlanef",
        [&legacy, &core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto values = ReadGuestFloats(
                address_space, arguments[1], 4U, thread_id);
            std::array<float, 4> equation{};
            std::ranges::copy(values, equation.begin());
            legacy.ValidateClipPlane(arguments[0], equation);
            const auto transformed = Gles1TransformClipPlane(
                core.Matrices().Current(kGles1Modelview, kTexture0), equation);
            static_cast<void>(require_frame("glClipPlanef"));
            legacy.SetClipPlane(arguments[0], transformed);
            return 0U;
        });
    dispatch.Bind(
        "glTexEnvf",
        [&legacy, &core, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array values{std::bit_cast<float>(arguments[2])};
            const auto texture = core.ActiveTexture();
            legacy.ValidateTextureEnvironment(texture, arguments[0],
                                              arguments[1], values);
            static_cast<void>(require_frame("glTexEnvf"));
            legacy.SetTextureEnvironment(texture, arguments[0], arguments[1],
                                         values);
            return 0U;
        });
    dispatch.Bind(
        "glTexEnvfv",
        [&legacy, &core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto count = arguments[1] == kGles1TextureEnvironmentColor
                                   ? 4U
                                   : IsScalarTextureEnvironmentPname(arguments[1])
                                         ? 1U
                                         : 0U;
            if (count == 0U) {
                throw std::invalid_argument(
                    "GLES1 texture environment pname is unsupported");
            }
            const auto values = ReadGuestFloats(
                address_space, arguments[2], count, thread_id);
            const auto texture = core.ActiveTexture();
            legacy.ValidateTextureEnvironment(texture, arguments[0],
                                              arguments[1], values);
            static_cast<void>(require_frame("glTexEnvfv"));
            legacy.SetTextureEnvironment(texture, arguments[0], arguments[1],
                                         values);
            return 0U;
        });
    dispatch.Bind(
        "glTexEnvi",
        [&legacy, &core, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            const std::array values{
                static_cast<float>(std::bit_cast<std::int32_t>(arguments[2]))};
            const auto texture = core.ActiveTexture();
            legacy.ValidateTextureEnvironment(texture, arguments[0],
                                              arguments[1], values);
            static_cast<void>(require_frame("glTexEnvi"));
            legacy.SetTextureEnvironment(texture, arguments[0], arguments[1],
                                         values);
            return 0U;
        });
    dispatch.Bind(
        "glGetBooleanv",
        [&legacy, &core, &draw, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            std::optional<std::int32_t> owned;
            if (const auto client =
                    Gles1ClientStateEnabled(arguments[0], draw, legacy)) {
                owned = *client ? 1 : 0;
            } else {
                try {
                    owned = core.Capability(arguments[0]) ? 1 : 0;
                } catch (const std::invalid_argument&) {
                    owned = Gles1OwnedInteger(arguments[0], core, legacy);
                }
            }
            const auto count = owned.has_value()
                                   ? 1U
                                   : NativeIntegerQueryCount(arguments[0]);
            auto output = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[1]}, count,
                gles::GuestTransferDirection::output, false, thread_id);
            auto& frame = require_frame("glGetBooleanv");
            const auto values = owned.has_value()
                                    ? std::vector<std::int32_t>{*owned}
                                    : frame.GetIntegers(arguments[0], count);
            WriteGuestBooleans(output, values);
            return 0U;
        });
    dispatch.Bind(
        "glGetFloatv",
        [&legacy, &core, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            std::size_t count{};
            switch (arguments[0]) {
            case 0x0BA6U:
            case 0x0BA7U:
            case 0x0BA8U: count = 16U; break;
            case 0x0B00U: count = 4U; break;
            case 0x0BC1U:
            case 0x0BC2U:
            case 0x84E1U:
            case kGles1MaxTextureAnisotropy: count = 1U; break;
            default:
                throw std::invalid_argument("GLES1 float query is unsupported");
            }
            auto output = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[1]},
                count * sizeof(std::uint32_t),
                gles::GuestTransferDirection::output, false, thread_id);
            std::vector<float> values;
            if (arguments[0] == kGles1MaxTextureAnisotropy) {
                const auto queried = require_frame("glGetFloatv")
                                         .GetIntegers(arguments[0], 1U);
                values = {static_cast<float>(queried.front())};
            } else {
                static_cast<void>(require_frame("glGetFloatv"));
                switch (arguments[0]) {
                case 0x0BA6U:
                case 0x0BA7U:
                case 0x0BA8U: {
                    const auto mode = arguments[0] == 0x0BA6U
                                          ? kGles1Modelview
                                          : arguments[0] == 0x0BA7U
                                                ? kGles1Projection
                                                : kGles1Texture;
                    const auto& matrix = core.Matrices().Current(
                        mode, core.ActiveTexture());
                    values.assign(matrix.begin(), matrix.end());
                    break;
                }
                case 0x0B00U:
                    values.assign(legacy.Color().begin(), legacy.Color().end());
                    break;
                case 0x0BC1U:
                    values = {static_cast<float>(legacy.AlphaFunction())};
                    break;
                case 0x0BC2U: values = {legacy.AlphaReference()}; break;
                case 0x84E1U:
                    values = {static_cast<float>(legacy.ClientActiveTexture())};
                    break;
                default: break;
                }
            }
            WriteGuestFloats(output, values);
            return 0U;
        });
    dispatch.Bind(
        "glGetIntegerv",
        [&legacy, &core, &draw, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            std::optional<std::array<std::int32_t, 4>> logical;
            if (arguments[0] == 0x0BA2U) {
                logical = core.Shared().Viewport();
            } else if (arguments[0] == 0x0C10U) {
                logical = core.Shared().Scissor();
            }
            auto owned = Gles1ClientArrayInteger(arguments[0], draw, legacy);
            if (!owned && !logical.has_value()) {
                owned = Gles1OwnedInteger(arguments[0], core, legacy);
            }
            const auto count = logical.has_value()
                                   ? logical->size()
                               : owned.has_value()
                                   ? 1U
                                   : NativeIntegerQueryCount(arguments[0]);
            auto output = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[1]},
                count * sizeof(std::uint32_t),
                gles::GuestTransferDirection::output, false, thread_id);
            auto& frame = require_frame("glGetIntegerv");
            const auto native_pname = arguments[0] == 0x0BE0U ? 0x80C9U :
                                      arguments[0] == 0x0BE1U ? 0x80C8U : arguments[0];
            const auto values = logical.has_value()
                                    ? std::vector<std::int32_t>(
                                          logical->begin(), logical->end())
                                : owned.has_value()
                                    ? std::vector<std::int32_t>{*owned}
                                    : frame.GetIntegers(native_pname, count);
            WriteGuestIntegers(output, values);
            return 0U;
        });
}
void BindAndroidBoundaryGles1Textures(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 texture frame resolver is missing");
    }
    dispatch.Bind("glGenBuffers", [&address_space, require_frame](
                                      const auto arguments,
                                      const std::uint64_t thread_id) {
        const auto count = Gles1ResourceCount(arguments[0], "glGenBuffers");
        auto output = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[1]},
            count * sizeof(std::uint32_t), gles::GuestTransferDirection::output,
            false, thread_id);
        const auto names = require_frame("glGenBuffers").GenerateBuffers(count);
        WriteGuestNames(output, names);
        return 0U;
    });
    dispatch.Bind("glDeleteBuffers", [&state, &address_space, require_frame](
                                         const auto arguments,
                                         const std::uint64_t thread_id) {
        const auto count = Gles1ResourceCount(arguments[0], "glDeleteBuffers");
        const auto input = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[1]},
            count * sizeof(std::uint32_t), gles::GuestTransferDirection::input,
            false, thread_id);
        const auto names = ReadGuestNames(input);
        require_frame("glDeleteBuffers").DeleteBuffers(names);
        auto next = state.TransferState();
        const auto bound = next.Snapshot();
        if (std::ranges::find(names, bound.array_buffer) != names.end())
            next.BindBuffer(0x8892U, 0U);
        if (std::ranges::find(names, bound.element_array_buffer) != names.end())
            next.BindBuffer(0x8893U, 0U);
        state.SetTransferState(std::move(next));
        return 0U;
    });
    dispatch.Bind("glBufferData", [&address_space, require_frame](
                                      const auto arguments,
                                      const std::uint64_t thread_id) {
        const auto size = Gles1ResourceCount(arguments[1], "glBufferData");
        const auto input = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[2]}, size,
            gles::GuestTransferDirection::input, true, thread_id);
        require_frame("glBufferData")
            .BufferData(arguments[0], static_cast<std::uint32_t>(size),
                        input.IsNull() ? std::nullopt
                                       : std::optional{input.Bytes()},
                        arguments[3]);
        return 0U;
    });
    dispatch.Bind("glBufferSubData", [&address_space, require_frame](
                                         const auto arguments,
                                         const std::uint64_t thread_id) {
        const auto offset = Gles1ResourceCount(arguments[1], "glBufferSubData offset");
        const auto size = Gles1ResourceCount(arguments[2], "glBufferSubData");
        const auto input = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[3]}, size,
            gles::GuestTransferDirection::input, false, thread_id);
        require_frame("glBufferSubData")
            .BufferSubData(arguments[0], static_cast<std::int32_t>(offset),
                           input.Bytes());
        return 0U;
    });
    dispatch.Bind("glReadPixels", [&state, &address_space, require_frame](
                                      const auto arguments,
                                      const std::uint64_t thread_id) {
        const auto resolution = state.TransferState().Resolve(
            {.function_name = "glReadPixels", .parameter_name = "pixels",
             .expression = "pixel_bytes(width,height,format,type)",
             .arguments = arguments});
        if (!resolution ||
            resolution->disposition != gles::GlesLengthDisposition::transfer) {
            throw std::logic_error("GLES1 readback size did not resolve");
        }
        auto output = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[6]},
            resolution->element_count, gles::GuestTransferDirection::output,
            false, thread_id);
        require_frame("glReadPixels")
            .ReadPixels(SignedTextureValue(arguments[0]),
                        SignedTextureValue(arguments[1]),
                        SignedTextureValue(arguments[2]),
                        SignedTextureValue(arguments[3]), arguments[4],
                        arguments[5], output.WritableBytes());
        output.Commit();
        return 0U;
    });
    dispatch.Bind(
        "glCompressedTexImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            const auto image_size = SignedTextureValue(arguments[6]);
            if (image_size < 0) {
                throw std::invalid_argument("GLES1 compressed image size is negative");
            }
            auto data = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{arguments[7]},
                static_cast<std::uint32_t>(image_size),
                gles::GuestTransferDirection::input, false, thread_id);
            auto& frame = require_frame("glCompressedTexImage2D");
            frame.CompressedTextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]), arguments[2],
                SignedTextureValue(arguments[3]), SignedTextureValue(arguments[4]),
                SignedTextureValue(arguments[5]), data.Bytes());
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            if (SignedTextureValue(arguments[1]) == 0) {
                state.SetTextureBaseFormat(arguments[0], arguments[2]);
            }
            return 0U;
        });
    dispatch.Bind(
        "glCopyTexImage2D",
        [&state, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t) {
            auto& frame = require_frame("glCopyTexImage2D");
            frame.CopyTextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]), arguments[2],
                SignedTextureValue(arguments[3]), SignedTextureValue(arguments[4]),
                SignedTextureValue(arguments[5]), SignedTextureValue(arguments[6]),
                SignedTextureValue(arguments[7]));
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            if (SignedTextureValue(arguments[1]) == 0) {
                state.SetTextureBaseFormat(arguments[0], arguments[2]);
            }
            return 0U;
        });
    dispatch.Bind(
        "glTexImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            auto pixels = PrepareTexturePixels(
                address_space, state, "glTexImage2D", arguments, 8U, true,
                thread_id);
            auto& frame = require_frame("glTexImage2D");
            frame.TextureImage2D(
                arguments[0], SignedTextureValue(arguments[1]),
                SignedTextureValue(arguments[2]), SignedTextureValue(arguments[3]),
                SignedTextureValue(arguments[4]), SignedTextureValue(arguments[5]),
                arguments[6], arguments[7],
                pixels.IsNull()
                    ? std::nullopt
                    : std::optional<std::span<const std::byte>>(pixels.Bytes()));
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            if (SignedTextureValue(arguments[1]) == 0) {
                state.SetTextureBaseFormat(
                    arguments[0], static_cast<std::uint32_t>(
                                      SignedTextureValue(arguments[2])));
            }
            return 0U;
        });
    dispatch.Bind(
        "glTexSubImage2D",
        [&state, &address_space, require_frame](
            const std::span<const std::uint32_t> arguments,
            const std::uint64_t thread_id) {
            auto pixels = PrepareTexturePixels(
                address_space, state, "glTexSubImage2D", arguments, 8U, false,
                thread_id);
            auto& frame = require_frame("glTexSubImage2D");
            frame.TextureSubImage2D(
                arguments[0], SignedTextureValue(arguments[1]),
                SignedTextureValue(arguments[2]), SignedTextureValue(arguments[3]),
                SignedTextureValue(arguments[4]), SignedTextureValue(arguments[5]),
                arguments[6], arguments[7], pixels.Bytes());
            GenerateAutomaticMipmap(state, frame, arguments[0],
                                    SignedTextureValue(arguments[1]));
            return 0U;
        });
}

}  // namespace ogplay::runtime::detail
