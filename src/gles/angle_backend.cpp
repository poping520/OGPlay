#include "ogplay/gles/angle_backend.h"

#include <stdexcept>

namespace ogplay::gles {
namespace {

constexpr AngleBackend kWindowsCandidates[]{
    {AngleRenderer::d3d11, AngleDevice::hardware},
    {AngleRenderer::vulkan, AngleDevice::hardware},
    {AngleRenderer::vulkan, AngleDevice::swiftshader},
};
constexpr AngleBackend kLinuxCandidates[]{
    {AngleRenderer::vulkan, AngleDevice::hardware},
    {AngleRenderer::vulkan, AngleDevice::swiftshader},
};
constexpr AngleBackend kMacosCandidates[]{
    {AngleRenderer::metal, AngleDevice::hardware},
    {AngleRenderer::vulkan, AngleDevice::swiftshader},
};

[[nodiscard]] bool IsAllowed(const AngleBackend backend,
                             const AngleBackendPreference preference) {
    switch (preference) {
    case AngleBackendPreference::automatic:
        return true;
    case AngleBackendPreference::hardware_only:
        return backend.device == AngleDevice::hardware;
    case AngleBackendPreference::software_only:
        return backend.device == AngleDevice::swiftshader;
    }
    throw std::invalid_argument("unknown ANGLE backend preference");
}

[[nodiscard]] bool IsAvailable(const AngleBackend backend,
                               const AngleBackendAvailability& availability) {
    if (backend.device == AngleDevice::swiftshader) {
        return backend.renderer == AngleRenderer::vulkan &&
               availability.swiftshader_vulkan;
    }
    switch (backend.renderer) {
    case AngleRenderer::d3d11:
        return availability.d3d11_hardware;
    case AngleRenderer::vulkan:
        return availability.vulkan_hardware;
    case AngleRenderer::metal:
        return availability.metal_hardware;
    }
    throw std::invalid_argument("unknown ANGLE renderer");
}

}  // namespace

std::span<const AngleBackend> AngleBackendCandidates(
    const AngleHostPlatform platform) {
    switch (platform) {
    case AngleHostPlatform::windows:
        return kWindowsCandidates;
    case AngleHostPlatform::linux:
        return kLinuxCandidates;
    case AngleHostPlatform::macos:
        return kMacosCandidates;
    }
    throw std::invalid_argument("unknown ANGLE host platform");
}

std::optional<AngleBackend> SelectAngleBackend(
    const AngleHostPlatform platform, const AngleBackendPreference preference,
    const AngleBackendAvailability& availability) {
    for (const auto backend : AngleBackendCandidates(platform)) {
        if (IsAllowed(backend, preference) && IsAvailable(backend, availability)) {
            return backend;
        }
    }
    return std::nullopt;
}

std::string_view AngleBackendName(const AngleBackend backend) {
    switch (backend.device) {
    case AngleDevice::swiftshader:
        if (backend.renderer != AngleRenderer::vulkan) {
            throw std::invalid_argument("SwiftShader requires ANGLE Vulkan");
        }
        return "vulkan/swiftshader";
    case AngleDevice::hardware:
        break;
    }
    if (backend.device != AngleDevice::hardware) {
        throw std::invalid_argument("unknown ANGLE device");
    }
    switch (backend.renderer) {
    case AngleRenderer::d3d11:
        return "d3d11/hardware";
    case AngleRenderer::vulkan:
        return "vulkan/hardware";
    case AngleRenderer::metal:
        return "metal/hardware";
    }
    throw std::invalid_argument("unknown ANGLE renderer");
}

}  // namespace ogplay::gles
