#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::gles {
class AngleFrame;
using GlesThunkId = std::uint16_t;
}

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

inline constexpr std::array<std::string_view, 3> kGuestGlesExtensions{
    "GL_OES_compressed_ETC1_RGB8_texture",
    "GL_IMG_texture_compression_pvrtc",
    "GL_OES_rgb8_rgba8"};

[[nodiscard]] std::vector<std::string_view> GuestGlesExtensions(gles::AngleFrame& frame);
class GuestGlContext;
class A32CallFrame;

class AndroidBoundaryGles final {
public:
    explicit AndroidBoundaryGles(memory::AddressSpace& address_space);
    AndroidBoundaryGles(memory::AddressSpace& address_space,
                        GuestGlContext& context);
    ~AndroidBoundaryGles();
    AndroidBoundaryGles(const AndroidBoundaryGles&) = delete;
    AndroidBoundaryGles& operator=(const AndroidBoundaryGles&) = delete;

    [[nodiscard]] std::optional<std::uint32_t> Dispatch(
        std::string_view symbol, const A32CallFrame& call,
        gles::AngleFrame* frame);
    [[nodiscard]] std::optional<std::uint32_t> Dispatch(
        gles::GlesThunkId function_id,
        const A32CallFrame& call, gles::AngleFrame* frame);
    [[nodiscard]] bool HasEnabledVertexAttribute() const noexcept;
    void SetVertexAttributeValue(std::uint32_t index,
                                 std::array<float, 4> value);
    [[nodiscard]] std::int32_t VertexAttributeParameter(
        std::uint32_t index, std::uint32_t parameter) const;
    [[nodiscard]] std::uint32_t VertexAttributePointerIdentity(
        std::uint32_t index) const;
    [[nodiscard]] gles::GlesTransferStateSnapshot TransferState() const noexcept;
    void RestoreNativeState(gles::AngleFrame& frame);
    void Reset() noexcept;

private:
    class Impl;
    std::unique_ptr<GuestGlContext> owned_context_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
