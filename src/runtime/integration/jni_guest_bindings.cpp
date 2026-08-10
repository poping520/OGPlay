#include "ogplay/runtime/integration/jni_guest_bindings.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_static_calls.h"
#include "ogplay/runtime/integration/jni_guest_string_bindings.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
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
    std::string result;
    result.reserve(32);
    for (std::size_t index = 0; index < kMaximumBytes; ++index) {
        std::byte byte{};
        address_space.Read(address.Add(index), std::span{&byte, 1}, thread_id);
        const auto value = std::to_integer<unsigned char>(byte);
        if (value == 0) return result;
        result.push_back(static_cast<char>(value));
    }
    throw JniGuestBindingError(
        "JNI guest " + std::string(field) + " is not null-terminated");
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
                           JniInvocationEngine& invocations,
                           JniStringStore& strings,
                           JniPrimitiveArrayStore& arrays,
                           JniJavaVm& java_vm,
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
    BindJniGuestStaticCallSlots(dispatcher, environment, classes, invocations,
                                address_space);
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
    BindJniGuestModifiedUtf8Slots(
        dispatcher, environment, strings, address_space);

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

}  // namespace ogplay::runtime
