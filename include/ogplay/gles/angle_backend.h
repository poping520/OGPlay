#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace ogplay::gles {

enum class AngleHostPlatform {
    windows,
    linux,
    macos,
};

enum class AngleRenderer {
    d3d11,
    vulkan,
    metal,
};

enum class AngleDevice {
    hardware,
    swiftshader,
};

enum class AngleBackendPreference {
    automatic,
    hardware_only,
    software_only,
};

struct AngleBackend final {
    AngleRenderer renderer{};
    AngleDevice device{};

    bool operator==(const AngleBackend&) const = default;
};

struct AngleBackendAvailability final {
    bool d3d11_hardware{};
    bool vulkan_hardware{};
    bool metal_hardware{};
    bool swiftshader_vulkan{};
};

[[nodiscard]] std::span<const AngleBackend> AngleBackendCandidates(
    AngleHostPlatform platform);
[[nodiscard]] std::optional<AngleBackend> SelectAngleBackend(
    AngleHostPlatform platform, AngleBackendPreference preference,
    const AngleBackendAvailability& availability);
[[nodiscard]] std::string_view AngleBackendName(AngleBackend backend);

}  // namespace ogplay::gles
