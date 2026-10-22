#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_environment.h"

TEST_CASE("DVM-242 JNI allocation hook owns publication and allows pending without skipping allocation") {
    using namespace ogplay::runtime;
    JniEnvironment env;
    env.AttachThread(9);
    const auto clazz = env.PublishLocalObject(9, {JniObjectDomain::host, 43});
    CHECK_THROWS_AS(static_cast<void>(env.AllocObject(9, clazz)), std::runtime_error);
    CHECK_THROWS_AS(static_cast<void>(env.AllocObject(9, JniReference{})), std::invalid_argument);
    int calls{};
    env.SetObjectAllocationHook([&](std::uint64_t thread, JniReference type) {
        ++calls; CHECK(thread == 9); CHECK(type == clazz);
        // Hook callbacks run outside the hook mutex.
        env.SetObjectAllocationHook({});
        return env.PublishLocalObject(thread, {JniObjectDomain::host, 44});
    });
    const auto object = env.AllocObject(9, clazz);
    CHECK(env.ResolveObjectForHle(9, object)->value == 44);
    CHECK(calls == 1);
    CHECK_THROWS_AS(static_cast<void>(env.AllocObject(9, clazz)), std::runtime_error);
    env.SetObjectAllocationHook([&](std::uint64_t, JniReference) { ++calls; return JniReference{}; });
    env.Throw(9, object);
    CHECK(env.AllocObject(9, clazz).IsNull());
    CHECK(calls == 2);
    CHECK(env.ExceptionCheck(9));
    CHECK_FALSE(env.PendingCallDiagnostics().empty());
    env.ExceptionClear(9); env.SetObjectAllocationHook({}); env.DetachThread(9);
}

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

TEST_CASE("JNI environment exposes pending exceptions and warns while permitting JNI calls") {
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
    CHECK(environment.GetVersion(7) == ogplay::runtime::kJniVersion1_6);
    const auto pending = environment.PendingExceptionMetadata(7)->throwable;
    environment.PushLocalFrame(7, 1);
    const auto nested = environment.ExceptionOccurred(7);
    CHECK_FALSE(nested.IsNull());
    CHECK(environment.PopLocalFrame(7).IsNull());
    CHECK(environment.PendingExceptionMetadata(7)->throwable == pending);
    const auto global = environment.NewGlobalRef(7, throwable);
    CHECK_FALSE(global.IsNull());
    CHECK_FALSE(environment.PendingCallDiagnostics().empty());
    environment.DeleteGlobalRef(7, global);
    CHECK(environment.PendingExceptionMetadata(7)->throwable == pending);
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
    CHECK(environment.ResolveObjectForHle(31, reference) == object);
    CHECK_FALSE(environment.PendingCallDiagnostics().empty());
    CHECK(environment.PendingExceptionMetadata(31)->throwable.value == 0x55);
    environment.ExceptionClear(31);
    environment.DetachThread(32);
    environment.DetachThread(31);
}

TEST_CASE("JNI HLE global object publication crosses attached threads") {
    ogplay::runtime::JniEnvironment environment({8, 4, 4});
    environment.AttachThread(41, 4);
    environment.AttachThread(42, 4);
    const ogplay::runtime::JniObjectIdentity object{
        ogplay::runtime::JniObjectDomain::host, 0x7788};
    const auto global = environment.PublishGlobalObjectForHle(41, object);
    CHECK(environment.ResolveObjectForHle(42, global) == object);
    environment.DeleteGlobalRef(42, global);
    CHECK_THROWS_AS(
        static_cast<void>(environment.ResolveObjectForHle(41, global)),
        ogplay::runtime::JniReferenceError);

    const auto throwable = environment.PublishLocalObject(
        41, {ogplay::runtime::JniObjectDomain::host, 0x99});
    environment.Throw(41, throwable);
    const auto pending_global = environment.PublishGlobalObjectForHle(41, object);
    CHECK(environment.ResolveObjectForHle(42, pending_global) == object);
    CHECK_FALSE(environment.PendingCallDiagnostics().empty());
    CHECK(environment.PendingExceptionMetadata(41)->throwable.value == 0x99);
    environment.DeleteGlobalRef(41, pending_global);
    environment.ExceptionClear(41);
    environment.DetachThread(42);
    environment.DetachThread(41);
}

TEST_CASE("JNI environment delegates every monitor operation to one backend") {
    ogplay::runtime::JniEnvironment environment({8, 4, 4});
    environment.AttachThread(51, 4);
    const ogplay::runtime::JniObjectIdentity object{
        ogplay::runtime::JniObjectDomain::dex_vm, 0x5151};
    const auto reference = environment.PublishLocalObject(51, object);
    std::size_t entered{};
    std::size_t exited{};
    std::size_t released{};
    std::size_t interrupted{};
    std::size_t shut_down{};
    environment.SetMonitorHooks({
        [&](const auto actual, const auto thread) {
            CHECK(actual == object);
            CHECK(thread == 51U);
            ++entered;
        },
        [&](const auto actual, const auto thread) {
            CHECK(actual == object);
            CHECK(thread == 51U);
            ++exited;
        },
        [&](const auto thread) {
            CHECK(thread == 51U);
            ++released;
            return std::size_t{2};
        },
        [&] {
            ++interrupted;
            return std::size_t{3};
        },
        [&] {
            ++shut_down;
            return std::size_t{4};
        },
        [&](const auto actual) {
            CHECK(actual == object);
            return ogplay::runtime::JniMonitorSnapshot{51, 2, 1, 0, false};
        }});

    environment.MonitorEnter(51, reference);
    environment.MonitorExit(51, reference);
    CHECK(environment.MonitorSnapshot(object).recursion == 2U);
    CHECK(environment.InterruptMonitorWaiters() == 3U);
    CHECK(environment.ShutdownMonitors() == 4U);
    environment.DetachThread(51);
    CHECK(entered == 1U);
    CHECK(exited == 1U);
    CHECK(released == 1U);
    CHECK(interrupted == 1U);
    CHECK(shut_down == 1U);
}

TEST_CASE("JNI pending metadata preserves ThrowNew class and message without clearing") {
    using namespace ogplay::runtime;
    JniEnvironment environment({8, 4, 4});
    environment.AttachThread(11, 4);
    CHECK_FALSE(environment.PendingExceptionMetadata(11).has_value());
    const JniObjectIdentity type{JniObjectDomain::host, 1234};
    environment.ThrowNew(11, type, "bad padding");
    const auto metadata = environment.PendingExceptionMetadata(11);
    REQUIRE(metadata.has_value());
    CHECK(metadata->exception_class == type);
    CHECK(metadata->modified_utf8_message == "bad padding");
    CHECK(environment.ExceptionCheck(11));
    environment.ExceptionClear(11);
    CHECK_FALSE(environment.PendingExceptionMetadata(11).has_value());
    const auto ref = environment.PublishLocalObject(11, {JniObjectDomain::dex_vm, 99});
    environment.Throw(11, ref);
    CHECK(environment.PendingExceptionMetadata(11)->throwable == JniObjectIdentity{JniObjectDomain::dex_vm, 99});
    CHECK(environment.PendingExceptionMetadata(11)->exception_class.value == 0);
    environment.ExceptionClear(11);
    environment.DetachThread(11);
}

TEST_CASE("JNI Describe roots the original exception across printing and restores pending") {
    using namespace ogplay::runtime;
    JniEnvironment env;
    env.AttachThread(601);
    const JniObjectIdentity original{JniObjectDomain::host, 6011};
    const auto ref = env.PublishLocalObject(601, original);
    env.Throw(601, ref);
    env.DeleteLocalRef(601, ref);
    int called = 0;
    env.SetExceptionDescriber([&](std::uint64_t thread, JniObjectIdentity throwable) {
        ++called;
        CHECK(throwable == original);
        CHECK_FALSE(env.ExceptionCheck(thread));
        bool rooted = false;
        env.VisitReferenceRoots([&](auto root) {
            if (root == original) rooted = true;
        });
        CHECK(rooted);
        const auto secondary = env.PublishLocalObject(thread, {JniObjectDomain::host, 6012});
        env.Throw(thread, secondary);
        env.DeleteLocalRef(thread, secondary);
    });
    env.ExceptionDescribe(601);
    CHECK(called == 1);
    CHECK(env.PendingExceptionMetadata(601)->throwable == original);
    env.ExceptionClear(601);
    bool retained = false;
    env.VisitReferenceRoots([&](auto root) {
        if (root == original) retained = true;
    });
    CHECK_FALSE(retained);
    env.ExceptionDescribe(601);
    CHECK(called == 1);
}

TEST_CASE("JNI request guarantees use Dalvik 512 while actual local usage can grow further") {
    using namespace ogplay::runtime;
    JniEnvironment env({1024, 16, 16});
    env.AttachThread(602);
    static_cast<void>(env.PublishLocalObject(602, {JniObjectDomain::host, 6021}));
    CHECK_NOTHROW(env.EnsureLocalCapacity(602, 511));
    CHECK_THROWS_AS(env.EnsureLocalCapacity(602, 512), JniReferenceError);
    CHECK_THROWS_AS(env.PushLocalFrame(602, 512), JniReferenceError);
    for (std::size_t i = 0; i < 512; ++i)
        static_cast<void>(env.PublishLocalObject(602, {JniObjectDomain::host, 6022 + i}));
    CHECK_THROWS_AS(env.EnsureLocalCapacity(602, 1), JniReferenceError);
    CHECK_THROWS_AS(env.PushLocalFrame(602, 1), JniReferenceError);
    CHECK_NOTHROW(static_cast<void>(env.PublishLocalObject(602, {JniObjectDomain::host, 6999})));
}

TEST_CASE("ADR-0153 pending diagnostics deduplicate slot thread and class and retain guest LR") {
    using namespace ogplay::runtime;
    JniEnvironment env;
    env.AttachThread(71);
    env.AttachThread(72);
    env.SetPendingCallDiagnostics([](const JniThrowableMetadata& value) {
        return value.throwable.value < 10 ? "java/lang/RuntimeException" : "java/lang/Error";
    });
    const auto one = env.PublishLocalObject(71, {JniObjectDomain::host, 1});
    const auto two = env.PublishLocalObject(72, {JniObjectDomain::host, 2});
    const auto same_class = env.PublishLocalObject(71, {JniObjectDomain::host, 3});
    env.Throw(71, one);
    env.Throw(72, two);
    const auto slot = FindJniSlot("NewStringUTF").value();
    env.EnterGuestCall(71, slot, 0x12345678);
    static_cast<void>(env.GetVersion(71));  // helper must not add a second slot/LR=0 warning
    env.LeaveGuestCall(71);
    env.EnterGuestCall(71, slot, 0x87654321);
    env.LeaveGuestCall(71);
    env.EnterGuestCall(71, slot, 0x99);
    env.Throw(71, same_class);  // different identity, same class: still no new warn
    env.LeaveGuestCall(71);
    env.EnterGuestCall(72, slot, 0x22);
    env.LeaveGuestCall(72);
    auto warnings = env.PendingCallDiagnostics();
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].guest_thread == 71);
    CHECK(std::get<std::string>(warnings[0].fields[0].value) == "NewStringUTF");
    CHECK(std::get<ogplay::core::GuestAddress>(warnings[0].fields[2].value).value == 0x12345678);
    env.ExceptionClear(71);
    const auto other = env.PublishLocalObject(71, {JniObjectDomain::host, 20});
    env.Throw(71, other);
    env.EnterGuestCall(71, slot, 0x33);
    env.LeaveGuestCall(71);
    CHECK(env.PendingCallDiagnostics().size() == 4);
    CHECK(env.PendingExceptionMetadata(71)->throwable.value == 20);
    env.ExceptionClear(71);
    env.Throw(71, one);
    env.EnterGuestCall(71, slot, 0x44);
    env.LeaveGuestCall(71);
    CHECK(env.PendingCallDiagnostics().size() == 5);  // Clear(Error) is new; NewStringUTF(RuntimeException) is deduped
}
