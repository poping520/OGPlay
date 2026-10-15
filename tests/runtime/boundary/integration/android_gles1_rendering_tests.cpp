#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include "ogplay/hal/clock.h"
#include <future>
#include "ogplay/runtime/vfs/vfs.h"
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/cpu/dynarmic.h"
#include "ogplay/core/logger.h"
#include "ogplay/gles/egl_lifecycle.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/bus.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/modules/gles1/gles1_dispatch.h"
#include "runtime/boundary/modules/gles1/gles1_draw.h"
#include "runtime/boundary/modules/gles1/gles1_query.h"
#include "runtime/boundary/modules/gles1/gles1_support.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/services/graphics_dispatch.h"
#include "runtime/boundary/modules/opensles/opensles_abi.h"

#include "android_boundary_fixture.h"

using namespace ogplay::test::android_boundary;

TEST_CASE("GLES1 high sampled unit uses the unique unit-zero coordinate array") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);

    const auto texture_name = fixture.output.Add(0x300U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                       {1U, texture_name.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(texture_name, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv2.so", "glActiveTexture", {0x84DFU}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x0DE1U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x0DE1U, 0x2800U, 0x2600U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x0DE1U, 0x2801U, 0x2600U}) == 0U);
    const auto pixels = fixture.output.Add(0x320U);
    const std::array<std::byte, 8> texture_pixels{
        std::byte{}, std::byte{}, std::byte{}, std::byte{0xff},
        std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}};
    fixture.memory.Write(pixels, texture_pixels, 1U);
    fixture.bus.Write32(fixture.stack, 1U, 1U);
    fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
    fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
    fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
    fixture.bus.Write32(fixture.stack.Add(16U), pixels.Value(), 1U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                       {0x0DE1U, 0U, 0x1908U, 2U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0DE1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTexEnvi",
              {ogplay::runtime::detail::kGles1TextureEnvironment,
               ogplay::runtime::detail::kGles1TextureEnvironmentMode,
               0x1E01U}) == 0U);

    const auto vertices = fixture.output.Add(0x380U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    const auto texcoords = fixture.output.Add(0x3C0U);
    for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
        fixture.bus.Write32(
            texcoords.Add(vertex * 2U * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(0.75F), 1U);
        fixture.bus.Write32(
            texcoords.Add((vertex * 2U + 1U) * sizeof(std::uint32_t)),
            0U, 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                       {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                       {2U, 0x1406U, 0U, texcoords.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1TextureCoordArray}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                       {0U, 0U, 0U,
                        std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    bool found_red_pixel{};
    for (std::size_t pixel = 0; pixel < frame->rgba8.size(); pixel += 4U) {
        found_red_pixel |= frame->rgba8[pixel] > 200U &&
                           frame->rgba8[pixel + 1U] < 20U &&
                           frame->rgba8[pixel + 2U] < 20U;
    }
    CHECK(found_red_pixel);
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 cube map textures bind upload and sample the fixed pipeline") {
    BoundaryFixture fixture;
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    fixture.boundary.OpenManagedSurface();
    const auto renderer = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F01U});
    REQUIRE(renderer != 0U);
    const auto renderer_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{renderer}, 256U, 1U);
    std::string renderer_text(renderer_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{renderer},
                        std::as_writable_bytes(std::span(renderer_text)), 1U);
    CHECK(renderer_text != "OpenGL ES-CM 1.1");
    const auto version = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F02U});
    REQUIRE(version != 0U);
    const auto version_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{version}, 64U, 1U);
    std::string version_text(version_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{version},
                        std::as_writable_bytes(std::span(version_text)), 1U);
    CHECK(version_text == "OpenGL ES-CM 1.1");
    const auto extensions = fixture.Call(
        "libGLESv1_CM.so", "glGetString", {0x1F03U});
    REQUIRE(extensions != 0U);
    const auto extension_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{extensions}, 4096U, 1U);
    std::string extension_text(extension_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{extensions},
                        std::as_writable_bytes(std::span(extension_text)), 1U);
    for (const auto* extension : {"GL_OES_texture_cube_map",
                                  "GL_OES_compressed_ETC1_RGB8_texture",
                                  "GL_IMG_texture_compression_pvrtc",
                                  "GL_OES_mapbuffer",
                                  "GL_OES_rgb8_rgba8"}) {
        CAPTURE(extension);
        CHECK(extension_text.find(std::string{extension} + ' ') !=
              std::string::npos);
    }
    CHECK(extension_text.ends_with(' '));

    const auto gles2_extensions = fixture.Call(
        "libGLESv2.so", "glGetString", {0x1F03U});
    REQUIRE(gles2_extensions != 0U);
    const auto gles2_extension_length = fixture.memory.CStringLength(
        ogplay::memory::GuestAddress{gles2_extensions}, 4096U, 1U);
    std::string gles2_extension_text(gles2_extension_length, '\0');
    fixture.memory.Read(ogplay::memory::GuestAddress{gles2_extensions},
                        std::as_writable_bytes(std::span(gles2_extension_text)), 1U);
    CHECK(gles2_extension_text.starts_with(
          "GL_OES_compressed_ETC1_RGB8_texture "
          "GL_IMG_texture_compression_pvrtc "
          "GL_OES_rgb8_rgba8 "));
    CHECK(gles2_extension_text.find("GL_OES_texture_3D") == std::string::npos);

    const auto names = fixture.output.Add(0x300U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGenTextures",
                       {1U, names.Value()}) == 0U);
    const auto texture = fixture.bus.Read32(names, 1U);
    REQUIRE(texture != 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x8513U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBindTexture",
                       {0x8515U, texture}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0x0500U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glGetError") == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x8513U, 0x2800U, 0x2600U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexParameteri",
                       {0x8513U, 0x2801U, 0x2600U}) == 0U);

    // +X uploads red, -X uploads blue and the remaining faces upload green;
    // the sampled direction (1, 0, 0) must select the red face.
    const auto faces = fixture.output.Add(0x320U);
    const std::array<std::array<std::byte, 4>, 3> face_pixels{
        std::array{std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff}}};
    for (auto face = 0x8515U; face <= 0x851AU; ++face) {
        const auto& pixel = face == 0x8515U   ? face_pixels[0]
                            : face == 0x8516U ? face_pixels[1]
                                              : face_pixels[2];
        fixture.memory.Write(faces, pixel, 1U);
        fixture.bus.Write32(fixture.stack, 1U, 1U);
        fixture.bus.Write32(fixture.stack.Add(4U), 0U, 1U);
        fixture.bus.Write32(fixture.stack.Add(8U), 0x1908U, 1U);
        fixture.bus.Write32(fixture.stack.Add(12U), 0x1401U, 1U);
        fixture.bus.Write32(fixture.stack.Add(16U), faces.Value(), 1U);
        CAPTURE(face);
        CHECK(fixture.Call("libGLESv1_CM.so", "glTexImage2D",
                           {face, 0U, 0x1908U, 1U}) == 0U);
    }

    const auto vertices = fixture.output.Add(0x340U);
    const std::array vertex_values{
        -0.75F, -0.75F, 0.0F, 0.75F, -0.75F, 0.0F,
        0.0F, 0.75F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    const auto colors = fixture.output.Add(0x370U);
    const std::array<std::byte, 12> color_values{
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
    fixture.memory.Write(colors, color_values, 1U);
    const auto texcoords = fixture.output.Add(0x3A0U);
    for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
        const std::array direction{1.0F, 0.0F, 0.0F};
        for (std::size_t component = 0; component < 3U; ++component) {
            fixture.bus.Write32(
                texcoords.Add((vertex * 3U + component) * 4U),
                std::bit_cast<std::uint32_t>(direction[component]), 1U);
        }
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glColorPointer",
                       {4U, 0x1401U, 0U, colors.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClientActiveTexture",
                       {0x84C0U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glTexCoordPointer",
                       {3U, 0x1406U, 0U, texcoords.Value()}) == 0U);
    for (const auto array : {
             ogplay::runtime::detail::kGles1VertexArray,
             ogplay::runtime::detail::kGles1ColorArray,
             ogplay::runtime::detail::kGles1TextureCoordArray}) {
        CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                           {array}) == 0U);
    }
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glTexEnvi",
              {ogplay::runtime::detail::kGles1TextureEnvironment,
               ogplay::runtime::detail::kGles1TextureEnvironmentMode,
               0x1E01U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x8513U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                       {0U, 0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) ==
          0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto cube_frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(cube_frame.has_value());
    bool found_red_pixel = false;
    bool found_blue_pixel = false;
    for (std::size_t pixel = 0; pixel < cube_frame->rgba8.size();
         pixel += 4U) {
        const auto red = cube_frame->rgba8[pixel] > 200U;
        const auto green = cube_frame->rgba8[pixel + 1U] > 200U;
        const auto blue = cube_frame->rgba8[pixel + 2U] > 200U;
        found_red_pixel |= red && !green && !blue;
        found_blue_pixel |= blue && !red && !green;
    }
    CHECK(found_red_pixel);
    CHECK_FALSE(found_blue_pixel);
}

TEST_CASE("GLES1 lighting preserves diffuse material alpha for blending") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0U, 0U, 4U, 3U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glClearColor",
              {std::bit_cast<std::uint32_t>(0.25F),
               std::bit_cast<std::uint32_t>(0.5F),
               std::bit_cast<std::uint32_t>(0.75F),
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);

    const auto vertices = fixture.output.Add(0x600U);
    const auto normals = fixture.output.Add(0x680U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(vertices.Add(index * 4U),
                            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
        fixture.bus.Write32(
            normals.Add(index * 4U),
            std::bit_cast<std::uint32_t>(index % 3U == 2U ? 1.0F : 0.0F), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glNormalPointer",
                       {0x1406U, 0U, normals.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1NormalArray}) == 0U);

    const auto diffuse = fixture.output.Add(0x700U);
    for (std::size_t component = 0; component < 4U; ++component) {
        fixture.bus.Write32(diffuse.Add(component * 4U), 0U, 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1201U, diffuse.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0B50U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BE2U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glBlendFunc",
                       {0x0302U, 0x0303U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto frame = fixture.boundary.TakeLatestFrame();
    REQUIRE(frame.has_value());
    for (std::size_t pixel = 0; pixel < frame->rgba8.size(); pixel += 4U) {
        CHECK(frame->rgba8[pixel] == doctest::Approx(64).epsilon(0.04));
        CHECK(frame->rgba8[pixel + 1U] == doctest::Approx(128).epsilon(0.04));
        CHECK(frame->rgba8[pixel + 2U] == doctest::Approx(191).epsilon(0.04));
    }
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 flat shading uses provoking colors for triangles strip and fan") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);
    const auto vertices = fixture.output.Add(0x600U);
    const auto colors = fixture.output.Add(0x700U);
    const auto indices = fixture.output.Add(0x780U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glColorPointer",
                       {4U, 0x1401U, 0U, colors.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1ColorArray}) == 0U);
    constexpr std::array<std::array<std::byte, 4>, 4> vertex_colors{
        std::array{std::byte{0xff}, std::byte{}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{0xff}, std::byte{}, std::byte{0xff}},
        std::array{std::byte{}, std::byte{}, std::byte{0xff}, std::byte{0xff}},
        std::array{std::byte{0xff}, std::byte{0xff}, std::byte{}, std::byte{0xff}}};
    for (std::size_t index = 0; index < vertex_colors.size(); ++index) {
        fixture.memory.Write(colors.Add(index * 4U), vertex_colors[index], 1U);
        fixture.memory.Write(indices.Add(index),
                             std::array{static_cast<std::byte>(index)}, 1U);
    }
    const auto write_vertices = [&](const std::span<const float> values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            fixture.bus.Write32(vertices.Add(index * 4U),
                                std::bit_cast<std::uint32_t>(values[index]), 1U);
        }
    };
    const auto render = [&](const std::uint32_t mode,
                            const std::uint32_t count,
                            const bool elements,
                            const std::uint32_t shade) {
        CHECK(fixture.Call("libGLESv1_CM.so", "glShadeModel", {shade}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor",
                           {0U, 0U, 0U,
                            std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
        if (elements) {
            CHECK(fixture.Call("libGLESv1_CM.so", "glDrawElements",
                               {mode, count, 0x1401U, indices.Value()}) == 0U);
        } else {
            CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                               {mode, 0U, count}) == 0U);
        }
        fixture.boundary.PresentManagedSurface();
        const auto frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(frame.has_value());
        return frame->rgba8;
    };
    const auto verify_difference = [&](const std::vector<std::uint8_t>& smooth,
                                       const std::vector<std::uint8_t>& flat) {
        REQUIRE(smooth.size() == flat.size());
        std::size_t different{};
        for (std::size_t index = 0; index < smooth.size(); index += 4U) {
            if (smooth[index] != flat[index] ||
                smooth[index + 1U] != flat[index + 1U] ||
                smooth[index + 2U] != flat[index + 2U]) {
                ++different;
            }
        }
        CHECK(different >= 2U);
    };

    constexpr std::array triangle{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    write_vertices(triangle);
    const auto triangle_smooth = render(0x0004U, 3U, false, 0x1D01U);
    const auto triangle_flat = render(0x0004U, 3U, false, 0x1D00U);
    verify_difference(triangle_smooth, triangle_flat);
    const auto center = (1U * 4U + 2U) * 4U;
    CHECK(triangle_flat[center] < 20U);
    CHECK(triangle_flat[center + 1U] < 20U);
    CHECK(triangle_flat[center + 2U] > 230U);

    constexpr std::array strip{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
        -1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F};
    write_vertices(strip);
    verify_difference(render(0x0005U, 4U, false, 0x1D01U),
                      render(0x0005U, 4U, false, 0x1D00U));

    constexpr std::array fan{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F,
        1.0F, 1.0F, 0.0F, -1.0F, 1.0F, 0.0F};
    write_vertices(fan);
    verify_difference(render(0x0006U, 4U, true, 0x1D01U),
                      render(0x0006U, 4U, true, 0x1D00U));
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 normalize changes lit pixels after inverse-transpose normal transform") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport", {0U, 0U, 4U, 3U}) == 0U);

    const auto vertices = fixture.output.Add(0x600U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(vertices.Add(index * 4U),
                            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);

    const auto value = fixture.output.Add(0x700U);
    const auto write4 = [&](const std::array<float, 4>& values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            fixture.bus.Write32(value.Add(index * 4U),
                                std::bit_cast<std::uint32_t>(values[index]), 1U);
        }
    };
    write4({0.0F, 0.0F, 0.0F, 1.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightModelfv",
                       {0x0B53U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1200U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1600U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1202U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1200U, value.Value()}) == 0U);
    write4({1.0F, 1.0F, 1.0F, 1.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glMaterialfv",
                       {0x0408U, 0x1201U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1201U, value.Value()}) == 0U);
    write4({0.0F, 0.0F, 1.0F, 0.0F});
    CHECK(fixture.Call("libGLESv1_CM.so", "glLightfv",
                       {0x4000U, 0x1203U, value.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glNormal3f",
                       {0U, 0U, std::bit_cast<std::uint32_t>(0.25F)}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x4000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0B50U}) == 0U);

    const auto render_red = [&]() {
        CHECK(fixture.Call("libGLESv1_CM.so", "glClearColor", {0U, 0U, 0U, 0U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glClear", {0x00004000U}) == 0U);
        CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                           {0x0004U, 0U, 3U}) == 0U);
        fixture.boundary.PresentManagedSurface();
        const auto frame = fixture.boundary.TakeLatestFrame();
        REQUIRE(frame.has_value());
        return frame->rgba8[(1U * 4U + 2U) * 4U];
    };
    const auto unnormalized = render_red();
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    const auto normalized = render_red();
    CHECK(unnormalized < 100U);
    CHECK(normalized > 200U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDisable", {0x0BA1U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glScalef",
              {std::bit_cast<std::uint32_t>(4.0F),
               std::bit_cast<std::uint32_t>(4.0F),
               std::bit_cast<std::uint32_t>(4.0F)}) == 0U);
    const auto scaled_without_rescale = render_red();
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x803AU}) == 0U);
    const auto scaled_with_rescale = render_red();
    CHECK(scaled_without_rescale < unnormalized);
    CHECK(scaled_with_rescale > scaled_without_rescale * 3U);
    CHECK(scaled_with_rescale == doctest::Approx(unnormalized).epsilon(0.12));
    CHECK(fixture.Call("libGLESv1_CM.so", "glDisable", {0x803AU}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x0BA1U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glLoadIdentity") == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glScalef",
              {std::bit_cast<std::uint32_t>(2.0F),
               std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(0.5F)}) == 0U);
    constexpr float inverse_sqrt_two = 0.70710678118F;
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glNormal3f",
              {std::bit_cast<std::uint32_t>(inverse_sqrt_two), 0U,
               std::bit_cast<std::uint32_t>(inverse_sqrt_two)}) == 0U);
    const auto non_uniform = render_red();
    const auto reference_z = 2.0F / std::sqrt(0.25F + 4.0F);
    CHECK(non_uniform == doctest::Approx(reference_z * 255.0F).epsilon(0.04));
    fixture.boundary.CloseManagedSurface();
}

TEST_CASE("GLES1 clip plane discards fixed pipeline fragments") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture fixture;
    fixture.boundary.OpenManagedSurface();
    CHECK(fixture.Call("libGLESv1_CM.so", "glViewport",
                       {0U, 0U, 4U, 3U}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glClearColor",
              {0U, 0U, std::bit_cast<std::uint32_t>(1.0F),
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    const auto vertices = fixture.output.Add(0x600U);
    constexpr std::array vertex_values{
        -1.0F, -1.0F, 0.0F, 1.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F};
    for (std::size_t index = 0; index < vertex_values.size(); ++index) {
        fixture.bus.Write32(
            vertices.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(vertex_values[index]), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glVertexPointer",
                       {3U, 0x1406U, 0U, vertices.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnableClientState",
                       {ogplay::runtime::detail::kGles1VertexArray}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glColor4f",
              {std::bit_cast<std::uint32_t>(1.0F), 0U, 0U,
               std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    CHECK(fixture.Call(
              "libGLESv1_CM.so", "glNormal3f",
              {0U, 0U, std::bit_cast<std::uint32_t>(1.0F)}) == 0U);
    const auto identity = fixture.output.Add(0x700U);
    for (std::size_t index = 0; index < 16U; ++index) {
        fixture.bus.Write32(
            identity.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(index % 5U == 0U ? 1.0F : 0.0F),
            1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glMultMatrixf",
                       {identity.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                       {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto visible = fixture.boundary.TakeLatestFrame();
    REQUIRE(visible.has_value());
    bool saw_red{};
    for (std::size_t pixel = 0; pixel < visible->rgba8.size(); pixel += 4U) {
        saw_red = saw_red || (visible->rgba8[pixel] > 200U &&
                              visible->rgba8[pixel + 2U] < 50U);
    }
    CHECK(saw_red);

    const auto equation = fixture.output.Add(0x780U);
    for (std::size_t index = 0; index < 4U; ++index) {
        fixture.bus.Write32(
            equation.Add(index * sizeof(std::uint32_t)),
            std::bit_cast<std::uint32_t>(index == 3U ? -1.0F : 0.0F), 1U);
    }
    CHECK(fixture.Call("libGLESv1_CM.so", "glClipPlanef",
                       {0x3000U, equation.Value()}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glEnable", {0x3000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glClear",
                       {0x00004000U}) == 0U);
    CHECK(fixture.Call("libGLESv1_CM.so", "glDrawArrays",
                       {0x0004U, 0U, 3U}) == 0U);
    fixture.boundary.PresentManagedSurface();
    const auto clipped = fixture.boundary.TakeLatestFrame();
    REQUIRE(clipped.has_value());
    for (std::size_t pixel = 0; pixel < clipped->rgba8.size(); pixel += 4U) {
        CHECK(clipped->rgba8[pixel] == 0U);
        CHECK(clipped->rgba8[pixel + 1U] == 0U);
        CHECK(clipped->rgba8[pixel + 2U] == 255U);
        CHECK(clipped->rgba8[pixel + 3U] == 255U);
    }
    fixture.boundary.CloseManagedSurface();
}
