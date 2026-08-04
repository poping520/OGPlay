#include <set>
#include <string_view>

#include <doctest/doctest.h>

#include "ogplay/runtime/jni/jni_java_vm.h"

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

TEST_CASE("common JNI slot directory binds only behavior-backed thunks") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniFunctionTable environment_table(ledger);
    ogplay::runtime::JniInvokeFunctionTable invoke_table(ledger);
    const ogplay::runtime::JniCommonSlotDirectory directory;
    directory.Install(environment_table, invoke_table);
    CHECK(environment_table.IsSealed());
    CHECK(invoke_table.IsSealed());
    CHECK(directory.Bindings().size() == 212);

    const auto call = ogplay::runtime::FindJniSlot("CallDoubleMethodA").value();
    const auto string = ogplay::runtime::FindJniSlot("NewStringUTF").value();
    const auto array = ogplay::runtime::FindJniSlot("SetLongArrayRegion").value();
    const auto field = ogplay::runtime::FindJniSlot("GetIntField").value();
    const auto object_array =
        ogplay::runtime::FindJniSlot("SetObjectArrayElement").value();
    CHECK(environment_table.IsBound(call));
    CHECK(environment_table.IsBound(string));
    CHECK(environment_table.IsBound(array));
    CHECK(environment_table.IsBound(field));
    CHECK(environment_table.IsBound(object_array));
    const auto field_binding = directory.FindByThunk(
        environment_table.Resolve(field, 0x1000));
    REQUIRE(field_binding.has_value());
    CHECK(field_binding->handler ==
          ogplay::runtime::JniSlotHandlerKind::field_store);
    const auto array_binding = directory.FindByThunk(
        environment_table.Resolve(object_array, 0x1000));
    REQUIRE(array_binding.has_value());
    CHECK(array_binding->handler ==
          ogplay::runtime::JniSlotHandlerKind::object_array_store);
    const auto thunk = environment_table.Resolve(call, 0x1000);
    const auto binding = directory.FindByThunk(thunk);
    REQUIRE(binding.has_value());
    CHECK(binding->name == "CallDoubleMethodA");
    CHECK(binding->handler == ogplay::runtime::JniSlotHandlerKind::invocation);
}

TEST_CASE("unbound JNI and JavaVM slots remain observable traps") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniFunctionTable environment_table(ledger);
    ogplay::runtime::JniInvokeFunctionTable invoke_table(ledger);
    const ogplay::runtime::JniCommonSlotDirectory directory;
    directory.Install(environment_table, invoke_table);

    const auto direct =
        ogplay::runtime::FindJniSlot("NewDirectByteBuffer").value();
    CHECK_THROWS_AS(
        static_cast<void>(environment_table.Resolve(direct, 0x1111)),
        ogplay::runtime::JniUnimplementedCall);
    const auto destroy =
        ogplay::runtime::FindJniInvokeSlot("DestroyJavaVM").value();
    CHECK_FALSE(invoke_table.IsBound(destroy));
    CHECK_THROWS_AS(
        static_cast<void>(invoke_table.Resolve(destroy, 0x2222)),
        ogplay::runtime::JniInvokeUnimplementedCall);

    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].count == 1);
    CHECK(hits[1].count == 1);
}
