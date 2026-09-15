// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_opengl_GLSurfaceView_EGLConfigChooser.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_EGLConfigChooser(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLConfigChooser;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_opengl_GLSurfaceView_EGLContextFactory.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_EGLContextFactory(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/opengl/GLSurfaceView$EGLContextFactory;"))
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_opengl_GLSurfaceView_Renderer.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView_Renderer(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/opengl/GLSurfaceView$Renderer;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_opengl_GLSurfaceView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLSurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/GLSurfaceView;", "Landroid/view/View;");
    builder.ConstantInt("RENDERMODE_WHEN_DIRTY", "I", 0)
        .ConstantInt("RENDERMODE_CONTINUOUSLY", "I", 1);
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
    builder.FinalMethod("setEGLContextClientVersion", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto version = call.arguments[0].AsInt();
            if (version < 1 || version > 3) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unsupported EGL context client version"};
            }
            context->gl_surface_client_versions[call.receiver.Value()] = version;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setEGLConfigChooser", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            context->gl_surface_config_specs[call.receiver.Value()] =
                {8, 8, 8, 8, call.arguments[0].AsInt() != 0 ? 16 : 0, 0};
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setEGLConfigChooser", "(IIIIII)V",
        [context](dx::IntrinsicContext& call) {
            std::vector<std::int32_t> values;
            values.reserve(6U);
            for (const auto& argument : call.arguments) {
                if (argument.AsInt() < 0) {
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "negative EGL config component"};
                }
                values.push_back(argument.AsInt());
            }
            context->gl_surface_config_specs[call.receiver.Value()] =
                std::move(values);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("setRenderMode", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto mode = call.arguments[0].AsInt();
            if (mode != 0 && mode != 1) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "renderMode"};
            }
            context->gl_surface_render_modes[call.receiver.Value()] = mode;
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getRenderMode", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->gl_surface_render_modes.find(
                call.receiver.Value());
            return dx::VmValue::Int(found == context->gl_surface_render_modes.end()
                ? 1
                : found->second);
        });
    builder.FinalMethod("requestRender", "()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("queueEvent", "(Ljava/lang/Runnable;)V",
        [context](dx::IntrinsicContext& call) {
            const auto runnable = call.arguments[0].ref;
            if (!runnable.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "runnable must not be null"};
            }
            std::scoped_lock lock(context->scheduler_mutex);
            if (context->scheduler_shutdown) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "GL thread is stopped"};
            }
            context->gl_surface_events.push_back(runnable);
            return dx::VmValue::Void();
        });
    // Render pause/resume is owned by the lifecycle driver.
    const auto lifecycle_noop = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("onPause", "()V", lifecycle_noop);
    builder.FinalMethod("onResume", "()V", lifecycle_noop);
    return std::move(builder).Build();
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

[[nodiscard]] std::uint64_t EglThreadId(dx::IntrinsicContext& call) {
    if (call.vm.AttachedThreadRuntime() != nullptr) {
        const auto current = call.vm.Threads().CurrentThreadObject();
        if (current.IsValid()) return call.vm.Threads().ThreadId(current);
    }
    return static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

[[nodiscard]] std::uint32_t NativeEgl(
    dx::IntrinsicContext& call, const Context& context,
    const std::string_view name,
    const std::span<const std::uint32_t> arguments) {
    if (context->session == nullptr) {
        ModelFailure(call, "Java EGL native registry is unavailable");
    }
    return context->session->InvokeManagedEgl(
        name, arguments, EglThreadId(call));
}

template <typename Function>
auto WithIntArray(dx::IntrinsicContext& call, const Context& context,
                  const dx::VmObjectRef array, const bool copy_back,
                  Function&& function) {
    using Result = std::invoke_result_t<Function, std::uint32_t>;
    if (!array.IsValid()) return static_cast<Result>(function(0U));
    const auto length = call.vm.Model().ArrayLength(array);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 4U);
    for (std::int32_t index = 0; index < length; ++index) {
        const auto value = static_cast<std::uint32_t>(
            call.vm.Model().GetPrimitiveElement(array, index));
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            bytes[static_cast<std::size_t>(index) * 4U + byte] =
                static_cast<std::byte>(value >> (byte * 8U));
        }
    }
    std::vector<std::byte> output;
    std::optional<Result> result;
    static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
        bytes, copy_back,
        [&](const memory::GuestAddress address) {
            result.emplace(function(address.Value()));
            return 0U;
        }, &output));
    if (copy_back) {
        for (std::int32_t index = 0; index < length; ++index) {
            std::uint32_t value{};
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                             output[static_cast<std::size_t>(index) * 4U + byte]))
                         << (byte * 8U);
            }
            call.vm.Model().SetPrimitiveElement(array, index, value);
        }
    }
    return std::move(*result);
}

template <typename Function>
auto WithIntArrayOffset(dx::IntrinsicContext& call, const Context& context,
                        const dx::VmObjectRef array, const std::int32_t offset,
                        const bool copy_back, Function&& function) {
    using Result = std::invoke_result_t<Function, std::uint32_t>;
    if (!array.IsValid()) {
        if (offset != 0) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "EGL array offset requires an array"};
        }
        return static_cast<Result>(function(0U));
    }
    const auto length = call.vm.Model().ArrayLength(array);
    if (offset < 0 || offset > length) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "EGL array offset is outside the array"};
    }
    std::vector<std::int32_t> values;
    values.reserve(static_cast<std::size_t>(length - offset));
    for (auto index = offset; index < length; ++index) {
        values.push_back(static_cast<std::int32_t>(
            call.vm.Model().GetPrimitiveElement(array, index)));
    }
    const auto temporary = call.vm.Model().NewPrimitiveArray(
        call.vm.Model().ObjectClass(array), JniPrimitiveKind::integer,
        static_cast<JniSize>(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        call.vm.Model().SetPrimitiveElement(
            temporary, static_cast<JniSize>(index),
            static_cast<std::uint32_t>(values[index]));
    }
    const auto result = WithIntArray(call, context, temporary, copy_back,
                                     std::forward<Function>(function));
    if (copy_back) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            call.vm.Model().SetPrimitiveElement(
                array, offset + static_cast<std::int32_t>(index),
                call.vm.Model().GetPrimitiveElement(
                    temporary, static_cast<JniSize>(index)));
        }
    }
    return result;
}

[[nodiscard]] dx::VmObjectRef FindWrapper(
    const std::unordered_map<std::uint32_t, std::uint32_t>& wrappers,
    const std::uint32_t handle, const dx::VmObjectRef none) {
    for (const auto& [identity, native] : wrappers) {
        if (native == handle) return dx::VmObjectRef{identity};
    }
    return none;
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
        const auto display = EnsureDisplay(call, context);
        if (context->session != nullptr) {
            context->egl.native_display = NativeEgl(
                call, context, "eglGetDisplay", std::array{0U});
            if (context->egl.native_display == 0U) {
                return dx::VmValue::Ref(context->egl.no_display);
            }
        }
        return dx::VmValue::Ref(display);
    };
}

dx::IntrinsicHandler EglInitializeHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            if (context->egl.native_display == 0U) {
                context->egl.native_display = NativeEgl(
                    call, context, "eglGetDisplay", std::array{0U});
                if (context->egl.native_display == 0U) return Bool(false);
            }
            const auto versions = call.arguments[1].ref;
            if (versions.IsValid() && call.vm.Model().ArrayLength(versions) < 2) {
                return Bool(false);
            }
            const auto result = WithIntArray(call, context, versions, true,
                [&](const std::uint32_t output) {
                    const auto minor = output == 0U ? 0U : output + 4U;
                    return Bool(NativeEgl(call, context, "eglInitialize",
                                          std::array{context->egl.native_display,
                                                     output, minor}) != 0U);
                });
            context->egl.initialized = result.AsInt() != 0;
            return result;
        }
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
        if (context->session != nullptr) {
            const auto size = call.arguments[3].AsInt();
            const auto configs = call.arguments[2].ref;
            const auto count = call.arguments[4].ref;
            if (size < 0 || !count.IsValid() ||
                call.vm.Model().ArrayLength(count) < 1) return Bool(false);
            return WithIntArray(call, context, call.arguments[1].ref, false,
                [&](const std::uint32_t attributes) {
                    return WithIntArray(call, context, count, true,
                        [&](const std::uint32_t count_address) {
                            std::array<std::byte, 4> config_bytes{};
                            std::vector<std::byte> config_output;
                            const auto result = context->session->NIO().WithTemporaryGuestMemory(
                                config_bytes, true,
                                [&](const memory::GuestAddress config_address) {
                                    return NativeEgl(call, context, "eglChooseConfig",
                                        std::array{context->egl.native_display,
                                                   attributes,
                                                   configs.IsValid() && size > 0
                                                       ? config_address.Value() : 0U,
                                                   static_cast<std::uint32_t>(size),
                                                   count_address});
                                }, &config_output);
                            if (result != 0U && configs.IsValid() && size > 0 &&
                                call.vm.Model().ArrayLength(configs) > 0) {
                                std::uint32_t handle{};
                                for (std::size_t byte = 0; byte < 4U; ++byte) {
                                    handle |= static_cast<std::uint32_t>(
                                        std::to_integer<std::uint8_t>(config_output[byte]))
                                              << (byte * 8U);
                                }
                                if (handle != 0U) {
                                    const auto wrapper = EnsureConfig(call, context);
                                    context->egl.native_config = handle;
                                    call.vm.Model().SetObjectElement(configs, 0, wrapper);
                                }
                            }
                            return Bool(result != 0U);
                        });
                });
        }
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
        if (context->session != nullptr) {
            return WithIntArray(call, context, call.arguments[3].ref, true,
                [&](const std::uint32_t output) {
                    return Bool(NativeEgl(call, context, "eglGetConfigAttrib",
                        std::array{context->egl.native_display,
                                   context->egl.native_config,
                                   call.arguments[2].cat1, output}) != 0U);
                });
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

dx::IntrinsicHandler EglGetConfigsHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            const auto configs = call.arguments[1].ref;
            const auto size = call.arguments[2].AsInt();
            const auto count = call.arguments[3].ref;
            if (size < 0 || !count.IsValid()) return Bool(false);
            return WithIntArray(call, context, count, true,
                [&](const std::uint32_t count_address) {
                    std::array<std::byte, 4> config_bytes{};
                    std::vector<std::byte> config_output;
                    const auto result = context->session->NIO().WithTemporaryGuestMemory(
                        config_bytes, true,
                        [&](const memory::GuestAddress config_address) {
                            return NativeEgl(call, context, "eglGetConfigs",
                                std::array{context->egl.native_display,
                                           configs.IsValid() && size > 0
                                               ? config_address.Value() : 0U,
                                           static_cast<std::uint32_t>(size),
                                           count_address});
                        }, &config_output);
                    if (result != 0U && configs.IsValid() && size > 0) {
                        const auto wrapper = EnsureConfig(call, context);
                        std::uint32_t handle{};
                        for (std::size_t byte = 0; byte < 4U; ++byte) {
                            handle |= static_cast<std::uint32_t>(
                                std::to_integer<std::uint8_t>(
                                    config_output[byte])) << (byte * 8U);
                        }
                        if (handle != 0U) {
                            context->egl.native_config = handle;
                            call.vm.Model().SetObjectElement(configs, 0,
                                                             wrapper);
                        }
                    }
                    return Bool(result != 0U);
                });
        }
        RequireInitialized(call, context);
        const auto configs = call.arguments[1].ref;
        const auto size = call.arguments[2].AsInt();
        const auto count = call.arguments[3].ref;
        if (size < 0 || !count.IsValid() ||
            call.vm.Model().ArrayLength(count) < 1 ||
            (configs.IsValid() && call.vm.Model().ArrayLength(configs) < size)) {
            SetError(context, kBadParameter);
            return Bool(false);
        }
        SetIntElement(call, count, 0, 1);
        if (configs.IsValid() && size > 0) {
            call.vm.Model().SetObjectElement(
                configs, 0, EnsureConfig(call, context));
        }
        return Bool(true);
    };
}

dx::IntrinsicHandler EglCreateContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return dx::VmValue::Ref(context->egl.no_context);
        RequireInitialized(call, context);
        if (!IsConfig(context, call.arguments[1].ref)) { SetError(context, kBadConfig); return dx::VmValue::Ref(context->egl.no_context); }
        const auto share = call.arguments[2].ref;
        if (context->session != nullptr) {
            std::uint32_t share_handle{};
            if (share.IsValid() && share != context->egl.no_context) {
                const auto found = context->egl.contexts.find(share.Value());
                if (found == context->egl.contexts.end()) {
                    return dx::VmValue::Ref(context->egl.no_context);
                }
                share_handle = found->second;
            }
            return WithIntArray(call, context, call.arguments[3].ref, false,
                [&](const std::uint32_t attributes) {
                    const auto handle = NativeEgl(call, context,
                        "eglCreateContext",
                        std::array{context->egl.native_display,
                                   context->egl.native_config, share_handle,
                                   attributes});
                    if (handle == 0U) {
                        return dx::VmValue::Ref(context->egl.no_context);
                    }
                    const auto instance = call.vm.NewIntrinsicInstance(
                        "Ljavax/microedition/khronos/egl/EGLContext;");
                    context->egl.contexts.emplace(instance.Value(), handle);
                    return dx::VmValue::Ref(instance);
                });
        }
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
        if (context->session != nullptr) {
            return WithIntArray(call, context, attributes, false,
                [&](const std::uint32_t attribute_address) {
                    const auto handle = NativeEgl(call, context,
                        "eglCreateWindowSurface",
                        std::array{context->egl.native_display,
                                   context->egl.native_config,
                                   holder.Value(), attribute_address});
                    if (handle == 0U) {
                        return dx::VmValue::Ref(context->egl.no_surface);
                    }
                    const auto wrapper = call.vm.NewIntrinsicInstance(
                        "Ljavax/microedition/khronos/egl/EGLSurface;");
                    context->egl.window_surface = wrapper;
                    context->egl.surfaces.emplace(wrapper.Value(), handle);
                    return dx::VmValue::Ref(wrapper);
                });
        }
        if (attributes.IsValid() && (call.vm.Model().ArrayLength(attributes) != 1 || IntElement(call, attributes, 0) != kNone)) {
            SetError(context, kBadAttribute); return dx::VmValue::Ref(context->egl.no_surface);
        }
        context->egl.window_surface = call.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLSurface;");
        return dx::VmValue::Ref(context->egl.window_surface);
    };
}

dx::IntrinsicHandler EglCreatePbufferSurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref) ||
            !IsConfig(context, call.arguments[1].ref)) {
            return dx::VmValue::Ref(context->egl.no_surface);
        }
        return WithIntArray(call, context, call.arguments[2].ref, false,
            [&](const std::uint32_t attributes) {
                const auto handle = NativeEgl(call, context,
                    "eglCreatePbufferSurface",
                    std::array{context->egl.native_display,
                               context->egl.native_config, attributes});
                if (handle == 0U) {
                    return dx::VmValue::Ref(context->egl.no_surface);
                }
                const auto wrapper = call.vm.NewIntrinsicInstance(
                    "Ljavax/microedition/khronos/egl/EGLSurface;");
                context->egl.surfaces.emplace(wrapper.Value(), handle);
                return dx::VmValue::Ref(wrapper);
            });
    };
}

dx::IntrinsicHandler EglDestroySurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            const auto found = context->egl.surfaces.find(
                call.arguments[1].ref.Value());
            if (found == context->egl.surfaces.end()) return Bool(false);
            const auto result = NativeEgl(call, context, "eglDestroySurface",
                std::array{context->egl.native_display, found->second});
            if (result != 0U) context->egl.surfaces.erase(found);
            return Bool(result != 0U);
        }
        if (call.arguments[1].ref != context->egl.window_surface) { SetError(context, kBadSurface); return Bool(false); }
        if (context->egl.current_surface == context->egl.window_surface) ModelFailure(call, "current surface cannot be destroyed");
        context->egl.window_surface = dx::VmObjectRef{}; return Bool(true);
    };
}

dx::IntrinsicHandler EglDestroyContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        const auto ref = call.arguments[1].ref;
        if (context->session != nullptr) {
            const auto found = context->egl.contexts.find(ref.Value());
            if (found == context->egl.contexts.end()) return Bool(false);
            const auto result = NativeEgl(call, context, "eglDestroyContext",
                std::array{context->egl.native_display, found->second});
            if (result != 0U) context->egl.contexts.erase(found);
            return Bool(result != 0U);
        }
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
        if (context->session != nullptr) {
            const auto surface_handle = [&](const dx::VmObjectRef value) {
                if (value == context->egl.no_surface) return 0U;
                const auto found = context->egl.surfaces.find(value.Value());
                return found == context->egl.surfaces.end() ? 0U : found->second;
            };
            const auto context_handle = [&] {
                if (egl_context == context->egl.no_context) return 0U;
                const auto found = context->egl.contexts.find(egl_context.Value());
                return found == context->egl.contexts.end() ? 0U : found->second;
            }();
            return Bool(NativeEgl(call, context, "eglMakeCurrent",
                std::array{context->egl.native_display,
                           surface_handle(draw), surface_handle(read),
                           context_handle}) != 0U);
        }
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
        if (context->session != nullptr) {
            const auto found = context->egl.surfaces.find(
                call.arguments[1].ref.Value());
            if (found == context->egl.surfaces.end()) return Bool(false);
            const auto result = NativeEgl(call, context, "eglSwapBuffers",
                std::array{context->egl.native_display, found->second});
            if (result != 0U) PaceEglSwap(*context, call.vm.ExecutionLock());
            return Bool(result != 0U);
        }
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
    return [context](dx::IntrinsicContext& call) {
        if (context->session != nullptr) {
            return dx::VmValue::Ref(
                NativeEgl(call, context, "eglGetCurrentDisplay", {}) ==
                        context->egl.native_display
                    ? context->egl.display : context->egl.no_display);
        }
        const auto current = context->egl.current_thread.has_value() &&
                             *context->egl.current_thread == std::this_thread::get_id();
        return dx::VmValue::Ref(current ? context->egl.current_display
                                        : context->egl.no_display);
    };
}

dx::IntrinsicHandler EglGetCurrentContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->session != nullptr) {
            return dx::VmValue::Ref(FindWrapper(
                context->egl.contexts,
                NativeEgl(call, context, "eglGetCurrentContext", {}),
                context->egl.no_context));
        }
        const auto current = context->egl.current_thread.has_value() &&
                             *context->egl.current_thread == std::this_thread::get_id();
        return dx::VmValue::Ref(current ? context->egl.current_context
                                        : context->egl.no_context);
    };
}

dx::IntrinsicHandler EglGetCurrentSurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto which = call.arguments[0].AsInt();
        if (context->session != nullptr) {
            return dx::VmValue::Ref(FindWrapper(
                context->egl.surfaces,
                NativeEgl(call, context, "eglGetCurrentSurface",
                          std::array{call.arguments[0].cat1}),
                context->egl.no_surface));
        }
        if (which != 0x3059 && which != 0x305A) { SetError(context, kBadParameter); return dx::VmValue::Ref(context->egl.no_surface); }
        const auto current = context->egl.current_thread.has_value() &&
                             *context->egl.current_thread == std::this_thread::get_id();
        return dx::VmValue::Ref(current ? context->egl.current_surface
                                        : context->egl.no_surface);
    };
}

dx::IntrinsicHandler EglQueryContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            const auto found = context->egl.contexts.find(
                call.arguments[1].ref.Value());
            if (found == context->egl.contexts.end()) return Bool(false);
            return WithIntArray(call, context, call.arguments[3].ref, true,
                [&](const std::uint32_t output) {
                    return Bool(NativeEgl(call, context, "eglQueryContext",
                        std::array{context->egl.native_display, found->second,
                                   call.arguments[2].cat1, output}) != 0U);
                });
        }
        RequireInitialized(call, context);
        const auto found = context->egl.contexts.find(call.arguments[1].ref.Value());
        if (found == context->egl.contexts.end()) {
            SetError(context, kBadContext);
            return Bool(false);
        }
        const auto output = call.arguments[3].ref;
        if (!output.IsValid() || call.vm.Model().ArrayLength(output) < 1) {
            SetError(context, kBadParameter);
            return Bool(false);
        }
        if (call.arguments[2].AsInt() != 0x3098) {
            SetError(context, kBadAttribute);
            return Bool(false);
        }
        SetIntElement(call, output, 0, found->second);
        return Bool(true);
    };
}

dx::IntrinsicHandler EglQueryStringHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        RequireInitialized(call, context);
        if (context->session != nullptr &&
            NativeEgl(call, context, "eglQueryString",
                      std::array{context->egl.native_display,
                                 call.arguments[1].cat1}) == 0U) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        switch (call.arguments[1].AsInt()) {
        case 0x3053: return MakeString(call, "OGPlay");
        case 0x3054: return MakeString(call, "1.4 OGPlay");
        case 0x3055: return MakeString(call, "");
        case 0x308D: return MakeString(call, "OpenGL_ES");
        default:
            SetError(context, kBadParameter);
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
    };
}

dx::IntrinsicHandler EglQuerySurfaceHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            const auto found = context->egl.surfaces.find(
                call.arguments[1].ref.Value());
            if (found == context->egl.surfaces.end()) return Bool(false);
            return WithIntArray(call, context, call.arguments[3].ref, true,
                [&](const std::uint32_t output) {
                    return Bool(NativeEgl(call, context, "eglQuerySurface",
                        std::array{context->egl.native_display, found->second,
                                   call.arguments[2].cat1, output}) != 0U);
                });
        }
        RequireInitialized(call, context);
        if (call.arguments[1].ref != context->egl.window_surface) {
            SetError(context, kBadSurface);
            return Bool(false);
        }
        const auto output = call.arguments[3].ref;
        if (!output.IsValid() || call.vm.Model().ArrayLength(output) < 1) {
            SetError(context, kBadParameter);
            return Bool(false);
        }
        switch (call.arguments[2].AsInt()) {
        case 0x3056:
            SetIntElement(call, output, 0,
                          static_cast<std::int32_t>(context->surface_height));
            break;
        case 0x3057:
            SetIntElement(call, output, 0,
                          static_cast<std::int32_t>(context->surface_width));
            break;
        case 0x3086: SetIntElement(call, output, 0, 0x3084); break;
        default:
            SetError(context, kBadAttribute);
            return Bool(false);
        }
        return Bool(true);
    };
}

dx::IntrinsicHandler EglReleaseThreadHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->session != nullptr) {
            return Bool(NativeEgl(call, context, "eglReleaseThread", {}) != 0U);
        }
        if (!context->egl.current_thread.has_value() ||
            *context->egl.current_thread != std::this_thread::get_id()) {
            return Bool(true);
        }
        if (context->session == nullptr) {
            ModelFailure(call, "guest session is absent");
        }
        context->session->ReleaseManagedSurfaceFromCallingThread();
        context->egl.current_display = dx::VmObjectRef{};
        context->egl.current_surface = dx::VmObjectRef{};
        context->egl.current_context = dx::VmObjectRef{};
        context->egl.current_thread.reset();
        return Bool(true);
    };
}

dx::IntrinsicHandler EglGetErrorHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->session != nullptr) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                NativeEgl(call, context, "eglGetError", {})));
        }
        const auto error = context->egl.last_error;
        context->egl.last_error = kSuccess;
        return dx::VmValue::Int(error);
    };
}

dx::IntrinsicHandler EglTerminateHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (!ValidateDisplay(context, call.arguments[0].ref)) return Bool(false);
        if (context->session != nullptr) {
            const auto result = NativeEgl(call, context, "eglTerminate",
                std::array{context->egl.native_display});
            if (result != 0U) {
                context->egl.native_display = 0U;
                context->egl.native_config = 0U;
                context->egl.contexts.clear();
                context->egl.surfaces.clear();
                context->egl.window_surface = dx::VmObjectRef{};
                context->egl.initialized = false;
            }
            return Bool(result != 0U);
        }
        if (context->egl.window_surface.IsValid() || !context->egl.contexts.empty() || context->egl.current_context.IsValid()) ModelFailure(call, "terminate requires retired facade objects");
        context->egl.initialized = false; return Bool(true);
    };
}

dx::IntrinsicHandler GlGetStringHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto parameter = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if (parameter != 0x1F00 && parameter != 0x1F01 && parameter != 0x1F02 &&
            parameter != 0x1F03 && parameter != 0x8B8C) {
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
        if (name == "glGetStringi" && descriptor == "(II)Ljava/lang/String;") {
            const std::array args{call.arguments[0].cat1, call.arguments[1].cat1};
            if (context->session->InvokeManagedGles(api, name, args) == 0U)
                return dx::VmValue::Ref(dx::VmObjectRef{});
            std::istringstream stream(context->session->ManagedGlString(0x1F03U));
            std::string extension;
            for (std::int32_t index = 0; index <= call.arguments[1].AsInt(); ++index) {
                if (!(stream >> extension)) {
                    return dx::VmValue::Ref(dx::VmObjectRef{});
                }
            }
            return MakeString(call, extension);
        }
        if (name == "glFenceSync" && descriptor == "(II)J") {
            const std::array args{call.arguments[0].cat1, call.arguments[1].cat1};
            return dx::VmValue::Long(context->session->InvokeManagedGles(
                api, name, args));
        }
        if (name == "glMapBufferRange" && descriptor == "(IIII)Ljava/nio/Buffer;") {
            const auto length = call.arguments[2].AsInt();
            const std::array args{call.arguments[0].cat1, call.arguments[1].cat1,
                                  call.arguments[2].cat1, call.arguments[3].cat1};
            const auto address = context->session->InvokeManagedGles(api, name, args);
            if (address == 0U) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto buffer = call.vm.NewIntrinsicInstance("Ljava/nio/DirectByteBuffer;");
            call.vm.NIO().WrapDirect(call.vm.Model().ToIdentity(buffer),
                                     memory::GuestAddress{address}, length);
            return dx::VmValue::Ref(buffer);
        }
        if (name == "glTransformFeedbackVaryings" &&
            descriptor == "(I[Ljava/lang/String;I)V") {
            const auto array = call.arguments[1].ref;
            if (!array.IsValid()) throw dx::VmJavaThrow{
                "Ljava/lang/NullPointerException;", "transform feedback varyings are null"};
            const auto count = call.vm.Model().ArrayLength(array);
            std::vector<std::uint32_t> pointers(static_cast<std::size_t>(count));
            std::function<std::uint32_t(std::int32_t)> marshal_names;
            marshal_names = [&](const std::int32_t index) -> std::uint32_t {
                if (index == count) {
                    std::vector<std::byte> bytes(pointers.size() * sizeof(std::uint32_t));
                    for (std::size_t i = 0; i < pointers.size(); ++i)
                        for (std::size_t byte = 0; byte < 4U; ++byte)
                            bytes[i * 4U + byte] = static_cast<std::byte>(pointers[i] >> (byte * 8U));
                    return context->session->NIO().WithTemporaryGuestMemory(
                        bytes, false, [&](const memory::GuestAddress address) {
                            const std::array args{
                                call.arguments[0].cat1, static_cast<std::uint32_t>(count),
                                address.Value(), call.arguments[2].cat1};
                            return context->session->InvokeManagedGles(
                                api, name, args);
                        });
                }
                const auto string = call.vm.Model().GetObjectElement(array, index);
                if (!string.IsValid()) throw dx::VmJavaThrow{
                    "Ljava/lang/NullPointerException;", "transform feedback varying is null"};
                const auto text = call.vm.StringUtf8(string);
                std::vector<std::byte> bytes(text.size() + 1U);
                for (std::size_t i = 0; i < text.size(); ++i)
                    bytes[i] = static_cast<std::byte>(text[i]);
                return context->session->NIO().WithTemporaryGuestMemory(
                    bytes, false, [&](const memory::GuestAddress address) {
                        pointers[static_cast<std::size_t>(index)] = address.Value();
                        return marshal_names(index + 1);
                    });
            };
            static_cast<void>(marshal_names(0));
            return dx::VmValue::Void();
        }
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
            if (type == "J" || type == "D") {
                const bool padded = (arguments.size() & 1U) != 0U;
                if (padded) arguments.push_back(0U);
                arguments.push_back(static_cast<std::uint32_t>(value.wide));
                arguments.push_back(static_cast<std::uint32_t>(value.wide >> 32U));
                const auto result = marshal(index + 1U);
                arguments.pop_back();
                arguments.pop_back();
                if (padded) arguments.pop_back();
                return result;
            }
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

[[nodiscard]] dx::VmObjectRef EnsureEgl14Object(
    dx::IntrinsicContext& call, dx::VmObjectRef& object,
    const char* descriptor) {
    if (!object.IsValid()) object = call.vm.NewIntrinsicInstance(descriptor);
    return object;
}

dx::IntrinsicHandler Egl14SimpleHandler(const Context& context,
                                        std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (context->session == nullptr) {
            ModelFailure(call, "EGL14 requires the native EGL registry");
        }
        if (name == "eglGetError") {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                NativeEgl(call, context, name, {})));
        }
        if (name == "eglGetDisplay") {
            if (call.arguments[0].AsInt() != 0) {
                return dx::VmValue::Ref(egl.egl14_no_display);
            }
            egl.native_display = NativeEgl(call, context, name, std::array{0U});
            return dx::VmValue::Ref(egl.native_display == 0U
                ? egl.egl14_no_display
                : EnsureEgl14Object(call, egl.egl14_display,
                                    "Landroid/opengl/EGLDisplay;"));
        }
        if (name == "eglInitialize") {
            if (egl.native_display == 0U) {
                egl.native_display = NativeEgl(
                    call, context, "eglGetDisplay", std::array{0U});
                if (egl.native_display == 0U) return Bool(false);
            }
            const auto major = WithIntArrayOffset(
                call, context, call.arguments[1].ref,
                call.arguments[2].AsInt(), true,
                [&](const std::uint32_t major_address) {
                    return WithIntArrayOffset(
                        call, context, call.arguments[3].ref,
                        call.arguments[4].AsInt(), true,
                        [&](const std::uint32_t minor_address) {
                            return NativeEgl(call, context, name,
                                std::array{egl.native_display, major_address,
                                           minor_address});
                        });
                });
            return Bool(major != 0U);
        }
        if (name == "eglTerminate") {
            const auto result = NativeEgl(call, context, name,
                                          std::array{egl.native_display});
            if (result != 0U) {
                egl.contexts.clear();
                egl.surfaces.clear();
                egl.egl14_contexts.clear();
                egl.egl14_surfaces.clear();
                egl.native_display = 0U;
                egl.native_config = 0U;
            }
            return Bool(result != 0U);
        }
        if (name == "eglQueryString") {
            if (NativeEgl(call, context, name,
                          std::array{egl.native_display,
                                     call.arguments[1].cat1}) == 0U) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            switch (call.arguments[1].AsInt()) {
            case 0x3053: return MakeString(call, "OGPlay");
            case 0x3054: return MakeString(call, "1.4 OGPlay");
            case 0x3055: return MakeString(call, "");
            case 0x308D: return MakeString(call, "OpenGL_ES");
            default: return dx::VmValue::Ref(dx::VmObjectRef{});
            }
        }
        if (name == "eglBindAPI") {
            return Bool(NativeEgl(call, context, name,
                                 std::array{call.arguments[0].cat1}) != 0U);
        }
        if (name == "eglQueryAPI") {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                NativeEgl(call, context, name, {})));
        }
        if (name == "eglWaitClient" || name == "eglWaitGL" ||
            name == "eglReleaseThread") {
            return Bool(NativeEgl(call, context, name, {}) != 0U);
        }
        if (name == "eglWaitNative") {
            return Bool(NativeEgl(call, context, name,
                                 std::array{call.arguments[0].cat1}) != 0U);
        }
        if (name == "eglSwapInterval") {
            return Bool(NativeEgl(call, context, name,
                std::array{egl.native_display,
                           call.arguments[1].cat1}) != 0U);
        }
        if (name == "eglGetCurrentDisplay") {
            return dx::VmValue::Ref(
                NativeEgl(call, context, name, {}) == egl.native_display
                    ? egl.egl14_display : egl.egl14_no_display);
        }
        if (name == "eglGetCurrentContext") {
            return dx::VmValue::Ref(FindWrapper(
                egl.egl14_contexts, NativeEgl(call, context, name, {}),
                egl.egl14_no_context));
        }
        if (name == "eglGetCurrentSurface") {
            return dx::VmValue::Ref(FindWrapper(
                egl.egl14_surfaces,
                NativeEgl(call, context, name,
                          std::array{call.arguments[0].cat1}),
                egl.egl14_no_surface));
        }
        throw std::logic_error("unsupported EGL14 simple handler: " + name);
    };
}

dx::IntrinsicHandler Egl14CreateContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        const auto share = call.arguments[2].ref;
        const auto found = egl.egl14_contexts.find(share.Value());
        const auto share_handle = share == egl.egl14_no_context
            ? 0U : found == egl.egl14_contexts.end() ? 0U : found->second;
        return WithIntArrayOffset(call, context, call.arguments[3].ref,
            call.arguments[4].AsInt(), false,
            [&](const std::uint32_t attributes) {
                const auto handle = NativeEgl(call, context, "eglCreateContext",
                    std::array{egl.native_display, egl.native_config,
                               share_handle, attributes});
                if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_context);
                const auto wrapper = call.vm.NewIntrinsicInstance(
                    "Landroid/opengl/EGLContext;");
                egl.egl14_contexts.emplace(wrapper.Value(), handle);
                return dx::VmValue::Ref(wrapper);
            });
    };
}

dx::IntrinsicHandler Egl14CreatePbufferHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        return WithIntArrayOffset(call, context, call.arguments[2].ref,
            call.arguments[3].AsInt(), false,
            [&](const std::uint32_t attributes) {
                const auto handle = NativeEgl(call, context,
                    "eglCreatePbufferSurface",
                    std::array{egl.native_display, egl.native_config,
                               attributes});
                if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_surface);
                const auto wrapper = call.vm.NewIntrinsicInstance(
                    "Landroid/opengl/EGLSurface;");
                egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                return dx::VmValue::Ref(wrapper);
            });
    };
}

dx::IntrinsicHandler Egl14CreateWindowHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!call.arguments[2].ref.IsValid()) {
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        return WithIntArrayOffset(call, context, call.arguments[3].ref,
            call.arguments[4].AsInt(), false,
            [&](const std::uint32_t attributes) {
                const auto handle = NativeEgl(call, context,
                    "eglCreateWindowSurface",
                    std::array{egl.native_display, egl.native_config,
                               call.arguments[2].ref.Value(), attributes});
                if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_surface);
                const auto wrapper = call.vm.NewIntrinsicInstance(
                    "Landroid/opengl/EGLSurface;");
                egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                return dx::VmValue::Ref(wrapper);
            });
    };
}

dx::IntrinsicHandler Egl14ObjectHandler(const Context& context,
                                        std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (name == "eglDestroyContext") {
            const auto found = egl.egl14_contexts.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_contexts.end()) return Bool(false);
            const auto result = NativeEgl(call, context, name,
                std::array{egl.native_display, found->second});
            if (result != 0U) egl.egl14_contexts.erase(found);
            return Bool(result != 0U);
        }
        if (name == "eglDestroySurface" || name == "eglSwapBuffers") {
            const auto found = egl.egl14_surfaces.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_surfaces.end()) return Bool(false);
            const auto result = NativeEgl(call, context, name,
                std::array{egl.native_display, found->second});
            if (result != 0U && name == "eglDestroySurface") {
                egl.egl14_surfaces.erase(found);
            }
            return Bool(result != 0U);
        }
        if (name == "eglMakeCurrent") {
            const auto handle = [](const auto& map, const dx::VmObjectRef ref,
                                   const dx::VmObjectRef none) {
                if (ref == none) return 0U;
                const auto found = map.find(ref.Value());
                return found == map.end() ? 0U : found->second;
            };
            return Bool(NativeEgl(call, context, name,
                std::array{egl.native_display,
                    handle(egl.egl14_surfaces, call.arguments[1].ref,
                           egl.egl14_no_surface),
                    handle(egl.egl14_surfaces, call.arguments[2].ref,
                           egl.egl14_no_surface),
                    handle(egl.egl14_contexts, call.arguments[3].ref,
                           egl.egl14_no_context)}) != 0U);
        }
        throw std::logic_error("unsupported EGL14 object handler: " + name);
    };
}

dx::IntrinsicHandler Egl14QueryValueHandler(const Context& context,
                                            std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        std::uint32_t handle = egl.native_config;
        if (name == "eglQueryContext") {
            const auto found = egl.egl14_contexts.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_contexts.end()) return Bool(false);
            handle = found->second;
        } else if (name == "eglQuerySurface") {
            const auto found = egl.egl14_surfaces.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_surfaces.end()) return Bool(false);
            handle = found->second;
        }
        return WithIntArrayOffset(call, context, call.arguments[3].ref,
            call.arguments[4].AsInt(), true,
            [&](const std::uint32_t output) {
                return Bool(NativeEgl(call, context, name,
                    std::array{egl.native_display, handle,
                               call.arguments[2].cat1, output}) != 0U);
            });
    };
}

dx::IntrinsicHandler Egl14ConfigsHandler(const Context& context,
                                         const bool choose) {
    return [context, choose](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        const auto configs_index = choose ? 3U : 1U;
        const auto configs_offset_index = choose ? 4U : 2U;
        const auto size_index = choose ? 5U : 3U;
        const auto count_index = choose ? 6U : 4U;
        const auto count_offset_index = choose ? 7U : 5U;
        const auto configs = call.arguments[configs_index].ref;
        const auto configs_offset = call.arguments[configs_offset_index].AsInt();
        const auto size = call.arguments[size_index].AsInt();
        if (size < 0 || configs_offset < 0 ||
            (configs.IsValid() && configs_offset >
                 call.vm.Model().ArrayLength(configs))) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "EGLConfig array range is invalid"};
        }
        const auto invoke = [&](const std::uint32_t attributes) {
            return WithIntArrayOffset(call, context,
                call.arguments[count_index].ref,
                call.arguments[count_offset_index].AsInt(), true,
                [&](const std::uint32_t count_address) {
                    std::array<std::byte, 4> config_bytes{};
                    std::vector<std::byte> output;
                    const auto result = context->session->NIO().WithTemporaryGuestMemory(
                        config_bytes, true,
                        [&](const memory::GuestAddress config_address) {
                            if (choose) {
                                return NativeEgl(call, context, "eglChooseConfig",
                                    std::array{egl.native_display, attributes,
                                        configs.IsValid() && size > 0
                                            ? config_address.Value() : 0U,
                                        static_cast<std::uint32_t>(size),
                                        count_address});
                            }
                            return NativeEgl(call, context, "eglGetConfigs",
                                std::array{egl.native_display,
                                    configs.IsValid() && size > 0
                                        ? config_address.Value() : 0U,
                                    static_cast<std::uint32_t>(size),
                                    count_address});
                        }, &output);
                    if (result != 0U && configs.IsValid() && size > 0) {
                        std::uint32_t handle{};
                        for (std::size_t byte = 0; byte < 4U; ++byte) {
                            handle |= static_cast<std::uint32_t>(
                                std::to_integer<std::uint8_t>(output[byte]))
                                      << (byte * 8U);
                        }
                        if (handle != 0U) {
                            egl.native_config = handle;
                            const auto wrapper = EnsureEgl14Object(
                                call, egl.egl14_config,
                                "Landroid/opengl/EGLConfig;");
                            call.vm.Model().SetObjectElement(
                                configs, configs_offset, wrapper);
                        }
                    }
                    return Bool(result != 0U);
                });
        };
        if (!choose) return invoke(0U);
        return WithIntArrayOffset(call, context, call.arguments[1].ref,
                                  call.arguments[2].AsInt(), false, invoke);
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

Decl Declare_android_opengl_EGL14(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGL14;", "Ljava/lang/Object;");
    constexpr std::array constants{
        std::pair{"EGL_DEFAULT_DISPLAY", 0},
        std::pair{"EGL_FALSE", 0},
        std::pair{"EGL_TRUE", 1},
        std::pair{"EGL_SUCCESS", 0x3000},
        std::pair{"EGL_NOT_INITIALIZED", 0x3001},
        std::pair{"EGL_BAD_ACCESS", 0x3002},
        std::pair{"EGL_BAD_ALLOC", 0x3003},
        std::pair{"EGL_BAD_ATTRIBUTE", 0x3004},
        std::pair{"EGL_BAD_CONFIG", 0x3005},
        std::pair{"EGL_BAD_CONTEXT", 0x3006},
        std::pair{"EGL_BAD_CURRENT_SURFACE", 0x3007},
        std::pair{"EGL_BAD_DISPLAY", 0x3008},
        std::pair{"EGL_BAD_MATCH", 0x3009},
        std::pair{"EGL_BAD_NATIVE_PIXMAP", 0x300A},
        std::pair{"EGL_BAD_NATIVE_WINDOW", 0x300B},
        std::pair{"EGL_BAD_PARAMETER", 0x300C},
        std::pair{"EGL_BAD_SURFACE", 0x300D},
        std::pair{"EGL_CONTEXT_LOST", 0x300E},
        std::pair{"EGL_BUFFER_SIZE", 0x3020},
        std::pair{"EGL_ALPHA_SIZE", 0x3021},
        std::pair{"EGL_BLUE_SIZE", 0x3022},
        std::pair{"EGL_GREEN_SIZE", 0x3023},
        std::pair{"EGL_RED_SIZE", 0x3024},
        std::pair{"EGL_DEPTH_SIZE", 0x3025},
        std::pair{"EGL_STENCIL_SIZE", 0x3026},
        std::pair{"EGL_CONFIG_CAVEAT", 0x3027},
        std::pair{"EGL_CONFIG_ID", 0x3028},
        std::pair{"EGL_LEVEL", 0x3029},
        std::pair{"EGL_MAX_PBUFFER_HEIGHT", 0x302A},
        std::pair{"EGL_MAX_PBUFFER_PIXELS", 0x302B},
        std::pair{"EGL_MAX_PBUFFER_WIDTH", 0x302C},
        std::pair{"EGL_NATIVE_RENDERABLE", 0x302D},
        std::pair{"EGL_NATIVE_VISUAL_ID", 0x302E},
        std::pair{"EGL_NATIVE_VISUAL_TYPE", 0x302F},
        std::pair{"EGL_SAMPLES", 0x3031},
        std::pair{"EGL_SAMPLE_BUFFERS", 0x3032},
        std::pair{"EGL_SURFACE_TYPE", 0x3033},
        std::pair{"EGL_TRANSPARENT_TYPE", 0x3034},
        std::pair{"EGL_TRANSPARENT_BLUE_VALUE", 0x3035},
        std::pair{"EGL_TRANSPARENT_GREEN_VALUE", 0x3036},
        std::pair{"EGL_TRANSPARENT_RED_VALUE", 0x3037},
        std::pair{"EGL_NONE", 0x3038},
        std::pair{"EGL_BIND_TO_TEXTURE_RGB", 0x3039},
        std::pair{"EGL_BIND_TO_TEXTURE_RGBA", 0x303A},
        std::pair{"EGL_MIN_SWAP_INTERVAL", 0x303B},
        std::pair{"EGL_MAX_SWAP_INTERVAL", 0x303C},
        std::pair{"EGL_LUMINANCE_SIZE", 0x303D},
        std::pair{"EGL_ALPHA_MASK_SIZE", 0x303E},
        std::pair{"EGL_COLOR_BUFFER_TYPE", 0x303F},
        std::pair{"EGL_RENDERABLE_TYPE", 0x3040},
        std::pair{"EGL_MATCH_NATIVE_PIXMAP", 0x3041},
        std::pair{"EGL_CONFORMANT", 0x3042},
        std::pair{"EGL_SLOW_CONFIG", 0x3050},
        std::pair{"EGL_NON_CONFORMANT_CONFIG", 0x3051},
        std::pair{"EGL_TRANSPARENT_RGB", 0x3052},
        std::pair{"EGL_RGB_BUFFER", 0x308E},
        std::pair{"EGL_LUMINANCE_BUFFER", 0x308F},
        std::pair{"EGL_NO_TEXTURE", 0x305C},
        std::pair{"EGL_TEXTURE_RGB", 0x305D},
        std::pair{"EGL_TEXTURE_RGBA", 0x305E},
        std::pair{"EGL_TEXTURE_2D", 0x305F},
        std::pair{"EGL_PBUFFER_BIT", 0x0001},
        std::pair{"EGL_PIXMAP_BIT", 0x0002},
        std::pair{"EGL_WINDOW_BIT", 0x0004},
        std::pair{"EGL_VG_COLORSPACE_LINEAR_BIT", 0x0020},
        std::pair{"EGL_VG_ALPHA_FORMAT_PRE_BIT", 0x0040},
        std::pair{"EGL_MULTISAMPLE_RESOLVE_BOX_BIT", 0x0200},
        std::pair{"EGL_SWAP_BEHAVIOR_PRESERVED_BIT", 0x0400},
        std::pair{"EGL_OPENGL_ES_BIT", 0x0001},
        std::pair{"EGL_OPENVG_BIT", 0x0002},
        std::pair{"EGL_OPENGL_ES2_BIT", 0x0004},
        std::pair{"EGL_OPENGL_BIT", 0x0008},
        std::pair{"EGL_VENDOR", 0x3053},
        std::pair{"EGL_VERSION", 0x3054},
        std::pair{"EGL_EXTENSIONS", 0x3055},
        std::pair{"EGL_CLIENT_APIS", 0x308D},
        std::pair{"EGL_HEIGHT", 0x3056},
        std::pair{"EGL_WIDTH", 0x3057},
        std::pair{"EGL_LARGEST_PBUFFER", 0x3058},
        std::pair{"EGL_TEXTURE_FORMAT", 0x3080},
        std::pair{"EGL_TEXTURE_TARGET", 0x3081},
        std::pair{"EGL_MIPMAP_TEXTURE", 0x3082},
        std::pair{"EGL_MIPMAP_LEVEL", 0x3083},
        std::pair{"EGL_RENDER_BUFFER", 0x3086},
        std::pair{"EGL_VG_COLORSPACE", 0x3087},
        std::pair{"EGL_VG_ALPHA_FORMAT", 0x3088},
        std::pair{"EGL_HORIZONTAL_RESOLUTION", 0x3090},
        std::pair{"EGL_VERTICAL_RESOLUTION", 0x3091},
        std::pair{"EGL_PIXEL_ASPECT_RATIO", 0x3092},
        std::pair{"EGL_SWAP_BEHAVIOR", 0x3093},
        std::pair{"EGL_MULTISAMPLE_RESOLVE", 0x3099},
        std::pair{"EGL_BACK_BUFFER", 0x3084},
        std::pair{"EGL_SINGLE_BUFFER", 0x3085},
        std::pair{"EGL_VG_COLORSPACE_sRGB", 0x3089},
        std::pair{"EGL_VG_COLORSPACE_LINEAR", 0x308A},
        std::pair{"EGL_VG_ALPHA_FORMAT_NONPRE", 0x308B},
        std::pair{"EGL_VG_ALPHA_FORMAT_PRE", 0x308C},
        std::pair{"EGL_DISPLAY_SCALING", 10000},
        std::pair{"EGL_BUFFER_PRESERVED", 0x3094},
        std::pair{"EGL_BUFFER_DESTROYED", 0x3095},
        std::pair{"EGL_OPENVG_IMAGE", 0x3096},
        std::pair{"EGL_CONTEXT_CLIENT_TYPE", 0x3097},
        std::pair{"EGL_CONTEXT_CLIENT_VERSION", 0x3098},
        std::pair{"EGL_MULTISAMPLE_RESOLVE_DEFAULT", 0x309A},
        std::pair{"EGL_MULTISAMPLE_RESOLVE_BOX", 0x309B},
        std::pair{"EGL_OPENGL_ES_API", 0x30A0},
        std::pair{"EGL_OPENVG_API", 0x30A1},
        std::pair{"EGL_OPENGL_API", 0x30A2},
        std::pair{"EGL_DRAW", 0x3059},
        std::pair{"EGL_READ", 0x305A},
        std::pair{"EGL_CORE_NATIVE_ENGINE", 0x305B}};
    for (const auto& [name, value] : constants) {
        builder.ConstantInt(name, "I", value);
    }
    builder.StaticField("EGL_NO_DISPLAY", "Landroid/opengl/EGLDisplay;")
        .StaticField("EGL_NO_CONTEXT", "Landroid/opengl/EGLContext;")
        .StaticField("EGL_NO_SURFACE", "Landroid/opengl/EGLSurface;");
    builder.ClassInitializer([context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        egl.egl14_no_display = call.vm.NewIntrinsicInstance(
            "Landroid/opengl/EGLDisplay;");
        egl.egl14_no_context = call.vm.NewIntrinsicInstance(
            "Landroid/opengl/EGLContext;");
        egl.egl14_no_surface = call.vm.NewIntrinsicInstance(
            "Landroid/opengl/EGLSurface;");
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_DISPLAY",
            "Landroid/opengl/EGLDisplay;", egl.egl14_no_display);
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_CONTEXT",
            "Landroid/opengl/EGLContext;", egl.egl14_no_context);
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_SURFACE",
            "Landroid/opengl/EGLSurface;", egl.egl14_no_surface);
        return dx::VmValue::Void();
    });
    const auto simple = [&](const char* name, const char* descriptor) {
        builder.StaticMethod(name, descriptor, Egl14SimpleHandler(context, name));
    };
    simple("eglGetError", "()I");
    simple("eglGetDisplay", "(I)Landroid/opengl/EGLDisplay;");
    simple("eglInitialize", "(Landroid/opengl/EGLDisplay;[II[II)Z");
    simple("eglTerminate", "(Landroid/opengl/EGLDisplay;)Z");
    simple("eglQueryString", "(Landroid/opengl/EGLDisplay;I)Ljava/lang/String;");
    simple("eglBindAPI", "(I)Z");
    simple("eglQueryAPI", "()I");
    simple("eglWaitClient", "()Z");
    simple("eglReleaseThread", "()Z");
    simple("eglSwapInterval", "(Landroid/opengl/EGLDisplay;I)Z");
    simple("eglGetCurrentContext", "()Landroid/opengl/EGLContext;");
    simple("eglGetCurrentSurface", "(I)Landroid/opengl/EGLSurface;");
    simple("eglGetCurrentDisplay", "()Landroid/opengl/EGLDisplay;");
    simple("eglWaitGL", "()Z");
    simple("eglWaitNative", "(I)Z");
    builder.StaticMethod("eglGetConfigs",
        "(Landroid/opengl/EGLDisplay;[Landroid/opengl/EGLConfig;II[II)Z",
        Egl14ConfigsHandler(context, false));
    builder.StaticMethod("eglChooseConfig",
        "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z",
        Egl14ConfigsHandler(context, true));
    builder.StaticMethod("eglGetConfigAttrib",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Z",
        Egl14QueryValueHandler(context, "eglGetConfigAttrib"));
    builder.StaticMethod("eglCreateWindowSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Ljava/lang/Object;[II)Landroid/opengl/EGLSurface;",
        Egl14CreateWindowHandler(context));
    builder.StaticMethod("eglCreatePbufferSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
        Egl14CreatePbufferHandler(context));
    builder.StaticMethod("eglCreateContext",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
        Egl14CreateContextHandler(context));
    for (const auto [name, descriptor] : {
             std::pair{"eglDestroyContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z"},
             std::pair{"eglDestroySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"},
             std::pair{"eglMakeCurrent", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z"},
             std::pair{"eglSwapBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"}}) {
        builder.StaticMethod(name, descriptor, Egl14ObjectHandler(context, name));
    }
    builder.StaticMethod("eglQueryContext",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;I[II)Z",
        Egl14QueryValueHandler(context, "eglQueryContext"));
    builder.StaticMethod("eglQuerySurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I[II)Z",
        Egl14QueryValueHandler(context, "eglQuerySurface"));
    builder.StaticMethod("eglCreatePixmapSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;",
        [context](dx::IntrinsicContext& call) {
            auto& egl = context->egl;
            return WithIntArrayOffset(call, context, call.arguments[3].ref,
                call.arguments[4].AsInt(), false,
                [&](const std::uint32_t attributes) {
                    const auto handle = NativeEgl(call, context,
                        "eglCreatePixmapSurface",
                        std::array{egl.native_display, egl.native_config,
                                   call.arguments[2].cat1, attributes});
                    if (handle == 0U) {
                        return dx::VmValue::Ref(egl.egl14_no_surface);
                    }
                    const auto wrapper = call.vm.NewIntrinsicInstance(
                        "Landroid/opengl/EGLSurface;");
                    egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                    return dx::VmValue::Ref(wrapper);
                });
        });
    builder.StaticMethod("eglCreatePbufferFromClientBuffer",
        "(Landroid/opengl/EGLDisplay;IILandroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
        [context](dx::IntrinsicContext& call) {
            auto& egl = context->egl;
            return WithIntArrayOffset(call, context, call.arguments[4].ref,
                call.arguments[5].AsInt(), false,
                [&](const std::uint32_t attributes) {
                    const auto handle = NativeEgl(call, context,
                        "eglCreatePbufferFromClientBuffer",
                        std::array{egl.native_display, call.arguments[1].cat1,
                                   call.arguments[2].cat1, egl.native_config,
                                   attributes});
                    if (handle == 0U) {
                        return dx::VmValue::Ref(egl.egl14_no_surface);
                    }
                    const auto wrapper = call.vm.NewIntrinsicInstance(
                        "Landroid/opengl/EGLSurface;");
                    egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                    return dx::VmValue::Ref(wrapper);
                });
        });
    for (const auto [name, descriptor] : {
             std::pair{"eglSurfaceAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;II)Z"},
             std::pair{"eglBindTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
             std::pair{"eglReleaseTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
             std::pair{"eglCopyBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"}}) {
        builder.StaticMethod(name, descriptor,
            [context, method = std::string{name}](dx::IntrinsicContext& call) {
                auto& egl = context->egl;
                const auto found = egl.egl14_surfaces.find(
                    call.arguments[1].ref.Value());
                const auto surface = found == egl.egl14_surfaces.end()
                    ? 0U : found->second;
                std::vector<std::uint32_t> arguments{
                    egl.native_display, surface,
                    static_cast<std::uint32_t>(call.arguments[2].cat1)};
                if (method == "eglSurfaceAttrib") {
                    arguments.push_back(
                        static_cast<std::uint32_t>(call.arguments[3].cat1));
                }
                return Bool(NativeEgl(call, context, method, arguments) != 0U);
            });
    }
    return std::move(builder).Build();
}
Decl Declare_android_opengl_GLES30(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES30;", "Landroid/opengl/GLES20;",
        gles::GlesApi::gles3, generated_java_gles::kGLES30Methods,
        generated_java_gles::kGLES30Constants);
}

dx::IntrinsicHandler EglWaitHandler(const Context& context,
                                    std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        if (context->session == nullptr) return EglUnsupportedHandler(name)(call);
        if (name == "eglWaitNative") {
            return Bool(NativeEgl(call, context, name,
                                 std::array{call.arguments[0].cat1}) != 0U);
        }
        return Bool(NativeEgl(call, context, name, {}) != 0U);
    };
}

dx::IntrinsicHandler EglCreatePixmapHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->session == nullptr) {
            return EglUnsupportedHandler("eglCreatePixmapSurface")(call);
        }
        return WithIntArray(call, context, call.arguments[3].ref, false,
            [&](const std::uint32_t attributes) {
                const auto result = NativeEgl(call, context,
                    "eglCreatePixmapSurface",
                    std::array{context->egl.native_display,
                               context->egl.native_config,
                               call.arguments[2].ref.Value(), attributes});
                return dx::VmValue::Ref(result == 0U
                    ? context->egl.no_surface : dx::VmObjectRef{});
            });
    };
}

dx::IntrinsicHandler EglCopyBuffersHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        if (context->session == nullptr) {
            return EglUnsupportedHandler("eglCopyBuffers")(call);
        }
        const auto found = context->egl.surfaces.find(
            call.arguments[1].ref.Value());
        return Bool(found != context->egl.surfaces.end() &&
            NativeEgl(call, context, "eglCopyBuffers",
                std::array{context->egl.native_display, found->second,
                           call.arguments[2].ref.Value()}) != 0U);
    };
}

[[nodiscard]] std::uint32_t Egl14WrapperHandle(
    const DexVmAndroidContext::EglFacadeState& egl,
    const dx::VmObjectRef object) {
    if (object == egl.egl14_no_display || object == egl.egl14_no_context ||
        object == egl.egl14_no_surface) return 0U;
    if (object == egl.egl14_display) return egl.native_display;
    if (object == egl.egl14_config) return egl.native_config;
    if (const auto found = egl.egl14_contexts.find(object.Value());
        found != egl.egl14_contexts.end()) return found->second;
    if (const auto found = egl.egl14_surfaces.find(object.Value());
        found != egl.egl14_surfaces.end()) return found->second;
    return 0U;
}

Decl Declare_android_opengl_EGLObjectHandle(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGLObjectHandle;", "Ljava/lang/Object;");
    builder.VirtualMethod("getHandle", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            Egl14WrapperHandle(context->egl, call.receiver)));
    });
    builder.OverrideMethod("hashCode", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            Egl14WrapperHandle(context->egl, call.receiver)));
    });
    return std::move(builder).Build();
}

dx::IntrinsicHandler Egl14EqualsHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto other = call.arguments[0].ref;
        if (!other.IsValid() ||
            call.vm.Model().ObjectClass(other) !=
                call.vm.Model().ObjectClass(call.receiver)) {
            return Bool(false);
        }
        return Bool(Egl14WrapperHandle(context->egl, call.receiver) ==
                    Egl14WrapperHandle(context->egl, other));
    };
}

Decl Declare_android_opengl_EGLConfig(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGLConfig;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
                        Egl14EqualsHandler(context));
    return std::move(builder).Build();
}
Decl Declare_android_opengl_EGLContext(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGLContext;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
                        Egl14EqualsHandler(context));
    return std::move(builder).Build();
}
Decl Declare_android_opengl_EGLDisplay(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGLDisplay;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
                        Egl14EqualsHandler(context));
    return std::move(builder).Build();
}
Decl Declare_android_opengl_EGLSurface(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/opengl/EGLSurface;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
                        Egl14EqualsHandler(context));
    return std::move(builder).Build();
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
    builder.FinalMethod("eglCreatePbufferSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)Ljavax/microedition/khronos/egl/EGLSurface;", EglCreatePbufferSurfaceHandler(context));
    builder.FinalMethod("eglCreateWindowSurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;", EglCreateWindowSurfaceHandler(context));
    builder.FinalMethod("eglDestroyContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z", EglDestroyContextHandler(context));
    builder.FinalMethod("eglDestroySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", EglDestroySurfaceHandler(context));
    builder.FinalMethod("eglGetConfigAttrib", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z", EglGetConfigAttribHandler(context));
    builder.FinalMethod("eglGetConfigs", "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z", EglGetConfigsHandler(context));
    builder.FinalMethod("eglGetCurrentContext", "()Ljavax/microedition/khronos/egl/EGLContext;", EglGetCurrentContextHandler(context));
    builder.FinalMethod("eglGetCurrentDisplay", "()Ljavax/microedition/khronos/egl/EGLDisplay;", EglGetCurrentDisplayHandler(context));
    builder.FinalMethod("eglGetCurrentSurface", "(I)Ljavax/microedition/khronos/egl/EGLSurface;", EglGetCurrentSurfaceHandler(context));
    builder.FinalMethod("eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;", EglGetDisplayHandler(context));
    builder.FinalMethod("eglGetError", "()I", EglGetErrorHandler(context));
    builder.FinalMethod("eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z", EglInitializeHandler(context));
    builder.FinalMethod("eglMakeCurrent", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z", EglMakeCurrentHandler(context));
    builder.FinalMethod("eglQueryContext", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z", EglQueryContextHandler(context));
    builder.FinalMethod("eglQueryString", "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;", EglQueryStringHandler(context));
    builder.FinalMethod("eglQuerySurface", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;I[I)Z", EglQuerySurfaceHandler(context));
    builder.FinalMethod("eglReleaseThread", "()Z", EglReleaseThreadHandler(context));
    builder.FinalMethod("eglSwapBuffers", "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", EglSwapBuffersHandler(context));
    builder.FinalMethod("eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", EglTerminateHandler(context));
    builder.FinalMethod("eglCopyBuffers",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljava/lang/Object;)Z",
        EglCopyBuffersHandler(context));
    builder.FinalMethod("eglCreatePixmapSurface",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
        EglCreatePixmapHandler(context));
    builder.FinalMethod("eglWaitGL", "()Z",
        EglWaitHandler(context, "eglWaitGL"));
    builder.FinalMethod("eglWaitNative", "(ILjava/lang/Object;)Z",
        EglWaitHandler(context, "eglWaitNative"));
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
