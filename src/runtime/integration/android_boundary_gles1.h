#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime::detail {

class AndroidBoundaryGles1FixedState;

inline constexpr std::uint32_t kGles1FlatShadeModel = 0x1D00U;
inline constexpr std::uint32_t kGles1SmoothShadeModel = 0x1D01U;
inline constexpr std::uint32_t kGles1DontCare = 0x1100U;
inline constexpr std::uint32_t kGles1GenerateMipmapHint = 0x8192U;
inline constexpr std::uint32_t kGles1GenerateMipmap = 0x8191U;
inline constexpr std::uint32_t kGles1Modelview = 0x1700U;
inline constexpr std::uint32_t kGles1Projection = 0x1701U;
inline constexpr std::uint32_t kGles1Texture = 0x1702U;

using Gles1Matrix = std::array<float, 16>;

class AndroidBoundaryGles1MatrixState final {
public:
    AndroidBoundaryGles1MatrixState();

    void Reset();
    void SetActiveTexture(std::uint32_t texture);
    void SetMode(std::uint32_t mode);
    [[nodiscard]] std::uint32_t Mode() const noexcept;
    void LoadIdentity();
    void Load(std::span<const float, 16> matrix);
    void Push();
    void Pop();
    void Rotate(float angle_degrees, float x, float y, float z);
    void Translate(float x, float y, float z);
    [[nodiscard]] const Gles1Matrix& Current() const noexcept;
    [[nodiscard]] const Gles1Matrix& Current(
        std::uint32_t mode, std::uint32_t texture) const;
    [[nodiscard]] std::size_t StackDepth(std::uint32_t mode) const;

private:
    [[nodiscard]] std::vector<Gles1Matrix>& CurrentStack() noexcept;
    [[nodiscard]] const std::vector<Gles1Matrix>& Stack(
        std::uint32_t mode, std::uint32_t texture) const;

    std::uint32_t mode_{kGles1Modelview};
    std::uint32_t active_texture_{0x84C0U};
    std::vector<Gles1Matrix> modelview_;
    std::vector<Gles1Matrix> projection_;
    std::array<std::vector<Gles1Matrix>, 32> textures_;
};

class AndroidBoundaryGles1State final {
public:
    AndroidBoundaryGles1State();
    ~AndroidBoundaryGles1State();
    void Reset();
    void SetShadeModel(std::uint32_t mode);
    [[nodiscard]] std::uint32_t ShadeModel() const noexcept;
    [[nodiscard]] const gles::GlesTransferState& TransferState() const noexcept;
    void SetTransferState(gles::GlesTransferState state) noexcept;
    void SetHint(std::uint32_t target, std::uint32_t mode);
    [[nodiscard]] std::uint32_t Hint(std::uint32_t target) const;
    void SetActiveTexture(std::uint32_t texture);
    [[nodiscard]] std::uint32_t ActiveTexture() const noexcept;
    void BindTexture(std::uint32_t target, std::uint32_t texture);
    void DeleteTextures(std::span<const std::uint32_t> textures) noexcept;
    void SetTextureBaseFormat(std::uint32_t target, std::uint32_t format);
    [[nodiscard]] std::optional<std::uint32_t> TextureBaseFormat(
        std::uint32_t target) const;
    [[nodiscard]] std::optional<std::uint32_t> TextureBaseFormat(
        std::uint32_t texture_unit, std::uint32_t target) const;
    void SetGenerateMipmap(std::uint32_t target, bool enabled);
    [[nodiscard]] bool GenerateMipmapEnabled(std::uint32_t target) const;
    void SetCapability(std::uint32_t capability, bool enabled);
    [[nodiscard]] bool Capability(std::uint32_t capability) const;
    [[nodiscard]] bool Capability(std::uint32_t texture_unit,
                                  std::uint32_t capability) const;
    [[nodiscard]] std::vector<std::uint32_t> EnabledTextureUnits() const;
    [[nodiscard]] AndroidBoundaryGles1MatrixState& Matrices() noexcept;
    [[nodiscard]] const AndroidBoundaryGles1MatrixState& Matrices() const noexcept;
    [[nodiscard]] AndroidBoundaryGles1FixedState& Fixed() noexcept;
    [[nodiscard]] const AndroidBoundaryGles1FixedState& Fixed() const noexcept;

private:
    std::uint32_t shade_model_{kGles1SmoothShadeModel};
    std::array<std::uint32_t, 5> hints_{kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare};
    std::uint32_t active_texture_{0x84C0U};
    std::map<std::uint32_t, std::uint32_t> bound_textures_;
    std::map<std::uint32_t, std::uint32_t> texture_base_formats_;
    std::map<std::uint32_t, bool> generate_mipmap_;
    std::map<std::uint64_t, bool> capabilities_;
    gles::GlesTransferState transfer_state_;
    AndroidBoundaryGles1MatrixState matrices_;
    std::unique_ptr<AndroidBoundaryGles1FixedState> fixed_;
};

using AndroidBoundaryFrameResolver =
    std::function<gles::AngleFrame&(std::string_view operation)>;

[[nodiscard]] std::int32_t ScaleAndroidBoundaryViewportComponent(
    std::int32_t value, std::uint32_t factor);

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space, std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
