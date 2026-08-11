#pragma once

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniEnvironment;
class JniGuestCallDispatcher;
class JniStringStore;

void BindJniGuestModifiedUtf8Slots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space);

void BindJniGuestUtf16Slots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniStringStore& strings, memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
