// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_opengl_GLSurfaceView_EGLConfigChooser.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_opengl_GLSurfaceView_EGLConfigChooser {

Decl Declare_android_opengl_GLSurfaceView_EGLConfigChooser(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_opengl_GLSurfaceView_EGLConfigChooser(const Context& context) {
    return dvm80_android_opengl_GLSurfaceView_EGLConfigChooser::Declare_android_opengl_GLSurfaceView_EGLConfigChooser(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_opengl_GLSurfaceView_EGLContextFactory.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_opengl_GLSurfaceView_EGLContextFactory {

Decl Declare_android_opengl_GLSurfaceView_EGLContextFactory(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLContextFactory;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_opengl_GLSurfaceView_EGLContextFactory(const Context& context) {
    return dvm80_android_opengl_GLSurfaceView_EGLContextFactory::Declare_android_opengl_GLSurfaceView_EGLContextFactory(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_opengl_GLSurfaceView_Renderer.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_opengl_GLSurfaceView_Renderer {

Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/opengl/GLSurfaceView$Renderer;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    return dvm80_android_opengl_GLSurfaceView_Renderer::Declare_android_opengl_GLSurfaceView_Renderer(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_opengl_GLSurfaceView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_opengl_GLSurfaceView {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/GLSurfaceView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setRenderer",
        "(Landroid/opengl/GLSurfaceView$Renderer;)V",
        [context](dx::IntrinsicContext& call) {
            context->renderer = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setEGLContextFactory",
        "(Landroid/opengl/GLSurfaceView$EGLContextFactory;)V",
        [context](dx::IntrinsicContext& call) {
            context->egl_context_factory = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setEGLConfigChooser",
        "(Landroid/opengl/GLSurfaceView$EGLConfigChooser;)V",
        [context](dx::IntrinsicContext& call) {
            context->egl_config_chooser = call.arguments[0].ref;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("requestRender", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    // Render pause/resume is owned by the lifecycle driver.
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("onPause", "()V", lifecycle_noop);
    builder.FinalMethod("onResume", "()V", lifecycle_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    return dvm80_android_opengl_GLSurfaceView::Declare_android_opengl_GLSurfaceView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from javax_microedition_khronos_egl.cpp ----
#include "catalog.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "generated/java_gles_surface.inc"

namespace ogplay::runtime::android_intrinsics {
namespace {

constexpr std::int32_t kSuccess = 0x3000;
constexpr std::int32_t kBadAttribute = 0x3004;
constexpr std::int32_t kBadConfig = 0x3005;
constexpr std::int32_t kBadContext = 0x3006;
constexpr std::int32_t kBadDisplay = 0x3008;
constexpr std::int32_t kBadNativeWindow = 0x300B;
constexpr std::int32_t kBadParameter = 0x300C;
constexpr std::int32_t kBadSurface = 0x300D;
constexpr std::int32_t kNone = 0x3038;
constexpr std::int32_t kDontCare = -1;

const std::unordered_map<std::int32_t, std::int32_t> kConfigFacts{
    {0x3020, 32}, {0x3021, 8}, {0x3022, 8}, {0x3023, 8}, {0x3024, 8},
    {0x3025, 24}, {0x3026, 8}, {0x3027, kNone}, {0x3028, 1}, {0x3029, 0},
    {0x302D, 0}, {0x302E, 0}, {0x302F, kNone}, {0x3031, 0}, {0x3032, 0},
    {0x3033, 0x05}, {0x3034, kNone}, {0x303D, 0}, {0x303E, 0},
    {0x303F, 0x308E}, {0x3040, 0x05}};

[[nodiscard]] dx::VmValue Bool(const bool value) {
    return dx::VmValue::Int(value ? 1 : 0);
}

void Record(dx::IntrinsicContext& call, const std::string& id) {
    if (auto* ledger = call.vm.Ledger(); ledger != nullptr) {
        ledger->RecordUnimplemented(id, 0);
    }
}

[[noreturn]] void ModelFailure(dx::IntrinsicContext& call,
                               const std::string& detail) {
    Record(call, "dexvm.egl_facade.model");
    throw std::runtime_error("EGL facade model violation: " + detail);
}

void SetError(const Context& context, const std::int32_t error) {
    context->egl.last_error = error;
}

[[nodiscard]] bool IsDisplay(const Context& context, const dx::VmObjectRef ref) {
    return ref.IsValid() && ref == context->egl.display;
}

[[nodiscard]] bool IsConfig(const Context& context, const dx::VmObjectRef ref) {
    return ref.IsValid() && ref == context->egl.config;
}

[[nodiscard]] bool ValidateDisplay(const Context& context,
                                   const dx::VmObjectRef ref) {
    if (!IsDisplay(context, ref)) {
        SetError(context, kBadDisplay);
        return false;
    }
    return true;
}

[[nodiscard]] std::int32_t IntElement(dx::IntrinsicContext& call,
                                      const dx::VmObjectRef array,
                                      const std::int32_t index) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(call.vm.Model().GetPrimitiveElement(array, index)));
}

void SetIntElement(dx::IntrinsicContext& call, const dx::VmObjectRef array,
                   const std::int32_t index, const std::int32_t value) {
    call.vm.Model().SetPrimitiveElement(
        array, index, static_cast<std::uint32_t>(value));
}

[[nodiscard]] std::optional<bool> AttributeListMatches(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef list) {
    if (!list.IsValid()) return true;
    const auto length = call.vm.Model().ArrayLength(list);
    for (std::int32_t index = 0; index < length;) {
        const auto attribute = IntElement(call, list, index++);
        if (attribute == kNone) return true;
        if (index >= length) {
            SetError(context, kBadAttribute);
            return std::nullopt;
        }
        const auto requested = IntElement(call, list, index++);
        const auto fact = kConfigFacts.find(attribute);
        if (fact == kConfigFacts.end()) {
            Record(call, "dexvm.egl_facade.config_attribute." +
                             std::to_string(attribute));
            SetError(context, kBadAttribute);
            return std::nullopt;
        }
        if (requested == kDontCare) continue;
        const bool minimum = attribute == 0x3020 ||
                             (attribute >= 0x3021 && attribute <= 0x3026) ||
                             attribute == 0x3031 || attribute == 0x3032 ||
                             attribute == 0x303D || attribute == 0x303E;
        const bool mask = attribute == 0x3033 || attribute == 0x3040;
        if ((minimum && fact->second < requested) ||
            (mask && (fact->second & requested) != requested) ||
            (!minimum && !mask && fact->second != requested)) {
            return false;
        }
    }
    SetError(context, kBadAttribute);
    return std::nullopt;
}

void RequireInitialized(dx::IntrinsicContext& call, const Context& context) {
    if (!context->egl.initialized) ModelFailure(call, "display is not initialized");
}

[[nodiscard]] dx::VmObjectRef EnsureDisplay(dx::IntrinsicContext& call,
                                            const Context& context) {
    if (!context->egl.display.IsValid()) {
        context->egl.display = call.vm.NewIntrinsicInstance(
            "Ljavax/microedition/khronos/egl/EGLDisplay;");
    }
    return context->egl.display;
}

[[nodiscard]] dx::VmObjectRef EnsureConfig(dx::IntrinsicContext& call,
                                           const Context& context) {
    if (!context->egl.config.IsValid()) {
        context->egl.config = call.vm.NewIntrinsicInstance(
            "Ljavax/microedition/khronos/egl/EGLConfig;");
    }
    return context->egl.config;
}

}  // namespace

namespace {

dx::IntrinsicHandler EglGetDisplayHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].ref.IsValid()) {
            Record(call, "dexvm.egl_facade.non_default_display");
            SetError(context, kBadParameter);
            return dx::VmValue::Ref(context->egl.no_display);
        }
        return dx::VmValue::Ref(EnsureDisplay(call, context));
    };
}

dx::IntrinsicHandler EglInitializeHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        const auto versions = call.arguments[1].ref;
        if (versions.IsValid()) {
            if (call.vm.Model().ArrayLength(versions) < 2) {
                SetError(context, kBadParameter); return Bool(false);
            }
            SetIntElement(call, versions, 0, 1); SetIntElement(call, versions, 1, 4);
        }
        context->egl.initialized = true;
        return Bool(true);
    };
}

dx::IntrinsicHandler EglChooseConfigHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        RequireInitialized(call, context);
        const auto size = call.arguments[3].AsInt();
        const auto num = call.arguments[4].ref;
        if (size < 0 || !num.IsValid() || call.vm.Model().ArrayLength(num) < 1) {
            SetError(context, kBadParameter); return Bool(false);
        }
        const auto matches = AttributeListMatches(call, context, call.arguments[1].ref);
        if (!matches.has_value()) return Bool(false);
        SetIntElement(call, num, 0, *matches ? 1 : 0);
        const auto configs = call.arguments[2].ref;
        if (*matches && configs.IsValid() && size > 0) {
            if (call.vm.Model().ArrayLength(configs) < 1) {
                SetError(context, kBadParameter); return Bool(false);
            }
            call.vm.Model().SetObjectElement(configs, 0, EnsureConfig(call, context));
        }
        return Bool(true);
    };
}

dx::IntrinsicHandler EglGetConfigAttribHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (!IsConfig(context, call.arguments[1].ref)) {
            SetError(context, kBadConfig); return Bool(false);
        }
        const auto output = call.arguments[3].ref;
        if (!output.IsValid() || call.vm.Model().ArrayLength(output) < 1) {
            SetError(context, kBadParameter); return Bool(false);
        }
        const auto attribute = call.arguments[2].AsInt();
        const auto fact = kConfigFacts.find(attribute);
        if (fact == kConfigFacts.end()) {
            Record(call, "dexvm.egl_facade.config_attribute." + std::to_string(attribute));
            SetError(context, kBadAttribute); return Bool(false);
        }
        SetIntElement(call, output, 0, fact->second); return Bool(true);
    };
}

dx::IntrinsicHandler EglCreateContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return dx::VmValue::Ref(context->egl.no_context);
        RequireInitialized(call, context);
        if (!IsConfig(context, call.arguments[1].ref)) { SetError(context, kBadConfig); return dx::VmValue::Ref(context->egl.no_context); }
        const auto share = call.arguments[2].ref;
        if (share.IsValid() && share != context->egl.no_context) ModelFailure(call, "shared contexts are unsupported");
        std::int32_t version = 1;
        const auto attributes = call.arguments[3].ref;
        if (attributes.IsValid()) {
            const auto length = call.vm.Model().ArrayLength(attributes);
            if (length == 1 && IntElement(call, attributes, 0) == kNone) {
                version = 1;
            } else if (length == 3 && IntElement(call, attributes, 0) == 12440 &&
                       IntElement(call, attributes, 2) == kNone) {
                version = IntElement(call, attributes, 1);
            } else {
                SetError(context, kBadAttribute); return dx::VmValue::Ref(context->egl.no_context);
            }
        }
        if (version != 1 && version != 2) { SetError(context, kBadAttribute); return dx::VmValue::Ref(context->egl.no_context); }
        const auto instance = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLContext;");
        context->egl.contexts.emplace(instance.Value(), version);
        return dx::VmValue::Ref(instance);
    };
}

dx::IntrinsicHandler EglCreateWindowSurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return dx::VmValue::Ref(context->egl.no_surface);
        RequireInitialized(call, context);
        if (!IsConfig(context, call.arguments[1].ref)) { SetError(context, kBadConfig); return dx::VmValue::Ref(context->egl.no_surface); }
        const auto holder = call.arguments[2].ref;
        const auto registered_holder = std::find_if(
            context->surface_holders.begin(), context->surface_holders.end(),
            [holder](const auto& entry) { return entry.second == holder; });
        if (!holder.IsValid() || registered_holder == context->surface_holders.end()) {
            SetError(context, kBadNativeWindow); return dx::VmValue::Ref(context->egl.no_surface);
        }
        if (context->session == nullptr || !context->session->ManagedSurfaceIsOpen()) ModelFailure(call, "managed surface is not open");
        if (context->egl.window_surface.IsValid()) ModelFailure(call, "a second window surface is unsupported");
        const auto attributes = call.arguments[3].ref;
        if (attributes.IsValid() && (call.vm.Model().ArrayLength(attributes) != 1 || IntElement(call, attributes, 0) != kNone)) {
            SetError(context, kBadAttribute); return dx::VmValue::Ref(context->egl.no_surface);
        }
        context->egl.window_surface = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLSurface;");
        return dx::VmValue::Ref(context->egl.window_surface);
    };
}

dx::IntrinsicHandler EglDestroySurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (call.arguments[1].ref != context->egl.window_surface) { SetError(context, kBadSurface); return Bool(false); }
        if (context->egl.current_surface == context->egl.window_surface) ModelFailure(call, "current surface cannot be destroyed");
        context->egl.window_surface = dx::VmObjectRef{}; return Bool(true);
    };
}

dx::IntrinsicHandler EglDestroyContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        const auto ref = call.arguments[1].ref;
        if (!context->egl.contexts.contains(ref.Value())) { SetError(context, kBadContext); return Bool(false); }
        if (context->egl.current_context == ref) ModelFailure(call, "current context cannot be destroyed");
        context->egl.contexts.erase(ref.Value()); return Bool(true);
    };
}

dx::IntrinsicHandler EglMakeCurrentHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        const auto draw = call.arguments[1].ref; const auto read = call.arguments[2].ref; const auto egl_context = call.arguments[3].ref;
        const bool unbind = draw == context->egl.no_surface && read == context->egl.no_surface && egl_context == context->egl.no_context;
        if (unbind) {
            if (context->egl.current_thread.has_value() && *context->egl.current_thread != std::this_thread::get_id()) ModelFailure(call, "another thread owns facade currency");
            if (context->session == nullptr) ModelFailure(call, "guest session is absent");
            context->session->ReleaseManagedSurfaceFromCallingThread();
            context->egl.current_display = dx::VmObjectRef{};
            context->egl.current_surface = dx::VmObjectRef{};
            context->egl.current_context = dx::VmObjectRef{};
            context->egl.current_thread.reset();
            return Bool(true);
        }
        if (draw != read || draw != context->egl.window_surface || !context->egl.contexts.contains(egl_context.Value())) ModelFailure(call, "invalid draw/read/context binding shape");
        if (context->egl.current_thread.has_value() && *context->egl.current_thread != std::this_thread::get_id()) ModelFailure(call, "another thread owns facade currency");
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        context->session->BindManagedSurfaceOnCallingThread();
        context->egl.current_display = call.arguments[0].ref; context->egl.current_surface = draw; context->egl.current_context = egl_context; context->egl.current_thread = std::this_thread::get_id();
        return Bool(true);
    };
}

dx::IntrinsicHandler EglSwapBuffersHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->egl.surface_retired.load(std::memory_order_acquire)) {
            SetError(context, kBadNativeWindow);
            return Bool(false);
        }
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (call.arguments[1].ref != context->egl.current_surface ||
            !context->egl.current_thread.has_value() ||
            *context->egl.current_thread != std::this_thread::get_id()) {
            SetError(context, kBadSurface); return Bool(false);
        }
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        context->session->PresentManagedSurface();
        PaceEglSwap(*context, call.vm.ExecutionLock());
        return Bool(true);
    };
}

dx::IntrinsicHandler EglGetCurrentDisplayHandler(const Context& context) {
    return [context](dx::IntrinsicContext&) { return dx::VmValue::Ref(context->egl.current_display.IsValid() ? context->egl.current_display : context->egl.no_display); };
}

dx::IntrinsicHandler EglGetCurrentSurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto which = call.arguments[0].AsInt();
        if (which != 0x3059 && which != 0x305A) { SetError(context, kBadParameter); return dx::VmValue::Ref(context->egl.no_surface); }
        return dx::VmValue::Ref(context->egl.current_surface.IsValid() ? context->egl.current_surface : context->egl.no_surface);
    };
}

dx::IntrinsicHandler EglGetErrorHandler(const Context& context) {
    return [context](dx::IntrinsicContext&) { const auto error = context->egl.last_error; context->egl.last_error = kSuccess; return dx::VmValue::Int(error); };
}

dx::IntrinsicHandler EglTerminateHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->egl.window_surface.IsValid() || !context->egl.contexts.empty() || context->egl.current_context.IsValid()) ModelFailure(call, "terminate requires retired facade objects");
        context->egl.initialized = false; return Bool(true);
    };
}

dx::IntrinsicHandler GlGetStringHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto parameter = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if (parameter != 0x1F00 && parameter != 0x1F01 && parameter != 0x1F02 && parameter != 0x1F03) {
            Record(call, "dexvm.gl10.glGetString." + std::to_string(parameter));
            throw std::runtime_error("GL10.glGetString parameter is unsupported");
        }
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        return MakeString(call, context->session->ManagedGlString(parameter));
    };
}

[[nodiscard]] std::vector<std::string> GlParameterTypes(
    const std::string_view descriptor) {
    std::vector<std::string> result;
    for (std::size_t cursor = 1; cursor < descriptor.find(')');) {
        const auto start = cursor;
        while (descriptor[cursor] == '[') ++cursor;
        if (descriptor[cursor] == 'L') {
            cursor = descriptor.find(';', cursor) + 1U;
        } else {
            ++cursor;
        }
        result.emplace_back(descriptor.substr(start, cursor - start));
    }
    return result;
}

[[nodiscard]] bool GlWritesPointer(const std::string_view name) {
    return name.starts_with("glGet") || name.starts_with("glGen") ||
           name.starts_with("glRead") || name.starts_with("glQuery");
}

[[nodiscard]] std::uint32_t ArrayElementSize(const char descriptor) {
    switch (descriptor) {
        case 'Z': case 'B': return 1U;
        case 'C': case 'S': return 2U;
        case 'I': case 'F': return 4U;
        case 'J': case 'D': return 8U;
        default: throw std::invalid_argument("unsupported GLES primitive array");
    }
}

[[nodiscard]] dx::VmValue ManagedGlResult(const std::string_view descriptor,
                                          const std::uint32_t result) {
    const auto returns = descriptor.substr(descriptor.find(')') + 1U);
    if (returns == "V") return dx::VmValue::Void();
    if (returns == "I" || returns == "Z") {
        return dx::VmValue::Int(static_cast<std::int32_t>(result));
    }
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "Java GLES return adapter is unavailable"};
}

dx::IntrinsicHandler JavaGlesHandler(const Context& context,
                                     const gles::GlesApi api,
                                     std::string name,
                                     std::string descriptor) {
    return [context, api, name = std::move(name),
            descriptor = std::move(descriptor)](dx::IntrinsicContext& call) {
        if (name == "glGetString") return GlGetStringHandler(context)(call);
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        if (name == "glShaderSource" && descriptor == "(ILjava/lang/String;)V") {
            auto text = call.vm.StringUtf8(call.arguments[1].ref);
            std::vector<std::byte> bytes;
            for (const auto character : text) bytes.push_back(static_cast<std::byte>(character));
            bytes.push_back(std::byte{});
            static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                bytes, false, [&](const memory::GuestAddress source) {
                    const std::array pointer_bytes{
                        static_cast<std::byte>(source.Value()),
                        static_cast<std::byte>(source.Value() >> 8U),
                        static_cast<std::byte>(source.Value() >> 16U),
                        static_cast<std::byte>(source.Value() >> 24U)};
                    return context->session->NIO().WithTemporaryGuestMemory(
                        pointer_bytes, false, [&](const memory::GuestAddress pointers) {
                            const std::array args{call.arguments[0].cat1, 1U,
                                                  pointers.Value(), 0U};
                            return context->session->InvokeManagedGles(
                                api, name, args);
                        });
                }));
            return dx::VmValue::Void();
        }
        const auto native = gles::FindGlesFunction(api, name);
        if (!native.has_value()) {
            Record(call, "dexvm.java_gles." + name + descriptor);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Java GLES method is outside the native API 19 catalog"};
        }
        const auto types = GlParameterTypes(descriptor);
        std::vector<std::uint32_t> arguments;
        const bool copy_back = GlWritesPointer(name);
        std::function<std::uint32_t(std::size_t)> marshal;
        marshal = [&](const std::size_t index) -> std::uint32_t {
            if (index == types.size()) {
                return context->session->InvokeManagedGles(api, name, arguments);
            }
            const auto& type = types[index];
            const auto& value = call.arguments[index];
            if (type.size() == 1U) {
                arguments.push_back(value.cat1);
                const auto result = marshal(index + 1U);
                arguments.pop_back();
                return result;
            }
            if (!value.ref.IsValid()) {
                arguments.push_back(0U);
                const auto result = marshal(index + 1U);
                arguments.pop_back();
                return result;
            }
            if (type == "Ljava/lang/String;") {
                auto text = call.vm.StringUtf8(value.ref);
                std::vector<std::byte> bytes;
                bytes.reserve(text.size() + 1U);
                for (const auto character : text) {
                    bytes.push_back(static_cast<std::byte>(character));
                }
                bytes.push_back(std::byte{});
                return context->session->NIO().WithTemporaryGuestMemory(
                    bytes, false, [&](const memory::GuestAddress address) {
                        arguments.push_back(address.Value());
                        const auto result = marshal(index + 1U);
                        arguments.pop_back();
                        return result;
                    });
            }
            const auto identity = call.vm.Model().ToIdentity(value.ref);
            if (context->session->NIO().Contains(identity)) {
                return context->session->NIO().WithBufferGuestMemory(
                    identity, copy_back, [&](const memory::GuestAddress address) {
                        arguments.push_back(address.Value());
                        const auto result = marshal(index + 1U);
                        arguments.pop_back();
                        return result;
                    });
            }
            if (type.front() == '[' && type.size() == 2U) {
                const auto size = ArrayElementSize(type[1]);
                std::int32_t offset{};
                std::size_t next = index + 1U;
                if (next < types.size() && types[next] == "I") {
                    offset = call.arguments[next].AsInt();
                    ++next;
                }
                const auto length = call.vm.Model().ArrayLength(value.ref);
                if (offset < 0 || offset > length) {
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "Java GLES array offset is outside the array"};
                }
                std::vector<std::byte> bytes(
                    static_cast<std::size_t>(length - offset) * size);
                for (std::int32_t element = offset; element < length; ++element) {
                    const auto bits = call.vm.Model().GetPrimitiveElement(value.ref, element);
                    for (std::uint32_t byte = 0; byte < size; ++byte) {
                        bytes[(element - offset) * size + byte] =
                            static_cast<std::byte>(bits >> (byte * 8U));
                    }
                }
                std::vector<std::byte> output;
                const auto result = context->session->NIO().WithTemporaryGuestMemory(
                    bytes, copy_back, [&](const memory::GuestAddress address) {
                        arguments.push_back(address.Value());
                        const auto nested = marshal(next);
                        arguments.pop_back();
                        return nested;
                    }, &output);
                if (copy_back) {
                    for (std::int32_t element = offset; element < length; ++element) {
                        std::uint64_t bits{};
                        for (std::uint32_t byte = 0; byte < size; ++byte) {
                            bits |= static_cast<std::uint64_t>(
                                std::to_integer<std::uint8_t>(
                                    output[(element - offset) * size + byte])) <<
                                (byte * 8U);
                        }
                        call.vm.Model().SetPrimitiveElement(value.ref, element, bits);
                    }
                }
                return result;
            }
            Record(call, "dexvm.java_gles.argument." + type);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Java GLES reference argument is unsupported"};
        };
        try {
            return ManagedGlResult(descriptor, marshal(0U));
        } catch (const std::invalid_argument&) {
            Record(call, "dexvm.java_gles.signature." + name + descriptor);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Java GLES signature has no native adapter"};
        }
    };
}

[[nodiscard]] DexVmAndroidContext::BitmapState& GlBitmap(
    const Context& context, const dx::VmObjectRef bitmap) {
    if (!bitmap.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "GLUtils bitmap is null"};
    }
    const auto found = context->bitmaps.find(bitmap.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "GLUtils bitmap is invalid or recycled"};
    }
    return found->second;
}

[[nodiscard]] std::vector<std::byte> RgbaPixels(
    const DexVmAndroidContext::BitmapState& bitmap) {
    std::vector<std::byte> bytes(bitmap.argb.size() * 4U);
    for (std::size_t index = 0; index < bitmap.argb.size(); ++index) {
        const auto pixel = bitmap.argb[index];
        bytes[index * 4U] = static_cast<std::byte>(pixel >> 16U);
        bytes[index * 4U + 1U] = static_cast<std::byte>(pixel >> 8U);
        bytes[index * 4U + 2U] = static_cast<std::byte>(pixel);
        bytes[index * 4U + 3U] = static_cast<std::byte>(pixel >> 24U);
    }
    return bytes;
}

dx::IntrinsicHandler GlUtilsTextureHandler(const Context& context,
                                           const bool sub_image,
                                           std::string descriptor) {
    return [context, sub_image, descriptor = std::move(descriptor)](
               dx::IntrinsicContext& call) {
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        const std::size_t bitmap_index = sub_image ? 4U :
            (descriptor == "(IILandroid/graphics/Bitmap;I)V" ? 2U : 3U);
        const auto& bitmap = GlBitmap(context, call.arguments[bitmap_index].ref);
        constexpr std::uint32_t kRgba = 0x1908U;
        constexpr std::uint32_t kUnsignedByte = 0x1401U;
        auto format = kRgba;
        auto type = kUnsignedByte;
        if (!sub_image && bitmap_index == 3U) {
            format = call.arguments[2].cat1;
            if (descriptor == "(IIILandroid/graphics/Bitmap;II)V") {
                type = call.arguments[4].cat1;
            }
        } else if (sub_image && descriptor.ends_with("Bitmap;II)V")) {
            format = call.arguments[5].cat1;
            type = call.arguments[6].cat1;
        }
        if (format != kRgba || type != kUnsignedByte) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "GLUtils supports ARGB_8888 as RGBA/UNSIGNED_BYTE"};
        }
        const auto pixels = RgbaPixels(bitmap);
        static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
            pixels, false, [&](const memory::GuestAddress address) {
                if (sub_image) {
                    const std::array args{
                        call.arguments[0].cat1, call.arguments[1].cat1,
                        call.arguments[2].cat1, call.arguments[3].cat1,
                        static_cast<std::uint32_t>(bitmap.width),
                        static_cast<std::uint32_t>(bitmap.height), format, type,
                        address.Value()};
                    return context->session->InvokeManagedGles(
                        gles::GlesApi::gles2, "glTexSubImage2D", args);
                }
                const auto border = call.arguments.back().cat1;
                const std::array args{
                    call.arguments[0].cat1, call.arguments[1].cat1, format,
                    static_cast<std::uint32_t>(bitmap.width),
                    static_cast<std::uint32_t>(bitmap.height), border, format,
                    type, address.Value()};
                return context->session->InvokeManagedGles(
                    gles::GlesApi::gles2, "glTexImage2D", args);
            }));
        return dx::VmValue::Void();
    };
}

Decl DeclareJavaGlesClass(
    const Context& context, const char* class_descriptor,
    const char* superclass, const gles::GlesApi api,
    const std::span<const generated_java_gles::MethodSpec> methods,
    const std::span<const generated_java_gles::ConstantSpec> constants) {
    auto builder = dx::IntrinsicClassBuilder::Class(class_descriptor, superclass);
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    for (const auto& constant : constants) {
        builder.ConstantInt(constant.name, "I", constant.value);
    }
    for (const auto& method : methods) {
        builder.StaticMethod(method.name, method.descriptor,
                             JavaGlesHandler(context, api, method.name,
                                             method.descriptor));
    }
    return std::move(builder).Build();
}

dx::IntrinsicHandler EglUnsupportedHandler(std::string method) {
    return [method = std::move(method)](dx::IntrinsicContext& call) -> dx::VmValue {
        Record(call, "dexvm.egl_facade." + method);
        throw std::runtime_error("EGL facade method is not implemented: " + method);
    };
}

}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void AttachEglSwapPacer(DexVmAndroidContext& context,
                        dexvm::VmExecutionLock& execution_lock) {
    const auto driver = std::this_thread::get_id();
    {
        std::scoped_lock lock(context.egl.pace_mutex);
        context.egl.pace_driver = driver;
        context.egl.pace_driver_blocked = false;
        context.egl.pace_shutdown = false;
        context.egl.pace_generation = 0;
    }
    execution_lock.SetBlockingObserver(
        &context.egl,
        [](void* opaque, const std::thread::id thread,
           const bool blocked) noexcept {
            auto& egl = *static_cast<DexVmAndroidContext::EglFacadeState*>(opaque);
            // Filtering before taking the pacer lock is essential: the swap
            // thread releases VmExecutionLock while holding pace_mutex.
            if (!egl.pace_driver.has_value() ||
                thread != *egl.pace_driver) {
                return;
            }
            {
                std::scoped_lock lock(egl.pace_mutex);
                if (egl.pace_shutdown || egl.pace_driver != thread) {
                    return;
                }
                egl.pace_driver_blocked = blocked;
            }
            egl.pace_changed.notify_all();
        });
}

void DetachEglSwapPacer(DexVmAndroidContext& context,
                        dexvm::VmExecutionLock& execution_lock) {
    ShutdownEglSwapPacer(context);
    execution_lock.SetBlockingObserver(nullptr, nullptr);
    std::scoped_lock lock(context.egl.pace_mutex);
    context.egl.pace_driver.reset();
    context.egl.pace_driver_blocked = false;
}

void AdvanceEglSwapPacer(DexVmAndroidContext& context) {
    {
        std::scoped_lock lock(context.egl.pace_mutex);
        ++context.egl.pace_generation;
    }
    context.egl.pace_changed.notify_all();
}

void ShutdownEglSwapPacer(DexVmAndroidContext& context) {
    {
        std::scoped_lock lock(context.egl.pace_mutex);
        context.egl.pace_shutdown = true;
    }
    context.egl.pace_changed.notify_all();
}

std::optional<EglSwapPacerSnapshot> TryEglSwapPacerSnapshot(
    DexVmAndroidContext& context) {
    std::unique_lock lock(context.egl.pace_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    return EglSwapPacerSnapshot{
        context.egl.pace_driver.has_value(),
        context.egl.pace_driver_blocked,
        context.egl.pace_shutdown,
        context.egl.surface_retired.load(std::memory_order_acquire),
        context.egl.pace_generation};
}

void RetireGuestEglSurface(DexVmAndroidContext& context) {
    context.egl.surface_retired.store(true, std::memory_order_release);
    ShutdownEglSwapPacer(context);
}

void PaceEglSwap(DexVmAndroidContext& context,
                 dexvm::VmExecutionLock& execution_lock) {
    auto& egl = context.egl;
    std::unique_lock lock(egl.pace_mutex);
    if (!egl.pace_driver.has_value() ||
        *egl.pace_driver == std::this_thread::get_id() ||
        egl.pace_driver_blocked || egl.pace_shutdown) {
        return;
    }
    const auto observed = egl.pace_generation;
    const auto depth = execution_lock.ReleaseForBlocking();
    try {
        egl.pace_changed.wait(lock, [&] {
            return egl.pace_generation != observed ||
                   egl.pace_driver_blocked || egl.pace_shutdown;
        });
    } catch (...) {
        lock.unlock();
        execution_lock.ReacquireAfterBlocking(depth);
        throw;
    }
    lock.unlock();
    execution_lock.ReacquireAfterBlocking(depth);
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLES10(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES10;", "Ljava/lang/Object;",
        gles::GlesApi::gles1, generated_java_gles::kGLES10Methods,
        generated_java_gles::kGLES10Constants);
}
Decl Declare_android_opengl_GLES10Ext(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES10Ext;", "Ljava/lang/Object;",
        gles::GlesApi::gles1_extensions, generated_java_gles::kGLES10ExtMethods,
        generated_java_gles::kGLES10ExtConstants);
}
Decl Declare_android_opengl_GLES11(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES11;", "Landroid/opengl/GLES10;",
        gles::GlesApi::gles1, generated_java_gles::kGLES11Methods,
        generated_java_gles::kGLES11Constants);
}
Decl Declare_android_opengl_GLES11Ext(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES11Ext;", "Ljava/lang/Object;",
        gles::GlesApi::gles1_extensions, generated_java_gles::kGLES11ExtMethods,
        generated_java_gles::kGLES11ExtConstants);
}
Decl Declare_android_opengl_GLES20(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES20;", "Ljava/lang/Object;",
        gles::GlesApi::gles2, generated_java_gles::kGLES20Methods,
        generated_java_gles::kGLES20Constants);
}
Decl Declare_android_opengl_GLUtils(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/GLUtils;", "Ljava/lang/Object;");
    for (const auto& method : generated_java_gles::kGLUtilsMethods) {
        const std::string_view name = method.name;
        if (name == "getInternalFormat" || name == "getType") {
            builder.StaticMethod(method.name, method.descriptor,
                [context, name](dx::IntrinsicContext& call) {
                    static_cast<void>(GlBitmap(context, call.arguments[0].ref));
                    return dx::VmValue::Int(name == "getInternalFormat"
                                                ? 0x1908 : 0x1401);
                });
        } else if (name == "texImage2D") {
            builder.StaticMethod(method.name, method.descriptor,
                GlUtilsTextureHandler(context, false, method.descriptor));
        } else if (name == "texSubImage2D") {
            builder.StaticMethod(method.name, method.descriptor,
                GlUtilsTextureHandler(context, true, method.descriptor));
        } else if (name == "getEGLErrorString") {
            builder.StaticMethod(method.name, method.descriptor,
                [](dx::IntrinsicContext& call) {
                    static const std::unordered_map<std::int32_t, std::string> names{
                        {0x3000, "EGL_SUCCESS"}, {0x3001, "EGL_NOT_INITIALIZED"},
                        {0x3002, "EGL_BAD_ACCESS"}, {0x3003, "EGL_BAD_ALLOC"},
                        {0x3004, "EGL_BAD_ATTRIBUTE"}, {0x3005, "EGL_BAD_CONFIG"},
                        {0x3006, "EGL_BAD_CONTEXT"}, {0x3007, "EGL_BAD_CURRENT_SURFACE"},
                        {0x3008, "EGL_BAD_DISPLAY"}, {0x3009, "EGL_BAD_MATCH"},
                        {0x300A, "EGL_BAD_NATIVE_PIXMAP"}, {0x300B, "EGL_BAD_NATIVE_WINDOW"},
                        {0x300C, "EGL_BAD_PARAMETER"}, {0x300D, "EGL_BAD_SURFACE"},
                        {0x300E, "EGL_CONTEXT_LOST"}};
                    const auto error = call.arguments[0].AsInt();
                    const auto found = names.find(error);
                    if (found != names.end()) return MakeString(call, found->second);
                    const auto hex = "0123456789abcdef";
                    std::string value{"0x"};
                    for (int shift = 28; shift >= 0; shift -= 4) {
                        value.push_back(hex[(static_cast<std::uint32_t>(error) >> shift) & 0xfU]);
                    }
                    return MakeString(call, value);
                });
        } else {
            builder.StaticMethod(method.name, method.descriptor,
                JavaGlesHandler(context, gles::GlesApi::gles2,
                                method.name, method.descriptor));
        }
    }
    return std::move(builder).Build();
}
Decl Declare_android_opengl_GLU(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLU;", "Ljava/lang/Object;",
        gles::GlesApi::gles1, generated_java_gles::kGLUMethods,
        generated_java_gles::kGLUConstants);
}

Decl Declare_javax_microedition_khronos_egl_EGL(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/microedition/khronos/egl/EGL;");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGL10(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/microedition/khronos/egl/EGL10;", {"Ljavax/microedition/khronos/egl/EGL;"});
    constexpr std::array constants{
        std::pair{"EGL_SUCCESS", 0x3000}, std::pair{"EGL_NOT_INITIALIZED", 0x3001},
        std::pair{"EGL_BAD_ACCESS", 0x3002}, std::pair{"EGL_BAD_ALLOC", 0x3003},
        std::pair{"EGL_BAD_ATTRIBUTE", 0x3004}, std::pair{"EGL_BAD_CONFIG", 0x3005},
        std::pair{"EGL_BAD_CONTEXT", 0x3006}, std::pair{"EGL_BAD_CURRENT_SURFACE", 0x3007},
        std::pair{"EGL_BAD_DISPLAY", 0x3008}, std::pair{"EGL_BAD_MATCH", 0x3009},
        std::pair{"EGL_BAD_NATIVE_PIXMAP", 0x300A}, std::pair{"EGL_BAD_NATIVE_WINDOW", 0x300B},
        std::pair{"EGL_BAD_PARAMETER", 0x300C}, std::pair{"EGL_BAD_SURFACE", 0x300D},
        std::pair{"EGL_BUFFER_SIZE", 0x3020}, std::pair{"EGL_ALPHA_SIZE", 0x3021},
        std::pair{"EGL_BLUE_SIZE", 0x3022}, std::pair{"EGL_GREEN_SIZE", 0x3023},
        std::pair{"EGL_RED_SIZE", 0x3024}, std::pair{"EGL_DEPTH_SIZE", 0x3025},
        std::pair{"EGL_STENCIL_SIZE", 0x3026}, std::pair{"EGL_CONFIG_CAVEAT", 0x3027},
        std::pair{"EGL_CONFIG_ID", 0x3028}, std::pair{"EGL_LEVEL", 0x3029},
        std::pair{"EGL_MAX_PBUFFER_HEIGHT", 0x302A}, std::pair{"EGL_MAX_PBUFFER_PIXELS", 0x302B},
        std::pair{"EGL_MAX_PBUFFER_WIDTH", 0x302C}, std::pair{"EGL_NATIVE_RENDERABLE", 0x302D},
        std::pair{"EGL_NATIVE_VISUAL_ID", 0x302E}, std::pair{"EGL_NATIVE_VISUAL_TYPE", 0x302F},
        std::pair{"EGL_SAMPLES", 0x3031}, std::pair{"EGL_SAMPLE_BUFFERS", 0x3032},
        std::pair{"EGL_SURFACE_TYPE", 0x3033}, std::pair{"EGL_TRANSPARENT_TYPE", 0x3034},
        std::pair{"EGL_TRANSPARENT_BLUE_VALUE", 0x3035}, std::pair{"EGL_TRANSPARENT_GREEN_VALUE", 0x3036},
        std::pair{"EGL_TRANSPARENT_RED_VALUE", 0x3037}, std::pair{"EGL_NONE", 0x3038},
        std::pair{"EGL_LUMINANCE_SIZE", 0x303D}, std::pair{"EGL_ALPHA_MASK_SIZE", 0x303E},
        std::pair{"EGL_COLOR_BUFFER_TYPE", 0x303F}, std::pair{"EGL_RENDERABLE_TYPE", 0x3040},
        std::pair{"EGL_SLOW_CONFIG", 0x3050}, std::pair{"EGL_NON_CONFORMANT_CONFIG", 0x3051},
        std::pair{"EGL_TRANSPARENT_RGB", 0x3052}, std::pair{"EGL_VENDOR", 0x3053},
        std::pair{"EGL_VERSION", 0x3054}, std::pair{"EGL_EXTENSIONS", 0x3055},
        std::pair{"EGL_HEIGHT", 0x3056}, std::pair{"EGL_WIDTH", 0x3057},
        std::pair{"EGL_LARGEST_PBUFFER", 0x3058}, std::pair{"EGL_DRAW", 0x3059},
        std::pair{"EGL_READ", 0x305A}, std::pair{"EGL_CORE_NATIVE_ENGINE", 0x305B},
        std::pair{"EGL_SINGLE_BUFFER", 0x3085}, std::pair{"EGL_RENDER_BUFFER", 0x3086},
        std::pair{"EGL_COLORSPACE", 0x3087}, std::pair{"EGL_ALPHA_FORMAT", 0x3088},
        std::pair{"EGL_RGB_BUFFER", 0x308E}, std::pair{"EGL_LUMINANCE_BUFFER", 0x308F},
        std::pair{"EGL_HORIZONTAL_RESOLUTION", 0x3090}, std::pair{"EGL_VERTICAL_RESOLUTION", 0x3091},
        std::pair{"EGL_PIXEL_ASPECT_RATIO", 0x3092}, std::pair{"EGL_DONT_CARE", -1},
        std::pair{"EGL_PBUFFER_BIT", 0x01}, std::pair{"EGL_PIXMAP_BIT", 0x02},
        std::pair{"EGL_WINDOW_BIT", 0x04}};
    for (const auto& [name, value] : constants) builder.ConstantInt(name, "I", value);
    builder.StaticField("EGL_DEFAULT_DISPLAY", "Ljava/lang/Object;")
        .StaticField("EGL_NO_DISPLAY", "Ljavax/microedition/khronos/egl/EGLDisplay;")
        .StaticField("EGL_NO_CONTEXT", "Ljavax/microedition/khronos/egl/EGLContext;")
        .StaticField("EGL_NO_SURFACE", "Ljavax/microedition/khronos/egl/EGLSurface;");
    builder.ClassInitializer([context](dx::IntrinsicContext& call) {
        context->egl.no_display = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLDisplay;");
        context->egl.no_context = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLContext;");
        context->egl.no_surface = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLSurface;");
        call.vm.SetIntrinsicStaticRef("Ljavax/microedition/khronos/egl/EGL10;", "EGL_NO_DISPLAY",
            "Ljavax/microedition/khronos/egl/EGLDisplay;", context->egl.no_display);
        call.vm.SetIntrinsicStaticRef("Ljavax/microedition/khronos/egl/EGL10;", "EGL_NO_CONTEXT",
            "Ljavax/microedition/khronos/egl/EGLContext;", context->egl.no_context);
        call.vm.SetIntrinsicStaticRef("Ljavax/microedition/khronos/egl/EGL10;", "EGL_NO_SURFACE",
            "Ljavax/microedition/khronos/egl/EGLSurface;", context->egl.no_surface);
        return dx::VmValue::Void();
    });
    const auto method = [&](const char* name, const char* descriptor) {
        builder.VirtualMethod(name, descriptor, EglUnsupportedHandler(name));
    };
    method("eglChooseConfig", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z");
    method("eglCopyBuffers", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljava/lang/Object;)Z");
    method("eglCreateContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;");
    method("eglCreatePbufferSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;");
    method("eglCreatePixmapSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;");
    method("eglCreateWindowSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;");
    method("eglDestroyContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z");
    method("eglDestroySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z");
    method("eglGetConfigAttrib", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z");
    method("eglGetConfigs", "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z");
    method("eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;");
    method("eglGetCurrentDisplay", "()Ljavax/microedition/khronos/egl/EGLDisplay;");
    method("eglGetCurrentSurface", "(I)Ljavax/microedition/khronos/egl/EGLSurface;");
    method("eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;");
    method("eglGetError", "()I");
    method("eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z");
    method("eglMakeCurrent", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z");
    method("eglQueryContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z");
    method("eglQueryString", "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;");
    method("eglQuerySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;I[I)Z");
    method("eglReleaseThread", "()Z");
    method("eglSwapBuffers", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z");
    method("eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z");
    method("eglWaitGL", "()Z");
    method("eglWaitNative", "(ILjava/lang/Object;)Z");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGL10_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/egl/EGL10$Impl;", "Ljava/lang/Object;", {"Ljavax/microedition/khronos/egl/EGL10;"});
    builder.FinalMethod("eglChooseConfig", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z", EglChooseConfigHandler(context));
    builder.FinalMethod("eglCreateContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;", EglCreateContextHandler(context));
    builder.FinalMethod("eglCreateWindowSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;", EglCreateWindowSurfaceHandler(context));
    builder.FinalMethod("eglDestroyContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z", EglDestroyContextHandler(context));
    builder.FinalMethod("eglDestroySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", EglDestroySurfaceHandler(context));
    builder.FinalMethod("eglGetConfigAttrib", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z", EglGetConfigAttribHandler(context));
    builder.FinalMethod("eglGetCurrentDisplay", "()Ljavax/microedition/khronos/egl/EGLDisplay;", EglGetCurrentDisplayHandler(context));
    builder.FinalMethod("eglGetCurrentSurface", "(I)Ljavax/microedition/khronos/egl/EGLSurface;", EglGetCurrentSurfaceHandler(context));
    builder.FinalMethod("eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;", EglGetDisplayHandler(context));
    builder.FinalMethod("eglGetError", "()I", EglGetErrorHandler(context));
    builder.FinalMethod("eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z", EglInitializeHandler(context));
    builder.FinalMethod("eglMakeCurrent", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z", EglMakeCurrentHandler(context));
    builder.FinalMethod("eglSwapBuffers", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", EglSwapBuffersHandler(context));
    builder.FinalMethod("eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", EglTerminateHandler(context));
    const auto gap = [&](const char* name, const char* descriptor) {
        builder.FinalMethod(name, descriptor, EglUnsupportedHandler(name));
    };
    gap("eglCopyBuffers", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljava/lang/Object;)Z");
    gap("eglCreatePbufferSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;");
    gap("eglCreatePixmapSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;");
    gap("eglGetConfigs", "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z");
    gap("eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;");
    gap("eglQueryContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z");
    gap("eglQueryString", "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;");
    gap("eglQuerySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;I[I)Z");
    gap("eglReleaseThread", "()Z");
    gap("eglWaitGL", "()Z");
    gap("eglWaitNative", "(ILjava/lang/Object;)Z");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGLConfig(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/egl/EGLConfig;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGLContext(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/egl/EGLContext;", "Ljava/lang/Object;");
    builder.StaticMethod("getEGL", "()Ljavax/microedition/khronos/egl/EGL;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "egl10_impl", "Ljavax/microedition/khronos/egl/EGL10$Impl;"));
    });
    builder.FinalMethod("getGL", "()Ljavax/microedition/khronos/opengles/GL;", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "gl10_impl", "Ljavax/microedition/khronos/opengles/GL10$Impl;"));
    });
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGLDisplay(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/egl/EGLDisplay;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_egl_EGLSurface(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/egl/EGLSurface;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_opengles_GL(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/microedition/khronos/opengles/GL;");
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_opengles_GL10(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Ljavax/microedition/khronos/opengles/GL10;", {"Ljavax/microedition/khronos/opengles/GL;"});
    builder.VirtualMethod("glGetString", "(I)Ljava/lang/String;",
                        EglUnsupportedHandler("GL10.glGetString"));
    return std::move(builder).Build();
}

Decl Declare_javax_microedition_khronos_opengles_GL10_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/microedition/khronos/opengles/GL10$Impl;", "Ljava/lang/Object;", {"Ljavax/microedition/khronos/opengles/GL10;"});
    builder.FinalMethod("glGetString", "(I)Ljava/lang/String;", GlGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
