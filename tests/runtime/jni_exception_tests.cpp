#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_exception.h"

TEST_CASE("JNI pending exceptions are isolated per attached guest thread") {
    ogplay::runtime::JniExceptionState exceptions;
    exceptions.AttachThread(11);
    exceptions.AttachThread(22);
    CHECK(exceptions.IsThreadAttached(11));
    CHECK_FALSE(exceptions.HasPending(11));
    CHECK_THROWS_AS(exceptions.AttachThread(11),
                    ogplay::runtime::JniExceptionError);
    CHECK_THROWS_AS(exceptions.AttachThread(0),
                    ogplay::runtime::JniExceptionError);

    const ogplay::runtime::JniObjectIdentity throwable{
        ogplay::runtime::JniObjectDomain::host, 101};
    exceptions.Throw(11, throwable);
    CHECK(exceptions.HasPending(11));
    CHECK(exceptions.Occurred(11) == throwable);
    CHECK_FALSE(exceptions.HasPending(22));
    CHECK_NOTHROW(exceptions.Throw(11, throwable));
    CHECK(exceptions.Occurred(11) == throwable);
    const ogplay::runtime::JniObjectIdentity replacement{ogplay::runtime::JniObjectDomain::host, 102};
    exceptions.Throw(11, replacement);
    CHECK(exceptions.Occurred(11) == replacement);
    CHECK_THROWS_AS(exceptions.Throw(
                        22, {ogplay::runtime::JniObjectDomain::host, 0}),
                    ogplay::runtime::JniExceptionError);

    exceptions.Clear(11);
    CHECK_FALSE(exceptions.Occurred(11).has_value());
    exceptions.DetachThread(11);
    CHECK_FALSE(exceptions.IsThreadAttached(11));
    CHECK_THROWS_AS(static_cast<void>(exceptions.HasPending(11)),
                    ogplay::runtime::JniExceptionError);
}

TEST_CASE("Dalvik non-CheckJNI pending permits ordinary inspection cleanup and release calls") {
    ogplay::runtime::JniExceptionState exceptions;
    exceptions.AttachThread(7);
    const auto find_class = ogplay::runtime::FindJniSlot("FindClass").value();
    const auto exception_check =
        ogplay::runtime::FindJniSlot("ExceptionCheck").value();
    const auto exception_clear =
        ogplay::runtime::FindJniSlot("ExceptionClear").value();
    const auto delete_local =
        ogplay::runtime::FindJniSlot("DeleteLocalRef").value();
    const auto release_utf =
        ogplay::runtime::FindJniSlot("ReleaseStringUTFChars").value();

    CHECK(exceptions.IsCallAllowed(7, find_class));
    exceptions.Throw(
        7, {ogplay::runtime::JniObjectDomain::dex_vm, 88});
    CHECK(exceptions.IsCallAllowed(7, find_class));
    CHECK(exceptions.IsCallAllowed(7, exception_check));
    CHECK(exceptions.IsCallAllowed(7, exception_clear));
    CHECK(exceptions.IsCallAllowed(7, delete_local));
    CHECK(exceptions.IsCallAllowed(7, release_utf));
    CHECK_NOTHROW(exceptions.RequireCallAllowed(7, find_class));
    CHECK(exceptions.Occurred(7)->value == 88);
    CHECK_NOTHROW(exceptions.RequireCallAllowed(7, exception_check));
    exceptions.Clear(7);
    CHECK_NOTHROW(exceptions.RequireCallAllowed(7, find_class));
}
