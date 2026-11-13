#pragma once

#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniEnvironment;
class JniGuestCallDispatcher;
class JniStringBackend;

void BindJniGuestStringSlots(JniGuestCallDispatcher& dispatcher, JniEnvironment& environment, JniClassRegistry& classes,
                             JniStringBackend& strings, memory::AddressSpace& address_space,
                             JniGuestStringLimits limits = {}, core::Logger* logger = nullptr);

}  // namespace ogplay::runtime
