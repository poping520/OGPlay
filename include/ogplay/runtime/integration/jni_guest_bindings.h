#pragma once

#include "ogplay/runtime/integration/jni_guest_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniEnvironment;
class JniClassRegistry;
class JniJavaVm;

class JniGuestBindingError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void BindJniGuestCoreSlots(JniGuestCallDispatcher& dispatcher,
                           JniEnvironment& environment,
                           JniClassRegistry& classes,
                           JniJavaVm& java_vm,
                           memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
