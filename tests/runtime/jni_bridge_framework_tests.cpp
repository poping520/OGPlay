#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include <set>
#include "ogplay/runtime/integration/native_activity_runtime.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "runtime/integration/dexvm_android/shared.h"
#include "../fixtures/bitmap_decode_data.h"
#include <chrono>
#include <atomic>
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <future>
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include <thread>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"

#include "native_library_fixture.h"

using namespace ogplay::test::native_library;

TEST_CASE("Build and SystemProperties JNI use the BootDex owner") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        auto& classes = fixture.session->Classes();
        auto& fields = fixture.session->Fields();
        const auto read = [&](JniReference ref) {
            return vm.StringUtf8(bridge.FromReference(ref));
        };
        for (const auto name : {"android/os/Build", "android/os/Build$VERSION",
                                "android/os/Build$VERSION_CODES",
                                "android/os/SystemProperties"}) {
            const auto owner = linker.ResolveDescriptor("L" + std::string(name) + ";");
            CHECK(linker.Class(owner).is_boot_dex);
            CHECK(classes.FindClass(name) == bridge.RegisteredClassIdentity(owner));
        }
        const auto build = *classes.FindClass("android/os/Build");
        const auto owner = linker.ResolveDescriptor("Landroid/os/Build;");
        CHECK(linker.Class(owner).clinit_state == ClinitState::uninitialized);
        REQUIRE(fields.EnsureClassInitialized(build, 1U));
        for (const auto name : {"BRAND", "CPU_ABI", "TAGS", "SERIAL"}) {
            const auto jni_field = classes.GetFieldId(build, name, "Ljava/lang/String;", true);
            const auto dex_field = linker.FindFieldRecursive(owner, name, "Ljava/lang/String;");
            REQUIRE(jni_field.has_value());
            REQUIRE(dex_field.has_value());
            const auto ref = std::get<JniReference>(fields.GetStatic(build, *jni_field, 1U));
            CHECK(bridge.FromReference(ref).Value() ==
                  linker.Class(owner).static_storage[linker.Field(*dex_field).slot]);
        }
        const auto version = *classes.FindClass("android/os/Build$VERSION");
        REQUIRE(fields.EnsureClassInitialized(version, 1U));
        const auto release = *classes.GetFieldId(version, "RELEASE", "Ljava/lang/String;", true);
        CHECK(read(std::get<JniReference>(fields.GetStatic(version, release, 1U))) == "4.4.4");
        const auto props = *classes.FindClass("android/os/SystemProperties");
        const auto get = *classes.GetMethodId(props, "get", "(Ljava/lang/String;)Ljava/lang/String;", true);
        CHECK(classes.ResolveMethod(get).declaration.implementation.starts_with("dexvm.m"));
        const auto props_owner = linker.ResolveDescriptor("Landroid/os/SystemProperties;");
        const auto dex_get = *linker.FindDirectMethod(props_owner, "get", "(Ljava/lang/String;)Ljava/lang/String;");
        CHECK(linker.Method(dex_get).kind != MethodKind::intrinsic);
        const auto key = vm.NewStringUtf8("ro.build.version.release");
        const std::array<JniValue, 1> args{bridge.PublishLocal(key)};
        const auto result = std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get, args, JniArgumentSource::value_array));
        CHECK(read(result) == "4.4.4");
        const std::array dex_args{VmValue::Ref(key)};
        const auto direct = vm.Call(dex_get, dex_args);
        REQUIRE_FALSE(direct.exception.IsValid());
        CHECK(read(result) == vm.StringUtf8(direct.value.ref));
        static_cast<void>(vm.CollectGarbage("build-jni"));
        CHECK(read(result) == "4.4.4");
        CHECK(read(std::get<JniReference>(fields.GetStatic(version, release, 1U))) == "4.4.4");
        const auto get_default = *classes.GetMethodId(props, "get",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", true);
        const std::array<JniValue, 2> missing{
            bridge.PublishLocal(vm.NewStringUtf8("missing.property")),
            bridge.PublishLocal(vm.NewStringUtf8("fallback"))};
        CHECK(read(std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get_default, missing, JniArgumentSource::value_array))) == "fallback");
        const auto get_int = *classes.GetMethodId(props, "getInt", "(Ljava/lang/String;I)I", true);
        const std::array<JniValue, 2> sdk_args{
            bridge.PublishLocal(vm.NewStringUtf8("ro.build.version.sdk")), JniInt{-1}};
        CHECK(std::get<JniInt>(fixture.session->Invocations().InvokeStatic(
            1U, props, get_int, sdk_args, JniArgumentSource::value_array)) == 19);
        // This validation belongs to the original Java method, before native_get.
        const std::array<JniValue, 1> too_long{
            bridge.PublishLocal(vm.NewStringUtf8(std::string(32, 'x')))};
        const auto failed = std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get, too_long, JniArgumentSource::value_array));
        CHECK(failed.IsNull());
        auto& environment = fixture.session->Environment();
        REQUIRE(environment.ExceptionCheck(1U));
        const auto exception_ref = environment.ExceptionOccurred(1U);
        environment.ExceptionClear(1U);
        const auto exception = bridge.FromReference(exception_ref);
        CHECK(linker.Class(bridge.Model().ObjectClass(exception)).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
    }
}

TEST_CASE("DVM-206 JNI keyboard lookup resolves BootDex load and character queries") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        fixture.context->input_devices = {{-1, "keyboard", "fixture:keyboard", kAndroidKeyboardSource, 2, {}}};
        auto& bridge = *fixture.bridge;
        auto& classes = fixture.session->Classes();
        auto& invocations = fixture.session->Invocations();
        const auto identity = classes.FindClass("android/view/KeyCharacterMap");
        REQUIRE(identity.has_value());
        const auto load = classes.GetMethodId(*identity, "load", "(I)Landroid/view/KeyCharacterMap;", true);
        const auto get = classes.GetMethodId(*identity, "get", "(II)I", false);
        REQUIRE(load.has_value()); REQUIRE(get.has_value());
        const std::array<JniValue, 1> args{JniInt{-1}};
        const auto map = std::get<JniReference>(invocations.InvokeStatic(1U, *identity, *load, args, JniArgumentSource::value_array));
        REQUIRE_FALSE(map.IsNull());
        for (const auto& [meta, expected] : std::array<std::pair<JniInt, JniInt>, 3>{{{0, 'a'}, {1, 'A'}, {0x3000, 0}}}) {
            const std::array<JniValue, 2> key{JniInt{29}, meta};
            CHECK(std::get<JniInt>(invocations.InvokeVirtual(1U, map, *identity, *get, key, JniArgumentSource::value_array)) == expected);
        }
        CHECK_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        CHECK_FALSE(bridge.Linker().Class(bridge.Model().ObjectClass(bridge.FromReference(map))).is_intrinsic);
    }
}

TEST_CASE("ClassLoader findLibrary JNI method ID dispatches to the application loader") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        const auto identity = fixture.session->Classes().FindClass("java/lang/ClassLoader");
        REQUIRE(identity.has_value());
        const auto method = fixture.session->Classes().GetMethodId(
            *identity, "findLibrary", "(Ljava/lang/String;)Ljava/lang/String;", false);
        REQUIRE(method.has_value());
        const auto app = vm.ClassLoaders().ApplicationLoader();
        const auto boot = vm.ClassLoaders().BootstrapLoader();
        const auto find = [&](const VmObjectRef loader, const std::string_view name) {
            const auto argument = vm.NewStringUtf8(name);
            const auto roots = vm.ProtectReferences(std::array{loader, argument});
            const std::array<JniValue, 1> args{bridge.PublishLocal(argument)};
            const auto actual = bridge.RegisteredClassIdentity(bridge.Model().ObjectClass(loader));
            REQUIRE(actual.has_value());
            const auto result = std::get<JniReference>(fixture.session->Invocations().InvokeVirtual(
                1U, bridge.PublishLocal(loader), *actual, *method, args, JniArgumentSource::value_array));
            CHECK_FALSE(fixture.session->Environment().ExceptionCheck(1U));
            return bridge.FromReference(result);
        };
        const auto result = find(app, "a");
        REQUIRE(result.IsValid());
        CHECK(vm.StringUtf8(result) == "/data/app-lib/liba.so");
        CHECK_FALSE(find(boot, "a").IsValid());
        CHECK_FALSE(find(app, "missing").IsValid());
        CHECK_FALSE(find(app, "../a").IsValid());
        CHECK(fixture.libraries->Records().empty());

        fixture.context->native_libraries = nullptr;
        const auto actual = bridge.Model().ObjectClass(app);
        const auto index = bridge.Linker().FindVtableIndex(
            actual, "findLibrary", "(Ljava/lang/String;)Ljava/lang/String;");
        REQUIRE(index.has_value());
        const std::array args{VmValue::Ref(app), VmValue::Ref(vm.NewStringUtf8("a"))};
        const auto unavailable = vm.Call(bridge.Linker().Class(actual).vtable[*index], args);
        REQUIRE(unavailable.exception.IsValid());
        CHECK(bridge.Linker().Class(unavailable.exception_class).descriptor ==
              "Ljava/lang/UnsatisfiedLinkError;");
    }
}

TEST_CASE("ClassLoader parent-first JNI lookup and app calls share bootstrap types") {
    using namespace ogplay::runtime;
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend, std::nullopt, "parent_first_jni_app.dex");
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        const auto parent = linker.ResolveDescriptor("Lorg/apache/commons/codec/Decoder;");
        CHECK(linker.Class(parent).is_boot_dex);
        CHECK(linker.Class(parent).defining_loader == kBootstrapLoader);
        const auto identity = fixture.session->Classes().FindClass("org/apache/commons/codec/Decoder");
        REQUIRE(identity.has_value());
        CHECK(bridge.RegisteredClassIdentity(parent) == identity);
        CHECK(vm.ClassLoaders().LoadClass(kApplicationLoader, "org.apache.commons.codec.Decoder") == parent);
        REQUIRE(fixture.session->Classes().GetMethodId(*identity, "decode",
            "(Ljava/lang/Object;)Ljava/lang/Object;", false).has_value());
        const auto concrete = fixture.session->Classes().FindClass("shared/JniDecoder");
        REQUIRE(concrete.has_value());
        const auto method = fixture.session->Classes().GetMethodId(*concrete, "decode",
            "(Ljava/lang/Object;)Ljava/lang/Object;", false);
        REQUIRE(method.has_value());
        const auto receiver = vm.NewIntrinsicInstance("Lshared/JniDecoder;");
        CHECK(linker.IsAssignable(parent, bridge.Model().ObjectClass(receiver)));
        const auto marker = vm.NewStringUtf8("delegated");
        const auto roots = vm.ProtectReferences(std::array{receiver, marker});
        const std::array<JniValue, 1> args{bridge.PublishLocal(marker)};
        const auto result = std::get<JniReference>(fixture.session->Invocations().InvokeVirtual(
            1U, bridge.PublishLocal(receiver), *concrete, *method, args, JniArgumentSource::value_array));
        CHECK(bridge.FromReference(result) == marker);
        CHECK_FALSE(fixture.session->Environment().ExceptionCheck(1U));
    }
}

TEST_CASE("JNI framework services use VM methods objects and state") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        auto& classes = fixture.session->Classes();
        auto& invocations = fixture.session->Invocations();
        fixture.context->device_id = "fixture-device";
        fixture.context->secure_settings["android_id"] = "0123456789abcdef";
        for (const auto name : {"android/content/Context", "android/content/ContextWrapper",
                               "android/content/ContentResolver", "android/app/Activity",
                               "android/telephony/TelephonyManager", "android/provider/Settings$Secure",
                               "android/os/Bundle", "android/media/AudioTrack",
                               "android/app/ApplicationErrorReport",
                               "android/app/ApplicationErrorReport$1",
                               "android/app/ApplicationErrorReport$AnrInfo",
                               "android/app/ApplicationErrorReport$BatteryInfo",
                               "android/app/ApplicationErrorReport$CrashInfo",
                               "android/app/ApplicationErrorReport$RunningServiceInfo",
                               "android/graphics/SurfaceTexture", "android/view/Surface"}) {
            const auto owner = linker.ResolveDescriptor("L" + std::string(name) + ";");
            const auto identity = classes.FindClass(name);
            REQUIRE(identity.has_value());
            CHECK(identity == bridge.RegisteredClassIdentity(owner));
            for (const auto id : linker.MethodsOf(owner)) {
                const auto& method = linker.Method(id);
                if (method.name == "<clinit>") continue;
                const auto jni_id = classes.GetMethodId(*identity, method.name, method.descriptor, method.is_static);
                REQUIRE(jni_id.has_value());
                CHECK(classes.ResolveMethod(*jni_id).declaration.implementation.starts_with("dexvm.m"));
            }
        }
        const auto call = [&](VmObjectRef receiver, const char* name, const char* desc,
                              std::vector<JniValue> args = {}) {
            const auto identity = *bridge.RegisteredClassIdentity(vm.Model().ObjectClass(receiver));
            const auto method = classes.GetMethodId(identity, name, desc, false);
            REQUIRE(method.has_value());
            return invocations.InvokeVirtual(1U, bridge.PublishLocal(receiver), identity, *method,
                                              args, JniArgumentSource::value_array);
        };
        const auto construct = [&](const char* desc, const char* signature,
                                   std::vector<JniValue> args = {}) {
            const auto object = vm.NewIntrinsicInstance(desc);
            const auto identity = *bridge.RegisteredClassIdentity(vm.Model().ObjectClass(object));
            const auto method = classes.GetMethodId(identity, "<init>", signature, false);
            REQUIRE(method.has_value());
            static_cast<void>(invocations.InvokeNonvirtual(1U, bridge.PublishLocal(object), identity,
                identity, *method, args, JniArgumentSource::value_array));
            REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
            return object;
        };
        const auto text = [&](const char* value) { return bridge.PublishLocal(vm.NewStringUtf8(value)); };
        const auto string_value = [&](JniValue value) { return vm.StringUtf8(bridge.FromReference(std::get<JniReference>(value))); };
        const auto activity = construct("Landroid/app/Activity;", "()V");
        const auto base = vm.NewIntrinsicInstance("Landroid/content/Context;");
        static_cast<void>(call(activity, "attachBaseContext", "(Landroid/content/Context;)V", {bridge.PublishLocal(base)}));
        REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto manager_ref = std::get<JniReference>(call(activity, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;", {text("activity")}));
        const auto manager = bridge.FromReference(manager_ref);
        const auto memory_info = construct("Landroid/app/ActivityManager$MemoryInfo;", "()V");
        const auto memory_ref = bridge.PublishLocal(memory_info);
        static_cast<void>(call(manager, "getMemoryInfo", "(Landroid/app/ActivityManager$MemoryInfo;)V", {memory_ref}));
        REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto info_class = *bridge.RegisteredClassIdentity(vm.Model().ObjectClass(memory_info));
        const auto info_identity = fixture.session->Environment().ResolveObjectForHle(1U, memory_ref);
        REQUIRE(info_identity.has_value());
        for (const auto& [name, expected] : std::array<std::pair<const char*, std::int64_t>, 3>{{
                {"totalMem", 1073741824}, {"availMem", 805306368}, {"threshold", 100663296}}}) {
            const auto field = classes.GetFieldId(info_class, name, "J", false);
            REQUIRE(field.has_value());
            CHECK(std::get<JniLong>(fixture.session->Fields().GetInstance(*info_identity, info_class, *field, 1U)) == expected);
        }
        fixture.context->activity = activity;
        const auto legacy = classes.RegisterClass({"fixture/CurrentActivity", {},
            {{"get", "()Landroid/app/Activity;", "activity.current", true}}, {}});
        const auto current = *classes.GetMethodId(legacy, "get", "()Landroid/app/Activity;", true);
        CHECK(bridge.FromReference(std::get<JniReference>(invocations.InvokeStatic(
            1U, legacy, current, {}, JniArgumentSource::value_array))) == activity);
        const auto phone = std::get<JniReference>(call(activity, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;", {text("phone")}));
        CHECK(bridge.FromReference(phone).Value() == fixture.context->singletons.at("phone").Value());
        CHECK(string_value(call(bridge.FromReference(phone), "getDeviceId", "()Ljava/lang/String;")) == "fixture-device");
        CHECK_FALSE(bridge.FromReference(std::get<JniReference>(call(
            bridge.FromReference(phone), "getSubscriberId", "()Ljava/lang/String;"))).IsValid());
        CHECK_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto sensor = std::get<JniReference>(call(activity, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;", {text("sensor")}));
        CHECK(std::get<JniInt>(call(bridge.FromReference(sensor), "getSensors", "()I")) == 0);
        CHECK_FALSE(bridge.FromReference(std::get<JniReference>(call(bridge.FromReference(sensor),
            "getDefaultSensor", "(I)Landroid/hardware/Sensor;", {JniInt{1}}))).IsValid());
        CHECK(std::get<JniBoolean>(call(bridge.FromReference(sensor), "registerListener",
            "(Landroid/hardware/SensorListener;I)Z", {JniReference{}, JniInt{1}})) == JniBoolean{false});
        CHECK_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto resolver = std::get<JniReference>(call(activity, "getContentResolver", "()Landroid/content/ContentResolver;"));
        CHECK(bridge.FromReference(resolver).Value() == fixture.context->singletons.at("content_resolver").Value());
        const auto secure = *classes.FindClass("android/provider/Settings$Secure");
        const auto get = *classes.GetMethodId(secure, "getString",
            "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;", true);
        const std::array<JniValue, 2> args{resolver, text("android_id")};
        CHECK(string_value(invocations.InvokeStatic(1U, secure, get, args, JniArgumentSource::value_array)) == "0123456789abcdef");
        const auto report = *classes.FindClass("android/app/ApplicationErrorReport");
        CHECK(linker.Class(linker.ResolveDescriptor("Landroid/app/ApplicationErrorReport;")).is_boot_dex);
        const auto receiver = classes.GetMethodId(report, "getErrorReportReceiver",
            "(Landroid/content/Context;Ljava/lang/String;I)Landroid/content/ComponentName;", true);
        REQUIRE(receiver.has_value());
        const std::array<JniValue, 3> report_args{
            bridge.PublishLocal(activity), text("fixture.app"), JniInt{1}};
        // Both an absent setting and explicit zero execute the original API19
        // early return, before any installer or external receiver query.
        for (const bool explicit_zero : {false, true}) {
            if (explicit_zero) {
                fixture.context->preferences["__android.settings.global"]
                    ["send_action_app_error"] = std::string("0");
            }
            CHECK(std::get<JniReference>(invocations.InvokeStatic(
                1U, report, *receiver, report_args, JniArgumentSource::value_array)).IsNull());
            REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        }
        const auto bundle = construct("Landroid/os/Bundle;", "()V");
        CHECK(linker.Class(vm.Model().ObjectClass(bundle)).is_boot_dex);
        static_cast<void>(call(bundle, "putInt", "(Ljava/lang/String;I)V", {text("answer"), JniInt{42}}));
        static_cast<void>(vm.CollectGarbage("jni-framework-state"));
        CHECK(std::get<JniInt>(call(bundle, "getInt", "(Ljava/lang/String;)I", {text("answer")})) == 42);
        CHECK(bridge.FromReference(std::get<JniReference>(call(activity, "getContentResolver",
            "()Landroid/content/ContentResolver;"))) == bridge.FromReference(resolver));
        fixture.context->activity = VmObjectRef{};
        CHECK(std::get<JniReference>(invocations.InvokeStatic(1U, legacy, current, {},
            JniArgumentSource::value_array)).IsNull());
    }
}

TEST_CASE("JNI AudioTrack uses the VM PCM player and lifecycle") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& classes = fixture.session->Classes();
        auto& invocations = fixture.session->Invocations();
        fixture.context->pcm_playback = &fixture.session->PcmPlayback();
        const auto owner = *classes.FindClass("android/media/AudioTrack");
        const auto host_object = AllocateJniHostObjectIdentity();
        fixture.session->Objects().Register(host_object, owner);
        const auto receiver = fixture.session->Environment().PublishLocalObject(1U, host_object);
        const auto constructor = *classes.GetMethodId(owner, "<init>", "(IIIIII)V", false);
        const auto get_minimum = *classes.GetMethodId(owner, "getMinBufferSize", "(III)I", true);
        const std::array<JniValue, 3> format{JniInt{8000}, JniInt{12}, JniInt{2}};
        const auto minimum = std::get<JniInt>(invocations.InvokeStatic(1U, owner, get_minimum,
            format, JniArgumentSource::value_array));
        REQUIRE(minimum > 0);
        const std::array<JniValue, 6> config{JniInt{3}, JniInt{8000}, JniInt{12}, JniInt{2}, minimum, JniInt{1}};
        static_cast<void>(invocations.InvokeNonvirtual(1U, receiver, owner, owner, constructor,
            config, JniArgumentSource::value_array));
        REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto track = bridge.FromReference(receiver);
        REQUIRE(fixture.context->audio_tracks.contains(track.Value()));
        const auto player = fixture.context->audio_tracks.at(track.Value()).player;
        const auto call = [&](const char* name, const char* desc, std::vector<JniValue> args = {}) {
            const auto method = classes.GetMethodId(owner, name, desc, false);
            REQUIRE(method.has_value());
            return invocations.InvokeVirtual(1U, receiver, owner, *method, args, JniArgumentSource::value_array);
        };
        auto& arrays = fixture.session->Arrays();
        const auto bytes = arrays.New(JniPrimitiveKind::byte, 8);
        arrays.SetRegion(bytes, 0, JniPrimitiveArrayData{std::vector<JniByte>{0, 1, 0, 2, 0, 3, 0, 4}});
        const auto pcm = fixture.session->Environment().PublishLocalObject(1U, bytes);
        CHECK(std::get<JniInt>(call("write", "([BII)I", {pcm, JniInt{0}, JniInt{8}})) == 8);
        CHECK(fixture.session->PcmPlayback().QueuedBytes(player) == 8U);
        static_cast<void>(call("play", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 3);
        std::array<std::int16_t, 4> mixed{};
        static_cast<void>(fixture.session->PcmPlayback().MixAdditiveStereoPcm16(mixed, 8000U));
        CHECK(mixed[0] != 0);
        CHECK(mixed[1] != 0);
        static_cast<void>(call("pause", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 2);
        static_cast<void>(call("stop", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 1);
        static_cast<void>(vm.CollectGarbage("jni-audio-owner"));
        CHECK(fixture.context->audio_tracks.contains(track.Value()));
        static_cast<void>(call("release", "()V"));
        CHECK(std::get<JniInt>(call("getState", "()I")) == 0);
    }
}

TEST_CASE("BootDex speech callbacks link and JNI diagnostics retain nested causes") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& linker = vm.Linker();
        auto& env = f.session->Environment();
        auto& classes = f.session->Classes();
        const auto cls = *classes.FindClass("java/lang/Class");
        const auto for_name = *classes.GetMethodId(cls, "forName", "(Ljava/lang/String;)Ljava/lang/Class;", true);
        const auto lookup = [&](const char* name) {
            const std::array<JniValue, 1> args{f.bridge->PublishLocal(vm.NewStringUtf8(name))};
            return std::get<JniReference>(f.session->Invocations().InvokeStatic(
                1, cls, for_name, args, JniArgumentSource::value_array));
        };
        CHECK_FALSE(lookup("fixture.SpeechCallbacks").IsNull());
        CHECK_FALSE(env.ExceptionCheck(1));
        const auto callback = linker.ResolveDescriptor("Lfixture/SpeechCallbacks;");
        for (const auto descriptor : {"Landroid/speech/tts/TextToSpeech$OnInitListener;",
                                      "Landroid/speech/tts/TextToSpeech$OnUtteranceCompletedListener;"}) {
            const auto type = linker.ResolveDescriptor(descriptor);
            CHECK(linker.Class(type).is_boot_dex);
            CHECK(linker.IsAssignable(type, callback));
        }
        const auto speech = linker.ResolveDescriptor("Landroid/speech/tts/TextToSpeech;");
        CHECK(linker.Class(speech).is_boot_dex);
        CHECK_FALSE(linker.Class(speech).is_intrinsic);
        const auto class_object = vm.Model().ClassObject(callback);
        const auto methods_slot = linker.FindVtableIndex(
            vm.Model().ObjectClass(class_object), "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
        REQUIRE(methods_slot.has_value());
        const auto methods_result = vm.Call(
            linker.Class(vm.Model().ObjectClass(class_object)).vtable[*methods_slot],
            std::array{VmValue::Ref(class_object)});
        REQUIRE_FALSE(methods_result.exception.IsValid());
        const auto methods_root = vm.ProtectReferences(std::array{methods_result.value.ref});
        CHECK(vm.Model().ArrayLength(methods_result.value.ref) == 4);
        bool found_service = false;
        for (runtime::JniSize i = 0; i < vm.Model().ArrayLength(methods_result.value.ref); ++i) {
            const auto method_object = vm.Model().GetObjectElement(methods_result.value.ref, i);
            const auto& metadata = vm.Reflection().MethodMetadata(method_object);
            if (linker.Method(metadata.method).name != "accessService") continue;
            found_service = true;
            CHECK(metadata.return_type == speech);
            CHECK(metadata.parameter_types == std::vector<DexClassId>{callback});
            CHECK((metadata.access_flags & kAccSynthetic) != 0);
        }
        CHECK(found_service);
        const auto exercise = *linker.FindDirectMethod(callback, "exercise", "()I");
        const auto result = vm.Call(exercise, {});
        REQUIRE_FALSE(result.exception.IsValid());
        CHECK(result.value.AsInt() == 42);

        CHECK(lookup("fixture.MissingCallbackImplementation").IsNull());
        const auto pending = env.PendingExceptionMetadata(1);
        REQUIRE(pending.has_value());
        auto records = f.logger.Snapshot(core::LogLevel::warn, "runtime.dexvm");
        REQUIRE_FALSE(records.empty());
        CHECK(records.back().message.find("ClassNotFoundException") != std::string::npos);
        CHECK(records.back().message.find("caused by Ljava/lang/NoClassDefFoundError;") != std::string::npos);
        CHECK(records.back().message.find("Lfixture/AbsentCallback;") != std::string::npos);
        env.PushLocalFrame(1, 2);
        const auto occurred = env.ExceptionOccurred(1);
        CHECK_FALSE(occurred.IsNull());
        CHECK(env.PendingExceptionMetadata(1)->throwable == pending->throwable);
        const auto global_pending = env.NewGlobalRef(1, occurred);
        CHECK_FALSE(global_pending.IsNull());
        CHECK_FALSE(env.PendingCallDiagnostics().empty());
        env.DeleteGlobalRef(1, global_pending);
        CHECK(env.PopLocalFrame(1).IsNull());
        CHECK(env.PendingExceptionMetadata(1)->throwable == pending->throwable);
        env.ExceptionClear(1);

        // Diagnostic traversal is bounded even for legal multi-object cause cycles.
        const auto first = vm.MakeThrowable("Ljava/lang/IllegalStateException;", "first");
        const auto first_root = vm.ProtectReferences(std::array{first});
        const auto second = vm.MakeThrowable("Ljava/lang/IllegalArgumentException;", "second");
        const auto second_root = vm.ProtectReferences(std::array{second});
        vm.InitThrowableCause(first, second);
        vm.InitThrowableCause(second, first);
        const auto probe = *classes.FindClass("fixture/ExceptionProbe");
        const auto raise = *classes.GetMethodId(probe, "raise", "(Ljava/lang/Throwable;)V", true);
        const std::array<JniValue, 1> args{f.bridge->PublishLocal(first)};
        static_cast<void>(f.session->Invocations().InvokeStatic(
            1, probe, raise, args, JniArgumentSource::value_array));
        records = f.logger.Snapshot(core::LogLevel::warn, "runtime.dexvm");
        CHECK(records.back().message.find("cause cycle") != std::string::npos);
        CHECK(records.back().message.size() < 4096);
        CHECK(env.PendingExceptionMetadata(1)->throwable == vm.Model().ToIdentity(first));
        env.ExceptionClear(1);
    }
}

TEST_CASE("DVM-112 BootDex ServiceConnection links and the shared bridge takes the absent branch") {
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        f.context->service_inventory_known = true;
        auto& vm = f.bridge->Vm();
        auto& linker = f.bridge->Linker();
        const auto contract = linker.ResolveDescriptor("Landroid/content/ServiceConnection;");
        CHECK_FALSE(linker.Class(contract).is_intrinsic);
        CHECK(linker.Class(contract).is_interface);
        const auto class_flags = kAccPublic | kAccAbstract | kAccInterface;
        CHECK((linker.Class(contract).access_flags & class_flags) == class_flags);
        REQUIRE(linker.Class(contract).own_virtual_methods.size() == 2);
        for (auto method : linker.Class(contract).own_virtual_methods) {
            CHECK((linker.Method(method).access_flags & (kAccPublic | kAccAbstract)) ==
                  (kAccPublic | kAccAbstract));
        }
        auto direct = [&](const char* owner, const char* name, const char* signature, std::vector<VmValue> args) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, signature);
            REQUIRE(method);
            const auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto base = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        const auto probe = vm.NewIntrinsicInstance("Lfixture/ServiceProbe;");
        const auto roots = vm.ProtectReferences(std::array{base, intent, probe});
        direct("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(intent), VmValue::Ref(vm.NewStringUtf8("example.ABSENT"))});
        direct("Lfixture/ServiceProbe;", "<init>", "()V", {VmValue::Ref(probe)});
        const auto probe_class = vm.Model().ObjectClass(probe);
        CHECK(linker.IsAssignable(contract, probe_class));
        const auto contract_identity = f.bridge->RegisteredClassIdentity(contract);
        const auto probe_identity = f.bridge->RegisteredClassIdentity(probe_class);
        REQUIRE(contract_identity.has_value());
        REQUIRE(probe_identity.has_value());
        CHECK(f.session->Classes().GetInterfaces(*probe_identity) ==
              std::vector{*contract_identity});
        CHECK(f.session->Classes().IsAssignableFrom(*contract_identity,
                                                    *probe_identity));
        CHECK(direct("Lfixture/ServiceProbe;", "discover",
            "(Landroid/content/Context;Landroid/content/Intent;)I",
            {VmValue::Ref(base), VmValue::Ref(intent)}).AsInt() == 1);
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(vm.CollectGarbage());
        CHECK(direct("Lfixture/ServiceProbe;", "exerciseCallbacks", "(Landroid/content/ServiceConnection;)I",
            {VmValue::Ref(probe)}).AsInt() == 2);
    }
}

TEST_CASE("DVM-144 application Proxy executes BootDex Binder transact and Java reply protocol") {
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& linker = f.bridge->Linker();
        const auto direct = [&](const char* owner, const char* name, const char* sig, std::vector<VmValue> args) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, sig);
            REQUIRE(method);
            const auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto on = [&](VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(receiver);
            const auto index = linker.FindVtableIndex(type, name, sig);
            REQUIRE(index);
            args.insert(args.begin(), VmValue::Ref(receiver));
            return vm.Call(linker.Class(type).vtable[*index], args);
        };
        const auto local = vm.NewIntrinsicInstance("Lfixture/EchoBinder;");
        const auto remote_shape = vm.NewIntrinsicInstance("Lfixture/ForcedProxyBinder;");
        const auto roots = vm.ProtectReferences(std::array{local, remote_shape});
        direct("Lfixture/EchoBinder;", "<init>", "()V", {VmValue::Ref(local)});
        direct("Lfixture/ForcedProxyBinder;", "<init>", "()V", {VmValue::Ref(remote_shape)});
        const auto descriptor = vm.NewStringUtf8("fixture.echo");
        CHECK(on(local, "queryLocalInterface", "(Ljava/lang/String;)Landroid/os/IInterface;", {VmValue::Ref(descriptor)}).value.ref == local);
        CHECK_FALSE(on(remote_shape, "queryLocalInterface", "(Ljava/lang/String;)Landroid/os/IInterface;", {VmValue::Ref(descriptor)}).value.ref.IsValid());
        for (const auto flags : {0, 1})
            CHECK(direct("Lfixture/EchoProxy;", "echo", "(Landroid/os/IBinder;II)I",
                         {VmValue::Ref(remote_shape), VmValue::Int(41), VmValue::Int(flags)}).AsInt() == 42);
        CHECK_FALSE(linker.Class(linker.ResolveDescriptor("Lfixture/EchoBinder;")).is_intrinsic);
        const auto binder = linker.ResolveDescriptor("Landroid/os/Binder;");
        const auto transact_index = linker.FindVtableIndex(binder, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z");
        REQUIRE(transact_index);
        CHECK(linker.Method(linker.Class(binder).vtable[*transact_index]).code.has_value());
        const auto data = direct("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;", {}).ref;
        const auto reply = direct("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;", {}).ref;
        const auto parcels = vm.ProtectReferences(std::array{data, reply});
        const auto invoke = [&](int code) {
            return on(local, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
                      {VmValue::Int(code), VmValue::Ref(data), VmValue::Ref(reply), VmValue::Int(0)});
        };
        const auto unknown = invoke(99);
        REQUIRE_FALSE(unknown.exception.IsValid());
        CHECK(unknown.value.AsInt() == 0);
        const auto failure = invoke(2);
        REQUIRE(failure.exception.IsValid());
        CHECK(linker.Class(failure.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        CHECK(on(reply, "dataSize", "()I", {}).value.AsInt() == 0);
        const auto info = invoke(0x5f4e5446);
        REQUIRE_FALSE(info.exception.IsValid());
        CHECK(info.value.AsInt() == 1);
        CHECK(on(reply, "dataPosition", "()I", {}).value.AsInt() == 0);
        CHECK(vm.StringUtf8(on(reply, "readString", "()Ljava/lang/String;", {}).value.ref) == "fixture.echo");
    }
}
