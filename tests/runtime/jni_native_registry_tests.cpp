#include <array>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object.h"

TEST_CASE("RegisterNatives resolves overloads by class name and descriptor") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto java_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const std::array methods{
        ogplay::runtime::JniNativeMethod{
            "step", "(I)I", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "step", "(J)J", ogplay::memory::GuestAddress{0x71002000}},
    };
    registry.RegisterNatives(java_class, methods);
    CHECK(registry.Resolve(java_class, "step", "(I)I") ==
          ogplay::memory::GuestAddress{0x71001000});
    CHECK(registry.Resolve(java_class, "step", "(J)J") ==
          ogplay::memory::GuestAddress{0x71002000});
    CHECK_FALSE(registry.Resolve(java_class, "step", "()V").has_value());
    const auto declarations = registry.Declarations(java_class);
    REQUIRE(declarations.size() == 2);
    CHECK(declarations[0].descriptor == "(I)I");
    CHECK(declarations[1].descriptor == "(J)J");

    // Identical registration is deterministic and idempotent.
    registry.RegisterNatives(java_class, methods);
    CHECK(registry.Declarations(java_class).size() == 2);
}

TEST_CASE("RegisterNatives validates a whole batch before mutation") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto java_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const std::array malformed{
        ogplay::runtime::JniNativeMethod{
            "valid", "()V", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "broken", "(V)V", ogplay::memory::GuestAddress{0x71002000}},
    };
    CHECK_THROWS_AS(registry.RegisterNatives(java_class, malformed),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Declarations(java_class).empty());

    const std::array duplicate{
        ogplay::runtime::JniNativeMethod{
            "same", "()V", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "same", "()V", ogplay::memory::GuestAddress{0x71001000}},
    };
    CHECK_THROWS_AS(registry.RegisterNatives(java_class, duplicate),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Declarations(java_class).empty());
}

TEST_CASE("RegisterNatives rejects target conflicts and unregisters one class") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto first_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const ogplay::runtime::JniObjectIdentity second_class{
        ogplay::runtime::JniObjectDomain::dex_vm, 1};
    const std::array first{ogplay::runtime::JniNativeMethod{
        "run", "()V", ogplay::memory::GuestAddress{0x71001000}}};
    const std::array conflict{ogplay::runtime::JniNativeMethod{
        "run", "()V", ogplay::memory::GuestAddress{0x71002000}}};
    registry.RegisterNatives(first_class, first);
    registry.RegisterNatives(second_class, conflict);
    CHECK_THROWS_AS(registry.RegisterNatives(first_class, conflict),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Resolve(first_class, "run", "()V") == first[0].target);
    CHECK(registry.UnregisterNatives(first_class) == 1);
    CHECK_FALSE(registry.Resolve(first_class, "run", "()V").has_value());
    CHECK(registry.Resolve(second_class, "run", "()V") ==
          conflict[0].target);
    CHECK(registry.UnregisterNatives(first_class) == 0);
}
