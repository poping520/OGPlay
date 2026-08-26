#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gles1_query.h"

namespace ogplay::runtime::detail {

inline constexpr std::uint32_t kGles1VertexArray = 0x8074U;
inline constexpr std::uint32_t kGles1NormalArray = 0x8075U;
inline constexpr std::uint32_t kGles1ColorArray = 0x8076U;
inline constexpr std::uint32_t kGles1TextureCoordArray = 0x8078U;
inline constexpr std::uint32_t kGles1MatrixIndexArray = 0x8844U;
inline constexpr std::uint32_t kGles1WeightArray = 0x86ADU;
inline constexpr std::uint32_t kGles1PointSizeArray = 0x898AU;
inline constexpr std::size_t kGles1MaximumDrawTextureUnits = 2U;

struct Gles1ClientArray final {
    std::int32_t size{};
    std::uint32_t type{};
    std::int32_t stride{};
    std::uint32_t pointer{};
    std::uint32_t buffer{};
    bool enabled{};
};

class AndroidBoundaryGles1DrawState final {
public:
    explicit AndroidBoundaryGles1DrawState(
        bool allow_single_stage_texcoord_fallback = true);
    void Reset() noexcept;
    void SetEnabled(std::uint32_t array, std::uint32_t client_texture,
                    bool enabled);
    void SetPointer(std::uint32_t array, std::uint32_t client_texture,
                    std::int32_t size, std::uint32_t type,
                    std::int32_t stride, std::uint32_t pointer,
                    std::uint32_t buffer);
    [[nodiscard]] Gles1ClientArray PreparePointer(
        std::uint32_t array, std::uint32_t client_texture,
        std::int32_t size, std::uint32_t type, std::int32_t stride,
        std::uint32_t pointer, std::uint32_t buffer) const;
    void CommitPointer(std::uint32_t array, std::uint32_t client_texture,
                       Gles1ClientArray pointer);
    [[nodiscard]] const Gles1ClientArray& Array(
        std::uint32_t array, std::uint32_t client_texture) const;
    void SetCurrentPaletteMatrix(std::uint32_t index);
    static void ValidateCurrentPaletteMatrix(std::uint32_t index);
    [[nodiscard]] std::uint32_t CurrentPaletteMatrix() const noexcept;
    [[nodiscard]] std::array<std::uint32_t,
                             kGles1MaximumDrawTextureUnits>
    ResolveTextureCoordinateUnits(
        std::span<const std::uint32_t> texture_units) const;

    void DrawArrays(gles::AngleFrame& frame,
                    const AndroidBoundaryGles1State& core,
                    const AndroidBoundaryGles1LegacyState& legacy,
                    memory::AddressSpace& address_space, std::uint32_t mode,
                    std::int32_t first, std::int32_t count,
                    std::uint64_t thread_id);
    void DrawElements(gles::AngleFrame& frame,
                      const AndroidBoundaryGles1State& core,
                      const AndroidBoundaryGles1LegacyState& legacy,
                      memory::AddressSpace& address_space, std::uint32_t mode,
                      std::int32_t count, std::uint32_t type,
                      std::uint32_t indices, std::uint64_t thread_id);

private:
    struct Program final {
        std::uint32_t name{};
        std::array<std::int32_t, 4U + kGles1MaximumDrawTextureUnits> attributes{};
        std::map<std::string, std::int32_t> uniforms;
        std::array<std::uint32_t, 5U + kGles1MaximumDrawTextureUnits> buffers{};
    };

    [[nodiscard]] static std::uint64_t ArrayKey(
        std::uint32_t array, std::uint32_t client_texture) noexcept;
    [[nodiscard]] Program& EnsureProgram(
        gles::AngleFrame& frame,
        const std::array<std::uint32_t, kGles1MaximumDrawTextureUnits>&
            sampled_targets);
    void PrepareArrays(gles::AngleFrame& frame, const Program& program,
                       const AndroidBoundaryGles1State& core,
                       const AndroidBoundaryGles1LegacyState& legacy,
                       memory::AddressSpace& address_space,
                       std::span<const std::uint32_t> texture_units,
                       std::uint32_t maximum_index, std::uint64_t thread_id);
    void ApplyUniforms(gles::AngleFrame& frame, const Program& program,
                       const AndroidBoundaryGles1State& core,
                       const AndroidBoundaryGles1LegacyState& legacy,
                       std::span<const std::uint32_t> texture_units,
                       const std::array<std::uint32_t,
                                        kGles1MaximumDrawTextureUnits>&
                           sampled_targets);

    std::map<std::uint64_t, Gles1ClientArray> arrays_;
    // Fixed-function programs are specialized per stage sampler type: a cube
    // map stage needs samplerCube/textureCube and a vec3 texcoord varying.
    std::array<Program, 1U << kGles1MaximumDrawTextureUnits> programs_;
    std::array<std::vector<std::byte>,
               4U + kGles1MaximumDrawTextureUnits>
        client_array_staging_;
    std::vector<std::byte> element_staging_;
    std::vector<std::uint16_t> draw_array_indices_;
    std::uint32_t current_palette_matrix_{};
    bool allow_single_stage_texcoord_fallback_{true};
};

[[nodiscard]] std::optional<bool> Gles1ClientStateEnabled(
    std::uint32_t capability, const AndroidBoundaryGles1DrawState& draw,
    const AndroidBoundaryGles1LegacyState& legacy);
[[nodiscard]] std::optional<std::int32_t> Gles1ClientArrayInteger(
    std::uint32_t pname, const AndroidBoundaryGles1DrawState& draw,
    const AndroidBoundaryGles1LegacyState& legacy);

void BindAndroidBoundaryGles1Draw(
    gles::GlesDispatchTable& dispatch,
    gles::GlesDispatchTable& extensions,
    AndroidBoundaryGles1DrawState& draw,
    AndroidBoundaryGles1State& core,
    AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
