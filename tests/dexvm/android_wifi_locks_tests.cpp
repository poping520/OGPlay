#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {
VmObjectRef NewWifiManager(AndroidValueVm& f) {
    const auto name = f.vm.NewStringUtf8("wifi");
    const auto binder = f.Static("Landroid/os/ServiceManager;", "getService",
                                 "(Ljava/lang/String;)Landroid/os/IBinder;", {VmValue::Ref(name)})
                            .ref;
    const auto service = f.Static("Landroid/net/wifi/IWifiManager$Stub;", "asInterface",
                                  "(Landroid/os/IBinder;)Landroid/net/wifi/IWifiManager;", {VmValue::Ref(binder)})
                             .ref;
    const auto base = f.New("Landroid/content/Context;");
    const auto manager =
        f.New("Landroid/net/wifi/WifiManager;", "(Landroid/content/Context;Landroid/net/wifi/IWifiManager;)V",
              {VmValue::Ref(base), VmValue::Ref(service)});
    CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
    return manager;
}
}  // namespace

TEST_CASE("Offline WifiInfo requires ACCESS_WIFI_STATE and reports no connection") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = NewWifiManager(f);
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        constexpr auto signature = "()Landroid/net/wifi/WifiInfo;";
        const auto denied = f.OnOutcome(manager, "getConnectionInfo", signature);
        REQUIRE(denied.exception.IsValid());
        CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/SecurityException;");

        f.context->granted_permissions.insert("android.permission.ACCESS_WIFI_STATE");
        const auto info = f.On(manager, "getConnectionInfo", signature).ref;
        REQUIRE(info.IsValid());
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        const auto copy = f.New("Landroid/net/wifi/WifiInfo;", "(Landroid/net/wifi/WifiInfo;)V", {VmValue::Ref(info)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.On(manager, "getConnectionInfo", signature).ref != info);
        for (const auto value : {info, copy}) {
            CHECK(f.On(value, "getNetworkId", "()I").AsInt() == -1);
            CHECK_FALSE(f.On(value, "getMacAddress", "()Ljava/lang/String;").ref.IsValid());
            const auto state = f.On(value, "getSupplicantState", "()Landroid/net/wifi/SupplicantState;").ref;
            REQUIRE(state.IsValid());
            CHECK(f.vm.StringUtf8(f.On(state, "name", "()Ljava/lang/String;").ref) == "UNINITIALIZED");
        }

        f.context->offline_connectivity.reset();
        const auto missing = f.OnOutcome(manager, "getConnectionInfo", signature);
        REQUIRE(missing.exception.IsValid());
        CHECK(f.linker.Class(missing.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-209 WifiLock Java state supports counted and uncounted ownership") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto manager = NewWifiManager(f);
        const auto tag = f.vm.NewStringUtf8("network lease");
        const auto lock = f.On(manager, "createWifiLock",
            "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
            {VmValue::Int(3), VmValue::Ref(tag)}).ref;
        CHECK(f.linker.Class(f.model.ObjectClass(lock)).is_boot_dex);
        for (const auto name : {"acquire", "release", "isHeld", "setReferenceCounted"}) {
            const auto signature = std::string(name) == "isHeld" ? "()Z" :
                std::string(name) == "setReferenceCounted" ? "(Z)V" : "()V";
            const auto index = f.linker.FindVtableIndex(f.model.ObjectClass(lock), name, signature);
            REQUIRE(index.has_value());
            CHECK(f.linker.Method(f.linker.Class(f.model.ObjectClass(lock)).vtable[*index]).kind != MethodKind::intrinsic);
        }
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        f.On(lock, "acquire", "()V");
        f.On(lock, "acquire", "()V");
        CHECK(f.vm.StringUtf8(f.On(lock, "toString", "()Ljava/lang/String;").ref).find("refcount = 2") != std::string::npos);
        f.On(lock, "release", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        const auto extra = f.OnOutcome(lock, "release", "()V");
        REQUIRE(extra.exception.IsValid());
        CHECK(f.model.ObjectClass(extra.exception) == f.linker.ResolveDescriptor("Ljava/lang/RuntimeException;"));
        const auto uncounted = f.On(manager, "createWifiLock",
            "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;", {VmValue::Ref(tag)}).ref;
        f.On(uncounted, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(uncounted, "release", "()V");
        f.On(uncounted, "acquire", "()V");
        f.On(uncounted, "acquire", "()V");
        CHECK(f.On(uncounted, "isHeld", "()Z").AsInt());
        f.On(uncounted, "release", "()V");
        f.On(uncounted, "release", "()V");
        CHECK_FALSE(f.On(uncounted, "isHeld", "()Z").AsInt());
        CHECK_FALSE(f.On(manager, "isWifiEnabled", "()Z").AsInt());
        CHECK(f.On(manager, "getWifiState", "()I").AsInt() == 1);
    }
}

TEST_CASE("DVM-209 WifiLock mode changes preserve separate count and held state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto manager = NewWifiManager(f);
        const auto lock = f.On(manager, "createWifiLock",
            "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;", {VmValue::Ref(VmObjectRef{})}).ref;
        f.On(lock, "acquire", "()V");
        f.On(lock, "acquire", "()V");
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        CHECK(f.vm.StringUtf8(f.On(lock, "toString", "()Ljava/lang/String;").ref).find("refcount = 2") != std::string::npos);
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "release", "()V");
        f.On(lock, "acquire", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
    }
}

TEST_CASE("DVM-209 WifiLock permissions quota and GC are bounded per manager") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = NewWifiManager(f);
        const auto create = [&](VmObjectRef service, int mode = 1) {
            return f.On(service, "createWifiLock",
                "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
                {VmValue::Int(mode), VmValue::Ref(f.vm.NewStringUtf8("quota"))}).ref;
        };
        const auto denied = create(manager);
        const auto rejection = f.OnOutcome(denied, "acquire", "()V");
        REQUIRE(rejection.exception.IsValid());
        CHECK(f.model.ObjectClass(rejection.exception) == f.linker.ResolveDescriptor("Ljava/lang/SecurityException;"));
        CHECK_FALSE(f.On(denied, "isHeld", "()Z").AsInt());
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto invalid = create(manager, 4);
        CHECK(f.OnOutcome(invalid, "acquire", "()V").exception.IsValid());
        std::vector<VmObjectRef> locks;
        for (int i = 0; i < 50; ++i) {
            locks.push_back(create(manager));
            f.On(locks.back(), "acquire", "()V");
        }
        const auto overflow = create(manager);
        CHECK(f.OnOutcome(overflow, "acquire", "()V").exception.IsValid());
        CHECK_FALSE(f.On(overflow, "isHeld", "()Z").AsInt());
        const auto other_manager = NewWifiManager(f);
        const auto other = create(other_manager);
        f.On(other, "acquire", "()V");
        f.On(locks[0], "release", "()V");
        const auto replacement = create(manager);
        f.On(replacement, "acquire", "()V");
        f.vm.SetGcIntegration({{}, {}, [other](const VmRootVisitor& visit) { visit(other); }});
        static_cast<void>(f.vm.CollectGarbage("wifi lease owners"));
        f.vm.RunFinalization();
        CHECK(f.On(other, "isHeld", "()Z").AsInt());
        CHECK(f.vm.MarkReachable().IsMarked(other_manager));
        f.vm.SetGcIntegration({});
        static_cast<void>(f.vm.CollectGarbage("wifi lease release"));
    }
}

TEST_CASE("DVM-209 MulticastLock Java state and finalization preserve API19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto manager = NewWifiManager(f);
        const auto create = [&] {
            return f.On(manager, "createMulticastLock",
                "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
                {VmValue::Ref(f.vm.NewStringUtf8("discovery"))}).ref;
        };
        const auto lock = create();
        const auto klass = f.model.ObjectClass(lock);
        CHECK(f.linker.Class(klass).is_boot_dex);
        for (const auto name : {"acquire", "release", "isHeld", "setReferenceCounted"}) {
            const auto signature = std::string(name) == "isHeld" ? "()Z" :
                std::string(name) == "setReferenceCounted" ? "(Z)V" : "()V";
            const auto index = f.linker.FindVtableIndex(klass, name, signature);
            REQUIRE(index.has_value());
            CHECK(f.linker.Method(f.linker.Class(klass).vtable[*index]).kind != MethodKind::intrinsic);
        }
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        f.On(lock, "acquire", "()V");
        f.On(lock, "acquire", "()V");
        CHECK(f.vm.StringUtf8(f.On(lock, "toString", "()Ljava/lang/String;").ref).find("refcount = 2") != std::string::npos);
        f.On(lock, "release", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "release", "()V");
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        // Switching modes did not reset the remaining count of one.
        CHECK(f.vm.StringUtf8(f.On(lock, "toString", "()Ljava/lang/String;").ref).find("refcount = 1") != std::string::npos);
        f.On(lock, "release", "()V");
        const auto excess = f.OnOutcome(lock, "release", "()V");
        REQUIRE(excess.exception.IsValid());
        CHECK(f.model.ObjectClass(excess.exception) == f.linker.ResolveDescriptor("Ljava/lang/RuntimeException;"));
        const auto uncounted = create();
        f.On(uncounted, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(uncounted, "release", "()V");
        f.On(uncounted, "acquire", "()V");
        f.On(uncounted, "acquire", "()V");
        f.On(uncounted, "release", "()V");
        CHECK_FALSE(f.On(uncounted, "isHeld", "()Z").AsInt());
        f.On(uncounted, "release", "()V");
        const auto abandoned = create();
        f.On(abandoned, "acquire", "()V");
        f.On(abandoned, "acquire", "()V");
        const auto object_class = f.linker.ResolveDescriptor("Ljava/lang/Object;");
        const auto object_finalize = f.linker.FindVtableIndex(object_class, "finalize", "()V");
        REQUIRE(object_finalize.has_value());
        CHECK((f.linker.Method(f.linker.Class(object_class).vtable[*object_finalize]).access_flags & kAccProtected) != 0U);
        f.On(abandoned, "finalize", "()V");
        CHECK_FALSE(f.On(abandoned, "isHeld", "()Z").AsInt());
        CHECK(f.vm.StringUtf8(f.On(abandoned, "toString", "()Ljava/lang/String;").ref).find("not refcounted") != std::string::npos);
        CHECK_FALSE(f.On(manager, "isWifiEnabled", "()Z").AsInt());
    }
}

TEST_CASE("DVM-209 MulticastLock uses its own permission for acquisition and release") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = NewWifiManager(f);
        const auto create = [&] {
            return f.On(manager, "createMulticastLock",
                "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
                {VmValue::Ref(VmObjectRef{})}).ref;
        };
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto denied = create();
        const auto rejection = f.OnOutcome(denied, "acquire", "()V");
        REQUIRE(rejection.exception.IsValid());
        CHECK(f.model.ObjectClass(rejection.exception) == f.linker.ResolveDescriptor("Ljava/lang/SecurityException;"));
        CHECK_FALSE(f.On(denied, "isHeld", "()Z").AsInt());
        f.context->granted_permissions.erase("android.permission.WAKE_LOCK");
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto lock = create();
        f.On(lock, "acquire", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
        f.context->granted_permissions.erase("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto release = f.OnOutcome(lock, "release", "()V");
        REQUIRE(release.exception.IsValid());
        CHECK(f.model.ObjectClass(release.exception) == f.linker.ResolveDescriptor("Ljava/lang/SecurityException;"));
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
    }
}

TEST_CASE("DVM-209 WifiLock and MulticastLock share quota and retain other owners") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto manager = NewWifiManager(f);
        const auto create = [&](VmObjectRef service, bool multicast) {
            return multicast ? f.On(service, "createMulticastLock",
                "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
                {VmValue::Ref(f.vm.NewStringUtf8("mixed"))}).ref :
                f.On(service, "createWifiLock",
                "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
                {VmValue::Ref(f.vm.NewStringUtf8("mixed"))}).ref;
        };
        std::vector<VmObjectRef> locks;
        for (int i = 0; i < 50; ++i) {
            locks.push_back(create(manager, i % 2 != 0));
            f.On(locks.back(), "acquire", "()V");
        }
        for (const bool multicast : {false, true}) {
            const auto overflow = create(manager, multicast);
            const auto rejected = f.OnOutcome(overflow, "acquire", "()V");
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.model.ObjectClass(rejected.exception) == f.linker.ResolveDescriptor("Ljava/lang/UnsupportedOperationException;"));
            CHECK_FALSE(f.On(overflow, "isHeld", "()Z").AsInt());
        }
        const auto other_manager = NewWifiManager(f);
        const auto other = create(other_manager, true);
        f.On(other, "acquire", "()V");
        f.On(locks[1], "release", "()V");
        CHECK(f.On(locks[3], "isHeld", "()Z").AsInt());
        CHECK(f.On(locks[0], "isHeld", "()Z").AsInt());
        const auto replacement = create(manager, true);
        f.On(replacement, "acquire", "()V");
        f.vm.SetGcIntegration({{}, {}, [other](const VmRootVisitor& visit) { visit(other); }});
        static_cast<void>(f.vm.CollectGarbage("mixed lease owners"));
        f.vm.RunFinalization();
        CHECK(f.vm.MarkReachable().IsMarked(other_manager));
        CHECK(f.On(other, "isHeld", "()Z").AsInt());
        f.vm.SetGcIntegration({});
        static_cast<void>(f.vm.CollectGarbage("last multicast lease"));
    }
}
