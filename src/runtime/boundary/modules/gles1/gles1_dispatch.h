#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "runtime/boundary/services/guest_gl_context.h"
#include "runtime/boundary/core/boundary_callback.h"

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
    explicit AndroidBoundaryGles1MatrixState(
        const SharedGlState& shared);

    void Reset();
    void SetMode(std::uint32_t mode);
    [[nodiscard]] std::uint32_t Mode() const noexcept;
    void LoadIdentity();
    void Load(std::span<const float, 16> matrix);
    void Push();
    void Pop();
    void Rotate(float angle_degrees, float x, float y, float z);
    void Multiply(std::span<const float, 16> matrix);
    void Translate(float x, float y, float z);
    [[nodiscard]] const Gles1Matrix& Current() const noexcept;
    [[nodiscard]] const Gles1Matrix& Current(
        std::uint32_t mode, std::uint32_t texture) const;
    [[nodiscard]] std::size_t StackDepth(std::uint32_t mode) const;

private:
    [[nodiscard]] std::vector<Gles1Matrix>& CurrentStack() noexcept;
    [[nodiscard]] const std::vector<Gles1Matrix>& Stack(
        std::uint32_t mode, std::uint32_t texture) const;
    [[nodiscard]] std::uint32_t ActiveTexture() const noexcept;

    const SharedGlState* shared_{};
    std::uint32_t mode_{kGles1Modelview};
    std::vector<Gles1Matrix> modelview_;
    std::vector<Gles1Matrix> projection_;
    std::array<std::vector<Gles1Matrix>, 32> textures_;
};

class AndroidBoundaryGles1State final {
public:
    AndroidBoundaryGles1State();
    explicit AndroidBoundaryGles1State(SharedGlState& shared);
    ~AndroidBoundaryGles1State();
    void Reset();
    void SetShadeModel(std::uint32_t mode);
    [[nodiscard]] std::uint32_t ShadeModel() const noexcept;
    [[nodiscard]] const gles::GlesTransferState& TransferState() const noexcept;
    void SetTransferState(gles::GlesTransferState state) noexcept;
    void SetHint(std::uint32_t target, std::uint32_t mode);
    [[nodiscard]] std::uint32_t Hint(std::uint32_t target) const;
    void ValidateActiveTexture(std::uint32_t texture) const;
    void SetActiveTexture(std::uint32_t texture);
    [[nodiscard]] std::uint32_t ActiveTexture() const noexcept;
    void ValidateTextureTarget(std::uint32_t target) const;
    void BindTexture(std::uint32_t target, std::uint32_t texture);
    [[nodiscard]] std::uint32_t BoundTexture(std::uint32_t target) const;
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
    void SetLogicOperation(std::uint32_t operation);
    [[nodiscard]] std::uint32_t LogicOperation() const noexcept;
    [[nodiscard]] AndroidBoundaryGles1MatrixState& Matrices() noexcept;
    [[nodiscard]] const AndroidBoundaryGles1MatrixState& Matrices() const noexcept;
    [[nodiscard]] AndroidBoundaryGles1FixedState& Fixed() noexcept;
    [[nodiscard]] const AndroidBoundaryGles1FixedState& Fixed() const noexcept;
    [[nodiscard]] SharedGlState& Shared() noexcept;
    [[nodiscard]] const SharedGlState& Shared() const noexcept;
    [[nodiscard]] std::uint32_t TakeGuestError() const noexcept;

private:
    SharedGlState owned_shared_;
    SharedGlState* shared_{&owned_shared_};
    std::uint32_t shade_model_{kGles1SmoothShadeModel};
    std::array<std::uint32_t, 5> hints_{kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare, kGles1DontCare,
                                       kGles1DontCare};
    std::map<std::uint64_t, bool> capabilities_;
    std::uint32_t logic_operation_{0x1503U};
    AndroidBoundaryGles1MatrixState matrices_;
    std::unique_ptr<AndroidBoundaryGles1FixedState> fixed_;
};

using AndroidBoundaryFrameResolver =
    BoundaryCallback<gles::AngleFrame&(std::string_view operation)>;

[[nodiscard]] std::int32_t ScaleAndroidBoundaryViewportComponent(
    std::int32_t value, std::uint32_t factor);

void BindAndroidBoundaryGles1Core(
    gles::GlesDispatchTable& dispatch, AndroidBoundaryGles1State& state,
    memory::AddressSpace& address_space, std::uint32_t supersample_factor,
    AndroidBoundaryFrameResolver require_frame);

}  // namespace ogplay::runtime::detail
