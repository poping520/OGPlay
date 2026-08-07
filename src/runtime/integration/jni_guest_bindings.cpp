#include "ogplay/runtime/integration/jni_guest_bindings.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_java_vm.h"

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

[[nodiscard]] std::size_t Capacity(const std::uint32_t value,
                                   const char* slot) {
    const auto signed_value = std::bit_cast<JniInt>(value);
    if (signed_value < 0) {
        throw JniGuestBindingError(
            std::string(slot) + " capacity cannot be negative");
    }
    return static_cast<std::size_t>(signed_value);
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
