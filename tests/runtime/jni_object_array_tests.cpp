#include <optional>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_object_array.h"

namespace {

ogplay::runtime::JniClassDeclaration Class(
    const char* name, std::optional<std::string> superclass = std::nullopt) {
    return {name, std::move(superclass), {}, {}};
}

}  // namespace

TEST_CASE("JNI object arrays are null initialized and publishable") {
    ogplay::runtime::JniClassRegistry classes;
    const auto object = classes.RegisterClass(Class("java/lang/Object"));
    ogplay::runtime::JniObjectArrayStore arrays(classes);
    const auto array = arrays.New(object, 2);
    CHECK(arrays.Length(array) == 2);
    CHECK(arrays.ElementClass(array) == object);
    CHECK_FALSE(arrays.Get(array, 0).has_value());

    ogplay::runtime::JniEnvironment environment;
    environment.AttachThread(9);
    const auto reference = environment.PublishLocalObject(9, array);
    CHECK(environment.IsSameObject(9, reference, reference));
    environment.DetachThread(9);
    arrays.Delete(array);
}

TEST_CASE("JNI object arrays enforce Java assignability") {
    ogplay::runtime::JniClassRegistry classes;
    const auto object = classes.RegisterClass(Class("java/lang/Object"));
    const auto base = classes.RegisterClass(Class("example/Base", "java/lang/Object"));
    const auto child = classes.RegisterClass(Class("example/Child", "example/Base"));
    const auto other = classes.RegisterClass(Class("example/Other", "java/lang/Object"));
    ogplay::runtime::JniObjectArrayStore arrays(classes);
    const ogplay::runtime::JniObjectValue child_value{
        ogplay::runtime::AllocateJniHostObjectIdentity(), child};
    const auto array = arrays.New(base, 2, child_value);
    CHECK(arrays.Get(array, 1) == child_value);
    arrays.Set(array, 0, std::nullopt);
    CHECK_FALSE(arrays.Get(array, 0).has_value());

    const ogplay::runtime::JniObjectValue other_value{
        ogplay::runtime::AllocateJniHostObjectIdentity(), other};
    try {
        arrays.Set(array, 0, other_value);
        FAIL("incompatible object array element was accepted");
    } catch (const ogplay::runtime::JniObjectArrayError& error) {
        CHECK(error.Reason() ==
              ogplay::runtime::JniObjectArrayErrorReason::incompatible_element);
        CHECK(std::string(error.what()).find("example/Other -> example/Base") != std::string::npos);
    }
    CHECK(classes.IsAssignableFrom(object, child));
    arrays.Delete(array);
}

TEST_CASE("JNI object arrays reject invalid sizes indices and identities") {
    ogplay::runtime::JniClassRegistry classes;
    const auto object = classes.RegisterClass(Class("java/lang/Object"));
    ogplay::runtime::JniObjectArrayStore arrays(classes);
    CHECK_THROWS_AS(static_cast<void>(arrays.New(object, -1)),
                    ogplay::runtime::JniObjectArrayError);
    CHECK_THROWS_AS(
        static_cast<void>(arrays.New({ogplay::runtime::JniObjectDomain::host,
                                      999999}, 1)),
        ogplay::runtime::JniObjectArrayError);
    const auto array = arrays.New(object, 1);
    CHECK_THROWS_AS(static_cast<void>(arrays.Get(array, -1)),
                    ogplay::runtime::JniObjectArrayError);
    CHECK_THROWS_AS(static_cast<void>(arrays.Get(array, 1)),
                    ogplay::runtime::JniObjectArrayError);
    arrays.Delete(array);
    CHECK_THROWS_AS(static_cast<void>(arrays.Length(array)),
                    ogplay::runtime::JniObjectArrayError);
}

TEST_CASE("JNI object arrays delegate synthetic covariance outside storage locks") {
    using namespace ogplay::runtime;
    JniClassRegistry classes;
    const auto object = classes.RegisterClass(Class("java/lang/Object"));
    JniObjectArrayStore arrays(classes);
    const JniObjectIdentity bytes{JniObjectDomain::dex_vm, 10};
    const JniObjectIdentity integers{JniObjectDomain::dex_vm, 11};
    const JniObjectValue byte_array{AllocateJniHostObjectIdentity(), bytes};
    const auto objects = arrays.New(object, 1);
    CHECK_THROWS_AS(arrays.Set(objects, 0, byte_array), JniObjectArrayError);
    arrays.SetAssignability([&](auto target, auto source) {
        CHECK(arrays.Length(objects) == 1);  // Reentry would deadlock if Set retained its lock.
        return target == object || target == source;
    });
    arrays.Set(objects, 0, byte_array);
    CHECK(arrays.Get(objects, 0) == byte_array);
    const auto typed = arrays.New(bytes, 1, byte_array);
    CHECK_THROWS_AS(arrays.Set(typed, 0, JniObjectValue{AllocateJniHostObjectIdentity(), integers}),
                    JniObjectArrayError);
    CHECK(arrays.Get(typed, 0) == byte_array);
    arrays.SetAssignability({});
    CHECK_THROWS_AS(arrays.Set(objects, 0, byte_array), JniObjectArrayError);
}

TEST_CASE("JNI object arrays use authoritative VM results and strict host fallback") {
    using namespace ogplay::runtime;
    JniClassRegistry classes;
    const auto object = classes.RegisterClass(Class("java/lang/Object"));
    const auto contract = classes.RegisterClass(Class("example/Contract"));
    const auto implementation = classes.RegisterClass(Class("example/Implementation", "java/lang/Object"));
    const auto unrelated = classes.RegisterClass(Class("example/Unrelated", "java/lang/Object"));
    JniObjectArrayStore arrays(classes);
    const JniObjectValue value{AllocateJniHostObjectIdentity(), implementation};
    const auto typed = arrays.New(contract, 1);
    CHECK_THROWS_AS(arrays.Set(typed, 0, value), JniObjectArrayError);
    arrays.SetAssignability([&](auto target, auto source) -> std::optional<bool> {
        CHECK(arrays.Length(typed) == 1);  // Includes New's initial-value validation.
        if (target == contract) return source == implementation;
        if (target == implementation && source == implementation) return false;
        return std::nullopt;
    });
    arrays.Set(typed, 0, value);
    CHECK(arrays.Get(typed, 0) == value);
    CHECK(arrays.Get(arrays.New(contract, 1, value), 0) == value);
    CHECK_THROWS_AS(static_cast<void>(arrays.New(contract, 1, JniObjectValue{AllocateJniHostObjectIdentity(), unrelated})),
                    JniObjectArrayError);
    CHECK_THROWS_AS(arrays.Set(typed, 0, JniObjectValue{AllocateJniHostObjectIdentity(), unrelated}),
                    JniObjectArrayError);
    CHECK(arrays.Get(typed, 0) == value);
    // false is authoritative even when the registry would accept the identity.
    CHECK_THROWS_AS(static_cast<void>(arrays.New(implementation, 1, value)), JniObjectArrayError);
    CHECK(arrays.Get(arrays.New(object, 1, value), 0) == value);
    CHECK_THROWS_AS(static_cast<void>(arrays.New(unrelated, 1, value)), JniObjectArrayError);
    const JniObjectIdentity synthetic{JniObjectDomain::dex_vm, 10};
    const JniObjectValue nested{AllocateJniHostObjectIdentity(), synthetic};
    CHECK(arrays.Get(arrays.New(synthetic, 1, nested), 0) == nested);
    CHECK_THROWS_AS(static_cast<void>(arrays.New(object, 1, nested)), JniObjectArrayError);
    arrays.SetAssignability({});
    CHECK_THROWS_AS(arrays.Set(typed, 0, value), JniObjectArrayError);
}
