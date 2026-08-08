#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string_view>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::runtime::detail {

inline constexpr std::uint32_t kGles1FlatShadeModel = 0x1D00U;
inline constexpr std::uint32_t kGles1SmoothShadeModel = 0x1D01U;
inline constexpr std::uint32_t kGles1DontCare = 0x1100U;
inline constexpr std::uint32_t kGles1GenerateMipmapHint = 0x8192U;

class AndroidBoundaryGles1State final {
public:
    void Reset() noexcept;
    void SetShadeModel(std::uint32_t mode);
    [[nodiscard]] std::uint32_t ShadeModel() const noexcept;
    [[nodiscard]] const gles::GlesTransferState& TransferState() const noexcept;
    void SetTransferState(gles::GlesTransferState state) noexcept;
    void SetHint(std::uint32_t target, std::uint32_t mode);
    [[nodiscard]] std::uint32_t Hint(std::uint32_t target) const;
    void SetActiveTexture(std::uint32_t texture) noexcept;
    [[nodiscard]] std::uint32_t ActiveTexture() const noexcept;
    void SetCapability(std::uint32_t capability, bool enabled);
    [[nodiscard]] bool Capability(std::uint32_t capability) const;

private:
    std::uint32_t shade_model_{kGles1SmoothShadeModel};
    std::array<std::uint32_t, 5> hints_{kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare};
    std::uint32_t active_texture_{0x84C0U};
    std::map<std::uint64_t, bool> capabilities_;
    gles::GlesTransferState transfer_state_;
};

using AndroidBoundaryFrameResolver =
    std::function<gles::AngleFrame&(std::string_view operation)>;

[[nodiscard]] std::int32_t ScaleAndroidBoundaryViewportComponent(
    std::int32_t value, std::uint32_t factor);

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
