#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string_view>

#include "android_boundary_gles1_query.h"

namespace ogplay::runtime::detail {

inline constexpr std::uint32_t kGles1VertexArray = 0x8074U;
inline constexpr std::uint32_t kGles1NormalArray = 0x8075U;
inline constexpr std::uint32_t kGles1ColorArray = 0x8076U;
inline constexpr std::uint32_t kGles1TextureCoordArray = 0x8078U;

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
    AndroidBoundaryGles1DrawState();
    void Reset() noexcept;
    void SetEnabled(std::uint32_t array, std::uint32_t client_texture,
                    bool enabled);
    void SetPointer(std::uint32_t array, std::uint32_t client_texture,
                    std::int32_t size, std::uint32_t type,
                    std::int32_t stride, std::uint32_t pointer,
                    std::uint32_t buffer);
    [[nodiscard]] const Gles1ClientArray& Array(
        std::uint32_t array, std::uint32_t client_texture) const;

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
        std::array<std::int32_t, 4> attributes{};
        std::map<std::string_view, std::int32_t> uniforms;
        std::array<std::uint32_t, 5> buffers{};
    };

    [[nodiscard]] static std::uint64_t ArrayKey(
        std::uint32_t array, std::uint32_t client_texture) noexcept;
    void EnsureProgram(gles::AngleFrame& frame);
    void PrepareArrays(gles::AngleFrame& frame,
                       const AndroidBoundaryGles1State& core,
                       const AndroidBoundaryGles1LegacyState& legacy,
                       memory::AddressSpace& address_space,
                       std::uint32_t maximum_index, std::uint64_t thread_id);
    void ApplyUniforms(gles::AngleFrame& frame,
                       const AndroidBoundaryGles1State& core,
                       const AndroidBoundaryGles1LegacyState& legacy);

    std::map<std::uint64_t, Gles1ClientArray> arrays_;
    Program program_;
};

void BindAndroidBoundaryGles1Draw(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1DrawState& draw,
    AndroidBoundaryGles1State& core,
    AndroidBoundaryGles1LegacyState& legacy,
    memory::AddressSpace& address_space,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
