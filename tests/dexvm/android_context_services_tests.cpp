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
#include <functional>
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

TEST_CASE("Context class loader preserves application identity and virtual base delegation") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto custom = IntrinsicClassBuilder::Class("Ltest/LoaderContext;", "Landroid/content/Context;");
        custom.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        custom.OverrideMethod("getClassLoader", "()Ljava/lang/ClassLoader;", [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.ClassLoaders().BootstrapLoader());
        });
        AndroidValueVm f(backend, {std::move(custom).Build()});
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto loader = f.On(base, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
        CHECK(loader == f.vm.ClassLoaders().ApplicationLoader());
        CHECK(f.On(loader, "getParent", "()Ljava/lang/ClassLoader;").ref == f.vm.ClassLoaders().BootstrapLoader());
        for (const auto* type : {"Landroid/app/Application;", "Landroid/app/Activity;", "Landroid/app/Service;"}) {
            const auto object = f.New(type);
            const auto object_root = f.vm.ProtectReferences(std::array{object});
            f.On(object, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
            CHECK(f.On(object, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == loader);
            // The framework object's defining loader is not its Context loader.
            CHECK(f.On(f.model.ClassObject(f.model.ObjectClass(object)), "getClassLoader", "()Ljava/lang/ClassLoader;")
                      .ref != loader);
            static_cast<void>(f.vm.CollectGarbage("context-class-loader"));
            CHECK(f.On(object, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == loader);
        }
        const auto overridden = f.New("Ltest/LoaderContext;");
        const auto overridden_root = f.vm.ProtectReferences(std::array{overridden});
        const auto wrapper =
            f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(overridden)});
        const auto wrapper_root = f.vm.ProtectReferences(std::array{wrapper});
        const auto nested =
            f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(wrapper)});
        const auto nested_root = f.vm.ProtectReferences(std::array{nested});
        CHECK(f.On(nested, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == f.vm.ClassLoaders().BootstrapLoader());
        const auto unattached =
            f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(VmObjectRef{})});
        const auto failure = f.OnOutcome(unattached, "getClassLoader", "()Ljava/lang/ClassLoader;");
        REQUIRE(failure.exception.IsValid());
        CHECK(f.linker.Class(failure.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Context class loader reflects BootDex system properties with API19 defaults") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto application = f.New("Landroid/app/Application;");
        const auto application_root = f.vm.ProtectReferences(std::array{application});
        f.On(application, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto loader = f.On(application, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
        const auto type = f.On(loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
                               {VmValue::Ref(f.vm.NewStringUtf8("android.os.SystemProperties"))})
                              .ref;
        CHECK(type == f.model.ClassObject(f.linker.ResolveDescriptor("Landroid/os/SystemProperties;")));
        CHECK(f.On(type, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == f.vm.ClassLoaders().BootstrapLoader());
        const auto parameters = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/Class;"),
                                                       f.linker.ResolveDescriptor("Ljava/lang/Class;"), 2);
        const auto parameters_root = f.vm.ProtectReferences(std::array{parameters});
        const auto string_class = f.model.ClassObject(f.linker.ResolveDescriptor("Ljava/lang/String;"));
        f.model.SetObjectElement(parameters, 0, string_class);
        f.model.SetObjectElement(parameters, 1, string_class);
        const auto method = f.On(type, "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                                 {VmValue::Ref(f.vm.NewStringUtf8("get")), VmValue::Ref(parameters)})
                                .ref;
        const auto method_root = f.vm.ProtectReferences(std::array{method});
        CHECK(f.linker.Method(f.vm.Reflection().MethodMetadata(method).method).kind == MethodKind::interpreted);
        const auto arguments = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                                      f.linker.ResolveDescriptor("Ljava/lang/Object;"), 2);
        const auto arguments_root = f.vm.ProtectReferences(std::array{arguments});
        f.model.SetObjectElement(arguments, 0, f.vm.NewStringUtf8("ro.serialno"));
        f.model.SetObjectElement(arguments, 1, f.vm.NewStringUtf8("Unknown"));
        const auto read = [&] {
            return f.vm.StringUtf8(f.On(method, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                                        {VmValue::Ref(type), VmValue::Ref(arguments)})
                                       .ref);
        };
        CHECK(read() == "Unknown");
        static_cast<void>(f.vm.CollectGarbage("context-properties-reflection"));
        CHECK(read() == "Unknown");
        f.model.SetObjectElement(arguments, 0, f.vm.NewStringUtf8("ro.build.version.sdk"));
        CHECK(read() == "19");
    }
}

TEST_CASE("Telephony subscriber identity is unavailable without a cellular subscription") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto phone = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                {VmValue::Ref(f.vm.NewStringUtf8("phone"))})
                               .ref;
        REQUIRE(phone.IsValid());
        const auto phone_root = f.vm.ProtectReferences(std::array{phone});
        const auto type = f.model.ObjectClass(phone);
        const auto index = f.linker.FindVtableIndex(type, "getSubscriberId", "()Ljava/lang/String;");
        REQUIRE(index.has_value());
        const auto flags = f.linker.Method(f.linker.Class(type).vtable[*index]).access_flags;
        CHECK((flags & kAccPublic) != 0U);
        CHECK((flags & (kAccFinal | kAccStatic)) == 0U);
        CHECK(f.On(phone, "getPhoneType", "()I").AsInt() == 0);
        CHECK(f.On(phone, "getSimState", "()I").AsInt() == 1);
        for (const auto events : {0, 1}) {
            CHECK_NOTHROW(f.On(phone, "listen", "(Landroid/telephony/PhoneStateListener;I)V",
                               {VmValue::Ref(VmObjectRef{}), VmValue::Int(events)}));
            CHECK(f.context->telephony_listeners.empty());
        }
        CHECK_FALSE(f.On(phone, "getSubscriberId", "()Ljava/lang/String;").ref.IsValid());
        static_cast<void>(f.vm.CollectGarbage("telephony-subscriber-absence"));
        CHECK_FALSE(f.On(phone, "getSubscriberId", "()Ljava/lang/String;").ref.IsValid());
    }
}

TEST_CASE("DVM-128 Settings.Secure reads the injected API 19 identity") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        fixture.context->secure_settings.insert_or_assign("android_id", "0123456789abcdef");
        const auto resolver = fixture.vm.NewIntrinsicInstance("Landroid/content/ContentResolver;");
        const auto key = fixture.vm.NewStringUtf8("android_id");
        const auto roots = fixture.vm.ProtectReferences(std::array{resolver, key});
        const auto read = [&] {
            return fixture
                .Static("Landroid/provider/Settings$Secure;", "getString",
                        "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                        "Ljava/lang/String;",
                        {VmValue::Ref(resolver), VmValue::Ref(key)})
                .ref;
        };
        CHECK(fixture.vm.StringUtf8(read()) == "0123456789abcdef");
        static_cast<void>(fixture.vm.CollectGarbage("dvm128-secure-settings"));
        CHECK(fixture.vm.StringUtf8(read()) == "0123456789abcdef");

        const auto unknown = fixture.vm.NewStringUtf8("unknown_setting");
        CHECK_FALSE(fixture
                        .Static("Landroid/provider/Settings$Secure;", "getString",
                                "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                                "Ljava/lang/String;",
                                {VmValue::Ref(resolver), VmValue::Ref(unknown)})
                        .ref.IsValid());

        const auto outcome = fixture.StaticOutcome("Landroid/provider/Settings$Secure;", "getString",
                                                   "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                                                   "Ljava/lang/String;",
                                                   {VmValue::Ref(VmObjectRef{}), VmValue::Ref(key)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/NullPointerException;");

        const auto null_name = fixture.StaticOutcome("Landroid/provider/Settings$Secure;", "getString",
                                                     "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                                                     "Ljava/lang/String;",
                                                     {VmValue::Ref(resolver), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_name.exception.IsValid());
        CHECK(fixture.linker.Class(null_name.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Settings BootDex routes moved keys through the bounded store") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        fixture.context->secure_settings.insert_or_assign("android_id", "0123456789abcdef");
        const auto resolver = fixture.vm.NewIntrinsicInstance("Landroid/content/ContentResolver;");
        const auto android_id = fixture.vm.NewStringUtf8("android_id");
        const auto volume = fixture.vm.NewStringUtf8("volume_music");
        const auto text = fixture.vm.NewStringUtf8("17");
        const auto roots = fixture.vm.ProtectReferences(std::array{resolver, android_id, volume, text});

        const auto get_string = "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                                "Ljava/lang/String;";
        const auto moved = fixture
                               .Static("Landroid/provider/Settings$System;", "getString", get_string,
                                       {VmValue::Ref(resolver), VmValue::Ref(android_id)})
                               .ref;
        CHECK(fixture.vm.StringUtf8(moved) == "0123456789abcdef");
        CHECK(fixture
                  .Static("Landroid/provider/Settings$Secure;", "putString",
                          "(Landroid/content/ContentResolver;Ljava/lang/String;"
                          "Ljava/lang/String;)Z",
                          {VmValue::Ref(resolver), VmValue::Ref(android_id), VmValue::Ref(text)})
                  .AsInt() == 0);

        CHECK(fixture
                  .Static("Landroid/provider/Settings$System;", "putString",
                          "(Landroid/content/ContentResolver;Ljava/lang/String;"
                          "Ljava/lang/String;)Z",
                          {VmValue::Ref(resolver), VmValue::Ref(volume), VmValue::Ref(text)})
                  .AsInt() == 1);
        const auto stored = fixture
                                .Static("Landroid/provider/Settings$System;", "getString", get_string,
                                        {VmValue::Ref(resolver), VmValue::Ref(volume)})
                                .ref;
        CHECK(fixture.vm.StringUtf8(stored) == "17");
        CHECK(fixture
                  .Static("Landroid/provider/Settings$System;", "getInt",
                          "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
                          {VmValue::Ref(resolver), VmValue::Ref(volume), VmValue::Int(3)})
                  .AsInt() == 17);

        const auto system_class = fixture.linker.ResolveDescriptor("Landroid/provider/Settings$System;");
        const auto public_get = fixture.linker.FindDirectMethod(system_class, "getString", get_string);
        REQUIRE(public_get.has_value());
        CHECK(fixture.linker.Method(*public_get).kind == MethodKind::interpreted);
        const auto cache_class = fixture.linker.ResolveDescriptor("Landroid/provider/Settings$NameValueCache;");
        const auto store_get = fixture.linker.FindVtableIndex(cache_class, "getStringForUser",
                                                              "(Landroid/content/ContentResolver;Ljava/lang/String;I)"
                                                              "Ljava/lang/String;");
        REQUIRE(store_get.has_value());
        CHECK(fixture.linker.Method(fixture.linker.Class(cache_class).vtable[*store_get]).kind ==
              MethodKind::intrinsic);
    }
}

TEST_CASE("DVM-116 BackupManager Java reports absent backup service") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int callbacks{};
        auto observer = IntrinsicClassBuilder::Class("Ltest/BackupObserver;", "Landroid/app/backup/RestoreObserver;");
        observer.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        const auto callback = [&callbacks](IntrinsicContext&) {
            ++callbacks;
            return VmValue::Void();
        };
        observer.VirtualMethod("restoreStarting", "(I)V", callback);
        observer.VirtualMethod("onUpdate", "(ILjava/lang/String;)V", callback);
        observer.VirtualMethod("restoreFinished", "(I)V", callback);
        AndroidValueVm f(backend, {std::move(observer).Build()});
        const auto activity = f.New("Landroid/app/Activity;");
        const auto manager =
            f.New("Landroid/app/backup/BackupManager;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto owner = f.model.ObjectClass(manager);
        CHECK(f.linker.Class(owner).is_boot_dex);
        const auto context = f.linker.FindFieldRecursive(owner, "mContext", "Landroid/content/Context;");
        REQUIRE(context.has_value());
        CHECK(f.model.InstanceSlots(manager)[f.linker.Field(*context).slot].bits == activity.Value());
        CHECK(f.vm.MarkReachable().IsMarked(activity));
        static_cast<void>(f.vm.CollectGarbage("dvm116-manager-context"));
        CHECK(f.model.ObjectClass(activity) == f.linker.ResolveDescriptor("Landroid/app/Activity;"));
        f.On(manager, "dataChanged", "()V");
        f.Static("Landroid/app/backup/BackupManager;", "dataChanged", "(Ljava/lang/String;)V",
                 {VmValue::Ref(f.vm.NewStringUtf8("org.example.fixture"))});
        f.Static("Landroid/app/backup/BackupManager;", "dataChanged", "(Ljava/lang/String;)V",
                 {VmValue::Ref(VmObjectRef{})});
        const auto receiver = f.New("Ltest/BackupObserver;");
        const auto observer_root = f.vm.ProtectReferences(std::array{receiver});
        CHECK(f.On(manager, "requestRestore", "(Landroid/app/backup/RestoreObserver;)I", {VmValue::Ref(receiver)})
                  .AsInt() == -1);
        CHECK(f.On(manager, "requestRestore", "(Landroid/app/backup/RestoreObserver;)I", {VmValue::Ref(VmObjectRef{})})
                  .AsInt() == -1);
        CHECK_FALSE(f.On(manager, "beginRestoreSession", "()Landroid/app/backup/RestoreSession;").ref.IsValid());
        CHECK(callbacks == 0);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.backup_service");
        CHECK(hits[0].count == 6);
        const auto service = f.linker.FindFieldRecursive(owner, "sService", "Landroid/app/backup/IBackupManager;");
        REQUIRE(service.has_value());
        const auto slot = f.linker.Field(*service).slot;
        CHECK(f.linker.Class(owner).static_storage[slot] == 0);
        // Unexpected service injection must fail explicitly, never claim success.
        f.linker.MutableClass(owner).static_storage[slot] = receiver.Value();
        const auto unsupported = f.OnOutcome(manager, "dataChanged", "()V");
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(unsupported.exception)).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.linker.Class(owner).static_storage[slot] == receiver.Value());
        f.linker.MutableClass(owner).static_storage[slot] = 0;
        const auto null_context =
            f.New("Landroid/app/backup/BackupManager;", "(Landroid/content/Context;)V", {VmValue::Ref(VmObjectRef{})});
        f.On(null_context, "dataChanged", "()V");
    }
}

TEST_CASE("DVM-220 BootDex sensor clients share an honest empty device boundary") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t callbacks{};
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/SensorClient;", "Ljava/lang/Object;",
            {"Landroid/hardware/SensorListener;", "Landroid/hardware/SensorEventListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        const auto callback = [&callbacks](IntrinsicContext&) {
            ++callbacks;
            return VmValue::Void();
        };
        listener.VirtualMethod("onSensorChanged", "(I[F)V", callback);
        listener.VirtualMethod("onAccuracyChanged", "(II)V", callback);
        listener.VirtualMethod("onSensorChanged", "(Landroid/hardware/SensorEvent;)V", callback);
        listener.VirtualMethod("onAccuracyChanged", "(Landroid/hardware/Sensor;I)V", callback);
        AndroidValueVm f(backend, {std::move(listener).Build()});
        for (const auto* name : {"Sensor", "SensorEvent", "SensorManager", "LocalSensorManager", "LegacySensorManager",
                                 "TriggerEvent", "TriggerEventListener"}) {
            const auto type = f.linker.ResolveDescriptor(std::string("Landroid/hardware/") + name + ";");
            CHECK(f.linker.Class(type).is_boot_dex);
        }
        for (const auto* name : {"SensorListener", "SensorEventListener"}) {
            const auto type = f.linker.ResolveDescriptor(std::string("Landroid/hardware/") + name + ";");
            CHECK(f.linker.Class(type).is_boot_dex);
            CHECK(f.linker.Class(type).is_interface);
            CHECK(f.linker.MethodsOf(type).size() == 2);
        }
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f
                .On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    {VmValue::Ref(f.vm.NewStringUtf8("sensor"))})
                .ref;
        };
        const auto manager = service();
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto client = f.New("Ltest/SensorClient;");
        const auto client_root = f.vm.ProtectReferences(std::array{client});
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/hardware/SensorManager;"),
                                    f.model.ObjectClass(manager)));
        const auto get_list = [&](int type) {
            return f.On(manager, "getSensorList", "(I)Ljava/util/List;", {VmValue::Int(type)}).ref;
        };
        const auto list = get_list(1);
        CHECK(f.On(list, "size", "()I").AsInt() == 0);
        CHECK(get_list(1) == list);
        CHECK(f.On(get_list(-1), "size", "()I").AsInt() == 0);
        CHECK_FALSE(f.On(manager, "getDefaultSensor", "(I)Landroid/hardware/Sensor;", {VmValue::Int(1)}).ref.IsValid());
        const auto add = f.OnOutcome(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(add.exception.IsValid());
        CHECK(f.linker.Class(add.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(manager, "getSensors", "()I").AsInt() == 0);
        for (const auto mask : {1, 2, 128}) {
            CHECK(f.On(manager, "registerListener", "(Landroid/hardware/SensorListener;I)Z",
                       {VmValue::Ref(client), VmValue::Int(mask)})
                      .AsInt() == 0);
            CHECK(f.On(manager, "registerListener", "(Landroid/hardware/SensorListener;II)Z",
                       {VmValue::Ref(client), VmValue::Int(mask), VmValue::Int(3)})
                      .AsInt() == 0);
            f.On(manager, "unregisterListener", "(Landroid/hardware/SensorListener;I)V",
                 {VmValue::Ref(client), VmValue::Int(mask)});
        }
        f.On(manager, "unregisterListener", "(Landroid/hardware/SensorListener;)V", {VmValue::Ref(client)});
        CHECK(f.On(manager, "registerListener", "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z",
                   {VmValue::Ref(client), VmValue::Ref(VmObjectRef{}), VmValue::Int(3)})
                  .AsInt() == 0);
        f.On(manager, "unregisterListener", "(Landroid/hardware/SensorEventListener;)V", {VmValue::Ref(client)});
        CHECK(f.On(manager, "flush", "(Landroid/hardware/SensorEventListener;)Z", {VmValue::Ref(client)}).AsInt() == 0);
        for (const auto& failure :
             {f.OnOutcome(manager, "flush", "(Landroid/hardware/SensorEventListener;)Z", {VmValue::Ref(VmObjectRef{})}),
              f.OnOutcome(manager, "requestTriggerSensor",
                          "(Landroid/hardware/TriggerEventListener;Landroid/hardware/Sensor;)Z",
                          {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})})}) {
            REQUIRE(failure.exception.IsValid());
            CHECK(f.linker.Class(failure.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        }
        static_cast<void>(f.vm.CollectGarbage("sensor-client-empty-directory"));
        CHECK(service() == manager);
        CHECK(get_list(1) == list);
        CHECK(f.On(manager, "getSensors", "()I").AsInt() == 0);
        CHECK(callbacks == 0);
    }
}

TEST_CASE("DVM228 ActivityManager queries preserve guest memory units thresholds and ownership") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        const auto base = f.New("Landroid/content/Context;");
        f.context->application_base_context = base;
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f
                .On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    {VmValue::Ref(f.vm.NewStringUtf8("activity"))})
                .ref;
        };
        const auto manager = service();
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        const auto info = f.New("Landroid/app/ActivityManager$MemoryInfo;");
        const auto roots = f.vm.ProtectReferences(std::array{info});
        const auto field_slot = [&](const char* name, const char* type) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(info), name, type);
            REQUIRE(field.has_value());
            return f.linker.Field(*field).slot;
        };
        const auto number = [&](const char* name) {
            const auto slot = field_slot(name, "J");
            const auto values = f.model.InstanceSlots(info);
            return static_cast<std::uint64_t>(values[slot].bits) |
                   (static_cast<std::uint64_t>(values[slot + 1U].bits) << 32U);
        };
        const auto low = [&] { return f.model.InstanceSlots(info)[field_slot("lowMemory", "Z")].bits != 0; };
        const auto query = [&] {
            f.On(manager, "getMemoryInfo", "(Landroid/app/ActivityManager$MemoryInfo;)V", {VmValue::Ref(info)});
        };
        f.context->memory_snapshot.reset();  // Explicitly exercise missing facts, independent of fixture defaults.
        const auto absent =
            f.OnOutcome(manager, "getMemoryInfo", "(Landroid/app/ActivityManager$MemoryInfo;)V", {VmValue::Ref(info)});
        REQUIRE(absent.exception.IsValid());
        CHECK(f.linker.Class(absent.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(number("availMem") == 0);
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        f.context->memory_snapshot = MakeGuestMemorySnapshot({});
        query();
        CHECK(number("totalMem") == UINT64_C(1073741824));
        CHECK(number("availMem") == UINT64_C(805306368));
        CHECK(number("threshold") == UINT64_C(100663296));
        CHECK(number("hiddenAppThreshold") == UINT64_C(100663296));
        CHECK(number("secondaryServerThreshold") == UINT64_C(100663296));
        CHECK(number("visibleAppThreshold") == UINT64_C(62914560));
        CHECK(number("foregroundAppThreshold") == UINT64_C(50331648));
        CHECK_FALSE(low());
        GuestProcFacts pressure;
        pressure.memory_free_kb = 65536;
        pressure.memory_cached_kb = 32768;
        pressure.memory_pressure = {65536, 131072, 65536, 49152, 32768};
        f.context->memory_snapshot = MakeGuestMemorySnapshot(pressure);
        query();
        CHECK_FALSE(low());  // Exactly HOME + (CACHED-HOME)/2, strict comparison.
        pressure.memory_free_kb -= 1;
        f.context->memory_snapshot = MakeGuestMemorySnapshot(pressure);
        query();
        CHECK(low());
        CHECK(number("threshold") == UINT64_C(67108864));
        GuestProcFacts wide;
        wide.memory_total_kb = 8388608;
        wide.memory_free_kb = 4194304;
        f.context->memory_snapshot = MakeGuestMemorySnapshot(wide);
        query();
        CHECK(number("totalMem") == UINT64_C(8589934592));
        CHECK(number("availMem") == UINT64_C(6442450944));
        const auto null = f.OnOutcome(manager, "getMemoryInfo", "(Landroid/app/ActivityManager$MemoryInfo;)V",
                                      {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null.exception.IsValid());
        CHECK(f.linker.Class(null.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto wrapper =
            f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        CHECK(f.On(wrapper, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                   {VmValue::Ref(f.vm.NewStringUtf8("activity"))})
                  .ref == manager);
        static_cast<void>(f.vm.CollectGarbage("activity-memory-service"));
        CHECK(service() == manager);
        query();
        CHECK(number("availMem") == UINT64_C(6442450944));
    }
}

TEST_CASE("DVM228 ActivityManager reports fresh current process facts and lifecycle importance") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::function<void()> during_create;
        auto activity_decl = IntrinsicClassBuilder::Class("Ltest/ProcessActivity;", "Landroid/app/Activity;");
        activity_decl.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        activity_decl.OverrideMethod("onCreate", "(Landroid/os/Bundle;)V", [&](IntrinsicContext&) {
            during_create();
            return VmValue::Void();
        });
        AndroidValueVm f(backend, {std::move(activity_decl).Build()});
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        f.context->package_name = "test.process";
        f.context->application_uid = 10234;
        const auto base = f.New("Landroid/content/Context;");
        f.context->application_base_context = base;
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto manager = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("activity"))})
                                 .ref;
        const auto query = [&] { return f.On(manager, "getRunningAppProcesses", "()Ljava/util/List;").ref; };
        const auto first = query();
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        const auto item = [&](VmObjectRef list) {
            CHECK(f.On(list, "size", "()I").AsInt() == 1);
            return f.On(list, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref;
        };
        const auto info = item(first);
        CHECK(f.linker.Class(f.model.ObjectClass(info)).is_boot_dex);
        const auto field = [&](VmObjectRef object, const char* name, const char* type = "I") {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        CHECK(field(info, "pid") == f.Static("Landroid/os/Process;", "myPid", "()I").AsInt());
        CHECK(field(info, "uid") == f.Static("Landroid/os/Process;", "myUid", "()I").AsInt());
        CHECK(f.vm.StringUtf8(VmObjectRef(field(info, "processName", "Ljava/lang/String;"))) == "test.process");
        const auto packages = VmObjectRef(field(info, "pkgList", "[Ljava/lang/String;"));
        CHECK(f.model.ArrayLength(packages) == 1);
        CHECK(f.vm.StringUtf8(f.model.GetObjectElement(packages, 0)) == "test.process");
        CHECK(field(info, "flags") == 0);
        CHECK(field(info, "importance") == 400);
        CHECK(field(info, "lru") == 1);
        for (const auto* name :
             {"lastTrimLevel", "importanceReasonCode", "importanceReasonPid", "importanceReasonImportance"})
            CHECK(field(info, name) == 0);
        CHECK(field(info, "importanceReasonComponent", "Landroid/content/ComponentName;") == 0);
        f.context->application_process_name = "test.process:worker";
        // A newly pushed record during onCreate has not started/resumed, but is not stopped.
        const auto activity = f.New("Ltest/ProcessActivity;");
        f.context->activity_stack.emplace_back().object = activity;
        during_create = [&] {
            const auto list = query();
            const auto root = f.vm.ProtectReferences(std::array{list});
            CHECK(field(item(list), "importance") == 100);
        };
        f.On(activity, "onCreate", "(Landroid/os/Bundle;)V", {VmValue::Ref(VmObjectRef{})});
        const auto second = query();
        const auto second_root = f.vm.ProtectReferences(std::array{second});
        const auto created = item(second);
        CHECK(second != first);
        CHECK(created != info);
        CHECK(VmObjectRef(field(created, "pkgList", "[Ljava/lang/String;")) != packages);
        CHECK(f.vm.StringUtf8(VmObjectRef(field(created, "processName", "Ljava/lang/String;"))) ==
              "test.process:worker");
        CHECK(field(created, "flags") == 4);
        CHECK(field(created, "importance") == 100);
        CHECK(field(created, "lru") == 0);
        static_cast<void>(f.vm.CollectGarbage("activity-process-facts"));
        CHECK(field(item(first), "importance") == 400);  // Detached value snapshot survives GC.
        f.context->activity_stack.back().stopped = true;
        const auto stopped = query();
        const auto stopped_root = f.vm.ProtectReferences(std::array{stopped});
        CHECK(field(item(stopped), "importance") == 400);
        CHECK(field(item(stopped), "lru") == 1);
        CHECK(field(item(stopped), "flags") == 4);
        f.context->activity_stack.insert(f.context->activity_stack.begin(), DexVmAndroidContext::ActivityRecord{});
        f.context->activity_stack.front().visible = true;
        const auto visible = query();
        const auto visible_root = f.vm.ProtectReferences(std::array{visible});
        CHECK(field(item(visible), "importance") == 200);
        CHECK(field(item(visible), "lru") == 0);
    }
}

TEST_CASE("DVM-239 Debug MemoryInfo keeps original values Parcel and failure semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto info = f.New("Landroid/os/Debug$MemoryInfo;");
        const auto root = f.vm.ProtectReferences(std::array{info});
        CHECK(f.linker.Class(f.model.ObjectClass(info)).is_boot_dex);
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 0);
        const auto query = [&](VmObjectRef object) {
            return f.StaticOutcome("Landroid/os/Debug;", "getMemoryInfo", "(Landroid/os/Debug$MemoryInfo;)V",
                                   {VmValue::Ref(object)});
        };
        const auto absent = query(info);
        REQUIRE(absent.exception.IsValid());
        CHECK(f.linker.Class(absent.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 0);
        GuestProcessMemorySnapshot snapshot;
        snapshot.heaps = {{{20, 2, 10, 1, 5, 1, 0}, {30, 3, 20, 1, 5, 1, 0}, {40, 4, 20, 1, 10, 1, 0}}};
        snapshot.other_stats[0] = 4;
        snapshot.other_stats[2] = 4;
        f.context->process_memory_snapshot = snapshot;
        REQUIRE_FALSE(query(info).exception.IsValid());
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 90);
        CHECK(f.On(info, "getTotalUss", "()I").AsInt() == 70);
        CHECK(f.On(info, "getTotalPrivateDirty", "()I").AsInt() == 50);
        CHECK(f.On(info, "getTotalSwappablePss", "()I").AsInt() == 9);
        CHECK(f.On(info, "getOtherPss", "(I)I", {VmValue::Int(0)}).AsInt() == 4);
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        f.On(info, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto copy = f.New("Landroid/os/Debug$MemoryInfo;");
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        f.On(copy, "readFromParcel", "(Landroid/os/Parcel;)V", {VmValue::Ref(parcel)});
        CHECK(f.On(copy, "getTotalPss", "()I").AsInt() == 90);
        CHECK(f.On(copy, "getOtherPss", "(I)I", {VmValue::Int(0)}).AsInt() == 4);
        f.On(parcel, "recycle", "()V");
        f.context->process_memory_snapshot->heaps[1][0] = -1;
        const auto invalid = query(info);
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 90);
        const auto null = query(VmObjectRef{});
        REQUIRE(null.exception.IsValid());
        CHECK(f.linker.Class(null.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto model = MakeVirtualPrivateProcessMemorySnapshot(1025, 8192);
        CHECK(model.basis == GuestProcessMemoryBasis::virtual_private_committed);
        CHECK(model.heaps[0][0] == 2);
        CHECK(model.heaps[2][0] == 8);
        CHECK_THROWS(static_cast<void>(MakeVirtualPrivateProcessMemorySnapshot(UINT64_MAX, 0)));
        auto incoherent = model;
        incoherent.heaps[0][2] = 3;
        CHECK_THROWS(ValidateGuestProcessMemorySnapshot(incoherent));
        f.context->process_memory_snapshot = model;
        REQUIRE_FALSE(query(info).exception.IsValid());
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 10);
        CHECK(f.On(info, "getTotalUss", "()I").AsInt() == 10);
        CHECK(f.On(info, "getTotalSwappablePss", "()I").AsInt() == 0);
        CHECK(f.On(info, "getOtherPss", "(I)I", {VmValue::Int(0)}).AsInt() == 0);
        f.context->process_memory_snapshot.reset();
        int queries = 0;
        f.context->read_process_memory = [&] {
            ++queries;
            return std::optional{model};
        };
        REQUIRE_FALSE(query(info).exception.IsValid());
        CHECK(queries == 1);
        f.context->read_process_memory = []() -> std::optional<GuestProcessMemorySnapshot> { return std::nullopt; };
        const auto busy = query(info);
        REQUIRE(busy.exception.IsValid());
        CHECK(f.linker.Class(busy.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 10);
        f.context->read_process_memory = []() -> std::optional<GuestProcessMemorySnapshot> {
            return MakeVirtualPrivateProcessMemorySnapshot(UINT64_MAX, 0);
        };
        const auto overflow = query(info);
        REQUIRE(overflow.exception.IsValid());
        CHECK(f.linker.Class(overflow.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        CHECK(f.On(info, "getTotalPss", "()I").AsInt() == 10);
        auto excessive = model;
        excessive.heaps[0][3] = excessive.heaps[1][3] = INT32_MAX;
        CHECK_THROWS(ValidateGuestProcessMemorySnapshot(excessive));
    }
}

TEST_CASE("DVM-222 NFC discovery preserves API19 absence without transport") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t callbacks{};
        auto listener = IntrinsicClassBuilder::Class("Ltest/NdefClient;", "Ljava/lang/Object;",
                                                     {"Landroid/nfc/NfcAdapter$CreateNdefMessageCallback;",
                                                      "Landroid/nfc/NfcAdapter$OnNdefPushCompleteCallback;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("createNdefMessage", "(Landroid/nfc/NfcEvent;)Landroid/nfc/NdefMessage;",
                               [&callbacks](IntrinsicContext&) {
                                   ++callbacks;
                                   return VmValue::Ref(VmObjectRef{});
                               });
        listener.VirtualMethod("onNdefPushComplete", "(Landroid/nfc/NfcEvent;)V", [&callbacks](IntrinsicContext&) {
            ++callbacks;
            return VmValue::Void();
        });
        auto mock = IntrinsicClassBuilder::Class("Ltest/NoApplicationContext;", "Landroid/content/Context;");
        mock.OverrideMethod("getApplicationContext", "()Landroid/content/Context;",
                            [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
        AndroidValueVm f(backend, {std::move(listener).Build(), std::move(mock).Build()});
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        for (const auto* descriptor : {"Landroid/nfc/NfcAdapter;", "Landroid/nfc/NfcManager;", "Landroid/nfc/NfcEvent;",
                                       "Landroid/nfc/NdefMessage;", "Landroid/nfc/NdefRecord;"})
            CHECK(f.linker.Class(f.linker.ResolveDescriptor(descriptor)).is_boot_dex);
        const auto client = f.New("Ltest/NdefClient;");
        const auto base = f.New("Landroid/content/Context;");
        const auto roots = f.vm.ProtectReferences(std::array{client, base});
        for (const auto* descriptor : {"Landroid/nfc/NfcAdapter$CreateNdefMessageCallback;",
                                       "Landroid/nfc/NfcAdapter$OnNdefPushCompleteCallback;"}) {
            const auto type = f.linker.ResolveDescriptor(descriptor);
            CHECK(f.linker.Class(type).is_boot_dex);
            CHECK(f.linker.Class(type).is_interface);
            CHECK(f.linker.IsAssignable(type, f.model.ObjectClass(client)));
        }
        const auto service = [&] {
            return f
                .On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    {VmValue::Ref(f.vm.NewStringUtf8("nfc"))})
                .ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).descriptor == "Landroid/nfc/NfcManager;");
        CHECK_FALSE(f.On(manager, "getDefaultAdapter", "()Landroid/nfc/NfcAdapter;").ref.IsValid());
        CHECK_FALSE(f.Static("Landroid/nfc/NfcAdapter;", "getDefaultAdapter",
                             "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;", {VmValue::Ref(base)})
                        .ref.IsValid());
        const auto pm = f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        for (const auto* feature : {"android.hardware.nfc", "android.hardware.nfc.hce"})
            CHECK(f.On(pm, "hasSystemFeature", "(Ljava/lang/String;)Z", {VmValue::Ref(f.vm.NewStringUtf8(feature))})
                      .AsInt() == 0);
        const auto no_application = f.vm.NewIntrinsicInstance("Ltest/NoApplicationContext;");
        for (const auto context : {VmObjectRef{}, no_application}) {
            const auto result =
                f.StaticOutcome("Landroid/nfc/NfcAdapter;", "getDefaultAdapter",
                                "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;", {VmValue::Ref(context)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        }
        const auto legacy =
            f.StaticOutcome("Landroid/nfc/NfcAdapter;", "getDefaultAdapter", "()Landroid/nfc/NfcAdapter;");
        REQUIRE(legacy.exception.IsValid());
        CHECK(f.linker.Class(legacy.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        // Inject an otherwise unobtainable receiver to verify that registration
        // never silently succeeds or retains a callback outside the device path.
        const auto adapter = f.vm.NewIntrinsicInstance("Landroid/nfc/NfcAdapter;");
        for (
            const auto& [name, signature] : std::array{
                std::pair{
                    "setNdefPushMessageCallback",
                    "(Landroid/nfc/NfcAdapter$CreateNdefMessageCallback;Landroid/app/Activity;[Landroid/app/Activity;)V"},
                std::pair{
                    "setOnNdefPushCompleteCallback",
                    "(Landroid/nfc/NfcAdapter$OnNdefPushCompleteCallback;Landroid/app/Activity;[Landroid/app/Activity;)V"}}) {
            const auto result =
                f.OnOutcome(adapter, name, signature,
                            {VmValue::Ref(client), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        }
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.nfc_transport");
        CHECK(hits[0].count == 2);
        static_cast<void>(f.vm.CollectGarbage("nfc-no-device"));
        CHECK(service() == manager);
        CHECK_FALSE(f.On(manager, "getDefaultAdapter", "()Landroid/nfc/NfcAdapter;").ref.IsValid());
        CHECK(callbacks == 0);
    }
}

TEST_CASE("DVM-341 USB manager preserves API19 absence without transport") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.vm.SetGcIntegration(
            {{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
        const auto base = f.New("Landroid/content/Context;");
        const auto roots = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f
                .On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                    {VmValue::Ref(f.vm.NewStringUtf8("usb"))})
                .ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(service() == manager);
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).descriptor == "Landroid/hardware/usb/UsbManager;");
        const auto devices = f.On(manager, "getDeviceList", "()Ljava/util/HashMap;").ref;
        REQUIRE(devices.IsValid());
        CHECK(f.On(devices, "size", "()I").AsInt() == 0);
        CHECK_FALSE(f.On(manager, "getAccessoryList", "()[Landroid/hardware/usb/UsbAccessory;").ref.IsValid());
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto* type : {"UsbDevice", "UsbAccessory"}) {
            const auto argument = std::string("(Landroid/hardware/usb/") + type + ";";
            CHECK(f.On(manager, "hasPermission", (argument + ")Z").c_str(), {VmValue::Ref(VmObjectRef{})}).AsInt() ==
                  0);
            const auto denied =
                f.OnOutcome(manager, "requestPermission", (argument + "Landroid/app/PendingIntent;)V").c_str(),
                            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
            REQUIRE(denied.exception.IsValid());
            CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        }
        const auto denied = f.OnOutcome(manager, "setCurrentFunction", "(Ljava/lang/String;Z)V",
                                        {VmValue::Ref(f.vm.NewStringUtf8("mtp")), VmValue::Int(1)});
        REQUIRE(denied.exception.IsValid());
        CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        for (const auto& [method, signature] : std::array{
                 std::pair{"openAccessory", "(Landroid/hardware/usb/UsbAccessory;)Landroid/os/ParcelFileDescriptor;"},
                 std::pair{"setMassStorageBackingFile", "(Ljava/lang/String;)V"}}) {
            const auto result = f.OnOutcome(manager, method, signature, {VmValue::Ref(VmObjectRef{})});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        }
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.usb_transport");
        CHECK(hits[0].count == 7);
        const auto property =
            f.Static("Landroid/os/SystemProperties;", "get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                     {VmValue::Ref(f.vm.NewStringUtf8("sys.usb.config")), VmValue::Ref(f.vm.NewStringUtf8(""))})
                .ref;
        const auto functions = f.vm.StringUtf8(property);
        for (const auto* function : {"mtp", "adb", "rndis", "mt", ""}) {
            const auto pos = functions.find(function);
            const auto end = pos == std::string::npos ? pos : pos + std::string_view(function).size();
            const bool expected = pos != std::string::npos && (pos == 0 || functions[pos - 1] == ',') &&
                                  (end == functions.size() || functions[end] == ',');
            CHECK(f.On(manager, "isFunctionEnabled", "(Ljava/lang/String;)Z",
                       {VmValue::Ref(f.vm.NewStringUtf8(function))})
                      .AsInt() == expected);
        }
        const auto defaults =
            f.Static("Landroid/os/SystemProperties;", "get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                     {VmValue::Ref(f.vm.NewStringUtf8("persist.sys.usb.config")), VmValue::Ref(f.vm.NewStringUtf8(""))})
                .ref;
        const auto default_functions = f.vm.StringUtf8(defaults);
        const auto comma = default_functions.find(',');
        CHECK(f.vm.StringUtf8(f.On(manager, "getDefaultFunction", "()Ljava/lang/String;").ref) ==
              (comma > 0 ? default_functions.substr(0, comma) : default_functions));
        static_cast<void>(f.vm.CollectGarbage("usb-no-device"));
        CHECK(service() == manager);
        CHECK(f.On(f.On(manager, "getDeviceList", "()Ljava/util/HashMap;").ref, "size", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-136 API 19 OrientationEventListener preserves absent sensor semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t orientation_callbacks{};
        auto listener =
            IntrinsicClassBuilder::Class("Ltest/OrientationListener;", "Landroid/view/OrientationEventListener;");
        listener.OverrideMethod("onOrientationChanged", "(I)V", [&orientation_callbacks](IntrinsicContext&) {
            ++orientation_callbacks;
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(listener).Build()});

        const auto orientation_type = fixture.linker.ResolveDescriptor("Landroid/view/OrientationEventListener;");
        CHECK(fixture.linker.Class(orientation_type).is_boot_dex);
        const auto implementation_type =
            fixture.linker.ResolveDescriptor("Landroid/view/OrientationEventListener$SensorEventListenerImpl;");
        CHECK(fixture.linker.Class(implementation_type).is_boot_dex);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto object = fixture.vm.NewIntrinsicInstance("Ltest/OrientationListener;");
        const auto constructor =
            fixture.linker.FindDirectMethod(orientation_type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor.has_value());
        const std::array constructor_arguments{VmValue::Ref(object), VmValue::Ref(context)};
        const auto constructed = fixture.vm.Call(*constructor, constructor_arguments);
        REQUIRE_MESSAGE(!constructed.exception.IsValid(), constructed.exception_message);

        CHECK(fixture.On(object, "canDetectOrientation", "()Z").AsInt() == 0);
        static_cast<void>(fixture.On(object, "enable", "()V"));
        static_cast<void>(fixture.On(object, "enable", "()V"));
        static_cast<void>(fixture.On(object, "disable", "()V"));
        CHECK(orientation_callbacks == 0);
    }
}

TEST_CASE("DVM-156 location facade links listeners and exposes no location source") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto listener = IntrinsicClassBuilder::Class("Ltest/LocationListener;", "Ljava/lang/Object;",
                                                     {"Landroid/location/LocationListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("onLocationChanged", "(Landroid/location/Location;)V",
                               [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("onStatusChanged", "(Ljava/lang/String;ILandroid/os/Bundle;)V",
                               [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("onProviderEnabled", "(Ljava/lang/String;)V",
                               [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("onProviderDisabled", "(Ljava/lang/String;)V",
                               [](IntrinsicContext&) { return VmValue::Void(); });

        AndroidValueVm fixture(backend, {std::move(listener).Build()});
        const auto context = fixture.New("Landroid/content/Context;");
        const auto service_name = fixture.vm.NewStringUtf8("location");
        const auto manager =
            fixture
                .On(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)})
                .ref;
        REQUIRE(manager.IsValid());
        CHECK(
            fixture
                .On(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)})
                .ref == manager);
        CHECK(fixture.model.ObjectClass(manager) ==
              fixture.linker.ResolveDescriptor("Landroid/location/LocationManager;"));

        const auto criteria = fixture.New("Landroid/location/Criteria;");
        CHECK_FALSE(fixture
                        .On(manager, "getBestProvider", "(Landroid/location/Criteria;Z)Ljava/lang/String;",
                            {VmValue::Ref(criteria), VmValue::Int(1)})
                        .ref.IsValid());
        const auto provider = fixture.vm.NewStringUtf8("gps");
        CHECK_FALSE(fixture
                        .On(manager, "getLastKnownLocation", "(Ljava/lang/String;)Landroid/location/Location;",
                            {VmValue::Ref(provider)})
                        .ref.IsValid());

        const auto callback = fixture.New("Ltest/LocationListener;");
        const auto request = fixture.OnOutcome(manager, "requestLocationUpdates",
                                               "(Ljava/lang/String;JFLandroid/location/LocationListener;"
                                               "Landroid/os/Looper;)V",
                                               {VmValue::Ref(provider), VmValue::Long(0), VmValue::Float(0.0F),
                                                VmValue::Ref(callback), VmValue::Ref(VmObjectRef{})});
        REQUIRE(request.exception.IsValid());
        CHECK(fixture.linker.Class(request.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto remove = fixture.OnOutcome(manager, "removeUpdates", "(Landroid/location/LocationListener;)V",
                                              {VmValue::Ref(callback)});
        REQUIRE(remove.exception.IsValid());
        CHECK(fixture.linker.Class(remove.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto hits = fixture.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.location_updates");
        CHECK(hits[0].count == 2);
    }
}

TEST_CASE("DVM-166 keyguard facade reads the replaceable platform snapshot") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto context = fixture.New("Landroid/content/Context;");
        const auto service_name = fixture.vm.NewStringUtf8("keyguard");
        const auto manager =
            fixture
                .On(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)})
                .ref;
        REQUIRE(manager.IsValid());
        CHECK(fixture.model.ObjectClass(manager) == fixture.linker.ResolveDescriptor("Landroid/app/KeyguardManager;"));
        CHECK(fixture.On(manager, "isKeyguardLocked", "()Z").AsInt() == 0);
        CHECK(fixture.On(manager, "isKeyguardSecure", "()Z").AsInt() == 0);
        CHECK(fixture.On(manager, "inKeyguardRestrictedInputMode", "()Z").AsInt() == 0);

        const auto second =
            fixture
                .On(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)})
                .ref;
        CHECK(second.IsValid());
        CHECK(second != manager);

        fixture.context->keyguard_state_provider = [] {
            return AndroidKeyguardState{.locked = true, .secure = true, .restricted_input = true};
        };
        CHECK(fixture.On(manager, "isKeyguardLocked", "()Z").AsInt() == 1);
        CHECK(fixture.On(manager, "isKeyguardSecure", "()Z").AsInt() == 1);
        CHECK(fixture.On(manager, "inKeyguardRestrictedInputMode", "()Z").AsInt() == 1);
    }
}

TEST_CASE("DVM-133 ContentResolver query returns null when no provider exists") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto resolver = fixture.vm.NewIntrinsicInstance("Landroid/content/ContentResolver;");
        const auto uri_text = fixture.vm.NewStringUtf8("content://com.facebook.katana.provider.AttributionIdProvider");
        const auto uri =
            fixture
                .Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(uri_text)})
                .ref;
        constexpr auto signature = "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;"
                                   "[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;";
        const std::vector<VmValue> absent_query{VmValue::Ref(uri), VmValue::Ref(VmObjectRef{}),
                                                VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                                                VmValue::Ref(VmObjectRef{})};
        CHECK_FALSE(fixture.On(resolver, "query", signature, absent_query).ref.IsValid());

        auto null_query = absent_query;
        null_query[0] = VmValue::Ref(VmObjectRef{});
        const auto outcome = fixture.OnOutcome(resolver, "query", signature, std::move(null_query));
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Activity top-level identity reports that it is not a child") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto activity = fixture.New("Landroid/app/Activity;");
        CHECK(fixture.On(activity, "isChild", "()Z").AsInt() == 0);
    }
}

TEST_CASE("DVM-241 offline connectivity distinguishes unsupported types and preserves original values") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->granted_permissions.insert("android.permission.ACCESS_NETWORK_STATE");
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto service_name = f.vm.NewStringUtf8("connectivity");
        const auto manager =
            f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)}).ref;
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        const auto query = [&](int type) {
            return f.OnOutcome(manager, "getNetworkInfo", "(I)Landroid/net/NetworkInfo;", {VmValue::Int(type)});
        };
        const auto fail = [&](const VmCallOutcome& outcome, const char* exception) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == exception);
        };
        for (const int type : {-1, 0, 9, 14, 15}) {
            const auto result = query(type);
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK_FALSE(result.value.ref.IsValid());
        }
        CHECK(f.Static("Landroid/net/ConnectivityManager;", "isNetworkTypeValid", "(I)Z", {VmValue::Int(14)}).AsInt() ==
              1);
        CHECK(f.Static("Landroid/net/ConnectivityManager;", "isNetworkTypeValid", "(I)Z", {VmValue::Int(15)}).AsInt() ==
              0);
        const auto result = query(1);
        REQUIRE_FALSE(result.exception.IsValid());
        REQUIRE(result.value.ref.IsValid());
        const auto info = result.value.ref;
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        CHECK(f.linker.Class(f.model.ObjectClass(info)).is_boot_dex);
        CHECK_FALSE(f.On(info, "isConnected", "()Z").AsInt());
        CHECK_FALSE(f.On(info, "isConnectedOrConnecting", "()Z").AsInt());
        CHECK_FALSE(f.On(info, "isAvailable", "()Z").AsInt());
        CHECK(f.On(info, "getType", "()I").AsInt() == 1);
        CHECK(f.On(info, "getSubtype", "()I").AsInt() == 0);
        CHECK(f.vm.StringUtf8(f.On(info, "getTypeName", "()Ljava/lang/String;").ref) == "WIFI");
        const auto state = f.On(info, "getState", "()Landroid/net/NetworkInfo$State;").ref;
        CHECK(f.vm.StringUtf8(f.On(state, "name", "()Ljava/lang/String;").ref) == "DISCONNECTED");
        CHECK(f.On(info, "getReason", "()Ljava/lang/String;").ref == VmObjectRef{});
        const auto copy = f.New("Landroid/net/NetworkInfo;", "(Landroid/net/NetworkInfo;)V", {VmValue::Ref(info)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(copy != info);
        CHECK_FALSE(f.On(copy, "isConnected", "()Z").AsInt());
        const auto connected = f.Static("Landroid/net/NetworkInfo$DetailedState;", "valueOf",
                                        "(Ljava/lang/String;)Landroid/net/NetworkInfo$DetailedState;",
                                        {VmValue::Ref(f.vm.NewStringUtf8("CONNECTED"))})
                                   .ref;
        f.On(copy, "setDetailedState", "(Landroid/net/NetworkInfo$DetailedState;Ljava/lang/String;Ljava/lang/String;)V",
             {VmValue::Ref(connected), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(copy, "isConnected", "()Z").AsInt() == 1);
        CHECK_FALSE(f.On(info, "isConnected", "()Z").AsInt());
        const auto fresh = query(1);
        REQUIRE_FALSE(fresh.exception.IsValid());
        CHECK(fresh.value.ref != info);
        const auto fresh_root = f.vm.ProtectReferences(std::array{fresh.value.ref});
        CHECK_FALSE(f.On(fresh.value.ref, "isConnected", "()Z").AsInt());
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        f.On(info, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto info_type = f.model.ObjectClass(info);
        const auto creator_field = f.linker.FindFieldRecursive(info_type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef{f.linker.Class(info_type).static_storage[f.linker.Field(*creator_field).slot]};
        const auto roundtrip =
            f.On(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;", {VmValue::Ref(parcel)}).ref;
        const auto roundtrip_root = f.vm.ProtectReferences(std::array{roundtrip});
        CHECK_FALSE(f.On(roundtrip, "isConnected", "()Z").AsInt());
        CHECK(f.On(roundtrip, "getState", "()Landroid/net/NetworkInfo$State;").ref == state);
        CHECK(f.On(roundtrip, "getType", "()I").AsInt() == 1);
        f.On(parcel, "recycle", "()V");
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.vm.MarkReachable().IsMarked(info));
        CHECK_FALSE(f.On(manager, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;").ref.IsValid());
        const auto wifi = f.New("Landroid/net/wifi/WifiManager;");
        const auto wifi_root = f.vm.ProtectReferences(std::array{wifi});
        CHECK_FALSE(f.On(wifi, "isWifiEnabled", "()Z").AsInt());
        CHECK(f.On(wifi, "getWifiState", "()I").AsInt() == 1);
        CHECK_FALSE(f.On(wifi, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;").ref.IsValid());
        CHECK_THROWS_AS(static_cast<void>(f.vm.Network().Resolve("offline.test")), NetworkRuntimeError);
        f.context->offline_connectivity->wifi_type_supported = false;
        const auto unsupported = query(1);
        REQUIRE_FALSE(unsupported.exception.IsValid());
        CHECK_FALSE(unsupported.value.ref.IsValid());
        f.context->offline_connectivity.reset();
        fail(query(1), "Ljava/lang/UnsupportedOperationException;");
        fail(f.OnOutcome(wifi, "getWifiState", "()I"), "Ljava/lang/UnsupportedOperationException;");
        f.context->offline_connectivity = AndroidOfflineConnectivity{};
        f.context->network_policy.enabled = true;
        fail(query(1), "Ljava/lang/UnsupportedOperationException;");
        f.context->network_policy.enabled = false;
        NetworkPolicy online;
        online.enabled = true;
        f.vm.Network().Configure(std::move(online), nullptr);
        fail(query(1), "Ljava/lang/UnsupportedOperationException;");
        f.vm.Network().Configure({}, nullptr);
        f.context->granted_permissions.clear();
        fail(query(1), "Ljava/lang/SecurityException;");
        fail(f.OnOutcome(manager, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;"),
             "Ljava/lang/SecurityException;");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.connectivity_offline");
    }
}

TEST_CASE("WU7-4 ActivityManager memory classes share the VM heap limits in both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto manager = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{manager});
        const auto memory = f.On(manager, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                 {VmValue::Ref(f.vm.NewStringUtf8("activity"))})
                                .ref;
        REQUIRE(memory.IsValid());
        const auto mr = f.vm.ProtectReferences(std::array{memory});
        CHECK(f.On(memory, "getMemoryClass", "()I").AsInt() ==
              static_cast<std::int32_t>(f.model.HeapGrowthLimitBytes() / (1024U * 1024U)));
        CHECK(f.On(memory, "getLargeMemoryClass", "()I").AsInt() ==
              static_cast<std::int32_t>(f.model.MaximumHeapBytes() / (1024U * 1024U)));
        CHECK(f.Static("Landroid/app/ActivityManager;", "staticGetMemoryClass", "()I").AsInt() ==
              f.On(memory, "getMemoryClass", "()I").AsInt());
        CHECK(f.Static("Landroid/app/ActivityManager;", "staticGetLargeMemoryClass", "()I").AsInt() ==
              f.On(memory, "getLargeMemoryClass", "()I").AsInt());
    }
}

TEST_CASE("DVM-264 DEX platform service registry preserves supported absent and unsupported results") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto context = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto roots = f.vm.ProtectReferences(std::array{context});
        const auto service = [&](const char* name) {
            return f.StaticOutcome("Lfixture/PlatformTables;", "service",
                                   "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/Object;",
                                   {VmValue::Ref(context), VmValue::Ref(f.vm.NewStringUtf8(name))});
        };
        const auto display = service("display");
        REQUIRE_FALSE(display.exception.IsValid());
        REQUIRE(display.value.ref.IsValid());
        const auto display_root = f.vm.ProtectReferences(std::array{display.value.ref});
        CHECK(f.linker.Class(f.model.ObjectClass(display.value.ref)).descriptor ==
              "Landroid/hardware/display/DisplayManager;");
        CHECK(service("display").value.ref == display.value.ref);
        const auto screen = f.Static("Lfixture/PlatformTables;", "display",
                                     "(Landroid/hardware/display/DisplayManager;I)Landroid/view/Display;",
                                     {display.value, VmValue::Int(0)})
                                .ref;
        REQUIRE(screen.IsValid());
        CHECK(f.On(screen, "getDisplayId", "()I").AsInt() == 0);
        CHECK(f.On(screen, "getWidth", "()I").AsInt() == static_cast<int>(f.context->surface_width));
        const auto window = service("window");
        CHECK(f.On(window.value.ref, "getDefaultDisplay", "()Landroid/view/Display;").ref == screen);
        const auto all = f.On(display.value.ref, "getDisplays", "()[Landroid/view/Display;").ref;
        CHECK(f.model.ArrayLength(all) == 1);
        CHECK(f.model.GetObjectElement(all, 0) == screen);
        const auto presentation =
            f.On(display.value.ref, "getDisplays", "(Ljava/lang/String;)[Landroid/view/Display;",
                 {VmValue::Ref(f.vm.NewStringUtf8("android.hardware.display.category.PRESENTATION"))})
                .ref;
        CHECK(f.model.ArrayLength(presentation) == 0);
        CHECK_FALSE(f.Static("Lfixture/PlatformTables;", "display",
                             "(Landroid/hardware/display/DisplayManager;I)Landroid/view/Display;",
                             {display.value, VmValue::Int(1)})
                        .ref.IsValid());
        const auto absent = service("ogplay.nonexistent.service");
        REQUIRE_FALSE(absent.exception.IsValid());
        CHECK_FALSE(absent.value.ref.IsValid());
        const auto unsupported = service("account");
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(unsupported.exception)).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK(unsupported.exception_message.find("account") != std::string::npos);
    }
}

TEST_CASE("DVM-264 DEX System properties include the frozen ROM keys and match guest uname") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto os = f.vm.NewIntrinsicInstance("Llibcore/io/Posix;");
        const auto roots = f.vm.ProtectReferences(std::array{os});
        const auto identity = f.Static("Lfixture/PlatformTables;", "uname",
                                       "(Llibcore/io/Posix;)Llibcore/io/StructUtsname;", {VmValue::Ref(os)})
                                  .ref;
        const auto identity_root = f.vm.ProtectReferences(std::array{identity});
        for (const auto& [key, field] : std::array{std::pair{"os.name", "sysname"}, std::pair{"os.arch", "machine"},
                                                   std::pair{"os.version", "release"}}) {
            const auto value =
                f.Static("Lfixture/PlatformTables;", "property", "(Ljava/lang/String;)Ljava/lang/String;",
                         {VmValue::Ref(f.vm.NewStringUtf8(key))})
                    .ref;
            REQUIRE(value.IsValid());
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(identity), field, "Ljava/lang/String;");
            REQUIRE(id.has_value());
            const auto expected = VmObjectRef(f.model.InstanceSlots(identity)[f.linker.Field(*id).slot].bits);
            CHECK(f.vm.StringUtf8(value) == f.vm.StringUtf8(expected));
        }
        const auto properties = f.Static("Lfixture/PlatformTables;", "properties", "()Ljava/util/Properties;").ref;
        const auto property_root = f.vm.ProtectReferences(std::array{properties});
        // This list is extracted independently from the checked-in ROM evidence, not the runtime table.
        const auto keys = std::array{
            "android.icu.cldr.version",
            "android.icu.library.version",
            "android.icu.unicode.version",
            "file.encoding",
            "file.separator",
            "java.boot.class.path",
            "java.class.path",
            "java.class.version",
            "java.compiler",
            "java.ext.dirs",
            "java.home",
            "java.io.tmpdir",
            "java.library.path",
            "java.runtime.name",
            "java.runtime.version",
            "java.specification.name",
            "java.specification.vendor",
            "java.specification.version",
            "java.vendor",
            "java.vendor.url",
            "java.version",
            "java.vm.name",
            "java.vm.specification.name",
            "java.vm.specification.vendor",
            "java.vm.specification.version",
            "java.vm.vendor",
            "java.vm.vendor.url",
            "java.vm.version",
            "line.separator",
            "os.arch",
            "os.name",
            "os.version",
            "path.separator",
            "user.home",
            "user.language",
            "user.name",
            "user.region",
        };
        for (const auto* key : keys) {
            INFO(key);
            CHECK(f.On(properties, "containsKey", "(Ljava/lang/Object;)Z", {VmValue::Ref(f.vm.NewStringUtf8(key))})
                      .AsInt() == 1);
        }
        // getProperties exposes the same live Properties object as get/setProperty.
        f.On(properties, "setProperty", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;",
             {VmValue::Ref(f.vm.NewStringUtf8("ogplay.live")), VmValue::Ref(f.vm.NewStringUtf8("updated"))});
        const auto live = f.Static("Lfixture/PlatformTables;", "property", "(Ljava/lang/String;)Ljava/lang/String;",
                                   {VmValue::Ref(f.vm.NewStringUtf8("ogplay.live"))})
                              .ref;
        CHECK(f.vm.StringUtf8(live) == "updated");
        static_cast<void>(f.vm.CollectGarbage("platform-properties"));
        CHECK(f.Static("Lfixture/PlatformTables;", "properties", "()Ljava/util/Properties;").ref == properties);
    }
}

TEST_CASE("DVM-266 AppGlobals keeps the ROM ActivityThread dependency explicit") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto app = f.linker.ResolveDescriptor("Landroid/app/AppGlobals;");
        CHECK(f.linker.Class(app).is_boot_dex);
        const auto result = f.StaticOutcome("Lfixture/PlatformTables;", "coreSetting", "(Ljava/lang/String;I)I",
                                            {VmValue::Ref(f.vm.NewStringUtf8("missing")), VmValue::Int(37)});
        // AOSP returns the default only after currentActivityThread() returns null.
        // That method is still a skeleton; do not invent an absent-thread result.
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id ==
                       "dexvm.intrinsic.Landroid/app/ActivityThread;.currentActivityThread()Landroid/app/ActivityThread;" &&
                   hit.count == 1;
        }));
    }
}

TEST_CASE("DVM-267 ROM Handler subclass receives a message through Looper") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto looper = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
        const auto listener = f.New("Lfixture/RomHandlerProbe;");
        const auto base = f.New("Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
        const auto handler = f.New("Landroid/hardware/input/InputManager$InputDeviceListenerDelegate;",
                                   "(Landroid/hardware/input/InputManager$InputDeviceListener;Landroid/os/Handler;)V",
                                   {VmValue::Ref(listener), VmValue::Ref(base)});
        f.Static("Lfixture/RomHandlerProbe;", "send", "(Landroid/os/Handler;)V", {VmValue::Ref(handler)});
        CHECK(f.Static("Lfixture/RomHandlerProbe;", "result", "()I").AsInt() == 0);
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(f.Static("Lfixture/RomHandlerProbe;", "result", "()I").AsInt() == 73);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-267 ROM BroadcastReceiver subclass constructs and dispatches onReceive") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "platform_tables.dex", true);
        const auto object = f.New("Landroid/media/MediaRouter$VolumeChangeReceiver;");
        const auto receiver =
            f.Static("Lfixture/RomReceiverProbe;", "receive",
                     "(Landroid/content/BroadcastReceiver;)Landroid/content/BroadcastReceiver;", {VmValue::Ref(object)})
                .ref;
        CHECK(receiver.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(receiver)).descriptor ==
              "Landroid/media/MediaRouter$VolumeChangeReceiver;");
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-332 ContentObserver process tree dispatch and Settings changes") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        struct Event {
            VmObjectRef observer;
            bool self;
            VmObjectRef uri;
        };

        std::vector<Event> events;
        bool wants_self = false;
        auto probe = IntrinsicClassBuilder::Class("Ltest/ContentObserverProbe;", "Landroid/database/ContentObserver;");
        probe.InstanceField("mHandler", "Landroid/os/Handler;");  // A subclass shadow must not hide the ROM field.
        probe.Constructor("(Landroid/os/Handler;)V", [](IntrinsicContext& call) {
            const auto type = call.vm.Linker().ResolveDescriptor("Landroid/database/ContentObserver;");
            const auto ctor = call.vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/os/Handler;)V");
            const std::array args{VmValue::Ref(call.receiver), call.arguments[0]};
            const auto result = call.vm.Call(*ctor, args);
            if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
            return VmValue::Void();
        });
        probe.OverrideMethod("deliverSelfNotifications", "()Z",
                             [&](IntrinsicContext&) { return VmValue::Int(wants_self); });
        probe.OverrideMethod("onChange", "(ZLandroid/net/Uri;)V", [&](IntrinsicContext& call) {
            events.push_back({call.receiver, call.arguments[0].AsInt() != 0, call.arguments[1].ref});
            return VmValue::Void();
        });
        AndroidValueVm f(backend, {std::move(probe).Build()});
        const auto resolver = f.vm.NewIntrinsicInstance("Landroid/content/ContentResolver;");
        const auto resolver_root = f.vm.ProtectReferences(std::array{resolver});
        const auto looper = f.Static("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;").ref;
        const auto handler = f.New("Landroid/os/Handler;", "(Landroid/os/Looper;)V", {VmValue::Ref(looper)});
        const auto handler_root = f.vm.ProtectReferences(std::array{handler});
        const auto make_observer = [&](VmObjectRef h) {
            return f.New("Ltest/ContentObserverProbe;", "(Landroid/os/Handler;)V", {VmValue::Ref(h)});
        };
        const auto parent = make_observer(handler);
        const auto exact = make_observer(VmObjectRef{});
        const auto child = make_observer(handler);
        const auto observer_roots = f.vm.ProtectReferences(std::array{parent, exact, child});
        const auto uri = [&](const char* text) {
            return f
                .Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                        {VmValue::Ref(f.vm.NewStringUtf8(text))})
                .ref;
        };
        const auto base_uri = uri("content://settings/system");
        const auto leaf_uri = uri("content://settings/system/accelerometer_rotation");
        const auto child_uri = uri("content://settings/system/accelerometer_rotation/child");
        const auto uris_root = f.vm.ProtectReferences(std::array{base_uri, leaf_uri, child_uri});
        constexpr auto reg_sig = "(Landroid/net/Uri;ZLandroid/database/ContentObserver;)V";
        constexpr auto unreg_sig = "(Landroid/database/ContentObserver;)V";
        const auto reg = [&](VmObjectRef u, bool descendants, VmObjectRef o) {
            f.On(resolver, "registerContentObserver", reg_sig,
                 {VmValue::Ref(u), VmValue::Int(descendants), VmValue::Ref(o)});
        };
        const auto unreg = [&](VmObjectRef o) {
            f.On(resolver, "unregisterContentObserver", unreg_sig, {VmValue::Ref(o)});
        };
        const auto notify = [&](VmObjectRef u, VmObjectRef sender = VmObjectRef{}) {
            events.clear();
            f.On(resolver, "notifyChange", "(Landroid/net/Uri;Landroid/database/ContentObserver;Z)V",
                 {VmValue::Ref(u), VmValue::Ref(sender), VmValue::Int(0)});
            CHECK(events.empty());  // Both Handler and no-Handler observers are asynchronous.
            CHECK_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        };
        const auto count = [&](VmObjectRef o) {
            return std::count_if(events.begin(), events.end(), [&](const Event& e) { return e.observer == o; });
        };
        reg(base_uri, false, parent);
        reg(leaf_uri, false, exact);
        reg(child_uri, false, child);
        notify(leaf_uri);
        CHECK(count(parent) == 0);
        CHECK(count(exact) == 1);
        CHECK(count(child) == 1);  // Descendants receive ancestor notifications regardless of flag.
        for (const auto& e : events) {
            CHECK_FALSE(e.self);
            CHECK(e.uri == leaf_uri);
        }
        unreg(parent);
        reg(base_uri, true, parent);
        notify(child_uri);
        CHECK(count(parent) == 1);
        CHECK(count(exact) == 0);
        CHECK(count(child) == 1);
        notify(leaf_uri, exact);
        CHECK(count(exact) == 0);
        CHECK(count(parent) == 1);
        wants_self = true;
        notify(leaf_uri, exact);
        CHECK(count(exact) == 1);
        for (const auto& e : events)
            CHECK(e.self == (e.observer == exact));
        reg(leaf_uri, true, exact);  // Duplicate and multi-node registrations all disappear on unregister.
        reg(child_uri, false, exact);
        notify(leaf_uri);
        CHECK(count(exact) == 3);
        unreg(exact);
        unreg(exact);
        notify(leaf_uri);
        CHECK(count(exact) == 0);
        const auto fail = [&](const VmCallOutcome& result, const char* type) {
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == type);
        };
        fail(f.OnOutcome(resolver, "registerContentObserver", reg_sig,
                         {VmValue::Ref(leaf_uri), VmValue::Int(0), VmValue::Ref(VmObjectRef{})}),
             "Ljava/lang/NullPointerException;");
        fail(f.OnOutcome(resolver, "registerContentObserver", reg_sig,
                         {VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Ref(exact)}),
             "Ljava/lang/IllegalArgumentException;");
        fail(f.OnOutcome(resolver, "unregisterContentObserver", unreg_sig, {VmValue::Ref(VmObjectRef{})}),
             "Ljava/lang/NullPointerException;");
        reg(leaf_uri, false, exact);
        const auto retained = make_observer(VmObjectRef{});
        reg(leaf_uri, false, retained);
        static_cast<void>(f.vm.CollectGarbage("content-observer-static-registry"));
        const auto other_resolver = f.vm.NewIntrinsicInstance("Landroid/content/ContentResolver;");
        const auto other_root = f.vm.ProtectReferences(std::array{other_resolver});
        events.clear();
        f.On(other_resolver, "notifyChange", "(Landroid/net/Uri;Landroid/database/ContentObserver;)V",
             {VmValue::Ref(leaf_uri), VmValue::Ref(VmObjectRef{})});
        CHECK(events.empty());
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(count(retained) == 1);  // Only the Java static tree retained this observer across GC.
        f.On(other_resolver, "unregisterContentObserver", unreg_sig, {VmValue::Ref(retained)});
        notify(VmObjectRef{});
        CHECK(count(parent) == 1);
        CHECK(count(exact) == 1);
        CHECK(count(child) == 1);
        CHECK(count(retained) == 0);
        CHECK(f.ledger.Unimplemented().empty());
        const auto put = [&](const char* table, const char* value) {
            events.clear();
            const auto result =
                f.Static(table, "putString", "(Landroid/content/ContentResolver;Ljava/lang/String;Ljava/lang/String;)Z",
                         {VmValue::Ref(resolver), VmValue::Ref(f.vm.NewStringUtf8("accelerometer_rotation")),
                          VmValue::Ref(value ? f.vm.NewStringUtf8(value) : VmObjectRef{})});
            CHECK(events.empty());
            CHECK_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
            return result.AsInt();
        };
        CHECK(put("Landroid/provider/Settings$System;", "1") == 1);
        CHECK(count(exact) == 1);
        REQUIRE_FALSE(events.empty());
        CHECK(f.On(events[0].uri, "toString", "()Ljava/lang/String;").ref.IsValid());
        CHECK(f.vm.StringUtf8(f.On(events[0].uri, "toString", "()Ljava/lang/String;").ref) ==
              "content://settings/system/accelerometer_rotation");
        CHECK(put("Landroid/provider/Settings$System;", "1") == 1);
        CHECK(events.empty());
        CHECK(put("Landroid/provider/Settings$System;", "0") == 1);
        CHECK(count(exact) == 1);
        CHECK(put("Landroid/provider/Settings$System;", nullptr) == 1);
        CHECK(count(exact) == 1);
        CHECK(put("Landroid/provider/Settings$System;", nullptr) == 1);
        CHECK(events.empty());
        CHECK(put("Landroid/provider/Settings$Secure;", "1") == 0);
        CHECK(events.empty());
        CHECK(put("Landroid/provider/Settings$Global;", "1") == 0);
        CHECK(events.empty());
        unreg(parent);
        unreg(exact);
        unreg(child);
        notify(base_uri);
        CHECK(events.empty());
    }
}
