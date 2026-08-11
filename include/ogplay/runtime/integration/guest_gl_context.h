#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "ogplay/gles/gles_transfer_state.h"

namespace ogplay::runtime {

using GuestGlContextId = std::uint32_t;

struct SharedGlState final {
    gles::GlesTransferState transfer;
    std::uint32_t active_texture{0x84C0U};

    void SetActiveTexture(std::uint32_t texture);
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

    void Reset() noexcept;

private:
    std::map<std::uint32_t, std::uint32_t> bound_textures_;
    std::map<std::uint32_t, std::uint32_t> texture_base_formats_;
    std::map<std::uint32_t, bool> generate_mipmap_;
};

class GuestGlContext final {
public:
    explicit GuestGlContext(GuestGlContextId id = 1U);

    [[nodiscard]] GuestGlContextId Id() const noexcept;
    [[nodiscard]] SharedGlState& Shared() noexcept;
    [[nodiscard]] const SharedGlState& Shared() const noexcept;
    void Reset() noexcept;

private:
    GuestGlContextId id_{};
    SharedGlState shared_;
};

}  // namespace ogplay::runtime
