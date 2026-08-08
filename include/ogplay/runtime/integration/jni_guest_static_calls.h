#pragma once

#include "ogplay/runtime/integration/jni_guest_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniClassRegistry;
class JniEnvironment;
class JniInvocationEngine;

void BindJniGuestStaticCallSlots(JniGuestCallDispatcher& dispatcher,
                                 JniEnvironment& environment,
                                 JniClassRegistry& classes,
                                 JniInvocationEngine& invocations,
                                 memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
