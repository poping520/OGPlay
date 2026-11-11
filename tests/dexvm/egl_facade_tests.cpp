#include <future>
#include <mutex>
#include "../runtime/boundary/integration/android_boundary_fixture.h"
#include "ogplay/runtime/jni/host_copy_payload_provider.h"
#include "ogplay/runtime/raster/bitmap.h"
#include "boot_dex.h"
#include "android_value_fixture.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <bit>
#include "ogplay/gles/angle_frame.h"
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/dex_activity_lifecycle.h"

namespace {
#if defined(_WIN32)
constexpr auto kNativeRenderer = ogplay::gles::AngleRenderer::d3d11;
#elif defined(__APPLE__)
constexpr auto kNativeRenderer = ogplay::gles::AngleRenderer::metal;
#else
constexpr auto kNativeRenderer = ogplay::gles::AngleRenderer::vulkan;
#endif

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct EglVm final {
    ogplay::test::android_value::AndroidValueVm fixture;
    JavaObjectModel& model{fixture.model};
    DexClassLinker& linker{fixture.linker};
    ogplay::core::CapabilityLedger& ledger{fixture.ledger};
    std::shared_ptr<DexVmAndroidContext> context{fixture.context};
    Interpreter& interpreter{fixture.vm};

    static std::vector<IntrinsicClassDecl> Catalog(const std::vector<IntrinsicClassDecl>& extras) {
        auto catalog = extras;
        catalog.push_back(
            std::move(IntrinsicClassBuilder::Class("Lfixture/GlView;", "Landroid/opengl/GLSurfaceView;")).Build());
        catalog.push_back(std::move(IntrinsicClassBuilder::Class("Lfixture/EglPolicies;", "Ljava/lang/Object;",
                                                                 {"Landroid/opengl/GLSurfaceView$EGLContextFactory;",
                                                                  "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"}))
                              .Build());
        return catalog;
    }

    explicit EglVm(const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                   AndroidGuestCallSession* session = nullptr, const char* dex_fixture = nullptr,
                   const std::vector<IntrinsicClassDecl>& extras = {}, ogplay::gles::AngleBackend graphics = {})
        : fixture(backend, Catalog(extras), false, dex_fixture, graphics) {
        context->session = session;
    }

    VmValue CallStatic(const char* owner, const char* name, const char* descriptor,
                       std::vector<VmValue> arguments = {}) {
        const auto klass = linker.ResolveDescriptor(owner);
        const auto method = linker.FindDirectMethod(klass, name, descriptor);
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmValue CallOn(const VmObjectRef receiver, const char* name, const char* descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto outcome = CallOnOutcome(receiver, name, descriptor, std::move(arguments));
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome CallOnOutcome(const VmObjectRef receiver, const char* name, const char* descriptor,
                                std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linker.Class(klass).vtable[*index], arguments);
    }

    VmObjectRef IntArray(const std::vector<std::int32_t>& values) {
        const auto klass = linker.ResolveDescriptor("[I");
        const auto array =
            model.NewPrimitiveArray(klass, JniPrimitiveKind::integer, static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetPrimitiveElement(array, static_cast<JniSize>(index), static_cast<std::uint32_t>(values[index]));
        }
        return array;
    }

    VmObjectRef FloatArray(const std::vector<float>& values) {
        const auto klass = linker.ResolveDescriptor("[F");
        const auto array =
            model.NewPrimitiveArray(klass, JniPrimitiveKind::float_value, static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetPrimitiveElement(array, static_cast<JniSize>(index), std::bit_cast<std::uint32_t>(values[index]));
        }
        return array;
    }

    VmObjectRef StringArray(const std::vector<std::u16string>& values) {
        const auto array =
            model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/String;"),
                                 linker.ResolveDescriptor("Ljava/lang/String;"), static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetObjectElement(array, static_cast<JniSize>(index), model.NewString(values[index]));
        }
        return array;
    }
};

void Put16(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1U] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::byte>(value >> (byte * 8U));
    }
}

std::vector<std::byte> MinimalLibcElf() {
    std::vector<std::byte> bytes(0x300, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56);
    Put32(bytes, 104, 56);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 34);
    Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1);
    Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10190U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101b0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0libc.so\0__system_property_area__\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 2);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x1c0, 9);
    Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x11};
    Put16(bytes, 0x1ce, 1);
    return bytes;
}

}  // namespace

TEST_CASE("BND38 GLU project and unproject follow API19 matrix and offset semantics") {
    EglVm vm;
    const auto error =
        vm.CallStatic("Landroid/opengl/GLU;", "gluErrorString", "(I)Ljava/lang/String;", {VmValue::Int(0x0502)}).ref;
    CHECK(vm.interpreter.StringUtf8(error) == "invalid operation");
    CHECK_FALSE(
        vm.CallStatic("Landroid/opengl/GLU;", "gluErrorString", "(I)Ljava/lang/String;", {VmValue::Int(0x7fffffff)})
            .ref.IsValid());
    const std::vector<float> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto model = vm.FloatArray(identity);
    const auto projection = vm.FloatArray(identity);
    const auto viewport = vm.IntArray({10, 20, 200, 100});
    const auto window = vm.FloatArray({-1, -1, -1, -1, -1});
    CHECK(vm.CallStatic("Landroid/opengl/GLU;", "gluProject", "(FFF[FI[FI[II[FI)I",
                        {VmValue::Float(0.0F), VmValue::Float(0.0F), VmValue::Float(0.0F), VmValue::Ref(model),
                         VmValue::Int(0), VmValue::Ref(projection), VmValue::Int(0), VmValue::Ref(viewport),
                         VmValue::Int(0), VmValue::Ref(window), VmValue::Int(1)})
              .AsInt() == 1);
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(vm.model.GetPrimitiveElement(window, 1))) ==
          doctest::Approx(110.0F));
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(vm.model.GetPrimitiveElement(window, 2))) ==
          doctest::Approx(70.0F));
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(vm.model.GetPrimitiveElement(window, 3))) ==
          doctest::Approx(0.5F));

    const auto object = vm.FloatArray({-1, -1, -1, -1, -1});
    CHECK(vm.CallStatic("Landroid/opengl/GLU;", "gluUnProject", "(FFF[FI[FI[II[FI)I",
                        {VmValue::Float(110.0F), VmValue::Float(70.0F), VmValue::Float(0.5F), VmValue::Ref(model),
                         VmValue::Int(0), VmValue::Ref(projection), VmValue::Int(0), VmValue::Ref(viewport),
                         VmValue::Int(0), VmValue::Ref(object), VmValue::Int(1)})
              .AsInt() == 1);
    for (std::int32_t index = 1; index < 4; ++index) {
        CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(vm.model.GetPrimitiveElement(object, index))) ==
              doctest::Approx(0.0F));
    }
    const auto singular = vm.FloatArray(std::vector<float>(16, 0.0F));
    CHECK(vm.CallStatic("Landroid/opengl/GLU;", "gluUnProject", "(FFF[FI[FI[II[FI)I",
                        {VmValue::Float(0), VmValue::Float(0), VmValue::Float(0), VmValue::Ref(singular),
                         VmValue::Int(0), VmValue::Ref(projection), VmValue::Int(0), VmValue::Ref(viewport),
                         VmValue::Int(0), VmValue::Ref(object), VmValue::Int(1)})
              .AsInt() == 0);
}

TEST_CASE("WU-3 publishes API 19 EGL14 classes and core signatures") {
    EglVm vm;
    for (const auto descriptor :
         {"Landroid/opengl/EGL14;", "Landroid/opengl/EGLConfig;", "Landroid/opengl/EGLContext;",
          "Landroid/opengl/EGLDisplay;", "Landroid/opengl/EGLSurface;", "Landroid/opengl/EGLObjectHandle;"}) {
        CHECK(vm.linker.FindClass(descriptor).has_value());
    }
    const auto egl14 = vm.linker.ResolveDescriptor("Landroid/opengl/EGL14;");
    const std::array methods{
        std::pair{"eglGetError", "()I"},
        std::pair{"eglGetDisplay", "(I)Landroid/opengl/EGLDisplay;"},
        std::pair{"eglInitialize", "(Landroid/opengl/EGLDisplay;[II[II)Z"},
        std::pair{"eglTerminate", "(Landroid/opengl/EGLDisplay;)Z"},
        std::pair{"eglQueryString", "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;"},
        std::pair{"eglGetConfigs", "(Landroid/opengl/EGLDisplay;[Landroid/opengl/EGLConfig;II[II)Z"},
        std::pair{"eglChooseConfig", "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z"},
        std::pair{"eglGetConfigAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Z"},
        std::pair{
            "eglCreateWindowSurface",
            "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Ljava/lang/Object;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglCreatePbufferSurface",
                  "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglCreatePixmapSurface",
                  "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglDestroySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"},
        std::pair{"eglBindAPI", "(I)Z"},
        std::pair{"eglQueryAPI", "()I"},
        std::pair{"eglWaitClient", "()Z"},
        std::pair{"eglCreatePbufferFromClientBuffer",
                  "(Landroid/opengl/EGLDisplay;IILandroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglSurfaceAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;II)Z"},
        std::pair{"eglBindTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
        std::pair{"eglReleaseTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
        std::pair{"eglSwapInterval", "(Landroid/opengl/EGLDisplay;I)Z"},
        std::pair{
            "eglCreateContext",
            "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;"},
        std::pair{"eglDestroyContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z"},
        std::pair{
            "eglMakeCurrent",
            "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z"},
        std::pair{"eglGetCurrentContext", "()Landroid/opengl/EGLContext;"},
        std::pair{"eglGetCurrentSurface", "(I)Landroid/opengl/EGLSurface;"},
        std::pair{"eglGetCurrentDisplay", "()Landroid/opengl/EGLDisplay;"},
        std::pair{"eglQueryContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;I[II)Z"},
        std::pair{"eglQuerySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I[II)Z"},
        std::pair{"eglReleaseThread", "()Z"},
        std::pair{"eglWaitGL", "()Z"},
        std::pair{"eglWaitNative", "(I)Z"},
        std::pair{"eglSwapBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"},
        std::pair{"eglCopyBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"}};
    for (const auto& [name, descriptor] : methods) {
        CAPTURE(name);
        CAPTURE(descriptor);
        CHECK(vm.linker.FindDirectMethod(egl14, name, descriptor).has_value());
    }
    auto& linked = vm.linker.Class(egl14);
    REQUIRE(static_cast<bool>(linked.clinit_implementation));
    IntrinsicContext call{vm.interpreter, VmObjectRef{}, {}};
    static_cast<void>(linked.clinit_implementation(call));
    CHECK(vm.context->egl.egl14_no_display.IsValid());
    CHECK(vm.context->egl.egl14_no_context.IsValid());
    CHECK(vm.context->egl.egl14_no_surface.IsValid());
}

TEST_CASE("WU-3 EGL14 arrays pbuffer and shared context use native registry") {
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{"libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    VirtualFileSystem filesystem;
    AndroidGuestCallSessionRequest request{19,
                                           "libc.so",
                                           std::span{&module, 1},
                                           {kNativeRenderer, ogplay::gles::AngleDevice::hardware},
                                           4,
                                           3,
                                           1000,
                                           1,
                                           &filesystem,
                                           {}};
    request.boundary_options.compressed_texture_family = ogplay::gles::CompressedTextureFamily::atc;
    auto session = AndroidGuestCallSession::Start(request);
    EglVm vm(InterpreterBackend::switch_dispatch, session.get());
    const auto display =
        vm.CallStatic("Landroid/opengl/EGL14;", "eglGetDisplay", "(I)Landroid/opengl/EGLDisplay;", {VmValue::Int(0)})
            .ref;
    REQUIRE(display.IsValid());
    const auto major = vm.IntArray({-1, -1, -1});
    const auto minor = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic(
                "Landroid/opengl/EGL14;", "eglInitialize", "(Landroid/opengl/EGLDisplay;[II[II)Z",
                {VmValue::Ref(display), VmValue::Ref(major), VmValue::Int(1), VmValue::Ref(minor), VmValue::Int(1)})
              .AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(major, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(major, 1) == 1U);
    CHECK(vm.model.GetPrimitiveElement(minor, 1) == 4U);
    const auto native_extensions =
        vm.CallStatic("Landroid/opengl/EGL14;", "eglQueryString", "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;",
                      {VmValue::Ref(display), VmValue::Int(0x3055)})
            .ref;
    CHECK(vm.interpreter.StringUtf8(native_extensions).find("EGL_KHR_get_all_proc_addresses") != std::string::npos);
    const auto foreign_display = vm.interpreter.NewIntrinsicInstance("Landroid/opengl/EGLDisplay;");
    CHECK_FALSE(vm.CallStatic("Landroid/opengl/EGL14;", "eglQueryString",
                              "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;",
                              {VmValue::Ref(foreign_display), VmValue::Int(0x3055)})
                    .ref.IsValid());
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() == 0x3008);
    const auto foreign_surface = vm.interpreter.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglDestroySurface",
                        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z",
                        {VmValue::Ref(display), VmValue::Ref(foreign_surface)})
              .AsInt() == 0);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() == 0x300D);

    const auto attributes = vm.IntArray({0, 0x3024, 8, 0x3033, 0x0001, 0x3038});
    const auto config_array_class = vm.linker.ResolveDescriptor("[Landroid/opengl/EGLConfig;");
    const auto config_class = vm.linker.ResolveDescriptor("Landroid/opengl/EGLConfig;");
    const auto configs = vm.model.NewObjectArray(config_array_class, config_class, 2);
    const auto count = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglChooseConfig",
                        "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z",
                        {VmValue::Ref(display), VmValue::Ref(attributes), VmValue::Int(1), VmValue::Ref(configs),
                         VmValue::Int(1), VmValue::Int(1), VmValue::Ref(count), VmValue::Int(1)})
              .AsInt() == 1);
    const auto config = vm.model.GetObjectElement(configs, 1);
    REQUIRE(config.IsValid());
    CHECK(vm.model.GetPrimitiveElement(count, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(count, 1) == 1U);

    const auto pixmap =
        vm.CallStatic("Landroid/opengl/EGL14;", "eglCreatePixmapSurface",
                      "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;",
                      {VmValue::Ref(display), VmValue::Ref(config), VmValue::Int(7),
                       VmValue::Ref(vm.IntArray({0, 0x3038})), VmValue::Int(1)})
            .ref;
    CHECK(pixmap == vm.context->egl.egl14_no_surface);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() == 0x300A);

    const auto surface_attributes = vm.IntArray({0, 0x3057, 2, 0x3056, 2, 0x3038});
    const auto surface =
        vm.CallStatic("Landroid/opengl/EGL14;", "eglCreatePbufferSurface",
                      "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
                      {VmValue::Ref(display), VmValue::Ref(config), VmValue::Ref(surface_attributes), VmValue::Int(1)})
            .ref;
    REQUIRE(surface.IsValid());
    const auto context_attributes = vm.IntArray({0, 0x3098, 3, 0x3038});
    const auto first =
        vm.CallStatic(
              "Landroid/opengl/EGL14;", "eglCreateContext",
              "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
              {VmValue::Ref(display), VmValue::Ref(config), VmValue::Ref(vm.context->egl.egl14_no_context),
               VmValue::Ref(context_attributes), VmValue::Int(1)})
            .ref;
    REQUIRE(first.IsValid());
    const auto shared =
        vm.CallStatic(
              "Landroid/opengl/EGL14;", "eglCreateContext",
              "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
              {VmValue::Ref(display), VmValue::Ref(config), VmValue::Ref(first), VmValue::Ref(context_attributes),
               VmValue::Int(1)})
            .ref;
    REQUIRE(shared.IsValid());
    CHECK(
        vm.CallStatic(
              "Landroid/opengl/EGL14;", "eglMakeCurrent",
              "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z",
              {VmValue::Ref(display), VmValue::Ref(surface), VmValue::Ref(surface), VmValue::Ref(first)})
            .AsInt() == 1);
    // BND54: interpreted Java strings, indexed strings and native query counts agree.
    const auto extensions_value =
        vm.CallStatic("Landroid/opengl/GLES20;", "glGetString", "(I)Ljava/lang/String;", {VmValue::Int(0x1F03)}).ref;
    const auto gl_extensions = vm.interpreter.StringUtf8(extensions_value);
    CHECK(gl_extensions.find("GL_AMD_compressed_ATC_texture") != std::string::npos);
    CHECK(gl_extensions.find("GL_IMG_texture_compression_pvrtc") == std::string::npos);
    const auto extension_query = vm.IntArray({0});
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES30;", "glGetIntegerv", "(I[II)V",
                                    {VmValue::Int(0x821D), VmValue::Ref(extension_query), VmValue::Int(0)}));
    const auto num_extensions = vm.model.GetPrimitiveElement(extension_query, 0);
    std::string indexed_extensions;
    for (std::uint32_t index = 0; index < num_extensions; ++index) {
        const auto value = vm.CallStatic("Landroid/opengl/GLES30;", "glGetStringi", "(II)Ljava/lang/String;",
                                         {VmValue::Int(0x1F03), VmValue::Int(static_cast<std::int32_t>(index))})
                               .ref;
        indexed_extensions += vm.interpreter.StringUtf8(value);
        indexed_extensions.push_back(' ');
    }
    CHECK(indexed_extensions == gl_extensions);
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glGetIntegerv", "(I[II)V",
                                    {VmValue::Int(0x86A2), VmValue::Ref(extension_query), VmValue::Int(0)}));
    const auto num_formats = vm.model.GetPrimitiveElement(extension_query, 0);
    REQUIRE(num_formats > 0U);
    REQUIRE(num_formats < 128U);
    const auto format_query = vm.IntArray(std::vector<std::int32_t>(num_formats, 0));
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glGetIntegerv", "(I[II)V",
                                    {VmValue::Int(0x86A3), VmValue::Ref(format_query), VmValue::Int(0)}));
    for (std::uint32_t index = 0; index < num_formats; ++index) {
        const auto format = vm.model.GetPrimitiveElement(format_query, static_cast<JniSize>(index));
        CHECK_FALSE((format >= 0x8c00U && format <= 0x8c03U));
    }
    CHECK(vm.CallStatic("Landroid/opengl/GLES20;", "glGetError", "()I").AsInt() == 0);
    const auto thread_id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    CHECK(
        session->InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glClearColor",
                                   std::array{std::bit_cast<std::uint32_t>(0.25F), std::bit_cast<std::uint32_t>(0.5F),
                                              std::bit_cast<std::uint32_t>(0.75F), std::bit_cast<std::uint32_t>(1.0F)},
                                   thread_id) == 0U);

    const auto vertex =
        vm.CallStatic("Landroid/opengl/GLES20;", "glCreateShader", "(I)I", {VmValue::Int(0x8B31)}).AsInt();
    const auto source =
        vm.model.NewString(u"attribute vec4 aPos; uniform float uScale; void main(){ gl_Position=aPos*uScale; }");
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glShaderSource", "(ILjava/lang/String;)V",
                                    {VmValue::Int(vertex), VmValue::Ref(source)}));
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glCompileShader", "(I)V", {VmValue::Int(vertex)}));
    const auto returned_source =
        vm.CallStatic("Landroid/opengl/GLES20;", "glGetShaderSource", "(I)Ljava/lang/String;", {VmValue::Int(vertex)})
            .ref;
    CHECK(vm.interpreter.StringUtf8(returned_source).find("uScale") != std::string::npos);
    CHECK(vm.interpreter
              .StringUtf8(vm.CallStatic("Landroid/opengl/GLES20;", "glGetShaderInfoLog", "(I)Ljava/lang/String;",
                                        {VmValue::Int(vertex)})
                              .ref)
              .empty());

    const auto fragment =
        vm.CallStatic("Landroid/opengl/GLES20;", "glCreateShader", "(I)I", {VmValue::Int(0x8B30)}).AsInt();
    const auto fragment_source = vm.model.NewString(u"precision mediump float; void main(){ gl_FragColor=vec4(1.0); }");
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glShaderSource", "(ILjava/lang/String;)V",
                                    {VmValue::Int(fragment), VmValue::Ref(fragment_source)}));
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glCompileShader", "(I)V", {VmValue::Int(fragment)}));
    const auto program = vm.CallStatic("Landroid/opengl/GLES20;", "glCreateProgram", "()I").AsInt();
    for (const auto shader : {vertex, fragment}) {
        static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glAttachShader", "(II)V",
                                        {VmValue::Int(program), VmValue::Int(shader)}));
    }
    static_cast<void>(vm.CallStatic("Landroid/opengl/GLES20;", "glLinkProgram", "(I)V", {VmValue::Int(program)}));
    CHECK(vm.interpreter
              .StringUtf8(vm.CallStatic("Landroid/opengl/GLES20;", "glGetProgramInfoLog", "(I)Ljava/lang/String;",
                                        {VmValue::Int(program)})
                              .ref)
              .empty());
    const auto size = vm.IntArray({-1});
    const auto type = vm.IntArray({-1});
    const auto active = vm.CallStatic("Landroid/opengl/GLES20;", "glGetActiveUniform", "(II[II[II)Ljava/lang/String;",
                                      {VmValue::Int(program), VmValue::Int(0), VmValue::Ref(size), VmValue::Int(0),
                                       VmValue::Ref(type), VmValue::Int(0)})
                            .ref;
    CHECK(vm.interpreter.StringUtf8(active) == "uScale");
    CHECK(vm.model.GetPrimitiveElement(size, 0) == 1U);
    CHECK(vm.model.GetPrimitiveElement(type, 0) == 0x1406U);
    const auto indices = vm.IntArray({-1});
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES30;", "glGetUniformIndices", "(I[Ljava/lang/String;[II)V",
        {VmValue::Int(program), VmValue::Ref(vm.StringArray({u"uScale"})), VmValue::Ref(indices), VmValue::Int(0)}));
    CHECK(vm.model.GetPrimitiveElement(indices, 0) != 0xffffffffU);
    CHECK(session->InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glClear", std::array{0x00004000U}, thread_id) ==
          0U);
    std::array<std::byte, 4> pixel_staging{};
    std::vector<std::byte> pixel_output;
    const auto read_result = session->NIO().WithTemporaryGuestMemory(
        pixel_staging, true,
        [&](const ogplay::memory::GuestAddress address) {
            return session->InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glReadPixels",
                                              std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U, address.Value()}, thread_id);
        },
        &pixel_output);
    CHECK(read_result == 0U);
    REQUIRE(pixel_output.size() == 4U);
    CHECK(std::to_integer<std::uint8_t>(pixel_output[0]) == doctest::Approx(64).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(pixel_output[1]) == doctest::Approx(128).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(pixel_output[2]) == doctest::Approx(191).epsilon(0.04));
    // GLUtils passes API19 premultiplied storage verbatim to GL.
    const auto bitmap = vm.interpreter.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    auto upload_bytes = std::make_shared<std::array<std::byte, 4>>();
    auto upload = std::make_shared<ogplay::runtime::raster::Bitmap>(1, 1, 5, true, true, *upload_bytes, upload_bytes,
                                                                    ogplay::memory::GuestAddress{});
    upload->Set(0, 0x80804020U);
    vm.context->bitmaps.emplace(bitmap.Value(), DexVmAndroidContext::BitmapState{1, 1, upload});
    const auto names = vm.IntArray({0});
    vm.CallStatic("Landroid/opengl/GLES20;", "glGenTextures", "(I[II)V",
                  {VmValue::Int(1), VmValue::Ref(names), VmValue::Int(0)});
    const auto texture = static_cast<int>(vm.model.GetPrimitiveElement(names, 0));
    vm.CallStatic("Landroid/opengl/GLES20;", "glBindTexture", "(II)V", {VmValue::Int(0x0DE1), VmValue::Int(texture)});
    vm.CallStatic("Landroid/opengl/GLUtils;", "texImage2D", "(IILandroid/graphics/Bitmap;I)V",
                  {VmValue::Int(0x0DE1), VmValue::Int(0), VmValue::Ref(bitmap), VmValue::Int(0)});
    vm.CallStatic("Landroid/opengl/GLES20;", "glGenFramebuffers", "(I[II)V",
                  {VmValue::Int(1), VmValue::Ref(names), VmValue::Int(0)});
    const auto framebuffer = static_cast<int>(vm.model.GetPrimitiveElement(names, 0));
    vm.CallStatic("Landroid/opengl/GLES20;", "glBindFramebuffer", "(II)V",
                  {VmValue::Int(0x8D40), VmValue::Int(framebuffer)});
    vm.CallStatic(
        "Landroid/opengl/GLES20;", "glFramebufferTexture2D", "(IIIII)V",
        {VmValue::Int(0x8D40), VmValue::Int(0x8CE0), VmValue::Int(0x0DE1), VmValue::Int(texture), VmValue::Int(0)});
    REQUIRE(
        vm.CallStatic("Landroid/opengl/GLES20;", "glCheckFramebufferStatus", "(I)I", {VmValue::Int(0x8D40)}).AsInt() ==
        0x8CD5);
    static_cast<void>(session->NIO().WithTemporaryGuestMemory(
        pixel_staging, true,
        [&](const ogplay::memory::GuestAddress address) {
            return session->InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glReadPixels",
                                              std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U, address.Value()}, thread_id);
        },
        &pixel_output));
    CHECK(pixel_output == std::vector<std::byte>{std::byte{64}, std::byte{32}, std::byte{16}, std::byte{128}});
    CHECK(vm.CallStatic("Landroid/opengl/GLES20;", "glGetError", "()I").AsInt() == 0);
    vm.CallStatic("Landroid/opengl/GLES20;", "glBindFramebuffer", "(II)V", {VmValue::Int(0x8D40), VmValue::Int(0)});
    vm.model.SetPrimitiveElement(names, 0, static_cast<std::uint32_t>(framebuffer));
    vm.CallStatic("Landroid/opengl/GLES20;", "glDeleteFramebuffers", "(I[II)V",
                  {VmValue::Int(1), VmValue::Ref(names), VmValue::Int(0)});
    vm.model.SetPrimitiveElement(names, 0, static_cast<std::uint32_t>(texture));
    vm.CallStatic("Landroid/opengl/GLES20;", "glDeleteTextures", "(I[II)V",
                  {VmValue::Int(1), VmValue::Ref(names), VmValue::Int(0)});
    const auto output = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglQuerySurface",
                        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I[II)Z",
                        {VmValue::Ref(display), VmValue::Ref(surface), VmValue::Int(0x3057), VmValue::Ref(output),
                         VmValue::Int(1)})
              .AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(output, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(output, 1) == 2U);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglWaitGL", "()Z").AsInt() == 1);

    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglReleaseThread", "()Z").AsInt() == 1);
    for (const auto context : {shared, first}) {
        CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglDestroyContext",
                            "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z",
                            {VmValue::Ref(display), VmValue::Ref(context)})
                  .AsInt() == 1);
    }
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglDestroySurface",
                        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z",
                        {VmValue::Ref(display), VmValue::Ref(surface)})
              .AsInt() == 1);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglTerminate", "(Landroid/opengl/EGLDisplay;)Z",
                        {VmValue::Ref(display)})
              .AsInt() == 1);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglInitialize", "(Landroid/opengl/EGLDisplay;[II[II)Z",
                        {VmValue::Ref(display), VmValue::Ref(VmObjectRef{}), VmValue::Int(0),
                         VmValue::Ref(VmObjectRef{}), VmValue::Int(0)})
              .AsInt() == 1);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglTerminate", "(Landroid/opengl/EGLDisplay;)Z",
                        {VmValue::Ref(display)})
              .AsInt() == 1);

    const auto egl10 =
        vm.CallStatic("Ljavax/microedition/khronos/egl/EGLContext;", "getEGL", "()Ljavax/microedition/khronos/egl/EGL;")
            .ref;
    const auto display10 =
        vm.CallOn(egl10, "eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
                  {VmValue::Ref(VmObjectRef{})})
            .ref;
    CHECK(vm.CallOn(egl10, "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
                    {VmValue::Ref(display10), VmValue::Ref(VmObjectRef{})})
              .AsInt() == 1);
    const auto configs10 =
        vm.model.NewObjectArray(vm.linker.ResolveDescriptor("[Ljavax/microedition/khronos/egl/EGLConfig;"),
                                vm.linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;"), 1);
    const auto count10 = vm.IntArray({0});
    CHECK(vm.CallOn(egl10, "eglGetConfigs",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display10), VmValue::Ref(configs10), VmValue::Int(1), VmValue::Ref(count10)})
              .AsInt() == 1);
    const auto config10 = vm.model.GetObjectElement(configs10, 0);
    REQUIRE(config10.IsValid());
    const auto surface10 =
        vm.CallOn(
              egl10, "eglCreatePbufferSurface",
              "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
              {VmValue::Ref(display10), VmValue::Ref(config10),
               VmValue::Ref(vm.IntArray({0x3057, 2, 0x3056, 2, 0x3038}))})
            .ref;
    REQUIRE(surface10.IsValid());
    const auto no_context10 =
        vm.linker.FindFieldRecursive(vm.linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGL10;"),
                                     "EGL_NO_CONTEXT", "Ljavax/microedition/khronos/egl/EGLContext;");
    REQUIRE(no_context10);
    const auto& no_context_field = vm.linker.Field(*no_context10);
    REQUIRE_FALSE(vm.interpreter.EnsureClassInitialized(no_context_field.owner).exception.IsValid());
    const auto no_context = VmObjectRef{vm.linker.Class(no_context_field.owner).static_storage[no_context_field.slot]};
    const auto context10 =
        vm.CallOn(
              egl10, "eglCreateContext",
              "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
              {VmValue::Ref(display10), VmValue::Ref(config10), VmValue::Ref(no_context),
               VmValue::Ref(vm.IntArray({0x3098, 2, 0x3038}))})
            .ref;
    REQUIRE(context10.IsValid());
    const auto shared10 =
        vm.CallOn(
              egl10, "eglCreateContext",
              "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
              {VmValue::Ref(display10), VmValue::Ref(config10), VmValue::Ref(context10),
               VmValue::Ref(vm.IntArray({0x3098, 2, 0x3038}))})
            .ref;
    REQUIRE(shared10.IsValid());
    CHECK(
        vm.CallOn(
              egl10, "eglMakeCurrent",
              "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
              {VmValue::Ref(display10), VmValue::Ref(surface10), VmValue::Ref(surface10), VmValue::Ref(context10)})
            .AsInt() == 1);
    const auto current10 =
        vm.CallOn(egl10, "eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;").ref;
    CHECK(vm.CallOn(current10, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(context10)}).AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglWaitGL", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglReleaseThread", "()Z").AsInt() == 1);
    for (const auto context : {shared10, context10}) {
        CHECK(vm.CallOn(egl10, "eglDestroyContext",
                        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                        {VmValue::Ref(display10), VmValue::Ref(context)})
                  .AsInt() == 1);
    }
    CHECK(vm.CallOn(egl10, "eglDestroySurface",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                    {VmValue::Ref(display10), VmValue::Ref(surface10)})
              .AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", {VmValue::Ref(display10)})
              .AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
                    {VmValue::Ref(display10), VmValue::Ref(VmObjectRef{})})
              .AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", {VmValue::Ref(display10)})
              .AsInt() == 1);
    session->Stop();
}

TEST_CASE("DVM-83 publishes the API 19 Java GLES link surface") {
    EglVm vm;
    const std::array classes{"Landroid/opengl/GLES10;",    "Landroid/opengl/GLES10Ext;", "Landroid/opengl/GLES11;",
                             "Landroid/opengl/GLES11Ext;", "Landroid/opengl/GLES20;",    "Landroid/opengl/GLES30;",
                             "Landroid/opengl/GLUtils;",   "Landroid/opengl/GLU;"};
    for (const auto descriptor : classes) {
        CAPTURE(descriptor);
        CHECK(vm.linker.FindClass(descriptor).has_value());
    }
    const auto gles20 = vm.linker.ResolveDescriptor("Landroid/opengl/GLES20;");
    CHECK(vm.linker.FindDirectMethod(gles20, "glBindTexture", "(II)V").has_value());
    CHECK(vm.linker.FindDirectMethod(gles20, "<init>", "()V").has_value());
    CHECK(vm.linker.FindDirectMethod(gles20, "glBufferData", "(IILjava/nio/Buffer;I)V").has_value());
    const auto gles30 = vm.linker.ResolveDescriptor("Landroid/opengl/GLES30;");
    CHECK(vm.linker.FindDirectMethod(gles30, "glBindVertexArray", "(I)V").has_value());
    CHECK(vm.linker.FindDirectMethod(gles30, "glGetStringi", "(II)Ljava/lang/String;").has_value());
    CHECK(vm.linker.FindDirectMethod(gles20, "glShaderSource", "(ILjava/lang/String;)V").has_value());
    const auto utils = vm.linker.ResolveDescriptor("Landroid/opengl/GLUtils;");
    CHECK(vm.linker.FindDirectMethod(utils, "texImage2D", "(IILandroid/graphics/Bitmap;I)V").has_value());
    CHECK(vm.linker.Class(vm.linker.ResolveDescriptor("Landroid/opengl/GLES11;")).super ==
          vm.linker.ResolveDescriptor("Landroid/opengl/GLES10;"));

    auto pixels = std::make_shared<std::array<std::byte, 4>>();
    const auto bitmap = vm.interpreter.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    vm.context->bitmaps.emplace(
        bitmap.Value(),
        DexVmAndroidContext::BitmapState{1, 1,
                                         std::make_shared<ogplay::runtime::raster::Bitmap>(
                                             1, 1, 5, true, true, *pixels, pixels, ogplay::memory::GuestAddress{})});
    CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getInternalFormat", "(Landroid/graphics/Bitmap;)I",
                        {VmValue::Ref(bitmap)})
              .AsInt() == 0x1908);
    CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getType", "(Landroid/graphics/Bitmap;)I", {VmValue::Ref(bitmap)})
              .AsInt() == 0x1401);
    for (const auto [config, format, type] :
         {std::array<std::int32_t, 3>{1, 0x1906, 0x1401}, std::array<std::int32_t, 3>{3, 0x1907, 0x8363},
          std::array<std::int32_t, 3>{4, 0x1908, 0x8033}}) {
        vm.context->bitmaps.at(bitmap.Value()).config = config;
        CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getInternalFormat", "(Landroid/graphics/Bitmap;)I",
                            {VmValue::Ref(bitmap)})
                  .AsInt() == format);
        CHECK(
            vm.CallStatic("Landroid/opengl/GLUtils;", "getType", "(Landroid/graphics/Bitmap;)I", {VmValue::Ref(bitmap)})
                .AsInt() == type);
    }
    const auto error =
        vm.CallStatic("Landroid/opengl/GLUtils;", "getEGLErrorString", "(I)Ljava/lang/String;", {VmValue::Int(0x3004)})
            .ref;
    CHECK(vm.interpreter.StringUtf8(error) == "EGL_BAD_ATTRIBUTE");
}

TEST_CASE("SurfaceTexture original Java connects a Surface producer to real external pixels and events") {
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
    using namespace ogplay::gles;
    REQUIRE(IsNativeAngleEglAvailable());
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int callbacks{};
        VmObjectRef notified;
        const auto listener =
            std::move(IntrinsicClassBuilder::Class("Lfixture/FrameListener;", "Ljava/lang/Object;",
                                                   {"Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;"})
                          .VirtualMethod("onFrameAvailable", "(Landroid/graphics/SurfaceTexture;)V",
                                         [&](IntrinsicContext& call) {
                                             ++callbacks;
                                             notified = call.arguments[0].ref;
                                             return VmValue::Void();
                                         }))
                .Build();
        EglVm fixture(backend, nullptr, nullptr, {listener});
        VmThreadRuntime threads(fixture.interpreter);
        fixture.context->threads = &threads;
        auto& vm = fixture.interpreter;
        const auto construct = [&](const char* descriptor, const char* signature, std::vector<VmValue> args) {
            auto object = vm.NewIntrinsicInstance(descriptor);
            const auto roots = vm.ProtectReferences(std::array{object});
            const auto method = fixture.linker.FindDirectMethod(fixture.model.ObjectClass(object), "<init>", signature);
            REQUIRE(method);
            args.insert(args.begin(), VmValue::Ref(object));
            const auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return object;
        };
        const auto texture = construct("Landroid/graphics/SurfaceTexture;", "(I)V", {VmValue::Int(-1)});
        const auto texture_root = vm.ProtectReferences(std::array{texture});
        CHECK(fixture.CallOn(texture, "getTimestamp", "()J").wide == 0);
        CHECK(fixture.context->surface_textures.at(texture.Value()).texture == UINT32_MAX);
        const auto failed = fixture.CallOnOutcome(texture, "updateTexImage", "()V");
        REQUIRE(failed.exception.IsValid());
        CHECK(fixture.linker.Class(failed.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(fixture.CallOnOutcome(texture, "releaseTexImage", "()V").exception.IsValid());
        fixture.CallOn(texture, "detachFromGLContext", "()V");  // no current context before first use

        const auto display = EglDisplayResources::Create({AngleRenderer::vulkan, AngleDevice::swiftshader});
        const auto target = EglSurfaceResources::Create(display, 2, 2);
        auto frame = AngleFrame::CreateContext(display, 2);
        frame.BindSurfaces(target, target);
        fixture.context->surface_texture_graphics = [&](const ExternalTextureCall& call) {
            return frame.UseExternalTexture(call);
        };
        const auto name = frame.GenerateTextures(1).front();
        fixture.CallOn(texture, "attachToGLContext", "(I)V", {VmValue::Int(static_cast<std::int32_t>(name))});
        const auto surface =
            construct("Landroid/view/Surface;", "(Landroid/graphics/SurfaceTexture;)V", {VmValue::Ref(texture)});
        const auto surface_root = vm.ProtectReferences(std::array{surface});
        CHECK(fixture.CallOn(surface, "getGenerationId", "()I").AsInt() == 1);
        CHECK(fixture.CallOn(surface, "isValid", "()Z").AsInt() == 1);
        const auto stream = ResolveAndroidSurfaceStream(vm, *fixture.context, surface);
        REQUIRE(stream);
        fixture.context->media_players.try_emplace(surface.Value());
        try {
            BindAndroidMediaSurface(vm, fixture.context, surface.Value(), surface);
            FAIL("unconnected MediaPlayer stream producer accepted");
        } catch (const VmJavaThrow& error) {
            CHECK(error.descriptor == "Ljava/lang/UnsupportedOperationException;");
        }
        fixture.context->media_players.erase(surface.Value());
        const auto producer = stream->Connect();
        const auto callback = vm.NewIntrinsicInstance("Lfixture/FrameListener;");
        fixture.CallOn(texture, "setOnFrameAvailableListener",
                       "(Landroid/graphics/SurfaceTexture$OnFrameAvailableListener;)V", {VmValue::Ref(callback)});
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(vm, *fixture.context));
        CHECK(callbacks == 0);

        SurfaceStreamFrame input;
        input.width = 2;
        input.height = 2;
        input.timestamp_ns = 123456789;
        input.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        input.transform[0] = 0.5F;
        CHECK(stream->Submit(producer, std::move(input)) == SurfaceStream::SubmitResult::queued);
        CHECK(callbacks == 0);  // producer never enters Java
        CHECK_FALSE(ogplay::test::android_value::DispatchFixtureMessages(vm, *fixture.context));
        CHECK(callbacks == 1);
        CHECK(notified == texture);
        const auto unpack = frame.GenerateBuffers(1);
        frame.BindBuffer(0x88EC, unpack.front());
        frame.BufferData(0x88EC, 256, std::nullopt, 0x88E4);
        frame.PixelStore(0x0CF5, 8);
        frame.PixelStore(0x0CF2, 7);
        fixture.CallOn(texture, "updateTexImage", "()V");
        CHECK(frame.BoundBuffer(0x88EC) == unpack.front());
        CHECK(frame.GetIntegers(0x0CF5, 1).front() == 8);
        CHECK(frame.GetIntegers(0x0CF2, 1).front() == 7);
        CHECK(fixture.CallOn(texture, "getTimestamp", "()J").wide == 123456789);
        const auto matrix =
            fixture.model.NewPrimitiveArray(fixture.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 16);
        fixture.CallOn(texture, "getTransformMatrix", "([F)V", {VmValue::Ref(matrix)});
        CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(fixture.model.GetPrimitiveElement(matrix, 0))) == 0.5F);
        fixture.CallOn(texture, "updateTexImage", "()V");  // empty queue keeps the current image and timestamp
        CHECK(fixture.CallOn(texture, "getTimestamp", "()J").wide == 123456789);

        const auto vertex = frame.CreateShader(0x8B31), fragment = frame.CreateShader(0x8B30);
        const std::string vs = "attribute vec2 p;varying vec2 uv;void main(){gl_Position=vec4(p,0.,1.);uv=(p+1.)*.5;}";
        const std::string fs =
            "#extension GL_OES_EGL_image_external : require\nprecision mediump float;uniform samplerExternalOES t;varying vec2 uv;void main(){gl_FragColor=texture2D(t,uv);}";
        frame.ShaderSource(vertex, {&vs, 1});
        frame.ShaderSource(fragment, {&fs, 1});
        frame.CompileShader(vertex);
        frame.CompileShader(fragment);
        const auto program = frame.CreateProgram();
        frame.AttachShader(program, vertex);
        frame.AttachShader(program, fragment);
        frame.LinkProgram(program);
        REQUIRE(frame.GetProgramParameter(program, 0x8B82) == 1);
        frame.UseProgram(program);
        frame.Uniform1i(frame.GetUniformLocation(program, "t"), 0);
        const std::array<float, 6> triangle{-1, -1, 3, -1, -1, 3};
        const auto buffers = frame.GenerateBuffers(1);
        frame.BindBuffer(0x8892, buffers.front());
        frame.BufferData(0x8892, sizeof(triangle), std::as_bytes(std::span(triangle)), 0x88E4);
        const auto position = static_cast<std::uint32_t>(frame.GetAttribLocation(program, "p"));
        frame.VertexAttributePointer(position, 2, 0x1406, false, 0, 0);
        frame.SetVertexAttributeEnabled(position, true);
        frame.Viewport(0, 0, 2, 2);
        frame.DrawArrays(4, 0, 3);
        CHECK(frame.ReadRgba8() ==
              std::vector<std::uint8_t>{0, 0, 255, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 0, 255});
        const auto wrong_context = fixture.context->surface_textures.at(texture.Value()).binding;
        frame.ReleaseCurrent();
        CHECK(fixture.CallOnOutcome(texture, "updateTexImage", "()V").exception.IsValid());
        {
            auto alien = AngleFrame::CreateContext(display, 2);
            alien.BindSurfaces(target, target);
            fixture.context->surface_texture_graphics = [&](const ExternalTextureCall& call) {
                return alien.UseExternalTexture(call);
            };
            const auto mismatch = fixture.CallOnOutcome(texture, "updateTexImage", "()V");
            REQUIRE(mismatch.exception.IsValid());
            CHECK(fixture.linker.Class(mismatch.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            alien.ReleaseCurrent();
        }
        fixture.context->surface_texture_graphics = [&](const ExternalTextureCall& call) {
            return frame.UseExternalTexture(call);
        };
        frame.BindSurfaces(target, target);
        fixture.CallOn(texture, "detachFromGLContext", "()V");
        const auto replacement = frame.GenerateTextures(1).front();
        fixture.CallOn(texture, "attachToGLContext", "(I)V", {VmValue::Int(static_cast<std::int32_t>(replacement))});
        CHECK(fixture.context->surface_textures.at(texture.Value()).binding == wrong_context);
        fixture.CallOn(texture, "releaseTexImage", "()V");
        CHECK(fixture.CallOn(texture, "getTimestamp", "()J").wide == 0);
        fixture.CallOn(texture, "detachFromGLContext", "()V");
        const auto released_name = frame.GenerateTextures(1).front();
        fixture.CallOn(texture, "attachToGLContext", "(I)V", {VmValue::Int(static_cast<std::int32_t>(released_name))});
        frame.TextureParameter(0x8D65, 0x2801, 0x2600);
        frame.TextureParameter(0x8D65, 0x2800, 0x2600);
        frame.DrawArrays(4, 0, 3);
        const auto released_pixels = frame.ReadRgba8();
        const std::string pattern =
            "___________________________________XX_XX_______X_X_____X_X____X_XXXXXXX_X____XXXXXXXXXXX__"
            "___XX_XXX_XX_______XXXXXXX_________X___X_________X_____X__________________________________";
        for (std::size_t row = 0; row < 2; ++row)
            for (std::size_t col = 0; col < 2; ++col) {
                const auto value = pattern[(row == 0 ? 9U : 3U) * 15 + (col == 0 ? 3U : 11U)] == 'X' ? 0 : 255;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    CHECK(released_pixels[(row * 2 + col) * 4 + channel] == value);
                CHECK(released_pixels[(row * 2 + col) * 4 + 3] == 255);
            }
        fixture.CallOn(surface, "release", "()V");
        CHECK(stream->Live());
        CHECK(fixture.CallOn(surface, "getGenerationId", "()I").AsInt() == 2);
        CHECK_FALSE(ResolveAndroidSurfaceStream(vm, *fixture.context, surface));
        CHECK(fixture.CallOn(surface, "isValid", "()Z").AsInt() == 0);
        fixture.CallOn(texture, "release", "()V");
        fixture.CallOn(texture, "release", "()V");
        CHECK_FALSE(stream->Live());
        CHECK(fixture.context->surface_stream_budget->Used() == 0);
        frame.DeleteTextures(std::array{released_name});
        frame.DeleteBuffers(buffers);
        frame.DeleteBuffers(unpack);
        frame.DeleteShader(vertex);
        frame.DeleteShader(fragment);
        frame.DeleteProgram(program);
        fixture.context->surface_texture_graphics = {};
        fixture.context->threads = nullptr;
    }
#endif
}

TEST_CASE("SurfaceTexture GC abandons consumers without producer endpoints retaining Java owners") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        EglVm fixture(backend);
        VmThreadRuntime threads(fixture.interpreter);
        fixture.context->threads = &threads;
        auto& vm = fixture.interpreter;
        auto texture = vm.NewIntrinsicInstance("Landroid/graphics/SurfaceTexture;");
        VmObjectRef surface;
        {
            const auto roots = vm.ProtectReferences(std::array{texture});
            const auto type = fixture.linker.ResolveDescriptor("Landroid/graphics/SurfaceTexture;");
            const auto init = fixture.linker.FindDirectMethod(type, "<init>", "(IZ)V");
            REQUIRE(init);
            const auto result = vm.Call(*init, std::array{VmValue::Ref(texture), VmValue::Int(-1), VmValue::Int(1)});
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            surface = vm.NewIntrinsicInstance("Landroid/view/Surface;");
            const auto surface_root = vm.ProtectReferences(std::array{surface});
            const auto constructor = fixture.linker.FindDirectMethod(fixture.model.ObjectClass(surface), "<init>",
                                                                     "(Landroid/graphics/SurfaceTexture;)V");
            REQUIRE(constructor);
            const auto made = vm.Call(*constructor, std::array{VmValue::Ref(surface), VmValue::Ref(texture)});
            REQUIRE_MESSAGE(!made.exception.IsValid(), made.exception_message);
        }
        auto stream = ResolveAndroidSurfaceStream(vm, *fixture.context, surface);
        REQUIRE(stream);
        CHECK(stream->SingleBuffered());
        {
            const auto roots = vm.ProtectReferences(std::array{surface});
            static_cast<void>(vm.CollectGarbage("SurfaceTexture weak owner"));
            vm.RunFinalization();
            static_cast<void>(vm.CollectGarbage("finalized SurfaceTexture"));
            CHECK_FALSE(stream->Live());
            CHECK(fixture.context->surface_textures.empty());
            CHECK(fixture.CallOn(surface, "isValid", "()Z").AsInt() == 0);
        }
        static_cast<void>(vm.CollectGarbage("Surface producer endpoint"));
        ReleaseAndroidSurfaceStreams(*fixture.context);
        CHECK(fixture.context->surface_stream_budget->Used() == 0);
        fixture.context->threads = nullptr;
    }
}

TEST_CASE("SurfaceTexture production cold graphics uses current context and shared external binding") {
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
    using namespace ogplay::gles;
    auto libc = MinimalLibcElf();
    const ogplay::loader::Elf32ModuleInput module{"libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    VirtualFileSystem filesystem;
    auto session = AndroidGuestCallSession::Start({19,
                                                   "libc.so",
                                                   std::span{&module, 1},
                                                   {AngleRenderer::vulkan, AngleDevice::swiftshader},
                                                   2,
                                                   2,
                                                   1000,
                                                   1,
                                                   &filesystem,
                                                   {}});
    CHECK_THROWS_AS(session->Process().UseExternalTexture({}), ExternalTextureError);
    session->OpenManagedSurface();
    std::array<std::byte, 4> bytes{};
    std::vector<std::byte> output;
    static_cast<void>(session->NIO().WithTemporaryGuestMemory(
        bytes, true,
        [&](ogplay::memory::GuestAddress address) {
            return session->InvokeManagedGles(GlesApi::gles2, "glGenTextures", std::array{1U, address.Value()}, 1);
        },
        &output));
    const auto word = [&] {
        std::uint32_t value{};
        for (std::size_t i = 0; i < 4; ++i)
            value |= std::to_integer<std::uint32_t>(output[i]) << (i * 8);
        return value;
    };
    const auto texture = word();
    REQUIRE(texture != 0);
    const std::array<std::uint8_t, 4> rgba{23, 59, 107, 255};
    const auto binding =
        session->Process().UseExternalTexture({ExternalTextureCall::Operation::upload, {}, texture, 1, 1, rgba});
    const auto query = [&] {
        static_cast<void>(session->NIO().WithTemporaryGuestMemory(
            bytes, true,
            [&](ogplay::memory::GuestAddress address) {
                return session->InvokeManagedGles(GlesApi::gles2, "glGetIntegerv", std::array{0x8D67U, address.Value()},
                                                  1);
            },
            &output));
        return word();
    };
    CHECK(query() == texture);
    static_cast<void>(
        session->Process().UseExternalTexture({ExternalTextureCall::Operation::detach, binding, texture}));
    CHECK(query() == 0);
    session->ReleaseManagedSurfaceFromCallingThread();
    CHECK_THROWS_AS(session->Process().UseExternalTexture({}), ExternalTextureError);
    session->Stop();
#endif
}

TEST_CASE("DVM-339 native window reports independent buffer geometry without GPU") {
    ogplay::test::android_boundary::BoundaryFixture f;
    const ogplay::memory::GuestAddress holder{0x1234};
    const auto window = f.boundary.AcquireSurfaceWindow({holder, 480, 320});
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 480);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getHeight", {window.Value()}) == 320);
    f.boundary.UpdateSurfaceWindow({holder, 240, 160});
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 240);
    CHECK(f.Call("libandroid.so", "ANativeWindow_getHeight", {window.Value()}) == 160);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 480, 320, 1}) != 0);

    // fromSurface can hold the hook mutex while waiting for the guest VM lock.
    // Retirement runs under the VM lock and must not wait for that hook mutex.
    struct PendingJni {
        std::mutex vm;
        std::promise<void> entered;
        ogplay::memory::GuestAddress window;
    } pending;

    pending.window = window;
    auto entered = pending.entered.get_future();
    std::unique_lock vm_lock(pending.vm);
    f.boundary.SetSurfaceHooks({&pending, +[](void* owner, std::uint64_t, ogplay::memory::GuestAddress,
                                              ogplay::runtime::SurfaceJavaReference) {
                                    auto& state = *static_cast<PendingJni*>(owner);
                                    state.entered.set_value();
                                    std::scoped_lock lock(state.vm);
                                    return state.window;
                                }});
    auto jni =
        std::async(std::launch::async, [&] { return f.Call("libandroid.so", "ANativeWindow_fromSurface", {1, 1}); });
    CHECK(entered.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    auto retired = std::async(std::launch::async, [&] { f.boundary.RetireSurfaceWindow(holder); });
    CHECK(retired.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    vm_lock.unlock();
    retired.get();
    CHECK(jni.get() == window.Value());
    CHECK(f.Call("libandroid.so", "ANativeWindow_getWidth", {window.Value()}) == 240);
    CHECK(f.Call("libandroid.so", "ANativeWindow_setBuffersGeometry", {window.Value(), 240, 160, 1}) != 0);
    CHECK(f.Call("libandroid.so", "ANativeWindow_release", {window.Value()}) == 0);
}

TEST_CASE("DVM-339 EGL buffer geometry changes on swap and publishes independently sized pixels") {
    ogplay::test::android_boundary::BoundaryFixture f;
    const ogplay::memory::GuestAddress holder{0x1234};
    const auto window = f.boundary.AcquireSurfaceWindow({holder, 2, 3});

    struct Capture {
        unsigned queues{};
        std::shared_ptr<const ogplay::video::VideoFrame> frame;
    } capture;

    f.boundary.SetSurfaceHooks(
        {&capture,
         +[](void*, std::uint64_t, ogplay::memory::GuestAddress, ogplay::runtime::SurfaceJavaReference) {
             return ogplay::memory::GuestAddress{};
         },
         +[](void* owner, ogplay::memory::GuestAddress identity, std::shared_ptr<ogplay::video::VideoFrame> frame) {
             CHECK(identity == ogplay::memory::GuestAddress{0x1234});
             static_cast<Capture*>(owner)->frame = std::move(frame);
         },
         +[](void* owner, ogplay::memory::GuestAddress identity) {
             CHECK(identity == ogplay::memory::GuestAddress{0x1234});
             ++static_cast<Capture*>(owner)->queues;
         }});
    REQUIRE(f.Call("libEGL.so", "eglInitialize", {1}) == 1);
    auto producer = window.Value();
    SUBCASE("ANativeWindow handle") {}
    SUBCASE("Java EGL holder alias") {
        f.boundary.UpdateSurfaceWindow({holder, 2, 3});
        producer = holder.Value();
    }
    const auto surface = f.Call("libEGL.so", "eglCreateWindowSurface", {1, 2, producer, 0});
    REQUIRE(surface != 0);
    CHECK(capture.queues == 0);                                       // creation is not a queue
    CHECK(f.Call("libEGL.so", "eglSwapBuffers", {1, surface}) == 0);  // not current
    CHECK(capture.queues == 0);
    const auto query = [&](unsigned attribute) {
        REQUIRE(f.Call("libEGL.so", "eglQuerySurface", {1, surface, attribute, f.output.Value()}) == 1);
        return f.bus.Read32(f.output);
    };
    CHECK(query(0x3057) == 2);
    CHECK(query(0x3056) == 3);
    f.bus.Write32(f.output, 0x3098U, 1);
    f.bus.Write32(f.output.Add(4), 2, 1);
    f.bus.Write32(f.output.Add(8), 0x3038U, 1);
    const auto context = f.Call("libEGL.so", "eglCreateContext", {1, 2, 0, f.output.Value()});
    REQUIRE(context != 0);
    REQUIRE(f.Call("libEGL.so", "eglMakeCurrent", {1, surface, surface, context}) == 1);
    const auto clear = [&] {
        CHECK(f.Call("libGLESv2.so", "glClearColor", {0x3f800000U, 0, 0, 0x3f800000U}) == 0);
        CHECK(f.Call("libGLESv2.so", "glClear", {0x4000U}) == 0);
    };
    clear();
    f.boundary.UpdateSurfaceWindow({holder, 3, 2});
    CHECK(query(0x3057) == 2);  // old dequeued buffer is still current
    REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, surface}) == 1);
    CHECK(capture.queues == 1);
    REQUIRE(capture.frame);
    const auto first = capture.frame;
    CHECK(capture.frame->width == 2);
    CHECK(capture.frame->height == 3);
    CHECK(query(0x3057) == 3);
    CHECK(query(0x3056) == 2);
    clear();
    REQUIRE(f.Call("libEGL.so", "eglSwapBuffers", {1, surface}) == 1);
    CHECK(capture.queues == 2);
    REQUIRE(capture.frame);
    CHECK(capture.frame != first);
    CHECK(first->width == 2);
    CHECK(first->height == 3);
    CHECK(first->rgba8.front() == 255);
    CHECK(capture.frame->width == 3);
    CHECK(capture.frame->height == 2);
    REQUIRE(capture.frame->rgba8.size() == 24);
    for (unsigned i = 0; i < 24; i += 4) {
        CHECK(capture.frame->rgba8[i] == 255);
        CHECK(capture.frame->rgba8[i + 1] == 0);
        CHECK(capture.frame->rgba8[i + 2] == 0);
        CHECK(capture.frame->rgba8[i + 3] == 255);
    }
    f.boundary.RetireSurfaceWindow(holder);
    CHECK(f.Call("libEGL.so", "eglSwapBuffers", {1, surface}) == 0);
    CHECK(capture.queues == 2);
    CHECK(f.Call("libEGL.so", "eglMakeCurrent", {1, 0, 0, 0}) == 1);
    CHECK(f.Call("libEGL.so", "eglTerminate", {1}) == 1);
}
