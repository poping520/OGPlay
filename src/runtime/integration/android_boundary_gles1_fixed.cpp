#include "android_boundary_gles1_fixed.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kFogStart = 0x0B63U;
constexpr std::uint32_t kFogEnd = 0x0B64U;
constexpr std::uint32_t kFogLinear = 0x2601U;
constexpr std::uint32_t kFogExp = 0x0800U;
constexpr std::uint32_t kFogExp2 = 0x0801U;
constexpr std::uint32_t kLightModelTwoSide = 0x0B52U;
constexpr std::uint32_t kLight0 = 0x4000U;
constexpr std::uint32_t kLight7 = 0x4007U;
constexpr std::uint32_t kLightDiffuse = 0x1201U;
constexpr std::uint32_t kLightSpecular = 0x1202U;
constexpr std::uint32_t kSpotDirection = 0x1204U;
constexpr std::uint32_t kSpotCutoff = 0x1206U;
constexpr std::uint32_t kConstantAttenuation = 0x1207U;
constexpr std::uint32_t kLinearAttenuation = 0x1208U;
constexpr std::uint32_t kQuadraticAttenuation = 0x1209U;
constexpr std::uint32_t kMaterialEmission = 0x1600U;
constexpr std::uint32_t kMaterialAmbientAndDiffuse = 0x1602U;

[[nodiscard]] std::uint64_t LightKey(const std::uint32_t light, const std::uint32_t pname) {
    if (light < kLight0 || light > kLight7) {
        throw std::invalid_argument("GLES1 light must be GL_LIGHT0..GL_LIGHT7");
    }
    return (static_cast<std::uint64_t>(light) << 32U) | pname;
}

void RequireFinite(const std::span<const float> values, const std::string_view operation) {
    if (!std::ranges::all_of(values, [](const float value) { return std::isfinite(value); })) {
        throw std::invalid_argument(std::string(operation) + " value must be finite");
    }
}

void RequireCount(const std::span<const float> values, const std::size_t expected,
                  const std::string_view operation) {
    if (values.size() != expected) {
        throw std::invalid_argument(std::string(operation) + " parameter count is invalid");
    }
    RequireFinite(values, operation);
}

[[nodiscard]] std::size_t FogCount(const std::uint32_t pname) {
    switch (pname) {
    case kGles1FogMode:
    case kGles1FogDensity:
    case kFogStart:
    case kFogEnd: return 1U;
    case kGles1FogColor: return 4U;
    default: throw std::invalid_argument("GLES1 fog pname is invalid");
    }
}

[[nodiscard]] std::size_t LightModelCount(const std::uint32_t pname) {
    if (pname == kGles1LightModelAmbient)
        return 4U;
    if (pname == kLightModelTwoSide)
        return 1U;
    throw std::invalid_argument("GLES1 light-model pname is invalid");
}

[[nodiscard]] std::size_t LightCount(const std::uint32_t pname) {
    switch (pname) {
    case kGles1LightAmbient:
    case kLightDiffuse:
    case kLightSpecular:
    case kGles1LightPosition: return 4U;
    case kSpotDirection: return 3U;
    case kGles1SpotExponent:
    case kSpotCutoff:
    case kConstantAttenuation:
    case kLinearAttenuation:
    case kQuadraticAttenuation: return 1U;
    default: throw std::invalid_argument("GLES1 light pname is invalid");
    }
}

[[nodiscard]] std::size_t MaterialCount(const std::uint32_t pname) {
    switch (pname) {
    case kGles1LightAmbient:
    case kLightDiffuse:
    case kLightSpecular:
    case kMaterialEmission:
    case kMaterialAmbientAndDiffuse: return 4U;
    case kGles1MaterialShininess: return 1U;
    default: throw std::invalid_argument("GLES1 material pname is invalid");
    }
}

[[nodiscard]] std::vector<float> ReadGuestFloats(const memory::AddressSpace& address_space,
                                                 const std::uint32_t address,
                                                 const std::size_t count,
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

} // namespace

AndroidBoundaryGles1FixedState::AndroidBoundaryGles1FixedState() { Reset(); }

void AndroidBoundaryGles1FixedState::SetMaterialFrontFaceQuirk(const bool enabled) noexcept {
    normalize_material_front_face_ = enabled;
}

void AndroidBoundaryGles1FixedState::Reset() {
    fog_ = {{kGles1FogMode, {static_cast<float>(kFogExp)}},
            {kGles1FogDensity, {1.0F}},
            {kFogStart, {0.0F}},
            {kFogEnd, {1.0F}},
            {kGles1FogColor, {0.0F, 0.0F, 0.0F, 0.0F}}};
    light_model_ = {{kGles1LightModelAmbient, {0.2F, 0.2F, 0.2F, 1.0F}},
                    {kLightModelTwoSide, {0.0F}}};
    lights_.clear();
    for (std::uint32_t light = kLight0; light <= kLight7; ++light) {
        lights_[LightKey(light, kGles1LightAmbient)] = {0.0F, 0.0F, 0.0F, 1.0F};
        const auto direct = light == kLight0 ? 1.0F : 0.0F;
        lights_[LightKey(light, kLightDiffuse)] = {direct, direct, direct, 1.0F};
        lights_[LightKey(light, kLightSpecular)] = {direct, direct, direct, 1.0F};
        lights_[LightKey(light, kGles1LightPosition)] = {0.0F, 0.0F, 1.0F, 0.0F};
        lights_[LightKey(light, kSpotDirection)] = {0.0F, 0.0F, -1.0F};
        lights_[LightKey(light, kGles1SpotExponent)] = {0.0F};
        lights_[LightKey(light, kSpotCutoff)] = {180.0F};
        lights_[LightKey(light, kConstantAttenuation)] = {1.0F};
        lights_[LightKey(light, kLinearAttenuation)] = {0.0F};
        lights_[LightKey(light, kQuadraticAttenuation)] = {0.0F};
    }
    material_ = {{kGles1LightAmbient, {0.2F, 0.2F, 0.2F, 1.0F}},
                 {kLightDiffuse, {0.8F, 0.8F, 0.8F, 1.0F}},
                 {kLightSpecular, {0.0F, 0.0F, 0.0F, 1.0F}},
                 {kMaterialEmission, {0.0F, 0.0F, 0.0F, 1.0F}},
                 {kGles1MaterialShininess, {0.0F}}};
}

void AndroidBoundaryGles1FixedState::SetFog(const std::uint32_t pname,
                                            const std::span<const float> values) {
    RequireCount(values, FogCount(pname), "GLES1 fog");
    if (pname == kGles1FogMode) {
        const auto mode = values.front();
        if (mode != static_cast<float>(kFogLinear) && mode != static_cast<float>(kFogExp) &&
            mode != static_cast<float>(kFogExp2)) {
            throw std::invalid_argument("GLES1 fog mode is invalid");
        }
    }
    if (pname == kGles1FogDensity && values.front() < 0.0F) {
        throw std::invalid_argument("GLES1 fog density must be non-negative");
    }
    fog_[pname].assign(values.begin(), values.end());
}

void AndroidBoundaryGles1FixedState::SetLightModel(const std::uint32_t pname,
                                                   const std::span<const float> values) {
    RequireCount(values, LightModelCount(pname), "GLES1 light model");
    light_model_[pname].assign(values.begin(), values.end());
}

void AndroidBoundaryGles1FixedState::SetLight(const std::uint32_t light, const std::uint32_t pname,
                                              const std::span<const float> values) {
    const auto key = LightKey(light, pname);
    RequireCount(values, LightCount(pname), "GLES1 light");
    const auto value = values.front();
    if (pname == kGles1SpotExponent && (value < 0.0F || value > 128.0F)) {
        throw std::invalid_argument("GLES1 spot exponent is outside 0..128");
    }
    if (pname == kSpotCutoff && ((value < 0.0F || value > 90.0F) && value != 180.0F)) {
        throw std::invalid_argument("GLES1 spot cutoff is invalid");
    }
    if ((pname == kConstantAttenuation || pname == kLinearAttenuation ||
         pname == kQuadraticAttenuation) &&
        value < 0.0F) {
        throw std::invalid_argument("GLES1 light attenuation must be non-negative");
    }
    lights_[key].assign(values.begin(), values.end());
}

void AndroidBoundaryGles1FixedState::SetMaterial(const std::uint32_t face,
                                                 const std::uint32_t pname,
                                                 const std::span<const float> values) {
    const auto accepted_face = normalize_material_front_face_ && face == 0x0404U
                                   ? kGles1FrontAndBack
                                   : face;
    if (accepted_face != kGles1FrontAndBack) {
        throw std::invalid_argument("GLES1 material face must be GL_FRONT_AND_BACK: " +
                                    std::to_string(face));
    }
    RequireCount(values, MaterialCount(pname), "GLES1 material");
    if (pname == kGles1MaterialShininess && (values.front() < 0.0F || values.front() > 128.0F)) {
        throw std::invalid_argument("GLES1 material shininess is outside 0..128");
    }
    if (pname == kMaterialAmbientAndDiffuse) {
        material_[kGles1LightAmbient].assign(values.begin(), values.end());
        material_[kLightDiffuse].assign(values.begin(), values.end());
        return;
    }
    material_[pname].assign(values.begin(), values.end());
}

const std::vector<float>& AndroidBoundaryGles1FixedState::Fog(const std::uint32_t pname) const {
    return fog_.at(pname);
}

const std::vector<float>&
AndroidBoundaryGles1FixedState::LightModel(const std::uint32_t pname) const {
    return light_model_.at(pname);
}

const std::vector<float>& AndroidBoundaryGles1FixedState::Light(const std::uint32_t light,
                                                                const std::uint32_t pname) const {
    return lights_.at(LightKey(light, pname));
}

const std::vector<float>&
AndroidBoundaryGles1FixedState::Material(const std::uint32_t pname) const {
    return material_.at(pname);
}

void BindAndroidBoundaryGles1FixedState(gles::GlesDispatchTable& dispatch,
                                        AndroidBoundaryGles1FixedState& state,
                                        memory::AddressSpace& address_space,
                                        AndroidBoundaryFrameResolver require_frame) {
    if (!require_frame) {
        throw std::invalid_argument("GLES1 fixed-state binding is incomplete");
    }
    dispatch.Bind("glFogf", [&state, require_frame](const auto arguments, const std::uint64_t) {
        static_cast<void>(require_frame("glFogf"));
        const std::array value{std::bit_cast<float>(arguments[1])};
        state.SetFog(arguments[0], value);
        return 0U;
    });
    dispatch.Bind("glFogfv", [&state, &address_space,
                              require_frame](const auto arguments, const std::uint64_t thread_id) {
        const auto values =
            ReadGuestFloats(address_space, arguments[1], FogCount(arguments[0]), thread_id);
        static_cast<void>(require_frame("glFogfv"));
        state.SetFog(arguments[0], values);
        return 0U;
    });
    dispatch.Bind("glLightModelfv", [&state, &address_space, require_frame](
                                        const auto arguments, const std::uint64_t thread_id) {
        const auto values =
            ReadGuestFloats(address_space, arguments[1], LightModelCount(arguments[0]), thread_id);
        static_cast<void>(require_frame("glLightModelfv"));
        state.SetLightModel(arguments[0], values);
        return 0U;
    });
    dispatch.Bind("glLightf", [&state, require_frame](const auto arguments, const std::uint64_t) {
        static_cast<void>(require_frame("glLightf"));
        const std::array value{std::bit_cast<float>(arguments[2])};
        state.SetLight(arguments[0], arguments[1], value);
        return 0U;
    });
    dispatch.Bind("glLightfv", [&state, &address_space, require_frame](
                                   const auto arguments, const std::uint64_t thread_id) {
        const auto values =
            ReadGuestFloats(address_space, arguments[2], LightCount(arguments[1]), thread_id);
        static_cast<void>(require_frame("glLightfv"));
        state.SetLight(arguments[0], arguments[1], values);
        return 0U;
    });
    dispatch.Bind("glMaterialf",
                  [&state, require_frame](const auto arguments, const std::uint64_t) {
                      static_cast<void>(require_frame("glMaterialf"));
                      const std::array value{std::bit_cast<float>(arguments[2])};
                      state.SetMaterial(arguments[0], arguments[1], value);
                      return 0U;
                  });
    dispatch.Bind("glMaterialfv", [&state, &address_space, require_frame](
                                      const auto arguments, const std::uint64_t thread_id) {
        const auto values =
            ReadGuestFloats(address_space, arguments[2], MaterialCount(arguments[1]), thread_id);
        static_cast<void>(require_frame("glMaterialfv"));
        state.SetMaterial(arguments[0], arguments[1], values);
        return 0U;
    });
}

} // namespace ogplay::runtime::detail
