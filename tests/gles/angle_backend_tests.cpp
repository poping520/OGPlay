#include <doctest/doctest.h>

#include <stdexcept>

#include "ogplay/gles/angle_backend.h"

using ogplay::gles::AngleBackend;
using ogplay::gles::AngleBackendAvailability;
using ogplay::gles::AngleBackendPreference;
using ogplay::gles::AngleDevice;
using ogplay::gles::AngleHostPlatform;
using ogplay::gles::AngleRenderer;

TEST_CASE("ANGLE backend order is deterministic per host platform") {
    const auto windows = ogplay::gles::AngleBackendCandidates(
        AngleHostPlatform::windows);
    REQUIRE(windows.size() == 3);
    CHECK(windows[0] == AngleBackend{AngleRenderer::d3d11,
                                     AngleDevice::hardware});
    CHECK(windows[1] == AngleBackend{AngleRenderer::vulkan,
                                     AngleDevice::hardware});
    CHECK(windows[2] == AngleBackend{AngleRenderer::vulkan,
                                     AngleDevice::swiftshader});

    const auto linux = ogplay::gles::AngleBackendCandidates(AngleHostPlatform::linux);
    REQUIRE(linux.size() == 2);
    CHECK(linux[0] == AngleBackend{AngleRenderer::vulkan, AngleDevice::hardware});
    CHECK(linux[1] == AngleBackend{AngleRenderer::vulkan,
                                   AngleDevice::swiftshader});

    const auto macos = ogplay::gles::AngleBackendCandidates(AngleHostPlatform::macos);
    REQUIRE(macos.size() == 2);
    CHECK(macos[0] == AngleBackend{AngleRenderer::metal, AngleDevice::hardware});
    CHECK(macos[1] == AngleBackend{AngleRenderer::vulkan,
                                   AngleDevice::swiftshader});
}

TEST_CASE("ANGLE automatic selection uses only reported availability") {
    AngleBackendAvailability availability{};
    availability.d3d11_hardware = true;
    availability.vulkan_hardware = true;
    availability.swiftshader_vulkan = true;

    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::windows, AngleBackendPreference::automatic,
              availability) ==
          AngleBackend{AngleRenderer::d3d11, AngleDevice::hardware});

    availability.d3d11_hardware = false;
    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::windows, AngleBackendPreference::automatic,
              availability) ==
          AngleBackend{AngleRenderer::vulkan, AngleDevice::hardware});

    availability.vulkan_hardware = false;
    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::linux, AngleBackendPreference::automatic,
              availability) ==
          AngleBackend{AngleRenderer::vulkan, AngleDevice::swiftshader});

    availability.swiftshader_vulkan = false;
    availability.metal_hardware = true;
    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::macos, AngleBackendPreference::automatic,
              availability) ==
          AngleBackend{AngleRenderer::metal, AngleDevice::hardware});
}

TEST_CASE("ANGLE preference never silently crosses hardware and software") {
    AngleBackendAvailability availability{};
    availability.d3d11_hardware = true;
    availability.swiftshader_vulkan = true;

    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::windows, AngleBackendPreference::hardware_only,
              availability) ==
          AngleBackend{AngleRenderer::d3d11, AngleDevice::hardware});
    CHECK(ogplay::gles::SelectAngleBackend(
              AngleHostPlatform::windows, AngleBackendPreference::software_only,
              availability) ==
          AngleBackend{AngleRenderer::vulkan, AngleDevice::swiftshader});

    availability.d3d11_hardware = false;
    CHECK_FALSE(ogplay::gles::SelectAngleBackend(
        AngleHostPlatform::windows, AngleBackendPreference::hardware_only,
        availability));
}

TEST_CASE("ANGLE invalid policy inputs fail explicitly") {
    CHECK(ogplay::gles::AngleBackendName(
              {AngleRenderer::metal, AngleDevice::hardware})
              .compare("metal/hardware") == 0);
    CHECK(ogplay::gles::AngleBackendName(
              {AngleRenderer::vulkan, AngleDevice::swiftshader})
              .compare("vulkan/swiftshader") == 0);

    const auto invalid_platform = [] {
        static_cast<void>(ogplay::gles::AngleBackendCandidates(
            static_cast<AngleHostPlatform>(99)));
    };
    const auto invalid_device = [] {
        static_cast<void>(ogplay::gles::AngleBackendName(
            {AngleRenderer::metal, AngleDevice::swiftshader}));
    };
    const auto unknown_device = [] {
        static_cast<void>(ogplay::gles::AngleBackendName(
            {AngleRenderer::vulkan, static_cast<AngleDevice>(99)}));
    };
    CHECK_THROWS_AS(invalid_platform(), std::invalid_argument);
    CHECK_THROWS_AS(invalid_device(), std::invalid_argument);
    CHECK_THROWS_AS(unknown_device(), std::invalid_argument);
}
