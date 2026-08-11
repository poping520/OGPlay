#include "ogplay/runtime/integration/jni_guest_bindings.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_static_calls.h"
#include "ogplay/runtime/integration/jni_guest_static_fields.h"
#include "ogplay/runtime/integration/jni_guest_string_bindings.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] JniSlot EnvironmentSlot(const std::string_view name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI guest slot is absent");
    }
    return *slot;
}

[[nodiscard]] JniInvokeSlot JavaVmSlot(const std::string_view name) {
    const auto slot = FindJniInvokeSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JavaVM guest slot is absent");
    }
    return *slot;
}

[[nodiscard]] JniGuestCallResult Word(const std::uint32_t value) {
    return {JniGuestReturnWidth::word, {value, 0U}};
}

[[nodiscard]] JniGuestCallResult Int(const JniInt value) {
    return Word(std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] JniGuestCallResult Reference(const JniReference value) {
    return Word(value.Value());
}

[[nodiscard]] std::string ReadCString(
    memory::AddressSpace& address_space,
    const memory::GuestAddress address,
    const std::uint64_t thread_id,
    const std::string_view field) {
    constexpr std::size_t kMaximumBytes = 1024;
    if (address.IsNull()) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " pointer is null");
    }
    std::size_t length{};
    try {
        length = address_space.CStringLength(address, kMaximumBytes, thread_id);
    } catch (const std::length_error&) {
        throw JniGuestBindingError(
            "JNI guest " + std::string(field) + " is not null-terminated");
    }
    std::string result(length, '\0');
    address_space.Read(address, std::as_writable_bytes(std::span(result)),
                       thread_id);
    return result;
}

[[nodiscard]] std::size_t Capacity(const std::uint32_t value,
                                   const char* slot) {
    const auto signed_value = std::bit_cast<JniInt>(value);
    if (signed_value < 0) {
        throw JniGuestBindingError(
            std::string(slot) + " capacity cannot be negative");
    }
    return static_cast<std::size_t>(signed_value);
}

[[nodiscard]] std::uint32_t Read32(
    memory::AddressSpace& address_space,
    const memory::GuestAddress address,
    const std::uint64_t thread_id) {
    std::array<std::byte, 4> bytes{};
    address_space.Read(address, bytes, thread_id);
    std::uint32_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= std::to_integer<std::uint32_t>(bytes[index])
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

void ValidateOutput(memory::AddressSpace& address_space,
                    const memory::GuestAddress address,
                    const std::uint64_t thread_id) {
    if (address.IsNull() || (address.Value() & 3U) != 0U) {
        throw JniGuestBindingError(
            "JNI guest output pointer must be word aligned");
    }
    address_space.Validate(
        {address, sizeof(std::uint32_t)},
        memory::AccessType::write, thread_id);
}

void Write32(memory::AddressSpace& address_space,
             const memory::GuestAddress address, const std::uint32_t value,
             const std::uint64_t thread_id) {
    ValidateOutput(address_space, address, thread_id);
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    address_space.Write(address, bytes, thread_id);
}

void WriteEnvironmentResult(memory::AddressSpace& address_space,
                            const JniGuestCallFrame& frame,
                            const JniJavaVmResult& result) {
    Write32(address_space,
            memory::GuestAddress{frame.registers[1]},
            result.status == JniStatus::ok
                ? kJniGuestEnvironment.Value()
                : 0U,
            frame.thread_id);
}

void RequireNullAttachArguments(const JniGuestCallFrame& frame) {
    if (frame.registers[2] != 0U) {
        throw JniGuestBindingError(
            "non-null JavaVM attach arguments are not implemented");
    }
}

}  // namespace

void BindJniGuestCoreSlots(JniGuestCallDispatcher& dispatcher,
                           JniEnvironment& environment,
                           JniClassRegistry& classes,
                           JniStringStore& strings,
                           JniPrimitiveArrayStore& arrays,
                           memory::AddressSpace& address_space) {
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetVersion"),
        [&environment](const JniGuestCallFrame& frame) {
            return Int(environment.GetVersion(frame.thread_id));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("Throw"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.Throw(frame.thread_id,
                              JniReference{frame.registers[1]});
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("ExceptionOccurred"),
        [&environment](const JniGuestCallFrame& frame) {
            return Reference(
                environment.ExceptionOccurred(frame.thread_id));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("ExceptionClear"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.ExceptionClear(frame.thread_id);
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("PushLocalFrame"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.PushLocalFrame(
                frame.thread_id,
                Capacity(frame.registers[1], "PushLocalFrame"));
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("PopLocalFrame"),
        [&environment](const JniGuestCallFrame& frame) {
            return Reference(environment.PopLocalFrame(
                frame.thread_id,
                JniReference{frame.registers[1]}));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewGlobalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            return Reference(environment.NewGlobalRef(
                frame.thread_id,
                JniReference{frame.registers[1]}));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("DeleteGlobalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.DeleteGlobalRef(
                frame.thread_id,
                JniReference{frame.registers[1]});
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("DeleteLocalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.DeleteLocalRef(
                frame.thread_id,
                JniReference{frame.registers[1]});
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("IsSameObject"),
        [&environment](const JniGuestCallFrame& frame) {
            return Word(environment.IsSameObject(
                            frame.thread_id,
                            JniReference{frame.registers[1]},
                            JniReference{frame.registers[2]})
                            ? 1U
                            : 0U);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewLocalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            return Reference(environment.NewLocalRef(
                frame.thread_id,
                JniReference{frame.registers[1]}));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("EnsureLocalCapacity"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.EnsureLocalCapacity(
                frame.thread_id,
                Capacity(frame.registers[1], "EnsureLocalCapacity"));
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetJavaVM"),
        [&address_space](const JniGuestCallFrame& frame) {
            Write32(address_space,
                    memory::GuestAddress{frame.registers[1]},
                    kJniGuestJavaVm.Value(), frame.thread_id);
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewWeakGlobalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            return Reference(environment.NewWeakGlobalRef(
                frame.thread_id,
                JniReference{frame.registers[1]}));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("DeleteWeakGlobalRef"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.DeleteWeakGlobalRef(
                frame.thread_id,
                JniReference{frame.registers[1]});
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("ExceptionCheck"),
        [&environment](const JniGuestCallFrame& frame) {
            return Word(environment.ExceptionCheck(frame.thread_id)
                            ? 1U
                            : 0U);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetStaticMethodID"),
        [&environment, &classes,
         &address_space](const JniGuestCallFrame& frame) {
            const auto identity = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!identity.has_value()) {
                throw JniGuestBindingError(
                    "GetStaticMethodID requires a valid class reference");
            }
            const auto name = ReadCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "method name");
            const auto descriptor = ReadCString(
                address_space, memory::GuestAddress{frame.registers[3]},
                frame.thread_id, "method descriptor");
            const auto method =
                classes.GetMethodId(*identity, name, descriptor, true);
            if (!method.has_value()) {
                throw JniGuestBindingError(
                    "JNI guest static method is not declared: " +
                    name + descriptor);
            }
            return Word(method->Value());
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetArrayLength"),
        [&environment, &arrays](const JniGuestCallFrame& frame) {
            const auto array = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!array.has_value()) {
                throw JniGuestBindingError(
                    "GetArrayLength requires a valid array reference");
            }
            return Int(arrays.Length(*array));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetByteArrayRegion"),
        [&environment, &arrays,
         &address_space](const JniGuestCallFrame& frame) {
            const auto array = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!array.has_value()) {
                throw JniGuestBindingError(
                    "GetByteArrayRegion requires a valid array reference");
            }
            if (arrays.Kind(*array) != JniPrimitiveKind::byte) {
                throw JniGuestBindingError(
                    "GetByteArrayRegion requires a byte array reference");
            }
            const auto start = std::bit_cast<JniSize>(frame.registers[2]);
            const auto length = std::bit_cast<JniSize>(frame.registers[3]);
            const auto data = std::get<std::vector<JniByte>>(
                arrays.Region(*array, start, length));
            const auto destination = memory::GuestAddress{
                Read32(address_space, frame.stack_pointer, frame.thread_id)};
            if (destination.IsNull() && !data.empty()) {
                throw JniGuestBindingError(
                    "GetByteArrayRegion requires a non-null output buffer");
            }
            address_space.Write(
                destination, std::as_bytes(std::span{data}), frame.thread_id);
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewStringUTF"),
        [&environment, &strings,
         &address_space](const JniGuestCallFrame& frame) {
            const auto text = ReadCString(
                address_space, memory::GuestAddress{frame.registers[1]},
                frame.thread_id, "modified UTF-8");
            const std::vector<std::uint8_t> encoded(text.begin(), text.end());
            const auto identity = strings.CreateModifiedUtf8(encoded);
            try {
                return Reference(environment.PublishLocalObject(
                    frame.thread_id, identity));
            } catch (...) {
                strings.Delete(identity);
                throw;
            }
        });
}

void BindJniGuestJavaVmSlots(JniGuestCallDispatcher& dispatcher,
                             JniJavaVm& java_vm,
                             memory::AddressSpace& address_space) {
    dispatcher.BindJavaVm(
        JavaVmSlot("AttachCurrentThread"),
        [&java_vm, &address_space](const JniGuestCallFrame& frame) {
            RequireNullAttachArguments(frame);
            ValidateOutput(
                address_space,
                memory::GuestAddress{frame.registers[1]},
                frame.thread_id);
            const auto result = java_vm.AttachCurrentThread(
                frame.thread_id, kJniVersion1_6);
            WriteEnvironmentResult(address_space, frame, result);
            return Int(static_cast<JniInt>(result.status));
        });
    dispatcher.BindJavaVm(
        JavaVmSlot("DetachCurrentThread"),
        [&java_vm](const JniGuestCallFrame& frame) {
            return Int(static_cast<JniInt>(
                java_vm.DetachCurrentThread(frame.thread_id)));
        });
    dispatcher.BindJavaVm(
        JavaVmSlot("GetEnv"),
        [&java_vm, &address_space](const JniGuestCallFrame& frame) {
            ValidateOutput(
                address_space,
                memory::GuestAddress{frame.registers[1]},
                frame.thread_id);
            const auto result = java_vm.GetEnv(
                frame.thread_id,
                std::bit_cast<JniInt>(frame.registers[2]));
            WriteEnvironmentResult(address_space, frame, result);
            return Int(static_cast<JniInt>(result.status));
        });
    dispatcher.BindJavaVm(
        JavaVmSlot("AttachCurrentThreadAsDaemon"),
        [&java_vm, &address_space](const JniGuestCallFrame& frame) {
            RequireNullAttachArguments(frame);
            ValidateOutput(
                address_space,
                memory::GuestAddress{frame.registers[1]},
                frame.thread_id);
            const auto result = java_vm.AttachCurrentThreadAsDaemon(
                frame.thread_id, kJniVersion1_6);
            WriteEnvironmentResult(address_space, frame, result);
            return Int(static_cast<JniInt>(result.status));
        });
}

void BindJniGuestNativeRegistrationSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniNativeRegistry& natives,
    memory::AddressSpace& address_space) {
    dispatcher.BindEnvironment(
        EnvironmentSlot("RegisterNatives"),
        [&environment, &classes, &natives,
         &address_space](const JniGuestCallFrame& frame) {
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "RegisterNatives requires a valid class reference");
            }
            try {
                static_cast<void>(
                    classes.IsAssignableFrom(*java_class, *java_class));
            } catch (const JniClassRegistryError&) {
                throw JniGuestBindingError(
                    "RegisterNatives requires a declared class");
            }

            const auto signed_count =
                std::bit_cast<JniInt>(frame.registers[3]);
            if (signed_count < 0) {
                throw JniGuestBindingError(
                    "RegisterNatives method count cannot be negative");
            }
            constexpr std::uint64_t kNativeMethodSize = 12U;
            constexpr std::uint64_t kMaximumNativeMethods = 65536U;
            const auto count = static_cast<std::uint64_t>(signed_count);
            if (count > kMaximumNativeMethods ||
                count > std::numeric_limits<std::uint32_t>::max() /
                            kNativeMethodSize) {
                throw JniGuestBindingError(
                    "RegisterNatives method array size is invalid");
            }
            const auto byte_size = count * kNativeMethodSize;
            std::vector<std::byte> encoded(static_cast<std::size_t>(byte_size));
            const auto methods_address =
                memory::GuestAddress{frame.registers[2]};
            if (!encoded.empty()) {
                if (methods_address.IsNull()) {
                    throw JniGuestBindingError(
                        "RegisterNatives requires a non-null method array");
                }
                address_space.Validate(
                    {methods_address, byte_size}, memory::AccessType::read,
                    frame.thread_id);
                address_space.Read(methods_address, encoded,
                                   frame.thread_id);
            }

            const auto word = [&encoded](const std::size_t offset) {
                std::uint32_t value{};
                for (std::size_t index = 0; index < 4U; ++index) {
                    value |= std::to_integer<std::uint32_t>(
                                 encoded[offset + index])
                             << static_cast<unsigned>(index * 8U);
                }
                return value;
            };
            std::vector<JniNativeMethod> methods;
            methods.reserve(static_cast<std::size_t>(count));
            for (std::size_t index = 0; index < count; ++index) {
                const auto offset = index *
                                    static_cast<std::size_t>(kNativeMethodSize);
                const auto name = ReadCString(
                    address_space, memory::GuestAddress{word(offset)},
                    frame.thread_id, "native method name");
                const auto descriptor = ReadCString(
                    address_space, memory::GuestAddress{word(offset + 4U)},
                    frame.thread_id, "native method descriptor");
                const auto target = memory::GuestAddress{word(offset + 8U)};
                if (target.IsNull()) {
                    throw JniGuestBindingError(
                        "RegisterNatives target cannot be null");
                }
                bool declared{};
                try {
                    declared = classes.GetMethodId(
                                   *java_class, name, descriptor, false)
                                   .has_value() ||
                               classes.GetMethodId(
                                   *java_class, name, descriptor, true)
                                   .has_value();
                } catch (const std::exception& error) {
                    throw JniGuestBindingError(
                        "RegisterNatives method is invalid: " +
                        std::string(error.what()));
                }
                if (!declared) {
                    throw JniGuestBindingError(
                        "RegisterNatives method is not declared: " + name +
                        descriptor);
                }
                methods.push_back({name, descriptor, target});
            }
            natives.RegisterNatives(*java_class, methods);
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("UnregisterNatives"),
        [&environment, &classes,
         &natives](const JniGuestCallFrame& frame) {
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "UnregisterNatives requires a valid class reference");
            }
            try {
                static_cast<void>(
                    classes.IsAssignableFrom(*java_class, *java_class));
            } catch (const JniClassRegistryError&) {
                throw JniGuestBindingError(
                    "UnregisterNatives requires a declared class");
            }
            static_cast<void>(natives.UnregisterNatives(*java_class));
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
}

A32GuestCallFrame ResolveJniRegisteredNativeCall(
    const JniNativeRegistry& natives, const JniObjectIdentity java_class,
    const std::string_view name, const std::string_view descriptor,
    A32GuestCallFrame frame) {
    const auto target = natives.Resolve(
        java_class, std::string(name), std::string(descriptor));
    if (!target.has_value()) {
        throw JniGuestBindingError(
            "registered JNI native method is unresolved: " +
            std::string(name) + std::string(descriptor));
    }
    frame.target = *target;
    return frame;
}

void BindJniGuestSlots(JniGuestCallDispatcher& dispatcher,
                       JniGuestBindingContext& context) {
    BindJniGuestCoreSlots(dispatcher, context.environment, context.classes,
                          context.strings, context.arrays,
                          context.address_space);
    BindJniGuestClassAndInstanceSlots(
        dispatcher, context.environment, context.classes,
        context.invocations, context.address_space, &context.objects);
    BindJniGuestStaticCallSlots(
        dispatcher, context.environment, context.classes,
        context.invocations, context.address_space);
    BindJniGuestStaticFieldSlots(
        dispatcher, context.environment, context.classes, context.fields,
        context.address_space);
    BindJniGuestInstanceFieldSlots(
        dispatcher, context.environment, context.classes, context.fields,
        context.objects, context.address_space);
    BindJniGuestModifiedUtf8Slots(
        dispatcher, context.environment, context.strings,
        context.address_space);
    BindJniGuestUtf16Slots(
        dispatcher, context.environment, context.strings,
        context.address_space);
    if (context.natives != nullptr) {
        BindJniGuestNativeRegistrationSlots(
            dispatcher, context.environment, context.classes,
            *context.natives, context.address_space);
    }
    BindJniGuestJavaVmSlots(dispatcher, context.java_vm,
                            context.address_space);
}

}  // namespace ogplay::runtime
