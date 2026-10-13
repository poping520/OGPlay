#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ogplay::gles {

enum class CompressedTextureFamily : std::uint8_t { all, atc, pvrtc };

[[nodiscard]] constexpr std::string_view CompressedTextureFamilyName(
    const CompressedTextureFamily family) noexcept {
    switch (family) {
    case CompressedTextureFamily::all: return "all";
    case CompressedTextureFamily::atc: return "atc";
    case CompressedTextureFamily::pvrtc: return "pvrtc";
    }
    return "invalid";
}

struct CompressedTextureCapabilities final {
    CompressedTextureFamily family{CompressedTextureFamily::all};
    std::vector<std::int32_t> native_formats;
    std::vector<std::int32_t> software_formats;
    std::vector<std::int32_t> available_formats;
    std::vector<std::int32_t> formats;
    std::vector<std::string_view> extensions;
};

[[nodiscard]] CompressedTextureCapabilities BuildCompressedTextureCapabilities(
    std::span<const std::int32_t> native_formats,
    CompressedTextureFamily family, int client_version);

}  // namespace ogplay::gles
