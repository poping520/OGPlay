#pragma once

#include "jni_guest_memory.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"

namespace ogplay::runtime::jni_guest_detail {

// Expected Dalvik Java error paths share ThrowableFactory publication. Invalid
// handles/descriptors and unrelated host faults keep their strict boundaries.
inline void ThrowJavaError(JniEnvironment& environment, JniClassRegistry& classes, std::uint64_t thread,
                           const std::string& type, const std::string& message) {
    auto error_class = classes.ResolveClass(type);
    if (!error_class) {
        // Standalone JNI owns host exception metadata, without a VM backend.
        error_class = classes.RegisterClass(JniClassDeclaration{.name = type});
    }
    environment.ThrowNew(thread, *error_class, message);
}

inline void ThrowLookupError(JniEnvironment& environment, JniClassRegistry& classes, std::uint64_t thread,
                             const std::string& type, const std::string& message) {
    ThrowJavaError(environment, classes, thread, type, message);
}

inline JniGuestCallResult LookupMethod(
    JniEnvironment& environment, JniClassRegistry& classes,
    memory::AddressSpace& memory, const JniGuestCallFrame& frame, bool is_static, JniFieldStore* fields) {
    const auto owner = environment.ResolveObjectForHle(frame.thread_id,
        JniReference{frame.registers[1]});
    if (!owner) throw JniGuestBindingError("JNI method lookup requires a valid class reference");
    if (fields && !fields->EnsureClassInitialized(*owner, frame.thread_id))
        return {JniGuestReturnWidth::word, {0U, 0U}};
    const auto name = ReadGuestCString(memory, memory::GuestAddress{frame.registers[2]},
                                      frame.thread_id, "method name");
    const auto descriptor = ReadGuestCString(memory, memory::GuestAddress{frame.registers[3]},
                                            frame.thread_id, "method descriptor");
    const auto method = classes.GetMethodId(*owner, name, descriptor, is_static);
    if (!method) {
        ThrowLookupError(environment, classes, frame.thread_id,
            "java/lang/NoSuchMethodError", classes.ClassName(*owner) + "." + name + descriptor);
    }
    return {JniGuestReturnWidth::word, {method ? method->Value() : 0U, 0U}};
}

} // namespace ogplay::runtime::jni_guest_detail
