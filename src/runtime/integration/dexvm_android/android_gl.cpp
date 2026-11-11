#include "rom_window.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/raster/bitmap.h"
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from javax_microedition_khronos_egl.cpp ----
#include "catalog.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <functional>
#include <numbers>
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

constexpr std::int32_t kBadConfig = 0x3005;
constexpr std::int32_t kBadContext = 0x3006;
constexpr std::int32_t kBadDisplay = 0x3008;
constexpr std::int32_t kBadNativeWindow = 0x300B;
constexpr std::int32_t kBadParameter = 0x300C;
constexpr std::int32_t kBadSurface = 0x300D;
constexpr std::int32_t kNone = 0x3038;

const std::unordered_map<std::int32_t, std::int32_t> kConfigFacts{
    {0x3020, 32},    {0x3021, 8},    {0x3022, 8},     {0x3023, 8}, {0x3024, 8}, {0x3025, 24},     {0x3026, 8},
    {0x3027, kNone}, {0x3028, 1},    {0x3029, 0},     {0x302D, 0}, {0x302E, 0}, {0x302F, kNone},  {0x3031, 0},
    {0x3032, 0},     {0x3033, 0x05}, {0x3034, kNone}, {0x303D, 0}, {0x303E, 0}, {0x303F, 0x308E}, {0x3040, 0x05}};

[[nodiscard]] dx::VmValue Bool(const bool value) {
    return dx::VmValue::Int(value ? 1 : 0);
}

void Record(dx::IntrinsicContext& call, const std::string& id) {
    if (auto* ledger = call.vm.Ledger(); ledger != nullptr) {
        ledger->RecordUnimplemented(id, 0);
    }
}

[[noreturn]] void ModelFailure(dx::IntrinsicContext& call, const std::string& detail) {
    Record(call, "dexvm.egl_facade.model");
    throw std::runtime_error("EGL facade model violation: " + detail);
}

[[nodiscard]] std::uint32_t ConfigHandle(const std::unordered_map<std::uint32_t, std::uint32_t>& configs,
                                         const dx::VmObjectRef ref) {
    const auto found = configs.find(ref.Value());
    return found == configs.end() ? 0U : found->second;
}

void LatchEglError(dx::IntrinsicContext& call, const Context& context, std::uint32_t error);

[[nodiscard]] bool ValidateEgl14Display(dx::IntrinsicContext& call, const Context& context,
                                        const dx::VmObjectRef display) {
    if (display.IsValid() && display == context->egl.egl14_display) return true;
    LatchEglError(call, context, kBadDisplay);
    return false;
}

[[nodiscard]] std::uint64_t EglThreadId(dx::IntrinsicContext& call, const Context& context) {
    if (context->native_thread_for_context) return context->native_thread_for_context(call.vm.CurrentContextToken());
    if (call.vm.AttachedThreadRuntime() != nullptr) {
        const auto current = call.vm.Threads().CurrentThreadObject();
        if (current.IsValid()) return call.vm.Threads().ThreadId(current);
    }
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

[[nodiscard]] std::uint32_t NativeGles(dx::IntrinsicContext& call, const Context& context, const gles::GlesApi api,
                                       const std::string_view name, const std::span<const std::uint32_t> arguments) {
    return context->session->InvokeManagedGles(api, name, arguments, EglThreadId(call, context));
}

[[nodiscard]] std::uint32_t NativeEgl(dx::IntrinsicContext& call, const Context& context, const std::string_view name,
                                      const std::span<const std::uint32_t> arguments) {
    if (context->session == nullptr) {
        ModelFailure(call, "Java EGL native registry is unavailable");
    }
    const auto thread = EglThreadId(call, context);
    if (name != "eglSwapBuffers") return context->session->InvokeManagedEgl(name, arguments, thread);

    // The boundary swap uses resolved handles and has its own graphics lock.
    // Reacquire the VM only after the boundary has released that lock.
    auto& lock = call.vm.ExecutionLock();
    const auto depth = lock.ReleaseForBlocking(false);
    std::uint32_t result;
    try {
        result = context->session->InvokeManagedEgl(name, arguments, thread);
    } catch (...) {
        lock.ReacquireAfterBlocking(depth, false);
        throw;
    }
    lock.ReacquireAfterBlocking(depth, false);
    return result;
}

void LatchEglError(dx::IntrinsicContext& call, const Context& context, const std::uint32_t error) {
    if (context->session != nullptr) {
        context->session->LatchManagedEglError(EglThreadId(call, context), error);
    } else {
        ModelFailure(call, "Java EGL native registry is unavailable");
    }
}

template <typename Function>
auto WithIntArray(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef array, const bool copy_back,
                  Function&& function) {
    using Result = std::invoke_result_t<Function, std::uint32_t>;
    if (!array.IsValid()) return static_cast<Result>(function(0U));
    const auto length = call.vm.Model().ArrayLength(array);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 4U);
    for (std::int32_t index = 0; index < length; ++index) {
        const auto value = static_cast<std::uint32_t>(call.vm.Model().GetPrimitiveElement(array, index));
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            bytes[static_cast<std::size_t>(index) * 4U + byte] = static_cast<std::byte>(value >> (byte * 8U));
        }
    }
    std::vector<std::byte> output;
    std::optional<Result> result;
    static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
        bytes, copy_back,
        [&](const memory::GuestAddress address) {
            result.emplace(function(address.Value()));
            return 0U;
        },
        &output));
    if (copy_back) {
        for (std::int32_t index = 0; index < length; ++index) {
            std::uint32_t value{};
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                value |= static_cast<std::uint32_t>(
                             std::to_integer<std::uint8_t>(output[static_cast<std::size_t>(index) * 4U + byte]))
                         << (byte * 8U);
            }
            call.vm.Model().SetPrimitiveElement(array, index, value);
        }
    }
    return std::move(*result);
}

template <typename Function>
auto WithIntArrayOffset(dx::IntrinsicContext& call, const Context& context, const dx::VmObjectRef array,
                        const std::int32_t offset, const bool copy_back, Function&& function) {
    using Result = std::invoke_result_t<Function, std::uint32_t>;
    if (!array.IsValid()) {
        if (offset != 0) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "EGL array offset requires an array"};
        }
        return static_cast<Result>(function(0U));
    }
    const auto length = call.vm.Model().ArrayLength(array);
    if (offset < 0 || offset > length) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "EGL array offset is outside the array"};
    }
    std::vector<std::int32_t> values;
    values.reserve(static_cast<std::size_t>(length - offset));
    for (auto index = offset; index < length; ++index) {
        values.push_back(static_cast<std::int32_t>(call.vm.Model().GetPrimitiveElement(array, index)));
    }
    const auto temporary = call.vm.Model().NewPrimitiveArray(
        call.vm.Model().ObjectClass(array), JniPrimitiveKind::integer, static_cast<JniSize>(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        call.vm.Model().SetPrimitiveElement(temporary, static_cast<JniSize>(index),
                                            static_cast<std::uint32_t>(values[index]));
    }
    const auto result = WithIntArray(call, context, temporary, copy_back, std::forward<Function>(function));
    if (copy_back) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            call.vm.Model().SetPrimitiveElement(
                array, offset + static_cast<std::int32_t>(index),
                call.vm.Model().GetPrimitiveElement(temporary, static_cast<JniSize>(index)));
        }
    }
    return result;
}

[[nodiscard]] dx::VmObjectRef FindWrapper(const std::unordered_map<std::uint32_t, std::uint32_t>& wrappers,
                                          const std::uint32_t handle, const dx::VmObjectRef none) {
    for (const auto& [identity, native] : wrappers) {
        if (native == handle) return dx::VmObjectRef{identity};
    }
    return none;
}

}  // namespace

namespace {

dx::IntrinsicHandler GlGetStringHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto parameter = static_cast<std::uint32_t>(call.arguments[0].AsInt());
        if (parameter != 0x1F00 && parameter != 0x1F01 && parameter != 0x1F02 && parameter != 0x1F03 &&
            parameter != 0x8B8C) {
            Record(call, "dexvm.gl10.glGetString." + std::to_string(parameter));
            throw std::runtime_error("GL10.glGetString parameter is unsupported");
        }
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        return MakeString(call, context->session->ManagedGlString(parameter));
    };
}

[[nodiscard]] std::vector<std::string> GlParameterTypes(const std::string_view descriptor) {
    std::vector<std::string> result;
    for (std::size_t cursor = 1; cursor < descriptor.find(')');) {
        const auto start = cursor;
        while (descriptor[cursor] == '[')
            ++cursor;
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
    return name.starts_with("glGet") || name.starts_with("glGen") || name.starts_with("glRead") ||
           name.starts_with("glQuery");
}

[[nodiscard]] std::uint32_t ArrayElementSize(const char descriptor) {
    switch (descriptor) {
    case 'Z':
    case 'B': return 1U;
    case 'C':
    case 'S': return 2U;
    case 'I':
    case 'F': return 4U;
    case 'J':
    case 'D': return 8U;
    default: throw std::invalid_argument("unsupported GLES primitive array");
    }
}

[[nodiscard]] dx::VmValue ManagedGlResult(const std::string_view descriptor, const std::uint32_t result) {
    const auto returns = descriptor.substr(descriptor.find(')') + 1U);
    if (returns == "V") return dx::VmValue::Void();
    if (returns == "I" || returns == "Z") {
        return dx::VmValue::Int(static_cast<std::int32_t>(result));
    }
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Java GLES return adapter is unavailable"};
}

[[nodiscard]] std::int32_t ManagedGlQueryInt(dx::IntrinsicContext& call, const Context& context,
                                             const gles::GlesApi api, const std::string_view name,
                                             const std::uint32_t object, const std::uint32_t parameter) {
    static_cast<void>(call);
    std::array<std::byte, 4> bytes{};
    std::vector<std::byte> output;
    static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
        bytes, true,
        [&](const memory::GuestAddress address) {
            const std::array args{object, parameter, address.Value()};
            return NativeGles(call, context, api, name, args);
        },
        &output));
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(output[index])) << (index * 8U);
    }
    return static_cast<std::int32_t>(value);
}

void SetJavaIntOutput(dx::IntrinsicContext& call, const dx::VmObjectRef output, const std::int32_t offset,
                      const std::int32_t value) {
    if (!output.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "GLES integer output is null"};
    }
    const auto identity = call.vm.Model().ToIdentity(output);
    if (call.vm.NIO().Contains(identity)) {
        const auto snapshot = call.vm.NIO().Snapshot(identity);
        if (snapshot.element != dx::NioElementKind::int_value || offset < 0 ||
            snapshot.position + offset >= snapshot.limit) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "GLES IntBuffer has no remaining element"};
        }
        call.vm.NIO().PutScalar(identity, dx::NioElementKind::int_value, (snapshot.position + offset) * 4,
                                static_cast<std::uint64_t>(value));
        return;
    }
    const auto length = call.vm.Model().ArrayLength(output);
    if (offset < 0 || offset >= length) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "GLES integer output offset is outside the array"};
    }
    call.vm.Model().SetPrimitiveElement(output, offset, static_cast<std::uint32_t>(value));
}

[[nodiscard]] std::string ManagedGlObjectString(dx::IntrinsicContext& call, const Context& context,
                                                const gles::GlesApi api, const std::string_view get_name,
                                                const std::string_view query_name, const std::uint32_t object,
                                                const std::uint32_t length_parameter) {
    const auto maximum = std::max(1, ManagedGlQueryInt(call, context, api, query_name, object, length_parameter));
    std::vector<std::byte> bytes(static_cast<std::size_t>(maximum));
    std::vector<std::byte> output;
    static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
        bytes, true,
        [&](const memory::GuestAddress address) {
            const std::array args{object, static_cast<std::uint32_t>(maximum), 0U, address.Value()};
            return NativeGles(call, context, api, get_name, args);
        },
        &output));
    const auto terminator = std::ranges::find(output, std::byte{});
    return std::string(reinterpret_cast<const char*>(output.data()),
                       static_cast<std::size_t>(terminator - output.begin()));
}

dx::IntrinsicHandler JavaGlesHandler(const Context& context, const gles::GlesApi api, std::string name,
                                     std::string descriptor) {
    return [context, api, name = std::move(name), descriptor = std::move(descriptor)](dx::IntrinsicContext& call) {
        if (name == "glGetString") return GlGetStringHandler(context)(call);
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        if (name == "glGetStringi" && descriptor == "(II)Ljava/lang/String;") {
            const std::array args{call.arguments[0].cat1, call.arguments[1].cat1};
            if (NativeGles(call, context, api, name, args) == 0U) return dx::VmValue::Ref(dx::VmObjectRef{});
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
            return dx::VmValue::Long(NativeGles(call, context, api, name, args));
        }
        if (name == "glMapBufferRange" && descriptor == "(IIII)Ljava/nio/Buffer;") {
            const auto length = call.arguments[2].AsInt();
            const std::array args{call.arguments[0].cat1, call.arguments[1].cat1, call.arguments[2].cat1,
                                  call.arguments[3].cat1};
            const auto address = NativeGles(call, context, api, name, args);
            if (address == 0U) return dx::VmValue::Ref(dx::VmObjectRef{});
            const auto buffer = call.vm.NewIntrinsicInstance("Ljava/nio/DirectByteBuffer;");
            call.vm.NIO().WrapDirect(call.vm.Model().ToIdentity(buffer), memory::GuestAddress{address}, length);
            return dx::VmValue::Ref(buffer);
        }
        if (descriptor == "(I)Ljava/lang/String;" &&
            (name == "glGetProgramInfoLog" || name == "glGetShaderInfoLog" || name == "glGetShaderSource")) {
            const bool program = name == "glGetProgramInfoLog";
            const auto query = program ? "glGetProgramiv" : "glGetShaderiv";
            const auto parameter = name == "glGetShaderSource" ? 0x8B88U : 0x8B84U;
            return MakeString(
                call, ManagedGlObjectString(call, context, api, name, query, call.arguments[0].cat1, parameter));
        }
        if ((name == "glGetActiveAttrib" || name == "glGetActiveUniform" || name == "glGetTransformFeedbackVarying") &&
            descriptor.ends_with(")Ljava/lang/String;")) {
            const bool buffer_form = descriptor.find("Ljava/nio/IntBuffer;") != std::string::npos;
            const auto size_ref = call.arguments[2].ref;
            const auto type_ref = call.arguments[buffer_form ? 3U : 4U].ref;
            const auto size_offset = buffer_form ? 0 : call.arguments[3].AsInt();
            const auto type_offset = buffer_form ? 0 : call.arguments[5].AsInt();
            const auto max_parameter = name == "glGetActiveAttrib"    ? 0x8B8AU
                                       : name == "glGetActiveUniform" ? 0x8B87U
                                                                      : 0x8C76U;
            const auto maximum = std::max(
                1, ManagedGlQueryInt(call, context, api, "glGetProgramiv", call.arguments[0].cat1, max_parameter));
            std::array<std::byte, 4> zeros{};
            std::vector<std::byte> size_bytes, type_bytes, name_bytes;
            std::vector<std::byte> name_input(static_cast<std::size_t>(maximum));
            static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                zeros, true,
                [&](const memory::GuestAddress size_address) {
                    return context->session->NIO().WithTemporaryGuestMemory(
                        zeros, true,
                        [&](const memory::GuestAddress type_address) {
                            return context->session->NIO().WithTemporaryGuestMemory(
                                name_input, true,
                                [&](const memory::GuestAddress name_address) {
                                    const std::array args{call.arguments[0].cat1,
                                                          call.arguments[1].cat1,
                                                          static_cast<std::uint32_t>(maximum),
                                                          0U,
                                                          size_address.Value(),
                                                          type_address.Value(),
                                                          name_address.Value()};
                                    return NativeGles(call, context, api, name, args);
                                },
                                &name_bytes);
                        },
                        &type_bytes);
                },
                &size_bytes));
            const auto decode = [](const std::vector<std::byte>& bytes) {
                std::uint32_t value{};
                for (std::size_t i = 0; i < 4U; ++i)
                    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8U);
                return static_cast<std::int32_t>(value);
            };
            SetJavaIntOutput(call, size_ref, size_offset, decode(size_bytes));
            SetJavaIntOutput(call, type_ref, type_offset, decode(type_bytes));
            const auto end = std::ranges::find(name_bytes, std::byte{});
            return MakeString(call, std::string(reinterpret_cast<const char*>(name_bytes.data()),
                                                static_cast<std::size_t>(end - name_bytes.begin())));
        }
        if (name == "glGetActiveUniformBlockName" && descriptor == "(II)Ljava/lang/String;") {
            std::array<std::byte, 4> length_input{};
            std::vector<std::byte> length_output;
            static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                length_input, true,
                [&](const memory::GuestAddress address) {
                    const std::array args{call.arguments[0].cat1, call.arguments[1].cat1, 0x8A41U, address.Value()};
                    return NativeGles(call, context, api, "glGetActiveUniformBlockiv", args);
                },
                &length_output));
            std::uint32_t maximum{};
            for (std::size_t byte = 0; byte < 4U; ++byte)
                maximum |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(length_output[byte]))
                           << (byte * 8U);
            maximum = std::max(1U, maximum);
            std::vector<std::byte> name_input(maximum), name_output;
            static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                name_input, true,
                [&](const memory::GuestAddress address) {
                    const std::array args{call.arguments[0].cat1, call.arguments[1].cat1, maximum, 0U, address.Value()};
                    return NativeGles(call, context, api, name, args);
                },
                &name_output));
            const auto end = std::ranges::find(name_output, std::byte{});
            return MakeString(call, std::string(reinterpret_cast<const char*>(name_output.data()),
                                                static_cast<std::size_t>(end - name_output.begin())));
        }
        if (name == "glGetUniformIndices" && descriptor.starts_with("(I[Ljava/lang/String;")) {
            const auto names = call.arguments[1].ref;
            if (!names.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "uniform names are null"};
            const auto count = call.vm.Model().ArrayLength(names);
            std::vector<std::uint32_t> pointers(static_cast<std::size_t>(count));
            std::vector<std::int32_t> indices(static_cast<std::size_t>(count));
            std::function<void(std::int32_t)> marshal_names;
            marshal_names = [&](const std::int32_t index) {
                if (index < count) {
                    const auto string = call.vm.Model().GetObjectElement(names, index);
                    if (!string.IsValid())
                        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "uniform name is null"};
                    const auto text = call.vm.StringUtf8(string);
                    std::vector<std::byte> bytes(text.size() + 1U);
                    std::ranges::transform(text, bytes.begin(), [](const char c) { return static_cast<std::byte>(c); });
                    static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                        bytes, false, [&](const memory::GuestAddress address) {
                            pointers[static_cast<std::size_t>(index)] = address.Value();
                            marshal_names(index + 1);
                            return 0U;
                        }));
                    return;
                }
                std::vector<std::byte> pointer_bytes(pointers.size() * 4U);
                for (std::size_t i = 0; i < pointers.size(); ++i)
                    for (std::size_t byte = 0; byte < 4U; ++byte)
                        pointer_bytes[i * 4U + byte] = static_cast<std::byte>(pointers[i] >> (byte * 8U));
                std::vector<std::byte> output_bytes(indices.size() * 4U);
                std::vector<std::byte> copied;
                static_cast<void>(context->session->NIO().WithTemporaryGuestMemory(
                    pointer_bytes, false, [&](const memory::GuestAddress pointer_address) {
                        return context->session->NIO().WithTemporaryGuestMemory(
                            output_bytes, true,
                            [&](const memory::GuestAddress output_address) {
                                const std::array args{call.arguments[0].cat1, static_cast<std::uint32_t>(count),
                                                      pointer_address.Value(), output_address.Value()};
                                return NativeGles(call, context, api, name, args);
                            },
                            &copied);
                    }));
                for (std::int32_t i = 0; i < count; ++i) {
                    std::uint32_t value{};
                    for (std::size_t byte = 0; byte < 4U; ++byte)
                        value |= static_cast<std::uint32_t>(
                                     std::to_integer<std::uint8_t>(copied[static_cast<std::size_t>(i) * 4U + byte]))
                                 << (byte * 8U);
                    SetJavaIntOutput(call, call.arguments[2].ref,
                                     descriptor.ends_with("[II)V") ? call.arguments[3].AsInt() + i : i,
                                     static_cast<std::int32_t>(value));
                }
            };
            marshal_names(0);
            return dx::VmValue::Void();
        }
        if (name == "glTransformFeedbackVaryings" && descriptor == "(I[Ljava/lang/String;I)V") {
            const auto array = call.arguments[1].ref;
            if (!array.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "transform feedback varyings are null"};
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
                            const std::array args{call.arguments[0].cat1, static_cast<std::uint32_t>(count),
                                                  address.Value(), call.arguments[2].cat1};
                            return NativeGles(call, context, api, name, args);
                        });
                }
                const auto string = call.vm.Model().GetObjectElement(array, index);
                if (!string.IsValid())
                    throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "transform feedback varying is null"};
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
            for (const auto character : text)
                bytes.push_back(static_cast<std::byte>(character));
            bytes.push_back(std::byte{});
            static_cast<void>(
                context->session->NIO().WithTemporaryGuestMemory(bytes, false, [&](const memory::GuestAddress source) {
                    const std::array pointer_bytes{
                        static_cast<std::byte>(source.Value()), static_cast<std::byte>(source.Value() >> 8U),
                        static_cast<std::byte>(source.Value() >> 16U), static_cast<std::byte>(source.Value() >> 24U)};
                    return context->session->NIO().WithTemporaryGuestMemory(
                        pointer_bytes, false, [&](const memory::GuestAddress pointers) {
                            const std::array args{call.arguments[0].cat1, 1U, pointers.Value(), 0U};
                            return NativeGles(call, context, api, name, args);
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
                return NativeGles(call, context, api, name, arguments);
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
                return context->session->NIO().WithTemporaryGuestMemory(bytes, false,
                                                                        [&](const memory::GuestAddress address) {
                                                                            arguments.push_back(address.Value());
                                                                            const auto result = marshal(index + 1U);
                                                                            arguments.pop_back();
                                                                            return result;
                                                                        });
            }
            const auto identity = call.vm.Model().ToIdentity(value.ref);
            if (context->session->NIO().Contains(identity)) {
                return context->session->NIO().WithBufferGuestMemory(identity, copy_back,
                                                                     [&](const memory::GuestAddress address) {
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
                std::vector<std::byte> bytes(static_cast<std::size_t>(length - offset) * size);
                for (std::int32_t element = offset; element < length; ++element) {
                    const auto bits = call.vm.Model().GetPrimitiveElement(value.ref, element);
                    for (std::uint32_t byte = 0; byte < size; ++byte) {
                        bytes[static_cast<std::size_t>(element - offset) * size + byte] =
                            static_cast<std::byte>(bits >> (byte * 8U));
                    }
                }
                std::vector<std::byte> output;
                const auto result = context->session->NIO().WithTemporaryGuestMemory(
                    bytes, copy_back,
                    [&](const memory::GuestAddress address) {
                        arguments.push_back(address.Value());
                        const auto nested = marshal(next);
                        arguments.pop_back();
                        return nested;
                    },
                    &output);
                if (copy_back) {
                    for (std::int32_t element = offset; element < length; ++element) {
                        std::uint64_t bits{};
                        for (std::uint32_t byte = 0; byte < size; ++byte) {
                            bits |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(
                                        output[static_cast<std::size_t>(element - offset) * size + byte]))
                                    << (byte * 8U);
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

[[nodiscard]] DexVmAndroidContext::BitmapState& GlBitmap(const Context& context, const dx::VmObjectRef bitmap) {
    if (!bitmap.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "GLUtils bitmap is null"};
    }
    const auto found = context->bitmaps.find(bitmap.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "GLUtils bitmap is invalid or recycled"};
    }
    return found->second;
}

dx::IntrinsicHandler GlUtilsTextureHandler(const Context& context, const bool sub_image, std::string descriptor) {
    return [context, sub_image, descriptor = std::move(descriptor)](dx::IntrinsicContext& call) {
        if (context->session == nullptr) ModelFailure(call, "guest session is absent");
        const std::size_t bitmap_index = sub_image ? 4U : (descriptor == "(IILandroid/graphics/Bitmap;I)V" ? 2U : 3U);
        const auto& bitmap = GlBitmap(context, call.arguments[bitmap_index].ref);
        constexpr std::uint32_t kRgba = 0x1908U;
        constexpr std::uint32_t kUnsignedByte = 0x1401U;
        constexpr std::uint32_t kAlpha = 0x1906U;
        constexpr std::uint32_t kRgb = 0x1907U;
        constexpr std::uint32_t kUnsignedShort565 = 0x8363U;
        constexpr std::uint32_t kUnsignedShort4444 = 0x8033U;
        constexpr std::uint32_t kUnsignedShort5551 = 0x8034U;
        constexpr std::uint32_t kLuminanceAlpha = 0x190AU;
        auto format = bitmap.config == 1 ? kAlpha : bitmap.config == 3 ? kRgb : kRgba;
        auto type = bitmap.config == 3 ? kUnsignedShort565 : bitmap.config == 4 ? kUnsignedShort4444 : kUnsignedByte;
        if (!sub_image && bitmap_index == 3U) {
            format = call.arguments[2].cat1;
            if (descriptor == "(IIILandroid/graphics/Bitmap;II)V") {
                type = call.arguments[4].cat1;
            }
        } else if (sub_image && descriptor.ends_with("Bitmap;II)V")) {
            format = call.arguments[5].cat1;
            type = call.arguments[6].cat1;
        }
        const bool compatible = ((bitmap.config == 1 || bitmap.config == 5) && type == kUnsignedByte) ||
                                ((bitmap.config == 3 || bitmap.config == 4) &&
                                 (type == kUnsignedShort4444 || type == kUnsignedShort565 ||
                                  type == kUnsignedShort5551 || (type == kUnsignedByte && format == kLuminanceAlpha)));
        if (!compatible) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "GLUtils format/type is incompatible with Bitmap.Config"};
        }
        const auto pixels = bitmap.pixels->RawPixels();
        static_cast<void>(
            context->session->NIO().WithTemporaryGuestMemory(pixels, false, [&](const memory::GuestAddress address) {
                if (sub_image) {
                    const std::array args{call.arguments[0].cat1,
                                          call.arguments[1].cat1,
                                          call.arguments[2].cat1,
                                          call.arguments[3].cat1,
                                          static_cast<std::uint32_t>(bitmap.width),
                                          static_cast<std::uint32_t>(bitmap.height),
                                          format,
                                          type,
                                          address.Value()};
                    return NativeGles(call, context, gles::GlesApi::gles2, "glTexSubImage2D", args);
                }
                const auto border = call.arguments.back().cat1;
                const std::array args{call.arguments[0].cat1,
                                      call.arguments[1].cat1,
                                      format,
                                      static_cast<std::uint32_t>(bitmap.width),
                                      static_cast<std::uint32_t>(bitmap.height),
                                      border,
                                      format,
                                      type,
                                      address.Value()};
                return NativeGles(call, context, gles::GlesApi::gles2, "glTexImage2D", args);
            }));
        return dx::VmValue::Void();
    };
}

Decl DeclareJavaGlesClass(const Context& context, const char* class_descriptor, const char* superclass,
                          const gles::GlesApi api, const std::span<const generated_java_gles::MethodSpec> methods,
                          const std::span<const generated_java_gles::ConstantSpec> constants) {
    auto builder = dx::IntrinsicClassBuilder::Class(class_descriptor, superclass);
    builder.Constructor("()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    for (const auto& constant : constants) {
        builder.ConstantInt(constant.name, "I", constant.value);
    }
    for (const auto& method : methods) {
        builder.StaticMethod(method.name, method.descriptor,
                             JavaGlesHandler(context, api, method.name, method.descriptor));
    }
    return std::move(builder).Build();
}

[[nodiscard]] dx::VmObjectRef EnsureEgl14Object(dx::IntrinsicContext& call, dx::VmObjectRef& object,
                                                const char* descriptor) {
    if (!object.IsValid()) object = call.vm.NewIntrinsicInstance(descriptor);
    return object;
}

[[nodiscard]] dx::VmObjectRef EnsureEgl14Config(dx::IntrinsicContext& call, DexVmAndroidContext::EglFacadeState& egl,
                                                const std::uint32_t handle) {
    const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLConfig;");
    egl.egl14_configs.emplace(wrapper.Value(), handle);
    return wrapper;
}

dx::IntrinsicHandler Egl14SimpleHandler(const Context& context, std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (context->session == nullptr) {
            ModelFailure(call, "EGL14 requires the native EGL registry");
        }
        if (name == "eglGetError") {
            return dx::VmValue::Int(static_cast<std::int32_t>(NativeEgl(call, context, name, {})));
        }
        if (name == "eglGetDisplay") {
            if (call.arguments[0].AsInt() != 0) {
                LatchEglError(call, context, kBadParameter);
                return dx::VmValue::Ref(egl.egl14_no_display);
            }
            egl.native_display = NativeEgl(call, context, name, std::array{0U});
            return dx::VmValue::Ref(egl.native_display == 0U
                                        ? egl.egl14_no_display
                                        : EnsureEgl14Object(call, egl.egl14_display, "Landroid/opengl/EGLDisplay;"));
        }
        if ((name == "eglInitialize" || name == "eglTerminate" || name == "eglQueryString" ||
             name == "eglSwapInterval") &&
            !ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return name == "eglQueryString" ? dx::VmValue::Ref(dx::VmObjectRef{}) : Bool(false);
        }
        if (name == "eglInitialize") {
            if (egl.native_display == 0U) {
                egl.native_display = NativeEgl(call, context, "eglGetDisplay", std::array{0U});
                if (egl.native_display == 0U) return Bool(false);
            }
            const auto major = WithIntArrayOffset(
                call, context, call.arguments[1].ref, call.arguments[2].AsInt(), true,
                [&](const std::uint32_t major_address) {
                    return WithIntArrayOffset(call, context, call.arguments[3].ref, call.arguments[4].AsInt(), true,
                                              [&](const std::uint32_t minor_address) {
                                                  return NativeEgl(
                                                      call, context, name,
                                                      std::array{egl.native_display, major_address, minor_address});
                                              });
                });
            return Bool(major != 0U);
        }
        if (name == "eglTerminate") {
            const auto result = NativeEgl(call, context, name, std::array{egl.native_display});
            if (result != 0U) {
                egl.egl14_contexts.clear();
                egl.egl14_surfaces.clear();
                egl.native_display = 0U;
                egl.egl14_configs.clear();
            }
            return Bool(result != 0U);
        }
        if (name == "eglQueryString") {
            if (NativeEgl(call, context, name, std::array{egl.native_display, call.arguments[1].cat1}) == 0U) {
                return dx::VmValue::Ref(dx::VmObjectRef{});
            }
            switch (call.arguments[1].AsInt()) {
            case 0x3053: return MakeString(call, "OGPlay");
            case 0x3054: return MakeString(call, "1.4 OGPlay");
            case 0x3055: return MakeString(call, context->session->ManagedEglString(0x3055U));
            case 0x308D: return MakeString(call, "OpenGL_ES");
            default: return dx::VmValue::Ref(dx::VmObjectRef{});
            }
        }
        if (name == "eglBindAPI") {
            return Bool(NativeEgl(call, context, name, std::array{call.arguments[0].cat1}) != 0U);
        }
        if (name == "eglQueryAPI") {
            return dx::VmValue::Int(static_cast<std::int32_t>(NativeEgl(call, context, name, {})));
        }
        if (name == "eglWaitClient" || name == "eglWaitGL" || name == "eglReleaseThread") {
            return Bool(NativeEgl(call, context, name, {}) != 0U);
        }
        if (name == "eglWaitNative") {
            return Bool(NativeEgl(call, context, name, std::array{call.arguments[0].cat1}) != 0U);
        }
        if (name == "eglSwapInterval") {
            return Bool(NativeEgl(call, context, name, std::array{egl.native_display, call.arguments[1].cat1}) != 0U);
        }
        if (name == "eglGetCurrentDisplay") {
            return dx::VmValue::Ref(NativeEgl(call, context, name, {}) == egl.native_display ? egl.egl14_display
                                                                                             : egl.egl14_no_display);
        }
        if (name == "eglGetCurrentContext") {
            return dx::VmValue::Ref(
                FindWrapper(egl.egl14_contexts, NativeEgl(call, context, name, {}), egl.egl14_no_context));
        }
        if (name == "eglGetCurrentSurface") {
            return dx::VmValue::Ref(FindWrapper(egl.egl14_surfaces,
                                                NativeEgl(call, context, name, std::array{call.arguments[0].cat1}),
                                                egl.egl14_no_surface));
        }
        throw std::logic_error("unsupported EGL14 simple handler: " + name);
    };
}

dx::IntrinsicHandler Egl14CreateContextHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return dx::VmValue::Ref(egl.egl14_no_context);
        }
        if (ConfigHandle(egl.egl14_configs, call.arguments[1].ref) == 0U) {
            LatchEglError(call, context, kBadConfig);
            return dx::VmValue::Ref(egl.egl14_no_context);
        }
        const auto share = call.arguments[2].ref;
        const auto found = egl.egl14_contexts.find(share.Value());
        if (share != egl.egl14_no_context && found == egl.egl14_contexts.end()) {
            LatchEglError(call, context, kBadContext);
            return dx::VmValue::Ref(egl.egl14_no_context);
        }
        const auto share_handle = share == egl.egl14_no_context       ? 0U
                                  : found == egl.egl14_contexts.end() ? 0U
                                                                      : found->second;
        return WithIntArrayOffset(call, context, call.arguments[3].ref, call.arguments[4].AsInt(), false,
                                  [&](const std::uint32_t attributes) {
                                      const auto handle =
                                          NativeEgl(call, context, "eglCreateContext",
                                                    std::array{egl.native_display,
                                                               ConfigHandle(egl.egl14_configs, call.arguments[1].ref),
                                                               share_handle, attributes});
                                      if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_context);
                                      const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLContext;");
                                      egl.egl14_contexts.emplace(wrapper.Value(), handle);
                                      return dx::VmValue::Ref(wrapper);
                                  });
    };
}

dx::IntrinsicHandler Egl14CreatePbufferHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        if (ConfigHandle(egl.egl14_configs, call.arguments[1].ref) == 0U) {
            LatchEglError(call, context, kBadConfig);
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        return WithIntArrayOffset(
            call, context, call.arguments[2].ref, call.arguments[3].AsInt(), false,
            [&](const std::uint32_t attributes) {
                const auto handle = NativeEgl(
                    call, context, "eglCreatePbufferSurface",
                    std::array{egl.native_display, ConfigHandle(egl.egl14_configs, call.arguments[1].ref), attributes});
                if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_surface);
                const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
                egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                return dx::VmValue::Ref(wrapper);
            });
    };
}

dx::IntrinsicHandler Egl14CreateWindowHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        if (ConfigHandle(egl.egl14_configs, call.arguments[1].ref) == 0U) {
            LatchEglError(call, context, kBadConfig);
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        auto window = call.arguments[2].ref;
        const auto unwrap = [&](std::string_view type) {
            return window.IsValid() && call.vm.Linker().IsAssignable(call.vm.Linker().ResolveDescriptor(type),
                                                                     call.vm.Model().ObjectClass(window));
        };
        if (unwrap("Landroid/view/SurfaceView;"))
            window = CallAndroidMethod(call.vm, window, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto root = call.vm.ProtectReferences(std::array{window});
        if (unwrap("Landroid/view/SurfaceHolder;"))
            window = CallAndroidMethod(call.vm, window, "getSurface", "()Landroid/view/Surface;").ref;
        const auto surface =
            unwrap("Landroid/view/Surface;") ? ResolveRomSurfaceWindow(call.vm, context, window) : std::nullopt;
        if (!surface) {
            LatchEglError(call, context, kBadNativeWindow);
            return dx::VmValue::Ref(egl.egl14_no_surface);
        }
        context->session->Process().UpdateSurfaceWindow(*surface);
        return WithIntArrayOffset(call, context, call.arguments[3].ref, call.arguments[4].AsInt(), false,
                                  [&](const std::uint32_t attributes) {
                                      const auto handle =
                                          NativeEgl(call, context, "eglCreateWindowSurface",
                                                    std::array{egl.native_display,
                                                               ConfigHandle(egl.egl14_configs, call.arguments[1].ref),
                                                               surface->identity.Value(), attributes});
                                      if (handle == 0U) return dx::VmValue::Ref(egl.egl14_no_surface);
                                      const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
                                      egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                                      return dx::VmValue::Ref(wrapper);
                                  });
    };
}

dx::IntrinsicHandler Egl14ObjectHandler(const Context& context, std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return Bool(false);
        }
        if (name == "eglDestroyContext") {
            const auto found = egl.egl14_contexts.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_contexts.end()) {
                LatchEglError(call, context, kBadContext);
                return Bool(false);
            }
            const auto result = NativeEgl(call, context, name, std::array{egl.native_display, found->second});
            if (result != 0U) egl.egl14_contexts.erase(found);
            return Bool(result != 0U);
        }
        if (name == "eglDestroySurface" || name == "eglSwapBuffers") {
            const auto found = egl.egl14_surfaces.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_surfaces.end()) {
                LatchEglError(call, context, kBadSurface);
                return Bool(false);
            }
            const auto result = NativeEgl(call, context, name, std::array{egl.native_display, found->second});
            if (result != 0U && name == "eglDestroySurface") {
                egl.egl14_surfaces.erase(found);
            }
            return Bool(result != 0U);
        }
        if (name == "eglMakeCurrent") {
            const auto handle = [](const auto& map, const dx::VmObjectRef ref, const dx::VmObjectRef none) {
                if (ref == none) return 0U;
                const auto found = map.find(ref.Value());
                return found == map.end() ? 0U : found->second;
            };
            const auto draw = handle(egl.egl14_surfaces, call.arguments[1].ref, egl.egl14_no_surface);
            const auto read = handle(egl.egl14_surfaces, call.arguments[2].ref, egl.egl14_no_surface);
            const auto current = handle(egl.egl14_contexts, call.arguments[3].ref, egl.egl14_no_context);
            if (call.arguments[1].ref != egl.egl14_no_surface && draw == 0U) {
                LatchEglError(call, context, kBadSurface);
                return Bool(false);
            }
            if (call.arguments[2].ref != egl.egl14_no_surface && read == 0U) {
                LatchEglError(call, context, kBadSurface);
                return Bool(false);
            }
            if (call.arguments[3].ref != egl.egl14_no_context && current == 0U) {
                LatchEglError(call, context, kBadContext);
                return Bool(false);
            }
            return Bool(NativeEgl(call, context, name, std::array{egl.native_display, draw, read, current}) != 0U);
        }
        throw std::logic_error("unsupported EGL14 object handler: " + name);
    };
}

dx::IntrinsicHandler Egl14QueryValueHandler(const Context& context, std::string name) {
    return [context, name = std::move(name)](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return Bool(false);
        }
        std::uint32_t handle{};
        if (name == "eglGetConfigAttrib") {
            handle = ConfigHandle(egl.egl14_configs, call.arguments[1].ref);
            if (handle == 0U) {
                LatchEglError(call, context, kBadConfig);
                return Bool(false);
            }
        } else if (name == "eglQueryContext") {
            const auto found = egl.egl14_contexts.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_contexts.end()) {
                LatchEglError(call, context, kBadContext);
                return Bool(false);
            }
            handle = found->second;
        } else if (name == "eglQuerySurface") {
            const auto found = egl.egl14_surfaces.find(call.arguments[1].ref.Value());
            if (found == egl.egl14_surfaces.end()) {
                LatchEglError(call, context, kBadSurface);
                return Bool(false);
            }
            handle = found->second;
        }
        return WithIntArrayOffset(
            call, context, call.arguments[3].ref, call.arguments[4].AsInt(), true, [&](const std::uint32_t output) {
                return Bool(NativeEgl(call, context, name,
                                      std::array{egl.native_display, handle, call.arguments[2].cat1, output}) != 0U);
            });
    };
}

dx::IntrinsicHandler Egl14ConfigsHandler(const Context& context, const bool choose) {
    return [context, choose](dx::IntrinsicContext& call) {
        auto& egl = context->egl;
        if (!ValidateEgl14Display(call, context, call.arguments[0].ref)) {
            return Bool(false);
        }
        const auto configs_index = choose ? 3U : 1U;
        const auto configs_offset_index = choose ? 4U : 2U;
        const auto size_index = choose ? 5U : 3U;
        const auto count_index = choose ? 6U : 4U;
        const auto count_offset_index = choose ? 7U : 5U;
        const auto configs = call.arguments[configs_index].ref;
        const auto configs_offset = call.arguments[configs_offset_index].AsInt();
        const auto size = call.arguments[size_index].AsInt();
        if (size < 0 || configs_offset < 0 ||
            (configs.IsValid() && configs_offset > call.vm.Model().ArrayLength(configs))) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "EGLConfig array range is invalid"};
        }
        const auto invoke = [&](const std::uint32_t attributes) {
            return WithIntArrayOffset(
                call, context, call.arguments[count_index].ref, call.arguments[count_offset_index].AsInt(), true,
                [&](const std::uint32_t count_address) {
                    const auto available =
                        configs.IsValid() ? call.vm.Model().ArrayLength(configs) - configs_offset : 0;
                    const auto capacity = configs.IsValid() && size > 0 ? std::min<std::int32_t>(size, available) : 0;
                    std::vector<std::byte> config_bytes(static_cast<std::size_t>(capacity) * 4U);
                    std::vector<std::byte> output;
                    const auto result = context->session->NIO().WithTemporaryGuestMemory(
                        config_bytes, true,
                        [&](const memory::GuestAddress config_address) {
                            if (choose) {
                                return NativeEgl(call, context, "eglChooseConfig",
                                                 std::array{egl.native_display, attributes,
                                                            capacity > 0 ? config_address.Value() : 0U,
                                                            static_cast<std::uint32_t>(capacity), count_address});
                            }
                            return NativeEgl(call, context, "eglGetConfigs",
                                             std::array{egl.native_display, capacity > 0 ? config_address.Value() : 0U,
                                                        static_cast<std::uint32_t>(capacity), count_address});
                        },
                        &output);
                    if (result != 0U && capacity > 0) {
                        for (std::int32_t index = 0; index < capacity; ++index) {
                            std::uint32_t handle{};
                            for (std::size_t byte = 0; byte < 4U; ++byte)
                                handle |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(
                                              output[static_cast<std::size_t>(index) * 4U + byte]))
                                          << (byte * 8U);
                            if (handle != 0U) {
                                const auto wrapper = EnsureEgl14Config(call, egl, handle);
                                call.vm.Model().SetObjectElement(configs, configs_offset + index, wrapper);
                            }
                        }
                    }
                    return Bool(result != 0U);
                });
        };
        if (!choose) return invoke(0U);
        return WithIntArrayOffset(call, context, call.arguments[1].ref, call.arguments[2].AsInt(), false, invoke);
    };
}

}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {
namespace {

bool EglProducerSettled(DexVmAndroidContext& context, const DexVmAndroidContext::EglFacadeState::Producer& producer,
                        const std::optional<std::vector<dexvm::VmThreadSnapshot>>& threads) {
    if (producer.waiting || producer.blocked) return true;
    if (threads && std::ranges::any_of(*threads, [&](const auto& thread) {
            return thread.context_token == producer.context_token &&
                   (thread.status != dexvm::VmThreadStatus::running ||
                    thread.wait_state != dexvm::VmThreadWaitState::none);
        }))
        return true;
    return context.session && producer.native_thread &&
           context.session->Process().IsNativeThreadWaiting(producer.native_thread);
}

}  // namespace

void AttachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock) {
    const auto driver = context.clock_driver;
    {
        std::scoped_lock lock(context.egl.pace_mutex);
        context.egl.pace_driver = driver;
        context.egl.pace_driver_blocked = false;
        context.egl.pace_shutdown = false;
        context.egl.pace_generation = 0;
    }
    AttachAndroidClockObserver(context, execution_lock);
}

void AttachAndroidClockObserver(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock) {
    execution_lock.SetBlockingObserver(
        &context, [](void* opaque, const std::thread::id thread, const bool blocked) noexcept {
            auto& context = *static_cast<DexVmAndroidContext*>(opaque);
            if (thread == context.clock_driver && context.clock_monitors)
                context.clock_monitors->ObserveClockDriverBlocking(blocked);
            auto& egl = context.egl;
            // The swap worker can release the execution lock under pace_mutex.
            {
                std::scoped_lock lock(egl.pace_mutex);
                if (egl.pace_driver == thread) egl.pace_driver_blocked = blocked;
                if (const auto it = egl.producers.find(thread); it != egl.producers.end()) {
                    it->second.blocked = blocked;
                    if (blocked) it->second.wait_timed_out = false;
                }
            }
            egl.pace_changed.notify_all();
        });
}

void DetachEglSwapPacer(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock) {
    ShutdownEglSwapPacer(context);
    AttachAndroidClockObserver(context, execution_lock);
    std::scoped_lock lock(context.egl.pace_mutex);
    context.egl.pace_driver.reset();
    context.egl.pace_driver_blocked = false;
}

bool EglProducersSettled(DexVmAndroidContext& context, bool wait) {
    auto& egl = context.egl;
    std::unique_lock lock(egl.pace_mutex);
    const auto settled = [&] {
        if (egl.pace_shutdown || egl.pace_driver_blocked || std::ranges::all_of(egl.producers, [](const auto& item) {
                return item.second.waiting || item.second.blocked;
            }))
            return true;
        // ReacquireAfterBlocking reports runnable before it competes for the VM
        // lock. The runtime wait state remains set until that acquisition ends.
        const auto threads = context.threads ? context.threads->TrySnapshot() : std::nullopt;
        bool ready = true;
        for (auto& [_, producer] : egl.producers) {
            if (EglProducerSettled(context, producer, threads))
                producer.wait_timed_out = false;
            else if (!producer.wait_timed_out)
                ready = false;
        }
        return ready;
    };
    // Native futex residency has no VM-lock transition. Poll only to recheck that
    // exact wait state and the caller's Clock deadline; no elapsed-time heuristic.
    return wait ? egl.pace_changed.wait_for(lock, std::chrono::milliseconds(1), settled) : settled();
}

void ReleaseOverdueEglProducers(DexVmAndroidContext& context, std::uint64_t waited_ns, core::Logger* logger) {
    auto& egl = context.egl;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> overdue;
    {
        std::scoped_lock lock(egl.pace_mutex);
        if (egl.pace_shutdown || egl.pace_driver_blocked) return;
        const auto threads = context.threads ? context.threads->TrySnapshot() : std::nullopt;
        for (auto& [_, producer] : egl.producers) {
            if (!producer.wait_timed_out && !EglProducerSettled(context, producer, threads)) {
                // One warning for this stall. Swap or blocking re-arms the producer.
                producer.wait_timed_out = true;
                overdue.emplace_back(producer.native_thread, producer.context_token);
            }
        }
    }
    if (logger)
        for (const auto& [thread, token] : overdue)
            logger->Write(core::LogLevel::warn, "runtime.egl.pacing", "EGL producer wait budget exceeded",
                          {.guest_thread = thread},
                          {{"producer_thread", thread}, {"context_token", token}, {"waited_ns", waited_ns}},
                          {.mode = core::RateLimitMode::none});
}

void AdvanceEglSwapPacer(DexVmAndroidContext& context) {
    {
        std::scoped_lock lock(context.egl.pace_mutex);
        ++context.egl.pace_generation;
        for (auto& [_, producer] : context.egl.producers)
            if (producer.waiting) producer.waiting = false;
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

std::optional<EglSwapPacerSnapshot> TryEglSwapPacerSnapshot(DexVmAndroidContext& context) {
    std::unique_lock lock(context.egl.pace_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    return EglSwapPacerSnapshot{context.egl.pace_driver.has_value(), context.egl.pace_driver_blocked,
                                context.egl.pace_shutdown, context.egl.surface_retired.load(std::memory_order_acquire),
                                context.egl.pace_generation};
}

void RetireGuestEglSurface(DexVmAndroidContext& context) {
    context.egl.surface_retired.store(true, std::memory_order_release);
    ShutdownEglSwapPacer(context);
}

void PaceEglSwap(DexVmAndroidContext& context, dexvm::VmExecutionLock& execution_lock, std::uint64_t native_thread,
                 std::uint32_t window_surface, std::uint64_t context_token) {
    auto& egl = context.egl;
    std::unique_lock lock(egl.pace_mutex);
    const auto thread = std::this_thread::get_id();
    if (const auto it = egl.producers.find(thread); it != egl.producers.end()) it->second.wait_timed_out = false;
    if (!egl.pace_driver.has_value() || *egl.pace_driver == thread || egl.pace_driver_blocked || egl.pace_shutdown) {
        return;
    }
    const auto observed = egl.pace_generation;
    if (native_thread) {
        auto& producer = egl.producers[thread];
        producer.native_thread = native_thread;
        producer.context_token = context_token;
        producer.window_surface = window_surface;
        producer.waiting = true;
        producer.blocked = false;
    }
    egl.pace_changed.notify_all();
    const auto depth = execution_lock.ReleaseForBlocking(false);
    try {
        egl.pace_changed.wait(
            lock, [&] { return egl.pace_generation != observed || egl.pace_driver_blocked || egl.pace_shutdown; });
    } catch (...) {
        lock.unlock();
        execution_lock.ReacquireAfterBlocking(depth, false);
        throw;
    }
    lock.unlock();
    execution_lock.ReacquireAfterBlocking(depth, false);
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_opengl_GLES10(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES10;", "Ljava/lang/Object;", gles::GlesApi::gles1,
                                generated_java_gles::kGLES10Methods, generated_java_gles::kGLES10Constants);
}

Decl Declare_android_opengl_GLES10Ext(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES10Ext;", "Ljava/lang/Object;",
                                gles::GlesApi::gles1_extensions, generated_java_gles::kGLES10ExtMethods,
                                generated_java_gles::kGLES10ExtConstants);
}

Decl Declare_android_opengl_GLES11(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES11;", "Landroid/opengl/GLES10;", gles::GlesApi::gles1,
                                generated_java_gles::kGLES11Methods, generated_java_gles::kGLES11Constants);
}

Decl Declare_android_opengl_GLES11Ext(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES11Ext;", "Ljava/lang/Object;",
                                gles::GlesApi::gles1_extensions, generated_java_gles::kGLES11ExtMethods,
                                generated_java_gles::kGLES11ExtConstants);
}

Decl Declare_android_opengl_GLES20(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES20;", "Ljava/lang/Object;", gles::GlesApi::gles2,
                                generated_java_gles::kGLES20Methods, generated_java_gles::kGLES20Constants);
}

Decl Declare_android_opengl_GLUtils(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/GLUtils;", "Ljava/lang/Object;");
    for (const auto& method : generated_java_gles::kGLUtilsMethods) {
        const std::string_view name = method.name;
        if (name == "getInternalFormat" || name == "getType") {
            builder.StaticMethod(method.name, method.descriptor, [context, name](dx::IntrinsicContext& call) {
                const auto& bitmap = GlBitmap(context, call.arguments[0].ref);
                if (name == "getInternalFormat") {
                    return dx::VmValue::Int(bitmap.config == 1 ? 0x1906 : bitmap.config == 3 ? 0x1907 : 0x1908);
                }
                return dx::VmValue::Int(bitmap.config == 3 ? 0x8363 : bitmap.config == 4 ? 0x8033 : 0x1401);
            });
        } else if (name == "texImage2D") {
            builder.StaticMethod(method.name, method.descriptor,
                                 GlUtilsTextureHandler(context, false, method.descriptor));
        } else if (name == "texSubImage2D") {
            builder.StaticMethod(method.name, method.descriptor,
                                 GlUtilsTextureHandler(context, true, method.descriptor));
        } else if (name == "getEGLErrorString") {
            builder.StaticMethod(method.name, method.descriptor, [](dx::IntrinsicContext& call) {
                static const std::unordered_map<std::int32_t, std::string> names{
                    {0x3000, "EGL_SUCCESS"},           {0x3001, "EGL_NOT_INITIALIZED"},
                    {0x3002, "EGL_BAD_ACCESS"},        {0x3003, "EGL_BAD_ALLOC"},
                    {0x3004, "EGL_BAD_ATTRIBUTE"},     {0x3005, "EGL_BAD_CONFIG"},
                    {0x3006, "EGL_BAD_CONTEXT"},       {0x3007, "EGL_BAD_CURRENT_SURFACE"},
                    {0x3008, "EGL_BAD_DISPLAY"},       {0x3009, "EGL_BAD_MATCH"},
                    {0x300A, "EGL_BAD_NATIVE_PIXMAP"}, {0x300B, "EGL_BAD_NATIVE_WINDOW"},
                    {0x300C, "EGL_BAD_PARAMETER"},     {0x300D, "EGL_BAD_SURFACE"},
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
                                 JavaGlesHandler(context, gles::GlesApi::gles2, method.name, method.descriptor));
        }
    }
    return std::move(builder).Build();
}

namespace {

dx::VmValue GluString(dx::IntrinsicContext& call, const std::int32_t error) {
    const char* text = nullptr;
    switch (error) {
    case 0: text = "no error"; break;
    case 0x0500: text = "invalid enum"; break;
    case 0x0501: text = "invalid value"; break;
    case 0x0502: text = "invalid operation"; break;
    case 0x0503: text = "stack overflow"; break;
    case 0x0504: text = "stack underflow"; break;
    case 0x0505: text = "out of memory"; break;
    default: return dx::VmValue::Ref(dx::VmObjectRef{});
    }
    return MakeString(call, text);
}

void GluCall(dx::IntrinsicContext& call, const dx::VmObjectRef receiver, const std::string_view name,
             const std::string_view descriptor, std::vector<dx::VmValue> arguments) {
    if (!receiver.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "gl == null"};
    }
    auto& linker = call.vm.Linker();
    const auto klass = call.vm.Model().ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(klass, std::string{name}, std::string{descriptor});
    if (!index.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "GL10 method is unavailable"};
    }
    arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
    const auto outcome = call.vm.Call(linker.Class(klass).vtable[*index], arguments);
    if (outcome.exception.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "GL10 method failed"};
    }
}

std::array<float, 16> GluReadMatrix(dx::IntrinsicContext& call, const dx::VmObjectRef array,
                                    const std::int32_t offset) {
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "matrix == null"};
    }
    if (offset < 0 || call.vm.Model().ArrayLength(array) - offset < 16) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "matrix offset is outside the array"};
    }
    std::array<float, 16> result{};
    for (std::int32_t index = 0; index < 16; ++index) {
        result[static_cast<std::size_t>(index)] = std::bit_cast<float>(
            static_cast<std::uint32_t>(call.vm.Model().GetPrimitiveElement(array, offset + index)));
    }
    return result;
}

std::array<std::int32_t, 4> GluReadViewport(dx::IntrinsicContext& call, const dx::VmObjectRef array,
                                            const std::int32_t offset) {
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "viewport == null"};
    }
    if (offset < 0 || call.vm.Model().ArrayLength(array) - offset < 4) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "viewport offset is outside the array"};
    }
    std::array<std::int32_t, 4> result{};
    for (std::int32_t index = 0; index < 4; ++index) {
        result[static_cast<std::size_t>(index)] =
            static_cast<std::int32_t>(call.vm.Model().GetPrimitiveElement(array, offset + index));
    }
    return result;
}

std::array<float, 16> GluMultiply(const std::array<float, 16>& left, const std::array<float, 16>& right) {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t inner = 0; inner < 4; ++inner) {
                result[column * 4U + row] += left[inner * 4U + row] * right[column * 4U + inner];
            }
        }
    }
    return result;
}

std::array<float, 4> GluMultiply(const std::array<float, 16>& matrix, const std::array<float, 4>& vector) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result[row] += matrix[column * 4U + row] * vector[column];
        }
    }
    return result;
}

std::optional<std::array<float, 16>> GluInverse(const std::array<float, 16>& matrix) {
    std::array<std::array<double, 8>, 4> rows{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            rows[row][column] = matrix[column * 4U + row];
        }
        rows[row][row + 4U] = 1.0;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1U; row < 4; ++row) {
            if (std::abs(rows[row][column]) > std::abs(rows[pivot][column])) pivot = row;
        }
        if (std::abs(rows[pivot][column]) < 1.0e-20) return std::nullopt;
        std::swap(rows[pivot], rows[column]);
        const auto divisor = rows[column][column];
        for (auto& value : rows[column])
            value /= divisor;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const auto factor = rows[row][column];
            for (std::size_t index = 0; index < 8; ++index) {
                rows[row][index] -= factor * rows[column][index];
            }
        }
    }
    std::array<float, 16> result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result[column * 4U + row] = static_cast<float>(rows[row][column + 4U]);
        }
    }
    return result;
}

void GluWriteVector(dx::IntrinsicContext& call, const dx::VmObjectRef array, const std::int32_t offset,
                    const std::span<const float> values) {
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "output == null"};
    }
    if (offset < 0 || call.vm.Model().ArrayLength(array) - offset < static_cast<std::int32_t>(values.size())) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "output offset is outside the array"};
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        call.vm.Model().SetPrimitiveElement(array, offset + static_cast<std::int32_t>(index),
                                            std::bit_cast<std::uint32_t>(values[index]));
    }
}

}  // namespace

Decl Declare_android_opengl_GLU(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/GLU;", "Ljava/lang/Object;");
    builder.StaticMethod("gluErrorString", "(I)Ljava/lang/String;",
                         [](dx::IntrinsicContext& call) { return GluString(call, call.arguments[0].AsInt()); });
    builder.StaticMethod("gluOrtho2D", "(Ljavax/microedition/khronos/opengles/GL10;FFFF)V",
                         [](dx::IntrinsicContext& call) {
                             GluCall(call, call.arguments[0].ref, "glOrthof", "(FFFFFF)V",
                                     {call.arguments[1], call.arguments[2], call.arguments[3], call.arguments[4],
                                      dx::VmValue::Float(-1.0F), dx::VmValue::Float(1.0F)});
                             return dx::VmValue::Void();
                         });
    builder.StaticMethod(
        "gluPerspective", "(Ljavax/microedition/khronos/opengles/GL10;FFFF)V", [](dx::IntrinsicContext& call) {
            const auto near_value = call.arguments[3].AsFloat();
            const auto top = near_value * std::tan(call.arguments[1].AsFloat() * std::numbers::pi_v<float> / 360.0F);
            const auto bottom = -top;
            const auto left = bottom * call.arguments[2].AsFloat();
            const auto right = top * call.arguments[2].AsFloat();
            GluCall(call, call.arguments[0].ref, "glFrustumf", "(FFFFFF)V",
                    {dx::VmValue::Float(left), dx::VmValue::Float(right), dx::VmValue::Float(bottom),
                     dx::VmValue::Float(top), call.arguments[3], call.arguments[4]});
            return dx::VmValue::Void();
        });
    builder.StaticMethod(
        "gluLookAt", "(Ljavax/microedition/khronos/opengles/GL10;FFFFFFFFF)V", [](dx::IntrinsicContext& call) {
            const std::array eye{call.arguments[1].AsFloat(), call.arguments[2].AsFloat(), call.arguments[3].AsFloat()};
            std::array forward{call.arguments[4].AsFloat() - eye[0], call.arguments[5].AsFloat() - eye[1],
                               call.arguments[6].AsFloat() - eye[2]};
            const std::array up{call.arguments[7].AsFloat(), call.arguments[8].AsFloat(), call.arguments[9].AsFloat()};
            const auto normalize = [](std::array<float, 3>& value) {
                const auto length = std::hypot(value[0], value[1], value[2]);
                if (length == 0.0F) return false;
                for (auto& component : value)
                    component /= length;
                return true;
            };
            if (!normalize(forward)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "eye and center coincide"};
            }
            std::array side{forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2],
                            forward[0] * up[1] - forward[1] * up[0]};
            if (!normalize(side)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "up vector is parallel to view"};
            }
            const std::array actual_up{side[1] * forward[2] - side[2] * forward[1],
                                       side[2] * forward[0] - side[0] * forward[2],
                                       side[0] * forward[1] - side[1] * forward[0]};
            std::array<float, 16> rotation{
                side[0], actual_up[0], -forward[0], 0.0F, side[1], actual_up[1], -forward[1], 0.0F,
                side[2], actual_up[2], -forward[2], 0.0F, 0.0F,    0.0F,         0.0F,        1.0F};
            std::array<float, 16> translation{1.0F, 0.0F, 0.0F, 0.0F, 0.0F,    1.0F,    0.0F,    0.0F,
                                              0.0F, 0.0F, 1.0F, 0.0F, -eye[0], -eye[1], -eye[2], 1.0F};
            const auto matrix = GluMultiply(rotation, translation);
            const auto array = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[F"),
                                                                 JniPrimitiveKind::float_value, 16);
            for (std::int32_t index = 0; index < 16; ++index) {
                call.vm.Model().SetPrimitiveElement(
                    array, index, std::bit_cast<std::uint32_t>(matrix[static_cast<std::size_t>(index)]));
            }
            GluCall(call, call.arguments[0].ref, "glMultMatrixf", "([FI)V",
                    {dx::VmValue::Ref(array), dx::VmValue::Int(0)});
            return dx::VmValue::Void();
        });
    builder.StaticMethod("gluProject", "(FFF[FI[FI[II[FI)I", [](dx::IntrinsicContext& call) {
        const auto model = GluReadMatrix(call, call.arguments[3].ref, call.arguments[4].AsInt());
        const auto projection = GluReadMatrix(call, call.arguments[5].ref, call.arguments[6].AsInt());
        const auto viewport = GluReadViewport(call, call.arguments[7].ref, call.arguments[8].AsInt());
        const auto clip = GluMultiply(
            GluMultiply(projection, model),
            std::array{call.arguments[0].AsFloat(), call.arguments[1].AsFloat(), call.arguments[2].AsFloat(), 1.0F});
        if (clip[3] == 0.0F) return dx::VmValue::Int(0);
        const auto inverse_w = 1.0F / clip[3];
        const std::array output{viewport[0] + viewport[2] * (clip[0] * inverse_w + 1.0F) * 0.5F,
                                viewport[1] + viewport[3] * (clip[1] * inverse_w + 1.0F) * 0.5F,
                                (clip[2] * inverse_w + 1.0F) * 0.5F};
        GluWriteVector(call, call.arguments[9].ref, call.arguments[10].AsInt(), output);
        return dx::VmValue::Int(1);
    });
    builder.StaticMethod("gluUnProject", "(FFF[FI[FI[II[FI)I", [](dx::IntrinsicContext& call) {
        const auto model = GluReadMatrix(call, call.arguments[3].ref, call.arguments[4].AsInt());
        const auto projection = GluReadMatrix(call, call.arguments[5].ref, call.arguments[6].AsInt());
        const auto viewport = GluReadViewport(call, call.arguments[7].ref, call.arguments[8].AsInt());
        const auto inverse = GluInverse(GluMultiply(projection, model));
        if (!inverse.has_value() || viewport[2] == 0 || viewport[3] == 0) return dx::VmValue::Int(0);
        const auto output =
            GluMultiply(*inverse, std::array{2.0F * (call.arguments[0].AsFloat() - viewport[0]) / viewport[2] - 1.0F,
                                             2.0F * (call.arguments[1].AsFloat() - viewport[1]) / viewport[3] - 1.0F,
                                             2.0F * call.arguments[2].AsFloat() - 1.0F, 1.0F});
        GluWriteVector(call, call.arguments[9].ref, call.arguments[10].AsInt(), output);
        return dx::VmValue::Int(1);
    });
    // AOSP 4.4 ROM line 26.
    builder.Constructor(
        "()V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

Decl Declare_android_opengl_EGL14(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGL14;", "Ljava/lang/Object;");
    constexpr std::array constants{std::pair{"EGL_DEFAULT_DISPLAY", 0},
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
        egl.egl14_no_display = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLDisplay;");
        egl.egl14_no_context = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLContext;");
        egl.egl14_no_surface = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_DISPLAY", "Landroid/opengl/EGLDisplay;",
                                      egl.egl14_no_display);
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_CONTEXT", "Landroid/opengl/EGLContext;",
                                      egl.egl14_no_context);
        call.vm.SetIntrinsicStaticRef("Landroid/opengl/EGL14;", "EGL_NO_SURFACE", "Landroid/opengl/EGLSurface;",
                                      egl.egl14_no_surface);
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
    builder.StaticMethod("eglGetConfigs", "(Landroid/opengl/EGLDisplay;[Landroid/opengl/EGLConfig;II[II)Z",
                         Egl14ConfigsHandler(context, false));
    builder.StaticMethod("eglChooseConfig", "(Landroid/opengl/EGLDisplay;[II[Landroid/opengl/EGLConfig;II[II)Z",
                         Egl14ConfigsHandler(context, true));
    builder.StaticMethod("eglGetConfigAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Z",
                         Egl14QueryValueHandler(context, "eglGetConfigAttrib"));
    builder.StaticMethod(
        "eglCreateWindowSurface",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Ljava/lang/Object;[II)Landroid/opengl/EGLSurface;",
        Egl14CreateWindowHandler(context));
    builder.StaticMethod("eglCreatePbufferSurface",
                         "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
                         Egl14CreatePbufferHandler(context));
    builder.StaticMethod(
        "eglCreateContext",
        "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;Landroid/opengl/EGLContext;[II)Landroid/opengl/EGLContext;",
        Egl14CreateContextHandler(context));
    for (
        const auto [name, descriptor] :
        {std::pair{"eglDestroyContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;)Z"},
         std::pair{"eglDestroySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"},
         std::pair{
             "eglMakeCurrent",
             "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;Landroid/opengl/EGLSurface;Landroid/opengl/EGLContext;)Z"},
         std::pair{"eglSwapBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;)Z"}}) {
        builder.StaticMethod(name, descriptor, Egl14ObjectHandler(context, name));
    }
    builder.StaticMethod("eglQueryContext", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLContext;I[II)Z",
                         Egl14QueryValueHandler(context, "eglQueryContext"));
    builder.StaticMethod("eglQuerySurface", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I[II)Z",
                         Egl14QueryValueHandler(context, "eglQuerySurface"));
    builder.StaticMethod("eglCreatePixmapSurface",
                         "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLConfig;I[II)Landroid/opengl/EGLSurface;",
                         [context](dx::IntrinsicContext& call) {
                             auto& egl = context->egl;
                             return WithIntArrayOffset(
                                 call, context, call.arguments[3].ref, call.arguments[4].AsInt(), false,
                                 [&](const std::uint32_t attributes) {
                                     const auto handle =
                                         NativeEgl(call, context, "eglCreatePixmapSurface",
                                                   std::array{egl.native_display,
                                                              ConfigHandle(egl.egl14_configs, call.arguments[1].ref),
                                                              call.arguments[2].cat1, attributes});
                                     if (handle == 0U) {
                                         return dx::VmValue::Ref(egl.egl14_no_surface);
                                     }
                                     const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
                                     egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                                     return dx::VmValue::Ref(wrapper);
                                 });
                         });
    builder.StaticMethod(
        "eglCreatePbufferFromClientBuffer",
        "(Landroid/opengl/EGLDisplay;IILandroid/opengl/EGLConfig;[II)Landroid/opengl/EGLSurface;",
        [context](dx::IntrinsicContext& call) {
            auto& egl = context->egl;
            return WithIntArrayOffset(
                call, context, call.arguments[4].ref, call.arguments[5].AsInt(), false,
                [&](const std::uint32_t attributes) {
                    const auto handle =
                        NativeEgl(call, context, "eglCreatePbufferFromClientBuffer",
                                  std::array{egl.native_display, call.arguments[1].cat1, call.arguments[2].cat1,
                                             ConfigHandle(egl.egl14_configs, call.arguments[3].ref), attributes});
                    if (handle == 0U) {
                        return dx::VmValue::Ref(egl.egl14_no_surface);
                    }
                    const auto wrapper = call.vm.NewIntrinsicInstance("Landroid/opengl/EGLSurface;");
                    egl.egl14_surfaces.emplace(wrapper.Value(), handle);
                    return dx::VmValue::Ref(wrapper);
                });
        });
    for (const auto [name, descriptor] :
         {std::pair{"eglSurfaceAttrib", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;II)Z"},
          std::pair{"eglBindTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
          std::pair{"eglReleaseTexImage", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"},
          std::pair{"eglCopyBuffers", "(Landroid/opengl/EGLDisplay;Landroid/opengl/EGLSurface;I)Z"}}) {
        builder.StaticMethod(name, descriptor, [context, method = std::string{name}](dx::IntrinsicContext& call) {
            auto& egl = context->egl;
            const auto found = egl.egl14_surfaces.find(call.arguments[1].ref.Value());
            const auto surface = found == egl.egl14_surfaces.end() ? 0U : found->second;
            std::vector<std::uint32_t> arguments{egl.native_display, surface,
                                                 static_cast<std::uint32_t>(call.arguments[2].cat1)};
            if (method == "eglSurfaceAttrib") {
                arguments.push_back(static_cast<std::uint32_t>(call.arguments[3].cat1));
            }
            return Bool(NativeEgl(call, context, method, arguments) != 0U);
        });
    }
    // AOSP 4.4 ROM line 30.
    builder.Constructor(
        "()V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

Decl Declare_android_opengl_GLES30(const Context& context) {
    return DeclareJavaGlesClass(context, "Landroid/opengl/GLES30;", "Landroid/opengl/GLES20;", gles::GlesApi::gles3,
                                generated_java_gles::kGLES30Methods, generated_java_gles::kGLES30Constants);
}

[[nodiscard]] std::uint32_t Egl14WrapperHandle(const DexVmAndroidContext::EglFacadeState& egl,
                                               const dx::VmObjectRef object) {
    if (object == egl.egl14_no_display || object == egl.egl14_no_context || object == egl.egl14_no_surface) return 0U;
    if (object == egl.egl14_display) return egl.native_display;
    if (const auto found = egl.egl14_configs.find(object.Value()); found != egl.egl14_configs.end())
        return found->second;
    if (const auto found = egl.egl14_contexts.find(object.Value()); found != egl.egl14_contexts.end())
        return found->second;
    if (const auto found = egl.egl14_surfaces.find(object.Value()); found != egl.egl14_surfaces.end())
        return found->second;
    return 0U;
}

Decl Declare_android_opengl_EGLObjectHandle(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGLObjectHandle;", "Ljava/lang/Object;");
    builder.VirtualMethod("getHandle", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(Egl14WrapperHandle(context->egl, call.receiver)));
    });
    builder.OverrideMethod("hashCode", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(Egl14WrapperHandle(context->egl, call.receiver)));
    });
    const auto wu_mHandle = builder.BoundInstanceField("mHandle", "I", dx::kAccPrivate | dx::kAccFinal);
    // AOSP 4.4 ROM line 27.
    builder.Constructor(
        "(I)V",
        [wu_mHandle](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            api.SetInt(wu_mHandle, c.arguments[0].AsInt());
            return dx::VmValue::Void();
        },
        dx::kAccProtected);

    return std::move(builder).Build();
}

dx::IntrinsicHandler Egl14EqualsHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto other = call.arguments[0].ref;
        if (!other.IsValid() || call.vm.Model().ObjectClass(other) != call.vm.Model().ObjectClass(call.receiver)) {
            return Bool(false);
        }
        return Bool(Egl14WrapperHandle(context->egl, call.receiver) == Egl14WrapperHandle(context->egl, other));
    };
}

Decl Declare_android_opengl_EGLConfig(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGLConfig;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z", Egl14EqualsHandler(context));
    return std::move(builder).Build();
}

Decl Declare_android_opengl_EGLContext(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGLContext;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z", Egl14EqualsHandler(context));
    return std::move(builder).Build();
}

Decl Declare_android_opengl_EGLDisplay(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGLDisplay;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z", Egl14EqualsHandler(context));
    return std::move(builder).Build();
}

Decl Declare_android_opengl_EGLSurface(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/opengl/EGLSurface;", "Landroid/opengl/EGLObjectHandle;");
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z", Egl14EqualsHandler(context));
    return std::move(builder).Build();
}

namespace {
using EglValue = dx::VmValue;

std::uint32_t RomEglHandle(dx::IntrinsicContext& call, std::size_t index, const char* field) {
    return WindowField(call.vm, dx::IntrinsicCall(call).NonNullRef(index, field), field, "I").bits;
}

dx::IntrinsicHandler RomEglForward(const Context& context, std::string name, std::vector<const char*> fields,
                                   bool pace = false) {
    return [context, name = std::move(name), fields = std::move(fields), pace](dx::IntrinsicContext& call) {
        std::vector<std::uint32_t> args;
        for (std::size_t i = 0; i < fields.size(); ++i)
            args.push_back(fields[i] ? RomEglHandle(call, i, fields[i]) : call.arguments[i].cat1);
        const auto result = NativeEgl(call, context, name, args);
        if (result && name == "eglMakeCurrent") {
            const auto token = call.vm.CurrentContextToken();
            const bool native_owned = context->threads && context->threads->NativeContextDaemon(token).has_value();
            const bool window = args.back() && context->egl.window_surfaces.contains(args[1]);
            const auto thread = window && token != 1U && !native_owned ? EglThreadId(call, context) : 0U;
            {
                std::scoped_lock lock(context->egl.pace_mutex);
                if (thread) {
                    auto& producer = context->egl.producers[std::this_thread::get_id()];
                    producer = {false, false, thread, token, args[1]};
                } else {
                    context->egl.producers.erase(std::this_thread::get_id());
                }
            }
            context->egl.pace_changed.notify_all();
        }
        if (result && name == "eglDestroySurface") {
            context->egl.window_surfaces.erase(args[1]);
            std::scoped_lock lock(context->egl.pace_mutex);
            std::erase_if(context->egl.producers,
                          [&](const auto& item) { return item.second.window_surface == args[1]; });
            context->egl.pace_changed.notify_all();
        }
        if (pace && result && context->egl.window_surfaces.contains(args[1])) {
            // Native-owned threads may call EGL through JNI while running a
            // process-lifetime loop. Keep their existing one-way pacing, but
            // do not make their loop depend on a future Clock advance.
            const bool native_owned =
                context->threads && context->threads->NativeContextDaemon(call.vm.CurrentContextToken()).has_value();
            PaceEglSwap(*context, call.vm.ExecutionLock(), native_owned ? 0U : EglThreadId(call, context), args[1],
                        call.vm.CurrentContextToken());
        }
        return EglValue::Int(static_cast<int>(result));
    };
}

dx::IntrinsicHandler RomEglConfigs(const Context& context, bool choose) {
    return [context, choose](dx::IntrinsicContext& call) {
        const auto display = RomEglHandle(call, 0, "mEGLDisplay");
        const auto configs = call.arguments[choose ? 2U : 1U].ref;
        const auto capacity = call.arguments[choose ? 3U : 2U].AsInt();
        const auto count = call.arguments[choose ? 4U : 3U].ref;
        if (capacity < 0 || !count.IsValid() || call.vm.Model().ArrayLength(count) < 1 ||
            (configs.IsValid() && call.vm.Model().ArrayLength(configs) < capacity)) {
            LatchEglError(call, context, kBadParameter);
            return Bool(false);
        }
        const auto length = configs.IsValid() ? static_cast<std::size_t>(capacity) : 0U;
        return WithIntArray(call, context, count, true, [&](std::uint32_t count_addr) {
            const auto execute = [&](std::uint32_t attributes) {
                std::vector<std::byte> storage(length * sizeof(std::uint32_t)), output;
                const auto result = context->session->NIO().WithTemporaryGuestMemory(
                    storage, true,
                    [&](memory::GuestAddress config_addr) {
                        const auto data = length == 0 ? 0U : config_addr.Value();
                        return choose ? NativeEgl(call, context, "eglChooseConfig",
                                                  std::array{display, attributes, data, static_cast<unsigned>(length),
                                                             count_addr})
                                      : NativeEgl(call, context, "eglGetConfigs",
                                                  std::array{display, data, static_cast<unsigned>(length), count_addr});
                    },
                    &output);
                if (result != 0U) {
                    for (std::size_t i = 0; i < length; ++i) {
                        std::uint32_t handle{};
                        for (std::size_t byte = 0; byte < sizeof(handle); ++byte)
                            handle |= std::to_integer<std::uint32_t>(output[i * sizeof(handle) + byte]) << (byte * 8U);
                        if (handle == 0U) break;
                        const auto wrapper = NewPlatformObject(call.vm, "Lcom/google/android/gles_jni/EGLConfigImpl;",
                                                               "(I)V", {EglValue::Int(static_cast<int>(handle))});
                        call.vm.Model().SetObjectElement(configs, static_cast<JniSize>(i), wrapper);
                    }
                }
                return Bool(result != 0U);
            };
            if (choose) return WithIntArray(call, context, call.arguments[1].ref, false, execute);
            return execute(0U);
        });
    };
}
}  // namespace

Decl Declare_com_google_android_gles_jni_EGLImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lcom/google/android/gles_jni/EGLImpl;");
    builder.MergesBootClass();
    constexpr auto kPublicNative = dx::kAccPublic | dx::kAccNative;
    constexpr auto kPrivateNative = dx::kAccPrivate | dx::kAccNative;
    constexpr auto dpy = "mEGLDisplay";
    constexpr auto cfg = "mEGLConfig";
    constexpr auto ctx = "mEGLContext";
    constexpr auto surf = "mEGLSurface";
    builder.StaticMethod(
        "_nativeClassInit", "()V", [](dx::IntrinsicContext&) { return EglValue::Void(); },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);

    builder.DirectMethod(
        "_eglGetDisplay", "(Ljava/lang/Object;)I",
        [context](dx::IntrinsicContext& c) {
            if (c.arguments[0].ref.IsValid()) {
                LatchEglError(c, context, kBadParameter);
                return EglValue::Int(0);
            }
            return EglValue::Int(static_cast<int>(NativeEgl(c, context, "eglGetDisplay", std::array{0U})));
        },
        kPrivateNative);
    for (const auto& [name, signature, native] :
         std::array{std::tuple{"_eglGetCurrentDisplay", "()I", "eglGetCurrentDisplay"},
                    std::tuple{"_eglGetCurrentContext", "()I", "eglGetCurrentContext"},
                    std::tuple{"_eglGetCurrentSurface", "(I)I", "eglGetCurrentSurface"}}) {
        builder.DirectMethod(
            name, signature,
            RomEglForward(context, native,
                          signature[1] == 'I' ? std::vector<const char*>{nullptr} : std::vector<const char*>{}),
            kPrivateNative);
    }
    builder.VirtualMethod(
        "eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z",
        [context](dx::IntrinsicContext& c) {
            const auto display = RomEglHandle(c, 0, dpy);
            const auto out = c.arguments[1].ref;
            if (out.IsValid() && c.vm.Model().ArrayLength(out) < 2) {
                LatchEglError(c, context, kBadParameter);
                return Bool(false);
            }
            return WithIntArray(c, context, out, true, [&](std::uint32_t address) {
                const auto result =
                    NativeEgl(c, context, "eglInitialize", std::array{display, address, address ? address + 4 : 0U});
                return Bool(result != 0U);
            });
        },
        kPublicNative);
    builder.VirtualMethod(
        "eglChooseConfig",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
        RomEglConfigs(context, true), kPublicNative);
    builder.VirtualMethod(
        "eglGetConfigs", "(Ljavax/microedition/khronos/egl/EGLDisplay;[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
        RomEglConfigs(context, false), kPublicNative);

    builder.DirectMethod(
        "_eglCreateContext",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)I",
        [context](dx::IntrinsicContext& c) {
            const auto display = RomEglHandle(c, 0, dpy);
            const auto config = RomEglHandle(c, 1, cfg);
            const auto share = RomEglHandle(c, 2, ctx);
            return WithIntArray(c, context, c.arguments[3].ref, false, [&](std::uint32_t address) {
                return EglValue::Int(static_cast<int>(
                    NativeEgl(c, context, "eglCreateContext", std::array{display, config, share, address})));
            });
        },
        kPrivateNative);
    builder.DirectMethod(
        "_eglCreatePbufferSurface",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;[I)I",
        [context](dx::IntrinsicContext& c) {
            const auto display = RomEglHandle(c, 0, dpy);
            const auto config = RomEglHandle(c, 1, cfg);
            return WithIntArray(c, context, c.arguments[2].ref, false, [&](std::uint32_t address) {
                return EglValue::Int(static_cast<int>(
                    NativeEgl(c, context, "eglCreatePbufferSurface", std::array{display, config, address})));
            });
        },
        kPrivateNative);
    builder.DirectMethod(
        "_eglCreateWindowSurface",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)I",
        [context](dx::IntrinsicContext& c) {
            const auto display = RomEglHandle(c, 0, dpy);
            const auto config = RomEglHandle(c, 1, cfg);
            const auto surface = ResolveRomSurfaceWindow(c.vm, context, c.arguments[2].ref);
            if (!surface) {
                LatchEglError(c, context, kBadNativeWindow);
                return EglValue::Int(0);
            }
            context->session->Process().UpdateSurfaceWindow(*surface);
            return WithIntArray(c, context, c.arguments[3].ref, false, [&](std::uint32_t address) {
                const auto handle = NativeEgl(c, context, "eglCreateWindowSurface",
                                              std::array{display, config, surface->identity.Value(), address});
                if (handle) context->egl.window_surfaces.insert(handle);
                return EglValue::Int(static_cast<int>(handle));
            });
        },
        kPrivateNative);

    const auto query = [&](const char* name, const char* sig, const char* target) {
        builder.VirtualMethod(
            name, sig,
            [context, name, target](dx::IntrinsicContext& c) {
                const auto display = RomEglHandle(c, 0, dpy);
                const auto resource = RomEglHandle(c, 1, target);
                const auto output = c.arguments[3].ref;
                if (!output.IsValid() || c.vm.Model().ArrayLength(output) < 1) {
                    LatchEglError(c, context, kBadParameter);
                    return Bool(false);
                }
                return WithIntArray(c, context, output, true, [&](std::uint32_t address) {
                    return Bool(
                        NativeEgl(c, context, name, std::array{display, resource, c.arguments[2].cat1, address}) != 0U);
                });
            },
            kPublicNative);
    };
    query("eglGetConfigAttrib",
          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z", cfg);
    query("eglQuerySurface",
          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;I[I)Z", surf);
    query("eglQueryContext",
          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;I[I)Z", ctx);
    builder.VirtualMethod(
        "eglQueryString", "(Ljavax/microedition/khronos/egl/EGLDisplay;I)Ljava/lang/String;",
        [context](dx::IntrinsicContext& c) {
            const auto param = c.arguments[1].cat1;
            if (NativeEgl(c, context, "eglQueryString", std::array{RomEglHandle(c, 0, dpy), param}) == 0U)
                return EglValue::Ref(dx::VmObjectRef{});
            return MakeString(c, context->session->ManagedEglString(param));
        },
        kPublicNative);
    builder.VirtualMethod(
        "eglMakeCurrent",
        "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
        RomEglForward(context, "eglMakeCurrent", {dpy, surf, surf, ctx}), kPublicNative);
    builder.VirtualMethod("eglDestroyContext",
                          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                          RomEglForward(context, "eglDestroyContext", {dpy, ctx}), kPublicNative);
    builder.VirtualMethod("eglDestroySurface",
                          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                          RomEglForward(context, "eglDestroySurface", {dpy, surf}), kPublicNative);
    builder.VirtualMethod("eglSwapBuffers",
                          "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                          RomEglForward(context, "eglSwapBuffers", {dpy, surf}, true), kPublicNative);
    builder.VirtualMethod("eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z",
                          RomEglForward(context, "eglTerminate", {dpy}), kPublicNative);
    builder.VirtualMethod("eglGetError", "()I", RomEglForward(context, "eglGetError", {}), kPublicNative);
    builder.VirtualMethod("eglReleaseThread", "()Z", RomEglForward(context, "eglReleaseThread", {}), kPublicNative);
    builder.VirtualMethod("eglWaitGL", "()Z", RomEglForward(context, "eglWaitGL", {}), kPublicNative);
    builder.VirtualMethod("eglWaitNative", "(ILjava/lang/Object;)Z", RomEglForward(context, "eglWaitNative", {nullptr}),
                          kPublicNative);
    return std::move(builder).Build();
}

Decl Declare_com_google_android_gles_jni_GLImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lcom/google/android/gles_jni/GLImpl;");
    builder.MergesBootClass();
    for (const auto& method : generated_java_gles::kGLImplNativeMethods) {
        dx::IntrinsicHandler handler;
        std::string name = method.name;
        std::string descriptor = method.descriptor;
        if (name == "_nativeClassInit") {
            // The original initializer caches JNI IDs. Host adapters use the
            // linker/NIO APIs directly and need no process-global JNI cache.
            handler = [](dx::IntrinsicContext&) { return dx::VmValue::Void(); };
        } else {
            if (name == "_glGetString") name = "glGetString";
            const bool bounds = name.ends_with("Bounds");
            if (bounds) {
                name.resize(name.size() - 6U);
                descriptor.erase(descriptor.size() - 3U, 1U);
            }
            const auto api = gles::FindGlesFunction(gles::GlesApi::gles1, name).has_value()
                                 ? gles::GlesApi::gles1
                                 : gles::GlesApi::gles1_extensions;
            auto bridge = JavaGlesHandler(context, api, name, descriptor);
            const bool client_pointer = name.ends_with("Pointer") || name.ends_with("PointerOES");
            handler = [context, bridge = std::move(bridge), bounds, client_pointer,
                       descriptor = std::move(descriptor)](dx::IntrinsicContext& call) {
                if (client_pointer && descriptor.find("Ljava/nio/Buffer;") != std::string::npos) {
                    const auto pointer = call.arguments[call.arguments.size() - (bounds ? 2U : 1U)].ref;
                    if (!pointer.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "pointer"};
                    const auto identity = call.vm.Model().ToIdentity(pointer);
                    const auto snapshot = call.vm.NIO().Snapshot(identity);
                    if (!snapshot.direct) {
                        if (context->target_sdk_version > 3U)
                            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                                  "pointer must be a direct Buffer"};
                        Record(call, "dexvm.gl_client_array.indirect_buffer");
                        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                              "persistent indirect GL buffers are not supported"};
                    }
                    if (bounds && call.arguments.back().AsInt() != snapshot.limit - snapshot.position)
                        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                              "pointer remaining count mismatch"};
                }
                dx::IntrinsicContext adapted{
                    call.vm, call.receiver, bounds ? call.arguments.first(call.arguments.size() - 1U) : call.arguments};
                return bridge(adapted);
            };
        }
        const auto flags = dx::kAccNative | (method.is_private ? dx::kAccPrivate : dx::kAccPublic);
        if (method.is_static)
            builder.StaticMethod(method.name, method.descriptor, std::move(handler), flags);
        else if (method.is_private)
            builder.DirectMethod(method.name, method.descriptor, std::move(handler), flags);
        else
            builder.VirtualMethod(method.name, method.descriptor, std::move(handler), flags);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
