#pragma once

#include "ogplay/runtime/integration/jni_guest_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniClassRegistry;
class JniEnvironment;
class JniFieldStore;

void BindJniGuestStaticFieldSlots(JniGuestCallDispatcher& dispatcher,
                                  JniEnvironment& environment,
                                  JniClassRegistry& classes,
                                  JniFieldStore& fields,
                                  memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
