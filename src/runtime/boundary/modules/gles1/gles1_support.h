#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

#include "gles1_dispatch.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {

inline Gles1Matrix Gles1IdentityMatrix() noexcept {
    return {1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F};
}

inline Gles1Matrix Gles1MultiplyMatrices(
    const Gles1Matrix& left, const Gles1Matrix& right) noexcept {
    Gles1Matrix result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t index = 0; index < 4; ++index) {
                result[column * 4 + row] +=
                    left[index * 4 + row] * right[column * 4 + index];
            }
        }
    }
    return result;
}

inline std::array<float, 4> Gles1TransformClipPlane(
    const Gles1Matrix& modelview,
    const std::span<const float, 4> equation) {
    std::array<std::array<double, 8>, 4> rows{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            rows[row][column] = modelview[column * 4U + row];
        }
        rows[row][4U + row] = 1.0;
    }
    for (std::size_t column = 0; column < 4U; ++column) {
        auto pivot = column;
        for (std::size_t row = column + 1U; row < 4U; ++row) {
            if (std::abs(rows[row][column]) >
                std::abs(rows[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(rows[pivot][column]) <=
            std::numeric_limits<double>::epsilon()) {
            throw std::invalid_argument(
                "GLES1 clip-plane modelview matrix is singular");
        }
        if (pivot != column) std::swap(rows[pivot], rows[column]);
        const auto divisor = rows[column][column];
        for (auto& value : rows[column]) value /= divisor;
        for (std::size_t row = 0; row < 4U; ++row) {
            if (row == column) continue;
            const auto factor = rows[row][column];
            for (std::size_t index = 0; index < 8U; ++index) {
                rows[row][index] -= factor * rows[column][index];
            }
        }
    }
    std::array<float, 4> transformed{};
    for (std::size_t row = 0; row < 4U; ++row) {
        double value{};
        for (std::size_t column = 0; column < 4U; ++column) {
            value += rows[column][4U + row] * equation[column];
        }
        transformed[row] = static_cast<float>(value);
    }
    if (!std::ranges::all_of(transformed, [](const float value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("GLES1 transformed clip plane is not finite");
    }
    return transformed;
}

inline void RequireFiniteGles1MatrixValues(
    const std::span<const float> values) {
    if (!std::ranges::all_of(values, [](const float value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("GLES1 matrix value must be finite");
    }
}

inline Gles1Matrix ReadGuestGles1Matrix(
    const memory::AddressSpace& address_space, const std::uint32_t address,
    const std::uint64_t thread_id) {
    std::array<std::byte, sizeof(Gles1Matrix)> bytes{};
    address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
    Gles1Matrix matrix{};
    for (std::size_t element = 0; element < matrix.size(); ++element) {
        std::uint32_t word{};
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            word |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                        bytes[element * sizeof(word) + byte]))
                    << (byte * 8U);
        }
        matrix[element] = std::bit_cast<float>(word);
    }
    RequireFiniteGles1MatrixValues(matrix);
    return matrix;
}

inline constexpr std::string_view kGles1FixedVertexShader = R"(
attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec4 a_color;
attribute float a_point_size;
attribute vec4 a_texcoord0;
attribute vec4 a_texcoord1;
uniform vec4 u_modelview0;
uniform vec4 u_modelview1;
uniform vec4 u_modelview2;
uniform vec4 u_modelview3;
uniform vec4 u_projection0;
uniform vec4 u_projection1;
uniform vec4 u_projection2;
uniform vec4 u_projection3;
uniform vec4 u_texture0_matrix0;
uniform vec4 u_texture0_matrix1;
uniform vec4 u_texture0_matrix2;
uniform vec4 u_texture0_matrix3;
uniform vec4 u_texture1_matrix0;
uniform vec4 u_texture1_matrix1;
uniform vec4 u_texture1_matrix2;
uniform vec4 u_texture1_matrix3;
uniform mat3 u_normal_matrix;
uniform float u_normalize_normal;
uniform float u_rescale_normal;
uniform vec4 u_current_color;
uniform vec4 u_current_normal;
uniform vec4 u_global_ambient;
uniform float u_light_enabled[8];
uniform vec4 u_light_ambient[8];
uniform vec4 u_light_diffuse[8];
uniform vec4 u_light_specular[8];
uniform vec4 u_light_position[8];
uniform vec4 u_light_spot_direction[8];
uniform vec4 u_light_attenuation[8];
uniform float u_light_spot_exponent[8];
uniform float u_light_spot_cutoff_cos[8];
uniform vec4 u_material_front_ambient;
uniform vec4 u_material_front_diffuse;
uniform vec4 u_material_front_specular;
uniform vec4 u_material_front_emission;
uniform float u_material_front_shininess;
uniform vec4 u_material_back_ambient;
uniform vec4 u_material_back_diffuse;
uniform vec4 u_material_back_specular;
uniform vec4 u_material_back_emission;
uniform float u_material_back_shininess;
uniform float u_light_model_two_side;
uniform float u_color_material;
uniform float u_has_color;
uniform float u_has_normal;
uniform float u_lighting;
uniform float u_point_size;
uniform float u_has_point_size;
uniform float u_point_size_min;
uniform float u_point_size_max;
uniform vec4 u_point_distance_attenuation;
uniform vec4 u_clip_plane[6];
varying vec4 v_color;
varying vec4 v_back_color;
varying vec2 v_texcoord0;
varying vec2 v_texcoord1;
varying float v_fog_distance;
varying vec3 v_clip_distance0;
varying vec3 v_clip_distance1;
vec4 transform(vec4 c0, vec4 c1, vec4 c2, vec4 c3, vec4 value) {
  return c0 * value.x + c1 * value.y + c2 * value.z + c3 * value.w;
}
vec4 illuminate(vec3 normal, vec4 vertexColor, vec4 materialAmbient,
                vec4 materialDiffuse, vec4 materialSpecular,
                vec4 materialEmission, float materialShininess, vec3 eyePosition) {
  if (u_color_material > 0.5) {
    materialAmbient = vertexColor;
    materialDiffuse = vertexColor;
  }
  vec3 rgb = materialEmission.rgb + materialAmbient.rgb * u_global_ambient.rgb;
  for (int i = 0; i < 8; ++i) {
    if (u_light_enabled[i] > 0.5) {
      vec3 delta = u_light_position[i].w == 0.0
          ? u_light_position[i].xyz
          : u_light_position[i].xyz - eyePosition;
      float distanceToLight = length(delta);
      vec3 lightDirection = normalize(delta);
      float diffuseFactor = max(dot(normal, lightDirection), 0.0);
      vec3 contribution = materialAmbient.rgb * u_light_ambient[i].rgb;
      contribution += materialDiffuse.rgb * u_light_diffuse[i].rgb * diffuseFactor;
      if (diffuseFactor > 0.0) {
        vec3 halfVector = normalize(lightDirection + normalize(-eyePosition));
        float specularFactor = pow(max(dot(normal, halfVector), 0.0),
                                   materialShininess);
        contribution += materialSpecular.rgb * u_light_specular[i].rgb *
                        specularFactor;
      }
      float spot = 1.0;
      if (u_light_spot_cutoff_cos[i] > -0.9999) {
        float spotCos = dot(normalize(u_light_spot_direction[i].xyz),
                            -lightDirection);
        spot = spotCos >= u_light_spot_cutoff_cos[i]
            ? pow(max(spotCos, 0.0), u_light_spot_exponent[i]) : 0.0;
      }
      float attenuation = 1.0;
      if (u_light_position[i].w != 0.0) {
        attenuation = 1.0 / max(u_light_attenuation[i].x +
            u_light_attenuation[i].y * distanceToLight +
            u_light_attenuation[i].z * distanceToLight * distanceToLight,
            0.000001);
      }
      rgb += contribution * spot * attenuation;
    }
  }
  return vec4(clamp(rgb, 0.0, 1.0), clamp(materialDiffuse.a, 0.0, 1.0));
}
void main() {
  vec4 eye = transform(u_modelview0, u_modelview1, u_modelview2,
                       u_modelview3, a_position);
  vec4 base = mix(u_current_color, a_color, u_has_color);
  vec3 normal = u_normal_matrix *
      mix(u_current_normal.xyz, a_normal, u_has_normal);
  normal *= u_rescale_normal;
  normal = mix(normal, normalize(normal), u_normalize_normal);
  vec4 frontLit = illuminate(normal, base, u_material_front_ambient,
      u_material_front_diffuse, u_material_front_specular,
      u_material_front_emission, u_material_front_shininess, eye.xyz);
  vec4 backLit = illuminate(-normal, base, u_material_back_ambient,
      u_material_back_diffuse, u_material_back_specular,
      u_material_back_emission, u_material_back_shininess, eye.xyz);
  v_color = mix(base, frontLit, u_lighting);
  v_back_color = mix(base, mix(frontLit, backLit, u_light_model_two_side),
                     u_lighting);
  v_texcoord0 = transform(u_texture0_matrix0, u_texture0_matrix1,
                          u_texture0_matrix2, u_texture0_matrix3, a_texcoord0).xy;
  v_texcoord1 = transform(u_texture1_matrix0, u_texture1_matrix1,
                          u_texture1_matrix2, u_texture1_matrix3, a_texcoord1).xy;
  v_fog_distance = abs(eye.z);
  v_clip_distance0 = vec3(dot(eye, u_clip_plane[0]),
                          dot(eye, u_clip_plane[1]),
                          dot(eye, u_clip_plane[2]));
  v_clip_distance1 = vec3(dot(eye, u_clip_plane[3]),
                          dot(eye, u_clip_plane[4]),
                          dot(eye, u_clip_plane[5]));
  gl_Position = transform(u_projection0, u_projection1, u_projection2,
                          u_projection3, eye);
  float pointAttenuation = dot(u_point_distance_attenuation.xyz,
      vec3(1.0, v_fog_distance, v_fog_distance * v_fog_distance));
  float pointSize = mix(u_point_size, a_point_size, u_has_point_size);
  gl_PointSize = clamp(pointSize * inversesqrt(max(pointAttenuation, 0.000001)),
                       u_point_size_min, u_point_size_max);
})";

inline constexpr std::string_view kGles1FixedFragmentShader = R"(
precision mediump float;
uniform sampler2D u_texture0;
uniform sampler2D u_texture1;
uniform float u_texture_enabled[2];
uniform int u_texture_environment[2];
uniform int u_texture_format[2];
uniform vec4 u_environment_color[2];
uniform int u_combine_rgb[2];
uniform int u_combine_alpha[2];
uniform int u_source_rgb0[2];
uniform int u_source_rgb1[2];
uniform int u_source_rgb2[2];
uniform int u_source_alpha0[2];
uniform int u_source_alpha1[2];
uniform int u_source_alpha2[2];
uniform int u_operand_rgb0[2];
uniform int u_operand_rgb1[2];
uniform int u_operand_rgb2[2];
uniform int u_operand_alpha0[2];
uniform int u_operand_alpha1[2];
uniform int u_operand_alpha2[2];
uniform float u_rgb_scale[2];
uniform float u_alpha_scale[2];
uniform float u_fog_enabled;
uniform int u_fog_mode;
uniform float u_fog_density;
uniform float u_fog_start;
uniform float u_fog_end;
uniform vec4 u_fog_color;
uniform float u_alpha_enabled;
uniform int u_alpha_function;
uniform float u_alpha_reference;
uniform float u_clip_enabled[6];
varying vec4 v_color;
varying vec4 v_back_color;
varying vec2 v_texcoord0;
varying vec2 v_texcoord1;
varying float v_fog_distance;
varying vec3 v_clip_distance0;
varying vec3 v_clip_distance1;
vec4 sourceValue(int source, vec4 texel, vec4 primary,
                 vec4 previous, vec4 constantColor) {
  if (source == 5890) return texel;
  if (source == 34166) return constantColor;
  if (source == 34167) return primary;
  return previous;
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
vec3 combineRgb(int mode, vec3 a, vec3 b, vec3 c) {
  if (mode == 7681) return a;
  if (mode == 8448) return a * b;
  if (mode == 260) return a + b;
  if (mode == 34164) return a + b - vec3(0.5);
  if (mode == 34165) return a * c + b * (vec3(1.0) - c);
  if (mode == 34023) return a - b;
  return vec3(4.0 * dot(a - vec3(0.5), b - vec3(0.5)));
}
float combineAlpha(int mode, float a, float b, float c) {
  if (mode == 7681) return a;
  if (mode == 8448) return a * b;
  if (mode == 260) return a + b;
  if (mode == 34164) return a + b - 0.5;
  if (mode == 34165) return a * c + b * (1.0 - c);
  return a - b;
}
vec4 applyStage(vec4 previous, vec4 texel, int stage) {
  vec4 color = previous;
  int environment = u_texture_environment[stage];
  int format = u_texture_format[stage];
  if (environment == 34160) {
    vec4 rs0 = sourceValue(u_source_rgb0[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    vec4 rs1 = sourceValue(u_source_rgb1[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    vec4 rs2 = sourceValue(u_source_rgb2[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    vec4 as0 = sourceValue(u_source_alpha0[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    vec4 as1 = sourceValue(u_source_alpha1[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    vec4 as2 = sourceValue(u_source_alpha2[stage], texel, v_color, previous,
                           u_environment_color[stage]);
    color.rgb = clamp(combineRgb(u_combine_rgb[stage],
        rgbOperand(u_operand_rgb0[stage], rs0),
        rgbOperand(u_operand_rgb1[stage], rs1),
        rgbOperand(u_operand_rgb2[stage], rs2)) * u_rgb_scale[stage], 0.0, 1.0);
    color.a = clamp(combineAlpha(u_combine_alpha[stage],
        alphaOperand(u_operand_alpha0[stage], as0),
        alphaOperand(u_operand_alpha1[stage], as1),
        alphaOperand(u_operand_alpha2[stage], as2)) * u_alpha_scale[stage], 0.0, 1.0);
    if (u_combine_rgb[stage] == 34479) color.a = color.r;
  } else if (environment == 7681) {
    if (format != 0) color.rgb = texel.rgb;
    if (format != 1) color.a = texel.a;
  } else if (environment == 260) {
    if (format != 0) color.rgb = min(color.rgb + texel.rgb, vec3(1.0));
    if (format != 1) color.a *= texel.a;
  } else if (environment == 3042) {
    if (format != 0)
      color.rgb = mix(color.rgb, u_environment_color[stage].rgb, texel.rgb);
    if (format != 1) color.a *= texel.a;
  } else if (environment == 8449) {
    if (format == 2) color.rgb = mix(color.rgb, texel.rgb, texel.a);
    else if (format != 0) color.rgb = texel.rgb;
  } else {
    if (format != 0) color.rgb *= texel.rgb;
    if (format != 1) color.a *= texel.a;
  }
  return color;
}
void main() {
  if ((u_clip_enabled[0] > 0.5 && v_clip_distance0.x < 0.0) ||
      (u_clip_enabled[1] > 0.5 && v_clip_distance0.y < 0.0) ||
      (u_clip_enabled[2] > 0.5 && v_clip_distance0.z < 0.0) ||
      (u_clip_enabled[3] > 0.5 && v_clip_distance1.x < 0.0) ||
      (u_clip_enabled[4] > 0.5 && v_clip_distance1.y < 0.0) ||
      (u_clip_enabled[5] > 0.5 && v_clip_distance1.z < 0.0)) discard;
  vec4 color = gl_FrontFacing ? v_color : v_back_color;
  if (u_texture_enabled[0] > 0.5)
    color = applyStage(color, texture2D(u_texture0, v_texcoord0), 0);
  if (u_texture_enabled[1] > 0.5)
    color = applyStage(color, texture2D(u_texture1, v_texcoord1), 1);
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

}  // namespace ogplay::runtime::detail
