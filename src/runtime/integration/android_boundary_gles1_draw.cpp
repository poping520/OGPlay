#include "android_boundary_gles1_draw.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "android_boundary_gles1_fixed.h"
#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"

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
constexpr std::uint32_t kModulate = 0x2100U;
constexpr std::uint32_t kReplace = 0x1E01U;
constexpr std::uint32_t kAdd = 0x0104U;
constexpr std::uint32_t kCombine = 0x8570U;

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
    const AndroidBoundaryGles1State& core, const std::uint32_t mode) {
    auto matrices = core.Matrices();
    matrices.SetMode(mode);
    return matrices.Current();
}

[[nodiscard]] std::array<float, 9> UpperMatrix3(
    const Gles1Matrix& matrix) noexcept {
    return {matrix[0], matrix[1], matrix[2], matrix[4], matrix[5],
            matrix[6], matrix[8], matrix[9], matrix[10]};
}

[[nodiscard]] std::uint32_t MaximumIndex(
    const std::span<const std::byte> bytes, const std::uint32_t type) {
    std::uint32_t maximum{};
    if (type == kUnsignedByte) {
        for (const auto value : bytes) {
            maximum = std::max(maximum,
                               static_cast<std::uint32_t>(
                                   std::to_integer<std::uint8_t>(value)));
        }
        return maximum;
    }
    if (type != kUnsignedShort || bytes.size() % 2U != 0U) {
        throw std::invalid_argument("GLES1 draw index type is unsupported");
    }
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2U) {
        maximum = std::max(
            maximum,
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                (static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + 1U]))
                 << 8U));
    }
    return maximum;
}

[[nodiscard]] std::int32_t Signed(const std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
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
    arrays_[ArrayKey(kGles1VertexArray, kTexture0)] = {};
    arrays_[ArrayKey(kGles1NormalArray, kTexture0)] = {};
    arrays_[ArrayKey(kGles1ColorArray, kTexture0)] = {};
    arrays_[ArrayKey(kGles1MatrixIndexArray, kTexture0)] = {};
    arrays_[ArrayKey(kGles1WeightArray, kTexture0)] = {};
    for (auto texture = kTexture0; texture <= 0x84DFU; ++texture) {
        arrays_[ArrayKey(kGles1TextureCoordArray, texture)] = {};
    }
    program_ = {};
    current_palette_matrix_ = 0U;
}

AndroidBoundaryGles1DrawState::AndroidBoundaryGles1DrawState() { Reset(); }

void AndroidBoundaryGles1DrawState::SetEnabled(
    const std::uint32_t array, const std::uint32_t client_texture,
    const bool enabled) {
    if (array != kGles1VertexArray && array != kGles1NormalArray &&
        array != kGles1ColorArray && array != kGles1TextureCoordArray &&
        array != kGles1MatrixIndexArray && array != kGles1WeightArray) {
        throw std::invalid_argument("GLES1 client state array is unsupported");
    }
    arrays_.at(ArrayKey(array, client_texture)).enabled = enabled;
}

void AndroidBoundaryGles1DrawState::SetPointer(
    const std::uint32_t array, const std::uint32_t client_texture,
    const std::int32_t size, const std::uint32_t type,
    const std::int32_t stride, const std::uint32_t pointer,
    const std::uint32_t buffer) {
    if (stride < 0) {
        throw std::invalid_argument("GLES1 client array stride is negative");
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
    auto& stored = arrays_.at(ArrayKey(array, client_texture));
    const auto enabled = stored.enabled;
    stored = {.size = size, .type = type, .stride = stride,
              .pointer = pointer, .buffer = buffer, .enabled = enabled};
}

const Gles1ClientArray& AndroidBoundaryGles1DrawState::Array(
    const std::uint32_t array, const std::uint32_t client_texture) const {
    return arrays_.at(ArrayKey(array, client_texture));
}

void AndroidBoundaryGles1DrawState::SetCurrentPaletteMatrix(
    const std::uint32_t index) {
    constexpr std::uint32_t kMaximumPaletteMatrices = 32U;
    if (index >= kMaximumPaletteMatrices) {
        throw std::invalid_argument("GLES1 palette matrix index is outside 0..31");
    }
    current_palette_matrix_ = index;
}

std::uint32_t AndroidBoundaryGles1DrawState::CurrentPaletteMatrix() const noexcept {
    return current_palette_matrix_;
}

void AndroidBoundaryGles1DrawState::EnsureProgram(gles::AngleFrame& frame) {
    if (program_.name != 0U) return;
    static const std::string vertex = R"(
attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
attribute vec4 a_texcoord;
uniform vec4 u_modelview0;
uniform vec4 u_modelview1;
uniform vec4 u_modelview2;
uniform vec4 u_modelview3;
uniform vec4 u_projection0;
uniform vec4 u_projection1;
uniform vec4 u_projection2;
uniform vec4 u_projection3;
uniform vec4 u_texture_matrix0;
uniform vec4 u_texture_matrix1;
uniform vec4 u_texture_matrix2;
uniform vec4 u_texture_matrix3;
uniform mat3 u_normal_matrix;
uniform vec4 u_current_color;
uniform vec4 u_current_normal;
uniform vec4 u_global_ambient;
uniform vec4 u_light_ambient;
uniform vec4 u_light_diffuse;
uniform vec4 u_light_position;
uniform vec4 u_material_ambient;
uniform vec4 u_material_diffuse;
uniform float u_has_color;
uniform float u_has_normal;
uniform float u_lighting;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_fog_distance;
vec4 transform(vec4 c0, vec4 c1, vec4 c2, vec4 c3, vec4 value) {
  return c0 * value.x + c1 * value.y + c2 * value.z + c3 * value.w;
}
void main() {
  vec4 eye = transform(u_modelview0, u_modelview1, u_modelview2,
                       u_modelview3, a_position);
  vec4 base = mix(u_current_color, a_color, u_has_color);
  vec3 normal = normalize(u_normal_matrix *
      mix(u_current_normal.xyz, a_normal, u_has_normal));
  vec3 light = u_light_position.w == 0.0
      ? normalize(u_light_position.xyz)
      : normalize(u_light_position.xyz - eye.xyz);
  vec4 lit = u_material_ambient * (u_global_ambient + u_light_ambient) +
             u_material_diffuse * u_light_diffuse * max(dot(normal, light), 0.0);
  v_color = mix(base, lit, u_lighting);
  v_texcoord = transform(u_texture_matrix0, u_texture_matrix1,
                         u_texture_matrix2, u_texture_matrix3, a_texcoord).xy;
  v_fog_distance = abs(eye.z);
  gl_Position = transform(u_projection0, u_projection1, u_projection2,
                          u_projection3, eye);
})";
    static const std::string fragment = R"(
precision mediump float;
uniform sampler2D u_texture;
uniform float u_texture_enabled;
uniform int u_texture_environment;
uniform int u_texture_format;
uniform vec4 u_environment_color;
uniform int u_combine_rgb;
uniform int u_combine_alpha;
uniform int u_source_rgb0;
uniform int u_source_rgb1;
uniform int u_source_rgb2;
uniform int u_source_alpha0;
uniform int u_source_alpha1;
uniform int u_source_alpha2;
uniform int u_operand_rgb0;
uniform int u_operand_rgb1;
uniform int u_operand_rgb2;
uniform int u_operand_alpha0;
uniform int u_operand_alpha1;
uniform int u_operand_alpha2;
uniform float u_rgb_scale;
uniform float u_alpha_scale;
uniform float u_fog_enabled;
uniform int u_fog_mode;
uniform float u_fog_density;
uniform float u_fog_start;
uniform float u_fog_end;
uniform vec4 u_fog_color;
uniform float u_alpha_enabled;
uniform int u_alpha_function;
uniform float u_alpha_reference;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_fog_distance;
vec4 sourceValue(int source, vec4 texel, vec4 primary) {
  if (source == 5890) return texel;
  if (source == 34166) return u_environment_color;
  return primary;
}
vec3 rgbOperand(int operand, vec4 value) {
  if (operand == 768) return value.rgb;
  if (operand == 769) return vec3(1.0) - value.rgb;
  if (operand == 770) return vec3(value.a);
  return vec3(1.0 - value.a);
}
float alphaOperand(int operand, vec4 value) {
  return operand == 770 ? value.a : 1.0 - value.a;
}
vec3 combineRgb(vec3 a, vec3 b, vec3 c) {
  if (u_combine_rgb == 7681) return a;
  if (u_combine_rgb == 8448) return a * b;
  if (u_combine_rgb == 260) return a + b;
  if (u_combine_rgb == 34164) return a + b - vec3(0.5);
  if (u_combine_rgb == 34165) return a * c + b * (vec3(1.0) - c);
  if (u_combine_rgb == 34023) return a - b;
  return vec3(4.0 * dot(a - vec3(0.5), b - vec3(0.5)));
}
float combineAlpha(float a, float b, float c) {
  if (u_combine_alpha == 7681) return a;
  if (u_combine_alpha == 8448) return a * b;
  if (u_combine_alpha == 260) return a + b;
  if (u_combine_alpha == 34164) return a + b - 0.5;
  if (u_combine_alpha == 34165) return a * c + b * (1.0 - c);
  return a - b;
}
void main() {
  vec4 color = v_color;
  if (u_texture_enabled > 0.5) {
    vec4 texel = texture2D(u_texture, v_texcoord);
    if (u_texture_environment == 34160) {
      vec4 rs0 = sourceValue(u_source_rgb0, texel, v_color);
      vec4 rs1 = sourceValue(u_source_rgb1, texel, v_color);
      vec4 rs2 = sourceValue(u_source_rgb2, texel, v_color);
      vec4 as0 = sourceValue(u_source_alpha0, texel, v_color);
      vec4 as1 = sourceValue(u_source_alpha1, texel, v_color);
      vec4 as2 = sourceValue(u_source_alpha2, texel, v_color);
      color.rgb = clamp(combineRgb(rgbOperand(u_operand_rgb0, rs0), rgbOperand(u_operand_rgb1, rs1), rgbOperand(u_operand_rgb2, rs2)) * u_rgb_scale, 0.0, 1.0);
      color.a = clamp(combineAlpha(alphaOperand(u_operand_alpha0, as0), alphaOperand(u_operand_alpha1, as1), alphaOperand(u_operand_alpha2, as2)) * u_alpha_scale, 0.0, 1.0);
      if (u_combine_rgb == 34479) color.a = color.r;
    } else if (u_texture_environment == 7681) {
      if (u_texture_format != 0) color.rgb = texel.rgb;
      if (u_texture_format != 1) color.a = texel.a;
    } else if (u_texture_environment == 260) {
      if (u_texture_format != 0) color.rgb = min(color.rgb + texel.rgb, vec3(1.0));
      if (u_texture_format != 1) color.a *= texel.a;
    } else {
      if (u_texture_format != 0) color.rgb *= texel.rgb;
      if (u_texture_format != 1) color.a *= texel.a;
    }
  }
  float fog = clamp((u_fog_end - v_fog_distance) /
                    max(u_fog_end - u_fog_start, 0.00001), 0.0, 1.0);
  if (u_fog_mode == 1) fog = clamp(exp(-u_fog_density * v_fog_distance), 0.0, 1.0);
  if (u_fog_mode == 2) { float d = u_fog_density * v_fog_distance;
                         fog = clamp(exp(-(d * d)), 0.0, 1.0); }
  if (u_fog_enabled > 0.5) color = mix(u_fog_color, color, fog);
  bool pass = true;
  if (u_alpha_function == 512) pass = false;
  else if (u_alpha_function == 513) pass = color.a < u_alpha_reference;
  else if (u_alpha_function == 514) pass = color.a == u_alpha_reference;
  else if (u_alpha_function == 515) pass = color.a <= u_alpha_reference;
  else if (u_alpha_function == 516) pass = color.a > u_alpha_reference;
  else if (u_alpha_function == 517) pass = color.a != u_alpha_reference;
  else if (u_alpha_function == 518) pass = color.a >= u_alpha_reference;
  if (u_alpha_enabled > 0.5 && !pass) discard;
  gl_FragColor = color;
})";
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
    program_.name = frame.CreateProgram();
    frame.AttachShader(program_.name, vertex_shader);
    frame.AttachShader(program_.name, fragment_shader);
    frame.LinkProgram(program_.name);
    if (frame.GetProgramParameter(program_.name, 0x8B82U) == 0) {
        throw std::runtime_error("GLES1 fixed program link failed");
    }
    frame.DeleteShader(vertex_shader);
    frame.DeleteShader(fragment_shader);
    const std::array attribute_names{"a_position", "a_normal", "a_color",
                                     "a_texcoord"};
    for (std::size_t index = 0; index < attribute_names.size(); ++index) {
        program_.attributes[index] =
            frame.GetAttribLocation(program_.name, attribute_names[index]);
    }
    constexpr std::array uniform_names{
        "u_modelview0", "u_modelview1", "u_modelview2", "u_modelview3",
        "u_projection0", "u_projection1", "u_projection2", "u_projection3",
        "u_texture_matrix0", "u_texture_matrix1", "u_texture_matrix2",
        "u_texture_matrix3", "u_normal_matrix",
        "u_current_color", "u_current_normal", "u_global_ambient",
        "u_light_ambient", "u_light_diffuse", "u_light_position",
        "u_material_ambient", "u_material_diffuse", "u_has_color",
        "u_has_normal", "u_lighting", "u_texture", "u_texture_enabled",
        "u_texture_environment", "u_texture_format",
        "u_environment_color", "u_combine_rgb", "u_combine_alpha",
        "u_source_rgb0", "u_source_rgb1", "u_source_rgb2",
        "u_source_alpha0", "u_source_alpha1", "u_source_alpha2",
        "u_operand_rgb0", "u_operand_rgb1", "u_operand_rgb2",
        "u_operand_alpha0", "u_operand_alpha1", "u_operand_alpha2",
        "u_rgb_scale", "u_alpha_scale",
        "u_fog_enabled", "u_fog_mode", "u_fog_density", "u_fog_start",
        "u_fog_end", "u_fog_color", "u_alpha_enabled", "u_alpha_function",
        "u_alpha_reference"};
    for (const auto* name : uniform_names) {
        program_.uniforms[name] = frame.GetUniformLocation(program_.name, name);
    }
    const auto buffers = frame.GenerateBuffers(program_.buffers.size());
    std::ranges::copy(buffers, program_.buffers.begin());
}

void AndroidBoundaryGles1DrawState::PrepareArrays(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space, const std::uint32_t maximum_index,
    const std::uint64_t thread_id) {
    if (Array(kGles1MatrixIndexArray, kTexture0).enabled ||
        Array(kGles1WeightArray, kTexture0).enabled) {
        throw std::runtime_error(
            "GLES1 matrix-palette skinning draw conversion is not implemented");
    }
    const std::array kinds{kGles1VertexArray, kGles1NormalArray,
                           kGles1ColorArray, kGles1TextureCoordArray};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto texture = kinds[index] == kGles1TextureCoordArray
                                 ? legacy.ClientActiveTexture()
                                 : kTexture0;
        const auto& array = Array(kinds[index], texture);
        const auto location = program_.attributes[index];
        if (location < 0) continue;
        frame.SetVertexAttributeEnabled(static_cast<std::uint32_t>(location),
                                        array.enabled);
        if (!array.enabled) continue;
        if (array.size == 0) {
            throw std::logic_error("GLES1 enabled client array has no pointer definition");
        }
        std::uint32_t offset = array.pointer;
        if (array.buffer == 0U) {
            auto transfer = gles::GuestBuffer::Prepare(
                address_space, memory::GuestAddress{array.pointer},
                ArrayBytes(array, maximum_index),
                gles::GuestTransferDirection::input, false, thread_id);
            frame.BindBuffer(kArrayBuffer, program_.buffers[index]);
            frame.BufferData(kArrayBuffer,
                             static_cast<std::uint32_t>(transfer.Bytes().size()),
                             transfer.Bytes(), kStaticDraw);
            offset = 0U;
        } else {
            frame.BindBuffer(kArrayBuffer, array.buffer);
        }
        frame.VertexAttributePointer(
            static_cast<std::uint32_t>(location), array.size, array.type,
            kinds[index] == kGles1ColorArray && array.type == kUnsignedByte,
            array.stride, offset);
    }
    frame.BindBuffer(kArrayBuffer, core.TransferState().Snapshot().array_buffer);
}

void AndroidBoundaryGles1DrawState::ApplyUniforms(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy) {
    const auto modelview = MatrixFor(core, kGles1Modelview);
    const auto projection = MatrixFor(core, kGles1Projection);
    const auto texture_matrix = MatrixFor(core, kGles1Texture);
    const auto normal = UpperMatrix3(modelview);
    const auto uniform = [this](const std::string_view name) {
        return program_.uniforms.at(name);
    };
    const auto set_matrix = [&frame, &uniform](const std::string_view prefix,
                                               const Gles1Matrix& value) {
        for (std::size_t column = 0; column < 4U; ++column) {
            const auto name = std::string(prefix) + std::to_string(column);
            frame.Uniform4f(uniform(name), value[column * 4U],
                            value[column * 4U + 1U], value[column * 4U + 2U],
                            value[column * 4U + 3U]);
        }
    };
    set_matrix("u_modelview", modelview);
    set_matrix("u_projection", projection);
    set_matrix("u_texture_matrix", texture_matrix);
    frame.UniformMatrix3(uniform("u_normal_matrix"), 1, false, normal);
    const auto& color = legacy.Color();
    frame.Uniform4f(uniform("u_current_color"), color[0], color[1], color[2], color[3]);
    frame.Uniform4f(uniform("u_current_normal"), 0.0F, 0.0F, 1.0F, 0.0F);
    const auto& fixed = core.Fixed();
    const auto set4 = [&frame, &uniform](const std::string_view name,
                                         const std::vector<float>& value) {
        frame.Uniform4f(uniform(name), value[0], value[1], value[2], value[3]);
    };
    set4("u_global_ambient", fixed.LightModel(kGles1LightModelAmbient));
    set4("u_light_ambient", fixed.Light(0x4000U, kGles1LightAmbient));
    set4("u_light_diffuse", fixed.Light(0x4000U, 0x1201U));
    set4("u_light_position", fixed.Light(0x4000U, kGles1LightPosition));
    set4("u_material_ambient", fixed.Material(kGles1LightAmbient));
    set4("u_material_diffuse", fixed.Material(0x1201U));
    const auto client_texture = legacy.ClientActiveTexture();
    frame.Uniform1f(uniform("u_has_color"),
                    Array(kGles1ColorArray, kTexture0).enabled ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_has_normal"),
                    Array(kGles1NormalArray, kTexture0).enabled ? 1.0F : 0.0F);
    frame.Uniform1f(uniform("u_lighting"),
                    core.Capability(0x0B50U) ? 1.0F : 0.0F);
    frame.Uniform1i(uniform("u_texture"),
                    static_cast<std::int32_t>(core.ActiveTexture() - kTexture0));
    frame.Uniform1f(uniform("u_texture_enabled"),
                    core.Capability(0x0DE1U) ? 1.0F : 0.0F);
    const auto texture_environment = static_cast<std::uint32_t>(
        legacy.TextureEnvironment(core.ActiveTexture(),
                                  kGles1TextureEnvironmentMode)[0]);
    if (texture_environment != kModulate &&
        texture_environment != kReplace && texture_environment != kAdd &&
        texture_environment != kCombine) {
        throw std::runtime_error(
            "GLES1 draw does not implement texture environment mode " +
            std::to_string(texture_environment));
    }
    frame.Uniform1i(uniform("u_texture_environment"),
                    static_cast<std::int32_t>(texture_environment));
    auto texture_format = TextureFormatClass::color_alpha;
    if (core.Capability(kTexture2d)) {
        const auto base_format = core.TextureBaseFormat(kTexture2d);
        if (!base_format.has_value()) {
            throw std::runtime_error(
                "GLES1 textured draw has no level-zero base format");
        }
        texture_format = ClassifyTextureFormat(*base_format);
    }
    frame.Uniform1i(uniform("u_texture_format"),
                    static_cast<std::int32_t>(texture_format));
    const auto environment_value = [&legacy, &core](const std::uint32_t pname) {
        return legacy.TextureEnvironment(core.ActiveTexture(), pname)[0];
    };
    const auto& environment_color = legacy.TextureEnvironment(
        core.ActiveTexture(), kGles1TextureEnvironmentColor);
    frame.Uniform4f(uniform("u_environment_color"), environment_color[0],
                    environment_color[1], environment_color[2],
                    environment_color[3]);
    frame.Uniform1i(uniform("u_combine_rgb"), static_cast<std::int32_t>(
        environment_value(kGles1CombineRgb)));
    frame.Uniform1i(uniform("u_combine_alpha"), static_cast<std::int32_t>(
        environment_value(kGles1CombineAlpha)));
    const auto set3 = [&frame, &uniform, &environment_value](const auto& names,
                                                             const std::uint32_t first) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            frame.Uniform1i(uniform(names[index]), static_cast<std::int32_t>(
                environment_value(first + static_cast<std::uint32_t>(index))));
        }
    };
    set3(std::array{"u_source_rgb0", "u_source_rgb1", "u_source_rgb2"},
         kGles1Source0Rgb);
    set3(std::array{"u_source_alpha0", "u_source_alpha1", "u_source_alpha2"},
         kGles1Source0Alpha);
    set3(std::array{"u_operand_rgb0", "u_operand_rgb1", "u_operand_rgb2"},
         kGles1Operand0Rgb);
    set3(std::array{"u_operand_alpha0", "u_operand_alpha1", "u_operand_alpha2"},
         kGles1Operand0Alpha);
    frame.Uniform1f(uniform("u_rgb_scale"), environment_value(kGles1RgbScale));
    frame.Uniform1f(uniform("u_alpha_scale"),
                    environment_value(kGles1AlphaScale));
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
    static_cast<void>(client_texture);
}

void AndroidBoundaryGles1DrawState::DrawArrays(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space, const std::uint32_t mode,
    const std::int32_t first, const std::int32_t count,
    const std::uint64_t thread_id) {
    if (first < 0 || count < 0) {
        throw std::invalid_argument("GLES1 draw array range is negative");
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
    EnsureProgram(frame);
    frame.UseProgram(program_.name);
    PrepareArrays(frame, core, legacy, address_space,
                  static_cast<std::uint32_t>(maximum), thread_id);
    ApplyUniforms(frame, core, legacy);
    std::vector<std::uint16_t> indices(static_cast<std::size_t>(count));
    if (maximum > (std::numeric_limits<std::uint16_t>::max)()) {
        throw std::length_error("GLES1 emulated draw-array index exceeds GLushort");
    }
    for (std::int32_t index = 0; index < count; ++index) {
        indices[static_cast<std::size_t>(index)] =
            static_cast<std::uint16_t>(first + index);
    }
    frame.BindBuffer(kElementArrayBuffer, program_.buffers[4]);
    frame.BufferData(kElementArrayBuffer,
                     static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t)),
                     std::as_bytes(std::span(indices)), kStaticDraw);
    frame.DrawElements(mode, count, kUnsignedShort, 0U);
    frame.BindBuffer(kElementArrayBuffer,
                     core.TransferState().Snapshot().element_array_buffer);
}

void AndroidBoundaryGles1DrawState::DrawElements(
    gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
    const AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space, const std::uint32_t mode,
    const std::int32_t count, const std::uint32_t type,
    const std::uint32_t indices, const std::uint64_t thread_id) {
    if (count < 0) throw std::invalid_argument("GLES1 draw element count is negative");
    if (type != kUnsignedByte && type != kUnsignedShort) {
        throw std::invalid_argument("GLES1 draw index type is unsupported");
    }
    if (!Array(kGles1VertexArray, kTexture0).enabled) {
        throw std::runtime_error("GLES1 draw requires GL_VERTEX_ARRAY");
    }
    if (count == 0) return;
    EnsureProgram(frame);
    const auto guest_element_buffer =
        core.TransferState().Snapshot().element_array_buffer;
    std::uint32_t maximum{};
    std::optional<gles::GuestBuffer> transferred;
    if (guest_element_buffer == 0U) {
        transferred.emplace(gles::GuestBuffer::Prepare(
            address_space, memory::GuestAddress{indices},
            static_cast<std::uint64_t>(count) * ScalarBytes(type),
            gles::GuestTransferDirection::input, false, thread_id));
        maximum = MaximumIndex(transferred->Bytes(), type);
    } else {
        for (const auto kind : {kGles1VertexArray, kGles1NormalArray,
                                kGles1ColorArray}) {
            const auto& array = Array(kind, kTexture0);
            if (array.enabled && array.buffer == 0U) {
                throw std::runtime_error(
                    "GLES1 cannot bound guest client arrays from an opaque element buffer");
            }
        }
    }
    frame.UseProgram(program_.name);
    PrepareArrays(frame, core, legacy, address_space, maximum, thread_id);
    ApplyUniforms(frame, core, legacy);
    if (transferred.has_value()) {
        frame.BindBuffer(kElementArrayBuffer, program_.buffers[4]);
        frame.BufferData(kElementArrayBuffer,
                         static_cast<std::uint32_t>(transferred->Bytes().size()),
                         transferred->Bytes(), kStaticDraw);
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
    const auto set_pointer = [&draw, &core, &legacy, require_frame](
                                 const std::uint32_t array,
                                 const std::span<const std::uint32_t> arguments,
                                 const std::int32_t size,
                                 const std::uint32_t type_index,
                                 const std::uint32_t stride_index,
                                 const std::uint32_t pointer_index,
                                 const std::string_view operation) {
        auto next = draw;
        next.SetPointer(array, legacy.ClientActiveTexture(), size,
                        arguments[type_index], Signed(arguments[stride_index]),
                        arguments[pointer_index],
                        core.TransferState().Snapshot().array_buffer);
        static_cast<void>(require_frame(operation));
        draw = std::move(next);
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
        auto next = draw;
        next.SetCurrentPaletteMatrix(arguments[0]);
        static_cast<void>(require_frame("glCurrentPaletteMatrixOES"));
        draw = std::move(next);
        return 0U;
    });
    const auto set_palette_pointer =
        [&draw, &core, require_frame](const std::uint32_t array,
                                     const auto arguments,
                                     const std::string_view operation) {
            auto next = draw;
            next.SetPointer(array, kTexture0, Signed(arguments[0]), arguments[1],
                            Signed(arguments[2]), arguments[3],
                            core.TransferState().Snapshot().array_buffer);
            static_cast<void>(require_frame(operation));
            draw = std::move(next);
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
