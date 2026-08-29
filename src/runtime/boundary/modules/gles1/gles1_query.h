#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/gles/gles_dispatch.h"
#include "gles1_dispatch.h"
#include "runtime/boundary/core/boundary_callback.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime::detail {

class AndroidBoundaryGles1DrawState;

inline constexpr std::uint32_t kGles1MapBufferArenaBegin = 0x72000000U;
inline constexpr std::uint32_t kGles1MapBufferArenaBytes = 0x02000000U;

class AndroidBoundaryGles1MapBufferState final {
public:
    void MapGuestArena(memory::AddressSpace& address_space);
    void Reset() noexcept;
    [[nodiscard]] std::uint32_t Map(
        gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
        memory::AddressSpace& address_space, std::uint32_t target,
        std::uint32_t access, std::uint64_t thread_id);
    [[nodiscard]] bool Unmap(
        gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
        memory::AddressSpace& address_space, std::uint32_t target,
        std::uint64_t thread_id);
    [[nodiscard]] std::uint32_t Pointer(
        gles::AngleFrame& frame, const AndroidBoundaryGles1State& core,
        std::uint32_t target, std::uint32_t parameter) const;

private:
    struct Mapping final {
        std::uint32_t guest_address{};
        std::uint32_t size{};
        std::byte* host_address{};
    };
    struct FreeRange final {
        std::uint32_t offset{};
        std::uint32_t size{};
    };

    [[nodiscard]] std::uint32_t Allocate(std::uint32_t size);
    void Release(std::uint32_t guest_address, std::uint32_t size) noexcept;

    bool arena_mapped_{};
    std::map<std::uint32_t, Mapping> mappings_;
    std::vector<FreeRange> free_ranges_;
};

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
    AndroidBoundaryGles1LegacyState(
        const AndroidBoundaryGles1LegacyState&) = delete;
    AndroidBoundaryGles1LegacyState& operator=(
        const AndroidBoundaryGles1LegacyState&) = delete;

    void Reset();
    void ValidateAlphaFunction(std::uint32_t function,
                               float reference) const;
    void ValidateClientActiveTexture(std::uint32_t texture) const;
    void ValidateColor(std::span<const float, 4> color) const;
    void ValidateNormal(std::span<const float, 3> normal) const;
    void ValidateClipPlane(std::uint32_t plane,
                           std::span<const float, 4> equation) const;
    void ValidateTextureEnvironment(std::uint32_t texture,
                                    std::uint32_t target,
                                    std::uint32_t pname,
                                    std::span<const float> values) const;
    void SetAlphaFunction(std::uint32_t function, float reference);
    void SetClientActiveTexture(std::uint32_t texture);
    void SetColor(std::span<const float, 4> color);
    void SetNormal(std::span<const float, 3> normal);
    void SetCurrentTextureCoordinate(std::uint32_t texture,
                                     std::span<const float, 4> coordinate);
    void SetClipPlane(std::uint32_t plane,
                      std::span<const float, 4> equation);
    void SetTextureEnvironment(std::uint32_t texture, std::uint32_t target,
                               std::uint32_t pname,
                               std::span<const float> values);

    [[nodiscard]] std::uint32_t AlphaFunction() const noexcept;
    [[nodiscard]] float AlphaReference() const noexcept;
    [[nodiscard]] std::uint32_t ClientActiveTexture() const noexcept;
    [[nodiscard]] const std::array<float, 4>& Color() const noexcept;
    [[nodiscard]] const std::array<float, 3>& Normal() const noexcept;
    [[nodiscard]] const std::array<float, 4>& CurrentTextureCoordinate(
        std::uint32_t texture) const;
    [[nodiscard]] const std::array<float, 4>& ClipPlane(
        std::uint32_t plane) const;
    [[nodiscard]] const std::vector<float>& TextureEnvironment(
        std::uint32_t texture, std::uint32_t pname) const;

private:
    std::uint32_t alpha_function_{};
    float alpha_reference_{};
    std::uint32_t client_active_texture_{};
    std::array<float, 4> color_{};
    std::array<float, 3> normal_{};
    std::array<std::array<float, 4>, 32> texture_coordinates_{};
    std::array<std::array<float, 4>, 6> clip_planes_{};
    std::map<std::uint64_t, std::vector<float>> texture_environment_;
};

using AndroidBoundaryGles1StringResolver =
    BoundaryCallback<std::string(std::uint32_t parameter)>;

void BindAndroidBoundaryGles1Queries(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1QueryStrings& strings,
    AndroidBoundaryGles1StringResolver resolve_string);

void BindAndroidBoundaryGles1MapBuffer(
    gles::GlesDispatchTable& extension_dispatch,
    AndroidBoundaryGles1MapBufferState& map_buffer,
    AndroidBoundaryGles1State& core,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

void BindAndroidBoundaryGles1Legacy(
    gles::GlesDispatchTable& dispatch,
    AndroidBoundaryGles1LegacyState& legacy,
    AndroidBoundaryGles1State& core,
    AndroidBoundaryGles1DrawState& draw,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

void BindAndroidBoundaryGles1Textures(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
