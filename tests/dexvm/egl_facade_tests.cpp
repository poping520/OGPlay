#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
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
        AndroidGuestCallSession* session = nullptr,
        const char* dex_fixture = nullptr,
        const std::vector<IntrinsicClassDecl>& extras = {})
        : interpreter([this, dex_fixture, &extras]() -> DexClassLinker& {
              linker.RegisterIntrinsics(CoreIntrinsicCatalog());
              auto android = AndroidIntrinsicCatalog(context);
              android.push_back(std::move(
                  IntrinsicClassBuilder::Class(
                      "Lfixture/GlView;", "Landroid/opengl/GLSurfaceView;"))
                                    .Build());
              android.push_back(std::move(
                  IntrinsicClassBuilder::Class(
                      "Lfixture/EglPolicies;", "Ljava/lang/Object;",
                      {"Landroid/opengl/GLSurfaceView$EGLContextFactory;",
                       "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"}))
                                    .Build());
              linker.RegisterIntrinsics(std::move(android));
              linker.RegisterIntrinsics(extras);
              ogplay::test::RegisterBootDex(linker);
              if (dex_fixture != nullptr) {
                  const auto path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) +
                                    "/" + dex_fixture;
                  std::ifstream stream(path, std::ios::binary);
                  REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
                  linker.RegisterDex(std::vector<std::uint8_t>(
                      std::istreambuf_iterator<char>(stream),
                      std::istreambuf_iterator<char>()));
              }
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

TEST_CASE("GLSurfaceView subclasses inherit a stable holder and initialized View context") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        EglVm vm(backend);
        const auto view = vm.interpreter.NewIntrinsicInstance("Lfixture/GlView;");
        const auto context = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        CHECK(vm.linker.IsAssignable(
            vm.linker.ResolveDescriptor("Landroid/view/SurfaceView;"),
            vm.model.ObjectClass(view)));
        vm.CallStatic("Landroid/opengl/GLSurfaceView;", "<init>",
                      "(Landroid/content/Context;)V",
                      {VmValue::Ref(view), VmValue::Ref(context)});
        CHECK(vm.CallOn(view, "getContext", "()Landroid/content/Context;").ref == context);
        CHECK(FindViewUiNode(*vm.context, view.Value()).has_value());
        const auto holder = vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        REQUIRE(holder.IsValid());
        CHECK(vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref == holder);
        vm.CallOn(holder, "setFormat", "(I)V", {VmValue::Int(-3)});
        CHECK(vm.CallOn(holder, "getSurface", "()Landroid/view/Surface;").ref.IsValid());
    }
}

TEST_CASE("SurfaceView null-attribute constructors initialize DEX subclasses and reject unsupported inputs") {
    constexpr auto subclass = "Lfixture/SurfaceSubclass;";
    constexpr auto two_args =
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        EglVm vm(backend, nullptr, "surface_view_ctor.dex");
        const auto type = vm.linker.ResolveDescriptor(subclass);
        const auto context = vm.interpreter.NewIntrinsicInstance(
            "Landroid/app/Application;");
        const auto construct = [&](const VmObjectRef view,
                                   const char* signature,
                                   std::vector<VmValue> arguments) {
            const auto method = vm.linker.FindDirectMethod(
                type, "<init>", signature);
            REQUIRE(method.has_value());
            arguments.insert(arguments.begin(), VmValue::Ref(view));
            return vm.interpreter.Call(*method, arguments);
        };
        const auto single = vm.interpreter.NewIntrinsicInstance(subclass);
        const auto paired = vm.interpreter.NewIntrinsicInstance(subclass);
        REQUIRE_FALSE(construct(single, "(Landroid/content/Context;)V",
                                {VmValue::Ref(context)}).exception.IsValid());
        REQUIRE_FALSE(construct(paired, two_args,
                                {VmValue::Ref(context),
                                 VmValue::Ref(VmObjectRef{})}).exception.IsValid());
        for (const auto view : {single, paired}) {
            CHECK(vm.CallOn(view, "getContext", "()Landroid/content/Context;")
                      .ref == context);
            const auto node = FindViewUiNode(*vm.context, view.Value());
            REQUIRE(node.has_value());
            CHECK_FALSE(vm.context->ui_tree.IsAttached(*node));
            const auto holder = vm.CallOn(
                view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
            REQUIRE(holder.IsValid());
            CHECK(vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;")
                      .ref == holder);
            CHECK_FALSE(vm.context->active_surface_holders.contains(holder.Value()));
        }
        CHECK(FindViewUiNode(*vm.context, single.Value()) !=
              FindViewUiNode(*vm.context, paired.Value()));
        CHECK(vm.context->surface_holders.at(single.Value()) !=
              vm.context->surface_holders.at(paired.Value()));

        const auto node_count = vm.context->ui_tree.Size();
        const auto missing_context = vm.interpreter.NewIntrinsicInstance(subclass);
        const auto null_result = construct(
            missing_context, two_args,
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_result.exception.IsValid());
        CHECK(vm.linker.Class(null_result.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
        CHECK_FALSE(FindViewUiNode(*vm.context, missing_context.Value()).has_value());

        const auto with_attrs = vm.interpreter.NewIntrinsicInstance(subclass);
        const auto attrs = vm.interpreter.NewIntrinsicInstance(
            "Lfixture/SurfaceAttributes;");
        CHECK(vm.linker.IsAssignable(
            vm.linker.ResolveDescriptor("Landroid/util/AttributeSet;"),
            vm.model.ObjectClass(attrs)));
        const auto attr_result = construct(
            with_attrs, two_args, {VmValue::Ref(context), VmValue::Ref(attrs)});
        REQUIRE(attr_result.exception.IsValid());
        CHECK(vm.linker.Class(attr_result.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK_FALSE(FindViewUiNode(*vm.context, with_attrs.Value()).has_value());
        CHECK(vm.context->ui_tree.Size() == node_count);
        const auto hits = vm.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.view_xml_attributes");
        CHECK(hits[0].count == 1);
    }
}

TEST_CASE("SurfaceHolder frame publishes before callbacks and retains per-holder identity across generations") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        struct Observation {
            int phase;
            VmObjectRef holder, frame;
            int width, height;
        };
        std::vector<Observation> observations;
        EglVm* active = nullptr;
        auto callback = IntrinsicClassBuilder::Class(
            "Lfixture/FrameCallback;", "Ljava/lang/Object;",
            {"Landroid/view/SurfaceHolder$Callback;"});
        for (const int phase : {0, 1, 2}) {
            callback.VirtualMethod(
                phase == 0 ? "surfaceCreated" : phase == 1 ? "surfaceChanged" : "surfaceDestroyed",
                phase == 1 ? "(Landroid/view/SurfaceHolder;III)V" : "(Landroid/view/SurfaceHolder;)V",
                [&, phase](IntrinsicContext& call) {
                    const auto holder = call.arguments[0].ref;
                    const auto frame = active->CallStatic(
                        "Lfixture/SurfaceFrameProbe;", "read",
                        "(Landroid/view/SurfaceHolder;)Landroid/graphics/Rect;",
                        {VmValue::Ref(holder)}).ref;
                    const auto width = active->CallOn(frame, "width", "()I").AsInt();
                    const auto height = active->CallOn(frame, "height", "()I").AsInt();
                    if (phase == 1) {
                        CHECK(width == call.arguments[2].AsInt());
                        CHECK(height == call.arguments[3].AsInt());
                    }
                    observations.push_back({phase, holder, frame, width, height});
                    return VmValue::Void();
                });
        }
        EglVm vm(backend, nullptr, "surface_frame.dex", {std::move(callback).Build()});
        active = &vm;
        const auto read = [&](VmObjectRef holder) {
            return vm.CallStatic("Lfixture/SurfaceFrameProbe;", "read",
                "(Landroid/view/SurfaceHolder;)Landroid/graphics/Rect;",
                {VmValue::Ref(holder)}).ref;
        };
        const auto shape = [&](VmObjectRef frame, int width, int height) {
            CHECK(vm.CallOn(frame, "width", "()I").AsInt() == width);
            CHECK(vm.CallOn(frame, "height", "()I").AsInt() == height);
            for (const auto* name : {"left", "top"}) {
                CHECK(vm.CallStatic("Lfixture/SurfaceFrameProbe;", name,
                    "(Landroid/graphics/Rect;)I", {VmValue::Ref(frame)}).AsInt() == 0);
            }
        };
        const auto make_view = [&] {
            const auto view = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
            const auto owner = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
            vm.CallStatic("Landroid/view/SurfaceView;", "<init>",
                          "(Landroid/content/Context;)V",
                          {VmValue::Ref(view), VmValue::Ref(owner)});
            return view;
        };
        const auto view = make_view();
        const auto holder = vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto frame = read(holder);
        REQUIRE(frame.IsValid());
        CHECK(read(holder) == frame);
        shape(frame, 0, 0);
        const auto listener = vm.interpreter.NewIntrinsicInstance("Lfixture/FrameCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V",
                  {VmValue::Ref(listener)});
        const auto node = FindViewUiNode(*vm.context, view.Value());
        REQUIRE(node.has_value());
        // A detached holder must not acquire the managed window dimensions.
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context,
                                                    SurfaceHolderPhase::created).has_value());
        CHECK(observations.empty());
        shape(frame, 0, 0);
        vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), *node);
        REQUIRE_FALSE(AttachSurfaceViewSubtree(vm.interpreter, *vm.context, *node).has_value());
        REQUIRE(observations.size() == 2);
        CHECK(observations[0].phase == 0);
        CHECK(observations[0].frame == frame);
        CHECK(observations[0].width == static_cast<int>(vm.context->surface_width));
        CHECK(observations[0].height == static_cast<int>(vm.context->surface_height));
        CHECK(observations[1].phase == 1);
        // A late-created holder publishes geometry without replaying callbacks.
        const auto late = make_view();
        const auto late_node = FindViewUiNode(*vm.context, late.Value());
        REQUIRE(late_node.has_value());
        vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), *late_node);
        const auto late_holder = vm.CallOn(late, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto late_frame = read(late_holder);
        CHECK(late_frame != frame);
        shape(late_frame, static_cast<int>(vm.context->surface_width),
                          static_cast<int>(vm.context->surface_height));
        CHECK(observations.size() == 2);
        vm.context->surface_width = 1024;
        vm.context->surface_height = 600;
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context,
                                                    SurfaceHolderPhase::changed).has_value());
        CHECK(read(holder) == frame);
        shape(frame, 1024, 600);
        shape(late_frame, 1024, 600);
        vm.context->surface_width = 0x80000000U;
        CHECK_THROWS_AS(static_cast<void>(DispatchSurfaceHolderCallbacks(
            vm.interpreter, *vm.context, SurfaceHolderPhase::changed)), VmJavaThrow);
        shape(frame, 1024, 600);
        vm.context->surface_width = 1024;
        REQUIRE_FALSE(DetachSurfaceViewSubtree(vm.interpreter, *vm.context, *node).has_value());
        vm.context->ui_tree.Detach(*node);
        CHECK(observations.back().phase == 2);
        shape(frame, 1024, 600);
        vm.context->surface_width = 640;
        vm.context->surface_height = 360;
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context,
                                                    SurfaceHolderPhase::changed).has_value());
        shape(frame, 1024, 600);
        shape(late_frame, 640, 360);
        vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), *node);
        REQUIRE_FALSE(AttachSurfaceViewSubtree(vm.interpreter, *vm.context, *node).has_value());
        CHECK(read(holder) == frame);
        shape(frame, 640, 360);
        // Retiring registrations does not clear fields on retained guest objects.
        REQUIRE_FALSE(RetireSurfaceHolderGeneration(vm.interpreter, *vm.context).has_value());
        vm.context->surface_width = 320;
        vm.context->surface_height = 240;
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter, *vm.context,
                                                    SurfaceHolderPhase::created).has_value());
        CHECK(read(holder) == frame);
        shape(frame, 640, 360);
        const auto replacement = vm.CallOn(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        CHECK(replacement != holder);
        CHECK(read(replacement) != frame);
        shape(read(replacement), 320, 240);
    }
}

TEST_CASE("SurfaceHolder frame follows its guest field in GC without a global root") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        EglVm vm(backend);
        const auto holder = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceHolder$Impl;");
        const auto frame = vm.CallOn(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref;
        vm.interpreter.SetGcIntegration({{}, {}, [holder](const VmRootVisitor& visit) { visit(holder); }});
        CHECK(vm.interpreter.MarkReachable().IsMarked(frame));
        static_cast<void>(vm.interpreter.CollectGarbage());
        CHECK(vm.model.IsValidRef(frame));
        CHECK(vm.CallOn(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref == frame);
        vm.interpreter.SetGcIntegration({});
        CHECK_FALSE(vm.interpreter.MarkReachable().IsMarked(holder));
        CHECK_FALSE(vm.interpreter.MarkReachable().IsMarked(frame));
        static_cast<void>(vm.interpreter.CollectGarbage());
        CHECK_FALSE(vm.model.IsValidRef(holder));
        CHECK_FALSE(vm.model.IsValidRef(frame));
    }
}

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

TEST_CASE("GLSurfaceView preserves the EGL pause preference per instance before renderer setup") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        EglVm vm(backend);
        const auto first = vm.interpreter.NewIntrinsicInstance("Lfixture/GlView;");
        const auto second = vm.interpreter.NewIntrinsicInstance("Lfixture/GlView;");
        const auto preference = [&](VmObjectRef view) {
            return vm.CallOn(view, "getPreserveEGLContextOnPause", "()Z").AsInt();
        };
        const auto set = [&](VmObjectRef view, int value) {
            vm.CallOn(view, "setPreserveEGLContextOnPause", "(Z)V", {VmValue::Int(value)});
        };
        CHECK(preference(first) == 0);
        CHECK(preference(second) == 0);
        set(first, 1);
        CHECK(preference(first) == 1);
        CHECK(preference(second) == 0);
        set(second, 1);
        set(first, 0);
        CHECK(preference(first) == 0);
        CHECK(preference(second) == 1);
        set(second, 1);
        CHECK(preference(second) == 1);
        CHECK_FALSE(vm.context->renderer.IsValid());
        for (const auto& [name, signature] : {
                 std::pair{"setPreserveEGLContextOnPause", "(Z)V"},
                 std::pair{"getPreserveEGLContextOnPause", "()Z"}}) {
            const auto type = vm.model.ObjectClass(first);
            const auto slot = vm.linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value());
            CHECK(vm.linker.Method(vm.linker.Class(type).vtable[*slot]).overridable);
        }
    }
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
        {19, "libc.so", std::span{&module, 1},
         {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 3,
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
        DexVmAndroidContext::BitmapState{1, 1, std::make_shared<ogplay::runtime::BitmapPixels>(1, 1, 5, std::vector<std::uint32_t>{0xff112233U}), false});
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
    CHECK(vm.model.GetPrimitiveElement(projected, 0) == 8);
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

TEST_CASE("BND49 Java EGL10 and EGL14 share stable RGB config facts") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto libc = MinimalLibcElf();
        const ogplay::loader::Elf32ModuleInput module{
            "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
        VirtualFileSystem filesystem;
        auto session = AndroidGuestCallSession::Start(
            {19, "libc.so", std::span{&module, 1},
             {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 3,
             1000, 1, &filesystem, {}});
        EglVm vm(backend, session.get());
        const auto egl = vm.CallStatic("Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
                                      "()Ljavax/microedition/khronos/egl/EGL;").ref;
        const auto display = vm.CallOn(egl, "eglGetDisplay",
            "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
            {VmValue::Ref(VmObjectRef{})}).ref;
        const auto output = vm.IntArray({0});
        REQUIRE(vm.CallOn(egl, "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
                          {VmValue::Ref(display), VmValue::Ref(VmObjectRef{})}).AsInt() == 1);
        const auto minimum = vm.IntArray({0x3024, 4, 0x3023, 4, 0x3022, 4, 0x3038});
        const auto configs = vm.model.NewObjectArray(vm.linker.ResolveDescriptor("[Ljavax/microedition/khronos/egl/EGLConfig;"),
            vm.linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;"), 2);
        REQUIRE(vm.CallOn(egl, "eglChooseConfig", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
            {VmValue::Ref(display), VmValue::Ref(minimum), VmValue::Ref(configs), VmValue::Int(2), VmValue::Ref(output)}).AsInt() == 1);
        REQUIRE(vm.model.GetPrimitiveElement(output, 0) == 2);
        for (int index = 0; index < 2; ++index) {
            const auto config = vm.model.GetObjectElement(configs, index);
            for (const auto [attribute, value] : {std::pair{0x3024, 8}, {0x3021, index == 0 ? 8 : 0}}) {
                REQUIRE(vm.CallOn(egl, "eglGetConfigAttrib", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                    {VmValue::Ref(display), VmValue::Ref(config), VmValue::Int(attribute), VmValue::Ref(output)}).AsInt() == 1);
                CHECK(vm.model.GetPrimitiveElement(output, 0) == static_cast<std::uint32_t>(value));
            }
        }
        const auto display14 = vm.CallStatic("Landroid/opengl/EGL14;", "eglGetDisplay", "(I)Landroid/opengl/EGLDisplay;", {VmValue::Int(0)}).ref;
        REQUIRE(vm.CallStatic("Landroid/opengl/EGL14;", "eglInitialize", "(Landroid/opengl/EGLDisplay;[II[II)Z",
            {VmValue::Ref(display14), VmValue::Ref(output), VmValue::Int(0), VmValue::Ref(output), VmValue::Int(0)}).AsInt() == 1);
        const auto configs14 = vm.model.NewObjectArray(vm.linker.ResolveDescriptor("[Landroid/opengl/EGLConfig;"),
            vm.linker.ResolveDescriptor("Landroid/opengl/EGLConfig;"), 2);
        REQUIRE(vm.CallStatic("Landroid/opengl/EGL14;", "eglChooseConfig", "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z",
            {VmValue::Ref(display14), VmValue::Ref(minimum), VmValue::Int(0), VmValue::Ref(configs14), VmValue::Int(0), VmValue::Int(2), VmValue::Ref(output), VmValue::Int(0)}).AsInt() == 1);
        REQUIRE(vm.model.GetPrimitiveElement(output, 0) == 2);
        for (int index = 0; index < 2; ++index) {
            REQUIRE(vm.CallStatic("Landroid/opengl/EGL14;", "eglGetConfigAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Z",
                {VmValue::Ref(display14), VmValue::Ref(vm.model.GetObjectElement(configs14, index)), VmValue::Int(0x3021), VmValue::Ref(output), VmValue::Int(0)}).AsInt() == 1);
            CHECK(vm.model.GetPrimitiveElement(output, 0) == (index == 0 ? 8U : 0U));
        }
    }
}

TEST_CASE("DVM225 BootDex GL interfaces clear pixels and retain context-local direct buffers") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto libc = MinimalLibcElf();
        const ogplay::loader::Elf32ModuleInput module{
            "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
        VirtualFileSystem filesystem;
        auto session = AndroidGuestCallSession::Start(
            {19, "libc.so", std::span{&module, 1},
             {kNativeRenderer, ogplay::gles::AngleDevice::hardware}, 4, 3,
             1000, 1, &filesystem, {}});
        EglVm vm(backend, session.get(), "gl_interface.dex");
        vm.interpreter.SetNioRuntime(&session->NIO());
        const auto ref = VmValue::Ref;
        const auto egl = vm.CallStatic("Ljavax/microedition/khronos/egl/EGLContext;", "getEGL",
            "()Ljavax/microedition/khronos/egl/EGL;").ref;
        const auto display = vm.CallOn(egl, "eglGetDisplay",
            "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;", {ref(VmObjectRef{})}).ref;
        CHECK(vm.CallOn(egl, "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
            {ref(display), ref(VmObjectRef{})}).AsInt() == 1);
        const auto config_class = vm.linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGLConfig;");
        const auto configs = vm.model.NewObjectArray(vm.linker.ResolveDescriptor(
            "[Ljavax/microedition/khronos/egl/EGLConfig;"), config_class, 1);
        const auto count = vm.IntArray({0});
        REQUIRE(vm.CallOn(egl, "eglChooseConfig",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
            {ref(display), ref(vm.IntArray({0x3021, 8, 0x3038})), ref(configs), VmValue::Int(1), ref(count)}).AsInt() == 1);
        const auto config = vm.model.GetObjectElement(configs, 0);
        const auto make_context = [&] {
            return vm.CallOn(egl, "eglCreateContext",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
                {ref(display), ref(config), ref(vm.context->egl.no_context), ref(vm.IntArray({0x3098, 2, 0x3038}))}).ref;
        };
        const auto first = make_context();
        const auto second = make_context();
        REQUIRE(first != vm.context->egl.no_context);
        REQUIRE(second != vm.context->egl.no_context);
        const auto gl = vm.CallOn(first, "getGL", "()Ljavax/microedition/khronos/opengles/GL;").ref;
        const auto other = vm.CallOn(second, "getGL", "()Ljavax/microedition/khronos/opengles/GL;").ref;
        CHECK(gl != other);
        CHECK(vm.CallOn(first, "getGL", "()Ljavax/microedition/khronos/opengles/GL;").ref == gl);
        CHECK(vm.linker.Class(vm.model.ObjectClass(gl)).descriptor == "Lcom/google/android/gles_jni/GLImpl;");
        CHECK_FALSE(vm.linker.Class(vm.model.ObjectClass(gl)).is_intrinsic);
        for (const auto* name : {"GL", "GL10", "GL10Ext", "GL11", "GL11Ext", "GL11ExtensionPack"}) {
            const auto type = vm.linker.ResolveDescriptor(std::string("Ljavax/microedition/khronos/opengles/") + name + ";");
            CHECK_FALSE(vm.linker.Class(type).is_intrinsic);
            CHECK(vm.linker.IsAssignable(type, vm.model.ObjectClass(gl)));
        }
        const auto surface = vm.CallOn(egl, "eglCreatePbufferSurface",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
            {ref(display), ref(config), ref(vm.IntArray({0x3057, 2, 0x3056, 2, 0x3038}))}).ref;
        REQUIRE(surface != vm.context->egl.no_surface);
        const auto bind = [&](VmObjectRef context) {
            CHECK(vm.CallOn(egl, "eglMakeCurrent",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                {ref(display), ref(surface), ref(surface), ref(context)}).AsInt() == 1);
        };
        bind(first);
        vm.CallStatic("Lfixture/GlInterfaceProbe;", "clear", "(Ljavax/microedition/khronos/opengles/GL10;FFFF)V",
            {ref(gl), VmValue::Float(.25F), VmValue::Float(.5F), VmValue::Float(.75F), VmValue::Float(1)});
        std::array<std::byte, 4> staging{};
        std::vector<std::byte> pixel;
        const auto read_pixel = [&] {
            static_cast<void>(session->NIO().WithTemporaryGuestMemory(staging, true, [&](ogplay::memory::GuestAddress address) {
                return session->InvokeManagedGles(ogplay::gles::GlesApi::gles2, "glReadPixels",
                    std::array{0U, 0U, 1U, 1U, 0x1908U, 0x1401U, address.Value()}, 1U);
            }, &pixel));
        };
        read_pixel();
        REQUIRE(pixel.size() == 4U);
        CHECK(std::to_integer<int>(pixel[0]) == doctest::Approx(64).epsilon(.04));
        CHECK(std::to_integer<int>(pixel[1]) == doctest::Approx(128).epsilon(.04));
        CHECK(std::to_integer<int>(pixel[2]) == doctest::Approx(191).epsilon(.04));
        CHECK(vm.CallOn(gl, "glGetError", "()I").AsInt() == 0);
        const auto buffer = vm.CallStatic("Ljava/nio/ByteBuffer;", "allocateDirect", "(I)Ljava/nio/ByteBuffer;", {VmValue::Int(36)}).ref;
        vm.CallStatic("Lfixture/GlInterfaceProbe;", "pointer", "(Ljavax/microedition/khronos/opengles/GL10;Ljava/nio/Buffer;)V", {ref(gl), ref(buffer)});
        CHECK(vm.CallOn(gl, "glGetError", "()I").AsInt() == 0);
        const auto field = vm.linker.FindFieldRecursive(vm.model.ObjectClass(gl), "_vertexPointer", "Ljava/nio/Buffer;");
        REQUIRE(field.has_value());
        CHECK(vm.model.InstanceSlots(gl)[vm.linker.Field(*field).slot].bits == buffer.Value());
        CHECK_FALSE((vm.model.InstanceSlots(other)[vm.linker.Field(*field).slot].bits != 0U));
        vm.interpreter.SetGcIntegration({{}, {}, [first, second, egl, display, surface](const VmRootVisitor& visit) { visit(first); visit(second); visit(egl); visit(display); visit(surface); }});
        CHECK(vm.interpreter.MarkReachable().IsMarked(gl));
        CHECK(vm.interpreter.MarkReachable().IsMarked(buffer));
        static_cast<void>(vm.interpreter.CollectGarbage());
        CHECK(vm.model.IsValidRef(buffer));
        const auto identity = vm.model.ToIdentity(buffer);
        session->NIO().SetOrder(identity, NioByteOrder::little_endian);
        const std::array vertices{-1.F, -1.F, 0.F, 3.F, -1.F, 0.F, -1.F, 3.F, 0.F};
        for (std::size_t i = 0; i < vertices.size(); ++i)
            session->NIO().PutScalar(identity, NioElementKind::float_value,
                static_cast<std::int32_t>(i * 4U), std::bit_cast<std::uint32_t>(vertices[i]));
        vm.CallOn(gl, "glViewport", "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(2), VmValue::Int(2)});
        vm.CallOn(gl, "glColor4f", "(FFFF)V", {VmValue::Float(1), VmValue::Float(0), VmValue::Float(0), VmValue::Float(1)});
        vm.CallOn(gl, "glEnableClientState", "(I)V", {VmValue::Int(0x8074)});
        vm.CallOn(gl, "glDrawArrays", "(III)V", {VmValue::Int(4), VmValue::Int(0), VmValue::Int(3)});
        CHECK(vm.CallOn(gl, "glGetError", "()I").AsInt() == 0);
        read_pixel();
        CHECK(std::to_integer<int>(pixel[0]) == 255);
        CHECK(std::to_integer<int>(pixel[1]) == 0);
        CHECK(std::to_integer<int>(pixel[2]) == 0);
        const auto heap = vm.CallStatic("Ljava/nio/ByteBuffer;", "allocate", "(I)Ljava/nio/ByteBuffer;", {VmValue::Int(36)}).ref;
        vm.context->target_sdk_version = 19;
        auto failure = vm.CallOnOutcome(gl, "glVertexPointer", "(IIILjava/nio/Buffer;)V",
            {VmValue::Int(3), VmValue::Int(0x1406), VmValue::Int(0), ref(heap)});
        CHECK(vm.linker.Class(failure.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        vm.context->target_sdk_version = 3;
        failure = vm.CallOnOutcome(gl, "glVertexPointer", "(IIILjava/nio/Buffer;)V",
            {VmValue::Int(3), VmValue::Int(0x1406), VmValue::Int(0), ref(heap)});
        CHECK(vm.linker.Class(failure.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(vm.model.InstanceSlots(gl)[vm.linker.Field(*field).slot].bits == buffer.Value());
        CHECK(vm.CallStatic("Lcom/google/android/gles_jni/GLImpl;", "nativeAllowIndirectBuffers", "()Z").AsInt() == 1);
        vm.context->target_sdk_version = 19;
        CHECK(vm.CallStatic("Lcom/google/android/gles_jni/GLImpl;", "nativeAllowIndirectBuffers", "()Z").AsInt() == 0);
        const auto null_error = vm.CallOnOutcome(gl, "glVertexPointer", "(IIILjava/nio/Buffer;)V",
            {VmValue::Int(3), VmValue::Int(0x1406), VmValue::Int(0), ref(VmObjectRef{})});
        CHECK(vm.linker.Class(null_error.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        vm.interpreter.SetGcIntegration({});
        CHECK_FALSE(vm.interpreter.MarkReachable().IsMarked(gl));
        CHECK_FALSE(vm.interpreter.MarkReachable().IsMarked(buffer));
        vm.CallOn(egl, "eglMakeCurrent",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
            {ref(display), ref(vm.context->egl.no_surface), ref(vm.context->egl.no_surface), ref(vm.context->egl.no_context)});
        for (const auto context : {first, second}) vm.CallOn(egl, "eglDestroyContext",
            "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z", {ref(display), ref(context)});
        vm.CallOn(egl, "eglDestroySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", {ref(display), ref(surface)});
        vm.CallOn(egl, "eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", {ref(display)});
    }
}

TEST_CASE("View subtree sizes are delivered once before Surface creation and include old sizes") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::vector<std::array<int,4>> sizes;
        int created{};
        auto child = IntrinsicClassBuilder::Class("Ltest/SizedSurface;", "Landroid/view/SurfaceView;");
        child.OverrideMethod("onSizeChanged", "(IIII)V", [&](IntrinsicContext& c) {
            CHECK(c.vm.CurrentContextToken()==1U);
            sizes.push_back({c.arguments[0].AsInt(),c.arguments[1].AsInt(),c.arguments[2].AsInt(),c.arguments[3].AsInt()});
            return VmValue::Void();
        });
        auto callback = IntrinsicClassBuilder::Class("Ltest/SizedSurfaceCallback;", "Ljava/lang/Object;", {"Landroid/view/SurfaceHolder$Callback;"});
        callback.VirtualMethod("surfaceCreated", "(Landroid/view/SurfaceHolder;)V", [&](IntrinsicContext&) {
            CHECK(sizes.size()==1);CHECK(sizes.back()[0]==120);CHECK(sizes.back()[1]==80);++created;
            return VmValue::Void();
        });
        callback.VirtualMethod("surfaceChanged", "(Landroid/view/SurfaceHolder;III)V", [](IntrinsicContext&) { return VmValue::Void(); });
        callback.VirtualMethod("surfaceDestroyed", "(Landroid/view/SurfaceHolder;)V", [](IntrinsicContext&) { return VmValue::Void(); });
        EglVm vm(backend,nullptr,nullptr,{std::move(child).Build(),std::move(callback).Build()});
        auto& c=*vm.context;c.surface_width=800;c.surface_height=480;
        const auto context=vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto parent=vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
        const auto root=c.ui_tree.CreateNode(ui::UiClass::FrameLayout);
        BindViewToUiNode(c,parent,root);
        c.ui_tree.Get(root)->layout.width={ui::SizeMode::MatchParent,0};
        c.ui_tree.Get(root)->layout.height={ui::SizeMode::MatchParent,0};
        c.ui_tree.Attach(c.ui_tree.Root(),root);c.content_view=parent;
        const auto surface=vm.interpreter.NewIntrinsicInstance("Ltest/SizedSurface;");
        vm.CallStatic("Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(surface),VmValue::Ref(context)});
        const auto node=*FindViewUiNode(c,surface.Value());
        c.ui_tree.Get(node)->layout.width={ui::SizeMode::Fixed,120};
        c.ui_tree.Get(node)->layout.height={ui::SizeMode::Fixed,80};
        c.ui_tree.Attach(root,node);
        const auto holder=vm.CallOn(surface,"getHolder","()Landroid/view/SurfaceHolder;").ref;
        vm.CallOn(holder,"addCallback","(Landroid/view/SurfaceHolder$Callback;)V",{VmValue::Ref(vm.interpreter.NewIntrinsicInstance("Ltest/SizedSurfaceCallback;"))});
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(vm.interpreter,c,SurfaceHolderPhase::created).has_value());
        CHECK(created==1);REQUIRE(sizes.size()==1);CHECK(sizes[0]==std::array<int,4>{120,80,0,0});
        CHECK_FALSE(DispatchAndroidViewSizes(vm.interpreter,c));CHECK(sizes.size()==1);
        c.ui_tree.Get(node)->layout.width.px=160;c.ui_tree.MarkLayoutDirty(node);
        CHECK(DispatchAndroidViewSizes(vm.interpreter,c));REQUIRE(sizes.size()==2);CHECK(sizes[1]==std::array<int,4>{160,80,120,80});
        c.ui_tree.Detach(node);
        c.ui_tree.Get(node)->layout.width.px=200;
        static_cast<void>(DispatchAndroidViewSizes(vm.interpreter,c));CHECK(sizes.size()==2);
        c.ui_tree.Attach(root,node);
        static_cast<void>(DispatchAndroidViewSizes(vm.interpreter,c));REQUIRE(sizes.size()==3);CHECK(sizes[2]==std::array<int,4>{200,80,160,80});
        REQUIRE_FALSE(RetireSurfaceHolderGeneration(vm.interpreter,c).has_value());
    }
}

TEST_CASE("View size callback subtree replacement never notifies detached snapshot children") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto state=std::make_shared<DexVmAndroidContext>();
        ui::UiNodeId child_node{};int child_calls{};
        auto parent=IntrinsicClassBuilder::Class("Ltest/MutatingSizeParent;", "Landroid/widget/FrameLayout;");
        parent.OverrideMethod("onSizeChanged","(IIII)V",[&](IntrinsicContext&) {
            state->ui_tree.Detach(child_node);return VmValue::Void();
        });
        auto child=IntrinsicClassBuilder::Class("Ltest/UnusedSizeChild;", "Landroid/view/View;");
        child.OverrideMethod("onSizeChanged","(IIII)V",[&](IntrinsicContext&) {++child_calls;return VmValue::Void();});
        EglVm vm(backend,nullptr,nullptr,{std::move(parent).Build(),std::move(child).Build()});state=vm.context;
        state->surface_width=200;state->surface_height=100;
        const auto parent_view=vm.interpreter.NewIntrinsicInstance("Ltest/MutatingSizeParent;");
        const auto p=state->ui_tree.CreateNode(ui::UiClass::FrameLayout);
        BindViewToUiNode(*state,parent_view,p);state->ui_tree.Attach(state->ui_tree.Root(),p);
        state->ui_tree.Get(p)->layout.width={ui::SizeMode::MatchParent,0};state->ui_tree.Get(p)->layout.height={ui::SizeMode::MatchParent,0};
        const auto child_view=vm.interpreter.NewIntrinsicInstance("Ltest/UnusedSizeChild;");
        child_node=state->ui_tree.CreateNode(ui::UiClass::View);BindViewToUiNode(*state,child_view,child_node);
        state->ui_tree.Get(child_node)->layout.width={ui::SizeMode::Fixed,30};state->ui_tree.Get(child_node)->layout.height={ui::SizeMode::Fixed,20};
        state->ui_tree.Attach(p,child_node);
        CHECK(DispatchAndroidViewSizes(vm.interpreter,*state));CHECK(child_calls==0);
        CHECK_FALSE(state->ui_layout_dispatching);
    }
}
