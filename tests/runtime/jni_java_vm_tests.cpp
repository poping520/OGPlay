#include <set>
#include <string_view>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni_java_vm.h"

TEST_CASE("JavaVM ABI has eight exact invoke slots") {
    const auto slots = ogplay::runtime::JniInvokeInterfaceSlots();
    REQUIRE(slots.size() == ogplay::runtime::kJniInvokeInterfaceSlotCount);
    CHECK(slots[0] == "reserved0");
    CHECK(slots[3] == "DestroyJavaVM");
    CHECK(slots[4] == "AttachCurrentThread");
    CHECK(slots[5] == "DetachCurrentThread");
    CHECK(slots[6] == "GetEnv");
    CHECK(slots[7] == "AttachCurrentThreadAsDaemon");
    CHECK(std::set<std::string_view>(slots.begin(), slots.end()).size() == 8);
    CHECK(ogplay::runtime::FindJniInvokeSlot("GetEnv")->Value() == 6);
    CHECK_FALSE(ogplay::runtime::FindJniInvokeSlot("missing").has_value());
}

TEST_CASE("JavaVM attach GetEnv and detach share one stable environment") {
    ogplay::runtime::JniEnvironment environment;
    ogplay::runtime::JniJavaVm vm(environment);
    CHECK(vm.GetEnv(41, ogplay::runtime::kJniVersion1_6).status ==
          ogplay::runtime::JniStatus::detached);
    const auto attached =
        vm.AttachCurrentThread(41, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    CHECK_FALSE(attached.environment.IsNull());
    CHECK(environment.IsThreadAttached(41));
    CHECK(vm.GetEnv(41, ogplay::runtime::kJniVersion1_1).environment ==
          attached.environment);
    CHECK(vm.AttachCurrentThread(41, ogplay::runtime::kJniVersion1_4)
              .environment == attached.environment);
    CHECK(vm.AttachedThreadCount() == 1);
    CHECK(vm.DetachCurrentThread(41) == ogplay::runtime::JniStatus::ok);
    CHECK_FALSE(environment.IsThreadAttached(41));
    CHECK(vm.DetachCurrentThread(41) ==
          ogplay::runtime::JniStatus::detached);
}

TEST_CASE("JavaVM daemon and version failures do not create partial state") {
    ogplay::runtime::JniEnvironment environment({2, 2, 2});
    ogplay::runtime::JniJavaVm vm(environment);
    CHECK(vm.AttachCurrentThread(0, ogplay::runtime::kJniVersion1_6).status ==
          ogplay::runtime::JniStatus::invalid_arguments);
    CHECK(vm.AttachCurrentThread(9, 0x00010008).status ==
          ogplay::runtime::JniStatus::version);
    CHECK(vm.AttachedThreadCount() == 0);
    CHECK_THROWS_AS(
        static_cast<void>(vm.AttachCurrentThread(
            9, ogplay::runtime::kJniVersion1_6, 3)),
        ogplay::runtime::JniReferenceError);
    CHECK(vm.AttachedThreadCount() == 0);
    const auto daemon = vm.AttachCurrentThreadAsDaemon(
        9, ogplay::runtime::kJniVersion1_6, 2);
    CHECK(daemon.status == ogplay::runtime::JniStatus::ok);
    CHECK(vm.IsDaemon(9));
    CHECK(vm.AttachCurrentThread(9, ogplay::runtime::kJniVersion1_6)
              .environment == daemon.environment);
    CHECK(vm.IsDaemon(9));
    CHECK(vm.DetachCurrentThread(9) == ogplay::runtime::JniStatus::ok);
}
