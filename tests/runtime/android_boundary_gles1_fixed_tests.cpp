#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../../src/runtime/integration/android_boundary_gles1_fixed.h"
#include "ogplay/memory/address_space.h"

namespace {

constexpr std::uint32_t kLight0 = 0x4000U;
constexpr std::uint32_t kDiffuse = 0x1201U;
constexpr std::uint32_t kSpotCutoff = 0x1206U;
constexpr std::uint32_t kConstantAttenuation = 0x1207U;

} // namespace

TEST_CASE("GLES1 lighting material and fog state validates and resets") {
    ogplay::runtime::detail::AndroidBoundaryGles1FixedState state;
    CHECK(state.Fog(ogplay::runtime::detail::kGles1FogDensity)[0] == 1.0F);
    CHECK(state.LightModel(ogplay::runtime::detail::kGles1LightModelAmbient)[0] == 0.2F);
    CHECK(state.Light(kLight0, kDiffuse)[0] == 1.0F);
    CHECK(state.Light(kLight0 + 1U, kDiffuse)[0] == 0.0F);
    CHECK(state.Material(ogplay::runtime::detail::kGles1MaterialShininess)[0] == 0.0F);

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
        state.SetMaterial(0U, ogplay::runtime::detail::kGles1MaterialShininess, shininess),
        "GLES1 material face must be GL_FRONT_AND_BACK: 0", std::invalid_argument);
    const std::array not_finite{std::numeric_limits<float>::infinity()};
    CHECK_THROWS_WITH_AS(state.SetFog(ogplay::runtime::detail::kGles1FogDensity, not_finite),
                         "GLES1 fog value must be finite", std::invalid_argument);

    state.Reset();
    CHECK(state.Fog(ogplay::runtime::detail::kGles1FogColor)[2] == 0.0F);
    CHECK(state.Light(kLight0, ogplay::runtime::detail::kGles1LightPosition)[2] == 1.0F);
    CHECK(state.Material(ogplay::runtime::detail::kGles1MaterialShininess)[0] == 0.0F);
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
    for (const auto symbol : {"glFogf", "glFogfv", "glLightModelfv", "glLightf", "glLightfv",
                              "glMaterialf", "glMaterialfv"}) {
        const auto id = ogplay::gles::FindGlesFunction(ogplay::gles::GlesApi::gles1, symbol);
        REQUIRE(id.has_value());
        CHECK(dispatch.IsBound(*id));
    }
    const auto alpha = ogplay::gles::FindGlesFunction(ogplay::gles::GlesApi::gles1, "glAlphaFunc");
    REQUIRE(alpha.has_value());
    CHECK_FALSE(dispatch.IsBound(*alpha));
}
