#pragma once

#include <string_view>

#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniEnvironment;
class JniClassRegistry;
class JniFieldStore;
class JniInvocationEngine;
class JniJavaVm;
class JniGuestObjectRegistry;
class JniNativeRegistry;
class JniPrimitiveArrayStore;
class JniStringStore;

struct JniGuestBindingContext final {
    JniEnvironment& environment;
    JniClassRegistry& classes;
    JniInvocationEngine& invocations;
    JniFieldStore& fields;
    JniStringStore& strings;
    JniPrimitiveArrayStore& arrays;
    JniJavaVm& java_vm;
    JniGuestObjectRegistry& objects;
    memory::AddressSpace& address_space;
    JniNativeRegistry* natives{};
};

class JniGuestBindingError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void BindJniGuestCoreSlots(JniGuestCallDispatcher& dispatcher,
                           JniEnvironment& environment,
                           JniClassRegistry& classes,
                           JniStringStore& strings,
                           JniPrimitiveArrayStore& arrays,
                           memory::AddressSpace& address_space);

void BindJniGuestJavaVmSlots(JniGuestCallDispatcher& dispatcher,
                             JniJavaVm& java_vm,
                             memory::AddressSpace& address_space);

void BindJniGuestNativeRegistrationSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniNativeRegistry& natives,
    memory::AddressSpace& address_space);

[[nodiscard]] A32GuestCallFrame ResolveJniRegisteredNativeCall(
    const JniNativeRegistry& natives, JniObjectIdentity java_class,
    std::string_view name, std::string_view descriptor,
    A32GuestCallFrame frame);

void BindJniGuestSlots(JniGuestCallDispatcher& dispatcher,
                       JniGuestBindingContext& context);

}  // namespace ogplay::runtime
