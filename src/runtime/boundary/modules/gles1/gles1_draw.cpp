#include "gles1_draw.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "gles1_fixed.h"
#include "gles1_support.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"
#include "runtime/boundary/services/gles_transfer_io.h"
namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kArrayBuffer = 0x8892U;
constexpr std::uint32_t kElementArrayBuffer = 0x8893U;
constexpr std::uint32_t kStaticDraw = 0x88E4U;
constexpr std::uint32_t kFloat = 0x1406U;
constexpr std::uint32_t kFixed = 0x140CU;
constexpr std::uint32_t kByte = 0x1400U;
constexpr std::uint32_t kUnsignedByte = 0x1401U;
constexpr std::uint32_t kShort = 0x1402U;
constexpr std::uint32_t kUnsignedShort = 0x1403U;
constexpr std::uint32_t kTexture2d = 0x0DE1U;
constexpr std::uint32_t kTextureCubeMap = 0x8513U;
constexpr std::uint32_t kModulate = 0x2100U;
constexpr std::uint32_t kReplace = 0x1E01U;
constexpr std::uint32_t kAdd = 0x0104U;
constexpr std::uint32_t kCombine = 0x8570U;
constexpr std::uint32_t kBlend = 0x0BE2U;
constexpr std::uint32_t kDecal = 0x2101U;

enum class TextureFormatClass : std::int32_t {
    alpha,
    color,
    color_alpha,
};
[[nodiscard]] TextureFormatClass ClassifyTextureFormat(
    const std::uint32_t format) {
    switch (format) {
    case 0x1906U: return TextureFormatClass::alpha;  // GL_ALPHA
    case 0x1907U:                                  // GL_RGB
    case 0x1909U:                                  // GL_LUMINANCE
    case 0x8D64U:                                  // GL_ETC1_RGB8_OES
    case 0x83F0U:                                  // DXT1 RGB
    case 0x8C00U:                                  // PVRTC RGB 4bpp
    case 0x8C01U:                                  // PVRTC RGB 2bpp
    case 0x8C92U: return TextureFormatClass::color; // ATC RGB
    case 0x1908U:                                  // GL_RGBA
    case 0x190AU:                                  // GL_LUMINANCE_ALPHA
    case 0x83F1U:                                  // DXT1 RGBA
    case 0x83F2U:                                  // DXT3 RGBA
    case 0x83F3U:                                  // DXT5 RGBA
    case 0x8C02U:                                  // PVRTC RGBA 4bpp
    case 0x8C03U:                                  // PVRTC RGBA 2bpp
    case 0x8C93U:                                  // ATC explicit alpha
    case 0x87EEU: return TextureFormatClass::color_alpha; // ATC interpolated alpha
    default:
        throw std::runtime_error(
            "GLES1 fixed draw does not classify texture base format " +
            std::to_string(format));
    }
}
[[nodiscard]] std::size_t ScalarBytes(const std::uint32_t type) {
    switch (type) {
    case kByte:
    case kUnsignedByte: return 1U;
    case kShort:
    case kUnsignedShort: return 2U;
    case kFloat:
    case kFixed: return 4U;
    default: throw std::invalid_argument("GLES1 client array type is unsupported");
    }
}
[[nodiscard]] std::uint64_t ArrayBytes(const Gles1ClientArray& array,
                                       const std::uint32_t maximum_index) {
    const auto packed = static_cast<std::uint64_t>(array.size) *
                        ScalarBytes(array.type);
    const auto stride = array.stride == 0
                            ? packed
                            : static_cast<std::uint64_t>(array.stride);
    if (maximum_index != 0U &&
        stride > ((std::numeric_limits<std::uint64_t>::max)() - packed) /
                     maximum_index) {
        throw std::length_error("GLES1 client array byte range overflows");
    }
    return stride * maximum_index + packed;
}
[[nodiscard]] Gles1Matrix MatrixFor(
    const AndroidBoundaryGles1State& core, const std::uint32_t mode,
    const std::uint32_t texture = kTexture0) {
    return core.Matrices().Current(mode, texture);
}
[[nodiscard]] std::array<float, 9> NormalMatrix(
    const Gles1Matrix& matrix) {
    const float a00 = matrix[0], a01 = matrix[4], a02 = matrix[8];
    const float a10 = matrix[1], a11 = matrix[5], a12 = matrix[9];
    const float a20 = matrix[2], a21 = matrix[6], a22 = matrix[10];
    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = a12 * a20 - a10 * a22;
    const float c02 = a10 * a21 - a11 * a20;
    const float c10 = a02 * a21 - a01 * a22;
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = a01 * a20 - a00 * a21;
    const float c20 = a01 * a12 - a02 * a11;
    const float c21 = a02 * a10 - a00 * a12;
    const float c22 = a00 * a11 - a01 * a10;
    const float determinant = a00 * c00 + a01 * c01 + a02 * c02;
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-20F) {
        throw std::runtime_error(
            "GLES1 normal matrix requires an invertible modelview");
    }
    const float inverse_determinant = 1.0F / determinant;
    return {c00 * inverse_determinant, c10 * inverse_determinant,
            c20 * inverse_determinant, c01 * inverse_determinant,
            c11 * inverse_determinant, c21 * inverse_determinant,
            c02 * inverse_determinant, c12 * inverse_determinant,
            c22 * inverse_determinant};
}
[[nodiscard]] std::uint32_t MaximumIndex(
    const std::span<const std::byte> bytes, const std::uint32_t type) {
    return gles_io::MaximumGuestIndex(bytes, type,
                                      "GLES1 draw index type is unsupported");
}
void BuildFlatTriangles(const std::uint32_t mode,
                        const std::span<const std::uint32_t> input,
                        std::vector<std::uint32_t>& vertices,
                        std::vector<std::uint32_t>& provoking) {
    vertices.clear();
    provoking.clear();
    const auto append = [&](const std::uint32_t a, const std::uint32_t b,
                            const std::uint32_t c) {
        vertices.insert(vertices.end(), {a, b, c});
        provoking.insert(provoking.end(), 3U, c);
    };
    if (mode == 0x0004U) {  // GL_TRIANGLES
        for (std::size_t index = 0; index + 2U < input.size(); index += 3U) {
            append(input[index], input[index + 1U], input[index + 2U]);
        }
    } else if (mode == 0x0005U) {  // GL_TRIANGLE_STRIP
        for (std::size_t index = 0; index + 2U < input.size(); ++index) {
            if ((index & 1U) == 0U) {
                append(input[index], input[index + 1U], input[index + 2U]);
            } else {
                append(input[index + 1U], input[index], input[index + 2U]);
            }
        }
    } else if (mode == 0x0006U) {  // GL_TRIANGLE_FAN
        for (std::size_t index = 1; index + 1U < input.size(); ++index) {
            append(input[0], input[index], input[index + 1U]);
        }
    }
}
[[nodiscard]] std::int32_t Signed(const std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}
[[nodiscard]] std::vector<std::uint32_t> DrawTextureUnits(
    const AndroidBoundaryGles1State& core) {
    auto enabled = core.EnabledTextureUnits();
    if (enabled.size() > kGles1MaximumDrawTextureUnits) {
        throw std::runtime_error(
            "GLES1 fixed draw exceeds its two texture-unit limit");
    }
    return enabled;
}

// A unit samples whichever of GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP is enabled,
// matching the fixed-function extension semantics.
[[nodiscard]] std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>
SampledTextureTargets(
    const AndroidBoundaryGles1State& core,
    const std::span<const std::uint32_t> texture_units) {
    std::array<std::uint32_t, kGles1MaximumDrawTextureUnits> targets{
        kTexture2d, kTexture2d};
    for (std::size_t stage = 0;
         stage < texture_units.size() && stage < targets.size(); ++stage) {
        if (core.Capability(texture_units[stage], kTextureCubeMap)) {
            targets[stage] = kTextureCubeMap;
        }
    }
    return targets;
}

[[nodiscard]] std::size_t ProgramVariantIndex(
    const std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>&
        sampled_targets) {
    std::size_t variant{};
    for (std::size_t stage = 0; stage < sampled_targets.size(); ++stage) {
        if (sampled_targets[stage] == kTextureCubeMap) {
            variant |= std::size_t{1U} << stage;
        }
    }
    return variant;
}

[[nodiscard]] std::string SubstituteShaderToken(
    const std::string_view source, const std::string_view from,
    const std::string_view to) {
    const auto first = source.find(from);
    if (first == std::string_view::npos ||
        source.find(from, first + 1U) != std::string_view::npos) {
        throw std::logic_error(
            "GLES1 fixed shader variant token is not unique: " +
            std::string{from});
    }
    std::string result{source};
    result.replace(first, from.size(), to);
    return result;
}

[[nodiscard]] std::pair<std::string, std::string> FixedShaderSourcesFor(
    const std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>&
        sampled_targets) {
    std::string vertex{kGles1FixedVertexShader};
    std::string fragment{kGles1FixedFragmentShader};
    for (std::size_t stage = 0; stage < sampled_targets.size(); ++stage) {
        if (sampled_targets[stage] != kTextureCubeMap) continue;
        const auto suffix = std::to_string(stage);
        vertex = SubstituteShaderToken(
            vertex, "varying vec2 v_texcoord" + suffix + ";",
            "varying vec3 v_texcoord" + suffix + ";");
        vertex = SubstituteShaderToken(
            vertex, "a_texcoord" + suffix + ").xy;",
            "a_texcoord" + suffix + ").xyz;");
        fragment = SubstituteShaderToken(
            fragment, "uniform sampler2D u_texture" + suffix + ";",
            "uniform samplerCube u_texture" + suffix + ";");
        fragment = SubstituteShaderToken(
            fragment, "varying vec2 v_texcoord" + suffix + ";",
            "varying vec3 v_texcoord" + suffix + ";");
        fragment = SubstituteShaderToken(
            fragment, "texture2D(u_texture" + suffix + ", v_texcoord" + suffix + ")",
            "textureCube(u_texture" + suffix + ", v_texcoord" + suffix + ")");
    }
    return {std::move(vertex), std::move(fragment)};
}
}  // namespace
std::uint64_t AndroidBoundaryGles1DrawState::ArrayKey(
    const std::uint32_t array, const std::uint32_t client_texture) noexcept {
    return (static_cast<std::uint64_t>(
                array == kGles1TextureCoordArray ? client_texture : kTexture0)
            << 32U) |
           array;
}

void AndroidBoundaryGles1DrawState::Reset() noexcept {
    arrays_.clear();
    arrays_[ArrayKey(kGles1VertexArray, kTexture0)] = {.size = 4, .type = kFloat};
    arrays_[ArrayKey(kGles1NormalArray, kTexture0)] = {.size = 3, .type = kFloat};
    arrays_[ArrayKey(kGles1ColorArray, kTexture0)] = {.size = 4, .type = kFloat};
    arrays_[ArrayKey(kGles1MatrixIndexArray, kTexture0)] =
        {.size = 4, .type = kUnsignedByte};
    arrays_[ArrayKey(kGles1WeightArray, kTexture0)] = {.size = 4, .type = kFloat};
    arrays_[ArrayKey(kGles1PointSizeArray, kTexture0)] = {.size = 1, .type = kFloat};
    for (auto texture = kTexture0; texture <= 0x84DFU; ++texture) {
        arrays_[ArrayKey(kGles1TextureCoordArray, texture)] =
            {.size = 4, .type = kFloat};
    }
    programs_ = {};
    current_palette_matrix_ = 0U;
}
AndroidBoundaryGles1DrawState::AndroidBoundaryGles1DrawState(
    const bool allow_single_stage_texcoord_fallback)
    : allow_single_stage_texcoord_fallback_(
          allow_single_stage_texcoord_fallback) {
    Reset();
}

void AndroidBoundaryGles1DrawState::SetEnabled(
    const std::uint32_t array, const std::uint32_t client_texture,
    const bool enabled) {
    if (array != kGles1VertexArray && array != kGles1NormalArray &&
        array != kGles1ColorArray && array != kGles1TextureCoordArray &&
        array != kGles1MatrixIndexArray && array != kGles1WeightArray &&
        array != kGles1PointSizeArray) {
        throw std::invalid_argument("GLES1 client state array is unsupported");
    }
    arrays_.at(ArrayKey(array, client_texture)).enabled = enabled;
}

void AndroidBoundaryGles1DrawState::SetPointer(
    const std::uint32_t array, const std::uint32_t client_texture,
    const std::int32_t size, const std::uint32_t type,
    const std::int32_t stride, const std::uint32_t pointer,
    const std::uint32_t buffer) {
    CommitPointer(array, client_texture,
                  PreparePointer(array, client_texture, size, type, stride,
                                 pointer, buffer));
}

Gles1ClientArray AndroidBoundaryGles1DrawState::PreparePointer(
    const std::uint32_t array, const std::uint32_t client_texture,
    const std::int32_t size, const std::uint32_t type,
    const std::int32_t stride, const std::uint32_t pointer,
    const std::uint32_t buffer) const {
    if (stride < 0) {
        throw gles::GlesApiError("GLES1 client array pointer", 0x0501U);
    }
    static_cast<void>(ScalarBytes(type));
    if ((array == kGles1VertexArray || array == kGles1TextureCoordArray) &&
        (size < 2 || size > 4)) {
        throw std::invalid_argument("GLES1 vertex/texture coordinate size is outside 2..4");
    }
    if (array == kGles1ColorArray && size != 4) {
        throw std::invalid_argument("GLES1 color array size must be four");
    }
    if (array == kGles1NormalArray && size != 3) {
        throw std::invalid_argument("GLES1 normal array size must be three");
    }
    if ((array == kGles1MatrixIndexArray || array == kGles1WeightArray) &&
        (size < 1 || size > 4)) {
        throw std::invalid_argument("GLES1 matrix palette array size is outside 1..4");
    }
    if (array == kGles1MatrixIndexArray && type != kUnsignedByte) {
        throw std::invalid_argument("GLES1 matrix index array requires GL_UNSIGNED_BYTE");
    }
    if (array == kGles1WeightArray && type != kFloat && type != kFixed) {
        throw std::invalid_argument("GLES1 weight array requires GL_FLOAT or GL_FIXED");
    }
    if (array == kGles1PointSizeArray &&
        (size != 1 || (type != kFloat && type != kFixed))) {
        throw std::invalid_argument(
            "GLES1 point-size array requires size one and GL_FLOAT or GL_FIXED");
    }
    const auto enabled = arrays_.at(ArrayKey(array, client_texture)).enabled;
    return {.size = size, .type = type, .stride = stride,
            .pointer = pointer, .buffer = buffer, .enabled = enabled};
}

void AndroidBoundaryGles1DrawState::CommitPointer(
    const std::uint32_t array, const std::uint32_t client_texture,
    Gles1ClientArray pointer) {
    arrays_.at(ArrayKey(array, client_texture)) = pointer;
}

const Gles1ClientArray& AndroidBoundaryGles1DrawState::Array(
    const std::uint32_t array, const std::uint32_t client_texture) const {
    return arrays_.at(ArrayKey(array, client_texture));
}

void AndroidBoundaryGles1DrawState::SetCurrentPaletteMatrix(
    const std::uint32_t index) {
    ValidateCurrentPaletteMatrix(index);
    current_palette_matrix_ = index;
}

void AndroidBoundaryGles1DrawState::ValidateCurrentPaletteMatrix(
    const std::uint32_t index) {
    constexpr std::uint32_t kMaximumPaletteMatrices = 32U;
    if (index >= kMaximumPaletteMatrices) {
        throw std::invalid_argument("GLES1 palette matrix index is outside 0..31");
    }
}

std::uint32_t AndroidBoundaryGles1DrawState::CurrentPaletteMatrix() const noexcept {
    return current_palette_matrix_;
}

std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>
AndroidBoundaryGles1DrawState::ResolveTextureCoordinateUnits(
    const std::span<const std::uint32_t> texture_units) const {
    std::array<std::uint32_t, kGles1MaximumDrawTextureUnits> resolved{};
    for (std::size_t stage = 0; stage < texture_units.size(); ++stage) {
        const auto texture = texture_units[stage];
        if (Array(kGles1TextureCoordArray, texture).enabled) {
            resolved[stage] = texture;
            continue;
        }
        std::optional<std::uint32_t> unique_enabled;
        bool ambiguous{};
        for (auto candidate = kTexture0; candidate <= 0x84DFU; ++candidate) {
            if (!Array(kGles1TextureCoordArray, candidate).enabled) continue;
            if (unique_enabled.has_value()) {
                ambiguous = true;
                break;
            }
            unique_enabled = candidate;
        }
        if (!unique_enabled.has_value()) {
            resolved[stage] = texture;
            continue;
        }
        if (texture_units.size() != 1U) {
            throw std::runtime_error(
                "GLES1 multi-stage draw has no texture coordinate array for "
                "one sampled unit; coordinate arrays cannot be shared");
        }
        if (ambiguous) {
            throw std::runtime_error(
                "GLES1 single-stage texture coordinate array fallback is "
                "ambiguous");
        }
        if (!allow_single_stage_texcoord_fallback_) {
            throw std::runtime_error(
                "GLES1 single-stage texture coordinate array fallback is "
                "disabled");
        }
        resolved[stage] = *unique_enabled;
    }
    return resolved;
}

AndroidBoundaryGles1DrawState::Program&
AndroidBoundaryGles1DrawState::EnsureProgram(
    gles::AngleFrame& frame,
    const std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>&
        sampled_targets) {
    auto& program = programs_[ProgramVariantIndex(sampled_targets)];
    if (program.name != 0U) return program;
    const auto [vertex, fragment] = FixedShaderSourcesFor(sampled_targets);
    const auto vertex_shader = frame.CreateShader(0x8B31U);
    frame.ShaderSource(vertex_shader, std::span(&vertex, 1));
    frame.CompileShader(vertex_shader);
    if (frame.GetShaderParameter(vertex_shader, 0x8B81U) == 0) {
        throw std::runtime_error("GLES1 fixed vertex shader compilation failed");
    }
    const auto fragment_shader = frame.CreateShader(0x8B30U);
    frame.ShaderSource(fragment_shader, std::span(&fragment, 1));
    frame.CompileShader(fragment_shader);
    if (frame.GetShaderParameter(fragment_shader, 0x8B81U) == 0) {
        throw std::runtime_error("GLES1 fixed fragment shader compilation failed");
    }
    program.name = frame.CreateProgram();
    frame.AttachShader(program.name, vertex_shader);
    frame.AttachShader(program.name, fragment_shader);
    frame.LinkProgram(program.name);
    if (frame.GetProgramParameter(program.name, 0x8B82U) == 0) {
        throw std::runtime_error("GLES1 fixed program link failed");
    }
    frame.DeleteShader(vertex_shader);
    frame.DeleteShader(fragment_shader);
    constexpr std::array attribute_names{
        "a_position", "a_normal", "a_color", "a_point_size",
        "a_texcoord0", "a_texcoord1"};
    for (std::size_t index = 0; index < attribute_names.size(); ++index) {
        program.attributes[index] =
            frame.GetAttribLocation(program.name, attribute_names[index]);
    }
    constexpr std::array common_uniforms{
        "u_modelview0", "u_modelview1", "u_modelview2", "u_modelview3",
        "u_projection0", "u_projection1", "u_projection2", "u_projection3",
        "u_normal_matrix", "u_current_color", "u_current_normal",
        "u_global_ambient", "u_material_front_ambient",
        "u_material_front_diffuse", "u_material_front_specular",
        "u_material_front_emission", "u_material_front_shininess",
        "u_material_back_ambient", "u_material_back_diffuse",
        "u_material_back_specular", "u_material_back_emission",
        "u_material_back_shininess", "u_light_model_two_side",
        "u_color_material", "u_normalize_normal", "u_rescale_normal",
        "u_has_color", "u_has_normal", "u_has_point_size", "u_lighting", "u_point_size",
        "u_point_size_min", "u_point_size_max", "u_point_distance_attenuation",
        "u_fog_enabled",
        "u_fog_mode", "u_fog_density", "u_fog_start", "u_fog_end",
        "u_fog_color", "u_alpha_enabled", "u_alpha_function",
        "u_alpha_reference"};
    for (const auto* name : common_uniforms) {
        program.uniforms[name] = frame.GetUniformLocation(program.name, name);
    }
    for (std::size_t plane = 0; plane < 6U; ++plane) {
        const auto suffix = "[" + std::to_string(plane) + "]";
        for (const auto* base : {"u_clip_plane", "u_clip_enabled"}) {
            const auto name = std::string{base} + suffix;
            program.uniforms[name] = frame.GetUniformLocation(program.name, name);
        }
    }
    for (std::size_t light = 0; light < 8U; ++light) {
        const auto suffix = "[" + std::to_string(light) + "]";
        for (const auto* base : {"u_light_enabled", "u_light_ambient",
                                 "u_light_diffuse", "u_light_specular",
                                 "u_light_position", "u_light_spot_direction",
                                 "u_light_attenuation", "u_light_spot_exponent",
                                 "u_light_spot_cutoff_cos"}) {
            const auto name = std::string{base} + suffix;
            program.uniforms[name] = frame.GetUniformLocation(program.name, name);
        }
    }
    constexpr std::array stage_uniforms{
        "u_texture_enabled", "u_texture_environment", "u_texture_format",
        "u_environment_color", "u_combine_rgb", "u_combine_alpha",
        "u_source_rgb0", "u_source_rgb1", "u_source_rgb2",
        "u_source_alpha0", "u_source_alpha1", "u_source_alpha2",
        "u_operand_rgb0", "u_operand_rgb1", "u_operand_rgb2",
        "u_operand_alpha0", "u_operand_alpha1", "u_operand_alpha2",
        "u_rgb_scale", "u_alpha_scale"};
    for (std::size_t stage = 0; stage < kGles1MaximumDrawTextureUnits; ++stage) {
        const auto suffix = std::to_string(stage);
        const auto sampler = std::string{"u_texture"} + suffix;
        program.uniforms[sampler] =
            frame.GetUniformLocation(program.name, sampler);
        for (std::size_t column = 0; column < 4U; ++column) {
            const auto name = std::string{"u_texture"} + suffix + "_matrix" +
                              std::to_string(column);
            program.uniforms[name] =
                frame.GetUniformLocation(program.name, name);
        }
        for (const auto* base : stage_uniforms) {
            const auto name = std::string{base} + "[" + suffix + "]";
            program.uniforms[name] =
                frame.GetUniformLocation(program.name, name);
        }
    }
    const auto buffers = frame.GenerateBuffers(program.buffers.size());
    std::ranges::copy(buffers, program.buffers.begin());
    return program;
}

void AndroidBoundaryGles1DrawState::PrepareArrays(
    gles::AngleFrame& frame, const Program& program,
    const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space,
    const std::span<const std::uint32_t> texture_units,
    const std::uint32_t maximum_index, const std::uint64_t thread_id,
    const std::span<const std::uint32_t> flat_vertices,
    const std::span<const std::uint32_t> flat_provoking) {
    if (Array(kGles1MatrixIndexArray, kTexture0).enabled ||
        Array(kGles1WeightArray, kTexture0).enabled) {
        throw std::runtime_error(
            "GLES1 matrix-palette skinning draw conversion is not implemented");
    }
    const auto prepare = [&](const std::size_t index,
                             const Gles1ClientArray& array,
                             const bool normalized) {
        const auto location = program.attributes[index];
        if (location < 0) return;
        frame.SetVertexAttributeEnabled(static_cast<std::uint32_t>(location),
                                        array.enabled);
        if (!array.enabled) return;
        if (array.size == 0) {
            throw std::logic_error(
                "GLES1 enabled client array has no pointer definition");
        }
        std::uint32_t offset = array.pointer;
        if (!flat_vertices.empty()) {
            const auto packed = static_cast<std::size_t>(array.size) *
                                ScalarBytes(array.type);
            const auto stride = array.stride == 0
                                    ? packed
                                    : static_cast<std::size_t>(array.stride);
            std::span<const std::byte> source_bytes;
            std::size_t source_offset{};
            if (array.buffer == 0U) {
                source_bytes = gles::PrepareGuestInput(
                    address_space, memory::GuestAddress{array.pointer},
                    ArrayBytes(array, maximum_index), false,
                    client_array_staging_[index], thread_id);
            } else {
                const auto* contents = core.BufferContents(array.buffer);
                if (contents == nullptr) {
                    throw std::runtime_error(
                        "GLES1 flat shading requires defined buffer contents");
                }
                source_bytes = *contents;
                source_offset = array.pointer;
            }
            const auto& sources = index == 1U || index == 2U
                                      ? flat_provoking
                                      : flat_vertices;
            std::vector<std::byte> expanded(sources.size() * packed);
            for (std::size_t vertex = 0; vertex < sources.size(); ++vertex) {
                const auto source = static_cast<std::size_t>(sources[vertex]);
                const auto begin = source_offset + source * stride;
                if (begin > source_bytes.size() ||
                    packed > source_bytes.size() - begin) {
                    throw std::runtime_error(
                        "GLES1 flat shading array exceeds buffer contents");
                }
                std::ranges::copy_n(
                    source_bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                    packed,
                    expanded.begin() + static_cast<std::ptrdiff_t>(vertex * packed));
            }
            frame.BindBuffer(kArrayBuffer, program.buffers[index]);
            frame.BufferData(kArrayBuffer,
                             static_cast<std::uint32_t>(expanded.size()),
                             expanded, kStaticDraw);
            offset = 0U;
            frame.VertexAttributePointer(static_cast<std::uint32_t>(location),
                                         array.size, array.type, normalized,
                                         0, offset);
            return;
        }
        if (array.buffer == 0U) {
            const auto transfer = gles::PrepareGuestInput(
                address_space, memory::GuestAddress{array.pointer},
                ArrayBytes(array, maximum_index),
                false, client_array_staging_[index], thread_id);
            frame.BindBuffer(kArrayBuffer, program.buffers[index]);
            frame.BufferData(kArrayBuffer,
                             static_cast<std::uint32_t>(transfer.size()),
                             transfer, kStaticDraw);
            offset = 0U;
        } else {
            frame.BindBuffer(kArrayBuffer, array.buffer);
        }
        frame.VertexAttributePointer(static_cast<std::uint32_t>(location),
                                     array.size, array.type, normalized,
                                     array.stride, offset);
    };
    constexpr std::array arrays{kGles1VertexArray, kGles1NormalArray,
                                kGles1ColorArray, kGles1PointSizeArray};
    for (std::size_t index = 0; index < arrays.size(); ++index) {
        const auto& array = Array(arrays[index], kTexture0);
        prepare(index, array,
                arrays[index] == kGles1ColorArray &&
                    array.type == kUnsignedByte);
    }
    const auto coordinate_units = ResolveTextureCoordinateUnits(texture_units);
    for (std::size_t stage = 0; stage < kGles1MaximumDrawTextureUnits; ++stage) {
        const auto location = program.attributes[arrays.size() + stage];
        if (stage >= texture_units.size()) {
            if (location >= 0) {
                frame.SetVertexAttributeEnabled(
                    static_cast<std::uint32_t>(location), false);
            }
            continue;
        }
        const auto texture = texture_units[stage];
        const auto& array = Array(kGles1TextureCoordArray,
                                  coordinate_units[stage]);
        prepare(arrays.size() + stage, array, false);
        if (!array.enabled && location >= 0) {
            const auto& coordinate = legacy.CurrentTextureCoordinate(texture);
            frame.VertexAttribute4f(static_cast<std::uint32_t>(location),
                                    coordinate[0], coordinate[1],
                                    coordinate[2], coordinate[3]);
        }
    }
    frame.BindBuffer(kArrayBuffer, core.TransferState().Snapshot().array_buffer);
}

void AndroidBoundaryGles1DrawState::ApplyUniforms(
    gles::AngleFrame& frame, const Program& program,
    const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    const std::span<const std::uint32_t> texture_units,
    const std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>&
        sampled_targets) {
    const auto modelview = MatrixFor(core, kGles1Modelview);
    const auto projection = MatrixFor(core, kGles1Projection);
    const auto normal = NormalMatrix(modelview);
    const auto uniform = [&program](const std::string_view name) {
        return program.uniforms.at(std::string{name});
    };
    const auto set_matrix = [&frame, &uniform](const std::string_view prefix,
                                               const Gles1Matrix& value) {
        for (std::size_t column = 0; column < 4U; ++column) {
            const auto name = std::string{prefix} + std::to_string(column);
            frame.Uniform4f(uniform(name), value[column * 4U],
                            value[column * 4U + 1U], value[column * 4U + 2U],
                            value[column * 4U + 3U]);
        }
    };
    set_matrix("u_modelview", modelview);
    set_matrix("u_projection", projection);
    frame.UniformMatrix3(uniform("u_normal_matrix"), 1, false, normal);
    const bool normalize = core.Capability(0x0BA1U);
    const bool rescale = core.Capability(0x803AU);
    frame.Uniform1f(uniform("u_normalize_normal"), normalize ? 1.0F : 0.0F);
    const float rescale_factor = rescale
        ? std::sqrt(modelview[2] * modelview[2] +
                    modelview[6] * modelview[6] +
                    modelview[10] * modelview[10])
        : 1.0F;
    frame.Uniform1f(uniform("u_rescale_normal"), rescale_factor);
    const auto& color = legacy.Color();
    frame.Uniform4f(uniform("u_current_color"), color[0], color[1], color[2],
                    color[3]);
    const auto& current_normal = legacy.Normal();
    frame.Uniform4f(uniform("u_current_normal"), current_normal[0],
                    current_normal[1], current_normal[2], 0.0F);
    const auto& fixed = core.Fixed();
    const auto set4 = [&frame, &uniform](const std::string_view name,
                                         const std::vector<float>& value) {
        frame.Uniform4f(uniform(name), value[0], value[1], value[2], value[3]);
    };
    set4("u_global_ambient", fixed.LightModel(kGles1LightModelAmbient));
    const auto set_material = [&fixed, &set4, &frame, &uniform](
                                  const std::string_view prefix,
                                  const std::uint32_t face) {
        set4(std::string{prefix} + "_ambient", fixed.Material(face, 0x1200U));
        set4(std::string{prefix} + "_diffuse", fixed.Material(face, 0x1201U));
        set4(std::string{prefix} + "_specular", fixed.Material(face, 0x1202U));
        set4(std::string{prefix} + "_emission", fixed.Material(face, 0x1600U));
        frame.Uniform1f(uniform(std::string{prefix} + "_shininess"),
                        fixed.Material(face, 0x1601U)[0]);
    };
    set_material("u_material_front", 0x0404U);
    set_material("u_material_back", 0x0405U);
    frame.Uniform1f(uniform("u_light_model_two_side"),
                    fixed.LightModel(0x0B52U)[0] != 0.0F ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_color_material"),
                    core.Capability(0x0B57U) ? 1.0F : 0.0F);
    for (std::size_t index = 0; index < 8U; ++index) {
        const auto light = 0x4000U + static_cast<std::uint32_t>(index);
        const auto suffix = "[" + std::to_string(index) + "]";
        const auto indexed = [&suffix](const std::string_view name) {
            return std::string{name} + suffix;
        };
        frame.Uniform1f(uniform(indexed("u_light_enabled")),
                        core.Capability(light) ? 1.0F : 0.0F);
        for (const auto [name, pname] :
             {std::pair{"u_light_ambient", 0x1200U},
              std::pair{"u_light_diffuse", 0x1201U},
              std::pair{"u_light_specular", 0x1202U},
              std::pair{"u_light_position", 0x1203U}}) {
            const auto& value = fixed.Light(light, pname);
            frame.Uniform4f(uniform(indexed(name)), value[0], value[1],
                            value[2], value[3]);
        }
        const auto& direction = fixed.Light(light, 0x1204U);
        frame.Uniform4f(uniform(indexed("u_light_spot_direction")),
                        direction[0], direction[1], direction[2], 0.0F);
        const auto& constant = fixed.Light(light, 0x1207U);
        const auto& linear = fixed.Light(light, 0x1208U);
        const auto& quadratic = fixed.Light(light, 0x1209U);
        frame.Uniform4f(uniform(indexed("u_light_attenuation")),
                        constant[0], linear[0], quadratic[0], 0.0F);
        frame.Uniform1f(uniform(indexed("u_light_spot_exponent")),
                        fixed.Light(light, 0x1205U)[0]);
        const auto cutoff = fixed.Light(light, 0x1206U)[0];
        frame.Uniform1f(uniform(indexed("u_light_spot_cutoff_cos")),
                        cutoff == 180.0F ? -1.0F
                                         : std::cos(cutoff * std::numbers::pi_v<float> / 180.0F));
    }
    frame.Uniform1f(uniform("u_has_color"),
                    Array(kGles1ColorArray, kTexture0).enabled ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_has_normal"),
                    Array(kGles1NormalArray, kTexture0).enabled ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_has_point_size"),
                    Array(kGles1PointSizeArray, kTexture0).enabled ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_lighting"),
                    core.Capability(0x0B50U) ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_point_size"), fixed.PointSize());
    frame.Uniform1f(uniform("u_point_size_min"),
                    fixed.PointParameter(0x8126U));
    frame.Uniform1f(uniform("u_point_size_max"),
                    fixed.PointParameter(0x8127U));
    const auto& attenuation = fixed.PointDistanceAttenuation();
    frame.Uniform4f(uniform("u_point_distance_attenuation"), attenuation[0],
                    attenuation[1], attenuation[2], 0.0F);
    for (std::size_t index = 0; index < 6U; ++index) {
        const auto suffix = "[" + std::to_string(index) + "]";
        const auto& plane = legacy.ClipPlane(0x3000U +
                                             static_cast<std::uint32_t>(index));
        frame.Uniform4f(uniform("u_clip_plane" + suffix), plane[0], plane[1],
                        plane[2], plane[3]);
        frame.Uniform1f(uniform("u_clip_enabled" + suffix),
                        core.Capability(0x3000U +
                                        static_cast<std::uint32_t>(index))
                            ? 1.0F : 0.0F);
    }
    for (std::size_t stage = 0; stage < kGles1MaximumDrawTextureUnits; ++stage) {
        const auto suffix = std::to_string(stage);
        const auto indexed = [&suffix](const std::string_view name) {
            return std::string{name} + "[" + suffix + "]";
        };
        frame.Uniform1f(uniform(indexed("u_texture_enabled")),
                        stage < texture_units.size() ? 1.0F : 0.0F);
        // Inactive stages park their sampler on the first texture-less unit
        // so a cube variant never mixes sampler types on one unit, which
        // GLES rejects with INVALID_OPERATION at draw time.
        frame.Uniform1i(uniform(std::string{"u_texture"} + suffix),
                        stage < texture_units.size()
                            ? static_cast<std::int32_t>(
                                  texture_units[stage] - kTexture0)
                            : static_cast<std::int32_t>(
                                  kGles1MaximumDrawTextureUnits));
        if (stage >= texture_units.size()) continue;
        const auto texture = texture_units[stage];
        set_matrix(std::string{"u_texture"} + suffix + "_matrix",
                   MatrixFor(core, kGles1Texture, texture));
        const auto environment = static_cast<std::uint32_t>(
            legacy.TextureEnvironment(
                texture, kGles1TextureEnvironmentMode)[0]);
        if (environment != kModulate && environment != kReplace &&
            environment != kAdd && environment != kCombine &&
            environment != kBlend && environment != kDecal) {
            throw std::runtime_error(
                "GLES1 draw does not implement texture environment mode " +
                std::to_string(environment));
        }
        frame.Uniform1i(uniform(indexed("u_texture_environment")),
                        static_cast<std::int32_t>(environment));
        const auto base_format =
            core.TextureBaseFormat(texture, sampled_targets[stage]);
        if (!base_format.has_value()) {
            throw std::runtime_error(
                "GLES1 textured draw has no level-zero base format");
        }
        frame.Uniform1i(
            uniform(indexed("u_texture_format")),
            static_cast<std::int32_t>(ClassifyTextureFormat(*base_format)));
        const auto value = [&legacy, texture](const std::uint32_t pname) {
            return legacy.TextureEnvironment(texture, pname)[0];
        };
        const auto& environment_color = legacy.TextureEnvironment(
            texture, kGles1TextureEnvironmentColor);
        frame.Uniform4f(uniform(indexed("u_environment_color")),
                        environment_color[0], environment_color[1],
                        environment_color[2], environment_color[3]);
        frame.Uniform1i(uniform(indexed("u_combine_rgb")),
                        static_cast<std::int32_t>(value(kGles1CombineRgb)));
        frame.Uniform1i(uniform(indexed("u_combine_alpha")),
                        static_cast<std::int32_t>(value(kGles1CombineAlpha)));
        const auto set3 = [&frame, &uniform, &indexed, &value](
                              const std::string_view base,
                              const std::uint32_t first) {
            for (std::size_t index = 0; index < 3U; ++index) {
                frame.Uniform1i(
                    uniform(indexed(std::string{base} + std::to_string(index))),
                    static_cast<std::int32_t>(
                        value(first + static_cast<std::uint32_t>(index))));
            }
        };
        set3("u_source_rgb", kGles1Source0Rgb);
        set3("u_source_alpha", kGles1Source0Alpha);
        set3("u_operand_rgb", kGles1Operand0Rgb);
        set3("u_operand_alpha", kGles1Operand0Alpha);
        frame.Uniform1f(uniform(indexed("u_rgb_scale")),
                        value(kGles1RgbScale));
        frame.Uniform1f(uniform(indexed("u_alpha_scale")),
                        value(kGles1AlphaScale));
    }
    frame.Uniform1f(uniform("u_fog_enabled"),
                    core.Capability(0x0B60U) ? 1.0F : 0.0F);
    const auto fog_mode = fixed.Fog(kGles1FogMode)[0];
    frame.Uniform1i(uniform("u_fog_mode"),
                    fog_mode == 0x0800U ? 1 : fog_mode == 0x0801U ? 2 : 0);
    frame.Uniform1f(uniform("u_fog_density"), fixed.Fog(kGles1FogDensity)[0]);
    frame.Uniform1f(uniform("u_fog_start"), fixed.Fog(0x0B63U)[0]);
    frame.Uniform1f(uniform("u_fog_end"), fixed.Fog(0x0B64U)[0]);
    set4("u_fog_color", fixed.Fog(kGles1FogColor));
    frame.Uniform1f(uniform("u_alpha_enabled"),
                    core.Capability(0x0BC0U) ? 1.0F : 0.0F);
    frame.Uniform1i(uniform("u_alpha_function"),
                    static_cast<std::int32_t>(legacy.AlphaFunction()));
    frame.Uniform1f(uniform("u_alpha_reference"), legacy.AlphaReference());
}

void AndroidBoundaryGles1DrawState::DrawArrays(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space, const std::uint32_t mode,
    const std::int32_t first, const std::int32_t count,
    const std::uint64_t thread_id) {
    if (first < 0 || count < 0) {
        throw gles::GlesApiError("glDrawArrays", 0x0501U);
    }
    if (!Array(kGles1VertexArray, kTexture0).enabled) {
        throw std::runtime_error("GLES1 draw requires GL_VERTEX_ARRAY");
    }
    if (count == 0) return;
    const auto maximum = static_cast<std::uint64_t>(first) +
                         static_cast<std::uint64_t>(count) - 1U;
    if (maximum > (std::numeric_limits<std::uint32_t>::max)()) {
        throw std::length_error("GLES1 draw array index overflows");
    }
    const auto texture_units = DrawTextureUnits(core);
    const auto sampled_targets = SampledTextureTargets(core, texture_units);
    auto& program = EnsureProgram(frame, sampled_targets);
    frame.UseProgram(program.name);
    std::vector<std::uint32_t> source_indices(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        source_indices[static_cast<std::size_t>(index)] =
            static_cast<std::uint32_t>(first + index);
    }
    const bool flat = core.ShadeModel() == kGles1FlatShadeModel &&
                      (mode == 0x0004U || mode == 0x0005U || mode == 0x0006U);
    if (flat) {
        BuildFlatTriangles(mode, source_indices, flat_vertices_,
                           flat_provoking_);
    } else {
        flat_vertices_.clear();
        flat_provoking_.clear();
    }
    PrepareArrays(frame, program, core, legacy, address_space, texture_units,
                  static_cast<std::uint32_t>(maximum), thread_id,
                  flat_vertices_, flat_provoking_);
    ApplyUniforms(frame, program, core, legacy, texture_units, sampled_targets);
    if (maximum > (std::numeric_limits<std::uint16_t>::max)()) {
        throw std::length_error("GLES1 emulated draw-array index exceeds GLushort");
    }
    const auto index_count = flat ? flat_vertices_.size()
                                  : static_cast<std::size_t>(count);
    if (draw_array_indices_.size() < index_count) {
        draw_array_indices_.resize(index_count);
    }
    const auto draw_indices = std::span(draw_array_indices_).first(index_count);
    for (std::size_t index = 0; index < index_count; ++index) {
        draw_indices[index] = flat
            ? static_cast<std::uint16_t>(index)
            : static_cast<std::uint16_t>(
                  first + static_cast<std::int32_t>(index));
    }
    frame.BindBuffer(kElementArrayBuffer, program.buffers.back());
    frame.BufferData(kElementArrayBuffer,
                     static_cast<std::uint32_t>(draw_indices.size() *
                                                sizeof(std::uint16_t)),
                     std::as_bytes(draw_indices), kStaticDraw);
    frame.DrawElements(flat ? 0x0004U : mode,
                       static_cast<std::int32_t>(index_count),
                       kUnsignedShort, 0U);
    frame.BindBuffer(kElementArrayBuffer,
                     core.TransferState().Snapshot().element_array_buffer);
}

void AndroidBoundaryGles1DrawState::DrawElements(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space, const std::uint32_t mode,
    const std::int32_t count, const std::uint32_t type,
    const std::uint32_t indices, const std::uint64_t thread_id) {
    if (count < 0) throw gles::GlesApiError("glDrawElements", 0x0501U);
    if (type != kUnsignedByte && type != kUnsignedShort) {
        throw std::invalid_argument("GLES1 draw index type is unsupported");
    }
    if (!Array(kGles1VertexArray, kTexture0).enabled) {
        throw std::runtime_error("GLES1 draw requires GL_VERTEX_ARRAY");
    }
    if (count == 0) return;
    const auto guest_element_buffer =
        core.TransferState().Snapshot().element_array_buffer;
    std::uint32_t maximum{};
    std::span<const std::byte> transferred;
    if (guest_element_buffer == 0U) {
        transferred = gles::PrepareGuestInput(
            address_space, memory::GuestAddress{indices},
            static_cast<std::uint64_t>(count) * ScalarBytes(type),
            false, element_staging_, thread_id);
        maximum = MaximumIndex(transferred, type);
    } else {
        const auto* contents = core.BufferContents(guest_element_buffer);
        const auto bytes = static_cast<std::size_t>(count) * ScalarBytes(type);
        if (contents != nullptr && indices <= contents->size() &&
            bytes <= contents->size() - indices) {
            transferred = std::span<const std::byte>(*contents).subspan(indices,
                                                                        bytes);
            maximum = MaximumIndex(transferred, type);
        } else if (core.ShadeModel() == kGles1FlatShadeModel &&
                   (mode == 0x0004U || mode == 0x0005U || mode == 0x0006U)) {
            throw std::runtime_error(
                "GLES1 draw indices exceed defined element buffer contents");
        } else {
            for (const auto kind : {kGles1VertexArray, kGles1NormalArray,
                                    kGles1ColorArray}) {
                const auto& array = Array(kind, kTexture0);
                if (array.enabled && array.buffer == 0U) {
                    throw std::runtime_error(
                        "GLES1 cannot bound guest client arrays from an opaque element buffer");
                }
            }
            const auto texture_units = DrawTextureUnits(core);
            const auto coordinate_units =
                ResolveTextureCoordinateUnits(texture_units);
            for (const auto coordinate_unit :
                 std::span(coordinate_units).first(texture_units.size())) {
                const auto& array = Array(kGles1TextureCoordArray,
                                          coordinate_unit);
                if (array.enabled && array.buffer == 0U) {
                    throw std::runtime_error(
                        "GLES1 cannot bound guest client arrays from an opaque element buffer");
                }
            }
        }
    }
    const auto texture_units = DrawTextureUnits(core);
    const auto sampled_targets = SampledTextureTargets(core, texture_units);
    auto& program = EnsureProgram(frame, sampled_targets);
    frame.UseProgram(program.name);
    const bool flat = core.ShadeModel() == kGles1FlatShadeModel &&
                      (mode == 0x0004U || mode == 0x0005U || mode == 0x0006U);
    if (flat) {
        std::vector<std::uint32_t> source_indices(
            static_cast<std::size_t>(count));
        for (std::size_t index = 0; index < source_indices.size(); ++index) {
            source_indices[index] = type == kUnsignedByte
                ? std::to_integer<std::uint8_t>(transferred[index])
                : static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(transferred[index * 2U])) |
                  (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                       transferred[index * 2U + 1U])) << 8U);
        }
        BuildFlatTriangles(mode, source_indices, flat_vertices_,
                           flat_provoking_);
        if (flat_vertices_.size() >
            (std::numeric_limits<std::uint16_t>::max)()) {
            throw std::length_error("GLES1 flat draw expansion exceeds GLushort");
        }
    } else {
        flat_vertices_.clear();
        flat_provoking_.clear();
    }
    PrepareArrays(frame, program, core, legacy, address_space, texture_units,
                  maximum, thread_id, flat_vertices_, flat_provoking_);
    ApplyUniforms(frame, program, core, legacy, texture_units, sampled_targets);
    if (flat) {
        const auto count_flat = flat_vertices_.size();
        if (draw_array_indices_.size() < count_flat) {
            draw_array_indices_.resize(count_flat);
        }
        const auto draw_indices =
            std::span(draw_array_indices_).first(count_flat);
        for (std::size_t index = 0; index < count_flat; ++index) {
            draw_indices[index] = static_cast<std::uint16_t>(index);
        }
        frame.BindBuffer(kElementArrayBuffer, program.buffers.back());
        frame.BufferData(kElementArrayBuffer,
                         static_cast<std::uint32_t>(draw_indices.size_bytes()),
                         std::as_bytes(draw_indices), kStaticDraw);
        frame.DrawElements(0x0004U, static_cast<std::int32_t>(count_flat),
                           kUnsignedShort, 0U);
    } else if (guest_element_buffer == 0U) {
        frame.BindBuffer(kElementArrayBuffer, program.buffers.back());
        frame.BufferData(kElementArrayBuffer,
                         static_cast<std::uint32_t>(transferred.size()),
                         transferred, kStaticDraw);
        frame.DrawElements(mode, count, type, 0U);
    } else {
        frame.BindBuffer(kElementArrayBuffer, guest_element_buffer);
        frame.DrawElements(mode, count, type, indices);
    }
    frame.BindBuffer(kElementArrayBuffer, guest_element_buffer);
}

void BindAndroidBoundaryGles1Draw(
    gles::GlesDispatchTable& dispatch, gles::GlesDispatchTable& extensions,
    AndroidBoundaryGles1DrawState& draw,
    AndroidBoundaryGles1State& core, AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) throw std::invalid_argument("GLES1 draw frame resolver is missing");
    const auto set_enabled = [&draw, &legacy, require_frame](
                                 const std::span<const std::uint32_t> arguments,
                                 const bool enabled, const std::string_view operation) {
        static_cast<void>(require_frame(operation));
        draw.SetEnabled(arguments[0], legacy.ClientActiveTexture(), enabled);
        return 0U;
    };
    dispatch.Bind("glEnableClientState", [set_enabled](const auto arguments, const auto) {
        return set_enabled(arguments, true, "glEnableClientState");
    });
    dispatch.Bind("glDisableClientState", [set_enabled](const auto arguments, const auto) {
        return set_enabled(arguments, false, "glDisableClientState");
    });
    dispatch.Bind("glIsEnabled", [&draw, &core, &legacy, require_frame](
                                     const auto arguments, const auto) {
        const auto client = Gles1ClientStateEnabled(arguments[0], draw, legacy);
        const auto enabled = client.has_value()
                                 ? *client
                                 : core.Capability(arguments[0]);
        static_cast<void>(require_frame("glIsEnabled"));
        return enabled ? 1U : 0U;
    });
    const auto set_pointer = [&draw, &core, &legacy, require_frame](
                                 const std::uint32_t array,
                                 const std::span<const std::uint32_t> arguments,
                                 const std::int32_t size,
                                 const std::uint32_t type_index,
                                 const std::uint32_t stride_index,
                                 const std::uint32_t pointer_index,
                                 const std::string_view operation) {
        const auto client_texture = legacy.ClientActiveTexture();
        auto next = draw.PreparePointer(
            array, client_texture, size, arguments[type_index],
            Signed(arguments[stride_index]), arguments[pointer_index],
            core.TransferState().Snapshot().array_buffer);
        static_cast<void>(require_frame(operation));
        draw.CommitPointer(array, client_texture, next);
        return 0U;
    };
    dispatch.Bind("glColorPointer", [set_pointer](const auto arguments, const auto) {
        return set_pointer(kGles1ColorArray, arguments, Signed(arguments[0]), 1, 2, 3,
                           "glColorPointer");
    });
    dispatch.Bind("glNormalPointer", [set_pointer](const auto arguments, const auto) {
        return set_pointer(kGles1NormalArray, arguments, 3, 0, 1, 2,
                           "glNormalPointer");
    });
    dispatch.Bind("glTexCoordPointer", [set_pointer](const auto arguments, const auto) {
        return set_pointer(kGles1TextureCoordArray, arguments, Signed(arguments[0]), 1, 2, 3,
                           "glTexCoordPointer");
    });
    dispatch.Bind("glVertexPointer", [set_pointer](const auto arguments, const auto) {
        return set_pointer(kGles1VertexArray, arguments, Signed(arguments[0]), 1, 2, 3,
                           "glVertexPointer");
    });
    dispatch.Bind("glGetPointerv", [&draw, &legacy, &address_space, require_frame](
                                        const auto arguments,
                                        const std::uint64_t thread_id) {
        std::uint32_t array{};
        switch (arguments[0]) {
        case 0x808EU: array = kGles1VertexArray; break;
        case 0x808FU: array = kGles1NormalArray; break;
        case 0x8090U: array = kGles1ColorArray; break;
        case 0x8092U: array = kGles1TextureCoordArray; break;
        default:
            throw std::invalid_argument("GLES1 pointer query is unsupported");
        }
        auto output = gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{arguments[1]},
            sizeof(std::uint32_t), gles::GuestTransferDirection::output,
            false, thread_id);
        static_cast<void>(require_frame("glGetPointerv"));
        const auto texture = array == kGles1TextureCoordArray
                                 ? legacy.ClientActiveTexture()
                                 : kTexture0;
        const auto pointer = draw.Array(array, texture).pointer;
        auto bytes = output.WritableBytes();
        for (std::size_t index = 0; index < sizeof(pointer); ++index) {
            bytes[index] = static_cast<std::byte>(pointer >> (index * 8U));
        }
        output.Commit();
        return 0U;
    });
    dispatch.Bind("glDrawArrays", [&draw, &core, &legacy, &address_space, require_frame](
                                      const auto arguments, const std::uint64_t thread_id) {
        draw.DrawArrays(require_frame("glDrawArrays"), core, legacy, address_space,
                        arguments[0], Signed(arguments[1]), Signed(arguments[2]), thread_id);
        return 0U;
    });
    dispatch.Bind("glDrawElements", [&draw, &core, &legacy, &address_space, require_frame](
                                        const auto arguments, const std::uint64_t thread_id) {
        draw.DrawElements(require_frame("glDrawElements"), core, legacy, address_space,
                          arguments[0], Signed(arguments[1]), arguments[2], arguments[3],
                          thread_id);
        return 0U;
    });
    extensions.Bind("glCurrentPaletteMatrixOES",
                    [&draw, require_frame](const auto arguments, const auto) {
        AndroidBoundaryGles1DrawState::ValidateCurrentPaletteMatrix(arguments[0]);
        static_cast<void>(require_frame("glCurrentPaletteMatrixOES"));
        draw.SetCurrentPaletteMatrix(arguments[0]);
        return 0U;
    });
    const auto set_palette_pointer =
        [&draw, &core, require_frame](const std::uint32_t array,
                                     const auto arguments,
                                     const std::string_view operation) {
            auto next = draw.PreparePointer(
                array, kTexture0, Signed(arguments[0]), arguments[1],
                Signed(arguments[2]), arguments[3],
                core.TransferState().Snapshot().array_buffer);
            static_cast<void>(require_frame(operation));
            draw.CommitPointer(array, kTexture0, next);
            return 0U;
        };
    extensions.Bind("glMatrixIndexPointerOES",
                    [set_palette_pointer](const auto arguments, const auto) {
        return set_palette_pointer(kGles1MatrixIndexArray, arguments,
                                   "glMatrixIndexPointerOES");
    });
    extensions.Bind("glWeightPointerOES",
                    [set_palette_pointer](const auto arguments, const auto) {
        return set_palette_pointer(kGles1WeightArray, arguments,
                                   "glWeightPointerOES");
    });
}

}  // namespace ogplay::runtime::detail
