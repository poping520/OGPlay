#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_environment.h"

TEST_CASE("JNI environment closes common reference operations") {
    ogplay::runtime::JniEnvironment environment({8, 4, 4});
    environment.AttachThread(11, 4);
    CHECK(environment.IsThreadAttached(11));
    CHECK(environment.GetVersion(11) == ogplay::runtime::kJniVersion1_6);

    environment.PushLocalFrame(11, 3);
    const auto object = environment.ExceptionOccurred(11);
    CHECK(object.IsNull());
    const auto null_local = environment.NewLocalRef(11, object);
    CHECK(null_local.IsNull());
    const auto null_global = environment.NewGlobalRef(11, object);
    CHECK(null_global.IsNull());
    CHECK(environment.IsSameObject(11, object, null_global));
    CHECK(environment.PopLocalFrame(11).IsNull());
    environment.EnsureLocalCapacity(11, 4);
    environment.DetachThread(11);
    CHECK_FALSE(environment.IsThreadAttached(11));
    CHECK_THROWS_AS(static_cast<void>(environment.GetVersion(11)),
                    ogplay::runtime::JniExceptionError);
}

TEST_CASE("JNI environment exposes pending exceptions and enforces the gate") {
    ogplay::runtime::JniEnvironment environment({8, 4, 4});
    environment.AttachThread(7, 4);

    CHECK_FALSE(environment.ExceptionCheck(7));
    const auto throwable = environment.PublishLocalObject(
        7, {ogplay::runtime::JniObjectDomain::host, 9001});
    environment.Throw(7, throwable);
    CHECK(environment.ExceptionCheck(7));
    const auto occurred = environment.ExceptionOccurred(7);
    CHECK_FALSE(occurred.IsNull());
    CHECK(occurred != throwable);
    CHECK_THROWS_AS(static_cast<void>(environment.GetVersion(7)),
                    ogplay::runtime::JniExceptionError);
    environment.ExceptionClear(7);
    CHECK(environment.IsSameObject(7, throwable, occurred));
    environment.DeleteLocalRef(7, occurred);
    CHECK(environment.GetVersion(7) == ogplay::runtime::kJniVersion1_6);

    CHECK_THROWS_AS(environment.Throw(7, ogplay::runtime::JniReference{}),
                    ogplay::runtime::JniExceptionError);

    environment.DetachThread(7);
}

TEST_CASE("JNI environment rolls back a failed thread attachment") {
    ogplay::runtime::JniEnvironment environment({2, 2, 2});
    CHECK_THROWS_AS(environment.AttachThread(9, 3),
                    ogplay::runtime::JniReferenceError);
    CHECK_FALSE(environment.IsThreadAttached(9));
    environment.AttachThread(9, 2);
    CHECK_THROWS_AS(environment.AttachThread(9, 2),
                    ogplay::runtime::JniExceptionError);
    environment.DetachThread(9);
}

TEST_CASE("JNI HLE object resolution is checked and preserves identity") {
    ogplay::runtime::JniEnvironment environment({8, 4, 4});
    environment.AttachThread(31, 4);
    environment.AttachThread(32, 4);
    const ogplay::runtime::JniObjectIdentity object{
        ogplay::runtime::JniObjectDomain::dex_vm, 0x1234};
    const auto reference = environment.PublishLocalObject(31, object);

    CHECK(environment.ResolveObjectForHle(31, reference) == object);
    CHECK_FALSE(environment
                    .ResolveObjectForHle(31, ogplay::runtime::JniReference{})
                    .has_value());
    CHECK_THROWS_AS(
        static_cast<void>(environment.ResolveObjectForHle(32, reference)),
        ogplay::runtime::JniReferenceError);

    const auto throwable = environment.PublishLocalObject(
        31, {ogplay::runtime::JniObjectDomain::host, 0x55});
    environment.Throw(31, throwable);
    CHECK_THROWS_AS(
        static_cast<void>(environment.ResolveObjectForHle(31, reference)),
        ogplay::runtime::JniExceptionError);
    environment.ExceptionClear(31);
    environment.DetachThread(32);
    environment.DetachThread(31);
}
