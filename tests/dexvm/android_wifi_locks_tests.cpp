#include "ogplay/runtime/integration/bitmap_pixels.h"
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
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-209 WifiLock Java state supports counted and uncounted ownership") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
        CHECK(f.context->wifi_lock_leases.size() == 1U);
        CHECK(f.vm.StringUtf8(f.On(lock, "toString", "()Ljava/lang/String;").ref).find("refcount = 2") != std::string::npos);
        f.On(lock, "release", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        CHECK(f.context->wifi_lock_leases.empty());
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
        CHECK_FALSE(f.On(manager, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;").ref.IsValid());
    }
}

TEST_CASE("DVM-209 WifiLock mode changes preserve separate count and held state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
        CHECK(f.context->wifi_lock_leases.empty());
        f.On(lock, "acquire", "()V");
        CHECK(f.On(lock, "isHeld", "()Z").AsInt());
    }
}

TEST_CASE("DVM-209 WifiLock permissions quota and GC are bounded per manager") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
        CHECK(f.context->wifi_lock_leases.empty());
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        const auto invalid = create(manager, 4);
        CHECK(f.OnOutcome(invalid, "acquire", "()V").exception.IsValid());
        std::vector<VmObjectRef> locks;
        for (int i = 0; i < 50; ++i) {
            locks.push_back(create(manager));
            if (i == 49) f.On(locks.back(), "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
            f.On(locks.back(), "acquire", "()V");
        }
        // Switching a held uncounted lock must not consume a second native lease.
        f.On(locks.back(), "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        f.On(locks.back(), "acquire", "()V");
        CHECK(f.context->wifi_lock_leases.size() == 50U);
        const auto overflow = create(manager);
        CHECK(f.OnOutcome(overflow, "acquire", "()V").exception.IsValid());
        CHECK_FALSE(f.On(overflow, "isHeld", "()Z").AsInt());
        const auto other_manager = f.New("Landroid/net/wifi/WifiManager;");
        const auto other = create(other_manager);
        f.On(other, "acquire", "()V");
        CHECK(f.context->wifi_lock_leases.size() == 51U);
        f.On(locks[0], "release", "()V");
        const auto replacement = create(manager);
        f.On(replacement, "acquire", "()V");
        f.vm.SetGcIntegration({{}, {}, [other](const VmRootVisitor& visit) { visit(other); }});
        static_cast<void>(f.vm.CollectGarbage("wifi lease owners"));
        CHECK(f.context->wifi_lock_leases.size() == 1U);
        CHECK(f.On(other, "isHeld", "()Z").AsInt());
        CHECK(f.vm.MarkReachable().IsMarked(other_manager));
        f.vm.SetGcIntegration({});
        static_cast<void>(f.vm.CollectGarbage("wifi lease release"));
        CHECK(f.context->wifi_lock_leases.empty());
    }
}

TEST_CASE("DVM-209 MulticastLock Java state and finalization preserve API19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
        CHECK(f.context->wifi_lock_leases.size() == 1U);
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
        CHECK(f.context->wifi_lock_leases.size() == 1U);
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
        CHECK(f.context->wifi_lock_leases.empty());
        CHECK(f.vm.StringUtf8(f.On(abandoned, "toString", "()Ljava/lang/String;").ref).find("not refcounted") != std::string::npos);
        CHECK_FALSE(f.On(manager, "isWifiEnabled", "()Z").AsInt());
        CHECK_FALSE(f.On(manager, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;").ref.IsValid());
    }
}

TEST_CASE("DVM-209 MulticastLock uses its own permission for acquisition and release") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
        CHECK(f.context->wifi_lock_leases.empty());
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
        CHECK(f.context->wifi_lock_leases.size() == 1U);
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        f.On(lock, "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
        f.On(lock, "release", "()V");
        CHECK_FALSE(f.On(lock, "isHeld", "()Z").AsInt());
        CHECK(f.context->wifi_lock_leases.empty());
    }
}

TEST_CASE("DVM-209 WifiLock and MulticastLock share quota and retain other owners") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.WAKE_LOCK");
        f.context->granted_permissions.insert("android.permission.CHANGE_WIFI_MULTICAST_STATE");
        const auto manager = f.New("Landroid/net/wifi/WifiManager;");
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
            if (i == 49) f.On(locks.back(), "setReferenceCounted", "(Z)V", {VmValue::Int(0)});
            f.On(locks.back(), "acquire", "()V");
        }
        CHECK(f.context->wifi_lock_leases.size() == 50U);
        f.On(locks.back(), "setReferenceCounted", "(Z)V", {VmValue::Int(1)});
        f.On(locks.back(), "acquire", "()V");
        CHECK(f.context->wifi_lock_leases.size() == 50U);
        for (const bool multicast : {false, true}) {
            const auto overflow = create(manager, multicast);
            const auto rejected = f.OnOutcome(overflow, "acquire", "()V");
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.model.ObjectClass(rejected.exception) == f.linker.ResolveDescriptor("Ljava/lang/UnsupportedOperationException;"));
            CHECK_FALSE(f.On(overflow, "isHeld", "()Z").AsInt());
            CHECK(f.context->wifi_lock_leases.size() == 50U);
        }
        const auto other_manager = f.New("Landroid/net/wifi/WifiManager;");
        const auto other = create(other_manager, true);
        f.On(other, "acquire", "()V");
        CHECK(f.context->wifi_lock_leases.size() == 51U);
        f.On(locks[1], "release", "()V");
        CHECK(f.On(locks[3], "isHeld", "()Z").AsInt());
        CHECK(f.context->wifi_lock_leases.contains(locks[3].Value()));
        CHECK(f.On(locks[0], "isHeld", "()Z").AsInt());
        CHECK(f.context->wifi_lock_leases.contains(locks[0].Value()));
        const auto replacement = create(manager, true);
        f.On(replacement, "acquire", "()V");
        f.vm.SetGcIntegration({{}, {}, [other](const VmRootVisitor& visit) { visit(other); }});
        static_cast<void>(f.vm.CollectGarbage("mixed lease owners"));
        CHECK(f.context->wifi_lock_leases.size() == 1U);
        CHECK(f.vm.MarkReachable().IsMarked(other_manager));
        CHECK(f.On(other, "isHeld", "()Z").AsInt());
        f.vm.SetGcIntegration({});
        static_cast<void>(f.vm.CollectGarbage("last multicast lease"));
        CHECK(f.context->wifi_lock_leases.empty());
    }
}
