#pragma once

#include "ogplay/runtime/integration/jni_guest_dispatch.h"

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

void BindJniGuestSlots(JniGuestCallDispatcher& dispatcher,
                       JniGuestBindingContext& context);

}  // namespace ogplay::runtime
