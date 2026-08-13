#include "ogplay/frontend/gui_visuals.h"

#include <algorithm>
#include <exception>
#include <optional>
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

[[nodiscard]] std::vector<std::byte> NormalizeIcon(
    const std::span<const std::byte> encoded) {
    const auto decoded = runtime::DecodeImageToArgb(encoded);
    if (!decoded.has_value()) return {};

    constexpr auto side = static_cast<std::size_t>(kLauncherIconSize);
    std::vector<std::uint8_t> rgba(side * side * 4U);
    const auto source_width = static_cast<std::size_t>(decoded->width);
    const auto source_height = static_cast<std::size_t>(decoded->height);
    for (std::size_t y = 0; y < side; ++y) {
        const auto source_y = y * source_height / side;
        for (std::size_t x = 0; x < side; ++x) {
            const auto source_x = x * source_width / side;
            const auto argb = decoded->argb[source_y * source_width + source_x];
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
