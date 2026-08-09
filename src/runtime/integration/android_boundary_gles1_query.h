#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/gles/gles_dispatch.h"
#include "android_boundary_gles1.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime::detail {

class AndroidBoundaryGles1QueryStrings final {
public:
    explicit AndroidBoundaryGles1QueryStrings(memory::AddressSpace& address_space);

    void Validate(std::uint32_t parameter) const;
    [[nodiscard]] std::uint32_t Publish(std::uint32_t parameter,
                                        std::string_view value,
                                        std::uint64_t thread_id);

private:
    memory::AddressSpace* address_space_{};
    bool region_mapped_{};
};

inline constexpr std::uint32_t kGles1MaxTextureAnisotropy = 0x84FFU;
inline constexpr std::uint32_t kGles1TextureEnvironment = 0x2300U;
inline constexpr std::uint32_t kGles1TextureEnvironmentMode = 0x2200U;
inline constexpr std::uint32_t kGles1TextureEnvironmentColor = 0x2201U;
inline constexpr std::uint32_t kGles1CombineRgb = 0x8571U;
inline constexpr std::uint32_t kGles1CombineAlpha = 0x8572U;
inline constexpr std::uint32_t kGles1RgbScale = 0x8573U;
inline constexpr std::uint32_t kGles1AlphaScale = 0x0D1CU;
inline constexpr std::uint32_t kGles1Source0Rgb = 0x8580U;
inline constexpr std::uint32_t kGles1Source1Rgb = 0x8581U;
inline constexpr std::uint32_t kGles1Source2Rgb = 0x8582U;
inline constexpr std::uint32_t kGles1Source0Alpha = 0x8588U;
inline constexpr std::uint32_t kGles1Source1Alpha = 0x8589U;
inline constexpr std::uint32_t kGles1Source2Alpha = 0x858AU;
inline constexpr std::uint32_t kGles1Operand0Rgb = 0x8590U;
inline constexpr std::uint32_t kGles1Operand1Rgb = 0x8591U;
inline constexpr std::uint32_t kGles1Operand2Rgb = 0x8592U;
inline constexpr std::uint32_t kGles1Operand0Alpha = 0x8598U;
inline constexpr std::uint32_t kGles1Operand1Alpha = 0x8599U;
inline constexpr std::uint32_t kGles1Operand2Alpha = 0x859AU;

class AndroidBoundaryGles1LegacyState final {
public:
    AndroidBoundaryGles1LegacyState();

    void Reset();
    void SetAlphaFunction(std::uint32_t function, float reference);
    void SetClientActiveTexture(std::uint32_t texture);
    void SetColor(std::span<const float, 4> color);
    void SetTextureEnvironment(std::uint32_t texture, std::uint32_t target,
                               std::uint32_t pname,
                               std::span<const float> values);

    [[nodiscard]] std::uint32_t AlphaFunction() const noexcept;
    [[nodiscard]] float AlphaReference() const noexcept;
    [[nodiscard]] std::uint32_t ClientActiveTexture() const noexcept;
    [[nodiscard]] const std::array<float, 4>& Color() const noexcept;
    [[nodiscard]] const std::vector<float>& TextureEnvironment(
        std::uint32_t texture, std::uint32_t pname) const;

private:
    std::uint32_t alpha_function_{};
    float alpha_reference_{};
    std::uint32_t client_active_texture_{};
    std::array<float, 4> color_{};
    std::map<std::uint64_t, std::vector<float>> texture_environment_;
};

using AndroidBoundaryGles1StringResolver =
    std::function<std::string(std::uint32_t parameter)>;

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string);

void BindAndroidBoundaryGles1Legacy(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1LegacyState& legacy,
    AndroidBoundaryGles1State& core,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

void BindAndroidBoundaryGles1Textures(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
