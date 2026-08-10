#pragma once

#include <memory>

#include "ogplay/runtime/integration/jni_guest_dispatch.h"

namespace ogplay::memory {
class AddressSpace;
}

namespace ogplay::runtime {

class JniClassRegistry;
class JniEnvironment;
class JniInvocationEngine;

class JniGuestObjectRegistry final {
public:
    explicit JniGuestObjectRegistry(const JniClassRegistry& classes);
    ~JniGuestObjectRegistry();
    JniGuestObjectRegistry(const JniGuestObjectRegistry&) = delete;
    JniGuestObjectRegistry& operator=(const JniGuestObjectRegistry&) = delete;
    JniGuestObjectRegistry(JniGuestObjectRegistry&&) noexcept;
    JniGuestObjectRegistry& operator=(JniGuestObjectRegistry&&) noexcept;

    [[nodiscard]] JniObjectIdentity Allocate(JniObjectIdentity java_class);
    void Register(JniObjectIdentity object, JniObjectIdentity java_class);
    void Forget(JniObjectIdentity object);
    [[nodiscard]] JniObjectIdentity ClassOf(JniObjectIdentity object) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

void BindJniGuestStaticCallSlots(JniGuestCallDispatcher& dispatcher,
                                 JniEnvironment& environment,
                                 JniClassRegistry& classes,
                                 JniInvocationEngine& invocations,
                                 memory::AddressSpace& address_space);

void BindJniGuestClassAndInstanceSlots(
    JniGuestCallDispatcher& dispatcher, JniEnvironment& environment,
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    memory::AddressSpace& address_space,
    JniGuestObjectRegistry* objects = nullptr);

}  // namespace ogplay::runtime
