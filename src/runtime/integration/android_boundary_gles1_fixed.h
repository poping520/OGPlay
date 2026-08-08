#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "ogplay/gles/gles_dispatch.h"

#include "android_boundary_gles1.h"

namespace ogplay::runtime::detail {

inline constexpr std::uint32_t kGles1FrontAndBack = 0x0408U;
inline constexpr std::uint32_t kGles1Front = 0x0404U;
inline constexpr std::uint32_t kGles1Back = 0x0405U;
inline constexpr std::uint32_t kGles1FogMode = 0x0B65U;
inline constexpr std::uint32_t kGles1FogDensity = 0x0B62U;
inline constexpr std::uint32_t kGles1FogColor = 0x0B66U;
inline constexpr std::uint32_t kGles1LightModelAmbient = 0x0B53U;
inline constexpr std::uint32_t kGles1LightAmbient = 0x1200U;
inline constexpr std::uint32_t kGles1LightPosition = 0x1203U;
inline constexpr std::uint32_t kGles1SpotExponent = 0x1205U;
inline constexpr std::uint32_t kGles1MaterialShininess = 0x1601U;

class AndroidBoundaryGles1FixedState final {
  public:
    AndroidBoundaryGles1FixedState();

    void SetMaterialSingleFaceQuirk(bool enabled) noexcept;
    void Reset();
    void SetFog(std::uint32_t pname, std::span<const float> values);
    void SetLightModel(std::uint32_t pname, std::span<const float> values);
    void SetLight(std::uint32_t light, std::uint32_t pname, std::span<const float> values);
    void SetMaterial(std::uint32_t face, std::uint32_t pname, std::span<const float> values);

    [[nodiscard]] const std::vector<float>& Fog(std::uint32_t pname) const;
    [[nodiscard]] const std::vector<float>& LightModel(std::uint32_t pname) const;
    [[nodiscard]] const std::vector<float>& Light(std::uint32_t light, std::uint32_t pname) const;
    [[nodiscard]] const std::vector<float>& Material(std::uint32_t pname) const;
    [[nodiscard]] const std::vector<float>& Material(
        std::uint32_t face, std::uint32_t pname) const;

  private:
    std::map<std::uint32_t, std::vector<float>> fog_;
    std::map<std::uint32_t, std::vector<float>> light_model_;
    std::map<std::uint64_t, std::vector<float>> lights_;
    std::map<std::uint32_t, std::vector<float>> material_front_;
    std::map<std::uint32_t, std::vector<float>> material_back_;
    bool allow_material_single_face_{};
};

void BindAndroidBoundaryGles1FixedState(gles::GlesDispatchTable& dispatch,
                                        AndroidBoundaryGles1FixedState& state,
                                        memory::AddressSpace& address_space,
                                        AndroidBoundaryFrameResolver require_frame);

} // namespace ogplay::runtime::detail
