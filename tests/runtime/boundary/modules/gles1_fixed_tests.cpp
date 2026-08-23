#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "runtime/boundary/modules/gles1/gles1_fixed.h"
#include "runtime/boundary/modules/gles1/gles1_completion.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_remaining.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "ogplay/memory/address_space.h"

namespace {

constexpr std::uint32_t kLight0 = 0x4000U;
constexpr std::uint32_t kDiffuse = 0x1201U;
constexpr std::uint32_t kSpotCutoff = 0x1206U;
constexpr std::uint32_t kConstantAttenuation = 0x1207U;

} // namespace

TEST_CASE("GLES1 single-face material quirk is required when disabled") {
    ogplay::runtime::detail::AndroidBoundaryGles1FixedState state;
    CHECK(state.Fog(ogplay::runtime::detail::kGles1FogDensity)[0] == 1.0F);
    CHECK(state.LightModel(ogplay::runtime::detail::kGles1LightModelAmbient)[0] == 0.2F);
    CHECK(state.Light(kLight0, kDiffuse)[0] == 1.0F);
    CHECK(state.Light(kLight0 + 1U, kDiffuse)[0] == 0.0F);
    CHECK(state.Material(ogplay::runtime::detail::kGles1MaterialShininess)[0] == 0.0F);
    CHECK(state.PointSize() == 1.0F);
    CHECK(state.PointParameter(0x8126U) == 0.0F);

    state.SetPointSize(4.0F);
    state.SetPointParameter(0x8126U, 2.0F);
    state.SetPointParameter(0x8127U, 8.0F);
    CHECK(state.PointSize() == 4.0F);
    CHECK(state.PointParameter(0x8127U) == 8.0F);
    CHECK_THROWS_WITH_AS(state.SetPointSize(0.0F),
                         "GLES1 point size must be finite and positive",
                         std::invalid_argument);
    CHECK_THROWS_WITH_AS(state.SetPointParameter(0U, 1.0F),
                         "GLES1 scalar point parameter is unsupported",
                         std::invalid_argument);

    const std::array fog_color{0.1F, 0.2F, 0.3F, 0.4F};
    state.SetFog(ogplay::runtime::detail::kGles1FogColor, fog_color);
    CHECK(state.Fog(ogplay::runtime::detail::kGles1FogColor)[2] == 0.3F);
    const std::array ambient{0.4F, 0.3F, 0.2F, 1.0F};
    state.SetLightModel(ogplay::runtime::detail::kGles1LightModelAmbient, ambient);
    CHECK(state.LightModel(ogplay::runtime::detail::kGles1LightModelAmbient)[1] == 0.3F);
    const std::array position{1.0F, 2.0F, 3.0F, 1.0F};
    state.SetLight(kLight0, ogplay::runtime::detail::kGles1LightPosition, position);
    CHECK(state.Light(kLight0, ogplay::runtime::detail::kGles1LightPosition)[2] == 3.0F);
    const std::array shininess{64.0F};
    state.SetMaterial(ogplay::runtime::detail::kGles1FrontAndBack,
                      ogplay::runtime::detail::kGles1MaterialShininess, shininess);
    CHECK(state.Material(ogplay::runtime::detail::kGles1MaterialShininess)[0] == 64.0F);

    const std::array negative{-1.0F};
    CHECK_THROWS_WITH_AS(state.SetFog(ogplay::runtime::detail::kGles1FogDensity, negative),
                         "GLES1 fog density must be non-negative", std::invalid_argument);
    const std::array invalid_cutoff{91.0F};
    CHECK_THROWS_WITH_AS(state.SetLight(kLight0, kSpotCutoff, invalid_cutoff),
                         "GLES1 spot cutoff is invalid", std::invalid_argument);
    CHECK_THROWS_WITH_AS(state.SetLight(kLight0, kConstantAttenuation, negative),
                         "GLES1 light attenuation must be non-negative", std::invalid_argument);
    const std::array invalid_shininess{129.0F};
    CHECK_THROWS_WITH_AS(state.SetMaterial(ogplay::runtime::detail::kGles1FrontAndBack,
                                           ogplay::runtime::detail::kGles1MaterialShininess,
                                           invalid_shininess),
                         "GLES1 material shininess is outside 0..128", std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        state.SetMaterial(0x0404U, ogplay::runtime::detail::kGles1MaterialShininess, shininess),
        "GLES1 material face must be GL_FRONT_AND_BACK: 1028", std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        state.SetMaterial(0x0405U, ogplay::runtime::detail::kGles1MaterialShininess, shininess),
        "GLES1 material face must be GL_FRONT_AND_BACK: 1029", std::invalid_argument);
    state.SetMaterialSingleFaceQuirk(true);
    state.SetMaterial(ogplay::runtime::detail::kGles1Front,
                      ogplay::runtime::detail::kGles1MaterialShininess, shininess);
    const std::array back_shininess{32.0F};
    state.SetMaterial(ogplay::runtime::detail::kGles1Back,
                      ogplay::runtime::detail::kGles1MaterialShininess, back_shininess);
    CHECK(state.Material(ogplay::runtime::detail::kGles1Front,
                         ogplay::runtime::detail::kGles1MaterialShininess)[0] == 64.0F);
    CHECK(state.Material(ogplay::runtime::detail::kGles1Back,
                         ogplay::runtime::detail::kGles1MaterialShininess)[0] == 32.0F);
    const std::array not_finite{std::numeric_limits<float>::infinity()};
    CHECK_THROWS_WITH_AS(state.SetFog(ogplay::runtime::detail::kGles1FogDensity, not_finite),
                         "GLES1 fog value must be finite", std::invalid_argument);

    state.Reset();
    CHECK(state.Fog(ogplay::runtime::detail::kGles1FogColor)[2] == 0.0F);
    CHECK(state.Light(kLight0, ogplay::runtime::detail::kGles1LightPosition)[2] == 1.0F);
    CHECK(state.Material(ogplay::runtime::detail::kGles1MaterialShininess)[0] == 0.0F);
    CHECK(state.PointSize() == 1.0F);
    CHECK(state.PointParameter(0x8126U) == 0.0F);
    state.SetMaterial(ogplay::runtime::detail::kGles1Back,
                      ogplay::runtime::detail::kGles1MaterialShininess, back_shininess);
    CHECK(state.Material(ogplay::runtime::detail::kGles1Back,
                         ogplay::runtime::detail::kGles1MaterialShininess)[0] == 32.0F);
}

TEST_CASE("GLES1 lighting material and fog handlers are explicit") {
    ogplay::memory::AddressSpace address_space;
    ogplay::gles::GlesDispatchTable dispatch{ogplay::gles::GlesApi::gles1};
    ogplay::runtime::detail::AndroidBoundaryGles1FixedState state;
    ogplay::runtime::detail::BindAndroidBoundaryGles1FixedState(
        dispatch, state, address_space,
        [](const std::string_view operation) -> ogplay::gles::AngleFrame& {
            throw std::runtime_error(std::string(operation) + " has no current ANGLE frame");
        });
    for (const auto symbol : {"glClearStencil", "glDepthRangef", "glFogf", "glFogfv",
                              "glLightModelfv", "glLightf", "glLightfv", "glLineWidth",
                              "glMaterialf", "glMaterialfv", "glPointParameterf",
                              "glPointSize", "glPolygonOffset", "glStencilFunc",
                              "glStencilMask", "glStencilOp"}) {
        const auto id = ogplay::gles::FindGlesFunction(ogplay::gles::GlesApi::gles1, symbol);
        REQUIRE(id.has_value());
        CHECK(dispatch.IsBound(*id));
    }
    const auto alpha = ogplay::gles::FindGlesFunction(ogplay::gles::GlesApi::gles1, "glAlphaFunc");
    REQUIRE(alpha.has_value());
    CHECK_FALSE(dispatch.IsBound(*alpha));
}

TEST_CASE("GLES1 fixed matrix and scalar completion is directly bound") {
    ogplay::memory::AddressSpace address_space;
    ogplay::gles::GlesDispatchTable dispatch{ogplay::gles::GlesApi::gles1};
    ogplay::gles::GlesDispatchTable extensions{
        ogplay::gles::GlesApi::gles1_extensions};
    ogplay::runtime::detail::AndroidBoundaryGles1State core;
    ogplay::runtime::detail::AndroidBoundaryGles1LegacyState legacy;
    ogplay::runtime::detail::AndroidBoundaryGles1DrawState draw;
    ogplay::runtime::detail::AndroidBoundaryGles1QueryStrings strings{
        address_space};
    const auto no_frame = [](const std::string_view operation)
        -> ogplay::gles::AngleFrame& {
        throw std::runtime_error(std::string(operation) +
                                 " has no current ANGLE frame");
    };
    ogplay::runtime::detail::BindAndroidBoundaryGles1Core(
        dispatch, core, address_space, 1U, no_frame);
    ogplay::runtime::detail::BindAndroidBoundaryGles1Textures(
        dispatch, core, address_space, no_frame);
    ogplay::runtime::detail::BindAndroidBoundaryGles1Draw(
        dispatch, extensions, draw, core, legacy, address_space, no_frame);
    ogplay::runtime::detail::BindAndroidBoundaryGles1Queries(
        dispatch, strings, [](const std::uint32_t) { return std::string{"test"}; });
    ogplay::runtime::detail::BindAndroidBoundaryGles1Legacy(
        dispatch, legacy, core, draw, address_space, no_frame);
    ogplay::runtime::detail::BindAndroidBoundaryGles1Completion(
        dispatch, core, legacy, address_space, no_frame);
    ogplay::runtime::detail::BindAndroidBoundaryGles1Remaining(
        dispatch, core, legacy, draw, address_space, no_frame);
    constexpr std::array symbols{
        "glAlphaFuncx", "glClearColorx", "glClearDepthx", "glClipPlanex",
        "glColor4x", "glDepthRangex", "glFogx", "glFogxv", "glFrustumf",
        "glFrustumx", "glLightModelf", "glLightModelx", "glLightModelxv",
        "glLightx", "glLightxv", "glLineWidthx", "glLoadMatrixx",
        "glMaterialx", "glMaterialxv", "glMultMatrixx", "glMultiTexCoord4f",
        "glMultiTexCoord4x", "glNormal3x", "glOrthof", "glOrthox",
        "glPointParameterfv", "glPointParameterx", "glPointParameterxv",
        "glPointSizex", "glPolygonOffsetx", "glRotatex", "glSampleCoveragex",
        "glScalef", "glScalex", "glTranslatex"};
    for (const auto* symbol : symbols) {
        const auto id = ogplay::gles::FindGlesFunction(
            ogplay::gles::GlesApi::gles1, symbol);
        CAPTURE(symbol);
        REQUIRE(id.has_value());
        CHECK(dispatch.IsBound(*id));
    }
    for (std::size_t index = 0;
         index < ogplay::gles::GlesFunctionCount(ogplay::gles::GlesApi::gles1);
         ++index) {
        CAPTURE(index);
        CHECK(dispatch.IsBound(static_cast<ogplay::gles::GlesThunkId>(index)));
    }

    const std::array scale{2.0F, 0.0F, 0.0F, 0.0F,
                           0.0F, 3.0F, 0.0F, 0.0F,
                           0.0F, 0.0F, 4.0F, 0.0F,
                           0.0F, 0.0F, 0.0F, 1.0F};
    core.Matrices().Translate(1.0F, 2.0F, 3.0F);
    core.Matrices().Multiply(scale);
    CHECK(core.Matrices().Current()[0] == 2.0F);
    CHECK(core.Matrices().Current()[5] == 3.0F);
    CHECK(core.Matrices().Current()[10] == 4.0F);
    CHECK(core.Matrices().Current()[12] == 1.0F);
    CHECK(core.Matrices().Current()[13] == 2.0F);
    CHECK(core.Matrices().Current()[14] == 3.0F);

    const std::array coordinate{0.25F, 0.5F, 0.75F, 1.0F};
    legacy.SetCurrentTextureCoordinate(0x84C1U, coordinate);
    CHECK(legacy.CurrentTextureCoordinate(0x84C1U)[2] == 0.75F);
    const std::array attenuation{1.0F, 0.5F, 0.25F};
    core.Fixed().SetPointDistanceAttenuation(attenuation);
    CHECK(core.Fixed().PointDistanceAttenuation()[1] == 0.5F);
    CHECK_THROWS_AS(core.Fixed().SetPointDistanceAttenuation(
                        std::array{-1.0F, 0.0F, 0.0F}),
                    std::invalid_argument);
    draw.SetPointer(ogplay::runtime::detail::kGles1PointSizeArray, 0x84C0U,
                    1, 0x1406U, 0, 0x1000U, 0U);
    draw.SetEnabled(ogplay::runtime::detail::kGles1PointSizeArray, 0x84C0U,
                    true);
    CHECK(draw.Array(ogplay::runtime::detail::kGles1PointSizeArray,
                     0x84C0U).enabled);
    CHECK(ogplay::runtime::detail::kGles1FixedVertexShader.find(
              "mix(u_point_size, a_point_size, u_has_point_size)") !=
          std::string_view::npos);
}
