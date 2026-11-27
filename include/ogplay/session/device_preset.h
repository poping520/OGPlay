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
// executable schema). Only the Build identity reaches the runtime today; the
// cpu/gpu/display/memory tables are validated and kept for later consumers.
struct DevicePreset final {
    std::string id, label, description;
    runtime::SimulatedDeviceState::BuildIdentity build;

    struct Cpu final {
        std::string name;
        std::uint32_t cores{};
    } cpu;

    struct Gpu final {
        std::string vendor, renderer;
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
