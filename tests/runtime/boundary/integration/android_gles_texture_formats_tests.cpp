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

TEST_CASE("BND50 ATC queries agree across APIs and reject invalid updates") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so","eglInitialize",{1})==1U);
    const auto context = AuditContext(f,2), surface = AuditSurface(f);
    AuditBind(f,context,surface);
    for (const auto library : {"libGLESv2.so","libGLESv1_CM.so"}) {
        CAPTURE(library);
        const auto gl = [&](const std::string_view name,
                            const std::initializer_list<std::uint32_t> args = {}) {
            const auto entry = f.boundary.Symbols().Lookup(library, name);
            REQUIRE(entry.has_value());
            return BoundaryCallAddress(f,entry->Value(),{args.begin(),args.size()});
        };
        const auto string = gl("glGetString",{0x1f03});
        const auto length = f.memory.CStringLength(ogplay::memory::GuestAddress{string},4096,1);
        std::string text(length,'\0');
        f.memory.Read(ogplay::memory::GuestAddress{string},std::as_writable_bytes(std::span(text)),1);
        CHECK(text.find("GL_AMD_compressed_ATC_texture")!=std::string::npos);
        gl("glGetIntegerv",{0x86a2,f.output.Value()});
        const auto count = f.bus.Read32(f.output,1);
        REQUIRE(count>0);REQUIRE(count<256);
        const auto output = f.output.Add(64), sentinel = output.Add(count*4);
        f.bus.Write32(sentinel,0xdeadbeef,1);
        gl("glGetIntegerv",{0x86a3,output.Value()});
        std::vector<std::uint32_t> formats;
        for(std::uint32_t i=0;i<count;++i) formats.push_back(f.bus.Read32(output.Add(i*4),1));
        for(const auto format : {0x8c92U,0x8c93U,0x87eeU}) CHECK(std::count(formats.begin(),formats.end(),format)==1);
        CHECK(f.bus.Read32(sentinel,1)==0xdeadbeef);
        gl("glGetFloatv",{0x86a3,output.Value()});
        for(std::uint32_t i=0;i<count;++i) CHECK(std::bit_cast<float>(f.bus.Read32(output.Add(i*4),1))==static_cast<float>(formats[i]));
        CHECK(f.bus.Read32(sentinel,1)==0xdeadbeef);
        gl("glGetBooleanv",{0x86a3,output.Value()});
        for(std::uint32_t i=0;i<count;++i) CHECK(f.bus.Read8(output.Add(i),1)==1U);
        CHECK(f.bus.Read32(sentinel,1)==0xdeadbeef);
        // Full array must be preflighted before any output is written.
        const auto end = f.output.Add(f.memory.PageSize()-4);
        f.bus.Write32(end,0xabcdef,1);
        CHECK_THROWS_AS(gl("glGetIntegerv",{0x86a3,end.Value()}),ogplay::memory::MemoryFault);
        CHECK(f.bus.Read32(end,1)==0xabcdef);
        gl("glGenTextures",{1,f.output.Value()});
        gl("glBindTexture",{0x0de1,f.bus.Read32(f.output,1)});
        const auto data = f.output.Add(16);
        const std::array<std::byte,16> bytes{};f.memory.Write(data,bytes,1);
        CHECK(gl("glCompressedTexImage2D",{0x0de1,0,0x8c93,4,4,0,16,data.Value()})==0U);
        CHECK(gl("glGetError")==0U);
        gl("glTexSubImage2D",{0x0de1,0,0,0,4,4,0x1908,0x1401,0});
        CHECK(gl("glGetError")==0x0502U);
        gl("glCopyTexSubImage2D",{0x0de1,0,0,0,0,0,4,4});
        CHECK(gl("glGetError")==0x0502U);
        gl("glCompressedTexSubImage2D",{0x0de1,0,0,0,4,4,0x8c93,16,data.Value()});
        CHECK(gl("glGetError")==0x0502U);
        gl("glCompressedTexImage2D",{0x0de1,0,0x8c93,4,4,0,15,data.Value()});
        CHECK(gl("glGetError")==0x0501U);
    }
}

TEST_CASE("Compressed image NULL data allocates the level for a later upload") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;

    struct Format final {
        std::uint32_t value, block_bytes;
        bool sub_image;
    };

    for (const auto version : {1U, 2U}) {
        BoundaryFixture f;
        REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
        const auto context = AuditContext(f, version), surface = AuditSurface(f);
        AuditBind(f, context, surface);
        const std::string_view library = version == 1U ? "libGLESv1_CM.so" : "libGLESv2.so";
        CAPTURE(library);
        const auto gl = [&](const std::string_view name, const std::initializer_list<std::uint32_t> args = {}) {
            const auto entry = f.boundary.Symbols().Lookup(library, name);
            REQUIRE(entry.has_value());
            return BoundaryCallAddress(f, entry->Value(), {args.begin(), args.size()});
        };
        const auto data = f.output.Add(64);
        const std::array<std::byte, 128> bytes{};
        f.memory.Write(data, bytes, 1);
        // PVRTC RGBA 4bpp, ETC1 and ATC explicit alpha cover native and decoded uploads.
        for (const auto& format : {Format{0x8C02U, 8U, true}, Format{0x8D64U, 8U, true}, Format{0x8C93U, 16U, false}}) {
            CAPTURE(format.value);
            gl("glGenTextures", {1, f.output.Value()});
            gl("glBindTexture", {0x0DE1, f.bus.Read32(f.output, 1)});
            const auto base = 4U * format.block_bytes;
            // PVRTC1 stores at least 2x2 blocks per level.
            const auto mip = format.value == 0x8C02U ? base : format.block_bytes;
            CHECK(gl("glCompressedTexImage2D", {0x0DE1, 0, format.value, 8, 8, 0, base, 0}) == 0U);
            CHECK(gl("glCompressedTexImage2D", {0x0DE1, 1, format.value, 4, 4, 0, mip, 0}) == 0U);
            CHECK(gl("glGetError") == 0U);
            if (format.sub_image) {
                CHECK(gl("glCompressedTexSubImage2D", {0x0DE1, 0, 0, 0, 8, 8, format.value, base, data.Value()}) == 0U);
                CHECK(gl("glGetError") == 0U);
                CHECK_THROWS_AS(gl("glCompressedTexSubImage2D", {0x0DE1, 0, 0, 0, 8, 8, format.value, base, 0}),
                                ogplay::gles::GuestTransferError);
            }
            CHECK(gl("glCompressedTexImage2D", {0x0DE1, 0, format.value, 8, 8, 0, base, data.Value()}) == 0U);
            CHECK(gl("glGetError") == 0U);
        }
        // NULL skips the guest copy only: imageSize and format checks still apply.
        gl("glCompressedTexImage2D", {0x0DE1, 0, 0x8C93, 8, 8, 0, 63, 0});
        CHECK(gl("glGetError") == 0x0501U);
        gl("glCompressedTexImage2D", {0x0DE1, 0, 0xFFFF, 8, 8, 0, 64, 0});
        CHECK(gl("glGetError") == 0x0500U);
        CHECK_THROWS_AS(gl("glCompressedTexImage2D", {0x0DE1, 0, 0x8C93, 8, 8, 0, 0x7FFFFFFFU, 0}),
                        ogplay::gles::GuestTransferError);
    }
}

TEST_CASE("BND50 ATC PBO upload retains shared texture format metadata") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so","eglInitialize",{1})==1U);
    const auto context = AuditContext(f), surface = AuditSurface(f);
    AuditBind(f,context,surface);
    const auto texture = AuditName(f,"glGenTextures");
    AuditGl(f,"glBindTexture",{0x0de1,texture});
    const auto buffer = AuditName(f,"glGenBuffers");
    AuditGl(f,"glBindBuffer",{0x88ec,buffer});
    const std::array<std::byte,20> block{};f.memory.Write(f.output,block,1);
    AuditGl(f,"glBufferData",{0x88ec,20,f.output.Value(),0x88e4});
    AuditGl(f,"glCompressedTexImage2D",{0x0de1,0,0x8c93,4,4,0,16,4});
    CHECK(AuditGl(f,"glGetError")==0U);
    AuditGl(f,"glGetIntegerv",{0x88ef,f.output.Value()});
    CHECK(f.bus.Read32(f.output,1)==buffer);
    AuditGl(f,"glTexSubImage2D",{0x0de1,0,0,0,4,4,0x1908,0x1401,4});
    CHECK(AuditGl(f,"glGetError")==0x0502U);
    AuditGl(f,"glBindBuffer",{0x88ec,0});
    // Replacing the level-zero image with ordinary RGBA removes the ATC restriction.
    AuditGl(f,"glTexImage2D",{0x0de1,0,0x1908,4,4,0,0x1908,0x1401,0});
    CHECK(AuditGl(f,"glGetError")==0U);
    const std::array<std::byte,64> rgba{};f.memory.Write(f.output,rgba,1);
    AuditGl(f,"glTexSubImage2D",{0x0de1,0,0,0,4,4,0x1908,0x1401,f.output.Value()});
    CHECK(AuditGl(f,"glGetError")==0U);
}

TEST_CASE("BND51 float extensions require exact native tokens and linear dependency") {
    const auto check = [](const std::string_view native, const bool upload, const bool linear) {
        const auto values = ogplay::runtime::GuestGlesExtensions(native);
        CHECK((std::count(values.begin(), values.end(), "GL_OES_texture_float") == 1) == upload);
        CHECK((std::count(values.begin(), values.end(), "GL_OES_texture_float_linear") == 1) == linear);
        CHECK(std::count(values.begin(), values.end(), "GL_EXT_color_buffer_float") == 0);
        CHECK(std::count(values.begin(), values.end(), "GL_OES_texture_half_float") == 0);
    };
    check("", false, false);
    check("GL_OES_texture_float_linear", false, false);
    check("GL_OES_texture_float", true, false);
    check("GL_OES_texture_float GL_OES_texture_float_linear", true, true);
    check("GL_OES_texture_float_extra xGL_OES_texture_float GL_OES_texture_float_linear_extra", false, false);
    check("\tGL_OES_texture_float_linear\nGL_EXT_color_buffer_float GL_OES_texture_half_float\rGL_OES_texture_float GL_OES_texture_float ", true, true);
}

TEST_CASE("BND51 float extension string indexed queries and counts share context facts") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f);
    const auto read = [&](const std::uint32_t pointer) {
        const ogplay::memory::GuestAddress address{pointer};
        REQUIRE(pointer != 0U);
        std::string value(f.memory.CStringLength(address, 4096, 1), '\0');
        f.memory.Read(address, std::as_writable_bytes(std::span(value)), 1);
        return value;
    };
    for (const auto version : {2U, 3U}) {
        CAPTURE(version);
        const auto context = AuditContext(f, version);
        AuditBind(f, context, surface);
        const auto extensions = read(AuditGl(f, "glGetString", {0x1F03}));
        std::vector<std::string> tokens;
        std::size_t start = 0;
        while (start < extensions.size()) {
            const auto end = extensions.find(' ', start);
            const auto token = extensions.substr(start, end - start);
            if (!token.empty()) tokens.push_back(token);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (version == 2U) CHECK(std::count(tokens.begin(), tokens.end(), "GL_OES_depth_texture") == 1);
        const auto count = AuditInteger(f, 0x821D);
        REQUIRE(count == tokens.size());
        if (version >= 3U) {
            for (std::uint32_t i = 0; i < count; ++i)
                CHECK(read(AuditGl(f, "glGetStringi", {0x1F03, i})) == tokens[i]);
            AuditGl(f, "glGetInteger64v", {0x821D, f.output.Value()});
            CHECK(f.bus.Read32(f.output, 1) == count);
            CHECK(f.bus.Read32(f.output.Add(4), 1) == 0U);
        } else {
            CHECK(AuditGl(f, "glGetStringi", {0x1F03, 0}) == 0U);
            CHECK(AuditGl(f, "glGetError") == 0x0502U);
        }
        AuditGl(f, "glGetFloatv", {0x821D, f.output.Value()});
        CHECK(std::bit_cast<float>(f.bus.Read32(f.output, 1)) == static_cast<float>(count));
        AuditGl(f, "glGetBooleanv", {0x821D, f.output.Value()});
        CHECK(f.bus.Read8(f.output, 1) == 1U);
        if (version >= 3U) {
            CHECK(AuditGl(f, "glGetStringi", {0x1F03, count}) == 0U);
            CHECK(AuditGl(f, "glGetError") == 0x0501U);
        }
        CHECK(AuditGl(f, "glGetError") == 0U);
        REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
        REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, context}) == 1U);
    }
}

TEST_CASE("BND51 guest float 2D and six cube faces upload update and sample") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 2);
    AuditBind(f, context, surface);
    const auto pointer = AuditGl(f, "glGetString", {0x1F03});
    const ogplay::memory::GuestAddress address{pointer};
    std::string extensions(f.memory.CStringLength(address, 4096, 1), '\0');
    f.memory.Read(address, std::as_writable_bytes(std::span(extensions)), 1);
    REQUIRE((" " + extensions + " ").find(" GL_OES_texture_float ") != std::string::npos);
    const bool linear = extensions.find("GL_OES_texture_float_linear") != std::string::npos;
    // Four distinct floating texels expose both float stride and interpolation.
    const std::array pixels{0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 1.0F,
                            0.0F, 1.0F, 1.0F, 1.0F, 1.0F, 1.0F, 0.0F, 1.0F};
    const auto data = f.output.Add(1024), vertices = f.output.Add(1200);
    f.memory.Write(data, std::as_bytes(std::span(pixels)), 1);
    const std::array triangle{-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
    f.memory.Write(vertices, std::as_bytes(std::span(triangle)), 1);
    const auto compile = [&](const std::uint32_t type, const std::string_view source) {
        const auto shader = AuditGl(f, "glCreateShader", {type});
        WriteGuestString(f, f.output.Add(512), source);
        f.bus.Write32(f.output.Add(256), f.output.Add(512).Value(), 1);
        AuditGl(f, "glShaderSource", {shader, 1, f.output.Add(256).Value(), 0});
        AuditGl(f, "glCompileShader", {shader});
        AuditGl(f, "glGetShaderiv", {shader, 0x8B81, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return shader;
    };
    for (const auto target : {0x0DE1U, 0x8513U}) {
        CAPTURE(target);
        const auto texture = AuditName(f, "glGenTextures");
        AuditGl(f, "glBindTexture", {target, texture});
        AuditGl(f, "glPixelStorei", {0x0CF5, 8});
        f.memory.Write(data, std::as_bytes(std::span(pixels)), 1);
        for (std::uint32_t i = 0; i < (target == 0x8513U ? 6U : 1U); ++i) {
            const auto face = target == 0x8513U ? 0x8515U + i : target;
            AuditGl(f, "glTexImage2D", {face, 0, 0x1908, 2, 2, 0, 0x1908, 0x1406, 0});
            CHECK(AuditGl(f, "glGetError") == 0U);
            AuditGl(f, "glTexSubImage2D", {face, 0, 0, 0, 2, 2, 0x1908, 0x1406, data.Value()});
            CHECK(AuditGl(f, "glGetError") == 0U);
        }
        AuditGl(f, "glTexParameteri", {target, 0x2802, 0x812F});
        AuditGl(f, "glTexParameteri", {target, 0x2803, 0x812F});
        const auto vertex = compile(0x8B31, "attribute vec2 p;void main(){gl_Position=vec4(p,0.0,1.0);}");
        const auto fragment = compile(0x8B30, target == 0x8513U
            ? "precision highp float;uniform samplerCube t;uniform vec3 uv;uniform float decode;void main(){vec4 c=textureCube(t,uv);gl_FragColor=mix(c,vec4(c.r*.25,(c.g+2.0)*.25,c.b*4.0,1.0),decode);}"
            : "precision highp float;uniform sampler2D t;uniform vec3 uv;uniform float decode;void main(){vec4 c=texture2D(t,uv.xy);gl_FragColor=mix(c,vec4(c.r*.25,(c.g+2.0)*.25,c.b*4.0,1.0),decode);}");
        const auto program = AuditGl(f, "glCreateProgram");
        AuditGl(f, "glAttachShader", {program, vertex});
        AuditGl(f, "glAttachShader", {program, fragment});
        AuditGl(f, "glLinkProgram", {program});
        AuditGl(f, "glGetProgramiv", {program, 0x8B82, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        AuditGl(f, "glUseProgram", {program});
        WriteGuestString(f, f.output.Add(256), "p");
        const auto attribute = AuditGl(f, "glGetAttribLocation", {program, f.output.Add(256).Value()});
        REQUIRE(attribute != UINT32_MAX);
        AuditGl(f, "glVertexAttribPointer", {attribute, 2, 0x1406, 0, 0, vertices.Value()});
        AuditGl(f, "glEnableVertexAttribArray", {attribute});
        WriteGuestString(f, f.output.Add(256), "uv");
        const auto uv = AuditGl(f, "glGetUniformLocation", {program, f.output.Add(256).Value()});
        REQUIRE(uv != UINT32_MAX);
        const auto word = [](const float value) { return std::bit_cast<std::uint32_t>(value); };
        for (const auto filter : {0x2600U, 0x2601U}) {
            if (filter == 0x2601U && !linear) continue;
            AuditGl(f, "glTexParameteri", {target, 0x2800, filter});
            AuditGl(f, "glTexParameteri", {target, 0x2801, filter});
            const bool nearest = filter == 0x2600U;
            AuditGl(f, "glUniform3f", {uv, word(target == 0x8513U ? 1.0F : (nearest ? .25F : .5F)),
                word(target == 0x8513U ? (nearest ? .5F : 0.0F) : (nearest ? .25F : .5F)),
                word(target == 0x8513U && nearest ? .5F : 0.0F)});
            AuditGl(f, "glDrawArrays", {4, 0, 3});
            const auto pixel = AuditPixel(f);
            for (const auto shift : {0U, 8U, 16U})
                CHECK(static_cast<int>((pixel >> shift) & 255U) == doctest::Approx(nearest ? 0 : 128).epsilon(.02));
            CHECK((pixel >> 24U) == 255U);
        }
        // Replacing from guest memory still uses the original float layout.
        const auto face = target == 0x8513U ? 0x8515U : target;
        AuditGl(f, "glTexImage2D", {face, 0, 0x1908, 2, 2, 0, 0x1908, 0x1406, data.Value()});
        CHECK(AuditGl(f, "glGetError") == 0U);
        // Values outside [0,1] must survive upload; RGBA8 conversion would fail this draw.
        auto wide_pixels = pixels;
        for (std::size_t i = 0; i < wide_pixels.size(); i += 4) {
            wide_pixels[i] = 2.0F; wide_pixels[i + 1] = -1.0F;
            wide_pixels[i + 2] = .123456F; wide_pixels[i + 3] = 1.0F;
        }
        f.memory.Write(data, std::as_bytes(std::span(wide_pixels)), 1);
        AuditGl(f, "glTexSubImage2D", {face, 0, 0, 0, 2, 2, 0x1908, 0x1406, data.Value()});
        WriteGuestString(f, f.output.Add(256), "decode");
        const auto decode = AuditGl(f, "glGetUniformLocation", {program, f.output.Add(256).Value()});
        REQUIRE(decode != UINT32_MAX);
        AuditGl(f, "glUniform1f", {decode, word(1)});
        AuditGl(f, "glDrawArrays", {4, 0, 3});
        const auto wide_pixel = AuditPixel(f);
        CHECK((wide_pixel & 255U) == doctest::Approx(128).epsilon(.02));
        CHECK(((wide_pixel >> 8U) & 255U) == doctest::Approx(64).epsilon(.02));
        CHECK(((wide_pixel >> 16U) & 255U) == doctest::Approx(126).epsilon(.02));
        const auto end = f.output.Add(f.memory.PageSize() - 16);
        CHECK_THROWS_AS(AuditGl(f, "glTexImage2D", {face, 0, 0x1908, 2, 2, 0, 0x1908, 0x1406, end.Value()}), ogplay::memory::MemoryFault);
        CHECK(AuditGl(f, "glGetError") == 0U);
        AuditGl(f, "glDisableVertexAttribArray", {attribute});
        AuditGl(f, "glUseProgram", {0});
        AuditGl(f, "glDeleteProgram", {program});
        AuditGl(f, "glDeleteShader", {vertex});
        AuditGl(f, "glDeleteShader", {fragment});
        f.bus.Write32(f.output, texture, 1);
        AuditGl(f, "glDeleteTextures", {1, f.output.Value()});
    }
}

TEST_CASE("BND51 float PBO uploads keep byte offsets bounded") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f);
    AuditBind(f, context, surface);
    const auto texture = AuditName(f, "glGenTextures");
    AuditGl(f, "glBindTexture", {0x0DE1, texture});
    const auto buffer = AuditName(f, "glGenBuffers");
    AuditGl(f, "glBindBuffer", {0x88EC, buffer});
    const std::array pixels{.25F, .5F, .75F, 1.0F};
    f.memory.Write(f.output.Add(4), std::as_bytes(std::span(pixels)), 1);
    AuditGl(f, "glBufferData", {0x88EC, 20, f.output.Value(), 0x88E4});
    AuditGl(f, "glTexImage2D", {0x0DE1, 0, 0x8814, 1, 1, 0, 0x1908, 0x1406, 4});
    CHECK(AuditGl(f, "glGetError") == 0U);
    AuditGl(f, "glTexSubImage2D", {0x0DE1, 0, 0, 0, 1, 1, 0x1908, 0x1406, 8});
    CHECK(AuditGl(f, "glGetError") == 0x0502U);
    AuditGl(f, "glTexSubImage2D", {0x0DE1, 0, 0, 0, 1, 1, 0x1908, 0x1406, 4});
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND52 packed depth stencil publication requires exact native token") {
    for (const auto native : {"", "GL_OES_packed_depth_stencil_extra", "xGL_OES_packed_depth_stencil"}) {
        const auto values = ogplay::runtime::GuestGlesExtensions(native);
        CHECK(std::count(values.begin(), values.end(), "GL_OES_packed_depth_stencil") == 0);
    }
    const auto values = ogplay::runtime::GuestGlesExtensions(
        "\tGL_OES_packed_depth_stencil\nGL_OES_packed_depth_stencil\r");
    CHECK(std::count(values.begin(), values.end(), "GL_OES_packed_depth_stencil") == 1);
}

TEST_CASE("BND52 packed framebuffer supports depth stencil and indexed pixel draw") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 2);
    AuditBind(f, context, surface);
    const ogplay::memory::GuestAddress address{AuditGl(f, "glGetString", {0x1F03})};
    std::string extensions(f.memory.CStringLength(address, 4096, 1), '\0');
    f.memory.Read(address, std::as_writable_bytes(std::span(extensions)), 1);
    REQUIRE((" " + extensions + " ").find(" GL_OES_packed_depth_stencil ") != std::string::npos);
    const auto framebuffer = AuditName(f, "glGenFramebuffers");
    const auto color = AuditName(f, "glGenRenderbuffers"), packed = AuditName(f, "glGenRenderbuffers");
    AuditGl(f, "glBindFramebuffer", {0x8D40, framebuffer});
    AuditGl(f, "glBindRenderbuffer", {0x8D41, color});
    AuditGl(f, "glRenderbufferStorage", {0x8D41, 0x8058, 4, 4});
    AuditGl(f, "glFramebufferRenderbuffer", {0x8D40, 0x8CE0, 0x8D41, color});
    AuditGl(f, "glBindRenderbuffer", {0x8D41, packed});
    AuditGl(f, "glRenderbufferStorage", {0x8D41, 0x88F0, 4, 4});
    for (const auto attachment : {0x8D00U, 0x8D20U}) {
        AuditGl(f, "glFramebufferRenderbuffer", {0x8D40, attachment, 0x8D41, packed});
        AuditGl(f, "glGetFramebufferAttachmentParameteriv", {0x8D40, attachment, 0x8CD1, f.output.Value()});
        CHECK(f.bus.Read32(f.output, 1) == packed);
    }
    REQUIRE(AuditGl(f, "glCheckFramebufferStatus", {0x8D40}) == 0x8CD5U);
    const auto compile = [&](const std::uint32_t type, const std::string_view source) {
        const auto shader = AuditGl(f, "glCreateShader", {type});
        WriteGuestString(f, f.output.Add(512), source);
        f.bus.Write32(f.output.Add(256), f.output.Add(512).Value(), 1);
        AuditGl(f, "glShaderSource", {shader, 1, f.output.Add(256).Value(), 0});
        AuditGl(f, "glCompileShader", {shader});
        AuditGl(f, "glGetShaderiv", {shader, 0x8B81, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return shader;
    };
    const auto vertex = compile(0x8B31, "attribute vec2 p; void main(){gl_Position=vec4(p,0.0,1.0);}");
    const auto fragment = compile(0x8B30, "precision mediump float; void main(){gl_FragColor=vec4(1.0,0.0,0.0,1.0);}");
    const auto program = AuditGl(f, "glCreateProgram");
    AuditGl(f, "glAttachShader", {program, vertex}); AuditGl(f, "glAttachShader", {program, fragment});
    AuditGl(f, "glLinkProgram", {program});
    AuditGl(f, "glGetProgramiv", {program, 0x8B82, f.output.Value()});
    REQUIRE(f.bus.Read32(f.output, 1) == 1U);
    AuditGl(f, "glUseProgram", {program});
    WriteGuestString(f, f.output.Add(256), "p");
    const auto attribute = AuditGl(f, "glGetAttribLocation", {program, f.output.Add(256).Value()});
    const std::array triangle{-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
    const std::array<std::uint16_t, 3> indices{0, 1, 2};
    f.memory.Write(f.output.Add(1024), std::as_bytes(std::span(triangle)), 1);
    f.memory.Write(f.output.Add(1200), std::as_bytes(std::span(indices)), 1);
    AuditGl(f, "glVertexAttribPointer", {attribute, 2, 0x1406, 0, 0, f.output.Add(1024).Value()});
    AuditGl(f, "glEnableVertexAttribArray", {attribute});
    AuditGl(f, "glViewport", {0, 0, 4, 4});
    AuditGl(f, "glEnable", {0x0B71}); AuditGl(f, "glEnable", {0x0B90});
    AuditGl(f, "glClearColor", {0, 0, 0, 0x3F800000});
    AuditGl(f, "glClear", {0x4500});
    AuditGl(f, "glDrawElements", {4, 3, 0x1403, f.output.Add(1200).Value()});
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    AuditGl(f, "glClear", {0x4000});
    AuditGl(f, "glDepthFunc", {0x0200}); // NEVER must reject the draw.
    AuditGl(f, "glDrawElements", {4, 3, 0x1403, f.output.Add(1200).Value()});
    CHECK(AuditPixel(f) == 0xFF000000U);
    AuditGl(f, "glDepthFunc", {0x0207});
    AuditGl(f, "glStencilFunc", {0x0200, 0, 0xFF});
    AuditGl(f, "glDrawElements", {4, 3, 0x1403, f.output.Add(1200).Value()});
    CHECK(AuditPixel(f) == 0xFF000000U);
    AuditGl(f, "glStencilFunc", {0x0202, 0, 0xFF}); // EQUAL matches the cleared stencil.
    AuditGl(f, "glDrawElements", {4, 3, 0x1403, f.output.Add(1200).Value()});
    CHECK(AuditPixel(f) == 0xFF0000FFU);
    CHECK(f.boundary.Stats().gl_errors == 0U);
}

TEST_CASE("Depth texture publication requires an exact native extension token") {
    constexpr std::string_view extension = "GL_OES_depth_texture";
    for (const auto native : {"", "GL_OES_depth_texture_extra", "xGL_OES_depth_texture", "GL_ANGLE_depth_texture",
                              "GL_OES_packed_depth_stencil"}) {
        CAPTURE(native);
        const auto values = ogplay::runtime::GuestGlesExtensions(native);
        CHECK(std::count(values.begin(), values.end(), extension) == 0);
    }
    const auto values = ogplay::runtime::GuestGlesExtensions(
        "\tGL_OES_depth_texture\nGL_OES_depth_texture\rGL_OES_packed_depth_stencil");
    CHECK(std::count(values.begin(), values.end(), extension) == 1);
}

TEST_CASE("ES2 depth textures store drawn depth and sample it in a separate framebuffer") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 2);
    AuditBind(f, context, surface);
    const auto pointer = AuditGl(f, "glGetString", {0x1F03});
    REQUIRE(pointer != 0U);
    const ogplay::memory::GuestAddress address{pointer};
    std::string extensions(f.memory.CStringLength(address, 4096, 1), '\0');
    f.memory.Read(address, std::as_writable_bytes(std::span(extensions)), 1);
    REQUIRE((" " + extensions + " ").find(" GL_OES_depth_texture ") != std::string::npos);

    const auto name = f.output.Add(256), source_text = f.output.Add(512), vertices = f.output.Add(1024);
    const auto compile = [&](const std::uint32_t type, const std::string_view source) {
        const auto shader = AuditGl(f, "glCreateShader", {type});
        WriteGuestString(f, source_text, source);
        f.bus.Write32(name, source_text.Value(), 1);
        AuditGl(f, "glShaderSource", {shader, 1, name.Value(), 0});
        AuditGl(f, "glCompileShader", {shader});
        AuditGl(f, "glGetShaderiv", {shader, 0x8B81, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return shader;
    };
    const auto vertex = compile(0x8B31, "attribute vec2 p; uniform float z; void main(){gl_Position=vec4(p,z,1.0);}");
    const auto link = [&](const std::string_view fragment_source) {
        const auto fragment = compile(0x8B30, fragment_source);
        const auto program = AuditGl(f, "glCreateProgram");
        AuditGl(f, "glAttachShader", {program, vertex});
        AuditGl(f, "glAttachShader", {program, fragment});
        WriteGuestString(f, name, "p");
        AuditGl(f, "glBindAttribLocation", {program, 0, name.Value()});
        AuditGl(f, "glLinkProgram", {program});
        AuditGl(f, "glGetProgramiv", {program, 0x8B82, f.output.Value()});
        REQUIRE(f.bus.Read32(f.output, 1) == 1U);
        return program;
    };
    const auto depth_program = link("precision mediump float; void main(){gl_FragColor=vec4(1.0);}");
    const auto sample_program = link("precision mediump float; uniform sampler2D t; "
                                     "void main(){gl_FragColor=vec4(texture2D(t,vec2(0.5)).r,0.0,0.0,1.0);}");
    WriteGuestString(f, name, "z");
    const auto z = AuditGl(f, "glGetUniformLocation", {depth_program, name.Value()});
    REQUIRE(z != UINT32_MAX);
    const std::array triangle{-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
    f.memory.Write(vertices, std::as_bytes(std::span(triangle)), 1);
    AuditGl(f, "glVertexAttribPointer", {0, 2, 0x1406, 0, 0, vertices.Value()});
    AuditGl(f, "glEnableVertexAttribArray", {0});

    constexpr std::uint32_t texture_2d = 0x0DE1, framebuffer_target = 0x8D40, renderbuffer_target = 0x8D41;
    constexpr std::uint32_t depth_component = 0x1902, depth_stencil = 0x84F9;

    struct DepthFormat {
        const char* name;
        std::uint32_t format, type, width, height;
    };

    const std::array formats{
        DepthFormat{"DEPTH_COMPONENT/UNSIGNED_SHORT", depth_component, 0x1403, 4, 4},
        DepthFormat{"DEPTH_COMPONENT/UNSIGNED_INT", depth_component, 0x1405, 4, 4},
        DepthFormat{"DEPTH_STENCIL/UNSIGNED_INT_24_8", depth_stencil, 0x84FA, 5, 3},
    };

    struct DepthSample {
        float ndc_z;
        int red;
    };

    const std::array samples{DepthSample{0.0F, 128}, DepthSample{-0.5F, 64}};
    for (const auto& format : formats) {
        CAPTURE(format.name);
        const auto texture = AuditName(f, "glGenTextures");
        AuditGl(f, "glBindTexture", {texture_2d, texture});
        AuditGl(f, "glTexImage2D",
                {texture_2d, 0, format.format, format.width, format.height, 0, format.format, format.type, 0});
        AuditGl(f, "glTexParameteri", {texture_2d, 0x2800, 0x2600});  // MAG_FILTER = NEAREST
        AuditGl(f, "glTexParameteri", {texture_2d, 0x2801, 0x2600});  // MIN_FILTER = NEAREST
        AuditGl(f, "glTexParameteri", {texture_2d, 0x2802, 0x812F});  // WRAP_S = CLAMP_TO_EDGE
        AuditGl(f, "glTexParameteri", {texture_2d, 0x2803, 0x812F});  // WRAP_T = CLAMP_TO_EDGE
        REQUIRE(AuditGl(f, "glGetError") == 0U);

        const auto framebuffer = AuditName(f, "glGenFramebuffers");
        const auto color = AuditName(f, "glGenRenderbuffers");
        AuditGl(f, "glBindFramebuffer", {framebuffer_target, framebuffer});
        AuditGl(f, "glBindRenderbuffer", {renderbuffer_target, color});
        AuditGl(f, "glRenderbufferStorage", {renderbuffer_target, 0x8058, format.width, format.height});  // RGBA8
        AuditGl(f, "glFramebufferRenderbuffer", {framebuffer_target, 0x8CE0, renderbuffer_target, color});
        AuditGl(f, "glFramebufferTexture2D", {framebuffer_target, 0x8D00, texture_2d, texture, 0});  // DEPTH_ATTACHMENT
        if (format.format == depth_stencil)
            AuditGl(f, "glFramebufferTexture2D",
                    {framebuffer_target, 0x8D20, texture_2d, texture, 0});  // STENCIL_ATTACHMENT
        REQUIRE(AuditGl(f, "glCheckFramebufferStatus", {framebuffer_target}) == 0x8CD5U);

        for (const auto& sample : samples) {
            AuditGl(f, "glBindTexture", {texture_2d, 0});
            AuditGl(f, "glBindFramebuffer", {framebuffer_target, framebuffer});
            AuditGl(f, "glViewport", {0, 0, format.width, format.height});
            AuditGl(f, "glEnable", {0x0B71});  // DEPTH_TEST
            AuditGl(f, "glClear", {0x0100});   // DEPTH_BUFFER_BIT
            AuditGl(f, "glUseProgram", {depth_program});
            AuditGl(f, "glUniform1f", {z, std::bit_cast<std::uint32_t>(sample.ndc_z)});
            AuditGl(f, "glDrawArrays", {4, 0, 3});

            // Sample on the pbuffer so the texture is never an attachment of the draw framebuffer.
            AuditGl(f, "glBindFramebuffer", {framebuffer_target, 0});
            AuditGl(f, "glViewport", {0, 0, 4, 4});
            AuditGl(f, "glDisable", {0x0B71});
            AuditGl(f, "glClear", {0x4000});  // COLOR_BUFFER_BIT
            AuditGl(f, "glUseProgram", {sample_program});
            AuditGl(f, "glBindTexture", {texture_2d, texture});
            AuditGl(f, "glDrawArrays", {4, 0, 3});
            const auto red = static_cast<int>(AuditPixel(f) & 0xFFU);
            MESSAGE(format.name << ", NDC z=" << sample.ndc_z << ", red=" << red);
            CHECK(std::abs(red - sample.red) <= 2);
        }
    }
    CHECK(AuditGl(f, "glGetError") == 0U);
    CHECK(f.boundary.Stats().gl_errors == 0U);
}

TEST_CASE("BND52 call errors remain attributable across slow fast managed and proc dispatch") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    BoundaryFixture f;
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
    const auto surface = AuditSurface(f), context = AuditContext(f, 2);
    AuditBind(f, context, surface);
    const auto empty = AuditName(f, "glGenFramebuffers");
    AuditGl(f, "glBindFramebuffer", {0x8D40, empty});
    const auto before = f.boundary.Stats().gl_errors;
    AuditGl(f, "glClear", {0x4000});
    FastBoundaryCall(f, "libGLESv2.so", "glClear", {0x4000});
    const std::array<std::uint32_t, 1> clear{0x4000};
    static_cast<void>(f.boundary.InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glClear", clear, 1));
    WriteGuestString(f, f.output.Add(256), "glClear");
    const auto proc = f.Call("libEGL.so", "eglGetProcAddress", {f.output.Add(256).Value()});
    REQUIRE(proc != 0U);
    BoundaryCallAddress(f, proc, clear);
    std::array<std::uint32_t, 16> registers{};
    registers[0] = 0x4000U; registers[13] = f.stack.Value();
    ogplay::cpu::A32HostCallContext fast_proc{
        registers, 1, ogplay::memory::GuestAddress{proc & ~UINT32_C(1)}};
    const auto hook = f.boundary.FastHostCallHook();
    REQUIRE(hook.invoke(hook.userdata, 2U, fast_proc) == ogplay::cpu::HostCallResult::handled);
    CHECK(f.boundary.Stats().gl_errors == before + 5U);
    const auto errors = f.boundary.Trace("glClear", 5);
    REQUIRE(errors.size() == 5U);
    for (const auto& entry : errors) CHECK(entry.error == 0x0506U);
    // A legal call must not inherit the pending context error; reading it must not recount it.
    AuditGl(f, "glClearColor", {0, 0, 0, 0});
    REQUIRE(f.boundary.Trace("glClearColor", 1).size() == 1U);
    CHECK_FALSE(f.boundary.Trace("glClearColor", 1).front().error.has_value());
    AuditGl(f, "glEnable", {0xFFFFFFFF});
    CHECK(f.boundary.Stats().gl_errors == before + 6U);
    CHECK(f.boundary.Trace("glEnable", 1).front().error == 0x0500U);
    CHECK(AuditGl(f, "glGetError") == 0x0506U);
    CHECK(AuditGl(f, "glGetError") == 0U);
    CHECK(f.boundary.Stats().gl_errors == before + 6U);
    CHECK_FALSE(f.boundary.Trace("glGetError", 1).front().error.has_value());
    const auto snapshot = f.boundary.TryTrace(32);
    REQUIRE(snapshot.has_value());
    CHECK(std::count_if(snapshot->begin(), snapshot->end(), [](const auto& e) { return e.error.has_value(); }) == 6);
    const auto other = AuditContext(f, 2);
    AuditBind(f, other, surface);
    CHECK(AuditGl(f, "glGetError") == 0U);
    AuditBind(f, context, surface);
    CHECK(AuditGl(f, "glGetError") == 0U);
}

TEST_CASE("BND54 texture family avoids conflicting consumer priorities") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    using ogplay::gles::CompressedTextureFamily;
    for (const auto family : {CompressedTextureFamily::all, CompressedTextureFamily::atc}) {
        BoundaryFixture f(1, nullptr, {}, true, {}, false, family);
        REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
        const auto context = AuditContext(f, 2), surface = AuditSurface(f);
        AuditBind(f, context, surface);
        const auto pointer = AuditGl(f, "glGetString", {0x1F03});
        const ogplay::memory::GuestAddress address{pointer};
        std::string extensions(f.memory.CStringLength(address, 4096, 1), '\0');
        f.memory.Read(address, std::as_writable_bytes(std::span(extensions)), 1);
        const bool atc = extensions.find("GL_AMD_compressed_ATC_texture") != extensions.npos;
        const bool pvrtc = extensions.find("GL_IMG_texture_compression_pvrtc") != extensions.npos;
        // A consumer whose file and resource selectors have opposite vendor priorities.
        const auto file_family = atc ? 4U : pvrtc ? 2U : 8U;
        const auto resource_family = pvrtc ? 2U : atc ? 4U : 8U;
        if (family == CompressedTextureFamily::all) CHECK(file_family != resource_family);
        else { CHECK(file_family == resource_family); CHECK(file_family == 4U); }
        REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
        REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, context}) == 1U);
        REQUIRE(f.Call("libEGL.so", "eglDestroySurface", {1, surface}) == 1U);
    }
}

TEST_CASE("BND54 texture family queries uploads and context lifetimes agree") {
    if (!ogplay::gles::IsNativeAngleEglAvailable()) return;
    using ogplay::gles::CompressedTextureFamily;
    for (const auto family : {CompressedTextureFamily::atc, CompressedTextureFamily::pvrtc}) {
        BoundaryFixture f(1, nullptr, {}, true, {}, false, family);
        REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1U);
        const auto surface = AuditSurface(f);
        const auto read_string = [&](std::uint32_t pointer) {
            const ogplay::memory::GuestAddress address{pointer};
            std::string value(f.memory.CStringLength(address, 4096, 1), '\0');
            f.memory.Read(address, std::as_writable_bytes(std::span(value)), 1);
            return value;
        };
        std::uint32_t previous{};
        for (const auto version : {1U, 2U, 3U, 3U}) {
            const auto context = AuditContext(f, version, version == 3U ? previous : 0U);
            AuditBind(f, context, surface);
            const auto gl = [&](std::string_view library, std::string_view name,
                                std::initializer_list<std::uint32_t> arguments = {}) {
                const auto entry = f.boundary.Symbols().Lookup(library, name); REQUIRE(entry);
                return BoundaryCallAddress(f, entry->Value(), {arguments.begin(), arguments.size()});
            };
            std::string gles1_entry_extensions;
            for (const auto library : {"libGLESv1_CM.so", "libGLESv2.so"}) {
                CAPTURE(version); CAPTURE(library);
                const auto extensions = read_string(gl(library, "glGetString", {0x1F03}));
                CHECK((extensions.find("GL_AMD_compressed_ATC_texture") != extensions.npos) == (family == CompressedTextureFamily::atc));
                CHECK((extensions.find("GL_IMG_texture_compression_pvrtc") != extensions.npos) == (family == CompressedTextureFamily::pvrtc));
                CHECK(extensions.find("GL_OES_compressed_ETC1_RGB8_texture") != extensions.npos);
                // Both ELF glGetString entries follow the current guest EGL client version.
                // ManagedGlString exposes the programmable API's extension set.
                if (std::string_view(library) == "libGLESv1_CM.so") {
                    gles1_entry_extensions = extensions;
                } else {
                    CHECK(extensions == gles1_entry_extensions);
                    if (version == 1U)
                        CHECK(extensions.find("GL_OES_matrix_palette") != extensions.npos);
                    else
                        CHECK(f.boundary.ManagedGlString(0x1F03) == extensions);
                }
                gl(library, "glGetIntegerv", {0x86A2, f.output.Value()});
                const auto count = f.bus.Read32(f.output, 1); REQUIRE(count > 0U); REQUIRE(count < 128U);
                const auto output = f.output.Add(64), sentinel = output.Add(count * 4U);
                f.bus.Write32(sentinel, 0xdeadbeef, 1);
                gl(library, "glGetIntegerv", {0x86A3, output.Value()});
                std::vector<std::uint32_t> formats;
                for (std::uint32_t i = 0; i < count; ++i) formats.push_back(f.bus.Read32(output.Add(i * 4U), 1));
                CHECK(f.bus.Read32(sentinel, 1) == 0xdeadbeef);
                CHECK(std::count(formats.begin(), formats.end(), 0x8d64U) == 1);
                for (const auto format : {0x8c92U, 0x8c93U, 0x87eeU})
                    CHECK((std::count(formats.begin(), formats.end(), format) == 1) == (family == CompressedTextureFamily::atc));
                for (const auto format : {0x8c00U, 0x8c01U, 0x8c02U, 0x8c03U})
                    CHECK((std::count(formats.begin(), formats.end(), format) == 1) == (family == CompressedTextureFamily::pvrtc));
                gl(library, "glGetFloatv", {0x86A3, output.Value()});
                for (std::uint32_t i = 0; i < count; ++i)
                    CHECK(std::bit_cast<float>(f.bus.Read32(output.Add(i * 4U), 1)) == static_cast<float>(formats[i]));
                gl(library, "glGetBooleanv", {0x86A3, output.Value()});
                for (std::uint32_t i = 0; i < count; ++i) CHECK(f.bus.Read8(output.Add(i), 1) == 1U);
                CHECK(gl(library, "glGetError") == 0U);
                if (version == 3U && std::string_view(library) == "libGLESv2.so") {
                    gl(library, "glGetInteger64v", {0x86A3, output.Value()});
                    for (std::uint32_t i = 0; i < count; ++i) CHECK(f.bus.Read64(output.Add(i * 8U), 1) == formats[i]);
                    gl(library, "glGetIntegerv", {0x821D, f.output.Value()});
                    const auto extension_count = f.bus.Read32(f.output, 1);
                    std::string indexed;
                    for (std::uint32_t i = 0; i < extension_count; ++i) {
                        indexed += read_string(gl(library, "glGetStringi", {0x1F03, i})); indexed.push_back(' ');
                    }
                    CHECK(indexed == extensions);
                    const auto hidden = family == CompressedTextureFamily::atc ? 0x8c02U : 0x8c92U;
                    gl(library, "glCompressedTexImage3D", {0x806F, 0, hidden, 4, 4, 1, 0, 32, output.Value()});
                    CHECK(gl(library, "glGetError") == 0x0500U);
                    gl(library, "glTexStorage2D", {0x0DE1, 1, hidden, 4, 4});
                    CHECK(gl(library, "glGetError") == 0x0500U);
                    gl(library, "glGenBuffers", {1, f.output.Value()});
                    gl(library, "glBindBuffer", {0x88EC, f.bus.Read32(f.output, 1)});
                    gl(library, "glBufferData", {0x88EC, 32, 0, 0x88E4});
                    gl(library, "glCompressedTexImage2D", {0x0DE1, 0, hidden, 4, 4, 0, 32, 0});
                    CHECK(gl(library, "glGetError") == 0x0500U);
                    gl(library, "glBindBuffer", {0x88EC, 0});
                }
                gl(library, "glGenTextures", {1, f.output.Value()});
                gl(library, "glBindTexture", {0x0DE1, f.bus.Read32(f.output, 1)});
                const auto bytes = f.output.Add(1024);
                const std::array<std::byte, 32> data{}; f.memory.Write(bytes, data, 1);
                const auto visible = family == CompressedTextureFamily::atc ? 0x8c92U : 0x8c02U;
                const auto hidden = family == CompressedTextureFamily::atc ? 0x8c02U : 0x8c92U;
                gl(library, "glCompressedTexImage2D", {0x0DE1, 0, visible, 8, 8, 0, 32, bytes.Value()});
                CHECK(gl(library, "glGetError") == 0U);
                gl(library, "glCompressedTexImage2D", {0x0DE1, 0, hidden, 8, 8, 0, 32, bytes.Value()});
                CHECK(gl(library, "glGetError") == 0x0500U);
            }
            REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1U);
            if (previous != 0U) REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, previous}) == 1U);
            previous = version >= 2U ? context : 0U;
            if (version == 1U) REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, context}) == 1U);
        }
        REQUIRE(f.Call("libEGL.so", "eglDestroyContext", {1, previous}) == 1U);
        REQUIRE(f.Call("libEGL.so", "eglDestroySurface", {1, surface}) == 1U);
    }
}
