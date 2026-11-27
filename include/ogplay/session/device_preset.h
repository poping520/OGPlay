#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/runtime/integration/dexvm_android.h"

namespace ogplay::session {

class DevicePresetError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One data/devices/<id>.toml (schema 1; tools/validate_devices.py is the
// executable schema). The runtime consumes the Build identity (including
// [board]), the cpuinfo identity and the GL strings; cpu name/cores, display
// and memory are validated and kept for display or later consumers.
struct DevicePreset final {
    std::string id, label, description;
    runtime::SimulatedDeviceState::BuildIdentity build;

    struct Cpu final {
        std::string name;
        std::string cpuinfo_hardware;  // /proc/cpuinfo "Hardware"
        std::uint32_t midr{};          // implementer/variant/part/revision
        std::uint32_t cores{};
    } cpu;

    struct Gpu final {
        std::string vendor, renderer;  // GL_VENDOR / GL_RENDERER
    } gpu;

    struct Display final {
        std::uint32_t width{}, height{}, density_dpi{};
    } display;

    std::uint32_t ram_mb{};
};

inline constexpr std::string_view kDefaultDevicePresetId = "gt-i9300";

[[nodiscard]] DevicePreset LoadDevicePresetText(std::string_view text, std::string_view expected_id);
// Reads <directory>/<id>.toml; the id must be lowercase kebab-case.
[[nodiscard]] DevicePreset LoadDevicePreset(const std::filesystem::path& directory, std::string_view id);

}  // namespace ogplay::session
