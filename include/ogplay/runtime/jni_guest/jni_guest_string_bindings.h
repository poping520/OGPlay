#pragma once

#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniEnvironment;
class JniGuestCallDispatcher;
class JniStringStore;

void BindJniGuestStringSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space,
    JniGuestStringLimits limits = {});

}  // namespace ogplay::runtime
