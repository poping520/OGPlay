#include "gles1_completion.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "gles1_fixed.h"
#include "gles1_support.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {
namespace {

constexpr float kFixedScale = 65536.0F;
constexpr std::uint32_t kTexture0 = 0x84C0U;
constexpr std::uint32_t kTexture31 = 0x84DFU;
constexpr std::uint32_t kFogMode = 0x0B65U;
constexpr std::uint32_t kLightModelTwoSide = 0x0B52U;
constexpr std::uint32_t kPointDistanceAttenuation = 0x8129U;

[[nodiscard]] float Fixed(const std::uint32_t word) noexcept {
    return static_cast<float>(std::bit_cast<std::int32_t>(word)) / kFixedScale;
}

[[nodiscard]] std::vector<float> ReadFixed(const memory::AddressSpace& address_space,
                                           const std::uint32_t address, const std::size_t count,
                                           const std::uint64_t thread_id) {
    std::vector<std::byte> bytes(count * sizeof(std::uint32_t));
    address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::uint32_t word{};
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            word |= static_cast<std::uint32_t>(
                        std::to_integer<std::uint8_t>(bytes[index * sizeof(word) + byte]))
                    << (byte * 8U);
        }
        values[index] = Fixed(word);
    }
    return values;
}

[[nodiscard]] std::vector<float> ReadFloats(const memory::AddressSpace& address_space,
                                            const std::uint32_t address, const std::size_t count,
                                            const std::uint64_t thread_id) {
    std::vector<std::byte> bytes(count * sizeof(std::uint32_t));
    address_space.Read(memory::GuestAddress{address}, bytes, thread_id);
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::uint32_t word{};
        for (std::size_t byte = 0; byte < sizeof(word); ++byte) {
            word |= static_cast<std::uint32_t>(
                        std::to_integer<std::uint8_t>(bytes[index * sizeof(word) + byte]))
                    << (byte * 8U);
        }
        values[index] = std::bit_cast<float>(word);
    }
    return values;
}

[[nodiscard]] Gles1Matrix ReadFixedMatrix(const memory::AddressSpace& address_space,
                                          const std::uint32_t address,
                                          const std::uint64_t thread_id) {
    const auto values = ReadFixed(address_space, address, 16U, thread_id);
    Gles1Matrix matrix{};
    std::ranges::copy(values, matrix.begin());
    RequireFiniteGles1MatrixValues(matrix);
    return matrix;
}

[[nodiscard]] std::size_t FogCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x0B65U:
    case 0x0B62U:
    case 0x0B63U:
    case 0x0B64U:
        return 1U;
    case 0x0B66U:
        return 4U;
    default:
        throw std::invalid_argument("GLES1 fog pname is invalid");
    }
}

[[nodiscard]] std::size_t LightModelCount(const std::uint32_t pname) {
    if (pname == 0x0B53U)
        return 4U;
    if (pname == kLightModelTwoSide)
        return 1U;
    throw std::invalid_argument("GLES1 light-model pname is invalid");
}

[[nodiscard]] std::size_t LightCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x1200U:
    case 0x1201U:
    case 0x1202U:
    case 0x1203U:
        return 4U;
    case 0x1204U:
        return 3U;
    case 0x1205U:
    case 0x1206U:
    case 0x1207U:
    case 0x1208U:
    case 0x1209U:
        return 1U;
    default:
        throw std::invalid_argument("GLES1 light pname is invalid");
    }
}

[[nodiscard]] std::size_t MaterialCount(const std::uint32_t pname) {
    switch (pname) {
    case 0x1200U:
    case 0x1201U:
    case 0x1202U:
    case 0x1600U:
    case 0x1602U:
        return 4U;
    case 0x1601U:
        return 1U;
    default:
        throw std::invalid_argument("GLES1 material pname is invalid");
    }
}

[[nodiscard]] Gles1Matrix ScaleMatrix(const float x, const float y, const float z) {
    const std::array values{x, y, z};
    RequireFiniteGles1MatrixValues(values);
    auto matrix = Gles1IdentityMatrix();
    matrix[0] = x;
    matrix[5] = y;
    matrix[10] = z;
    return matrix;
}

[[nodiscard]] Gles1Matrix OrthoMatrix(const float left, const float right, const float bottom,
                                      const float top, const float near_value,
                                      const float far_value) {
    const std::array values{left, right, bottom, top, near_value, far_value};
    RequireFiniteGles1MatrixValues(values);
    if (left == right || bottom == top || near_value == far_value) {
        throw std::invalid_argument("GLES1 orthographic volume is degenerate");
    }
    return {2.0F / (right - left),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            2.0F / (top - bottom),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            -2.0F / (far_value - near_value),
            0.0F,
            -(right + left) / (right - left),
            -(top + bottom) / (top - bottom),
            -(far_value + near_value) / (far_value - near_value),
            1.0F};
}

[[nodiscard]] Gles1Matrix FrustumMatrix(const float left, const float right, const float bottom,
                                        const float top, const float near_value,
                                        const float far_value) {
    const std::array values{left, right, bottom, top, near_value, far_value};
    RequireFiniteGles1MatrixValues(values);
    if (left == right || bottom == top || near_value == far_value || near_value <= 0.0F ||
        far_value <= 0.0F) {
        throw std::invalid_argument("GLES1 frustum volume is invalid");
    }
    return {2.0F * near_value / (right - left),
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            2.0F * near_value / (top - bottom),
            0.0F,
            0.0F,
            (right + left) / (right - left),
            (top + bottom) / (top - bottom),
            -(far_value + near_value) / (far_value - near_value),
            -1.0F,
            0.0F,
            0.0F,
            -(2.0F * far_value * near_value) / (far_value - near_value),
            0.0F};
}

template <typename Convert>
[[nodiscard]] Gles1Matrix MatrixArguments(const std::span<const std::uint32_t> arguments,
                                          Convert convert, const bool frustum) {
    return frustum
               ? FrustumMatrix(convert(arguments[0]), convert(arguments[1]), convert(arguments[2]),
                               convert(arguments[3]), convert(arguments[4]), convert(arguments[5]))
               : OrthoMatrix(convert(arguments[0]), convert(arguments[1]), convert(arguments[2]),
                             convert(arguments[3]), convert(arguments[4]), convert(arguments[5]));
}

} // namespace

void BindAndroidBoundaryGles1Completion(gles::GlesDispatchTable& dispatch,
                                        AndroidBoundaryGles1State& core,
                                        AndroidBoundaryGles1LegacyState& legacy,
                                        memory::AddressSpace& address_space,
                                        AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 completion frame resolver is missing");
    }
    auto& fixed = core.Fixed();
    dispatch.Bind("glAlphaFuncx", [&legacy, require_frame](const auto a, const auto) {
        const auto value = Fixed(a[1]);
        legacy.ValidateAlphaFunction(a[0], value);
        static_cast<void>(require_frame("glAlphaFuncx"));
        legacy.SetAlphaFunction(a[0], value);
        return 0U;
    });
    dispatch.Bind("glClearColorx", [&core, require_frame](const auto a, const auto) {
        const std::array color{Fixed(a[0]), Fixed(a[1]), Fixed(a[2]), Fixed(a[3])};
        require_frame("glClearColorx").ClearColor(color[0], color[1], color[2], color[3]);
        core.Shared().SetClearColor(color);
        return 0U;
    });
    dispatch.Bind("glClearDepthx", [&core, require_frame](const auto a, const auto) {
        const auto value = Fixed(a[0]);
        require_frame("glClearDepthx").ClearDepth(value);
        core.Shared().SetClearDepth(value);
        return 0U;
    });
    dispatch.Bind("glColor4x", [&legacy, require_frame](const auto a, const auto) {
        const std::array value{Fixed(a[0]), Fixed(a[1]), Fixed(a[2]), Fixed(a[3])};
        legacy.ValidateColor(value);
        static_cast<void>(require_frame("glColor4x"));
        legacy.SetColor(value);
        return 0U;
    });
    dispatch.Bind("glNormal3x", [&legacy, require_frame](const auto a, const auto) {
        const std::array value{Fixed(a[0]), Fixed(a[1]), Fixed(a[2])};
        legacy.ValidateNormal(value);
        static_cast<void>(require_frame("glNormal3x"));
        legacy.SetNormal(value);
        return 0U;
    });
    dispatch.Bind("glDepthRangex", [require_frame](const auto a, const auto) {
        require_frame("glDepthRangex").DepthRange(Fixed(a[0]), Fixed(a[1]));
        return 0U;
    });
    dispatch.Bind("glLineWidthx", [require_frame](const auto a, const auto) {
        require_frame("glLineWidthx").LineWidth(Fixed(a[0]));
        return 0U;
    });
    dispatch.Bind("glPolygonOffsetx", [require_frame](const auto a, const auto) {
        require_frame("glPolygonOffsetx").PolygonOffset(Fixed(a[0]), Fixed(a[1]));
        return 0U;
    });
    dispatch.Bind("glSampleCoveragex", [require_frame](const auto a, const auto) {
        require_frame("glSampleCoveragex").SampleCoverage(Fixed(a[0]), a[1] != 0U);
        return 0U;
    });
    dispatch.Bind("glPointSizex", [&fixed, require_frame](const auto a, const auto) {
        const auto value = Fixed(a[0]);
        static_cast<void>(require_frame("glPointSizex"));
        fixed.SetPointSize(value);
        return 0U;
    });
    dispatch.Bind("glPointParameterx", [&fixed, require_frame](const auto a, const auto) {
        const auto value = Fixed(a[1]);
        static_cast<void>(require_frame("glPointParameterx"));
        fixed.SetPointParameter(a[0], value);
        return 0U;
    });
    dispatch.Bind("glFogx", [&fixed, require_frame](const auto a, const auto) {
        const std::array value{a[0] == kFogMode ? static_cast<float>(a[1]) : Fixed(a[1])};
        static_cast<void>(require_frame("glFogx"));
        fixed.SetFog(a[0], value);
        return 0U;
    });
    dispatch.Bind(
        "glFogxv", [&fixed, &address_space, require_frame](const auto a, const auto thread) {
            auto values = ReadFixed(address_space, a[1], FogCount(a[0]), thread);
            if (a[0] == kFogMode) {
                std::array<std::byte, 4> bytes{};
                address_space.Read(memory::GuestAddress{a[1]}, bytes, thread);
                std::uint32_t raw{};
                for (std::size_t i = 0; i < 4; ++i)
                    raw |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[i]))
                           << (i * 8U);
                values[0] = static_cast<float>(raw);
            }
            static_cast<void>(require_frame("glFogxv"));
            fixed.SetFog(a[0], values);
            return 0U;
        });
    dispatch.Bind("glLightModelf", [&fixed, require_frame](const auto a, const auto) {
        const std::array value{std::bit_cast<float>(a[1])};
        static_cast<void>(require_frame("glLightModelf"));
        fixed.SetLightModel(a[0], value);
        return 0U;
    });
    dispatch.Bind("glLightModelx", [&fixed, require_frame](const auto a, const auto) {
        const std::array value{a[0] == kLightModelTwoSide ? static_cast<float>(a[1]) : Fixed(a[1])};
        static_cast<void>(require_frame("glLightModelx"));
        fixed.SetLightModel(a[0], value);
        return 0U;
    });
    const auto bind_fixed_vector = [&dispatch, &address_space,
                                    require_frame](const std::string_view name, const auto count,
                                                   const auto commit) {
        dispatch.Bind(name, [&address_space, require_frame, name, count,
                             commit](const auto a, const auto thread) {
            const auto values = ReadFixed(address_space, a.back(), count(a), thread);
            static_cast<void>(require_frame(name));
            commit(a, values);
            return 0U;
        });
    };
    bind_fixed_vector(
        "glLightModelxv", [](const auto a) { return LightModelCount(a[0]); },
        [&fixed](const auto a, const auto& v) { fixed.SetLightModel(a[0], v); });
    dispatch.Bind("glLightx", [&fixed, require_frame](const auto a, const auto) {
        const std::array value{Fixed(a[2])};
        static_cast<void>(require_frame("glLightx"));
        fixed.SetLight(a[0], a[1], value);
        return 0U;
    });
    bind_fixed_vector(
        "glLightxv", [](const auto a) { return LightCount(a[1]); },
        [&fixed](const auto a, const auto& v) { fixed.SetLight(a[0], a[1], v); });
    dispatch.Bind("glMaterialx", [&fixed, require_frame](const auto a, const auto) {
        const std::array value{Fixed(a[2])};
        static_cast<void>(require_frame("glMaterialx"));
        fixed.SetMaterial(a[0], a[1], value);
        return 0U;
    });
    bind_fixed_vector(
        "glMaterialxv", [](const auto a) { return MaterialCount(a[1]); },
        [&fixed](const auto a, const auto& v) { fixed.SetMaterial(a[0], a[1], v); });
    dispatch.Bind("glPointParameterfv", [&fixed, &address_space, require_frame](const auto a,
                                                                                const auto thread) {
        if (a[0] == kPointDistanceAttenuation) {
            const auto values = ReadFloats(address_space, a[1], 3U, thread);
            static_cast<void>(require_frame("glPointParameterfv"));
            fixed.SetPointDistanceAttenuation(std::span<const float, 3>{values.data(), 3U});
        } else {
            const auto values = ReadFloats(address_space, a[1], 1U, thread);
            static_cast<void>(require_frame("glPointParameterfv"));
            fixed.SetPointParameter(a[0], values[0]);
        }
        return 0U;
    });
    dispatch.Bind("glPointParameterxv", [&fixed, &address_space, require_frame](const auto a,
                                                                                const auto thread) {
        if (a[0] == kPointDistanceAttenuation) {
            const auto values = ReadFixed(address_space, a[1], 3U, thread);
            static_cast<void>(require_frame("glPointParameterxv"));
            fixed.SetPointDistanceAttenuation(std::span<const float, 3>{values.data(), 3U});
        } else {
            const auto values = ReadFixed(address_space, a[1], 1U, thread);
            static_cast<void>(require_frame("glPointParameterxv"));
            fixed.SetPointParameter(a[0], values[0]);
        }
        return 0U;
    });
    dispatch.Bind("glClipPlanex", [&legacy, &core, &address_space,
                                   require_frame](const auto a, const auto thread) {
        const auto values = ReadFixed(address_space, a[1], 4U, thread);
        std::array<float, 4> equation{};
        std::ranges::copy(values, equation.begin());
        legacy.ValidateClipPlane(a[0], equation);
        const auto transformed =
            Gles1TransformClipPlane(core.Matrices().Current(kGles1Modelview, kTexture0), equation);
        static_cast<void>(require_frame("glClipPlanex"));
        legacy.SetClipPlane(a[0], transformed);
        return 0U;
    });
    const auto bind_matrix = [&dispatch, &core, require_frame](const std::string_view name,
                                                               const auto convert,
                                                               const bool frustum) {
        dispatch.Bind(name,
                      [&core, require_frame, name, convert, frustum](const auto a, const auto) {
                          const auto matrix = MatrixArguments(a, convert, frustum);
                          static_cast<void>(require_frame(name));
                          core.Matrices().Multiply(matrix);
                          return 0U;
                      });
    };
    bind_matrix("glFrustumf", [](const auto word) { return std::bit_cast<float>(word); }, true);
    bind_matrix("glFrustumx", [](const auto word) { return Fixed(word); }, true);
    bind_matrix("glOrthof", [](const auto word) { return std::bit_cast<float>(word); }, false);
    bind_matrix("glOrthox", [](const auto word) { return Fixed(word); }, false);
    dispatch.Bind("glScalef", [&core, require_frame](const auto a, const auto) {
        const auto matrix = ScaleMatrix(std::bit_cast<float>(a[0]), std::bit_cast<float>(a[1]),
                                        std::bit_cast<float>(a[2]));
        static_cast<void>(require_frame("glScalef"));
        core.Matrices().Multiply(matrix);
        return 0U;
    });
    dispatch.Bind("glScalex", [&core, require_frame](const auto a, const auto) {
        const auto matrix = ScaleMatrix(Fixed(a[0]), Fixed(a[1]), Fixed(a[2]));
        static_cast<void>(require_frame("glScalex"));
        core.Matrices().Multiply(matrix);
        return 0U;
    });
    dispatch.Bind("glRotatex", [&core, require_frame](const auto a, const auto) {
        static_cast<void>(require_frame("glRotatex"));
        core.Matrices().Rotate(Fixed(a[0]), Fixed(a[1]), Fixed(a[2]), Fixed(a[3]));
        return 0U;
    });
    dispatch.Bind("glTranslatex", [&core, require_frame](const auto a, const auto) {
        static_cast<void>(require_frame("glTranslatex"));
        core.Matrices().Translate(Fixed(a[0]), Fixed(a[1]), Fixed(a[2]));
        return 0U;
    });
    dispatch.Bind("glLoadMatrixx",
                  [&core, &address_space, require_frame](const auto a, const auto thread) {
                      const auto matrix = ReadFixedMatrix(address_space, a[0], thread);
                      static_cast<void>(require_frame("glLoadMatrixx"));
                      core.Matrices().Load(matrix);
                      return 0U;
                  });
    dispatch.Bind("glMultMatrixx",
                  [&core, &address_space, require_frame](const auto a, const auto thread) {
                      const auto matrix = ReadFixedMatrix(address_space, a[0], thread);
                      static_cast<void>(require_frame("glMultMatrixx"));
                      core.Matrices().Multiply(matrix);
                      return 0U;
                  });
    const auto bind_texcoord = [&dispatch, &legacy, require_frame](const std::string_view name,
                                                                   const auto convert) {
        dispatch.Bind(name, [&legacy, require_frame, name, convert](const auto a, const auto) {
            if (a[0] < kTexture0 || a[0] > kTexture31)
                throw std::invalid_argument("GLES1 texture unit is outside GL_TEXTURE0..31");
            const std::array value{convert(a[1]), convert(a[2]), convert(a[3]), convert(a[4])};
            static_cast<void>(require_frame(name));
            legacy.SetCurrentTextureCoordinate(a[0], value);
            return 0U;
        });
    };
    bind_texcoord("glMultiTexCoord4f", [](const auto word) { return std::bit_cast<float>(word); });
    bind_texcoord("glMultiTexCoord4x", [](const auto word) { return Fixed(word); });
}

} // namespace ogplay::runtime::detail
