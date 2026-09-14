#pragma once

#include <array>
#include <memory>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

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
    std::uint32_t resource_group{1U};
    void ShareObjectsFrom(const SharedGlState& source) { objects_ = source.objects_; }
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
    [[nodiscard]] std::uint32_t ReadFramebuffer() const noexcept { return read_framebuffer_; }
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
    struct ObjectMetadata {
        std::map<TextureObjectKey, std::uint32_t> texture_base_formats;
        std::map<TextureObjectKey, bool> generate_mipmap;
    };
    std::shared_ptr<ObjectMetadata> objects_{std::make_shared<ObjectMetadata>()};
    std::uint32_t framebuffer_{};
    std::uint32_t read_framebuffer_{};
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

struct ProgrammableAttribute final {
    std::int32_t size{4};
    std::uint32_t type{0x1406U};
    bool normalized{};
    std::int32_t stride{};
    std::uint32_t pointer{}, buffer{};
    bool enabled{}, defined{};
    std::uint32_t definition_lr{}, enable_lr{};
    std::array<float, 4> current{0.0F, 0.0F, 0.0F, 1.0F};
    bool integer{};
    std::uint32_t divisor{};
    std::uint32_t current_kind{};  // 0 float, 1 signed integer, 2 unsigned integer
    std::array<std::uint32_t, 4> integer_current{};
};

struct ProgrammableGlState final {
    std::array<ProgrammableAttribute, 16> attributes{};
    std::map<std::uint32_t, std::array<ProgrammableAttribute, 16>> vertex_arrays;
    std::uint32_t vertex_array{};
    std::uint32_t fixed_vertex_array{};
    std::vector<std::uint32_t> staging_buffers;
    std::vector<std::vector<std::byte>> client_array_staging;
    std::uint32_t index_staging_buffer{};
    void BindVertexArray(std::uint32_t name) {
        vertex_arrays[vertex_array] = attributes;
        const auto constants = attributes;
        attributes = vertex_arrays[name];
        for (std::size_t i = 0; i < attributes.size(); ++i) {
            attributes[i].current = constants[i].current;
            attributes[i].current_kind = constants[i].current_kind;
            attributes[i].integer_current = constants[i].integer_current;
        }
        vertex_array = name;
    }
};

class GuestGlContext final {
public:
    explicit GuestGlContext(GuestGlContextId id = 1U);

    [[nodiscard]] GuestGlContextId Id() const noexcept;
    ProgrammableGlState& Programmable() noexcept { return programmable_; }
    const ProgrammableGlState& Programmable() const noexcept { return programmable_; }
    void SetShareGroup(std::uint32_t group) noexcept { share_group_ = group; shared_.resource_group = group; }
    [[nodiscard]] std::uint32_t ShareGroup() const noexcept {
        return share_group_ == 0U ? id_ : share_group_;
    }
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
    std::uint32_t share_group_{};
    SharedGlState shared_;
    NativeGlState native_;
    ProgrammableGlState programmable_;
};

}  // namespace ogplay::runtime
