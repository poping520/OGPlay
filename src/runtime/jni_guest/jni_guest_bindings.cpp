#include "jni_guest_lookup.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_fields.h"
#include "ogplay/runtime/jni_guest/jni_guest_string_bindings.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "jni_guest_memory.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_utf.h"

namespace ogplay::runtime {

void BindJniGuestArraySlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniStringStore& strings,
    JniPrimitiveArrayStore& arrays,
    JniGuestObjectRegistry& objects, memory::AddressSpace& address_space);

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

[[nodiscard]] std::string Hex32(const std::uint32_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

[[nodiscard]] std::string JniCallFailure(
    const std::string_view slot, const JniGuestCallFrame& frame,
    const std::string_view cause) {
    return "JNI guest call failed:\n"
           "  slot=" +
           std::string(slot) + "\n"
           "  guest_thread=" + std::to_string(frame.thread_id) + "\n"
           "  call_site:\n"
           "    lr=" + Hex32(frame.link_register) + "\n"
           "    sp=" + Hex32(frame.stack_pointer.Value()) + "\n"
           "  registers:\n"
           "    r0=" + Hex32(frame.registers[0]) + "\n"
           "    r1=" + Hex32(frame.registers[1]) + "\n"
           "    r2=" + Hex32(frame.registers[2]) + "\n"
           "    r3=" + Hex32(frame.registers[3]) + "\n"
           "  cause:\n    " + std::string(cause);
}

// FatalError is already terminal: retain every readable byte without letting a
// bad pointer or encoding replace the original fatal diagnostic.
[[nodiscard]] std::string FatalErrorDiagnostic(
    memory::AddressSpace& space, const JniGuestCallFrame& frame) {
    constexpr std::size_t kScanLimit = 4096U;
    constexpr std::string_view kHex = "0123456789abcdef";
    const memory::GuestAddress pointer{frame.registers[1]};
    std::string message;
    std::string status = pointer.IsNull() ? "null_pointer" : "scan_limit_reached";
    std::string read_error;
    std::size_t scanned{};
    if (!pointer.IsNull()) {
        try {
            for (std::size_t offset = 0; offset < kScanLimit; ++offset) {
                const auto byte = ReadGuest8(space, pointer.Add(offset), frame.thread_id);
                ++scanned;
                if (byte == 0U) {
                    status = "complete";
                    break;
                }
                if (byte == '\\' || byte == '"') {
                    message += '\\';
                    message += static_cast<char>(byte);
                } else if (byte >= 0x20U && byte <= 0x7eU) {
                    message += static_cast<char>(byte);
                } else {
                    message += "\\x";
                    message += kHex[byte >> 4U];
                    message += kHex[byte & 0xfU];
                }
            }
        } catch (const memory::MemoryFault& error) {
            status = "memory_fault";
            read_error = "\n    read_error=" + std::string(memory::ToString(error.Reason())) +
                         " at " + Hex32(error.Address().Value());
        } catch (const std::overflow_error&) {
            status = "address_overflow";
        }
    }
    return JniCallFailure("FatalError", frame,
        "JNI FatalError:\n    message=\"" + message + "\"\n    message_pointer=" +
        Hex32(pointer.Value()) + "\n    message_status=" + status +
        "\n    scanned_bytes=" + std::to_string(scanned) + read_error);
}

void BindDirectBufferSlots(JniGuestCallDispatcher& dispatcher,
                           JniGuestBindingContext& context) {
    auto& nio = *context.nio;
    auto& environment = context.environment;
    auto& classes = context.classes;
    auto& objects = context.objects;
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewDirectByteBuffer"),
        [&environment, &classes, &objects, &nio](
            const JniGuestCallFrame& frame) -> JniGuestCallResult {
            const auto capacity = static_cast<std::int64_t>(
                static_cast<std::uint64_t>(frame.registers[3]) << 32U |
                frame.registers[2]);
            if (capacity < 0 || capacity > std::numeric_limits<std::int32_t>::max())
                throw JniGuestBindingError("NewDirectByteBuffer capacity is invalid");
            const auto java_class = classes.FindClass("java/nio/DirectByteBuffer");
            if (!java_class.has_value())
                throw JniGuestBindingError("java/nio/DirectByteBuffer is not declared");
            const auto object = objects.Allocate(*java_class);
            JniReference reference;
            try {
                nio.WrapDirect(object, memory::GuestAddress(frame.registers[1]),
                               static_cast<std::int32_t>(capacity));
                reference = environment.PublishLocalObject(frame.thread_id, object);
                return Reference(reference);
            } catch (...) {
                nio.Sweep(object);
                objects.Forget(object);
                throw;
            }
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetDirectBufferAddress"),
        [&environment, &nio](const JniGuestCallFrame& frame) {
            const auto object = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!object.has_value() || !nio.Contains(*object)) return Word(0U);
            const auto state = nio.Snapshot(*object);
            return Word(state.direct_address.has_value()
                            ? state.direct_address->Value() : 0U);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetDirectBufferCapacity"),
        [&environment, &nio](const JniGuestCallFrame& frame) -> JniGuestCallResult {
            const auto object = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!object.has_value() || !nio.Contains(*object)) {
                return {JniGuestReturnWidth::double_word,
                        {UINT32_MAX, UINT32_MAX}};
            }
            // Dalvik Jni.cpp:2758-2759 reads Buffer.capacity even for heap buffers.
            const auto capacity = static_cast<std::uint64_t>(
                nio.Snapshot(*object).capacity);
            return {JniGuestReturnWidth::double_word,
                    {static_cast<std::uint32_t>(capacity),
                     static_cast<std::uint32_t>(capacity >> 32U)}};
        });
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

JniJavaVmResult AttachGuestThread(JniGuestCallDispatcher& dispatcher, JniJavaVm& vm, memory::AddressSpace& space,
                                  const JniGuestCallFrame& frame, bool daemon) {
    ValidateOutput(space, memory::GuestAddress{frame.registers[1]}, frame.thread_id);
    // Dalvik Jni.cpp:2785-2790: repeated attach ignores all arguments.
    const auto existing = vm.GetEnv(frame.thread_id, kJniVersion1_6);
    if (existing.status == JniStatus::ok) return existing;
    auto version = kJniVersion1_2;  // Dalvik Jni.cpp:2815-2820: NULL arguments use 1.2.
    std::optional<std::string> name;
    if (frame.registers[2] != 0U) {
        const auto args = memory::GuestAddress{frame.registers[2]};
        version = std::bit_cast<JniInt>(ReadGuest32(space, args, frame.thread_id));
        // Dalvik Jni.cpp:2822-2826, 288-296: attach excludes JNI 1.1.
        if (version != kJniVersion1_2 && version != kJniVersion1_4 && version != kJniVersion1_6)
            return {JniStatus::version, JniEnvHandle{}};
        const auto name_pointer = ReadGuest32(space, args.Add(4U), frame.thread_id);
        const auto group = ReadGuest32(space, args.Add(8U), frame.thread_id);
        // Dalvik Jni.cpp:2831-2842 resolves the supplied group; no silent fallback.
        if (group != 0U) {
            dispatcher.RecordUnimplemented("runtime.jni.attach_thread_group", frame.link_register);
            throw JniGuestBindingError("non-null JavaVM attach ThreadGroup is not implemented");
        }
        if (name_pointer != 0U) {
            const auto encoded =
                ReadGuestCString(space, memory::GuestAddress{name_pointer}, frame.thread_id, "thread name");
            const std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
            // Jni.cpp:2831-2842 -> Thread.cpp:1868-1871 creates Java UTF-16 from MUTF-8.
            // Preserve code units (including unpaired surrogates), not canonical UTF-8.
            static_cast<void>(DecodeJniModifiedUtf8(bytes));
            name = encoded;
        }
    }
    return daemon ? vm.AttachCurrentThreadAsDaemon(frame.thread_id, version, 16, name)
                  : vm.AttachCurrentThread(frame.thread_id, version, 16, name);
}

}  // namespace

void BindJniGuestCoreSlots(JniGuestCallDispatcher& dispatcher,
                           JniEnvironment& environment,
                           JniClassRegistry& classes,
                           JniStringStore& strings,
                           JniPrimitiveArrayStore&,
                           memory::AddressSpace& address_space,
                           const JniGuestStringLimits limits,
                           JniGuestStringObserver observer, JniFieldStore* fields) {
    ValidateJniGuestStringLimits(limits);
    dispatcher.BindEnvironment(
        EnvironmentSlot("DefineClass"), [&environment, &dispatcher](const JniGuestCallFrame& frame) {
            environment.RequireAllowed(frame.thread_id, "DefineClass");
            // Dalvik Jni.cpp:1198: unsupported DefineClass returns NULL without an exception.
            dispatcher.RecordWarning("runtime.jni.DefineClass.dalvik_unsupported", frame.link_register);
            return Reference(JniReference{});
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetSuperclass"), [&environment, &classes](const JniGuestCallFrame& frame) {
            // Dalvik Jni.cpp:1264-1279 returns the stored superclass (NULL for Object).
            const auto type = environment.ResolveObjectForHle(frame.thread_id, JniReference{frame.registers[1]});
            if (!type) throw JniGuestBindingError("GetSuperclass requires a class");
            const auto parent = classes.GetSuperclass(*type);
            return Reference(parent ? environment.PublishLocalObject(frame.thread_id, *parent) : JniReference{});
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("IsAssignableFrom"), [&environment, &classes](const JniGuestCallFrame& frame) {
            // Dalvik Jni.cpp:1275-1280: clazz1 is the source, clazz2 the destination.
            const auto source = environment.ResolveObjectForHle(frame.thread_id, JniReference{frame.registers[1]});
            const auto target = environment.ResolveObjectForHle(frame.thread_id, JniReference{frame.registers[2]});
            if (!source || !target) throw JniGuestBindingError("IsAssignableFrom requires classes");
            return Int(classes.IsAssignableFrom(*target, *source) ? 1 : 0);
        });
    dispatcher.BindEnvironment(EnvironmentSlot("GetObjectRefType"), [&environment](const JniGuestCallFrame& frame) {
        // Dalvik Jni.cpp:662-679, 2683-2685 validates reference-table membership without dereferencing handles.
        return Int(environment.GetObjectRefType(frame.thread_id, JniReference{frame.registers[1]}));
    });
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
        EnvironmentSlot("ThrowNew"),
        [&environment, &classes,
         &address_space](const JniGuestCallFrame& frame) {
            const auto java_class = environment.ResolveObjectForHle(
                frame.thread_id, JniReference{frame.registers[1]});
            if (!java_class.has_value()) {
                throw JniGuestBindingError(
                    "ThrowNew requires a valid exception class reference");
            }
            static_cast<void>(
                classes.IsAssignableFrom(*java_class, *java_class));
            const auto message = ReadGuestCString(
                address_space, memory::GuestAddress{frame.registers[2]},
                frame.thread_id, "exception message");
            const std::vector<std::uint8_t> encoded(
                message.begin(), message.end());
            static_cast<void>(DecodeJniModifiedUtf8(encoded));
            environment.ThrowNew(frame.thread_id, *java_class, message);
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
        EnvironmentSlot("ExceptionDescribe"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.ExceptionDescribe(frame.thread_id);
            return JniGuestCallResult{};
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("FatalError"),
        [&address_space](const JniGuestCallFrame& frame) -> JniGuestCallResult {
            throw JniGuestBindingError(FatalErrorDiagnostic(address_space, frame));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("PushLocalFrame"), [&environment, &classes](const JniGuestCallFrame& frame) {
            try {
                environment.PushLocalFrame(frame.thread_id, Capacity(frame.registers[1], "PushLocalFrame"));
            } catch (const JniReferenceError& error) {
                if (error.Reason() != JniReferenceErrorReason::capacity_exceeded) throw;
                // Dalvik Jni.cpp:1418-1428.
                environment.ExceptionClear(frame.thread_id);
                jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id, "java/lang/OutOfMemoryError",
                                                 "out of stack in JNI PushLocalFrame");
                return Int(static_cast<JniInt>(JniStatus::error));
            }
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("PopLocalFrame"), [&environment, &classes](const JniGuestCallFrame& frame) {
            try {
                return Reference(environment.PopLocalFrame(frame.thread_id, JniReference{frame.registers[1]}));
            } catch (const JniReferenceError& error) {
                if (error.Reason() != JniReferenceErrorReason::frame_underflow) throw;
                // Dalvik Jni.cpp:1435-1445: decode result, replace pending with
                // RuntimeException, and return a local result even on underflow.
                const auto result = environment.ResolveObjectForHle(frame.thread_id, JniReference{frame.registers[1]});
                environment.ExceptionClear(frame.thread_id);
                jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id, "java/lang/RuntimeException",
                                                 "too many PopLocalFrame calls");
                return Reference(result ? environment.PublishLocalObject(frame.thread_id, *result) : JniReference{});
            }
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
        EnvironmentSlot("EnsureLocalCapacity"), [&environment, &classes](const JniGuestCallFrame& frame) {
            try {
                environment.EnsureLocalCapacity(frame.thread_id, Capacity(frame.registers[1], "EnsureLocalCapacity"));
            } catch (const JniReferenceError& error) {
                if (error.Reason() != JniReferenceErrorReason::capacity_exceeded) throw;
                // Dalvik Jni.cpp:1485-1491.
                jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id, "java/lang/OutOfMemoryError",
                                                 "can't ensure local reference capacity");
                return Int(static_cast<JniInt>(JniStatus::error));
            }
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
        EnvironmentSlot("MonitorEnter"),
        [&environment](const JniGuestCallFrame& frame) {
            environment.MonitorEnter(
                frame.thread_id, JniReference{frame.registers[1]});
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("MonitorExit"), [&environment, &classes](const JniGuestCallFrame& frame) {
            try {
                environment.MonitorExit(frame.thread_id, JniReference{frame.registers[1]});
            } catch (const JniMonitorError& error) {
                if (error.Reason() != JniMonitorErrorReason::not_owner) throw;
                // Dalvik Jni.cpp:2534-2549: failed exit replaces any pending exception.
                environment.ExceptionClear(frame.thread_id);
                jni_guest_detail::ThrowJavaError(environment, classes, frame.thread_id,
                                                 "java/lang/IllegalMonitorStateException", error.what());
                return Int(static_cast<JniInt>(JniStatus::error));
            }
            return Int(static_cast<JniInt>(JniStatus::ok));
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("GetStaticMethodID"),
        [&environment, &classes, fields,
         &address_space](const JniGuestCallFrame& frame) {
            return jni_guest_detail::LookupMethod(environment, classes, address_space, frame, true, fields);
        });
    dispatcher.BindEnvironment(
        EnvironmentSlot("NewStringUTF"),
        [&environment, &strings, &address_space, limits,
         observer = std::move(observer)](const JniGuestCallFrame& frame) {
            if (frame.registers[1] == 0U) {
                return Reference(JniReference{});
            }
            JniObjectIdentity identity{};
            bool created{};
            try {
                const auto encoded = ReadGuestModifiedUtf8(address_space,
                    memory::GuestAddress{frame.registers[1]}, frame.thread_id,
                    limits.maximum_modified_utf8_bytes);
                const auto chars = DecodeJniModifiedUtf8(encoded);
                if (chars.size() > limits.maximum_utf16_code_units)
                    throw JniGuestBindingError("JNI string UTF-16 resource budget exceeded: code_units=" +
                        std::to_string(chars.size()) + " budget=" + std::to_string(limits.maximum_utf16_code_units));
                identity = strings.Create(chars);
                created = true;
                const auto reference = environment.PublishLocalObject(frame.thread_id, identity);
                if (observer) {
                    try { observer(frame, encoded.size(), chars.size()); }
                    catch (...) { /* diagnostics must not affect the result */ }
                }
                return Reference(reference);
            } catch (const std::bad_alloc&) {
                if (created) strings.Delete(identity);
                throw JniGuestBindingError(JniCallFailure("NewStringUTF", frame, "JNI string allocation failed"));
            } catch (const JniModifiedUtf8Error& error) {
                throw JniGuestBindingError(JniCallFailure("NewStringUTF", frame,
                    "invalid Modified UTF-8 at byte=" + std::to_string(error.Offset()) + ": " + error.what()));
            } catch (const memory::MemoryFault& error) {
                throw JniGuestBindingError(JniCallFailure("NewStringUTF", frame,
                    "JNI string input memory fault: " + std::string(memory::ToString(error.Reason())) + ": " + error.what()));
            } catch (const std::overflow_error& error) {
                throw JniGuestBindingError(JniCallFailure("NewStringUTF", frame,
                    "JNI string guest address overflow: " + std::string(error.what())));
            } catch (const std::exception& error) {
                if (created) strings.Delete(identity);
                throw JniGuestBindingError(
                    JniCallFailure("NewStringUTF", frame, error.what()));
            }
        });
}

void BindJniGuestJavaVmSlots(JniGuestCallDispatcher& dispatcher,
                             JniJavaVm& java_vm,
                             memory::AddressSpace& address_space) {
    dispatcher.BindJavaVm(JavaVmSlot("AttachCurrentThread"),
                          [&dispatcher, &java_vm, &address_space](const JniGuestCallFrame& frame) {
                              const auto result = AttachGuestThread(dispatcher, java_vm, address_space, frame, false);
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
    dispatcher.BindJavaVm(JavaVmSlot("AttachCurrentThreadAsDaemon"),
                          [&dispatcher, &java_vm, &address_space](const JniGuestCallFrame& frame) {
                              const auto result = AttachGuestThread(dispatcher, java_vm, address_space, frame, true);
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
                const auto name = ReadGuestCString(
                    address_space, memory::GuestAddress{word(offset)},
                    frame.thread_id, "native method name");
                const auto descriptor = ReadGuestCString(
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
    ValidateJniGuestStringLimits(context.string_limits);
    context.environment.SetPendingCallDiagnostics(
        [&classes = context.classes, &objects = context.objects](const JniThrowableMetadata& pending) {
            if (pending.exception_class.value) return classes.ClassName(pending.exception_class);
            try {
                return classes.ClassName(objects.ClassOf(pending.throwable));
            } catch (const JniGuestBindingError&) {
                return std::string{"unresolved-class"};
            }
        },
        [&dispatcher, logger = context.logger](const JniPendingCallWarning& w) {
            dispatcher.RecordWarning("runtime.jni.pending." + std::string(JniSlotName(w.slot)) + "." +
                                         std::to_string(w.thread) + "." + w.exception_class,
                                     w.guest_lr);
            if (logger)
                logger->Write(core::LogLevel::warn, "runtime.jni.pending", "JNI call with pending exception allowed",
                              {.guest_thread = w.thread},
                              {{"slot", std::string(JniSlotName(w.slot))},
                               {"exception_class", w.exception_class},
                               {"guest_lr", core::GuestAddress{w.guest_lr}},
                               {"guest_thread", w.thread}},
                              {.mode = core::RateLimitMode::none});
        });
    dispatcher.SetEnvironmentCallHooks(
        [&env = context.environment](const JniGuestCallFrame& f) {
            env.EnterGuestCall(f.thread_id, JniSlot{f.thunk.slot}, f.link_register);
        },
        [&env = context.environment](const JniGuestCallFrame& f) { env.LeaveGuestCall(f.thread_id); });
    dispatcher.BindEnvironment(EnvironmentSlot("FromReflectedMethod"),
        [&env = context.environment](const JniGuestCallFrame& frame) {
            return Word(env.FromReflectedMethod(frame.thread_id, JniReference{frame.registers[1]}).Value());
        });
    dispatcher.BindEnvironment(EnvironmentSlot("ToReflectedMethod"),
        [&env = context.environment](const JniGuestCallFrame& frame) {
            return Reference(env.ToReflectedMethod(frame.thread_id, JniReference{frame.registers[1]},
                JniMethodId{frame.registers[2]}, frame.registers[3] != 0U));
        });
    dispatcher.BindEnvironment(EnvironmentSlot("FromReflectedField"),
        [&env = context.environment](const JniGuestCallFrame& frame) {
            return Word(env.FromReflectedField(frame.thread_id, JniReference{frame.registers[1]}).Value());
        });
    dispatcher.BindEnvironment(EnvironmentSlot("ToReflectedField"),
        [&env = context.environment](const JniGuestCallFrame& frame) {
            return Reference(env.ToReflectedField(frame.thread_id, JniReference{frame.registers[1]},
                JniFieldId{frame.registers[2]}, frame.registers[3] != 0U));
        });
    BindJniGuestCoreSlots(dispatcher, context.environment, context.classes,
                         context.strings, context.arrays,
                         context.address_space, context.string_limits, context.string_observer, &context.fields);
    BindJniGuestClassAndInstanceSlots(
        dispatcher, context.environment, context.classes,
        context.invocations, context.address_space, &context.objects, &context.fields);
    BindJniGuestStaticCallSlots(
        dispatcher, context.environment, context.classes,
        context.invocations, context.address_space);
    BindJniGuestStaticFieldSlots(
        dispatcher, context.environment, context.classes, context.fields,
        context.address_space);
    BindJniGuestInstanceFieldSlots(
        dispatcher, context.environment, context.classes, context.fields,
        context.objects, context.address_space);
    BindJniGuestArraySlots(
        dispatcher, context.environment, context.classes, context.strings,
        context.arrays, context.objects, context.address_space);
    BindJniGuestStringSlots(dispatcher, context.environment, context.classes, context.strings, context.address_space,
                            context.string_limits);
    if (context.natives != nullptr) {
        BindJniGuestNativeRegistrationSlots(
            dispatcher, context.environment, context.classes,
            *context.natives, context.address_space);
    }
    if (context.nio != nullptr) BindDirectBufferSlots(dispatcher, context);
    BindJniGuestJavaVmSlots(dispatcher, context.java_vm,
                            context.address_space);
}

}  // namespace ogplay::runtime
