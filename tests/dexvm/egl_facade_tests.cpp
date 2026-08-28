#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"

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

    EglVm()
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
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, {}) {
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
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome =
            interpreter.Call(linker.Class(klass).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
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
};

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

TEST_CASE("DVM-83 publishes the API 19 Java GLES link surface") {
    EglVm vm;
    const std::array classes{
        "Landroid/opengl/GLES10;", "Landroid/opengl/GLES10Ext;",
        "Landroid/opengl/GLES11;", "Landroid/opengl/GLES11Ext;",
        "Landroid/opengl/GLES20;", "Landroid/opengl/GLUtils;",
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

    const auto attributes = vm.IntArray({0x3024, 8, 0x3025, 24,
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
    CHECK(vm.CallOn(egl, "eglChooseConfig",
                    "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(attributes),
                     VmValue::Ref(configs), VmValue::Int(1), VmValue::Ref(count)}).AsInt() == 1);
    const auto config = vm.model.GetObjectElement(configs, 0);
    REQUIRE(config.IsValid());
    const auto context_attributes = vm.IntArray({12440, 2, 0x3038});
    const auto context = vm.CallOn(
        egl, "eglCreateContext",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
        {VmValue::Ref(display), VmValue::Ref(config),
         VmValue::Ref(vm.context->egl.no_context),
         VmValue::Ref(context_attributes)}).ref;
    REQUIRE(context.IsValid());
    CHECK(vm.context->egl.contexts.at(context.Value()) == 2);
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
