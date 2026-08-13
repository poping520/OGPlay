#include "ogplay/frontend/gui_visuals.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <stb_image_write.h>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/arsc.h"
#include "ogplay/runtime/integration/host_image_decode.h"

namespace ogplay::frontend {
namespace {

struct PngOutput final {
    std::vector<std::byte> bytes;
    bool failed{};
};

void AppendPngBytes(void* context, void* data, const int size) noexcept {
    auto& output = *static_cast<PngOutput*>(context);
    if (output.failed || data == nullptr || size <= 0) return;
    try {
        const auto* begin = static_cast<const std::byte*>(data);
        output.bytes.insert(output.bytes.end(), begin, begin + size);
    } catch (...) {
        output.failed = true;
    }
}

[[nodiscard]] bool SupportedPngPath(const std::string_view path) {
    return path.ends_with(".png") && !path.ends_with(".9.png");
}

[[nodiscard]] bool ContainsControlCharacters(const std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](const char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7fU;
    });
}

[[nodiscard]] std::vector<std::byte> NormalizeIcon(
    const std::span<const std::byte> encoded) {
    const auto decoded = runtime::DecodeImageToArgb(encoded);
    if (!decoded.has_value()) return {};

    constexpr auto side = static_cast<std::size_t>(kLauncherIconSize);
    const auto source_width = static_cast<std::size_t>(decoded->width);
    const auto source_height = static_cast<std::size_t>(decoded->height);
    const auto resized = ResizeArgbBilinear(
        decoded->argb, static_cast<std::uint32_t>(source_width),
        static_cast<std::uint32_t>(source_height), kLauncherIconSize,
        kLauncherIconSize);
    std::vector<std::uint8_t> rgba(side * side * 4U);
    for (std::size_t y = 0; y < side; ++y) {
        for (std::size_t x = 0; x < side; ++x) {
            const auto argb = resized[y * side + x];
            const auto destination = (y * side + x) * 4U;
            rgba[destination] = static_cast<std::uint8_t>(argb >> 16U);
            rgba[destination + 1U] = static_cast<std::uint8_t>(argb >> 8U);
            rgba[destination + 2U] = static_cast<std::uint8_t>(argb);
            rgba[destination + 3U] = static_cast<std::uint8_t>(argb >> 24U);
        }
    }

    PngOutput output;
    const auto encoded_ok = stbi_write_png_to_func(
        AppendPngBytes, &output, static_cast<int>(kLauncherIconSize),
        static_cast<int>(kLauncherIconSize), 4, rgba.data(),
        static_cast<int>(kLauncherIconSize * 4U));
    if (encoded_ok == 0 || output.failed) return {};
    return output.bytes;
}

[[nodiscard]] std::optional<loader::ArscTable> ReadResources(
    const std::span<const std::byte> apk_bytes, const loader::ApkArchive& archive) {
    try {
        const auto bytes = loader::ReadApkEntry(apk_bytes, archive, "resources.arsc");
        return loader::ParseArsc(std::span(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace

std::vector<std::uint32_t> ResizeArgbBilinear(
    const std::span<const std::uint32_t> source,
    const std::uint32_t source_width, const std::uint32_t source_height,
    const std::uint32_t destination_width,
    const std::uint32_t destination_height) {
    const auto source_pixels = static_cast<std::uint64_t>(source_width) *
                               static_cast<std::uint64_t>(source_height);
    if (source_width == 0 || source_height == 0 || destination_width == 0 ||
        destination_height == 0 || source_pixels != source.size()) {
        throw std::invalid_argument("ARGB resize dimensions are invalid");
    }
    std::vector<std::uint32_t> result(
        static_cast<std::size_t>(destination_width) * destination_height);
    const auto channel = [](const std::uint32_t pixel, const unsigned shift) {
        return static_cast<float>((pixel >> shift) & 0xffU);
    };
    for (std::uint32_t y = 0; y < destination_height; ++y) {
        const auto source_y = std::clamp(
            (static_cast<float>(y) + 0.5F) *
                    static_cast<float>(source_height) /
                    static_cast<float>(destination_height) -
                0.5F,
            0.0F, static_cast<float>(source_height - 1U));
        const auto y0 = static_cast<std::uint32_t>(std::floor(source_y));
        const auto y1 = std::min(y0 + 1U, source_height - 1U);
        const auto fy = source_y - static_cast<float>(y0);
        for (std::uint32_t x = 0; x < destination_width; ++x) {
            const auto source_x = std::clamp(
                (static_cast<float>(x) + 0.5F) *
                        static_cast<float>(source_width) /
                        static_cast<float>(destination_width) -
                    0.5F,
                0.0F, static_cast<float>(source_width - 1U));
            const auto x0 = static_cast<std::uint32_t>(std::floor(source_x));
            const auto x1 = std::min(x0 + 1U, source_width - 1U);
            const auto fx = source_x - static_cast<float>(x0);
            const auto top_left = source[static_cast<std::size_t>(y0) *
                                         source_width + x0];
            const auto top_right = source[static_cast<std::size_t>(y0) *
                                          source_width + x1];
            const auto bottom_left = source[static_cast<std::size_t>(y1) *
                                            source_width + x0];
            const auto bottom_right = source[static_cast<std::size_t>(y1) *
                                             source_width + x1];
            std::uint32_t pixel{};
            for (const auto shift : {24U, 16U, 8U, 0U}) {
                const auto top = channel(top_left, shift) * (1.0F - fx) +
                                 channel(top_right, shift) * fx;
                const auto bottom = channel(bottom_left, shift) * (1.0F - fx) +
                                    channel(bottom_right, shift) * fx;
                const auto value = static_cast<std::uint32_t>(std::lround(
                    top * (1.0F - fy) + bottom * fy));
                pixel |= value << shift;
            }
            result[static_cast<std::size_t>(y) * destination_width + x] = pixel;
        }
    }
    return result;
}

bool ApkApplicationVisuals::Used(
    const ApplicationVisualFallback fallback) const noexcept {
    return std::find(fallbacks.begin(), fallbacks.end(), fallback) != fallbacks.end();
}

ApkApplicationVisuals ExtractApkApplicationVisuals(
    const std::span<const std::byte> apk_bytes) {
    const auto archive = loader::ParseApkArchive(apk_bytes);
    ApkApplicationVisuals result;
    result.manifest = loader::ReadAndroidManifest(apk_bytes, archive);
    result.display_name = result.manifest.package;

    const auto label_is_reference =
        result.manifest.application_label.has_value() &&
        std::holds_alternative<std::uint32_t>(*result.manifest.application_label);
    const auto needs_resources = result.manifest.application_icon.has_value() ||
                                 label_is_reference;
    auto resources = needs_resources ? ReadResources(apk_bytes, archive)
                                     : std::optional<loader::ArscTable>{};
    if (resources.has_value() &&
        resources->package_name != result.manifest.package) {
        resources.reset();
    }

    if (!result.manifest.application_label.has_value()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::label_attribute_missing);
    } else if (std::holds_alternative<std::string>(
                   *result.manifest.application_label)) {
        const auto& literal =
            std::get<std::string>(*result.manifest.application_label);
        if (literal.empty()) {
            result.fallbacks.push_back(
                ApplicationVisualFallback::label_literal_empty);
        } else {
            result.display_name = literal;
        }
    } else if (!resources.has_value()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::label_resources_unavailable);
    } else {
        const auto resource_id =
            std::get<std::uint32_t>(*result.manifest.application_label);
        const auto* entry = resources->FindById(resource_id);
        if (entry == nullptr || !entry->string_value.has_value() ||
            entry->string_value->empty()) {
            result.fallbacks.push_back(
                ApplicationVisualFallback::label_resource_missing);
        } else {
            result.display_name = *entry->string_value;
        }
    }
    if (ContainsControlCharacters(result.display_name)) {
        result.display_name = result.manifest.package;
        result.fallbacks.push_back(
            ApplicationVisualFallback::label_control_characters);
    }

    if (!result.manifest.application_icon.has_value()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_attribute_missing);
        return result;
    }
    if (!resources.has_value()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_resources_unavailable);
        return result;
    }
    const auto* icon = resources->FindById(*result.manifest.application_icon);
    if (icon == nullptr || !icon->string_value.has_value()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_resource_missing);
        return result;
    }
    if (!SupportedPngPath(*icon->string_value)) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_path_unsupported);
        return result;
    }
    try {
        const auto bytes = loader::ReadApkEntry(apk_bytes, archive,
                                                *icon->string_value);
        result.icon_png = NormalizeIcon(bytes);
    } catch (const std::exception&) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_entry_unavailable);
        return result;
    }
    if (result.icon_png.empty()) {
        result.fallbacks.push_back(
            ApplicationVisualFallback::icon_decode_failed);
    }
    return result;
}

}  // namespace ogplay::frontend
