#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
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

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct EglVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter interpreter;

    explicit EglVm(
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
        AndroidGuestCallSession* session = nullptr)
        : interpreter([this]() -> DexClassLinker& {
              linker.RegisterIntrinsics(CoreIntrinsicCatalog());
              auto android = AndroidIntrinsicCatalog(context);
              android.push_back(std::move(
                  IntrinsicClassBuilder::Class(
                      "Lfixture/EglPolicies;", "Ljava/lang/Object;",
                      {"Landroid/opengl/GLSurfaceView$EGLContextFactory;",
                       "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"}))
                                    .Build());
              linker.RegisterIntrinsics(std::move(android));
              ogplay::test::RegisterBootDex(linker);
              linker.Link();
              return linker;
        }(), model, nullptr, ledger, {.backend = backend}) {
        context->session = session;
        const auto egl10 = linker.ResolveDescriptor(
            "Ljavax/microedition/khronos/egl/EGL10;");
        auto& linked = linker.Class(egl10);
        REQUIRE(static_cast<bool>(linked.clinit_implementation));
        IntrinsicContext call{interpreter, VmObjectRef{}, {}};
        static_cast<void>(linked.clinit_implementation(call));
    }

    VmValue CallStatic(const char* owner, const char* name,
                       const char* descriptor,
                       std::vector<VmValue> arguments = {}) {
        const auto klass = linker.ResolveDescriptor(owner);
        const auto method = linker.FindDirectMethod(klass, name, descriptor);
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmValue CallOn(const VmObjectRef receiver, const char* name,
                   const char* descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto outcome = CallOnOutcome(
            receiver, name, descriptor, std::move(arguments));
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome CallOnOutcome(
        const VmObjectRef receiver, const char* name, const char* descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(linker.Class(klass).vtable[*index], arguments);
    }

    VmObjectRef IntArray(const std::vector<std::int32_t>& values) {
        const auto klass = linker.ResolveDescriptor("[I");
        const auto array = model.NewPrimitiveArray(
            klass, JniPrimitiveKind::integer,
            static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetPrimitiveElement(array, static_cast<JniSize>(index),
                                      static_cast<std::uint32_t>(values[index]));
        }
        return array;
    }

    VmObjectRef FloatArray(const std::vector<float>& values) {
        const auto klass = linker.ResolveDescriptor("[F");
        const auto array = model.NewPrimitiveArray(
            klass, JniPrimitiveKind::float_value,
            static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetPrimitiveElement(
                array, static_cast<JniSize>(index),
                std::bit_cast<std::uint32_t>(values[index]));
        }
        return array;
    }

    VmObjectRef StringArray(const std::vector<std::u16string>& values) {
        const auto array = model.NewObjectArray(
            linker.ResolveDescriptor("[Ljava/lang/String;"),
            linker.ResolveDescriptor("Ljava/lang/String;"),
            static_cast<JniSize>(values.size()));
        for (std::size_t index = 0; index < values.size(); ++index) {
            model.SetObjectElement(array, static_cast<JniSize>(index),
                                   model.NewString(values[index]));
        }
        return array;
    }
};

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1U] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<std::byte>(value >> (byte * 8U));
    }
}

std::vector<std::byte> MinimalLibcElf() {
    std::vector<std::byte> bytes(0x300, std::byte{});
    bytes[0] = std::byte{0x7f}; bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'}; bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1}; bytes[5] = std::byte{1}; bytes[6] = std::byte{1};
    Put16(bytes, 16, 3); Put16(bytes, 18, 40); Put32(bytes, 20, 1);
    Put32(bytes, 28, 52); Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52); Put16(bytes, 42, 32); Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U); Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300); Put32(bytes, 76, 6); Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100); Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56); Put32(bytes, 104, 56); Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    Put32(bytes, 0x100, ogplay::loader::kElfDynamicStringTable);
    Put32(bytes, 0x104, 0x10160U);
    Put32(bytes, 0x108, ogplay::loader::kElfDynamicStringTableSize);
    Put32(bytes, 0x10c, 34); Put32(bytes, 0x110, ogplay::loader::kElfDynamicSoname);
    Put32(bytes, 0x114, 1); Put32(bytes, 0x118, ogplay::loader::kElfDynamicHash);
    Put32(bytes, 0x11c, 0x10190U);
    Put32(bytes, 0x120, ogplay::loader::kElfDynamicSymbolTable);
    Put32(bytes, 0x124, 0x101b0U);
    Put32(bytes, 0x128, ogplay::loader::kElfDynamicSymbolEntrySize);
    Put32(bytes, 0x12c, 16);
    const char strings[] = "\0libc.so\0__system_property_area__\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1); Put32(bytes, 0x194, 2); Put32(bytes, 0x198, 1);
    Put32(bytes, 0x1c0, 9); Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4); bytes[0x1cc] = std::byte{0x11};
    Put16(bytes, 0x1ce, 1);
    return bytes;
}

}  // namespace

TEST_CASE("GLSurfaceView retains linked EGL policy identities") {
    EglVm vm;
    const auto policy = vm.interpreter.NewIntrinsicInstance(
        "Lfixture/EglPolicies;");
    const auto policy_class = vm.model.ObjectClass(policy);
    CHECK(vm.linker.IsAssignable(
        vm.linker.ResolveDescriptor(
            "Landroid/opengl/GLSurfaceView$EGLContextFactory;"),
        policy_class));
    CHECK(vm.linker.IsAssignable(
        vm.linker.ResolveDescriptor(
            "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"),
        policy_class));

    const auto view = vm.interpreter.NewIntrinsicInstance(
        "Landroid/opengl/GLSurfaceView;");
    static_cast<void>(vm.CallOn(
        view, "setEGLContextFactory",
        "(Landroid/opengl/GLSurfaceView$EGLContextFactory;)V",
        {VmValue::Ref(policy)}));
    static_cast<void>(vm.CallOn(
        view, "setEGLConfigChooser",
        "(Landroid/opengl/GLSurfaceView$EGLConfigChooser;)V",
        {VmValue::Ref(policy)}));
    CHECK(vm.context->egl_context_factory == policy);
    CHECK(vm.context->egl_config_chooser == policy);

    static_cast<void>(vm.CallOn(
        view, "setEGLContextFactory",
        "(Landroid/opengl/GLSurfaceView$EGLContextFactory;)V",
        {VmValue::Ref(VmObjectRef{})}));
    CHECK_FALSE(vm.context->egl_context_factory.IsValid());
    CHECK(vm.context->egl_config_chooser == policy);
}

TEST_CASE("DVM-134 GLSurfaceView stores validated render mode per view") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        EglVm vm(backend);
        const auto view = vm.interpreter.NewIntrinsicInstance(
            "Landroid/opengl/GLSurfaceView;");
        CHECK(vm.CallOn(view, "getRenderMode", "()I").AsInt() == 1);
        static_cast<void>(vm.CallOn(
            view, "setRenderMode", "(I)V", {VmValue::Int(0)}));
        CHECK(vm.CallOn(view, "getRenderMode", "()I").AsInt() == 0);
        static_cast<void>(vm.CallOn(
            view, "setRenderMode", "(I)V", {VmValue::Int(1)}));
        CHECK(vm.context->gl_surface_render_modes.at(view.Value()) == 1);

        const auto invalid = vm.CallOnOutcome(
            view, "setRenderMode", "(I)V", {VmValue::Int(2)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(vm.linker.Class(invalid.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
    }
}

TEST_CASE("BND37 GLSurfaceView publishes API19 context config and GL queue entrypoints") {
    EglVm vm;
    const auto view = vm.interpreter.NewIntrinsicInstance(
        "Landroid/opengl/GLSurfaceView;");
    static_cast<void>(vm.CallOn(
        view, "setEGLContextClientVersion", "(I)V", {VmValue::Int(2)}));
    CHECK(vm.context->gl_surface_client_versions.at(view.Value()) == 2);

    static_cast<void>(vm.CallOn(
        view, "setEGLConfigChooser", "(Z)V", {VmValue::Int(1)}));
    CHECK(vm.context->gl_surface_config_specs.at(view.Value()) ==
          std::vector<std::int32_t>{8, 8, 8, 8, 16, 0});
    static_cast<void>(vm.CallOn(
        view, "setEGLConfigChooser", "(IIIIII)V",
        {VmValue::Int(5), VmValue::Int(6), VmValue::Int(5),
         VmValue::Int(0), VmValue::Int(24), VmValue::Int(8)}));
    CHECK(vm.context->gl_surface_config_specs.at(view.Value()) ==
          std::vector<std::int32_t>{5, 6, 5, 0, 24, 8});

    const auto runnable = vm.interpreter.NewIntrinsicInstance(
        "Ljava/lang/Object;");
    static_cast<void>(vm.CallOn(
        view, "queueEvent", "(Ljava/lang/Runnable;)V",
        {VmValue::Ref(runnable)}));
    REQUIRE(vm.context->gl_surface_events.size() == 1U);
    CHECK(vm.context->gl_surface_events.front() == runnable);
    static_cast<void>(vm.CallOn(view, "setRenderMode", "(I)V",
                                {VmValue::Int(0)}));
    static_cast<void>(vm.CallOn(view, "requestRender", "()V"));
    CHECK(vm.context->gl_surface_render_requests.at(view.Value()));
    vm.context->gl_surface_renderer_view = view;
    CHECK(ogplay::session::ConsumeGlSurfaceDrawRequest(*vm.context));
    CHECK_FALSE(ogplay::session::ConsumeGlSurfaceDrawRequest(*vm.context));
    static_cast<void>(vm.CallOn(view, "requestRender", "()V"));
    CHECK(ogplay::session::ConsumeGlSurfaceDrawRequest(*vm.context));
    static_cast<void>(vm.CallOn(view, "setRenderMode", "(I)V",
                                {VmValue::Int(1)}));
    CHECK(ogplay::session::ConsumeGlSurfaceDrawRequest(*vm.context));
    CHECK(ogplay::session::ConsumeGlSurfaceDrawRequest(*vm.context));

    const auto invalid = vm.CallOnOutcome(
        view, "setEGLContextClientVersion", "(I)V", {VmValue::Int(4)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(vm.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");
}

TEST_CASE("BND38 GLU project and unproject follow API19 matrix and offset semantics") {
    EglVm vm;
    const auto error = vm.CallStatic(
        "Landroid/opengl/GLU;", "gluErrorString", "(I)Ljava/lang/String;",
        {VmValue::Int(0x0502)}).ref;
    CHECK(vm.interpreter.StringUtf8(error) == "invalid operation");
    CHECK_FALSE(vm.CallStatic(
        "Landroid/opengl/GLU;", "gluErrorString", "(I)Ljava/lang/String;",
        {VmValue::Int(0x7fffffff)}).ref.IsValid());
    const std::vector<float> identity{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto model = vm.FloatArray(identity);
    const auto projection = vm.FloatArray(identity);
    const auto viewport = vm.IntArray({10, 20, 200, 100});
    const auto window = vm.FloatArray({-1, -1, -1, -1, -1});
    CHECK(vm.CallStatic(
        "Landroid/opengl/GLU;", "gluProject", "(FFF[FI[FI[II[FI)I",
        {VmValue::Float(0.0F), VmValue::Float(0.0F), VmValue::Float(0.0F),
         VmValue::Ref(model), VmValue::Int(0), VmValue::Ref(projection),
         VmValue::Int(0), VmValue::Ref(viewport), VmValue::Int(0),
         VmValue::Ref(window), VmValue::Int(1)}).AsInt() == 1);
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(
              vm.model.GetPrimitiveElement(window, 1))) ==
          doctest::Approx(110.0F));
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(
              vm.model.GetPrimitiveElement(window, 2))) ==
          doctest::Approx(70.0F));
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(
              vm.model.GetPrimitiveElement(window, 3))) ==
          doctest::Approx(0.5F));

    const auto object = vm.FloatArray({-1, -1, -1, -1, -1});
    CHECK(vm.CallStatic(
        "Landroid/opengl/GLU;", "gluUnProject", "(FFF[FI[FI[II[FI)I",
        {VmValue::Float(110.0F), VmValue::Float(70.0F), VmValue::Float(0.5F),
         VmValue::Ref(model), VmValue::Int(0), VmValue::Ref(projection),
         VmValue::Int(0), VmValue::Ref(viewport), VmValue::Int(0),
         VmValue::Ref(object), VmValue::Int(1)}).AsInt() == 1);
    for (std::int32_t index = 1; index < 4; ++index) {
        CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(
                  vm.model.GetPrimitiveElement(object, index))) ==
              doctest::Approx(0.0F));
    }
    const auto singular = vm.FloatArray(std::vector<float>(16, 0.0F));
    CHECK(vm.CallStatic(
        "Landroid/opengl/GLU;", "gluUnProject", "(FFF[FI[FI[II[FI)I",
        {VmValue::Float(0), VmValue::Float(0), VmValue::Float(0),
         VmValue::Ref(singular), VmValue::Int(0), VmValue::Ref(projection),
         VmValue::Int(0), VmValue::Ref(viewport), VmValue::Int(0),
         VmValue::Ref(object), VmValue::Int(1)}).AsInt() == 0);
}

TEST_CASE("EGL facade publishes singleton interface hierarchy") {
    EglVm vm;
    const auto egl = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;
    const auto egl_again = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;
    CHECK(egl == egl_again);
    const auto egl10 = vm.linker.ResolveDescriptor(
        "Ljavax/microedition/khronos/egl/EGL10;");
    CHECK(vm.linker.IsAssignable(egl10, vm.model.ObjectClass(egl)));

    const auto facade_context = vm.interpreter.NewIntrinsicInstance(
        "Ljavax/microedition/khronos/egl/EGLContext;");
    const auto gl = vm.CallOn(
        facade_context, "getGL",
        "()Ljavax/microedition/khronos/opengles/GL;").ref;
    CHECK(vm.linker.IsAssignable(
        vm.linker.ResolveDescriptor(
            "Ljavax/microedition/khronos/opengles/GL10;"),
        vm.model.ObjectClass(gl)));
    CHECK(vm.context->egl.no_display.IsValid());
    CHECK(vm.context->egl.no_context.IsValid());
    CHECK(vm.context->egl.no_surface.IsValid());
}

TEST_CASE("WU-3 publishes API 19 EGL14 classes and core signatures") {
    EglVm vm;
    for (const auto descriptor : {
             "Landroid/opengl/EGL14;", "Landroid/opengl/EGLConfig;",
             "Landroid/opengl/EGLContext;", "Landroid/opengl/EGLDisplay;",
             "Landroid/opengl/EGLSurface;",
             "Landroid/opengl/EGLObjectHandle;"}) {
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
        std::pair{"eglCreateWindowSurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Ljava/lang/Object;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglCreatePbufferSurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglCreatePixmapSurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglDestroySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"},
        std::pair{"eglBindAPI", "(I)Z"},
        std::pair{"eglQueryAPI", "()I"},
        std::pair{"eglWaitClient", "()Z"},
        std::pair{"eglCreatePbufferFromClientBuffer", "(Landroid/opengl/EGLDisplay;IILandroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;"},
        std::pair{"eglSurfaceAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;II)Z"},
        std::pair{"eglBindTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
        std::pair{"eglReleaseTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
        std::pair{"eglSwapInterval", "(Landroid/opengl/EGLDisplay;I)Z"},
        std::pair{"eglCreateContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;"},
        std::pair{"eglDestroyContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z"},
        std::pair{"eglMakeCurrent", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z"},
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
    const ogplay::loader::Elf32ModuleInput module{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    VirtualFileSystem filesystem;
    auto session = AndroidGuestCallSession::Start(
        {19, "libc.so", std::span{&module, 1}, {}, 4, 3,
         1000, 1, &filesystem, {}});
    EglVm vm(InterpreterBackend::switch_dispatch, session.get());
    const auto display = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglGetDisplay",
        "(I)Landroid/opengl/EGLDisplay;", {VmValue::Int(0)}).ref;
    REQUIRE(display.IsValid());
    const auto major = vm.IntArray({-1, -1, -1});
    const auto minor = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglInitialize",
        "(Landroid/opengl/EGLDisplay;[II[II)Z",
        {VmValue::Ref(display), VmValue::Ref(major), VmValue::Int(1),
         VmValue::Ref(minor), VmValue::Int(1)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(major, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(major, 1) == 1U);
    CHECK(vm.model.GetPrimitiveElement(minor, 1) == 4U);
    const auto native_extensions = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglQueryString",
        "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;",
        {VmValue::Ref(display), VmValue::Int(0x3055)}).ref;
    CHECK(vm.interpreter.StringUtf8(native_extensions).find(
              "EGL_KHR_get_all_proc_addresses") != std::string::npos);
    const auto foreign_display = vm.interpreter.NewIntrinsicInstance(
        "Landroid/opengl/EGLDisplay;");
    CHECK_FALSE(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglQueryString",
        "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;",
        {VmValue::Ref(foreign_display), VmValue::Int(0x3055)}).ref.IsValid());
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() ==
          0x3008);
    const auto foreign_surface = vm.interpreter.NewIntrinsicInstance(
        "Landroid/opengl/EGLSurface;");
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglDestroySurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z",
        {VmValue::Ref(display), VmValue::Ref(foreign_surface)}).AsInt() == 0);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() ==
          0x300D);

    const auto attributes = vm.IntArray(
        {0, 0x3024, 8, 0x3033, 0x0001, 0x3038});
    const auto config_array_class = vm.linker.ResolveDescriptor(
        "[Landroid/opengl/EGLConfig;");
    const auto config_class = vm.linker.ResolveDescriptor(
        "Landroid/opengl/EGLConfig;");
    const auto configs = vm.model.NewObjectArray(
        config_array_class, config_class, 2);
    const auto count = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglChooseConfig",
        "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z",
        {VmValue::Ref(display), VmValue::Ref(attributes), VmValue::Int(1),
         VmValue::Ref(configs), VmValue::Int(1), VmValue::Int(1),
         VmValue::Ref(count), VmValue::Int(1)}).AsInt() == 1);
    const auto config = vm.model.GetObjectElement(configs, 1);
    REQUIRE(config.IsValid());
    CHECK(vm.model.GetPrimitiveElement(count, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(count, 1) == 1U);

    const auto pixmap = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglCreatePixmapSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;",
        {VmValue::Ref(display), VmValue::Ref(config), VmValue::Int(7),
         VmValue::Ref(vm.IntArray({0, 0x3038})), VmValue::Int(1)}).ref;
    CHECK(pixmap == vm.context->egl.egl14_no_surface);
    CHECK(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetError", "()I").AsInt() ==
          0x300A);

    const auto surface_attributes = vm.IntArray(
        {0, 0x3057, 2, 0x3056, 2, 0x3038});
    const auto surface = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglCreatePbufferSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
        {VmValue::Ref(display), VmValue::Ref(config),
         VmValue::Ref(surface_attributes), VmValue::Int(1)}).ref;
    REQUIRE(surface.IsValid());
    const auto context_attributes = vm.IntArray(
        {0, 0x3098, 3, 0x3038});
    const auto first = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglCreateContext",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
        {VmValue::Ref(display), VmValue::Ref(config),
         VmValue::Ref(vm.context->egl.egl14_no_context),
         VmValue::Ref(context_attributes), VmValue::Int(1)}).ref;
    REQUIRE(first.IsValid());
    const auto shared = vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglCreateContext",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
        {VmValue::Ref(display), VmValue::Ref(config), VmValue::Ref(first),
         VmValue::Ref(context_attributes), VmValue::Int(1)}).ref;
    REQUIRE(shared.IsValid());
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglMakeCurrent",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z",
        {VmValue::Ref(display), VmValue::Ref(surface), VmValue::Ref(surface),
         VmValue::Ref(first)}).AsInt() == 1);
    const auto thread_id = static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    CHECK(session->InvokeManagedGles(
        ogplay::gles::GlesApi::gles2, "glClearColor",
        std::array{std::bit_cast<std::uint32_t>(0.25F),
                   std::bit_cast<std::uint32_t>(0.5F),
                   std::bit_cast<std::uint32_t>(0.75F),
                   std::bit_cast<std::uint32_t>(1.0F)}, thread_id) == 0U);

    const auto vertex = vm.CallStatic(
        "Landroid/opengl/GLES20;", "glCreateShader", "(I)I",
        {VmValue::Int(0x8B31)}).AsInt();
    const auto source = vm.model.NewString(
        u"attribute vec4 aPos; uniform float uScale; void main(){ gl_Position=aPos*uScale; }");
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glShaderSource", "(ILjava/lang/String;)V",
        {VmValue::Int(vertex), VmValue::Ref(source)}));
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glCompileShader", "(I)V",
        {VmValue::Int(vertex)}));
    const auto returned_source = vm.CallStatic(
        "Landroid/opengl/GLES20;", "glGetShaderSource", "(I)Ljava/lang/String;",
        {VmValue::Int(vertex)}).ref;
    CHECK(vm.interpreter.StringUtf8(returned_source).find("uScale") != std::string::npos);
    CHECK(vm.interpreter.StringUtf8(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glGetShaderInfoLog", "(I)Ljava/lang/String;",
        {VmValue::Int(vertex)}).ref).empty());

    const auto fragment = vm.CallStatic(
        "Landroid/opengl/GLES20;", "glCreateShader", "(I)I",
        {VmValue::Int(0x8B30)}).AsInt();
    const auto fragment_source = vm.model.NewString(
        u"precision mediump float; void main(){ gl_FragColor=vec4(1.0); }");
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glShaderSource", "(ILjava/lang/String;)V",
        {VmValue::Int(fragment), VmValue::Ref(fragment_source)}));
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glCompileShader", "(I)V",
        {VmValue::Int(fragment)}));
    const auto program = vm.CallStatic(
        "Landroid/opengl/GLES20;", "glCreateProgram", "()I").AsInt();
    for (const auto shader : {vertex, fragment}) {
        static_cast<void>(vm.CallStatic(
            "Landroid/opengl/GLES20;", "glAttachShader", "(II)V",
            {VmValue::Int(program), VmValue::Int(shader)}));
    }
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glLinkProgram", "(I)V",
        {VmValue::Int(program)}));
    CHECK(vm.interpreter.StringUtf8(vm.CallStatic(
        "Landroid/opengl/GLES20;", "glGetProgramInfoLog", "(I)Ljava/lang/String;",
        {VmValue::Int(program)}).ref).empty());
    const auto size = vm.IntArray({-1});
    const auto type = vm.IntArray({-1});
    const auto active = vm.CallStatic(
        "Landroid/opengl/GLES20;", "glGetActiveUniform",
        "(II[II[II)Ljava/lang/String;",
        {VmValue::Int(program), VmValue::Int(0), VmValue::Ref(size),
         VmValue::Int(0), VmValue::Ref(type), VmValue::Int(0)}).ref;
    CHECK(vm.interpreter.StringUtf8(active) == "uScale");
    CHECK(vm.model.GetPrimitiveElement(size, 0) == 1U);
    CHECK(vm.model.GetPrimitiveElement(type, 0) == 0x1406U);
    const auto indices = vm.IntArray({-1});
    static_cast<void>(vm.CallStatic(
        "Landroid/opengl/GLES30;", "glGetUniformIndices",
        "(I[Ljava/lang/String;[II)V",
        {VmValue::Int(program), VmValue::Ref(vm.StringArray({u"uScale"})),
         VmValue::Ref(indices), VmValue::Int(0)}));
    CHECK(vm.model.GetPrimitiveElement(indices, 0) != 0xffffffffU);
    CHECK(session->InvokeManagedGles(
        ogplay::gles::GlesApi::gles2, "glClear",
        std::array{0x00004000U}, thread_id) == 0U);
    std::array<std::byte, 4> pixel_staging{};
    std::vector<std::byte> pixel_output;
    const auto read_result = session->NIO().WithTemporaryGuestMemory(
        pixel_staging, true,
        [&](const ogplay::memory::GuestAddress address) {
            return session->InvokeManagedGles(
                ogplay::gles::GlesApi::gles2, "glReadPixels",
                std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U,
                           address.Value()}, thread_id);
        }, &pixel_output);
    CHECK(read_result == 0U);
    REQUIRE(pixel_output.size() == 4U);
    CHECK(std::to_integer<std::uint8_t>(pixel_output[0]) ==
          doctest::Approx(64).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(pixel_output[1]) ==
          doctest::Approx(128).epsilon(0.04));
    CHECK(std::to_integer<std::uint8_t>(pixel_output[2]) ==
          doctest::Approx(191).epsilon(0.04));
    const auto output = vm.IntArray({-1, -1});
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglQuerySurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I[II)Z",
        {VmValue::Ref(display), VmValue::Ref(surface), VmValue::Int(0x3057),
         VmValue::Ref(output), VmValue::Int(1)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(output, 0) == UINT32_MAX);
    CHECK(vm.model.GetPrimitiveElement(output, 1) == 2U);
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglWaitGL", "()Z").AsInt() == 1);

    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglReleaseThread", "()Z").AsInt() == 1);
    for (const auto context : {shared, first}) {
        CHECK(vm.CallStatic(
            "Landroid/opengl/EGL14;", "eglDestroyContext",
            "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z",
            {VmValue::Ref(display), VmValue::Ref(context)}).AsInt() == 1);
    }
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglDestroySurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z",
        {VmValue::Ref(display), VmValue::Ref(surface)}).AsInt() == 1);
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglTerminate",
        "(Landroid/opengl/EGLDisplay;)Z",
        {VmValue::Ref(display)}).AsInt() == 1);
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglInitialize",
        "(Landroid/opengl/EGLDisplay;[II[II)Z",
        {VmValue::Ref(display), VmValue::Ref(VmObjectRef{}), VmValue::Int(0),
         VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}).AsInt() == 1);
    CHECK(vm.CallStatic(
        "Landroid/opengl/EGL14;", "eglTerminate",
        "(Landroid/opengl/EGLDisplay;)Z",
        {VmValue::Ref(display)}).AsInt() == 1);

    const auto egl10 = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;
    const auto display10 = vm.CallOn(
        egl10, "eglGetDisplay",
        "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
        {VmValue::Ref(VmObjectRef{})}).ref;
    CHECK(vm.CallOn(
        egl10, "eglInitialize",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
        {VmValue::Ref(display10), VmValue::Ref(VmObjectRef{})}).AsInt() == 1);
    const auto configs10 = vm.model.NewObjectArray(
        vm.linker.ResolveDescriptor(
            "[Ljavax/microedition/khronos/egl/EGLConfig;"),
        vm.linker.ResolveDescriptor(
            "Ljavax/microedition/khronos/egl/EGLConfig;"), 1);
    const auto count10 = vm.IntArray({0});
    CHECK(vm.CallOn(
        egl10, "eglGetConfigs",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
        {VmValue::Ref(display10), VmValue::Ref(configs10), VmValue::Int(1),
         VmValue::Ref(count10)}).AsInt() == 1);
    const auto config10 = vm.model.GetObjectElement(configs10, 0);
    REQUIRE(config10.IsValid());
    const auto surface10 = vm.CallOn(
        egl10, "eglCreatePbufferSurface",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
        {VmValue::Ref(display10), VmValue::Ref(config10),
         VmValue::Ref(vm.IntArray({0x3057, 2, 0x3056, 2, 0x3038}))}).ref;
    REQUIRE(surface10.IsValid());
    const auto context10 = vm.CallOn(
        egl10, "eglCreateContext",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
        {VmValue::Ref(display10), VmValue::Ref(config10),
         VmValue::Ref(vm.context->egl.no_context),
         VmValue::Ref(vm.IntArray({0x3098, 2, 0x3038}))}).ref;
    REQUIRE(context10.IsValid());
    const auto shared10 = vm.CallOn(
        egl10, "eglCreateContext",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
        {VmValue::Ref(display10), VmValue::Ref(config10),
         VmValue::Ref(context10),
         VmValue::Ref(vm.IntArray({0x3098, 2, 0x3038}))}).ref;
    REQUIRE(shared10.IsValid());
    CHECK(vm.CallOn(
        egl10, "eglMakeCurrent",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
        {VmValue::Ref(display10), VmValue::Ref(surface10),
         VmValue::Ref(surface10), VmValue::Ref(context10)}).AsInt() == 1);
    CHECK(vm.CallOn(
        egl10, "eglGetCurrentContext",
        "()Ljavax/microedition/khronos/egl/EGLContext;").ref == context10);
    CHECK(vm.CallOn(egl10, "eglWaitGL", "()Z").AsInt() == 1);
    CHECK(vm.CallOn(egl10, "eglReleaseThread", "()Z").AsInt() == 1);
    for (const auto context : {shared10, context10}) {
        CHECK(vm.CallOn(
            egl10, "eglDestroyContext",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z",
            {VmValue::Ref(display10), VmValue::Ref(context)}).AsInt() == 1);
    }
    CHECK(vm.CallOn(
        egl10, "eglDestroySurface",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
        {VmValue::Ref(display10), VmValue::Ref(surface10)}).AsInt() == 1);
    CHECK(vm.CallOn(
        egl10, "eglTerminate",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z",
        {VmValue::Ref(display10)}).AsInt() == 1);
    CHECK(vm.CallOn(
        egl10, "eglInitialize",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
        {VmValue::Ref(display10), VmValue::Ref(VmObjectRef{})}).AsInt() == 1);
    CHECK(vm.CallOn(
        egl10, "eglTerminate",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z",
        {VmValue::Ref(display10)}).AsInt() == 1);
    session->Stop();
}

TEST_CASE("DVM-83 publishes the API 19 Java GLES link surface") {
    EglVm vm;
    const std::array classes{
        "Landroid/opengl/GLES10;", "Landroid/opengl/GLES10Ext;",
        "Landroid/opengl/GLES11;", "Landroid/opengl/GLES11Ext;",
        "Landroid/opengl/GLES20;", "Landroid/opengl/GLES30;",
        "Landroid/opengl/GLUtils;",
        "Landroid/opengl/GLU;"};
    for (const auto descriptor : classes) {
        CAPTURE(descriptor);
        CHECK(vm.linker.FindClass(descriptor).has_value());
    }
    const auto gles20 = vm.linker.ResolveDescriptor("Landroid/opengl/GLES20;");
    CHECK(vm.linker.FindDirectMethod(gles20, "glBindTexture", "(II)V").has_value());
    CHECK(vm.linker.FindDirectMethod(gles20, "<init>", "()V").has_value());
    CHECK(vm.linker.FindDirectMethod(
        gles20, "glBufferData", "(IILjava/nio/Buffer;I)V").has_value());
    const auto gles30 = vm.linker.ResolveDescriptor("Landroid/opengl/GLES30;");
    CHECK(vm.linker.FindDirectMethod(gles30, "glBindVertexArray", "(I)V")
              .has_value());
    CHECK(vm.linker.FindDirectMethod(gles30, "glGetStringi", "(II)Ljava/lang/String;")
              .has_value());
    CHECK(vm.linker.FindDirectMethod(
        gles20, "glShaderSource", "(ILjava/lang/String;)V").has_value());
    const auto utils = vm.linker.ResolveDescriptor("Landroid/opengl/GLUtils;");
    CHECK(vm.linker.FindDirectMethod(
        utils, "texImage2D", "(IILandroid/graphics/Bitmap;I)V").has_value());
    CHECK(vm.linker.Class(vm.linker.ResolveDescriptor("Landroid/opengl/GLES11;")).super ==
          vm.linker.ResolveDescriptor("Landroid/opengl/GLES10;"));

    const auto bitmap = vm.interpreter.NewIntrinsicInstance(
        "Landroid/graphics/Bitmap;");
    vm.context->bitmaps.emplace(bitmap.Value(),
        DexVmAndroidContext::BitmapState{1, 1, {0xff112233U}, false});
    CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getInternalFormat",
                        "(Landroid/graphics/Bitmap;)I",
                        {VmValue::Ref(bitmap)}).AsInt() == 0x1908);
    CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getType",
                        "(Landroid/graphics/Bitmap;)I",
                        {VmValue::Ref(bitmap)}).AsInt() == 0x1401);
    for (const auto [config, format, type] : {
             std::array<std::int32_t, 3>{1, 0x1906, 0x1401},
             std::array<std::int32_t, 3>{3, 0x1907, 0x8363},
             std::array<std::int32_t, 3>{4, 0x1908, 0x8033}}) {
        vm.context->bitmaps.at(bitmap.Value()).config = config;
        CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getInternalFormat",
                            "(Landroid/graphics/Bitmap;)I",
                            {VmValue::Ref(bitmap)}).AsInt() == format);
        CHECK(vm.CallStatic("Landroid/opengl/GLUtils;", "getType",
                            "(Landroid/graphics/Bitmap;)I",
                            {VmValue::Ref(bitmap)}).AsInt() == type);
    }
    const auto error = vm.CallStatic("Landroid/opengl/GLUtils;",
        "getEGLErrorString", "(I)Ljava/lang/String;",
        {VmValue::Int(0x3004)}).ref;
    CHECK(vm.interpreter.StringUtf8(error) == "EGL_BAD_ATTRIBUTE");
}

TEST_CASE("EGL facade teardown retirement fails swap without entering graphics") {
    EglVm vm;
    const auto egl = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;

    RetireGuestEglSurface(*vm.context);
    CHECK(vm.context->egl.surface_retired.load());
    CHECK(vm.context->egl.pace_shutdown);
    CHECK(vm.CallOn(
              egl, "eglSwapBuffers",
              "(Ljavax/microedition/khronos/egl/EGLDisplay;"
              "Ljavax/microedition/khronos/egl/EGLSurface;)Z",
              {VmValue::Ref(VmObjectRef{}),
               VmValue::Ref(VmObjectRef{})})
              .AsInt() == 0);
    CHECK(vm.CallOn(egl, "eglGetError", "()I").AsInt() == 0x300B);
    CHECK(vm.CallOn(egl, "eglGetError", "()I").AsInt() == 0x3000);
}

TEST_CASE("EGL facade performs two-pass config selection and context state") {
    EglVm vm;
    const auto egl = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;
    const auto display = vm.CallOn(
        egl, "eglGetDisplay",
        "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
        {VmValue::Ref(VmObjectRef{})}).ref;
    REQUIRE(display.IsValid());
    const auto versions = vm.IntArray({0, 0});
    CHECK(vm.CallOn(egl, "eglInitialize",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(versions)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(versions, 0) == 1);
    CHECK(vm.model.GetPrimitiveElement(versions, 1) == 4);

    const auto attributes = vm.IntArray({0x3024, 5, 0x3023, 6,
                                         0x3022, 5, 0x3021, 0,
                                         0x3025, 0, 0x3026, 0,
                                         0x3033, 0x04, 0x3038});
    const auto count = vm.IntArray({0});
    CHECK(vm.CallOn(egl, "eglChooseConfig",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(attributes),
                     VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Ref(count)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(count, 0) == 1);

    const auto config_array_class = vm.linker.ResolveDescriptor(
        "[Ljavax/microedition/khronos/egl/EGLConfig;");
    const auto config_class = vm.linker.ResolveDescriptor(
        "Ljavax/microedition/khronos/egl/EGLConfig;");
    const auto configs = vm.model.NewObjectArray(config_array_class, config_class, 1);
    CHECK(vm.CallOn(egl, "eglGetConfigs",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(configs),
                     VmValue::Int(1), VmValue::Ref(count)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(count, 0) == 1);
    CHECK(vm.CallOn(egl, "eglChooseConfig",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(attributes),
                     VmValue::Ref(configs), VmValue::Int(1), VmValue::Ref(count)}).AsInt() == 1);
    const auto config = vm.model.GetObjectElement(configs, 0);
    REQUIRE(config.IsValid());
    const auto projected = vm.IntArray({-1});
    CHECK(vm.CallOn(egl, "eglGetConfigAttrib",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(config),
                     VmValue::Int(0x3024), VmValue::Ref(projected)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(projected, 0) == 5);
    const auto context_attributes = vm.IntArray({12440, 2, 0x3038});
    const auto context = vm.CallOn(
        egl, "eglCreateContext",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
        {VmValue::Ref(display), VmValue::Ref(config),
         VmValue::Ref(vm.context->egl.no_context),
         VmValue::Ref(context_attributes)}).ref;
    REQUIRE(context.IsValid());
    CHECK(vm.context->egl.contexts.at(context.Value()) == 2);

    const auto query = vm.IntArray({0});
    CHECK(vm.CallOn(egl, "eglQueryContext",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(context),
                     VmValue::Int(0x3098), VmValue::Ref(query)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(query, 0) == 2);
    const auto version = vm.CallOn(
        egl, "eglQueryString",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;",
        {VmValue::Ref(display), VmValue::Int(0x3054)}).ref;
    CHECK(vm.interpreter.StringUtf8(version) == "1.4 OGPlay");
    const auto extensions = vm.CallOn(
        egl, "eglQueryString",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;",
        {VmValue::Ref(display), VmValue::Int(0x3055)}).ref;
    CHECK(vm.interpreter.StringUtf8(extensions).find(
              "EGL_KHR_get_all_proc_addresses") != std::string::npos);

    CHECK(vm.CallOn(egl, "eglGetCurrentContext",
                    "()Ljavax/microedition/khronos/egl/EGLContext;").ref ==
          vm.context->egl.no_context);
    vm.context->egl.current_context = context;
    vm.context->egl.current_thread = std::this_thread::get_id();
    CHECK(vm.CallOn(egl, "eglGetCurrentContext",
                    "()Ljavax/microedition/khronos/egl/EGLContext;").ref == context);
    vm.context->egl.current_context = VmObjectRef{};
    vm.context->egl.current_thread.reset();
    CHECK(vm.CallOn(egl, "eglReleaseThread", "()Z").AsInt() == 1);

    vm.context->surface_width = 800;
    vm.context->surface_height = 480;
    vm.context->egl.window_surface = vm.interpreter.NewIntrinsicInstance(
        "Ljavax/microedition/khronos/egl/EGLSurface;");
    CHECK(vm.CallOn(egl, "eglQuerySurface",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;I[I)Z",
                    {VmValue::Ref(display),
                     VmValue::Ref(vm.context->egl.window_surface),
                     VmValue::Int(0x3057), VmValue::Ref(query)}).AsInt() == 1);
    CHECK(vm.model.GetPrimitiveElement(query, 0) == 800);
}

TEST_CASE("EGL facade reports unknown config attributes through EGL error") {
    EglVm vm;
    const auto egl = vm.CallStatic(
        "Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
        "()Ljavax/microedition/khronos/egl/EGL;").ref;
    const auto display = vm.CallOn(
        egl, "eglGetDisplay",
        "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
        {VmValue::Ref(VmObjectRef{})}).ref;
    static_cast<void>(vm.CallOn(
        egl, "eglInitialize",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
        {VmValue::Ref(display), VmValue::Ref(VmObjectRef{})}));
    const auto bad = vm.IntArray({0x7fffffff, 1, 0x3038});
    const auto count = vm.IntArray({99});
    CHECK(vm.CallOn(egl, "eglChooseConfig",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(bad), VmValue::Ref(VmObjectRef{}),
                     VmValue::Int(0), VmValue::Ref(count)}).AsInt() == 0);
    CHECK(vm.CallOn(egl, "eglGetError", "()I").AsInt() == 0x3004);
    CHECK(vm.CallOn(egl, "eglGetError", "()I").AsInt() == 0x3000);
    CHECK_FALSE(vm.ledger.Unimplemented().empty());
}
