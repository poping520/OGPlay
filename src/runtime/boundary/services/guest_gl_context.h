#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::runtime {

using GuestGlContextId = std::uint32_t;

enum class GuestGlRenderer : std::uint8_t { fixed_function, programmable };

struct TextureBindingKey final {
    std::uint32_t texture_unit{};
    std::uint32_t target{};
    auto operator<=>(const TextureBindingKey&) const = default;
};

struct TextureObjectKey final {
    std::uint32_t texture{};
    std::uint32_t target{};
    auto operator<=>(const TextureObjectKey&) const = default;
};

struct SharedGlState final {
    gles::GlesTransferState transfer;
    std::uint32_t active_texture{0x84C0U};

    void ValidateActiveTexture(std::uint32_t texture) const;
    void SetActiveTexture(std::uint32_t texture);
    void ValidateTextureTarget(std::uint32_t target) const;
    void BindTexture(std::uint32_t target, std::uint32_t texture);
    [[nodiscard]] std::uint32_t BoundTexture(std::uint32_t target) const;
    [[nodiscard]] std::uint32_t BoundTexture(std::uint32_t texture_unit,
                                             std::uint32_t target) const;
    void DeleteTextures(std::span<const std::uint32_t> textures) noexcept;
    void SetTextureBaseFormat(std::uint32_t target, std::uint32_t format);
    [[nodiscard]] std::optional<std::uint32_t> TextureBaseFormat(
        std::uint32_t target) const;
    [[nodiscard]] std::optional<std::uint32_t> TextureBaseFormat(
        std::uint32_t texture_unit, std::uint32_t target) const;
    void SetGenerateMipmap(std::uint32_t target, bool enabled);
    [[nodiscard]] bool GenerateMipmapEnabled(std::uint32_t target) const;
    void ValidateFramebufferTarget(std::uint32_t target) const;
    void BindFramebuffer(std::uint32_t target, std::uint32_t framebuffer);
    void ValidateRenderbufferTarget(std::uint32_t target) const;
    void BindRenderbuffer(std::uint32_t target, std::uint32_t renderbuffer);
    void DeleteFramebuffers(std::span<const std::uint32_t> framebuffers) noexcept;
    void DeleteRenderbuffers(std::span<const std::uint32_t> renderbuffers) noexcept;
    [[nodiscard]] std::uint32_t Framebuffer() const noexcept;
    [[nodiscard]] std::uint32_t Renderbuffer() const noexcept;
    void SetViewport(std::array<std::int32_t, 4> viewport) noexcept;
    void SetScissor(std::array<std::int32_t, 4> scissor) noexcept;
    [[nodiscard]] const std::array<std::int32_t, 4>& Viewport() const noexcept;
    [[nodiscard]] const std::array<std::int32_t, 4>& Scissor() const noexcept;
    void SetClearColor(std::array<float, 4> color) noexcept;
    void SetClearDepth(float depth) noexcept;
    void SetClearStencil(std::int32_t stencil) noexcept;
    [[nodiscard]] const std::array<float, 4>& ClearColor() const noexcept;
    [[nodiscard]] float ClearDepth() const noexcept;
    [[nodiscard]] std::int32_t ClearStencil() const noexcept;
    void ValidateCapability(std::uint32_t capability) const;
    void SetCapability(std::uint32_t capability, bool enabled);
    [[nodiscard]] bool Capability(std::uint32_t capability) const;
    void SetCurrentProgram(std::uint32_t program) noexcept;
    [[nodiscard]] std::uint32_t CurrentProgram() const noexcept;
    [[nodiscard]] const std::map<TextureBindingKey, std::uint32_t>&
    TextureBindings() const noexcept;
    // GLES-visible errors latch per context and surface through glGetError
    // instead of terminating the guest call.
    void SetGuestError(std::uint32_t error) noexcept;
    [[nodiscard]] std::uint32_t TakeGuestError() noexcept;

    void Reset() noexcept;

private:
    std::map<TextureBindingKey, std::uint32_t> bound_textures_;
    std::map<TextureObjectKey, std::uint32_t> texture_base_formats_;
    std::map<TextureObjectKey, bool> generate_mipmap_;
    std::uint32_t framebuffer_{};
    std::uint32_t renderbuffer_{};
    std::array<std::int32_t, 4> viewport_{};
    std::array<std::int32_t, 4> scissor_{};
    std::array<float, 4> clear_color_{};
    float clear_depth_{1.0F};
    std::int32_t clear_stencil_{};
    std::map<std::uint32_t, bool> capabilities_;
    std::uint32_t current_program_{};
    std::uint32_t guest_error_{};
};

class NativeGlState final {
public:
    void BeginFixedDraw();
    void EndFixedDraw();
    [[nodiscard]] bool FixedDrawActive() const noexcept;
    void Reset() noexcept;

private:
    bool fixed_draw_active_{};
};

class GuestGlContext final {
public:
    explicit GuestGlContext(GuestGlContextId id = 1U);

    [[nodiscard]] GuestGlContextId Id() const noexcept;
    [[nodiscard]] SharedGlState& Shared() noexcept;
    [[nodiscard]] const SharedGlState& Shared() const noexcept;
    [[nodiscard]] NativeGlState& Native() noexcept;
    [[nodiscard]] const NativeGlState& Native() const noexcept;
    [[nodiscard]] GuestGlRenderer SelectDrawRenderer(
        bool fixed_vertex_array_enabled,
        bool programmable_attribute_enabled) const noexcept;
    void Reset() noexcept;

private:
    GuestGlContextId id_{};
    SharedGlState shared_;
    NativeGlState native_;
};

}  // namespace ogplay::runtime
