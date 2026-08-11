#pragma once

#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniClassRegistry;
class JniEnvironment;
class JniFieldStore;
class JniGuestObjectRegistry;

void BindJniGuestStaticFieldSlots(JniGuestCallDispatcher& dispatcher,
                                  JniEnvironment& environment,
                                  JniClassRegistry& classes,
                                  JniFieldStore& fields,
                                  memory::AddressSpace& address_space);

void BindJniGuestInstanceFieldSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniFieldStore& fields,
    JniGuestObjectRegistry& objects, memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
