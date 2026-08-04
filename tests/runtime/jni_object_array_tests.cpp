#include <optional>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_environment.h"
#include "ogplay/runtime/jni_object.h"
#include "ogplay/runtime/jni_object_array.h"

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
