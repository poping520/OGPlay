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
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct AndroidValueVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;

    AndroidValueVm(InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                   const std::vector<IntrinsicClassDecl>& extras = {},
                   bool force_all_bridge = false,
                   const char* dex_fixture = nullptr)
        : vm(
              [this, &extras, dex_fixture]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog(
                      AndroidCoreIntrinsicServices(context)));
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  ogplay::test::RegisterBootDex(linker);
                  linker.RegisterIntrinsics(extras);
                  if (dex_fixture != nullptr) {
                      const auto path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + dex_fixture;
                      std::ifstream input(path, std::ios::binary);
                      REQUIRE_MESSAGE(input.good(), "missing fixture: ", path);
                      linker.RegisterDex(std::vector<std::uint8_t>(
                          std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
                  }
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {.backend = backend, .force_all_bridge = force_all_bridge}) {
        vm.SetLogger(&logger);
        RegisterAndroidValueStateTables(vm, context);
    }

    VmObjectRef New(const char* descriptor,
                    const char* constructor = "()V",
                    std::vector<VmValue> arguments = {}) {
        const auto klass = linker.ResolveDescriptor(descriptor);
        const auto initialized = vm.EnsureClassInitialized(klass);
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        const auto object = vm.NewIntrinsicInstance(descriptor);
        const auto method = linker.FindDirectMethod(klass, "<init>", constructor);
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(object));
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return object;
    }

    VmValue Static(const char* descriptor, const char* name,
                   const char* signature,
                   std::vector<VmValue> arguments = {}) {
        const auto outcome = StaticOutcome(
            descriptor, name, signature, std::move(arguments));
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome StaticOutcome(const char* descriptor, const char* name,
                                const char* signature,
                                std::vector<VmValue> arguments = {}) {
        const auto klass = linker.ResolveDescriptor(descriptor);
        const auto method = linker.FindDirectMethod(klass, name, signature);
        REQUIRE(method.has_value());
        return vm.Call(*method, arguments);
    }

    VmValue On(const VmObjectRef receiver, const char* name,
               const char* signature,
               std::vector<VmValue> arguments = {}) {
        const auto outcome = OnOutcome(receiver, name, signature,
                                       std::move(arguments));
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome OnOutcome(const VmObjectRef receiver, const char* name,
                            const char* signature,
                            std::vector<VmValue> arguments = {}) {
        const auto klass = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(klass, name, signature);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.Call(linker.Class(klass).vtable[*index], arguments);
    }

    VmObjectRef Bytes(const std::string& value) {
        const auto array = model.NewPrimitiveArray(
            linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
            static_cast<JniSize>(value.size()));
        std::vector<std::byte> bytes(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
            bytes[index] = static_cast<std::byte>(value[index]);
        model.WriteByteRegion(array, 0, bytes);
        return array;
    }

    std::string BytesOf(const VmObjectRef array) {
        const auto bytes = model.ReadByteRegion(array, 0, model.ArrayLength(array));
        std::string result(bytes.size(), '\0');
        for (std::size_t index = 0; index < bytes.size(); ++index)
            result[index] = static_cast<char>(bytes[index]);
        return result;
    }
};

}  // namespace

TEST_CASE("Context class loader preserves application identity and virtual base delegation") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        auto custom = IntrinsicClassBuilder::Class(
            "Ltest/LoaderContext;", "Landroid/content/Context;");
        custom.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        custom.OverrideMethod("getClassLoader", "()Ljava/lang/ClassLoader;",
            [](IntrinsicContext& call) {
                return VmValue::Ref(call.vm.ClassLoaders().BootstrapLoader());
            });
        AndroidValueVm f(backend, {std::move(custom).Build()});
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto loader = f.On(base, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
        CHECK(loader == f.vm.ClassLoaders().ApplicationLoader());
        CHECK(f.On(loader, "getParent", "()Ljava/lang/ClassLoader;").ref ==
              f.vm.ClassLoaders().BootstrapLoader());
        for (const auto* type : {"Landroid/app/Application;", "Landroid/app/Activity;",
                                 "Landroid/app/Service;"}) {
            const auto object = f.New(type);
            const auto object_root = f.vm.ProtectReferences(std::array{object});
            f.On(object, "attachBaseContext", "(Landroid/content/Context;)V",
                 {VmValue::Ref(base)});
            CHECK(f.On(object, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == loader);
            // The framework object's defining loader is not its Context loader.
            CHECK(f.On(f.model.ClassObject(f.model.ObjectClass(object)),
                       "getClassLoader", "()Ljava/lang/ClassLoader;").ref != loader);
            static_cast<void>(f.vm.CollectGarbage("context-class-loader"));
            CHECK(f.On(object, "getClassLoader", "()Ljava/lang/ClassLoader;").ref == loader);
        }
        const auto overridden = f.New("Ltest/LoaderContext;");
        const auto overridden_root = f.vm.ProtectReferences(std::array{overridden});
        const auto wrapper = f.New("Landroid/content/ContextWrapper;",
            "(Landroid/content/Context;)V", {VmValue::Ref(overridden)});
        const auto wrapper_root = f.vm.ProtectReferences(std::array{wrapper});
        const auto nested = f.New("Landroid/content/ContextWrapper;",
            "(Landroid/content/Context;)V", {VmValue::Ref(wrapper)});
        const auto nested_root = f.vm.ProtectReferences(std::array{nested});
        CHECK(f.On(nested, "getClassLoader", "()Ljava/lang/ClassLoader;").ref ==
              f.vm.ClassLoaders().BootstrapLoader());
        const auto unattached = f.New("Landroid/content/ContextWrapper;",
            "(Landroid/content/Context;)V", {VmValue::Ref(VmObjectRef{})});
        const auto failure = f.OnOutcome(unattached, "getClassLoader",
                                         "()Ljava/lang/ClassLoader;");
        REQUIRE(failure.exception.IsValid());
        CHECK(f.linker.Class(failure.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Context class loader reflects BootDex system properties with API19 defaults") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto application = f.New("Landroid/app/Application;");
        const auto application_root = f.vm.ProtectReferences(std::array{application});
        f.On(application, "attachBaseContext", "(Landroid/content/Context;)V",
             {VmValue::Ref(base)});
        const auto loader = f.On(application, "getClassLoader", "()Ljava/lang/ClassLoader;").ref;
        const auto type = f.On(loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
            {VmValue::Ref(f.vm.NewStringUtf8("android.os.SystemProperties"))}).ref;
        CHECK(type == f.model.ClassObject(f.linker.ResolveDescriptor("Landroid/os/SystemProperties;")));
        CHECK(f.On(type, "getClassLoader", "()Ljava/lang/ClassLoader;").ref ==
              f.vm.ClassLoaders().BootstrapLoader());
        const auto parameters = f.model.NewObjectArray(
            f.linker.ResolveDescriptor("[Ljava/lang/Class;"),
            f.linker.ResolveDescriptor("Ljava/lang/Class;"), 2);
        const auto parameters_root = f.vm.ProtectReferences(std::array{parameters});
        const auto string_class = f.model.ClassObject(f.linker.ResolveDescriptor("Ljava/lang/String;"));
        f.model.SetObjectElement(parameters, 0, string_class);
        f.model.SetObjectElement(parameters, 1, string_class);
        const auto method = f.On(type, "getMethod",
            "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
            {VmValue::Ref(f.vm.NewStringUtf8("get")), VmValue::Ref(parameters)}).ref;
        const auto method_root = f.vm.ProtectReferences(std::array{method});
        CHECK(f.linker.Method(f.vm.Reflection().MethodMetadata(method).method).kind ==
              MethodKind::interpreted);
        const auto arguments = f.model.NewObjectArray(
            f.linker.ResolveDescriptor("[Ljava/lang/Object;"),
            f.linker.ResolveDescriptor("Ljava/lang/Object;"), 2);
        const auto arguments_root = f.vm.ProtectReferences(std::array{arguments});
        f.model.SetObjectElement(arguments, 0, f.vm.NewStringUtf8("ro.serialno"));
        f.model.SetObjectElement(arguments, 1, f.vm.NewStringUtf8("Unknown"));
        const auto read = [&] {
            return f.vm.StringUtf8(f.On(method, "invoke",
                "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
                {VmValue::Ref(type), VmValue::Ref(arguments)}).ref);
        };
        CHECK(read() == "Unknown");
        static_cast<void>(f.vm.CollectGarbage("context-properties-reflection"));
        CHECK(read() == "Unknown");
        f.model.SetObjectElement(arguments, 0, f.vm.NewStringUtf8("ro.build.version.sdk"));
        CHECK(read() == "19");
    }
}

TEST_CASE("Telephony subscriber identity is unavailable without a cellular subscription") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto phone = f.On(base, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(f.vm.NewStringUtf8("phone"))}).ref;
        REQUIRE(phone.IsValid());
        const auto phone_root = f.vm.ProtectReferences(std::array{phone});
        const auto type = f.model.ObjectClass(phone);
        const auto index = f.linker.FindVtableIndex(type, "getSubscriberId",
                                                   "()Ljava/lang/String;");
        REQUIRE(index.has_value());
        const auto flags = f.linker.Method(f.linker.Class(type).vtable[*index]).access_flags;
        CHECK((flags & kAccPublic) != 0U);
        CHECK((flags & (kAccFinal | kAccStatic)) == 0U);
        CHECK(f.On(phone, "getPhoneType", "()I").AsInt() == 0);
        CHECK(f.On(phone, "getSimState", "()I").AsInt() == 1);
        CHECK_FALSE(f.On(phone, "getSubscriberId", "()Ljava/lang/String;").ref.IsValid());
        static_cast<void>(f.vm.CollectGarbage("telephony-subscriber-absence"));
        CHECK_FALSE(f.On(phone, "getSubscriberId", "()Ljava/lang/String;").ref.IsValid());
    }
}

TEST_CASE("DVM-128 Settings.Secure reads the injected API 19 identity") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        fixture.context->secure_settings.insert_or_assign(
            "android_id", "0123456789abcdef");
        const auto resolver = fixture.vm.NewIntrinsicInstance(
            "Landroid/content/ContentResolver;");
        const auto key = fixture.vm.NewStringUtf8("android_id");
        const auto roots = fixture.vm.ProtectReferences(
            std::array{resolver, key});
        const auto read = [&] {
            return fixture.Static(
                "Landroid/provider/Settings$Secure;", "getString",
                "(Landroid/content/ContentResolver;Ljava/lang/String;)"
                "Ljava/lang/String;",
                {VmValue::Ref(resolver), VmValue::Ref(key)}).ref;
        };
        CHECK(fixture.vm.StringUtf8(read()) == "0123456789abcdef");
        static_cast<void>(fixture.vm.CollectGarbage("dvm128-secure-settings"));
        CHECK(fixture.vm.StringUtf8(read()) == "0123456789abcdef");

        const auto unknown = fixture.vm.NewStringUtf8("unknown_setting");
        CHECK_FALSE(fixture.Static(
            "Landroid/provider/Settings$Secure;", "getString",
            "(Landroid/content/ContentResolver;Ljava/lang/String;)"
            "Ljava/lang/String;",
            {VmValue::Ref(resolver), VmValue::Ref(unknown)}).ref.IsValid());

        const auto outcome = fixture.StaticOutcome(
            "Landroid/provider/Settings$Secure;", "getString",
            "(Landroid/content/ContentResolver;Ljava/lang/String;)"
            "Ljava/lang/String;",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(key)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");

        const auto null_name = fixture.StaticOutcome(
            "Landroid/provider/Settings$Secure;", "getString",
            "(Landroid/content/ContentResolver;Ljava/lang/String;)"
            "Ljava/lang/String;",
            {VmValue::Ref(resolver), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_name.exception.IsValid());
        CHECK(fixture.linker.Class(null_name.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Settings BootDex routes moved keys through the bounded store") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        fixture.context->secure_settings.insert_or_assign(
            "android_id", "0123456789abcdef");
        const auto resolver = fixture.vm.NewIntrinsicInstance(
            "Landroid/content/ContentResolver;");
        const auto android_id = fixture.vm.NewStringUtf8("android_id");
        const auto volume = fixture.vm.NewStringUtf8("volume_music");
        const auto text = fixture.vm.NewStringUtf8("17");
        const auto roots = fixture.vm.ProtectReferences(
            std::array{resolver, android_id, volume, text});

        const auto get_string =
            "(Landroid/content/ContentResolver;Ljava/lang/String;)"
            "Ljava/lang/String;";
        const auto moved = fixture.Static(
            "Landroid/provider/Settings$System;", "getString", get_string,
            {VmValue::Ref(resolver), VmValue::Ref(android_id)}).ref;
        CHECK(fixture.vm.StringUtf8(moved) == "0123456789abcdef");
        CHECK(fixture.Static(
                  "Landroid/provider/Settings$Secure;", "putString",
                  "(Landroid/content/ContentResolver;Ljava/lang/String;"
                  "Ljava/lang/String;)Z",
                  {VmValue::Ref(resolver), VmValue::Ref(android_id),
                   VmValue::Ref(text)}).AsInt() == 0);

        CHECK(fixture.Static(
                  "Landroid/provider/Settings$System;", "putString",
                  "(Landroid/content/ContentResolver;Ljava/lang/String;"
                  "Ljava/lang/String;)Z",
                  {VmValue::Ref(resolver), VmValue::Ref(volume),
                   VmValue::Ref(text)}).AsInt() == 1);
        const auto stored = fixture.Static(
            "Landroid/provider/Settings$System;", "getString", get_string,
            {VmValue::Ref(resolver), VmValue::Ref(volume)}).ref;
        CHECK(fixture.vm.StringUtf8(stored) == "17");
        CHECK(fixture.Static(
                  "Landroid/provider/Settings$System;", "getInt",
                  "(Landroid/content/ContentResolver;Ljava/lang/String;I)I",
                  {VmValue::Ref(resolver), VmValue::Ref(volume),
                   VmValue::Int(3)}).AsInt() == 17);

        const auto system_class = fixture.linker.ResolveDescriptor(
            "Landroid/provider/Settings$System;");
        const auto public_get = fixture.linker.FindDirectMethod(
            system_class, "getString", get_string);
        REQUIRE(public_get.has_value());
        CHECK(fixture.linker.Method(*public_get).kind ==
              MethodKind::interpreted);
        const auto cache_class = fixture.linker.ResolveDescriptor(
            "Landroid/provider/Settings$NameValueCache;");
        const auto store_get = fixture.linker.FindVtableIndex(
            cache_class, "getStringForUser",
            "(Landroid/content/ContentResolver;Ljava/lang/String;I)"
            "Ljava/lang/String;");
        REQUIRE(store_get.has_value());
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(cache_class).vtable[*store_get]).kind ==
              MethodKind::intrinsic);
    }
}

TEST_CASE("DVM-116 BackupManager Java reports absent backup service") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int callbacks{};
        auto observer = IntrinsicClassBuilder::Class(
            "Ltest/BackupObserver;", "Landroid/app/backup/RestoreObserver;");
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
        const auto manager = f.New("Landroid/app/backup/BackupManager;",
                                   "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto owner = f.model.ObjectClass(manager);
        CHECK(f.linker.Class(owner).is_boot_dex);
        const auto context = f.linker.FindFieldRecursive(
            owner, "mContext", "Landroid/content/Context;");
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
        CHECK(f.On(manager, "requestRestore", "(Landroid/app/backup/RestoreObserver;)I",
                   {VmValue::Ref(receiver)}).AsInt() == -1);
        CHECK(f.On(manager, "requestRestore", "(Landroid/app/backup/RestoreObserver;)I",
                   {VmValue::Ref(VmObjectRef{})}).AsInt() == -1);
        CHECK_FALSE(f.On(manager, "beginRestoreSession", "()Landroid/app/backup/RestoreSession;")
                        .ref.IsValid());
        CHECK(callbacks == 0);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.backup_service");
        CHECK(hits[0].count == 6);
        const auto service = f.linker.FindFieldRecursive(
            owner, "sService", "Landroid/app/backup/IBackupManager;");
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
        const auto null_context = f.New("Landroid/app/backup/BackupManager;",
                                        "(Landroid/content/Context;)V", {VmValue::Ref(VmObjectRef{})});
        f.On(null_context, "dataChanged", "()V");
    }
}

TEST_CASE("DVM-140 Intent putExtras merges Java Bundle mappings without aliasing") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto intent = f.New("Landroid/content/Intent;");
        const auto source = f.New("Landroid/os/Bundle;");
        const auto key = f.vm.NewStringUtf8("payload");
        const auto keep = f.vm.NewStringUtf8("keep");
        const auto roots = f.vm.ProtectReferences(std::array{intent, source, key, keep});
        constexpr auto signature = "(Landroid/os/Bundle;)Landroid/content/Intent;";
        CHECK(f.On(intent, "putExtras", signature, {VmValue::Ref(source)}).ref == intent);
        CHECK(f.On(f.On(intent, "getExtras", "()Landroid/os/Bundle;").ref,
                   "isEmpty", "()Z").AsInt() == 1);
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Int(1)});
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
             {VmValue::Ref(keep), VmValue::Int(9)});
        const auto payload = f.New("Ljava/util/HashMap;");
        f.On(source, "putSerializable", "(Ljava/lang/String;Ljava/io/Serializable;)V",
             {VmValue::Ref(key), VmValue::Ref(payload)});
        CHECK(f.On(intent, "putExtras", signature, {VmValue::Ref(source)}).ref == intent);
        f.On(source, "clear", "()V");
        static_cast<void>(f.vm.CollectGarbage("dvm140-merged-extras"));
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                   {VmValue::Ref(key)}).ref == payload);
        CHECK(f.On(intent, "getIntExtra", "(Ljava/lang/String;I)I",
                   {VmValue::Ref(keep), VmValue::Int(0)}).AsInt() == 9);
        f.On(source, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
             {VmValue::Ref(key), VmValue::Ref(VmObjectRef{})});
        f.On(intent, "putExtras", signature, {VmValue::Ref(source)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK_FALSE(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                         {VmValue::Ref(key)}).ref.IsValid());
        for (const auto target : {intent, f.New("Landroid/content/Intent;")}) {
            const auto outcome = f.OnOutcome(target, "putExtras", signature, {VmValue::Ref(VmObjectRef{})});
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/NullPointerException;");
            CHECK(f.On(target, "getExtras", "()Landroid/os/Bundle;").ref.IsValid());
        }
    }
}

TEST_CASE("DVM-117 Intent extras use BootDex Bundle identity copies and GC") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto intent = f.New("Landroid/content/Intent;");
        const auto intent_root = f.vm.ProtectReferences(std::array{intent});
        CHECK_FALSE(f.On(intent, "getExtras", "()Landroid/os/Bundle;").ref.IsValid());
        const auto key = f.vm.NewStringUtf8("payload");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        const auto payload = f.New("Ljava/util/HashMap;");
        const auto child = f.vm.NewStringUtf8("retained");
        f.On(payload, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
             {VmValue::Ref(key), VmValue::Ref(child)});
        CHECK(f.On(intent, "putExtra", "(Ljava/lang/String;Ljava/io/Serializable;)Landroid/content/Intent;",
                   {VmValue::Ref(key), VmValue::Ref(payload)}).ref == intent);
        CHECK(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm117-intent-extras"));
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                   {VmValue::Ref(key)}).ref == payload);
        CHECK_FALSE(f.On(intent, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;",
                         {VmValue::Ref(key)}).ref.IsValid());
        CHECK(f.On(intent, "getIntExtra", "(Ljava/lang/String;I)I",
                   {VmValue::Ref(key), VmValue::Int(99)}).AsInt() == 99);
        const auto copy = f.On(intent, "getExtras", "()Landroid/os/Bundle;").ref;
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.linker.Class(f.model.ObjectClass(copy)).is_boot_dex);
        CHECK(f.On(copy, "getSerializable", "(Ljava/lang/String;)Ljava/io/Serializable;",
                   {VmValue::Ref(key)}).ref == payload);
        f.On(copy, "remove", "(Ljava/lang/String;)V", {VmValue::Ref(key)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
        const auto text = f.vm.NewStringUtf8("same identity");
        f.On(intent, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Ref(text)});
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                   {VmValue::Ref(key)}).ref == text);
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(payload));
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Int(42)});
        const auto boxed = f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                                {VmValue::Ref(key)}).ref;
        CHECK(f.On(boxed, "intValue", "()I").AsInt() == 42);
        f.On(intent, "putExtra", "(Ljava/lang/String;Ljava/io/Serializable;)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK_FALSE(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;",
                         {VmValue::Ref(key)}).ref.IsValid());
        f.On(intent, "removeExtra", "(Ljava/lang/String;)V", {VmValue::Ref(key)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 0);
        // Android permits null keys. ArrayMap collision chains and live views
        // are exercised through Bundle, including the cached array reuse path.
        for (const char* name : {"Aa", "BB", "third"})
            f.On(copy, "putInt", "(Ljava/lang/String;I)V",
                 {VmValue::Ref(f.vm.NewStringUtf8(name)), VmValue::Int(7)});
        f.On(copy, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
             {VmValue::Ref(VmObjectRef{}), VmValue::Ref(text)});
        CHECK(f.On(copy, "size", "()I").AsInt() == 4);
        const auto keys = f.On(copy, "keySet", "()Ljava/util/Set;").ref;
        const auto keys_root = f.vm.ProtectReferences(std::array{keys});
        const auto iterator = f.On(keys, "iterator", "()Ljava/util/Iterator;").ref;
        const auto iterator_root = f.vm.ProtectReferences(std::array{iterator});
        int count{};
        while (f.On(iterator, "hasNext", "()Z").AsInt()) {
            f.On(iterator, "next", "()Ljava/lang/Object;");
            f.On(iterator, "remove", "()V");
            ++count;
        }
        CHECK(count == 4);
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
    }
}

TEST_CASE("Binder BootDex Bundle Parcel uses the byte protocol and reconstructs values") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        const auto bundle = f.New("Landroid/os/Bundle;");
        const auto key = f.vm.NewStringUtf8("bytes");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        const auto bytes = f.Bytes("snapshot child");
        f.On(bundle, "putSerializable", "(Ljava/lang/String;Ljava/io/Serializable;)V",
             {VmValue::Ref(key), VmValue::Ref(bytes)});
        f.On(bundle, "writeToParcel", "(Landroid/os/Parcel;I)V",
             {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(bundle, "clear", "()V");
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(bytes));
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(bundle));
        static_cast<void>(f.vm.CollectGarbage("dvm117-parcel-snapshot"));
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto type = f.linker.ResolveDescriptor("Landroid/os/Bundle;");
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef(f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]);
        const auto copy = f.On(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;",
                               {VmValue::Ref(parcel)}).ref;
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.BytesOf(f.On(copy, "getByteArray", "(Ljava/lang/String;)[B", {VmValue::Ref(key)}).ref) ==
              "snapshot child");
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(2)}).ref;
        CHECK(f.linker.Class(f.model.ObjectClass(array)).descriptor == "[Landroid/os/Bundle;");
        f.On(copy, "clear", "()V");
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto another = f.On(parcel, "readBundle", "()Landroid/os/Bundle;").ref;
        const auto another_bytes = f.On(
            another, "getSerializable",
            "(Ljava/lang/String;)Ljava/io/Serializable;",
            {VmValue::Ref(key)}).ref;
        CHECK(f.BytesOf(another_bytes) == "snapshot child");
        f.On(parcel, "recycle", "()V");
        // Retire the nested Bundle copy constructor's last Java return root.
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
    }
}

TEST_CASE("Binder byte Parcel preserves API19 positions append and pure marshalling") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto source = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto target = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{source, target});
        const auto text = f.model.NewString(u"Parcel \U0001F331");
        f.On(source, "writeInt", "(I)V", {VmValue::Int(0x12345678)});
        f.On(source, "writeString", "(Ljava/lang/String;)V", {VmValue::Ref(text)});
        const auto size = f.On(source, "dataSize", "()I").AsInt();
        CHECK(size > 8);
        CHECK((size & 3) == 0);
        f.On(target, "appendFrom", "(Landroid/os/Parcel;II)V",
             {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(size)});
        f.On(source, "recycle", "()V");
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(target, "readInt", "()I").AsInt() == 0x12345678);
        CHECK(f.model.StringValue(
                  f.On(target, "readString", "()Ljava/lang/String;").ref) ==
              u"Parcel \U0001F331");
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto wire = f.On(target, "marshall", "()[B").ref;
        CHECK(f.model.ArrayLength(wire) == size);

        const auto token = f.vm.NewStringUtf8("example.echo");
        const auto wrong = f.vm.NewStringUtf8("example.other");
        f.On(target, "setDataSize", "(I)V", {VmValue::Int(0)});
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(target, "writeInterfaceToken", "(Ljava/lang/String;)V",
             {VmValue::Ref(token)});
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto mismatch = f.OnOutcome(
            target, "enforceInterface", "(Ljava/lang/String;)V",
            {VmValue::Ref(wrong)});
        REQUIRE(mismatch.exception.IsValid());
        CHECK(f.linker.Class(mismatch.exception_class).descriptor ==
              "Ljava/lang/SecurityException;");
    }
}

TEST_CASE("DVM-118 Resources metrics share display facts and BootDex value semantics") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 960;
        f.context->surface_height = 540;
        f.context->ui_density = 1.5F;
        f.context->ui_scaled_density = 1.75F;
        const auto resources = f.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto root = f.vm.ProtectReferences(std::array{resources});
        const auto metrics = f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref;
        const auto field = [&](VmObjectRef object, const char* name, const char* signature) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, signature);
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        CHECK(f.linker.Class(f.model.ObjectClass(metrics)).is_boot_dex);
        CHECK(field(metrics, "widthPixels", "I") == 960);
        CHECK(field(metrics, "heightPixels", "I") == 540);
        CHECK(field(metrics, "densityDpi", "I") == 240);
        CHECK(std::bit_cast<float>(field(metrics, "density", "F")) == 1.5F);
        CHECK(std::bit_cast<float>(field(metrics, "scaledDensity", "F")) == 1.75F);
        CHECK(field(metrics, "noncompatDensity", "F") == field(metrics, "density", "F"));
        static_cast<void>(f.vm.CollectGarbage("dvm118-resources-metrics"));
        CHECK(f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref == metrics);
        const auto other = f.New("Landroid/util/DisplayMetrics;");
        const auto other_root = f.vm.ProtectReferences(std::array{other});
        CHECK(field(other, "widthPixels", "I") == 0);
        f.On(other, "setToDefaults", "()V");
        CHECK(field(other, "densityDpi", "I") == 240);
        CHECK(std::bit_cast<float>(field(other, "scaledDensity", "F")) == 1.5F);
        const auto display = f.vm.NewIntrinsicInstance("Landroid/view/Display;");
        f.On(display, "getMetrics", "(Landroid/util/DisplayMetrics;)V", {VmValue::Ref(other)});
        CHECK(f.On(metrics, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(other)}).AsInt() == 1);
        CHECK(f.On(metrics, "hashCode", "()I").AsInt() == f.On(other, "hashCode", "()I").AsInt());
        f.context->surface_width = 1280;
        CHECK(f.On(resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref == metrics);
        CHECK(field(metrics, "widthPixels", "I") == 1280);
        CHECK(field(other, "widthPixels", "I") == 960);
        f.On(other, "setTo", "(Landroid/util/DisplayMetrics;)V", {VmValue::Ref(metrics)});
        CHECK(field(other, "widthPixels", "I") == 1280);
        const auto apply = [&](int unit, float value, VmObjectRef target) {
            return f.Static("Landroid/util/TypedValue;", "applyDimension",
                "(IFLandroid/util/DisplayMetrics;)F",
                {VmValue::Int(unit), VmValue::Float(value), VmValue::Ref(target)}).AsFloat();
        };
        CHECK(apply(0, 5.0F, VmObjectRef{}) == 5.0F);
        CHECK(apply(1, 5.0F, metrics) == 7.5F);
        CHECK(apply(2, 5.0F, metrics) == 8.75F);
        CHECK(apply(3, 72.0F, metrics) == doctest::Approx(240.0F));
        CHECK(apply(4, 1.0F, metrics) == 240.0F);
        CHECK(apply(5, 25.4F, metrics) == doctest::Approx(240.0F));
        CHECK(apply(99, 5.0F, metrics) == 0.0F);
        CHECK(f.Static("Landroid/util/TypedValue;", "complexToDimensionPixelSize",
                        "(ILandroid/util/DisplayMetrics;)I",
                        {VmValue::Int(0x501), VmValue::Ref(metrics)}).AsInt() == 8);
        CHECK(f.vm.StringUtf8(f.Static("Landroid/util/TypedValue;", "coerceToString",
                "(II)Ljava/lang/String;", {VmValue::Int(18), VmValue::Int(1)}).ref) == "true");
        const auto bad = f.OnOutcome(display, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V",
                                     {VmValue::Ref(VmObjectRef{})});
        REQUIRE(bad.exception.IsValid());
        CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("DVM-137 Configuration uses API19 Java shape and managed device facts") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 960;
        f.context->surface_height = 540;
        f.context->ui_density = 1.5F;
        const auto resources =
            f.vm.NewIntrinsicInstance("Landroid/content/res/Resources;");
        const auto resources_root = f.vm.ProtectReferences(std::array{resources});
        const auto field = [&](const VmObjectRef object, const char* name,
                               const char* descriptor = "I") {
            const auto id = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        const auto configuration = f.On(
            resources, "getConfiguration",
            "()Landroid/content/res/Configuration;").ref;
        const auto configuration_root =
            f.vm.ProtectReferences(std::array{configuration});
        const auto owner = f.model.ObjectClass(configuration);
        CHECK(f.linker.Class(owner).is_boot_dex);
        CHECK(field(configuration, "touchscreen") == 3);
        CHECK(field(configuration, "keyboard") == 2);
        CHECK(field(configuration, "keyboardHidden") == 1);
        CHECK(field(configuration, "hardKeyboardHidden") == 1);
        CHECK(field(configuration, "navigation") == 1);
        CHECK(field(configuration, "navigationHidden") == 2);
        CHECK(field(configuration, "orientation") == 2);
        CHECK(field(configuration, "screenWidthDp") == 640);
        CHECK(field(configuration, "screenHeightDp") == 360);
        CHECK(field(configuration, "smallestScreenWidthDp") == 360);
        CHECK(field(configuration, "densityDpi") == 240);
        CHECK(field(configuration, "screenLayout") == 0x10000062);
        const auto default_locale = f.Static(
            "Ljava/util/Locale;", "getDefault", "()Ljava/util/Locale;").ref;
        REQUIRE(default_locale.IsValid());
        CHECK(f.Static("Landroid/text/TextUtils;",
                       "getLayoutDirectionFromLocale",
                       "(Ljava/util/Locale;)I",
                       {VmValue::Ref(default_locale)}).AsInt() == 0);
        const auto unsupported_locale = f.New(
            "Ljava/util/Locale;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fr"))});
        const auto unsupported_direction = f.StaticOutcome(
            "Landroid/text/TextUtils;", "getLayoutDirectionFromLocale",
            "(Ljava/util/Locale;)I",
            {VmValue::Ref(unsupported_locale)});
        REQUIRE(unsupported_direction.exception.IsValid());
        CHECK(f.linker.Class(unsupported_direction.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        REQUIRE_FALSE(f.ledger.Unimplemented().empty());
        CHECK(f.ledger.Unimplemented().back().id ==
              "dexvm.locale_layout_direction");
        CHECK_MESSAGE(VmObjectRef(field(configuration, "locale",
                                       "Ljava/util/Locale;")) == default_locale,
                      "configuration locale=",
                      field(configuration, "locale", "Ljava/util/Locale;"),
                      " default locale=", default_locale.Value());
        const auto rendered = f.vm.StringUtf8(
            f.On(configuration, "toString", "()Ljava/lang/String;").ref);
        CHECK(f.vm.StringUtf8(f.On(default_locale, "toString",
                                  "()Ljava/lang/String;").ref) == "en_US");
        CHECK(rendered.find("ldltr") != std::string::npos);

        const auto defaults = f.New("Landroid/content/res/Configuration;");
        const auto defaults_root = f.vm.ProtectReferences(std::array{defaults});
        CHECK(std::bit_cast<float>(field(defaults, "fontScale", "F")) == 1.0F);
        CHECK(field(defaults, "keyboard") == 0);
        CHECK(field(defaults, "orientation") == 0);
        const auto copy = f.New(
            "Landroid/content/res/Configuration;",
            "(Landroid/content/res/Configuration;)V",
            {VmValue::Ref(configuration)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.On(copy, "equals", "(Landroid/content/res/Configuration;)Z",
                   {VmValue::Ref(configuration)}).AsInt() == 1);
        CHECK(f.vm.StringUtf8(
                  f.On(copy, "toString", "()Ljava/lang/String;").ref)
                  .find("land") != std::string::npos);

        const auto parcel = f.Static(
            "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto parcel_root = f.vm.ProtectReferences(std::array{parcel});
        f.On(configuration, "writeToParcel", "(Landroid/os/Parcel;I)V",
             {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto creator_id = f.linker.FindFieldRecursive(
            owner, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_id.has_value());
        const auto creator = VmObjectRef(
            f.linker.Class(owner)
                .static_storage[f.linker.Field(*creator_id).slot]);
        REQUIRE(creator.IsValid());
        const auto restored = f.On(
            creator, "createFromParcel",
            "(Landroid/os/Parcel;)Ljava/lang/Object;",
            {VmValue::Ref(parcel)}).ref;
        CHECK(f.On(restored, "equals",
                   "(Landroid/content/res/Configuration;)Z",
                   {VmValue::Ref(configuration)}).AsInt() == 1);

        static_cast<void>(f.vm.CollectGarbage("dvm137-configuration"));
        CHECK(f.On(resources, "getConfiguration",
                   "()Landroid/content/res/Configuration;").ref == configuration);
        CHECK(VmObjectRef(field(configuration, "locale",
                                "Ljava/util/Locale;")) == default_locale);
        f.context->surface_width = 540;
        f.context->surface_height = 960;
        CHECK(f.On(resources, "getConfiguration",
                   "()Landroid/content/res/Configuration;").ref == configuration);
        CHECK(field(configuration, "orientation") == 1);
        CHECK(field(configuration, "screenWidthDp") == 360);
        CHECK(field(configuration, "screenHeightDp") == 640);
    }
}

TEST_CASE("DVM-138 View.getParent follows the live UiTree hierarchy") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto first_parent = f.New(
            "Landroid/widget/RelativeLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second_parent = f.New(
            "Landroid/widget/FrameLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});

        const auto view_parent =
            f.linker.ResolveDescriptor("Landroid/view/ViewParent;");
        CHECK(f.linker.Class(view_parent).is_interface);
        CHECK(f.linker.IsAssignable(
            view_parent, f.model.ObjectClass(first_parent)));
        const auto method = f.linker.FindVtableIndex(
            f.model.ObjectClass(child), "getParent",
            "()Landroid/view/ViewParent;");
        REQUIRE(method.has_value());
        CHECK((f.linker.Method(
                   f.linker.Class(f.model.ObjectClass(child))
                       .vtable[*method])
                   .access_flags &
               (kAccPublic | kAccFinal)) ==
              (kAccPublic | kAccFinal));

        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(first_parent, "addView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == first_parent);
        f.On(first_parent, "removeView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(second_parent, "addView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == second_parent);
        f.On(second_parent, "removeViews", "(II)V",
             {VmValue::Int(0), VmValue::Int(1)});
        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());

        f.On(activity, "setContentView", "(Landroid/view/View;)V",
             {VmValue::Ref(first_parent)});
        CHECK_FALSE(f.On(first_parent, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());
    }
}

TEST_CASE("DVM-119 Java layout params drive FrameLayout geometry and copy semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 100;
        f.context->surface_height = 100;
        const auto activity = f.New("Landroid/app/Activity;");
        const auto frame = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto frame_params = f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V",
                                         {VmValue::Int(-1), VmValue::Int(-1)});
        f.On(frame, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(frame_params)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto params = f.New("Landroid/widget/FrameLayout$LayoutParams;", "(III)V",
                                   {VmValue::Int(20), VmValue::Int(10), VmValue::Int(85)});
        f.On(params, "setMargins", "(IIII)V",
             {VmValue::Int(1), VmValue::Int(2), VmValue::Int(3), VmValue::Int(4)});
        CHECK(f.linker.Class(f.model.ObjectClass(params)).is_boot_dex);
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/view/ViewGroup$MarginLayoutParams;"),
                                    f.model.ObjectClass(params)));
        const auto copy = f.New("Landroid/widget/FrameLayout$LayoutParams;",
                                "(Landroid/widget/FrameLayout$LayoutParams;)V", {VmValue::Ref(params)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        const auto field = [&](VmObjectRef object, const char* name) -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, "I");
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot];
        };
        CHECK(field(copy, "gravity").bits == 85);
        CHECK(field(copy, "bottomMargin").bits == 4);
        f.On(frame, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(child), VmValue::Ref(params)});
        f.On(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(frame)});
        CHECK(f.On(child, "getLeft", "()I").AsInt() == 77);
        CHECK(f.On(child, "getTop", "()I").AsInt() == 86);
        CHECK(f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == params);
        field(params, "width").bits = 30;
        f.On(child, "requestLayout", "()V");
        CHECK(f.On(child, "getLeft", "()I").AsInt() == 67);
        CHECK(f.On(child, "getWidth", "()I").AsInt() == 30);
        CHECK(field(copy, "width").bits == 20);
        const auto bad = f.OnOutcome(child, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
                                     {VmValue::Ref(VmObjectRef{})});
        REQUIRE(bad.exception.IsValid());
        CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        const auto relative = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                    {VmValue::Int(10), VmValue::Int(10)});
        f.On(relative, "addRule", "(I)V", {VmValue::Int(4)});
        const auto baseline = f.OnOutcome(child, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
                                          {VmValue::Ref(relative)});
        REQUIRE(baseline.exception.IsValid());
        CHECK(f.linker.Class(baseline.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == params);
        const auto invalid = f.OnOutcome(relative, "addRule", "(I)V", {VmValue::Int(22)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/ArrayIndexOutOfBoundsException;");
        const auto group = f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto group_params = f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V",
                                         {VmValue::Int(100), VmValue::Int(100)});
        f.On(group, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(group_params)});
        const auto first = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        f.On(first, "setId", "(I)V", {VmValue::Int(1001)});
        const auto first_params = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                         {VmValue::Int(20), VmValue::Int(10)});
        const auto second_params = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                          {VmValue::Int(10), VmValue::Int(10)});
        f.On(second_params, "addRule", "(II)V", {VmValue::Int(1), VmValue::Int(1001)});
        f.On(group, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(first), VmValue::Ref(first_params)});
        f.On(group, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(second), VmValue::Ref(second_params)});
        f.On(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        CHECK(f.On(group, "getGravity", "()I").AsInt() == 0x00800033);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(85)});
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 70);
        CHECK(f.On(second, "getLeft", "()I").AsInt() == 90);
        CHECK(f.On(first, "getTop", "()I").AsInt() == 90);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(17)});
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 35);
        CHECK(f.On(second, "getLeft", "()I").AsInt() == 55);
        CHECK(f.On(first, "getTop", "()I").AsInt() == 45);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(group, "getGravity", "()I").AsInt() == 0x00800033);
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-167 ViewGroup width-height add uses virtual default params") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto relative = f.New(
            "Landroid/widget/RelativeLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});

        f.On(relative, "addView", "(Landroid/view/View;II)V",
             {VmValue::Ref(child), VmValue::Int(-1), VmValue::Int(-1)});
        const auto params = f.On(
            child, "getLayoutParams",
            "()Landroid/view/ViewGroup$LayoutParams;").ref;
        REQUIRE(params.IsValid());
        CHECK(f.model.ObjectClass(params) ==
              f.linker.ResolveDescriptor(
                  "Landroid/widget/RelativeLayout$LayoutParams;"));
        const auto field = [&](const char* name) -> std::int32_t {
            const auto id = f.linker.FindFieldRecursive(
                f.model.ObjectClass(params), name, "I");
            REQUIRE(id.has_value());
            return static_cast<std::int32_t>(
                f.model.InstanceSlots(params)[f.linker.Field(*id).slot].bits);
        };
        CHECK(field("width") == -1);
        CHECK(field("height") == -1);
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == relative);
    }
}

TEST_CASE("DVM-97 action-only Intent follows the LocalBroadcastManager match chain") {
    AndroidValueVm fixture;
    const auto action = fixture.vm.NewStringUtf8("org.example.PLANT");
    const auto intent = fixture.New(
        "Landroid/content/Intent;", "(Ljava/lang/String;)V",
        {VmValue::Ref(action)});
    const auto filter = fixture.New(
        "Landroid/content/IntentFilter;", "(Ljava/lang/String;)V",
        {VmValue::Ref(action)});

    CHECK(fixture.vm.StringUtf8(
        fixture.On(intent, "getAction", "()Ljava/lang/String;").ref) ==
          "org.example.PLANT");
    CHECK_FALSE(fixture.On(intent, "getData", "()Landroid/net/Uri;")
                    .ref.IsValid());
    CHECK_FALSE(fixture.On(intent, "getScheme", "()Ljava/lang/String;")
                    .ref.IsValid());
    CHECK_FALSE(fixture.On(intent, "getCategories", "()Ljava/util/Set;")
                    .ref.IsValid());
    CHECK(fixture.On(intent, "getFlags", "()I").AsInt() == 0);
    CHECK_FALSE(fixture.On(
        intent, "resolveTypeIfNeeded",
        "(Landroid/content/ContentResolver;)Ljava/lang/String;",
        {VmValue::Ref(VmObjectRef{})}).ref.IsValid());

    CHECK(fixture.On(filter, "countActions", "()I").AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(fixture.On(
        filter, "getAction", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) ==
          "org.example.PLANT");
    CHECK(fixture.On(
        filter, "match",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
        "Landroid/net/Uri;Ljava/util/Set;Ljava/lang/String;)I",
        {VmValue::Ref(action), VmValue::Ref(VmObjectRef{}),
         VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
         VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})}).AsInt() ==
          0x00108000);

    fixture.On(intent, "setFlags", "(I)Landroid/content/Intent;",
               {VmValue::Int(0x04)});
    fixture.On(intent, "addFlags", "(I)Landroid/content/Intent;",
               {VmValue::Int(0x08)});
    CHECK(fixture.On(intent, "getFlags", "()I").AsInt() == 0x0c);
}

TEST_CASE("DVM-97 IntentFilter matches bounded MIME URI authority and categories") {
    AndroidValueVm fixture;
    const auto action = fixture.vm.NewStringUtf8("org.example.VIEW");
    const auto mime = fixture.vm.NewStringUtf8("image/png");
    const auto uri_text =
        fixture.vm.NewStringUtf8("content://cdn.Example.com:443/plants/pea");
    const auto uri = fixture.Static(
        "Landroid/net/Uri;", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        {VmValue::Ref(uri_text)}).ref;

    CHECK(fixture.vm.StringUtf8(
        fixture.On(uri, "getScheme", "()Ljava/lang/String;").ref) == "content");
    CHECK(fixture.vm.StringUtf8(
        fixture.On(uri, "getHost", "()Ljava/lang/String;").ref) ==
          "cdn.Example.com");
    CHECK(fixture.On(uri, "getPort", "()I").AsInt() == 443);
    CHECK(fixture.vm.StringUtf8(
        fixture.On(uri, "getPath", "()Ljava/lang/String;").ref) ==
          "/plants/pea");
    CHECK(fixture.On(uri, "toString", "()Ljava/lang/String;").ref == uri_text);

    const auto intent = fixture.New(
        "Landroid/content/Intent;",
        "(Ljava/lang/String;Landroid/net/Uri;)V",
        {VmValue::Ref(action), VmValue::Ref(uri)});
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(intent)).is_boot_dex);
    fixture.On(intent, "setDataAndType",
               "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(uri), VmValue::Ref(mime)});
    const auto category = fixture.vm.NewStringUtf8("org.example.GREEN");
    fixture.On(intent, "addCategory",
               "(Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(category)});
    const auto categories =
        fixture.On(intent, "getCategories", "()Ljava/util/Set;").ref;
    REQUIRE(categories.IsValid());
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(categories)).descriptor ==
          "Landroid/util/ArraySet;");
    CHECK(fixture.On(intent, "hasCategory", "(Ljava/lang/String;)Z",
                     {VmValue::Ref(category)}).AsInt() == 1);
    CHECK(fixture.On(
        intent, "resolveTypeIfNeeded",
        "(Landroid/content/ContentResolver;)Ljava/lang/String;",
        {VmValue::Ref(VmObjectRef{})}).ref == mime);

    const auto filter = fixture.New(
        "Landroid/content/IntentFilter;",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(action), VmValue::Ref(mime)});
    fixture.On(filter, "addDataScheme", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("content"))});
    fixture.On(filter, "addDataAuthority",
               "(Ljava/lang/String;Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("*.example.com")),
                VmValue::Ref(fixture.vm.NewStringUtf8("443"))});
    fixture.On(filter, "addCategory", "(Ljava/lang/String;)V",
               {VmValue::Ref(category)});
    CHECK(fixture.On(filter, "countDataTypes", "()I").AsInt() == 1);
    CHECK(fixture.On(filter, "countDataSchemes", "()I").AsInt() == 1);
    CHECK(fixture.On(filter, "countCategories", "()I").AsInt() == 1);

    const auto partial_filter = fixture.New("Landroid/content/IntentFilter;");
    fixture.On(partial_filter, "addDataType", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("image/*"))});
    CHECK(fixture.On(
        partial_filter, "hasDataType", "(Ljava/lang/String;)Z",
        {VmValue::Ref(fixture.vm.NewStringUtf8("image/jpeg"))}).AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(fixture.On(
        partial_filter, "getDataType", "(I)Ljava/lang/String;",
        {VmValue::Int(0)}).ref) == "image");

    const auto match = [&](const VmObjectRef requested_action,
                           const VmObjectRef requested_type,
                           const VmObjectRef requested_uri,
                           const VmObjectRef requested_categories) {
        const auto requested_scheme = requested_uri.IsValid()
            ? fixture.On(requested_uri, "getScheme", "()Ljava/lang/String;").ref
            : VmObjectRef{};
        return fixture.On(
            filter, "match",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Landroid/net/Uri;Ljava/util/Set;Ljava/lang/String;)I",
            {VmValue::Ref(requested_action), VmValue::Ref(requested_type),
             VmValue::Ref(requested_scheme), VmValue::Ref(requested_uri),
             VmValue::Ref(requested_categories),
             VmValue::Ref(VmObjectRef{})}).AsInt();
    };
    CHECK(match(action, mime, uri, categories) == 0x00608000);
    CHECK(match(fixture.vm.NewStringUtf8("org.example.OTHER"), mime, uri,
                categories) == -3);
    CHECK(match(action, fixture.vm.NewStringUtf8("text/plain"), uri,
                categories) == -1);
    const auto wrong_uri = fixture.Static(
        "Landroid/net/Uri;", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        {VmValue::Ref(fixture.vm.NewStringUtf8(
            "content://example.org:443/plants/pea"))}).ref;
    CHECK(match(action, mime, wrong_uri, categories) == -2);

    const auto unmatched_intent = fixture.New("Landroid/content/Intent;");
    fixture.On(unmatched_intent, "addCategory",
               "(Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(fixture.vm.NewStringUtf8("org.example.BLUE"))});
    const auto unmatched_categories = fixture.On(
        unmatched_intent, "getCategories", "()Ljava/util/Set;").ref;
    CHECK(match(action, mime, uri, unmatched_categories) == -4);
}

TEST_CASE("DVM-132 API 19 Uri executes from BootDex on both interpreters") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto text = fixture.vm.NewStringUtf8(
            "content://user@cdn.example.com:443/plants/pea%20pod?kind=snow%20pea#leaf");
        const auto uri = fixture.Static(
            "Landroid/net/Uri;", "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;",
            {VmValue::Ref(text)}).ref;

        const auto owner = fixture.model.ObjectClass(uri);
        CHECK(fixture.linker.Class(owner).descriptor ==
              "Landroid/net/Uri$StringUri;");
        CHECK(fixture.linker.Class(owner).is_boot_dex);
        CHECK(fixture.vm.StringUtf8(
            fixture.On(uri, "getScheme", "()Ljava/lang/String;").ref) ==
              "content");
        CHECK(fixture.vm.StringUtf8(
            fixture.On(uri, "getHost", "()Ljava/lang/String;").ref) ==
              "cdn.example.com");
        CHECK(fixture.On(uri, "getPort", "()I").AsInt() == 443);
        CHECK(fixture.vm.StringUtf8(
            fixture.On(uri, "getPath", "()Ljava/lang/String;").ref) ==
              "/plants/pea pod");
        CHECK(fixture.vm.StringUtf8(
            fixture.On(uri, "getQueryParameter",
                       "(Ljava/lang/String;)Ljava/lang/String;",
                       {VmValue::Ref(fixture.vm.NewStringUtf8("kind"))}).ref) ==
              "snow pea");
        CHECK(fixture.vm.StringUtf8(
            fixture.On(uri, "toString", "()Ljava/lang/String;").ref) ==
              fixture.vm.StringUtf8(text));
        CHECK(fixture.On(
            text, "regionMatches", "(ILjava/lang/String;II)Z",
            {VmValue::Int(0), VmValue::Ref(text), VmValue::Int(0),
             VmValue::Int(-1)}).AsInt() == 1);

        const auto builder = fixture.On(
            uri, "buildUpon", "()Landroid/net/Uri$Builder;").ref;
        REQUIRE(builder.IsValid());
        fixture.On(
            builder, "appendPath",
            "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("winter mint"))});
        fixture.On(
            builder, "appendQueryParameter",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri$Builder;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("level")),
             VmValue::Ref(fixture.vm.NewStringUtf8("1+2"))});
        const auto built = fixture.On(
            builder, "build", "()Landroid/net/Uri;").ref;
        CHECK(fixture.vm.StringUtf8(
            fixture.On(built, "toString", "()Ljava/lang/String;").ref) ==
              "content://user@cdn.example.com:443/plants/pea%20pod/winter%20mint"
              "?kind=snow%20pea&level=1%2B2#leaf");

        const auto encoded = fixture.Static(
            "Landroid/net/Uri;", "encode",
            "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("雪 pea/+"))}).ref;
        CHECK(fixture.vm.StringUtf8(encoded) == "%E9%9B%AA%20pea%2F%2B");
        CHECK(fixture.vm.StringUtf8(fixture.Static(
            "Landroid/net/Uri;", "decode",
            "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(encoded)}).ref) == "雪 pea/+");
    }
}

TEST_CASE("DVM-220 BootDex sensor clients share an honest empty device boundary") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        std::int32_t callbacks{};
        auto listener = IntrinsicClassBuilder::Class("Ltest/SensorClient;", "Ljava/lang/Object;",
            {"Landroid/hardware/SensorListener;", "Landroid/hardware/SensorEventListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        const auto callback = [&callbacks](IntrinsicContext&) { ++callbacks; return VmValue::Void(); };
        listener.VirtualMethod("onSensorChanged", "(I[F)V", callback);
        listener.VirtualMethod("onAccuracyChanged", "(II)V", callback);
        listener.VirtualMethod("onSensorChanged", "(Landroid/hardware/SensorEvent;)V", callback);
        listener.VirtualMethod("onAccuracyChanged", "(Landroid/hardware/Sensor;I)V", callback);
        AndroidValueVm f(backend, {std::move(listener).Build()});
        for (const auto* name : {"Sensor", "SensorEvent", "SensorManager", "LocalSensorManager",
                                "LegacySensorManager", "TriggerEvent", "TriggerEventListener"}) {
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
            return f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                {VmValue::Ref(f.vm.NewStringUtf8("sensor"))}).ref;
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
        CHECK_FALSE(f.On(manager, "getDefaultSensor", "(I)Landroid/hardware/Sensor;",
                         {VmValue::Int(1)}).ref.IsValid());
        const auto add = f.OnOutcome(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(add.exception.IsValid());
        CHECK(f.linker.Class(add.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(manager, "getSensors", "()I").AsInt() == 0);
        for (const auto mask : {1, 2, 128}) {
            CHECK(f.On(manager, "registerListener", "(Landroid/hardware/SensorListener;I)Z",
                {VmValue::Ref(client), VmValue::Int(mask)}).AsInt() == 0);
            CHECK(f.On(manager, "registerListener", "(Landroid/hardware/SensorListener;II)Z",
                {VmValue::Ref(client), VmValue::Int(mask), VmValue::Int(3)}).AsInt() == 0);
            f.On(manager, "unregisterListener", "(Landroid/hardware/SensorListener;I)V",
                 {VmValue::Ref(client), VmValue::Int(mask)});
        }
        f.On(manager, "unregisterListener", "(Landroid/hardware/SensorListener;)V", {VmValue::Ref(client)});
        CHECK(f.On(manager, "registerListener",
            "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z",
            {VmValue::Ref(client), VmValue::Ref(VmObjectRef{}), VmValue::Int(3)}).AsInt() == 0);
        f.On(manager, "unregisterListener", "(Landroid/hardware/SensorEventListener;)V", {VmValue::Ref(client)});
        CHECK(f.On(manager, "flush", "(Landroid/hardware/SensorEventListener;)Z", {VmValue::Ref(client)}).AsInt() == 0);
        for (const auto& failure : {
                 f.OnOutcome(manager, "flush", "(Landroid/hardware/SensorEventListener;)Z", {VmValue::Ref(VmObjectRef{})}),
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

TEST_CASE("DVM-224 notification cancellation uses an explicit empty application inventory") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto foreign = IntrinsicClassBuilder::Class("Ltest/ForeignNotificationContext;", "Landroid/content/Context;");
        foreign.OverrideMethod("getPackageName", "()Ljava/lang/String;",
            [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8("other.application")); });
        AndroidValueVm f(backend, {std::move(foreign).Build()});
        f.context->package_name = "fixture";
        f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) {
            VisitAndroidSessionRoots(*f.context, visit);
        }});
        const auto base = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{base});
        const auto service = [&] {
            return f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                {VmValue::Ref(f.vm.NewStringUtf8("notification"))}).ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).is_boot_dex);
        CHECK(f.Static("Landroid/app/NotificationManager;", "from",
            "(Landroid/content/Context;)Landroid/app/NotificationManager;", {VmValue::Ref(base)}).ref == manager);
        for (const auto id : {-1, 0, 123, 2147483647}) {
            f.On(manager, "cancel", "(I)V", {VmValue::Int(id)});
            for (const auto tag : {VmObjectRef{}, f.vm.NewStringUtf8(""), f.vm.NewStringUtf8("download")}) {
                f.On(manager, "cancel", "(Ljava/lang/String;I)V", {VmValue::Ref(tag), VmValue::Int(id)});
                f.On(manager, "cancel", "(Ljava/lang/String;I)V", {VmValue::Ref(tag), VmValue::Int(id)});
            }
        }
        f.On(manager, "cancelAll", "()V");
        f.On(manager, "cancelAll", "()V");
        CHECK(f.ledger.Unimplemented().empty());
        // Cancellation never invalidates process-local PendingIntent tokens.
        const auto intent = f.New("Landroid/content/Intent;");
        const auto operation = f.Static("Landroid/app/PendingIntent;", "getService",
            "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;",
            {VmValue::Ref(base), VmValue::Int(4), VmValue::Ref(intent), VmValue::Int(0)}).ref;
        const auto operation_root = f.vm.ProtectReferences(std::array{operation});
        f.On(manager, "cancelAll", "()V");
        REQUIRE(f.context->pending_intents.contains(operation.Value()));
        CHECK_FALSE(f.context->pending_intents.at(operation.Value()).canceled);
        static_cast<void>(f.vm.CollectGarbage("notification-empty-inventory"));
        CHECK(service() == manager);
        f.On(manager, "cancel", "(I)V", {VmValue::Int(123)});
        const auto null_post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V",
            {VmValue::Int(1), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_post.exception.IsValid());
        CHECK(f.linker.Class(null_post.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        // The private publication boundary must fail even before value/renderer support is added.
        const auto post = f.linker.FindDirectMethod(f.model.ObjectClass(manager), "nativeRejectPost", "(Ljava/lang/String;)V");
        REQUIRE(post.has_value());
        const auto rejected = f.vm.Call(*post, std::array{VmValue::Ref(manager), VmValue::Ref(f.vm.NewStringUtf8("fixture"))});
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.notification_post");
        CHECK(hits[0].count == 1);
        const auto other = f.vm.NewIntrinsicInstance("Ltest/ForeignNotificationContext;");
        const auto denied = f.OnOutcome(other, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(f.vm.NewStringUtf8("notification"))});
        REQUIRE(denied.exception.IsValid());
        CHECK(f.linker.Class(denied.exception_class).descriptor == "Ljava/lang/SecurityException;");
        const auto cancel = f.linker.FindDirectMethod(f.model.ObjectClass(manager), "nativeCancel", "(Ljava/lang/String;Ljava/lang/String;IZ)V");
        REQUIRE(cancel.has_value());
        const auto foreign_cancel = f.vm.Call(*cancel, std::array{VmValue::Ref(manager),
            VmValue::Ref(f.vm.NewStringUtf8("other.application")), VmValue::Ref(VmObjectRef{}), VmValue::Int(1), VmValue::Int(0)});
        REQUIRE(foreign_cancel.exception.IsValid());
        CHECK(f.linker.Class(foreign_cancel.exception_class).descriptor == "Ljava/lang/SecurityException;");
    }
}

TEST_CASE("DVM-222 NFC discovery preserves API19 absence without transport") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        std::int32_t callbacks{};
        auto listener = IntrinsicClassBuilder::Class("Ltest/NdefClient;", "Ljava/lang/Object;",
            {"Landroid/nfc/NfcAdapter$CreateNdefMessageCallback;",
             "Landroid/nfc/NfcAdapter$OnNdefPushCompleteCallback;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod("createNdefMessage", "(Landroid/nfc/NfcEvent;)Landroid/nfc/NdefMessage;",
            [&callbacks](IntrinsicContext&) { ++callbacks; return VmValue::Ref(VmObjectRef{}); });
        listener.VirtualMethod("onNdefPushComplete", "(Landroid/nfc/NfcEvent;)V",
            [&callbacks](IntrinsicContext&) { ++callbacks; return VmValue::Void(); });
        auto mock = IntrinsicClassBuilder::Class("Ltest/NoApplicationContext;", "Landroid/content/Context;");
        mock.OverrideMethod("getApplicationContext", "()Landroid/content/Context;",
            [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
        AndroidValueVm f(backend, {std::move(listener).Build(), std::move(mock).Build()});
        f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) {
            VisitAndroidSessionRoots(*f.context, visit);
        }});
        for (const auto* descriptor : {"Landroid/nfc/NfcAdapter;", "Landroid/nfc/NfcManager;",
             "Landroid/nfc/NfcEvent;", "Landroid/nfc/NdefMessage;", "Landroid/nfc/NdefRecord;"})
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
            return f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                {VmValue::Ref(f.vm.NewStringUtf8("nfc"))}).ref;
        };
        const auto manager = service();
        REQUIRE(manager.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(manager)).descriptor == "Landroid/nfc/NfcManager;");
        CHECK_FALSE(f.On(manager, "getDefaultAdapter", "()Landroid/nfc/NfcAdapter;").ref.IsValid());
        CHECK_FALSE(f.Static("Landroid/nfc/NfcAdapter;", "getDefaultAdapter",
            "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;", {VmValue::Ref(base)}).ref.IsValid());
        const auto pm = f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        for (const auto* feature : {"android.hardware.nfc", "android.hardware.nfc.hce"})
            CHECK(f.On(pm, "hasSystemFeature", "(Ljava/lang/String;)Z",
                {VmValue::Ref(f.vm.NewStringUtf8(feature))}).AsInt() == 0);
        const auto no_application = f.vm.NewIntrinsicInstance("Ltest/NoApplicationContext;");
        for (const auto context : {VmObjectRef{}, no_application}) {
            const auto result = f.StaticOutcome("Landroid/nfc/NfcAdapter;", "getDefaultAdapter",
                "(Landroid/content/Context;)Landroid/nfc/NfcAdapter;", {VmValue::Ref(context)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        }
        const auto legacy = f.StaticOutcome("Landroid/nfc/NfcAdapter;", "getDefaultAdapter",
                                            "()Landroid/nfc/NfcAdapter;");
        REQUIRE(legacy.exception.IsValid());
        CHECK(f.linker.Class(legacy.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        // Inject an otherwise unobtainable receiver to verify that registration
        // never silently succeeds or retains a callback outside the device path.
        const auto adapter = f.vm.NewIntrinsicInstance("Landroid/nfc/NfcAdapter;");
        for (const auto& [name, signature] : std::array{
             std::pair{"setNdefPushMessageCallback", "(Landroid/nfc/NfcAdapter$CreateNdefMessageCallback;Landroid/app/Activity;[Landroid/app/Activity;)V"},
             std::pair{"setOnNdefPushCompleteCallback", "(Landroid/nfc/NfcAdapter$OnNdefPushCompleteCallback;Landroid/app/Activity;[Landroid/app/Activity;)V"}}) {
            const auto result = f.OnOutcome(adapter, name, signature,
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

TEST_CASE("DVM-136 API 19 OrientationEventListener preserves absent sensor semantics") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t orientation_callbacks{};
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/OrientationListener;",
            "Landroid/view/OrientationEventListener;");
        listener.OverrideMethod(
            "onOrientationChanged", "(I)V",
            [&orientation_callbacks](IntrinsicContext&) {
                ++orientation_callbacks;
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(listener).Build()});

        const auto orientation_type = fixture.linker.ResolveDescriptor(
            "Landroid/view/OrientationEventListener;");
        CHECK(fixture.linker.Class(orientation_type).is_boot_dex);
        const auto implementation_type = fixture.linker.ResolveDescriptor(
            "Landroid/view/OrientationEventListener$SensorEventListenerImpl;");
        CHECK(fixture.linker.Class(implementation_type).is_boot_dex);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto object = fixture.vm.NewIntrinsicInstance(
            "Ltest/OrientationListener;");
        const auto constructor = fixture.linker.FindDirectMethod(
            orientation_type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor.has_value());
        const std::array constructor_arguments{
            VmValue::Ref(object), VmValue::Ref(context)};
        const auto constructed = fixture.vm.Call(
            *constructor, constructor_arguments);
        REQUIRE_MESSAGE(!constructed.exception.IsValid(),
                        constructed.exception_message);

        CHECK(fixture.On(object, "canDetectOrientation", "()Z").AsInt() == 0);
        static_cast<void>(fixture.On(object, "enable", "()V"));
        static_cast<void>(fixture.On(object, "enable", "()V"));
        static_cast<void>(fixture.On(object, "disable", "()V"));
        CHECK(orientation_callbacks == 0);
    }
}

TEST_CASE("DVM-156 location facade links listeners and exposes no location source") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/LocationListener;", "Ljava/lang/Object;",
            {"Landroid/location/LocationListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        listener.VirtualMethod(
            "onLocationChanged", "(Landroid/location/Location;)V",
            [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod(
            "onStatusChanged",
            "(Ljava/lang/String;ILandroid/os/Bundle;)V",
            [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod(
            "onProviderEnabled", "(Ljava/lang/String;)V",
            [](IntrinsicContext&) { return VmValue::Void(); });
        listener.VirtualMethod(
            "onProviderDisabled", "(Ljava/lang/String;)V",
            [](IntrinsicContext&) { return VmValue::Void(); });

        AndroidValueVm fixture(backend, {std::move(listener).Build()});
        const auto context = fixture.New("Landroid/content/Context;");
        const auto service_name = fixture.vm.NewStringUtf8("location");
        const auto manager = fixture.On(
            context, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(service_name)}).ref;
        REQUIRE(manager.IsValid());
        CHECK(fixture.On(
            context, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(service_name)}).ref == manager);
        CHECK(fixture.model.ObjectClass(manager) ==
              fixture.linker.ResolveDescriptor(
                  "Landroid/location/LocationManager;"));

        const auto criteria = fixture.New("Landroid/location/Criteria;");
        CHECK_FALSE(fixture.On(
            manager, "getBestProvider",
            "(Landroid/location/Criteria;Z)Ljava/lang/String;",
            {VmValue::Ref(criteria), VmValue::Int(1)}).ref.IsValid());
        const auto provider = fixture.vm.NewStringUtf8("gps");
        CHECK_FALSE(fixture.On(
            manager, "getLastKnownLocation",
            "(Ljava/lang/String;)Landroid/location/Location;",
            {VmValue::Ref(provider)}).ref.IsValid());

        const auto callback = fixture.New("Ltest/LocationListener;");
        const auto request = fixture.OnOutcome(
            manager, "requestLocationUpdates",
            "(Ljava/lang/String;JFLandroid/location/LocationListener;"
            "Landroid/os/Looper;)V",
            {VmValue::Ref(provider), VmValue::Long(0), VmValue::Float(0.0F),
             VmValue::Ref(callback), VmValue::Ref(VmObjectRef{})});
        REQUIRE(request.exception.IsValid());
        CHECK(fixture.linker.Class(request.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto remove = fixture.OnOutcome(
            manager, "removeUpdates",
            "(Landroid/location/LocationListener;)V",
            {VmValue::Ref(callback)});
        REQUIRE(remove.exception.IsValid());
        CHECK(fixture.linker.Class(remove.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = fixture.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.location_updates");
        CHECK(hits[0].count == 2);
    }
}

TEST_CASE("DVM-166 keyguard facade reads the replaceable platform snapshot") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto context = fixture.New("Landroid/content/Context;");
        const auto service_name = fixture.vm.NewStringUtf8("keyguard");
        const auto manager = fixture.On(
            context, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(service_name)}).ref;
        REQUIRE(manager.IsValid());
        CHECK(fixture.model.ObjectClass(manager) ==
              fixture.linker.ResolveDescriptor(
                  "Landroid/app/KeyguardManager;"));
        CHECK(fixture.On(manager, "isKeyguardLocked", "()Z").AsInt() == 0);
        CHECK(fixture.On(manager, "isKeyguardSecure", "()Z").AsInt() == 0);
        CHECK(fixture.On(manager, "inKeyguardRestrictedInputMode", "()Z")
                  .AsInt() == 0);

        const auto second = fixture.On(
            context, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(service_name)}).ref;
        CHECK(second.IsValid());
        CHECK(second != manager);

        fixture.context->keyguard_state_provider = [] {
            return AndroidKeyguardState{
                .locked = true,
                .secure = true,
                .restricted_input = true};
        };
        CHECK(fixture.On(manager, "isKeyguardLocked", "()Z").AsInt() == 1);
        CHECK(fixture.On(manager, "isKeyguardSecure", "()Z").AsInt() == 1);
        CHECK(fixture.On(manager, "inKeyguardRestrictedInputMode", "()Z")
                  .AsInt() == 1);
    }
}

TEST_CASE("DVM-174 AnimationListener type compatibility links and dispatches") {
    constexpr auto kListener =
        "Landroid/view/animation/Animation$AnimationListener;";
    constexpr auto kAnimation = "Landroid/view/animation/Animation;";
    constexpr auto kSignature =
        "(Landroid/view/animation/Animation;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t starts{};
        std::int32_t ends{};
        std::int32_t repeats{};
        auto implementor = IntrinsicClassBuilder::Class(
            "Ltest/AnimationListener;", "Ljava/lang/Object;",
            {kListener});
        implementor.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        implementor.VirtualMethod(
            "onAnimationStart", kSignature,
            [&starts](IntrinsicContext&) {
                ++starts;
                return VmValue::Void();
            });
        implementor.VirtualMethod(
            "onAnimationEnd", kSignature,
            [&ends](IntrinsicContext&) {
                ++ends;
                return VmValue::Void();
            });
        implementor.VirtualMethod(
            "onAnimationRepeat", kSignature,
            [&repeats](IntrinsicContext&) {
                ++repeats;
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(implementor).Build()});

        const auto listener = fixture.linker.ResolveDescriptor(kListener);
        const auto& listener_class = fixture.linker.Class(listener);
        CHECK(listener_class.is_boot_dex);
        CHECK(listener_class.is_interface);
        CHECK(listener_class.access_flags ==
              (kAccPublic | kAccInterface | kAccAbstract));
        CHECK_FALSE(fixture.linker.FindClass(kAnimation).has_value());

        std::vector<std::string> declared;
        for (const auto method : listener_class.own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            declared.push_back(linked.name);
            CHECK(linked.descriptor == kSignature);
            CHECK(linked.kind == MethodKind::abstract);
            CHECK(linked.declared_invoke_kind ==
                  DeclaredInvokeKind::interface_call);
            CHECK((linked.access_flags & (kAccPublic | kAccAbstract)) ==
                  (kAccPublic | kAccAbstract));
        }
        CHECK(declared == std::vector<std::string>{
            "onAnimationEnd", "onAnimationRepeat", "onAnimationStart"});

        const auto object = fixture.New("Ltest/AnimationListener;");
        const auto object_class = fixture.model.ObjectClass(object);
        CHECK(fixture.linker.IsAssignable(listener, object_class));
        CHECK_FALSE(fixture.linker.IsAssignable(object_class, listener));

        const auto animation = VmValue::Ref(VmObjectRef{});
        fixture.On(object, "onAnimationStart", kSignature, {animation});
        fixture.On(object, "onAnimationEnd", kSignature, {animation});
        fixture.On(object, "onAnimationRepeat", kSignature, {animation});
        CHECK(starts == 1);
        CHECK(ends == 1);
        CHECK(repeats == 1);
    }
}

TEST_CASE("DVM-175 View.setOnClickListener is overridable and super registers the listener") {
    constexpr auto kSignature = "(Landroid/view/View$OnClickListener;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t overrides{};
        VmObjectRef last_wrapper{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/WrappingView;", "Landroid/view/View;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    view, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.OverrideMethod(
            "setOnClickListener", kSignature,
            [&overrides, &last_wrapper](IntrinsicContext& call) {
                ++overrides;
                const auto incoming = call.arguments[0].ref;
                last_wrapper = incoming.IsValid()
                    ? call.vm.NewIntrinsicInstance("Ltest/ClickWrapper;")
                    : VmObjectRef{};
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                std::optional<VmMethodId> inherited;
                for (const auto method :
                     call.vm.Linker().Class(view).own_virtual_methods) {
                    const auto& linked = call.vm.Linker().Method(method);
                    if (linked.name == "setOnClickListener" &&
                        linked.descriptor == kSignature) {
                        inherited = method;
                        break;
                    }
                }
                const std::array arguments{
                    VmValue::Ref(call.receiver),
                    VmValue::Ref(last_wrapper)};
                const auto outcome = call.vm.Call(*inherited, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto wrapper = IntrinsicClassBuilder::Class("Ltest/ClickWrapper;");
        wrapper.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/ClickListener;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnClickListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        AndroidValueVm fixture(
            backend,
            {std::move(subclass).Build(), std::move(wrapper).Build(),
             std::move(listener).Build()});

        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        std::optional<VmMethodId> view_method;
        for (const auto method :
             fixture.linker.Class(view_type).own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            if (linked.name == "setOnClickListener" &&
                linked.descriptor == kSignature) {
                view_method = method;
                break;
            }
        }
        REQUIRE(view_method.has_value());
        CHECK(fixture.linker.Method(*view_method).overridable);
        CHECK((fixture.linker.Method(*view_method).access_flags & kAccFinal) ==
              0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto object = fixture.New(
            "Ltest/WrappingView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, "setOnClickListener", kSignature);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, "setOnClickListener", kSignature);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(object_class).vtable[*subclass_slot])
                  .owner == object_class);

        const auto callback = fixture.New("Ltest/ClickListener;");
        fixture.On(object, "setOnClickListener", kSignature,
                   {VmValue::Ref(callback)});
        CHECK(overrides == 1);
        const auto node = FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(node.has_value());
        REQUIRE(fixture.context->ui_click_listeners.contains(*node));
        CHECK(fixture.context->ui_click_listeners.at(*node) == last_wrapper);
        CHECK(last_wrapper != callback);

        fixture.On(object, "setOnClickListener", kSignature,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(overrides == 2);
        CHECK_FALSE(last_wrapper.IsValid());
        CHECK_FALSE(fixture.context->ui_click_listeners.contains(*node));
    }
}

TEST_CASE("DVM-133 ContentResolver query returns null when no provider exists") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto resolver = fixture.vm.NewIntrinsicInstance(
            "Landroid/content/ContentResolver;");
        const auto uri_text = fixture.vm.NewStringUtf8(
            "content://com.facebook.katana.provider.AttributionIdProvider");
        const auto uri = fixture.Static(
            "Landroid/net/Uri;", "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;",
            {VmValue::Ref(uri_text)}).ref;
        constexpr auto signature =
            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;"
            "[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;";
        const std::vector<VmValue> absent_query{
            VmValue::Ref(uri), VmValue::Ref(VmObjectRef{}),
            VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
            VmValue::Ref(VmObjectRef{})};
        CHECK_FALSE(fixture.On(
            resolver, "query", signature, absent_query).ref.IsValid());

        auto null_query = absent_query;
        null_query[0] = VmValue::Ref(VmObjectRef{});
        const auto outcome = fixture.OnOutcome(
            resolver, "query", signature, std::move(null_query));
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("DVM-97 dynamic content MIME and malformed filters fail explicitly") {
    AndroidValueVm fixture;
    const auto content_uri = fixture.Static(
        "Landroid/net/Uri;", "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("content://plants/1"))}).ref;
    const auto intent = fixture.New("Landroid/content/Intent;");
    fixture.On(intent, "setData",
               "(Landroid/net/Uri;)Landroid/content/Intent;",
               {VmValue::Ref(content_uri)});
    const auto resolver = fixture.vm.NewIntrinsicInstance(
        "Landroid/content/ContentResolver;");
    auto outcome = fixture.OnOutcome(
        intent, "resolveTypeIfNeeded",
        "(Landroid/content/ContentResolver;)Ljava/lang/String;",
        {VmValue::Ref(resolver)});
    REQUIRE(outcome.exception.IsValid());
    CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");

    const auto explicit_intent = fixture.New("Landroid/content/Intent;");
    fixture.On(explicit_intent, "setData",
               "(Landroid/net/Uri;)Landroid/content/Intent;",
               {VmValue::Ref(content_uri)});
    fixture.On(
        explicit_intent, "setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("org.example")),
         VmValue::Ref(fixture.vm.NewStringUtf8("org.example.Target"))});
    outcome = fixture.OnOutcome(
        explicit_intent, "resolveTypeIfNeeded",
        "(Landroid/content/ContentResolver;)Ljava/lang/String;",
        {VmValue::Ref(VmObjectRef{})});
    CHECK_FALSE(outcome.exception.IsValid());
    CHECK_FALSE(outcome.value.ref.IsValid());

    const auto filter = fixture.New("Landroid/content/IntentFilter;");
    outcome = fixture.OnOutcome(
        filter, "addDataType", "(Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("not-a-mime"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
          "Landroid/content/IntentFilter$MalformedMimeTypeException;");
}

TEST_CASE("DVM-86 Base64 and sparse arrays preserve data semantics") {
    AndroidValueVm fixture;
    const auto input = fixture.Bytes("OGPlay");
    const auto encoded = fixture.Static(
        "Landroid/util/Base64;", "encodeToString", "([BI)Ljava/lang/String;",
        {VmValue::Ref(input), VmValue::Int(2)}).ref;
    CHECK(fixture.vm.StringUtf8(encoded) == "T0dQbGF5");
    const auto decoded = fixture.Static(
        "Landroid/util/Base64;", "decode", "(Ljava/lang/String;I)[B",
        {VmValue::Ref(encoded), VmValue::Int(0)}).ref;
    CHECK(fixture.BytesOf(decoded) == "OGPlay");

    const auto sparse = fixture.New("Landroid/util/SparseArray;");
    const auto first = fixture.vm.NewStringUtf8("first");
    const auto second = fixture.vm.NewStringUtf8("second");
    fixture.On(sparse, "put", "(ILjava/lang/Object;)V",
               {VmValue::Int(7), VmValue::Ref(second)});
    fixture.On(sparse, "put", "(ILjava/lang/Object;)V",
               {VmValue::Int(2), VmValue::Ref(first)});
    CHECK(fixture.On(sparse, "size", "()I").AsInt() == 2);
    CHECK(fixture.On(sparse, "keyAt", "(I)I", {VmValue::Int(0)}).AsInt() == 2);
    CHECK(fixture.On(sparse, "get", "(I)Ljava/lang/Object;",
                     {VmValue::Int(7)}).ref == second);

    const auto ints = fixture.New("Landroid/util/SparseIntArray;");
    fixture.On(ints, "put", "(II)V", {VmValue::Int(9), VmValue::Int(42)});
    CHECK(fixture.On(ints, "get", "(I)I", {VmValue::Int(9)}).AsInt() == 42);
}

TEST_CASE("Log debug throwable overload renders without changing control flow") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto message = fixture.vm.NewStringUtf8("failure detail");
        const auto throwable = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(message)});
        const auto tag = fixture.vm.NewStringUtf8("Kiwi");
        const auto text = fixture.vm.NewStringUtf8("task failed");
        const auto roots = fixture.vm.ProtectReferences(
            std::array{tag, text, throwable});
        CHECK(fixture.Static(
                  "Landroid/util/Log;", "d",
                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
                  {VmValue::Ref(tag), VmValue::Ref(text), VmValue::Ref(throwable)})
                  .AsInt() == 0);
    }
}

TEST_CASE("EventLog write overloads preserve API 19 payload semantics") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto text = fixture.vm.NewStringUtf8("cookie.db\ncorrupt");
        const auto integer = fixture.New("Ljava/lang/Integer;", "(I)V",
                                         {VmValue::Int(42)});
        const auto wide = fixture.New("Ljava/lang/Long;", "(J)V",
                                      {VmValue::Long(INT64_C(9000000000))});
        const auto values = fixture.model.NewObjectArray(
            fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
            fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 4);
        fixture.model.SetObjectElement(values, 0, text);
        fixture.model.SetObjectElement(values, 1, integer);
        fixture.model.SetObjectElement(values, 2, wide);
        fixture.model.SetObjectElement(values, 3, VmObjectRef{});
        const auto roots = fixture.vm.ProtectReferences(
            std::array{text, integer, wide, values});

        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent", "(II)I",
                             {VmValue::Int(75004), VmValue::Int(7)}).AsInt() == 5);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent", "(IJ)I",
                             {VmValue::Int(75004), VmValue::Long(8)}).AsInt() == 9);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent",
                             "(ILjava/lang/String;)I",
                             {VmValue::Int(75004), VmValue::Ref(text)}).AsInt() == 23);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent",
                             "(I[Ljava/lang/Object;)I",
                             {VmValue::Int(75004), VmValue::Ref(values)}).AsInt() == 48);

        const auto records = fixture.logger.Snapshot(
            ogplay::core::LogLevel::info, "runtime.dexvm.guest");
        REQUIRE(records.size() == 4);
        CHECK(records[0].message == "EventLog tag=75004 payload=int(7)");
        CHECK(records[1].message == "EventLog tag=75004 payload=long(8)");
        CHECK(records[2].message ==
              "EventLog tag=75004 payload=string(\"cookie.db\\ncorrupt\")");
        CHECK(records[3].message ==
              "EventLog tag=75004 payload=list[string(\"cookie.db\\ncorrupt\"), int(42), long(9000000000), string(\"NULL\")]");

        const auto bad = fixture.model.NewObjectArray(
            fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
            fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        fixture.model.SetObjectElement(bad, 0,
                                       fixture.New("Ljava/lang/Object;"));
        const auto outcome = fixture.StaticOutcome(
            "Landroid/util/EventLog;", "writeEvent", "(I[Ljava/lang/Object;)I",
            {VmValue::Int(1), VmValue::Ref(bad)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");

        const auto reads = fixture.StaticOutcome(
            "Landroid/util/EventLog;", "readEvents",
            "([ILjava/util/Collection;)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(reads.exception.IsValid());
        CHECK(fixture.linker.Class(reads.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-176 Log.getStackTraceString follows API 19 throwable rules") {
    const auto super_construct = [](IntrinsicContext& call, const char* owner,
                                    const char* signature) {
        const auto type = call.vm.Linker().ResolveDescriptor(owner);
        const auto constructor =
            call.vm.Linker().FindDirectMethod(type, "<init>", signature);
        std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
        arguments.insert(arguments.end(), call.arguments.begin(),
                         call.arguments.end());
        const auto outcome = call.vm.Call(*constructor, arguments);
        if (outcome.exception.IsValid()) {
            throw VmJavaThrow{
                call.vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message, outcome.exception};
        }
        return VmValue::Void();
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t printed{};
        auto custom_host = IntrinsicClassBuilder::Class(
            "Ltest/CustomUnknownHost;", "Ljava/net/UnknownHostException;");
        custom_host.Constructor(
            "(Ljava/lang/String;)V",
            [&super_construct](IntrinsicContext& call) {
                return super_construct(call, "Ljava/net/UnknownHostException;",
                                       "(Ljava/lang/String;)V");
            });
        auto overridden = IntrinsicClassBuilder::Class(
            "Ltest/OverriddenTrace;", "Ljava/lang/RuntimeException;");
        overridden.Constructor("()V", [&super_construct](IntrinsicContext& call) {
            return super_construct(call, "Ljava/lang/RuntimeException;", "()V");
        });
        overridden.OverrideMethod(
            "printStackTrace", "(Ljava/io/PrintWriter;)V",
            [&printed](IntrinsicContext& call) {
                ++printed;
                const auto marker = call.vm.NewStringUtf8("override-marker");
                const auto writer = call.arguments[0].ref;
                const auto roots =
                    call.vm.ProtectReferences(std::array{marker, writer});
                const auto index = call.vm.Linker().FindVtableIndex(
                    call.vm.Model().ObjectClass(writer), "print",
                    "(Ljava/lang/String;)V");
                if (!index) {
                    throw DexVmError(DexVmErrorReason::unresolved_reference,
                                     "PrintWriter.print(String)");
                }
                const std::array arguments{
                    VmValue::Ref(writer), VmValue::Ref(marker)};
                const auto outcome = call.vm.Call(
                    call.vm.Linker().Class(
                        call.vm.Model().ObjectClass(writer)).vtable[*index],
                    arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto throwing = IntrinsicClassBuilder::Class(
            "Ltest/ThrowingTrace;", "Ljava/lang/RuntimeException;");
        throwing.Constructor("()V", [&super_construct](IntrinsicContext& call) {
            return super_construct(call, "Ljava/lang/RuntimeException;", "()V");
        });
        throwing.OverrideMethod(
            "printStackTrace", "(Ljava/io/PrintWriter;)V",
            [](IntrinsicContext&) -> VmValue {
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "print failed"};
            });
        AndroidValueVm fixture(
            backend,
            {std::move(custom_host).Build(), std::move(overridden).Build(),
             std::move(throwing).Build()});
        const auto stack = [&](const VmObjectRef throwable) {
            return fixture.vm.StringUtf8(
                fixture.Static(
                    "Landroid/util/Log;", "getStackTraceString",
                    "(Ljava/lang/Throwable;)Ljava/lang/String;",
                    {VmValue::Ref(throwable)}).ref);
        };

        CHECK(stack(VmObjectRef{}) == "");

        const auto host_message = fixture.vm.NewStringUtf8("an.appads.com");
        const auto unknown_host = fixture.New(
            "Ljava/net/UnknownHostException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(host_message)});
        CHECK(stack(unknown_host) == "");

        const auto download = fixture.vm.NewStringUtf8("download failed");
        const auto nested = fixture.New(
            "Ljava/io/IOException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(download)});
        fixture.On(nested, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(unknown_host)});
        CHECK(stack(nested) == "");

        const auto custom_message = fixture.vm.NewStringUtf8("nested.example");
        const auto custom = fixture.New(
            "Ltest/CustomUnknownHost;", "(Ljava/lang/String;)V",
            {VmValue::Ref(custom_message)});
        CHECK(stack(custom) == "");
        const auto wrapped_custom = fixture.New(
            "Ljava/io/IOException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(download)});
        fixture.On(wrapped_custom, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(custom)});
        CHECK(stack(wrapped_custom) == "");

        const auto attach_frame = [&](const VmObjectRef throwable,
                                      const char* class_name,
                                      const char* method) {
            const auto declaring = fixture.vm.NewStringUtf8(class_name);
            const auto method_name = fixture.vm.NewStringUtf8(method);
            const auto file = fixture.vm.NewStringUtf8("Fixture.java");
            const auto element = fixture.New(
                "Ljava/lang/StackTraceElement;",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V",
                {VmValue::Ref(declaring), VmValue::Ref(method_name),
                 VmValue::Ref(file), VmValue::Int(42)});
            const auto array = fixture.model.NewObjectArray(
                fixture.linker.ResolveDescriptor(
                    "[Ljava/lang/StackTraceElement;"),
                fixture.linker.ResolveDescriptor(
                    "Ljava/lang/StackTraceElement;"),
                1);
            fixture.model.SetObjectElement(array, 0, element);
            const auto roots = fixture.vm.ProtectReferences(
                std::array{throwable, element, array});
            fixture.On(throwable, "setStackTrace",
                       "([Ljava/lang/StackTraceElement;)V",
                       {VmValue::Ref(array)});
        };

        const auto spoofed_message =
            fixture.vm.NewStringUtf8("UnknownHostException an.appads.com");
        const auto spoofed = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(spoofed_message)});
        attach_frame(spoofed, "test.Spoofed", "run");
        const auto spoofed_text = stack(spoofed);
        CHECK(spoofed_text.find("UnknownHostException an.appads.com") !=
              std::string::npos);
        CHECK(spoofed_text.find("java.lang.RuntimeException") !=
              std::string::npos);
        CHECK(spoofed_text.find("\tat test.Spoofed.run(Fixture.java:42)") !=
              std::string::npos);

        const auto cause_message = fixture.vm.NewStringUtf8("inner-detail");
        const auto cause = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(cause_message)});
        attach_frame(cause, "test.Cause", "fail");
        const auto outer_message = fixture.vm.NewStringUtf8("outer-detail");
        const auto outer = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(outer_message)});
        attach_frame(outer, "test.Outer", "act");
        fixture.On(outer, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(cause)});
        const auto outer_text = stack(outer);
        CHECK(outer_text.find("java.lang.RuntimeException: outer-detail") !=
              std::string::npos);
        CHECK(outer_text.find("\tat test.Outer.act(Fixture.java:42)") !=
              std::string::npos);
        CHECK(outer_text.find("Caused by: java.lang.RuntimeException: inner-detail") !=
              std::string::npos);
        CHECK(outer_text.find("\tat test.Cause.fail(Fixture.java:42)") !=
              std::string::npos);

        printed = 0;
        const auto overridden_object = fixture.New("Ltest/OverriddenTrace;");
        CHECK(stack(overridden_object) == "override-marker");
        CHECK(printed == 1);

        const auto throwing_object = fixture.New("Ltest/ThrowingTrace;");
        const auto thrown = fixture.StaticOutcome(
            "Landroid/util/Log;", "getStackTraceString",
            "(Ljava/lang/Throwable;)Ljava/lang/String;",
            {VmValue::Ref(throwing_object)});
        CHECK(thrown.exception.IsValid());
        CHECK(fixture.linker.Class(thrown.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        CHECK(thrown.exception_message == "print failed");
    }
}

TEST_CASE("DVM-177 LinearLayout programmatic AttributeSet constructor") {
    constexpr auto kTwoArg =
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    const auto super_construct = [](IntrinsicContext& call, const char* owner,
                                    const char* signature) {
        const auto type = call.vm.Linker().ResolveDescriptor(owner);
        const auto constructor =
            call.vm.Linker().FindDirectMethod(type, "<init>", signature);
        std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
        arguments.insert(arguments.end(), call.arguments.begin(),
                         call.arguments.end());
        const auto outcome = call.vm.Call(*constructor, arguments);
        if (outcome.exception.IsValid()) {
            throw VmJavaThrow{
                call.vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message, outcome.exception};
        }
        return VmValue::Void();
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ProgrammaticLinear;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto type = call.vm.Model().ObjectClass(call.receiver);
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>",
                    "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0],
                    VmValue::Ref(VmObjectRef{})};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.Constructor(
            kTwoArg, [&super_construct](IntrinsicContext& call) {
                return super_construct(
                    call, "Landroid/widget/LinearLayout;", kTwoArg);
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto context = fixture.New("Landroid/content/Context;");
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto two_arg = fixture.linker.FindDirectMethod(
            fixture.linker.ResolveDescriptor("Landroid/widget/LinearLayout;"),
            "<init>", kTwoArg);
        REQUIRE(two_arg.has_value());

        const auto before = fixture.context->object_to_ui_node.size();
        const auto layout = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        const auto layout_node = FindViewUiNode(*fixture.context, layout.Value());
        REQUIRE(layout_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*layout_node)->kind ==
              ui::UiClass::LinearLayout);
        CHECK(fixture.context->ui_tree.Get(*layout_node)->orientation ==
              ui::Orientation::Horizontal);
        CHECK(fixture.On(layout, "getOrientation", "()I").AsInt() == 0);
        CHECK(fixture.context->object_to_ui_node.size() == before + 1);
        CHECK(fixture.context->ui_node_to_object.at(*layout_node) == layout);

        const auto second = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(second_node.has_value());
        CHECK(*second_node != *layout_node);
        CHECK(second != layout);
        CHECK(fixture.context->object_to_ui_node.size() == before + 2);
        CHECK(fixture.On(layout, "getOrientation", "()I").AsInt() == 0);
        CHECK(fixture.context->object_to_ui_node.size() == before + 2);

        const auto subclass_object = fixture.New(
            "Ltest/ProgrammaticLinear;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        CHECK(fixture.model.ObjectClass(subclass_object) ==
              fixture.linker.ResolveDescriptor("Ltest/ProgrammaticLinear;"));
        const auto subclass_node =
            FindViewUiNode(*fixture.context, subclass_object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->kind ==
              ui::UiClass::LinearLayout);
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->orientation ==
              ui::Orientation::Horizontal);
        CHECK(*subclass_node != *layout_node);

        fixture.On(subclass_object, "setOrientation", "(I)V",
                   {VmValue::Int(1)});
        CHECK(fixture.On(subclass_object, "getOrientation", "()I").AsInt() == 1);
        const auto child = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        fixture.On(subclass_object, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(child)});
        fixture.On(activity, "setContentView", "(Landroid/view/View;)V",
                   {VmValue::Ref(subclass_object)});
        CHECK(fixture.On(child, "getParent", "()Landroid/view/ViewParent;")
                  .ref == subclass_object);

        const auto null_context = fixture.vm.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        const auto null_outcome = fixture.vm.Call(
            *two_arg,
            std::array{VmValue::Ref(null_context),
                       VmValue::Ref(VmObjectRef{}),
                       VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_outcome.exception.IsValid());
        CHECK(fixture.linker.Class(null_outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");

        const auto attrs = fixture.vm.NewStringUtf8("not an AttributeSet");
        const auto attr_target = fixture.vm.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        const auto attr_outcome = fixture.vm.Call(
            *two_arg,
            std::array{VmValue::Ref(attr_target), VmValue::Ref(context),
                       VmValue::Ref(attrs)});
        REQUIRE(attr_outcome.exception.IsValid());
        CHECK(fixture.linker.Class(attr_outcome.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK(attr_outcome.exception_message ==
              "constructing a View from an AttributeSet is unsupported");
        bool recorded = false;
        for (const auto& hit : fixture.ledger.Unimplemented()) {
            if (hit.id == "dexvm.view_xml_attributes") {
                recorded = true;
                CHECK(hit.count >= 1);
            }
        }
        CHECK(recorded);
    }
}

TEST_CASE("DVM-178 ViewGroup clipChildren and clipToPadding are overridable state") {
    constexpr auto kSetClipChildren = "setClipChildren";
    constexpr auto kSetClipToPadding = "setClipToPadding";
    constexpr auto kGetClipChildren = "getClipChildren";
    constexpr auto kBoolVoid = "(Z)V";
    constexpr auto kBool = "()Z";
    const auto find_virtual = [](const DexClassLinker& linker,
                                 const DexClassId type, const char* name,
                                 const char* signature) {
        std::optional<VmMethodId> found;
        for (const auto method : linker.Class(type).own_virtual_methods) {
            const auto& linked = linker.Method(method);
            if (linked.name == name && linked.descriptor == signature) {
                found = method;
                break;
            }
        }
        return found;
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t overrides{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ClipLinear;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/widget/LinearLayout;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.OverrideMethod(
            kSetClipChildren, kBoolVoid,
            [&overrides](IntrinsicContext& call) {
                ++overrides;
                const auto group = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/ViewGroup;");
                std::optional<VmMethodId> inherited;
                for (const auto method :
                     call.vm.Linker().Class(group).own_virtual_methods) {
                    const auto& linked = call.vm.Linker().Method(method);
                    if (linked.name == kSetClipChildren &&
                        linked.descriptor == kBoolVoid) {
                        inherited = method;
                        break;
                    }
                }
                const std::array arguments{VmValue::Ref(call.receiver),
                                           call.arguments[0]};
                const auto outcome = call.vm.Call(*inherited, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto group_type =
            fixture.linker.ResolveDescriptor("Landroid/view/ViewGroup;");
        const auto children_method =
            find_virtual(fixture.linker, group_type, kSetClipChildren, kBoolVoid);
        const auto padding_method =
            find_virtual(fixture.linker, group_type, kSetClipToPadding, kBoolVoid);
        const auto getter =
            find_virtual(fixture.linker, group_type, kGetClipChildren, kBool);
        REQUIRE(children_method.has_value());
        REQUIRE(padding_method.has_value());
        REQUIRE(getter.has_value());
        CHECK(fixture.linker.Method(*children_method).overridable);
        CHECK(fixture.linker.Method(*padding_method).overridable);
        CHECK(fixture.linker.Method(*getter).overridable);
        CHECK((fixture.linker.Method(*children_method).access_flags & kAccFinal) ==
              0);
        CHECK((fixture.linker.Method(*padding_method).access_flags & kAccFinal) ==
              0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto second = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto first_node = FindViewUiNode(*fixture.context, first.Value());
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(first_node.has_value());
        REQUIRE(second_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*first_node)->clip_children);
        CHECK(fixture.context->ui_tree.Get(*first_node)->clip_to_padding);
        CHECK(fixture.On(first, kGetClipChildren, kBool).AsInt() == 1);
        CHECK(fixture.On(second, kGetClipChildren, kBool).AsInt() == 1);

        fixture.context->ui_tree.ClearDrawDirty();
        fixture.context->ui_tree.ClearLayoutDirty();
        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(1)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(1)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->draw_dirty);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->layout_dirty);

        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK(fixture.On(first, kGetClipChildren, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clip_children);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clip_to_padding);
        CHECK(fixture.context->ui_tree.Get(*first_node)->draw_dirty);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->layout_dirty);
        CHECK(fixture.context->ui_tree.Get(*second_node)->clip_children);
        CHECK(fixture.context->ui_tree.Get(*second_node)->clip_to_padding);
        CHECK(fixture.On(second, kGetClipChildren, kBool).AsInt() == 1);

        fixture.context->ui_tree.ClearDrawDirty();
        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->draw_dirty);

        const auto object = fixture.New(
            "Ltest/ClipLinear;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto group_slot = fixture.linker.FindVtableIndex(
            group_type, kSetClipChildren, kBoolVoid);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, kSetClipChildren, kBoolVoid);
        REQUIRE(group_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*group_slot == *subclass_slot);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(object_class).vtable[*subclass_slot])
                  .owner == object_class);
        fixture.On(object, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        CHECK(overrides == 1);
        const auto subclass_node =
            FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK_FALSE(fixture.context->ui_tree.Get(*subclass_node)->clip_children);
        CHECK(fixture.On(object, kGetClipChildren, kBool).AsInt() == 0);
        fixture.On(object, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*subclass_node)->clip_to_padding);
    }
}

TEST_CASE("DVM-179 View clickable state is overridable and shared with UiNode") {
    constexpr auto kSetClickable = "setClickable";
    constexpr auto kIsClickable = "isClickable";
    constexpr auto kSetListener = "setOnClickListener";
    constexpr auto kBoolVoid = "(Z)V";
    constexpr auto kBool = "()Z";
    constexpr auto kListener = "(Landroid/view/View$OnClickListener;)V";
    const auto find_virtual = [](const DexClassLinker& linker,
                                 const DexClassId type, const char* name,
                                 const char* signature) {
        std::optional<VmMethodId> found;
        for (const auto method : linker.Class(type).own_virtual_methods) {
            const auto& linked = linker.Method(method);
            if (linked.name == name && linked.descriptor == signature) {
                found = method;
                break;
            }
        }
        return found;
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t setters{};
        std::int32_t getters{};
        std::int32_t clicks{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ClickableView;", "Landroid/view/View;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    view, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        const auto call_super = [](IntrinsicContext& call, const char* name,
                                   const char* signature) {
            const auto view = call.vm.Linker().ResolveDescriptor(
                "Landroid/view/View;");
            std::optional<VmMethodId> inherited;
            for (const auto method :
                 call.vm.Linker().Class(view).own_virtual_methods) {
                const auto& linked = call.vm.Linker().Method(method);
                if (linked.name == name && linked.descriptor == signature) {
                    inherited = method;
                    break;
                }
            }
            std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
            arguments.insert(arguments.end(), call.arguments.begin(),
                             call.arguments.end());
            const auto outcome = call.vm.Call(*inherited, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    call.vm.Linker().Class(outcome.exception_class).descriptor,
                    outcome.exception_message, outcome.exception};
            }
            return outcome.value;
        };
        subclass.OverrideMethod(
            kSetClickable, kBoolVoid,
            [&setters, &call_super](IntrinsicContext& call) {
                ++setters;
                return call_super(call, kSetClickable, kBoolVoid);
            });
        subclass.OverrideMethod(
            kIsClickable, kBool,
            [&getters, &call_super](IntrinsicContext& call) {
                ++getters;
                return call_super(call, kIsClickable, kBool);
            });
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/CountClick;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnClickListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        listener.VirtualMethod(
            "onClick", "(Landroid/view/View;)V",
            [&clicks](IntrinsicContext&) {
                ++clicks;
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(subclass).Build(), std::move(listener).Build()});
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        REQUIRE(find_virtual(fixture.linker, view_type, kSetClickable, kBoolVoid)
                    .has_value());
        REQUIRE(find_virtual(fixture.linker, view_type, kIsClickable, kBool)
                    .has_value());
        CHECK(fixture.linker.Method(
                          *find_virtual(fixture.linker, view_type, kSetClickable,
                                        kBoolVoid))
                  .overridable);
        CHECK((fixture.linker.Method(
                           *find_virtual(fixture.linker, view_type,
                                         kSetClickable, kBoolVoid))
                   .access_flags &
               kAccFinal) == 0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto second = fixture.New(
            "Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto first_node = FindViewUiNode(*fixture.context, first.Value());
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(first_node.has_value());
        REQUIRE(second_node.has_value());
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clickable);
        CHECK(fixture.On(second, kIsClickable, kBool).AsInt() == 0);

        fixture.On(first, kSetClickable, kBoolVoid, {VmValue::Int(1)});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 1);
        CHECK(fixture.context->ui_tree.Get(*first_node)->clickable);
        CHECK(fixture.On(second, kIsClickable, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*second_node)->clickable);

        const auto callback = fixture.New("Ltest/CountClick;");
        fixture.On(first, kSetListener, kListener, {VmValue::Ref(callback)});
        CHECK(fixture.context->ui_tree.Get(*first_node)->clickable);
        REQUIRE(fixture.context->ui_click_listeners.contains(*first_node));
        fixture.On(first, kSetClickable, kBoolVoid, {VmValue::Int(0)});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 0);
        REQUIRE(fixture.context->ui_click_listeners.contains(*first_node));
        CHECK(fixture.context->ui_click_listeners.at(*first_node) == callback);
        CHECK_FALSE(InvokeViewOnClick(fixture.vm, *fixture.context, first.Value())
                        .has_value());
        CHECK(clicks == 1);

        fixture.On(first, kSetListener, kListener,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 1);
        CHECK_FALSE(fixture.context->ui_click_listeners.contains(*first_node));

        const auto object = fixture.New(
            "Ltest/ClickableView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, kSetClickable, kBoolVoid);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, kSetClickable, kBoolVoid);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        fixture.On(object, kSetListener, kListener,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(getters >= 1);
        CHECK(setters >= 1);
        CHECK(fixture.On(object, kIsClickable, kBool).AsInt() == 1);
        const auto subclass_node =
            FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->clickable);
    }
}

TEST_CASE("Activity top-level identity reports that it is not a child") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto activity = fixture.New("Landroid/app/Activity;");
        CHECK(fixture.On(activity, "isChild", "()Z").AsInt() == 0);
    }
}

TEST_CASE("DVM-86 graphics value classes keep geometry and path state") {
    AndroidValueVm fixture;
    CHECK(fixture.Static("Landroid/graphics/Color;", "parseColor",
                         "(Ljava/lang/String;)I",
                         {VmValue::Ref(fixture.vm.NewStringUtf8("#112233"))})
              .AsInt() == static_cast<std::int32_t>(0xff112233U));
    const auto rect = fixture.New("Landroid/graphics/RectF;", "(FFFF)V",
        {VmValue::Float(1.0F), VmValue::Float(2.0F),
         VmValue::Float(6.0F), VmValue::Float(9.0F)});
    CHECK(fixture.On(rect, "width", "()F").AsFloat() == doctest::Approx(5.0F));
    CHECK(fixture.On(rect, "contains", "(FF)Z",
                     {VmValue::Float(3.0F), VmValue::Float(4.0F)}).AsInt() == 1);

    const auto path = fixture.New("Landroid/graphics/Path;");
    CHECK(fixture.On(path, "isEmpty", "()Z").AsInt() == 1);
    fixture.On(path, "moveTo", "(FF)V", {VmValue::Float(1), VmValue::Float(2)});
    fixture.On(path, "lineTo", "(FF)V", {VmValue::Float(3), VmValue::Float(4)});
    CHECK(fixture.context->paths.at(path.Value()).commands.size() == 2U);
    CHECK(fixture.On(path, "isEmpty", "()Z").AsInt() == 0);
}

TEST_CASE("DVM-98 Android platform enums share generated enum behavior") {
    AndroidValueVm fixture;
    const std::vector<std::pair<std::string, std::vector<std::string>>> specs{
        {"Landroid/graphics/Bitmap$Config;",
         {"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"}},
        {"Landroid/graphics/Region$Op;", {"REPLACE"}},
        {"Landroid/graphics/Path$Direction;", {"CW", "CCW"}},
        {"Landroid/graphics/PorterDuff$Mode;",
         {"CLEAR", "SRC", "DST", "SRC_OVER", "DST_OVER", "SRC_IN",
          "DST_IN", "SRC_OUT", "DST_OUT", "SRC_ATOP", "DST_ATOP",
          "XOR", "DARKEN", "LIGHTEN", "MULTIPLY", "SCREEN", "ADD",
          "OVERLAY"}},
        {"Landroid/net/NetworkInfo$State;", {"CONNECTED"}},
        {"Landroid/os/AsyncTask$Status;",
         {"PENDING", "RUNNING", "FINISHED"}},
        {"Landroid/widget/ImageView$ScaleType;",
         {"CENTER", "CENTER_INSIDE", "FIT_CENTER", "FIT_XY",
          "CENTER_CROP"}}};
    const auto enum_class = fixture.linker.ResolveDescriptor("Ljava/lang/Enum;");

    for (const auto& [descriptor, constants] : specs) {
        CAPTURE(descriptor);
        const auto klass = fixture.linker.ResolveDescriptor(descriptor);
        const auto& linked_class = fixture.linker.Class(klass);
        REQUIRE(linked_class.super.has_value());
        CHECK(*linked_class.super == enum_class);
        CHECK((linked_class.access_flags & (kAccFinal | kAccEnum)) ==
              (kAccFinal | kAccEnum));

        const auto array_descriptor = "[" + descriptor;
        const auto values_descriptor = "()" + array_descriptor;
        const auto first = fixture.Static(descriptor.c_str(), "values",
                                          values_descriptor.c_str()).ref;
        const auto second = fixture.Static(descriptor.c_str(), "values",
                                           values_descriptor.c_str()).ref;
        CHECK(first != second);
        REQUIRE(fixture.model.ArrayLength(first) ==
                static_cast<JniSize>(constants.size()));

        for (std::size_t ordinal = 0; ordinal < constants.size(); ++ordinal) {
            const auto& name = constants[ordinal];
            const auto field = fixture.linker.FindFieldRecursive(
                klass, name, descriptor);
            REQUIRE(field.has_value());
            CHECK(fixture.linker.Field(*field).access_flags ==
                  (kAccPublic | kAccStatic | kAccFinal | kAccEnum));
            const auto value = VmObjectRef(
                fixture.linker.Class(klass).static_storage[
                    fixture.linker.Field(*field).slot]);
            CHECK(fixture.model.GetObjectElement(
                      first, static_cast<JniSize>(ordinal)) == value);
            CHECK(fixture.vm.StringUtf8(
                      fixture.On(value, "name", "()Ljava/lang/String;").ref) ==
                  name);
            CHECK(fixture.On(value, "ordinal", "()I").AsInt() ==
                  static_cast<std::int32_t>(ordinal));

            const auto value_of_descriptor =
                "(Ljava/lang/String;)" + descriptor;
            CHECK(fixture.Static(
                      descriptor.c_str(), "valueOf",
                      value_of_descriptor.c_str(),
                      {VmValue::Ref(fixture.vm.NewStringUtf8(name))})
                      .ref == value);
        }

        const auto values_field = fixture.linker.FindFieldRecursive(
            klass, "$VALUES", array_descriptor);
        REQUIRE(values_field.has_value());
        CHECK(fixture.linker.Field(*values_field).access_flags ==
              (kAccPrivate | kAccStatic | kAccFinal | kAccSynthetic));
    }
}

TEST_CASE("Bitmap Config matches the API 19 enum and native mapping") {
    AndroidValueVm fixture;
    constexpr auto descriptor = "Landroid/graphics/Bitmap$Config;";
    const auto config_class = fixture.linker.ResolveDescriptor(descriptor);
    const auto enum_class = fixture.linker.ResolveDescriptor("Ljava/lang/Enum;");
    REQUIRE(fixture.linker.Class(config_class).super.has_value());
    CHECK(*fixture.linker.Class(config_class).super == enum_class);

    const auto initialized = fixture.vm.EnsureClassInitialized(config_class);
    REQUIRE_MESSAGE(!initialized.exception.IsValid(),
                    initialized.exception_message);
    const auto constant = [&](const char* name) {
        const auto field = fixture.linker.FindFieldRecursive(
            config_class, name, descriptor);
        REQUIRE(field.has_value());
        const auto& linked = fixture.linker.Field(*field);
        CHECK((linked.access_flags & 0x4019U) == 0x4019U);
        return VmObjectRef(
            fixture.linker.Class(linked.owner).static_storage[linked.slot]);
    };
    const std::array constants{constant("ALPHA_8"), constant("RGB_565"),
                               constant("ARGB_4444"),
                               constant("ARGB_8888")};
    for (const auto value : constants) CHECK(value.IsValid());

    const auto native_int = fixture.linker.FindFieldRecursive(
        config_class, "nativeInt", "I");
    const auto name_field = fixture.linker.FindFieldRecursive(
        config_class, "name", "Ljava/lang/String;");
    const auto ordinal = fixture.linker.FindFieldRecursive(
        config_class, "ordinal", "I");
    REQUIRE(native_int.has_value());
    REQUIRE(name_field.has_value());
    REQUIRE(ordinal.has_value());
    const std::array native_values{1U, 3U, 4U, 5U};
    const std::array names{"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"};
    for (std::size_t index = 0; index < constants.size(); ++index) {
        const auto slots = fixture.model.InstanceSlots(constants[index]);
        CHECK(slots[fixture.linker.Field(*native_int).slot].bits ==
              native_values[index]);
        CHECK(slots[fixture.linker.Field(*ordinal).slot].bits == index);
        CHECK(fixture.vm.StringUtf8(VmObjectRef(
                  slots[fixture.linker.Field(*name_field).slot].bits)) ==
              names[index]);
    }

    const std::array<VmObjectRef, 6> native_mapping{
        VmObjectRef{}, constants[0], VmObjectRef{}, constants[1],
        constants[2], constants[3]};
    for (std::int32_t index = 0; index < 6; ++index) {
        CHECK(fixture.Static(descriptor, "nativeToConfig",
                             "(I)Landroid/graphics/Bitmap$Config;",
                             {VmValue::Int(index)})
                  .ref == native_mapping[static_cast<std::size_t>(index)]);
    }
    const auto by_name = fixture.Static(
        descriptor, "valueOf",
        "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("ARGB_8888"))});
    CHECK(by_name.ref == constants[3]);

    const auto first_values = fixture.Static(
        descriptor, "values", "()[Landroid/graphics/Bitmap$Config;").ref;
    const auto second_values = fixture.Static(
        descriptor, "values", "()[Landroid/graphics/Bitmap$Config;").ref;
    CHECK(first_values != second_values);
    CHECK(fixture.model.ArrayLength(first_values) == 4);
    for (JniSize index = 0; index < 4; ++index) {
        CHECK(fixture.model.GetObjectElement(first_values, index) ==
              constants[static_cast<std::size_t>(index)]);
    }
    CHECK(fixture.linker.FindFieldRecursive(
              config_class, "$VALUES",
              "[Landroid/graphics/Bitmap$Config;").has_value());
    CHECK(fixture.linker.FindFieldRecursive(
              config_class, "sConfigs",
              "[Landroid/graphics/Bitmap$Config;").has_value());
    CHECK(fixture.linker.FindDirectMethod(
              config_class, "<init>", "(Ljava/lang/String;II)V").has_value());

    const auto bitmap = fixture.Static(
        "Landroid/graphics/Bitmap;", "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        {VmValue::Int(2), VmValue::Int(2), VmValue::Ref(constants[1])}).ref;
    REQUIRE(bitmap.IsValid());
    REQUIRE(fixture.context->bitmaps.contains(bitmap.Value()));
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).config == 3);
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).pixels->Snapshot() ==
          std::vector<std::uint32_t>(4, 0U));
    const auto pixels = fixture.model.NewPrimitiveArray(
        fixture.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
    const std::array<std::uint32_t, 4> colors{
        0xff112233U, 0xff445566U, 0xff778899U, 0xffaabbccU};
    for (JniSize index = 0; index < 4; ++index) {
        fixture.model.SetPrimitiveElement(pixels, index, colors[index]);
    }
    fixture.On(bitmap, "setPixels", "([IIIIIII)V",
               {VmValue::Ref(pixels), VmValue::Int(0), VmValue::Int(2),
                VmValue::Int(0), VmValue::Int(0), VmValue::Int(2),
                VmValue::Int(2)});
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).pixels->Snapshot() ==
          std::vector<std::uint32_t>(colors.begin(), colors.end()));

    const auto canvas = fixture.vm.NewIntrinsicInstance(
        "Landroid/graphics/Canvas;");
    fixture.context->canvases.emplace(
        canvas.Value(),
        DexVmAndroidContext::CanvasState{
            1U, 3U, 2U, std::vector<std::uint32_t>(6, 0xff000000U), true});
    fixture.On(canvas, "drawBitmap",
               "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V",
               {VmValue::Ref(bitmap), VmValue::Float(1.0F),
                VmValue::Float(0.0F), VmValue::Ref(VmObjectRef{0})});
    CHECK(fixture.context->canvases.at(canvas.Value()).argb ==
          std::vector<std::uint32_t>{
              0xff000000U, colors[0], colors[1],
              0xff000000U, colors[2], colors[3]});
}

TEST_CASE("DVM-86 Parcel Bundle and bounded services share session state") {
    AndroidValueVm fixture;
    const auto parcel = fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
    fixture.On(parcel, "writeInt", "(I)V", {VmValue::Int(37)});
    fixture.On(parcel, "writeString", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("value"))});
    CHECK(fixture.On(parcel, "dataSize", "()I").AsInt() > 4);
    fixture.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
    CHECK(fixture.On(parcel, "readInt", "()I").AsInt() == 37);
    CHECK(fixture.vm.StringUtf8(
              fixture.On(parcel, "readString", "()Ljava/lang/String;").ref) ==
          "value");

    const auto bundle = fixture.New("Landroid/os/Bundle;");
    fixture.On(bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("key")),
         VmValue::Ref(fixture.vm.NewStringUtf8("stored"))});
    const auto parcel2 = fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
    fixture.On(parcel2, "writeBundle", "(Landroid/os/Bundle;)V", {VmValue::Ref(bundle)});
    fixture.On(bundle, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("key")),
         VmValue::Ref(fixture.vm.NewStringUtf8("mutated"))});
    fixture.On(parcel2, "setDataPosition", "(I)V", {VmValue::Int(0)});
    const auto copy = fixture.On(parcel2, "readBundle", "()Landroid/os/Bundle;").ref;
    CHECK(copy != bundle);
    CHECK(fixture.vm.StringUtf8(fixture.On(
              copy, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
              {VmValue::Ref(fixture.vm.NewStringUtf8("key"))}).ref) == "stored");

    const auto power = fixture.New("Landroid/os/PowerManager;");
    const auto lock = fixture.On(power, "newWakeLock",
        "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
        {VmValue::Int(1), VmValue::Ref(fixture.vm.NewStringUtf8("test"))}).ref;
    fixture.On(lock, "acquire", "()V");
    CHECK(fixture.On(lock, "isHeld", "()Z").AsInt() == 1);
    fixture.On(lock, "release", "()V");
    CHECK(fixture.On(lock, "isHeld", "()Z").AsInt() == 0);

    const auto vibrator = fixture.New("Landroid/os/Vibrator;");
    fixture.On(vibrator, "vibrate", "(J)V", {VmValue::Long(250)});
    CHECK(fixture.context->last_vibration_millis == 250);
    fixture.On(vibrator, "cancel", "()V");
    CHECK(fixture.context->last_vibration_millis == 0);

    const auto android_context = fixture.New("Landroid/content/Context;");
    const auto service_name = fixture.vm.NewStringUtf8("power");
    const auto service1 = fixture.On(android_context, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)}).ref;
    const auto service2 = fixture.On(android_context, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service_name)}).ref;
    CHECK(service1.IsValid());
    CHECK(service1 == service2);
}

TEST_CASE("DVM-86 value side tables sweep with their guest owners") {
    AndroidValueVm fixture;
    static_cast<void>(fixture.New("Landroid/util/SparseArray;"));
    static_cast<void>(fixture.New("Landroid/graphics/Path;"));
    static_cast<void>(fixture.Static(
        "Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;"));
    const auto power = fixture.New("Landroid/os/PowerManager;");
    static_cast<void>(fixture.On(power, "newWakeLock",
        "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
        {VmValue::Int(1), VmValue::Ref(fixture.vm.NewStringUtf8("sweep"))}));
    REQUIRE_FALSE(fixture.context->paths.empty());
    REQUIRE_FALSE(fixture.context->parcel_backings.empty());
    REQUIRE_FALSE(fixture.context->wake_locks.empty());
    const auto result = fixture.vm.CollectGarbage("dvm86-value-state");
    CHECK(result.freed_objects >= 4U);
    CHECK(fixture.context->paths.empty());
    // Parcel.obtain() may retain the recycled owner in the API 19 Java pool;
    // its backing is released by recycle/nativeFreeBuffer and is safe to reuse.
    CHECK(fixture.context->parcel_backings.size() <= 1U);
    CHECK(fixture.context->wake_locks.empty());
}

TEST_CASE("DVM-86 rooted Bundle traces byte arrays and Parcelable identities") {
    AndroidValueVm fixture;
    const auto bundle = fixture.New("Landroid/os/Bundle;");
    const auto bytes = fixture.Bytes("kept");
    const auto key = fixture.vm.NewStringUtf8("payload");
    fixture.On(bundle, "putByteArray", "(Ljava/lang/String;[B)V",
               {VmValue::Ref(key), VmValue::Ref(bytes)});
    fixture.vm.SetGcIntegration(
        {{}, {}, [bundle](const VmRootVisitor& visit) { visit(bundle); }});

    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(bundle));
    CHECK(marked.IsMarked(bytes));
    static_cast<void>(fixture.vm.CollectGarbage("dvm86-bundle-edge"));
    CHECK(fixture.On(bundle, "getByteArray", "(Ljava/lang/String;)[B",
                     {VmValue::Ref(fixture.vm.NewStringUtf8("payload"))}).ref ==
          bytes);
    CHECK(fixture.BytesOf(bytes) == "kept");
}

TEST_CASE(
    "DVM-107 framework Pair and sparse containers execute Java on both backends") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto a = f.vm.NewStringUtf8("equal");
        const auto b = f.vm.NewStringUtf8("equal");
        const auto pair =
            f.Static("Landroid/util/Pair;", "create",
                     "(Ljava/lang/Object;Ljava/lang/Object;)Landroid/util/Pair;",
                     {VmValue::Ref(a), VmValue::Ref(VmObjectRef{})})
                .ref;
        const auto other =
            f.New("Landroid/util/Pair;", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                  {VmValue::Ref(b), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(pair, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(other)})
                  .AsInt() == 1);
        CHECK(f.On(pair, "hashCode", "()I").AsInt() ==
              f.On(other, "hashCode", "()I").AsInt());
        CHECK(
            f.On(pair, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(VmObjectRef{})})
                .AsInt() == 0);
        for (const auto* descriptor :
             {"Landroid/util/SparseArray;", "Landroid/util/LongSparseArray;"}) {
            CAPTURE(descriptor);
            const bool wide =
                std::string(descriptor).find("LongSparse") != std::string::npos;
            const auto sparse = f.New(descriptor, "(I)V", {VmValue::Int(0)});
            const auto roots = f.vm.ProtectReferences(std::array{sparse, a, b, pair});
            const auto key = [&](std::int64_t value) {
                return wide ? VmValue::Long(value)
                            : VmValue::Int(static_cast<std::int32_t>(value));
            };
            const auto put_sig =
                wide ? "(JLjava/lang/Object;)V" : "(ILjava/lang/Object;)V";
            const auto get_sig =
                wide ? "(J)Ljava/lang/Object;" : "(I)Ljava/lang/Object;";
            const auto delete_sig = wide ? "(J)V" : "(I)V";
            const auto max_key = wide ? INT64_C(0x100000001) : INT64_C(0x7fffffff);
            f.On(sparse, "append", put_sig, {key(max_key), VmValue::Ref(pair)});
            f.On(sparse, "append", put_sig, {key(-7), VmValue::Ref(a)});
            f.On(sparse, "put", put_sig, {key(0), VmValue::Ref(b)});
            f.On(sparse, "delete", delete_sig, {key(0)});
            CHECK(f.On(sparse, "size", "()I").AsInt() == 2);
            const auto first_key =
                f.On(sparse, "keyAt", wide ? "(I)J" : "(I)I", {VmValue::Int(0)});
            CHECK((wide ? first_key.AsLong() : first_key.AsInt()) == -7);
            CHECK(
                f.On(sparse, "indexOfValue", "(Ljava/lang/Object;)I", {VmValue::Ref(b)})
                    .AsInt() == -1);
            CHECK(f.On(sparse, "get",
                       wide ? "(JLjava/lang/Object;)Ljava/lang/Object;"
                            : "(ILjava/lang/Object;)Ljava/lang/Object;",
                       {key(0), VmValue::Ref(pair)})
                      .ref == pair);
            const auto clone_sig = std::string("()") + descriptor;
            const auto clone = f.On(sparse, "clone", clone_sig.c_str()).ref;
            const auto clone_root = f.vm.ProtectReferences(std::array{clone});
            f.On(sparse, "clear", "()V");
            CHECK(f.On(clone, "get", get_sig, {key(max_key)}).ref == pair);
            CHECK(f.On(clone, "size", "()I").AsInt() == 2);
            f.On(clone, "removeAt", "(I)V", {VmValue::Int(0)});
            CHECK(f.On(clone, "size", "()I").AsInt() == 1);
        }
        for (const auto* descriptor :
             {"Landroid/util/SparseIntArray;", "Landroid/util/SparseBooleanArray;",
              "Landroid/util/SparseLongArray;"}) {
            CAPTURE(descriptor);
            const std::string name = descriptor;
            const bool wide = name.find("Long") != std::string::npos;
            const bool boolean = name.find("Boolean") != std::string::npos;
            const auto sparse = f.New(descriptor, "(I)V", {VmValue::Int(0)});
            const auto value = wide ? VmValue::Long(INT64_C(0x123456789))
                                    : VmValue::Int(boolean ? 1 : -42);
            const auto put_sig = wide ? "(IJ)V" : boolean ? "(IZ)V" : "(II)V";
            f.On(sparse, "append", put_sig, {VmValue::Int(9), value});
            f.On(sparse, "append", put_sig, {VmValue::Int(-9), value});
            f.On(sparse, "delete", "(I)V", {VmValue::Int(9)});
            CHECK(f.On(sparse, "size", "()I").AsInt() == 1);
            CHECK(f.On(sparse, "keyAt", "(I)I", {VmValue::Int(0)}).AsInt() == -9);
            const auto result = f.On(sparse, "get",
                                     wide      ? "(I)J"
                                     : boolean ? "(I)Z"
                                               : "(I)I",
                                     {VmValue::Int(-9)});
            CHECK((wide ? result.AsLong() : result.AsInt()) ==
                  (wide      ? INT64_C(0x123456789)
                   : boolean ? 1
                             : -42));
            const auto clone_sig = std::string("()") + descriptor;
            const auto clone = f.On(sparse, "clone", clone_sig.c_str()).ref;
            f.On(sparse, "clear", "()V");
            CHECK(f.On(clone, "size", "()I").AsInt() == 1);
        }
    }
}

TEST_CASE(
    "DVM-107 sparse and Pair fields keep children alive and release deleted values") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto sparse = f.New("Landroid/util/SparseArray;");
        const auto roots = f.vm.ProtectReferences(std::array{sparse});
        const auto child = f.New("Ljava/lang/Object;");
        const auto pair =
            f.New("Landroid/util/Pair;", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                  {VmValue::Ref(child), VmValue::Ref(VmObjectRef{})});
        f.On(sparse, "put", "(ILjava/lang/Object;)V",
             {VmValue::Int(3), VmValue::Ref(pair)});
        CHECK(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm107-sparse-strong-edge"));
        CHECK(f.On(sparse, "get", "(I)Ljava/lang/Object;", {VmValue::Int(3)}).ref ==
              pair);
        f.On(sparse, "delete", "(I)V", {VmValue::Int(3)});
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(pair));
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm107-sparse-deleted"));
        CHECK(f.On(sparse, "size", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-107 ComponentName Java value and Parcel roundtrip") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto component =
            f.New("Landroid/content/ComponentName;",
                  "(Ljava/lang/String;Ljava/lang/String;)V",
                  {VmValue::Ref(f.vm.NewStringUtf8("fixture")),
                   VmValue::Ref(f.vm.NewStringUtf8("fixture.sub.Main$Nested"))});
        CHECK(f.vm.StringUtf8(
                  f.On(component, "getShortClassName", "()Ljava/lang/String;").ref) ==
              ".sub.Main$Nested");
        CHECK(
            f.vm.StringUtf8(
                f.On(component, "flattenToShortString", "()Ljava/lang/String;").ref) ==
            "fixture/.sub.Main$Nested");
        const auto flat =
            f.On(component, "flattenToString", "()Ljava/lang/String;").ref;
        const auto parsed =
            f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                     "(Ljava/lang/String;)Landroid/content/ComponentName;",
                     {VmValue::Ref(flat)})
                .ref;
        CHECK(f.On(component, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(parsed)})
                  .AsInt() == 1);
        CHECK(f.On(component, "compareTo", "(Landroid/content/ComponentName;)I",
                   {VmValue::Ref(parsed)})
                  .AsInt() == 0);
        CHECK(f.On(component, "hashCode", "()I").AsInt() ==
              f.On(parsed, "hashCode", "()I").AsInt());
        CHECK_FALSE(f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                             "(Ljava/lang/String;)Landroid/content/ComponentName;",
                             {VmValue::Ref(f.vm.NewStringUtf8("invalid"))})
                        .ref.IsValid());
        const auto shortened =
            f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                     "(Ljava/lang/String;)Landroid/content/ComponentName;",
                     {VmValue::Ref(f.vm.NewStringUtf8("fixture/.Main"))})
                .ref;
        CHECK(f.vm.StringUtf8(
                  f.On(shortened, "getClassName", "()Ljava/lang/String;").ref) ==
              "fixture.Main");
        const auto parcel =
            f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        f.On(component, "writeToParcel", "(Landroid/os/Parcel;I)V",
             {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto restored = f.New("Landroid/content/ComponentName;",
                                    "(Landroid/os/Parcel;)V", {VmValue::Ref(parcel)});
        CHECK(
            f.On(component, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(restored)})
                .AsInt() == 1);
        CHECK(f.On(component, "describeContents", "()I").AsInt() == 0);
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto type = f.model.ObjectClass(component);
        const auto creator_field = f.linker.FindFieldRecursive(
            type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef(
            f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]);
        const auto via_creator =
            f.On(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;",
                 {VmValue::Ref(parcel)})
                .ref;
        CHECK(f.On(component, "equals", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(via_creator)})
                  .AsInt() == 1);
        const auto array =
            f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(2)}).ref;
        CHECK(f.model.ArrayLength(array) == 2);
        CHECK(f.linker.Class(f.model.ObjectClass(array)).descriptor ==
              "[Landroid/content/ComponentName;");
        f.On(parcel, "recycle", "()V");
        const auto output = f.New("Ljava/io/StringWriter;");
        const auto printer = f.New("Ljava/io/PrintWriter;", "(Ljava/io/Writer;)V",
                                   {VmValue::Ref(output)});
        f.Static("Landroid/content/ComponentName;", "printShortString",
                 "(Ljava/io/PrintWriter;Ljava/lang/String;Ljava/lang/String;)V",
                 {VmValue::Ref(printer), VmValue::Ref(f.vm.NewStringUtf8("fixture")),
                  VmValue::Ref(f.vm.NewStringUtf8("fixture.Main"))});
        CHECK(f.vm.StringUtf8(f.On(output, "toString", "()Ljava/lang/String;").ref) ==
              "fixture/.Main");
        CHECK(f.On(printer, "checkError", "()Z").AsInt() == 0);
    }
}

TEST_CASE(
    "DVM-107 Activity local name and Intent identity follow the launch component") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "fixture";
        const auto base = f.New("Landroid/content/Context;");
        const auto attach = [&](const char* name) {
            const auto activity = f.New("Landroid/app/Activity;");
            f.On(activity, "attachBaseContext", "(Landroid/content/Context;)V",
                 {VmValue::Ref(base)});
            f.context->current_intent = VmObjectRef{};
            AttachAndroidActivityIdentity(f.vm, f.context, activity, name);
            return activity;
        };
        const auto activity = attach("fixture.sub.LaunchAlias$Nested");
        const auto roots = f.vm.ProtectReferences(std::array{activity, base});
        const auto component =
            f.On(activity, "getComponentName", "()Landroid/content/ComponentName;").ref;
        const auto intent =
            f.On(activity, "getIntent", "()Landroid/content/Intent;").ref;
        CHECK(f.On(intent, "getComponent", "()Landroid/content/ComponentName;").ref ==
              component);
        CHECK(f.vm.StringUtf8(
                  f.On(activity, "getLocalClassName", "()Ljava/lang/String;").ref) ==
              "sub.LaunchAlias$Nested");
        const auto second = attach("fixture.Second");
        CHECK(f.On(activity, "getIntent", "()Landroid/content/Intent;").ref == intent);
        CHECK(f.On(second, "getIntent", "()Landroid/content/Intent;").ref != intent);
        f.On(intent, "setComponent",
             "(Landroid/content/ComponentName;)Landroid/content/Intent;",
             {VmValue::Ref(VmObjectRef{})});
        f.On(activity, "setIntent", "(Landroid/content/Intent;)V",
             {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(
            f.On(activity, "getIntent", "()Landroid/content/Intent;").ref.IsValid());
        static_cast<void>(f.vm.CollectGarbage("dvm107-activity-component"));
        CHECK(f.On(activity, "getComponentName", "()Landroid/content/ComponentName;")
                  .ref == component);
        CHECK(f.vm.StringUtf8(
                  f.On(activity, "getLocalClassName", "()Ljava/lang/String;").ref) ==
              "sub.LaunchAlias$Nested");
        for (const auto* name :
             {"fixture2.Main", "other.Main", "fixture", "fixture.Main"}) {
            const auto candidate = attach(name);
            CHECK(
                f.vm.StringUtf8(
                    f.On(candidate, "getLocalClassName", "()Ljava/lang/String;").ref) ==
                (std::string(name) == "fixture.Main" ? "Main" : name));
        }
        const auto unattached = f.New("Landroid/app/Activity;");
        CHECK(f.OnOutcome(unattached, "getLocalClassName", "()Ljava/lang/String;")
                  .exception.IsValid());
        const auto prefs =
            f.On(activity, "getPreferences", "(I)Landroid/content/SharedPreferences;",
                 {VmValue::Int(0)})
                .ref;
        CHECK(prefs == f.On(activity, "getSharedPreferences",
                            "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                            {VmValue::Ref(f.vm.NewStringUtf8("sub.LaunchAlias$Nested")),
                             VmValue::Int(0)})
                           .ref);
    }
}

TEST_CASE("DVM-107 builder CharSequence range append uses UTF16 and validates before "
          "mutation") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor :
             {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto builder = f.New(descriptor);
            const auto signature =
                std::string("(Ljava/lang/CharSequence;II)") + descriptor;
            const auto text = f.model.NewString(u"a😀中z");
            CHECK(f.On(builder, "append", signature.c_str(),
                       {VmValue::Ref(text), VmValue::Int(1), VmValue::Int(4)})
                      .ref == builder);
            CHECK(f.model.StringValue(
                      f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                  u"😀中");
            f.On(builder, "append", signature.c_str(),
                 {VmValue::Ref(builder), VmValue::Int(0), VmValue::Int(2)});
            f.On(builder, "append", signature.c_str(),
                 {VmValue::Ref(VmObjectRef{}), VmValue::Int(1), VmValue::Int(3)});
            CHECK(f.model.StringValue(
                      f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                  u"😀中😀ul");
            for (const auto range :
                 {std::pair{-1, 1}, std::pair{2, 1}, std::pair{0, 6}}) {
                const auto result =
                    f.OnOutcome(builder, "append", signature.c_str(),
                                {VmValue::Ref(text), VmValue::Int(range.first),
                                 VmValue::Int(range.second)});
                REQUIRE(result.exception.IsValid());
                CHECK(f.linker.Class(result.exception_class).descriptor ==
                      "Ljava/lang/IndexOutOfBoundsException;");
                CHECK(f.model.StringValue(
                          f.On(builder, "toString", "()Ljava/lang/String;").ref) ==
                      u"😀中😀ul");
            }
        }
    }
}

TEST_CASE(
    "DVM-107 Activity queries honor overrides and Intent setters use ComponentName") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto declaration = IntrinsicClassBuilder::Class("Ltest/NamedActivity;",
                                                        "Landroid/app/Activity;");
        declaration.Constructor("()V",
                                [](IntrinsicContext&) { return VmValue::Void(); });
        declaration.OverrideMethod(
            "getPackageName", "()Ljava/lang/String;", [](IntrinsicContext& c) {
                return VmValue::Ref(c.vm.NewStringUtf8("virtual"));
            });
        declaration.OverrideMethod(
            "getLocalClassName", "()Ljava/lang/String;", [](IntrinsicContext& c) {
                return VmValue::Ref(c.vm.NewStringUtf8("preferences-name"));
            });
        std::vector<IntrinsicClassDecl> extras;
        extras.push_back(std::move(declaration).Build());
        auto null_package = IntrinsicClassBuilder::Class("Ltest/NullPackageActivity;", "Landroid/app/Activity;");
        null_package.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        null_package.OverrideMethod("getPackageName", "()Ljava/lang/String;", [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
        extras.push_back(std::move(null_package).Build());
        AndroidValueVm f(backend, extras);
        f.context->package_name = "fixture";
        const auto activity = f.New("Ltest/NamedActivity;");
        const auto base = f.New("Landroid/content/Context;");
        f.On(activity, "attachBaseContext", "(Landroid/content/Context;)V",
             {VmValue::Ref(base)});
        AttachAndroidActivityIdentity(f.vm, f.context, activity, "virtual.RealName");
        const auto null_activity = f.New("Ltest/NullPackageActivity;");
        AttachAndroidActivityIdentity(f.vm, f.context, null_activity, "fixture.Main");
        const auto null_result = f.OnOutcome(null_activity, "getLocalClassName", "()Ljava/lang/String;");
        REQUIRE(null_result.exception.IsValid());
        CHECK(f.linker.Class(null_result.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        // invoke-super equivalent: Activity's implementation still dispatches
        // getPackageName.
        const auto parent = f.linker.ResolveDescriptor("Landroid/app/Activity;");
        const auto slot = f.linker.FindVtableIndex(parent, "getLocalClassName",
                                                   "()Ljava/lang/String;");
        REQUIRE(slot.has_value());
        const auto local = f.vm.Call(f.linker.Class(parent).vtable[*slot],
                                     std::array{VmValue::Ref(activity)});
        REQUIRE_FALSE(local.exception.IsValid());
        CHECK(f.vm.StringUtf8(local.value.ref) == "RealName");
        const auto prefs =
            f.On(activity, "getPreferences", "(I)Landroid/content/SharedPreferences;",
                 {VmValue::Int(0)})
                .ref;
        CHECK(f.context->preference_names.at(prefs.Value()) == "preferences-name");
        const auto clazz =
            f.model.ClassObject(f.linker.ResolveDescriptor("Ltest/NamedActivity;"));
        const auto intent = f.New("Landroid/content/Intent;",
                                  "(Landroid/content/Context;Ljava/lang/Class;)V",
                                  {VmValue::Ref(activity), VmValue::Ref(clazz)});
        const auto component_name = [&] {
            return f.On(intent, "getComponent", "()Landroid/content/ComponentName;")
                .ref;
        };
        CHECK(
            f.vm.StringUtf8(
                f.On(component_name(), "getPackageName", "()Ljava/lang/String;").ref) ==
            "virtual");
        CHECK(f.vm.StringUtf8(
                  f.On(component_name(), "getClassName", "()Ljava/lang/String;").ref) ==
              "test.NamedActivity");
        f.On(intent, "setClass",
             "(Landroid/content/Context;Ljava/lang/Class;)Landroid/content/Intent;",
             {VmValue::Ref(base), VmValue::Ref(clazz)});
        CHECK(
            f.vm.StringUtf8(
                f.On(component_name(), "getPackageName", "()Ljava/lang/String;").ref) ==
            "fixture");
        f.On(intent, "setClassName",
             "(Landroid/content/Context;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(base), VmValue::Ref(f.vm.NewStringUtf8("fixture.Next"))});
        f.On(base, "startActivity", "(Landroid/content/Intent;)V",
             {VmValue::Ref(intent)});
        CHECK(f.context->pending_activity_descriptor == "Lfixture/Next;");
        CHECK(f.context->pending_activity_component_name == "fixture.Next");
        f.context->pending_activity_descriptor.clear();
        f.context->pending_activity_component_name.clear();
        f.context->activity_switch_pending = false;
        f.On(intent, "setClassName",
             "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("external")),
              VmValue::Ref(f.vm.NewStringUtf8(".Literal"))});
        CHECK(f.vm.StringUtf8(
                  f.On(component_name(), "getClassName", "()Ljava/lang/String;").ref) ==
              ".Literal");
        const auto rejected =
            f.OnOutcome(base, "startActivity", "(Landroid/content/Intent;)V",
                        {VmValue::Ref(intent)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK_FALSE(f.context->activity_switch_pending);
        CHECK(f.context->pending_activity_descriptor.empty());

        f.context->activity_inventory_known = true;
        f.context->activity_components = {
            {ogplay::loader::AndroidManifestComponentKind::activity,
             "fixture.RealActivity", std::nullopt, true,
             {{{"fixture.OPEN"}, {"android.intent.category.DEFAULT"}, false}},
             std::nullopt},
            {ogplay::loader::AndroidManifestComponentKind::activity_alias,
             "fixture.OpenAlias", std::optional<std::string>{"fixture.RealActivity"},
             true,
             {{{"fixture.ALIAS"}, {"android.intent.category.DEFAULT"}, false}},
             std::nullopt}};
        const auto implicit = f.New(
            "Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture.ALIAS"))});
        f.On(base, "startActivity", "(Landroid/content/Intent;)V",
             {VmValue::Ref(implicit)});
        CHECK(f.context->pending_activity_descriptor == "Lfixture/RealActivity;");
        CHECK(f.context->pending_activity_component_name == "fixture.OpenAlias");
        const auto resolved =
            f.On(implicit, "getComponent", "()Landroid/content/ComponentName;").ref;
        CHECK(f.vm.StringUtf8(
                  f.On(resolved, "getClassName", "()Ljava/lang/String;").ref) ==
              "fixture.OpenAlias");
        f.context->pending_activity_descriptor.clear();
        f.context->pending_activity_component_name.clear();
        f.context->activity_switch_pending = false;

        const auto missing = f.New(
            "Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture.MISSING"))});
        const auto missing_outcome = f.OnOutcome(
            base, "startActivity", "(Landroid/content/Intent;)V",
            {VmValue::Ref(missing)});
        REQUIRE(missing_outcome.exception.IsValid());
        CHECK(f.linker.Class(missing_outcome.exception_class).descriptor ==
              "Landroid/content/ActivityNotFoundException;");
        CHECK_FALSE(f.context->activity_switch_pending);

        f.context->activity_components.front().intent_filters[0].categories.clear();
        const auto no_default = f.New(
            "Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture.OPEN"))});
        const auto no_default_outcome = f.OnOutcome(
            base, "startActivity", "(Landroid/content/Intent;)V",
            {VmValue::Ref(no_default)});
        REQUIRE(no_default_outcome.exception.IsValid());
        CHECK(f.linker.Class(no_default_outcome.exception_class).descriptor ==
              "Landroid/content/ActivityNotFoundException;");
        f.context->activity_components.front().intent_filters[0].categories = {
            "android.intent.category.DEFAULT"};

        f.context->activity_components.push_back(
            {ogplay::loader::AndroidManifestComponentKind::activity,
             "fixture.OtherActivity", std::nullopt, true,
             {{{"fixture.ALIAS"}, {"android.intent.category.DEFAULT"}, false}},
             std::nullopt});
        const auto ambiguous = f.New(
            "Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture.ALIAS"))});
        const auto ambiguous_outcome = f.OnOutcome(
            base, "startActivity", "(Landroid/content/Intent;)V",
            {VmValue::Ref(ambiguous)});
        REQUIRE(ambiguous_outcome.exception.IsValid());
        CHECK(f.linker.Class(ambiguous_outcome.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK_FALSE(f.context->activity_switch_pending);

        f.context->activity_components.back().intent_filters[0].has_data = true;
        const auto unique_again = f.New(
            "Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture.ALIAS"))});
        f.On(base, "startActivity", "(Landroid/content/Intent;)V",
             {VmValue::Ref(unique_again)});
        CHECK(f.context->pending_activity_descriptor == "Lfixture/RealActivity;");
    }
}

TEST_CASE("DVM-109 UUID deserialization restores private readObject invariants") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        VmThreadRuntime threads(f.vm);
        const auto uuid = f.Static("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(f.vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto buffer = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto out = f.New("Ljava/io/ObjectOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffer)});
        f.On(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(uuid)});
        f.On(out, "flush", "()V");
        const auto bytes = f.On(buffer, "toByteArray", "()[B").ref;
        const auto source = f.New("Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(bytes)});
        const auto input = f.New("Ljava/io/ObjectInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(source)});
        const auto result = f.OnOutcome(input, "readObject", "()Ljava/lang/Object;");
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        const auto restored = result.value.ref;
        CHECK(f.On(restored, "version", "()I").AsInt() == 1);
        CHECK(f.On(restored, "variant", "()I").AsInt() == 2);
        CHECK(f.On(restored, "timestamp", "()J").AsLong() == INT64_C(130742845922168750));
        CHECK(f.On(restored, "clockSequence", "()I").AsInt() == 0x2765);
        CHECK(f.On(restored, "node", "()J").AsLong() == INT64_C(0x00a0c91e6bf6));
        CHECK(f.On(restored, "hashCode", "()I").AsInt() == f.On(uuid, "hashCode", "()I").AsInt());
        CHECK(f.On(restored, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(uuid)}).AsInt() == 1);
    }
}

TEST_CASE("DVM-108 UUID values and UTF16 substring search follow API19") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto failure = f.New("Ljava/security/ProviderException;", "(Ljava/lang/String;)V",
                                   {VmValue::Ref(f.vm.NewStringUtf8("provider failed"))});
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Ljava/lang/RuntimeException;"), f.model.ObjectClass(failure)));
        CHECK(f.vm.StringUtf8(f.On(failure, "getMessage", "()Ljava/lang/String;").ref) == "provider failed");
        const auto text = f.model.NewString(u"a😀中😀z");
        const auto search = [&](std::u16string_view needle, int start) {
            return f.On(text, "indexOf", "(Ljava/lang/String;I)I",
                        {VmValue::Ref(f.model.NewString(std::u16string(needle))), VmValue::Int(start)}).AsInt();
        };
        CHECK(search(u"😀", -1) == 1);
        CHECK(search(u"😀", 2) == 4);
        CHECK(search(u"中", 4) == -1);
        CHECK(search(u"", 100) == 7);
        CHECK(search(u"z", 100) == -1);
        const auto null_search = f.OnOutcome(text, "indexOf", "(Ljava/lang/String;I)I",
                                            {VmValue::Ref(VmObjectRef{}), VmValue::Int(100)});
        REQUIRE(null_search.exception.IsValid());
        CHECK(f.linker.Class(null_search.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto prefix = [&](std::u16string_view value, int start) {
            return f.On(text, "startsWith", "(Ljava/lang/String;I)Z",
                        {VmValue::Ref(f.model.NewString(std::u16string(value))), VmValue::Int(start)}).AsInt();
        };
        CHECK(prefix(u"😀", 1) == 1);
        CHECK(prefix(u"😀", 2) == 0);
        CHECK(prefix(u"", 7) == 1);
        CHECK(prefix(u"", 8) == 0);
        CHECK(prefix(u"", -1) == 0);
        const auto missing_prefix = f.OnOutcome(text, "startsWith", "(Ljava/lang/String;I)Z",
                                               {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(missing_prefix.exception.IsValid());
        CHECK(f.linker.Class(missing_prefix.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto zero = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(0), VmValue::Long(0)});
        const auto negative = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(INT64_MIN), VmValue::Long(-1)});
        const auto equal = f.New("Ljava/util/UUID;", "(JJ)V", {VmValue::Long(INT64_MIN), VmValue::Long(-1)});
        CHECK(f.On(negative, "getMostSignificantBits", "()J").AsLong() == INT64_MIN);
        CHECK(f.On(negative, "getLeastSignificantBits", "()J").AsLong() == -1);
        CHECK(f.vm.StringUtf8(f.On(negative, "toString", "()Ljava/lang/String;").ref) == "80000000-0000-0000-ffff-ffffffffffff");
        CHECK(f.On(negative, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(equal)}).AsInt() == 1);
        CHECK(f.On(negative, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(zero)}).AsInt() == 0);
        CHECK(f.On(negative, "hashCode", "()I").AsInt() == f.On(equal, "hashCode", "()I").AsInt());
        CHECK(f.On(negative, "compareTo", "(Ljava/util/UUID;)I", {VmValue::Ref(zero)}).AsInt() < 0);
        CHECK(f.On(equal, "compareTo", "(Ljava/util/UUID;)I", {VmValue::Ref(negative)}).AsInt() == 0);
        CHECK(f.On(negative, "variant", "()I").AsInt() == 7);
        CHECK(f.On(zero, "variant", "()I").AsInt() == 0);
        const auto parse = *f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ljava/util/UUID;"), "fromString", "(Ljava/lang/String;)Ljava/util/UUID;");
        for (const auto* invalid : {"", "a-b-c-d", "a-b-c-d-e-f", "invalid-1-1-1-1"}) {
            const auto result = f.vm.Call(parse, std::array{VmValue::Ref(f.vm.NewStringUtf8(invalid))});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Ljava/lang/IllegalArgumentException;"), result.exception_class));
        }
    }
}

TEST_CASE("DVM-112 resolveService distinguishes absent candidates from unsupported queries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto manager = f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("example.SERVICE"))});
        const auto roots = f.vm.ProtectReferences(std::array{base, manager, intent});
        auto query = [&](VmObjectRef value, int flags = 0) {
            return f.OnOutcome(manager, "resolveService",
                "(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;",
                {VmValue::Ref(value), VmValue::Int(flags)});
        };
        auto fail = [&](const VmCallOutcome& result, const char* type = "Ljava/lang/UnsupportedOperationException;") {
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == type);
        };
        auto absent = [&] {
            const auto result = query(intent);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            CHECK_FALSE(result.value.ref.IsValid());
        };
        fail(query(VmObjectRef{}), "Ljava/lang/NullPointerException;");
        fail(query(intent));  // Missing inventory is not an empty installed-service list.
        f.context->service_inventory_known = true;
        absent();
        f.context->service_components = {
            {"example.Plain", true, {}},
            {"example.Other", true, {{{"example.OTHER"}, {}, false}}},
            {"example.Disabled", false, {{{"example.SERVICE"}, {}, false}}},
        };
        absent();
        CHECK(f.ledger.Unimplemented().size() == 1);
        CHECK(f.ledger.Unimplemented()[0].id == "dexvm.service_resolution");
        CHECK(f.ledger.Unimplemented()[0].count == 1);
        f.context->service_components.push_back(
            {"example.Candidate", true, {{{"example.SERVICE"}, {"example.EXTRA"}, false}}});
        fail(query(intent));  // An Intent without categories can match a filter with categories.
        f.context->service_components.back().intent_filters[0].has_data = true;
        fail(query(intent));  // Data constraints cannot be silently discarded.
        f.context->application_enabled = false;
        absent();
        f.context->application_enabled = true;
        f.context->service_components.clear();
        for (const int flags : {1, 128, -1}) fail(query(intent, flags));
        const auto empty = f.New("Landroid/content/Intent;");
        fail(query(empty));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("text/plain"))});
        fail(query(intent));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("example.CATEGORY"))});
        fail(query(intent));
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V",
             {VmValue::Ref(f.vm.NewStringUtf8("example.CATEGORY"))});
        const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
            {VmValue::Ref(f.vm.NewStringUtf8("content://example/value"))}).ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(uri)});
        fail(query(intent));
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
            {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Local"))});
        fail(query(intent));
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        static_cast<void>(f.vm.CollectGarbage());
        absent();
        CHECK(f.ledger.Unimplemented()[0].count == 11);
    }
}

TEST_CASE("DVM-142 PackageManager reflection resolves the BootDex ResolveInfo family") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);

        for (const auto* descriptor : {
                 "Landroid/content/pm/PackageItemInfo;",
                 "Landroid/content/pm/ApplicationInfo;",
                 "Landroid/content/pm/ComponentInfo;",
                 "Landroid/content/pm/ActivityInfo;",
                 "Landroid/content/pm/ServiceInfo;",
                 "Landroid/content/pm/ProviderInfo;",
                 "Landroid/content/pm/ResolveInfo;",
             }) {
            CHECK(f.New(descriptor).IsValid());
        }

        const auto pattern = f.vm.NewStringUtf8("/example");
        CHECK(f.New("Landroid/os/PatternMatcher;", "(Ljava/lang/String;I)V",
                    {VmValue::Ref(pattern), VmValue::Int(0)})
                  .IsValid());
        CHECK(f.New("Landroid/content/pm/PathPermission;",
                    "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;)V",
                    {VmValue::Ref(pattern), VmValue::Int(0),
                     VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})})
                  .IsValid());

        const auto class_array = f.model.NewObjectArray(
            f.linker.ResolveDescriptor("[Ljava/lang/Class;"),
            f.linker.ResolveDescriptor("Ljava/lang/Class;"), 1);
        f.model.SetObjectElement(
            class_array, 0,
            f.model.ClassObject(
                f.linker.ResolveDescriptor("Ljava/lang/String;")));
        const auto package_manager_class = f.model.ClassObject(
            f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager;"));
        const auto reflected = f.OnOutcome(
            package_manager_class, "getMethod",
            "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
            {VmValue::Ref(f.vm.NewStringUtf8("hasSystemFeature")),
             VmValue::Ref(class_array)});
        REQUIRE_MESSAGE(!reflected.exception.IsValid(),
                        reflected.exception_message);
        CHECK(reflected.value.ref.IsValid());
    }
}

TEST_CASE("PackageManager getInstallerPackageName reports direct-load provenance") {
    constexpr auto kQuery = "(Ljava/lang/String;)Ljava/lang/String;";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        const auto base = f.New("Landroid/content/Context;");
        const auto manager = f.On(base, "getPackageManager",
                                 "()Landroid/content/pm/PackageManager;").ref;
        const auto package = f.On(base, "getPackageName", "()Ljava/lang/String;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{base, manager, package});
        const auto query = [&](VmObjectRef name) {
            return f.OnOutcome(manager, "getInstallerPackageName", kQuery,
                               {VmValue::Ref(name)});
        };
        const auto current = query(package);
        REQUIRE_MESSAGE(!current.exception.IsValid(), current.exception_message);
        CHECK_FALSE(current.value.ref.IsValid());

        // API19 throws IllegalArgumentException, including for null input.
        for (const auto* name : {"org.example.missing", "android", ""}) {
            const auto unknown = query(f.vm.NewStringUtf8(name));
            REQUIRE(unknown.exception.IsValid());
            CHECK(f.linker.Class(unknown.exception_class).descriptor ==
                  "Ljava/lang/IllegalArgumentException;");
            CHECK(unknown.exception_message == std::string("Unknown package: ") + name);
        }
        const auto null_name = query(VmObjectRef{});
        REQUIRE(null_name.exception.IsValid());
        CHECK(f.linker.Class(null_name.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
        CHECK(null_name.exception_message == "Unknown package: null");
        CHECK_FALSE(f.On(manager, "getInstallerPackageName", kQuery,
                         {VmValue::Ref(package)}).ref.IsValid());

        // An unconfigured Context must never make the empty string a package.
        f.context->package_name.clear();
        const auto empty = query(f.vm.NewStringUtf8(""));
        REQUIRE(empty.exception.IsValid());
        CHECK(f.linker.Class(empty.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
    }
}

TEST_CASE("DVM-180 getPackageInfo returns current-package Activity metadata") {
    using ogplay::loader::AndroidManifestActivityComponent;
    using ogplay::loader::AndroidManifestComponentKind;
    constexpr auto kGetActivities = 0x00000001;
    constexpr auto kGetMetaData = 0x00000080;
    constexpr auto kGetPermissions = 0x00001000;
    constexpr auto kGetPackageInfo =
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->package_version_code = 7U;
        f.context->package_version_name = "1.2.3";
        f.context->requested_permissions = {"android.permission.INTERNET"};
        f.context->granted_permissions.insert("android.permission.CAMERA");
        f.context->activity_components = {
            {AndroidManifestComponentKind::activity, ".Main", std::nullopt, true,
             {{{"android.intent.action.MAIN"},
               {"android.intent.category.LAUNCHER"},
               false}},
             std::nullopt, true},
            {AndroidManifestComponentKind::activity, "org.example.game.Hidden",
             std::nullopt, true, {}, std::nullopt, std::nullopt},
            {AndroidManifestComponentKind::activity, ".Disabled", std::nullopt,
             false, {{{"android.intent.action.VIEW"}, {}, false}}, std::nullopt,
             false},
            {AndroidManifestComponentKind::activity, ".ForcedPrivate", std::nullopt,
             true, {{{"android.intent.action.SEND"}, {}, false}}, std::nullopt,
             false},
            {AndroidManifestComponentKind::activity_alias, ".Alias",
             std::optional<std::string>{".Main"}, true,
             {{{"android.intent.action.MAIN"},
               {"android.intent.category.DEFAULT"},
               false}},
             std::nullopt, std::nullopt},
        };

        const auto manager = f.vm.NewIntrinsicInstance(
            "Landroid/content/pm/PackageManager;");
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto package = f.vm.NewStringUtf8("org.example.game");
        const auto pm_class =
            f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager;");
        CHECK(f.linker.FindFieldRecursive(pm_class, "GET_ACTIVITIES", "I")
                  .has_value());
        const auto field = [&](const VmObjectRef object, const std::string& name,
                               const std::string& descriptor) {
            const auto found = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot];
        };
        const auto ref_field = [&](const VmObjectRef object,
                                   const std::string& name,
                                   const std::string& descriptor) {
            const auto slot = field(object, name, descriptor);
            if (slot.bits == 0U) return VmObjectRef{};
            REQUIRE(slot.tag == SlotTag::ref);
            return VmObjectRef{static_cast<std::uint32_t>(slot.bits)};
        };
        const auto bool_field = [&](const VmObjectRef object,
                                    const std::string& name) {
            const auto slot = field(object, name, "Z");
            REQUIRE(slot.tag == SlotTag::cat1);
            return slot.bits != 0U;
        };
        const auto query = [&](const std::int32_t flags) {
            return f.On(manager, "getPackageInfo", kGetPackageInfo,
                        {VmValue::Ref(package), VmValue::Int(flags)});
        };
        const auto activity_at = [&](const VmObjectRef array,
                                     const JniSize index) {
            REQUIRE(array.IsValid());
            REQUIRE(f.model.ArrayLength(array) > index);
            return f.model.GetObjectElement(array, index);
        };
        const auto name_of = [&](const VmObjectRef info) {
            return f.vm.StringUtf8(
                ref_field(info, "name", "Ljava/lang/String;"));
        };

        const auto none = query(0);
        CHECK_FALSE(ref_field(none.ref, "activities",
                              "[Landroid/content/pm/ActivityInfo;")
                        .IsValid());
        CHECK_FALSE(ref_field(none.ref, "requestedPermissions",
                              "[Ljava/lang/String;")
                        .IsValid());

        const auto permissions_only = query(kGetPermissions);
        CHECK_FALSE(ref_field(permissions_only.ref, "activities",
                              "[Landroid/content/pm/ActivityInfo;")
                        .IsValid());
        const auto permissions = ref_field(permissions_only.ref,
                                           "requestedPermissions",
                                           "[Ljava/lang/String;");
        REQUIRE(permissions.IsValid());
        REQUIRE(f.model.ArrayLength(permissions) == 1);
        CHECK(f.vm.StringUtf8(f.model.GetObjectElement(permissions, 0)) ==
              "android.permission.INTERNET");

        const auto activities_only = query(kGetActivities);
        CHECK_FALSE(ref_field(activities_only.ref, "requestedPermissions",
                              "[Ljava/lang/String;")
                        .IsValid());
        auto activities = ref_field(activities_only.ref, "activities",
                                    "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(activities.IsValid());
        REQUIRE(f.model.ArrayLength(activities) == 4);
        const auto main = activity_at(activities, 0);
        const auto hidden = activity_at(activities, 1);
        const auto forced_private = activity_at(activities, 2);
        const auto alias = activity_at(activities, 3);
        CHECK(name_of(main) == "org.example.game.Main");
        CHECK(name_of(hidden) == "org.example.game.Hidden");
        CHECK(name_of(forced_private) == "org.example.game.ForcedPrivate");
        CHECK(name_of(alias) == "org.example.game.Alias");
        CHECK(f.vm.StringUtf8(ref_field(main, "packageName",
                                        "Ljava/lang/String;")) ==
              "org.example.game");
        CHECK(bool_field(main, "enabled"));
        CHECK(bool_field(main, "exported"));
        CHECK_FALSE(bool_field(hidden, "exported"));
        CHECK_FALSE(bool_field(forced_private, "exported"));
        CHECK(bool_field(alias, "exported"));
        CHECK_FALSE(ref_field(main, "targetActivity", "Ljava/lang/String;")
                        .IsValid());
        CHECK(f.vm.StringUtf8(ref_field(alias, "targetActivity",
                                        "Ljava/lang/String;")) ==
              "org.example.game.Main");
        const auto application = ref_field(
            alias, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
        REQUIRE(application.IsValid());
        CHECK(f.vm.StringUtf8(ref_field(application, "packageName",
                                        "Ljava/lang/String;")) ==
              "org.example.game");
        CHECK(ref_field(activities_only.ref, "applicationInfo",
                        "Landroid/content/pm/ApplicationInfo;") == application);

        const auto combined = query(kGetActivities | kGetPermissions | kGetMetaData);
        CHECK(f.model.ArrayLength(ref_field(
                  combined.ref, "activities",
                  "[Landroid/content/pm/ActivityInfo;")) == 4);
        CHECK(f.model.ArrayLength(ref_field(
                  combined.ref, "requestedPermissions",
                  "[Ljava/lang/String;")) == 1);
        CHECK(ref_field(ref_field(combined.ref, "applicationInfo",
                                  "Landroid/content/pm/ApplicationInfo;"),
                        "metaData", "Landroid/os/Bundle;")
                  .IsValid());
        CHECK_FALSE(ref_field(ref_field(activities_only.ref, "applicationInfo",
                                        "Landroid/content/pm/ApplicationInfo;"),
                              "metaData", "Landroid/os/Bundle;")
                        .IsValid());

        const auto flags_4097 = query(kGetActivities | kGetPermissions);
        CHECK(f.model.ArrayLength(ref_field(
                  flags_4097.ref, "activities",
                  "[Landroid/content/pm/ActivityInfo;")) == 4);

        const auto mutated_name = f.vm.NewStringUtf8("mutated.Name");
        const auto name_slot = f.linker.FindFieldRecursive(
            f.model.ObjectClass(main), "name", "Ljava/lang/String;");
        REQUIRE(name_slot.has_value());
        f.model.InstanceSlots(main)[f.linker.Field(*name_slot).slot] = {
            mutated_name.Value(), SlotTag::ref};
        f.model.SetObjectElement(activities, 0, VmObjectRef{});
        const auto again = query(kGetActivities);
        const auto fresh = ref_field(again.ref, "activities",
                                     "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(f.model.ArrayLength(fresh) == 4);
        CHECK(name_of(f.model.GetObjectElement(fresh, 0)) ==
              "org.example.game.Main");
        CHECK(f.model.GetObjectElement(fresh, 0).IsValid());

        f.context->activity_components = {
            {AndroidManifestComponentKind::activity, ".OnlyDisabled",
             std::nullopt, false, {}, std::nullopt, std::nullopt},
        };
        const auto disabled_only = query(kGetActivities);
        const auto disabled_array = ref_field(
            disabled_only.ref, "activities",
            "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(disabled_array.IsValid());
        CHECK(f.model.ArrayLength(disabled_array) == 0);

        f.context->activity_components.clear();
        const auto empty = query(kGetActivities);
        CHECK_FALSE(ref_field(empty.ref, "activities",
                              "[Landroid/content/pm/ActivityInfo;")
                        .IsValid());

        const auto unknown = f.OnOutcome(
            manager, "getPackageInfo", kGetPackageInfo,
            {VmValue::Ref(f.vm.NewStringUtf8("org.example.missing")),
             VmValue::Int(kGetActivities)});
        REQUIRE(unknown.exception.IsValid());
        CHECK(f.linker.Class(unknown.exception_class).descriptor ==
              "Landroid/content/pm/PackageManager$NameNotFoundException;");
        const auto unsupported = f.OnOutcome(
            manager, "getPackageInfo", kGetPackageInfo,
            {VmValue::Ref(package), VmValue::Int(0x00000002)});
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(unsupported.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        static_cast<void>(f.vm.CollectGarbage());
    }
}

TEST_CASE("PackageManager archive queries preserve sealed path and API19 metadata semantics") {
    constexpr auto signature = "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;";
    constexpr auto activities_flag = 1;
    constexpr auto disabled_flag = 0x200;
    using Kind = ogplay::loader::AndroidManifestComponentKind;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        VirtualFileSystem vfs;
        AndroidValueVm f(backend);
        auto& context = *f.context;
        context.vfs = &vfs;
        context.package_name = "org.example.game";
        context.package_resource_path = "/data/app/org.example.game-1.apk";
        context.package_version_code = 7;
        context.package_version_name = "1.2.3";
        context.application_class_name = "org.example.game.Application";
        context.target_sdk_version = 19;
        context.application_uid = 10000;
        context.application_label = std::string("Game 名称");
        context.application_enabled = false;
        context.activity_inventory_known = true;
        // Inject already-parsed, sealed facts; raw ZIP parsing is owned by startup.
        context.apk_bytes = {std::byte{'a'}, std::byte{'p'}, std::byte{'k'}};
        vfs.PutFile(context.package_resource_path, context.apk_bytes, false);
        vfs.SetWorkingDirectory("/data/app");
        vfs.AddPathAlias("/archive", "/data/app");
        context.activity_components = {
            {Kind::activity, ".Main", std::nullopt, true, {}, std::nullopt, false},
            {Kind::activity, ".Disabled", std::nullopt, false, {}, std::nullopt, false},
            {Kind::activity_alias, ".Alias", std::string(".Main"), true, {}, std::nullopt, false},
        };
        const auto manager = f.vm.NewIntrinsicInstance("Landroid/content/pm/PackageManager;");
        const auto root = f.vm.ProtectReferences(std::array{manager});
        const auto field = [&](VmObjectRef object, const char* name, const char* descriptor) {
            const auto handle = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(handle.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*handle).slot];
        };
        const auto ref_field = [&](VmObjectRef object, const char* name, const char* descriptor) {
            const auto slot = field(object, name, descriptor);
            return VmObjectRef(static_cast<std::uint32_t>(slot.bits));
        };
        const auto text = [&](VmObjectRef object, const char* name) {
            return f.vm.StringUtf8(ref_field(object, name, "Ljava/lang/String;"));
        };
        const auto query = [&](const std::string& path, std::int32_t flags = activities_flag) {
            return f.On(manager, "getPackageArchiveInfo", signature,
                        {VmValue::Ref(f.vm.NewStringUtf8(path)), VmValue::Int(flags)}).ref;
        };
        const auto expect_unsupported = [&](const std::string& path, std::int32_t flags) {
            const auto outcome = f.OnOutcome(manager, "getPackageArchiveInfo", signature,
                        {VmValue::Ref(f.vm.NewStringUtf8(path)), VmValue::Int(flags)});
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        };
        const auto path = context.package_resource_path;
        const auto info = query(path);
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        REQUIRE(info.IsValid());
        CHECK(text(info, "packageName") == context.package_name);
        CHECK(text(info, "versionName") == "1.2.3");
        CHECK(field(info, "versionCode", "I").bits == 7);
        CHECK_FALSE(ref_field(info, "requestedPermissions", "[Ljava/lang/String;").IsValid());
        // PackageInfo's long time fields are explicit zero archive facts, not host times.
        for (const auto name : {"firstInstallTime", "lastUpdateTime"}) {
            const auto handle = f.linker.FindFieldRecursive(f.model.ObjectClass(info), name, "J");
            REQUIRE(handle.has_value());
            const auto slot = f.linker.Field(*handle).slot;
            CHECK(f.model.InstanceSlots(info)[slot].bits == 0);
            CHECK(f.model.InstanceSlots(info)[slot + 1].bits == 0);
        }
        const auto application = ref_field(info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
        REQUIRE(application.IsValid());
        CHECK(text(application, "packageName") == context.package_name);
        CHECK(text(application, "className") == context.application_class_name);
        CHECK(field(application, "uid", "I").bits == UINT32_MAX);
        CHECK(field(application, "targetSdkVersion", "I").bits == 19);
        CHECK(field(application, "enabled", "Z").bits == 0);
        CHECK(field(application, "flags", "I").bits == ((1U << 2) | (1U << 23)));
        for (const auto name : {"sourceDir", "publicSourceDir", "dataDir", "nativeLibraryDir"})
            CHECK_FALSE(ref_field(application, name, "Ljava/lang/String;").IsValid());
        CHECK_FALSE(ref_field(application, "metaData", "Landroid/os/Bundle;").IsValid());
        auto array = ref_field(info, "activities", "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(f.model.ArrayLength(array) == 2);
        const auto main = f.model.GetObjectElement(array, 0);
        const auto alias = f.model.GetObjectElement(array, 1);
        CHECK(text(main, "name") == "org.example.game.Main");
        CHECK(text(alias, "name") == "org.example.game.Alias");
        CHECK(text(alias, "targetActivity") == "org.example.game.Main");
        CHECK(ref_field(alias, "applicationInfo", "Landroid/content/pm/ApplicationInfo;") == application);
        CHECK_FALSE(ref_field(query(path, 0), "activities", "[Landroid/content/pm/ActivityInfo;").IsValid());
        CHECK_FALSE(ref_field(query(path, disabled_flag), "activities", "[Landroid/content/pm/ActivityInfo;").IsValid());
        const auto all = query(path, activities_flag | disabled_flag);
        const auto all_array = ref_field(all, "activities", "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(f.model.ArrayLength(all_array) == 3);
        CHECK(text(f.model.GetObjectElement(all_array, 1), "name") == "org.example.game.Disabled");
        CHECK(field(f.model.GetObjectElement(all_array, 1), "enabled", "Z").bits == 0);
        CHECK(text(f.model.GetObjectElement(all_array, 2), "name") == "org.example.game.Alias");
        for (const auto spelling : {"/data/app/./org.example.game-1.apk", "/DATA/APP/ORG.EXAMPLE.GAME-1.APK",
                                   "org.example.game-1.apk", "/archive/org.example.game-1.apk"})
            CHECK(text(query(spelling), "packageName") == context.package_name);
        for (const auto spelling : {"", "/", "/data/app", "/missing.apk", "/host/file.apk"})
            CHECK_FALSE(query(spelling).IsValid());
        const auto null_path = f.OnOutcome(manager, "getPackageArchiveInfo", signature,
                                        {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(null_path.exception.IsValid());
        CHECK(f.linker.Class(null_path.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        for (const auto flags : {2, 0x40, 0x80, 0x1000, -1}) expect_unsupported(path, flags);
        vfs.PutFile("/data/app/other.apk", context.apk_bytes, false);
        expect_unsupported("/data/app/other.apk", 0); // unregistered archive cannot pretend to be absent.
        context.activity_inventory_known = false;
        expect_unsupported(path, activities_flag);
        CHECK(query(path, 0).IsValid());
        context.activity_inventory_known = true;
        // Guest mutation of a returned result cannot corrupt the next snapshot.
        f.model.SetObjectElement(array, 0, VmObjectRef{});
        const auto fresh = query(path);
        CHECK(fresh != info);
        CHECK(text(f.model.GetObjectElement(ref_field(fresh, "activities", "[Landroid/content/pm/ActivityInfo;"), 0),
                   "name") == "org.example.game.Main");
        const auto installed = f.On(manager, "getPackageInfo", signature,
                    {VmValue::Ref(f.vm.NewStringUtf8(context.package_name)), VmValue::Int(activities_flag)}).ref;
        const auto installed_app = ref_field(installed, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
        CHECK(field(installed_app, "uid", "I").bits == 10000);
        CHECK(text(installed_app, "sourceDir") == path);
        CHECK(text(installed_app, "dataDir") == "/data/data/org.example.game");
        CHECK(text(installed_app, "nativeLibraryDir") == "/data/app-lib");
        CHECK_FALSE(ref_field(application, "sourceDir", "Ljava/lang/String;").IsValid());
        context.activity_components[0].enabled = false;
        context.activity_components[2].enabled = false;
        CHECK(f.model.ArrayLength(ref_field(query(path), "activities", "[Landroid/content/pm/ActivityInfo;")) == 0);
        context.activity_components.clear();
        CHECK_FALSE(ref_field(query(path), "activities", "[Landroid/content/pm/ActivityInfo;").IsValid());
        VirtualFileSystem writable_backing;
        writable_backing.PutFile(path, context.apk_bytes, true);
        context.vfs = &writable_backing;
        expect_unsupported(path, 0);
        VirtualFileSystem wrong_size;
        wrong_size.PutFile(path, std::array{std::byte{0}}, false);
        context.vfs = &wrong_size;
        expect_unsupported(path, 0);
        context.vfs = nullptr;
        expect_unsupported(path, 0);
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id == "dexvm.package_archive_info" && hit.count >= 9;
        }));
        static_cast<void>(f.vm.CollectGarbage());
    }
}

TEST_CASE("PackageManager getPermissionInfo queries definitions, not requests") {
    constexpr auto kQuery =
        "(Ljava/lang/String;I)Landroid/content/pm/PermissionInfo;";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->permission_inventory_known = true;
        f.context->requested_permissions = {"external.REQUESTED"};
        f.context->granted_permissions.insert("external.REQUESTED");
        f.context->arsc.entries.push_back(
            {.resource_id = 0x7f050001U, .type_name = "string",
             .entry_name = "permission_value", .string_value = "resolved",
             .value_type = 0x03U});
        ogplay::loader::AndroidManifestPermissionDefinition definition;
        definition.name = "org.example.game.C2D_MESSAGE";
        definition.package_name = "org.example.game";
        definition.protection_level = 2;
        definition.group = "org.example.GROUP";
        definition.flags = 1;
        definition.description_res = 0x7f030002U;
        definition.meta_data = {
            {"permission.string", std::string("ready")},
            {"permission.bool", true}, {"permission.int", std::int32_t{42}},
            {"permission.value", ogplay::loader::AndroidManifestMetaDataValueReference{0x7f050001U}},
            {"permission.resource", ogplay::loader::AndroidManifestMetaDataResourceReference{0x7f030001U}}};
        f.context->defined_permissions = {definition};
        const auto manager = f.vm.NewIntrinsicInstance(
            "Landroid/content/pm/PackageManager;");
        const auto roots = f.vm.ProtectReferences(std::array{manager});
        const auto query = [&](const char* name, const int flags) {
            return f.OnOutcome(manager, "getPermissionInfo", kQuery,
                               {VmValue::Ref(f.vm.NewStringUtf8(name)),
                                VmValue::Int(flags)});
        };
        const auto field = [&](const VmObjectRef object, const char* name,
                               const char* descriptor) {
            const auto found = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot].bits;
        };
        const auto ref_field = [&](const VmObjectRef object, const char* name,
                                   const char* descriptor) {
            return VmObjectRef{static_cast<std::uint32_t>(field(object, name, descriptor))};
        };
        const auto string_field = [&](const VmObjectRef object, const char* name) {
            return f.vm.StringUtf8(ref_field(object, name, "Ljava/lang/String;"));
        };
        const auto get_string = [&](const VmObjectRef bundle, const char* key) {
            return f.On(bundle, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                        {VmValue::Ref(f.vm.NewStringUtf8(key))}).ref;
        };
        const auto plain = query("org.example.game.C2D_MESSAGE", 0);
        REQUIRE_FALSE(plain.exception.IsValid());
        CHECK(string_field(plain.value.ref, "name") == "org.example.game.C2D_MESSAGE");
        CHECK(string_field(plain.value.ref, "packageName") == "org.example.game");
        CHECK(string_field(plain.value.ref, "group") == "org.example.GROUP");
        CHECK(field(plain.value.ref, "protectionLevel", "I") == 2);
        CHECK(field(plain.value.ref, "flags", "I") == 1);
        CHECK(field(plain.value.ref, "descriptionRes", "I") == 0x7f030002U);
        CHECK_FALSE(ref_field(plain.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        const auto with_meta = query("org.example.game.C2D_MESSAGE", 0x80);
        REQUIRE_FALSE(with_meta.exception.IsValid());
        const auto info = with_meta.value.ref;
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        const auto metadata = ref_field(info, "metaData", "Landroid/os/Bundle;");
        REQUIRE(metadata.IsValid());
        CHECK(f.vm.StringUtf8(get_string(metadata, "permission.string")) == "ready");
        CHECK(f.vm.StringUtf8(get_string(metadata, "permission.value")) == "resolved");
        CHECK(f.On(metadata, "getBoolean", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("permission.bool"))}).AsInt() == 1);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("permission.int"))}).AsInt() == 42);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("permission.resource"))}).AsInt() ==
              static_cast<std::int32_t>(0x7f030001U));
        const auto again = query("org.example.game.C2D_MESSAGE", 0x80);
        REQUIRE_FALSE(again.exception.IsValid());
        CHECK(again.value.ref != info);
        CHECK(ref_field(again.value.ref, "metaData", "Landroid/os/Bundle;") != metadata);
        const auto missing = f.linker.ResolveDescriptor(
            "Landroid/content/pm/PackageManager$NameNotFoundException;");
        CHECK(query("external.REQUESTED", 0).exception_class == missing);
        CHECK(query("other.package.Permission", 0).exception_class == missing);
        const auto unsupported = query("org.example.game.C2D_MESSAGE", 0x200);
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(unsupported.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        f.context->permission_inventory_known = false;
        const auto unavailable = query("org.example.game.C2D_MESSAGE", 0);
        REQUIRE(unavailable.exception.IsValid());
        CHECK(f.linker.Class(unavailable.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto& hit) {
            return hit.id == "dexvm.permission_info" && hit.count == 2;
        }));
        static_cast<void>(f.vm.CollectGarbage("permission-info"));
        CHECK(f.vm.StringUtf8(get_string(ref_field(info, "metaData",
                                                    "Landroid/os/Bundle;"),
                                         "permission.string")) == "ready");
    }
}

TEST_CASE("PackageManager getServiceInfo uses current Manifest service facts") {
    constexpr auto kQuery =
        "(Landroid/content/ComponentName;I)Landroid/content/pm/ServiceInfo;";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->application_process_name = "org.example.game:app";
        f.context->service_inventory_known = true;
        f.context->application_meta_data.emplace("application.key", std::string("app"));
        f.context->arsc.entries.push_back(
            {.resource_id = 0x7f050001U, .type_name = "string",
             .entry_name = "service_value", .string_value = "resolved",
             .value_type = 0x03U});
        ogplay::loader::AndroidManifestServiceComponent push;
        push.name = "org.example.game.PushService";
        push.exported = true;
        push.process_name = "org.example.game:push";
        push.permission = "org.example.SERVICE";
        push.flags = 3;
        push.meta_data = {{"service.string", std::string("ready")},
                          {"service.bool", true}, {"service.int", std::int32_t{42}},
                          {"service.value", ogplay::loader::AndroidManifestMetaDataValueReference{0x7f050001U}},
                          {"service.resource", ogplay::loader::AndroidManifestMetaDataResourceReference{0x7f030001U}}};
        ogplay::loader::AndroidManifestServiceComponent disabled;
        disabled.name = "org.example.game.DisabledService";
        disabled.enabled = false;
        ogplay::loader::AndroidManifestServiceComponent no_meta;
        no_meta.name = "org.example.game.NoMetaService";
        f.context->service_components = {push, disabled, no_meta};
        const auto manager = f.vm.NewIntrinsicInstance(
            "Landroid/content/pm/PackageManager;");
        const auto component = [&](const char* package, const char* name) {
            return f.New("Landroid/content/ComponentName;",
                         "(Ljava/lang/String;Ljava/lang/String;)V",
                         {VmValue::Ref(f.vm.NewStringUtf8(package)),
                          VmValue::Ref(f.vm.NewStringUtf8(name))});
        };
        const auto present = component("org.example.game", "org.example.game.PushService");
        const auto empty = component("org.example.game", "org.example.game.NoMetaService");
        const auto hidden = component("org.example.game", "org.example.game.DisabledService");
        const auto absent = component("org.example.game", "org.example.game.Absent");
        const auto foreign = component("other.package", "org.example.game.PushService");
        const auto roots = f.vm.ProtectReferences(
            std::array{manager, present, empty, hidden, absent, foreign});
        const auto query = [&](const VmObjectRef name, const int flags) {
            return f.OnOutcome(manager, "getServiceInfo", kQuery,
                               {VmValue::Ref(name), VmValue::Int(flags)});
        };
        const auto field = [&](const VmObjectRef object, const char* name,
                               const char* descriptor) {
            const auto found = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot].bits;
        };
        const auto ref_field = [&](const VmObjectRef object, const char* name,
                                   const char* descriptor) {
            return VmObjectRef{static_cast<std::uint32_t>(field(object, name, descriptor))};
        };
        const auto string_field = [&](const VmObjectRef object, const char* name) {
            return f.vm.StringUtf8(ref_field(object, name, "Ljava/lang/String;"));
        };
        const auto bundle_string = [&](const VmObjectRef bundle, const char* key) {
            return f.On(bundle, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                        {VmValue::Ref(f.vm.NewStringUtf8(key))}).ref;
        };
        const auto plain = query(present, 0);
        REQUIRE_FALSE(plain.exception.IsValid());
        CHECK(string_field(plain.value.ref, "name") == "org.example.game.PushService");
        CHECK(string_field(plain.value.ref, "packageName") == "org.example.game");
        CHECK(string_field(plain.value.ref, "processName") == "org.example.game:push");
        CHECK(string_field(plain.value.ref, "permission") == "org.example.SERVICE");
        CHECK(field(plain.value.ref, "enabled", "Z") == 1);
        CHECK(field(plain.value.ref, "exported", "Z") == 1);
        CHECK(field(plain.value.ref, "flags", "I") == 3);
        CHECK_FALSE(ref_field(plain.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        const auto with_meta = query(present, 0x80);
        REQUIRE_FALSE(with_meta.exception.IsValid());
        const auto info = with_meta.value.ref;
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        const auto metadata = ref_field(info, "metaData", "Landroid/os/Bundle;");
        REQUIRE(metadata.IsValid());
        CHECK(f.vm.StringUtf8(bundle_string(metadata, "service.string")) == "ready");
        CHECK(f.vm.StringUtf8(bundle_string(metadata, "service.value")) == "resolved");
        CHECK_FALSE(bundle_string(metadata, "application.key").IsValid());
        CHECK(f.On(metadata, "getBoolean", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("service.bool"))}).AsInt() == 1);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("service.int"))}).AsInt() == 42);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("service.resource"))}).AsInt() ==
              static_cast<std::int32_t>(0x7f030001U));
        const auto app = ref_field(info, "applicationInfo",
                                   "Landroid/content/pm/ApplicationInfo;");
        const auto app_meta = ref_field(app, "metaData", "Landroid/os/Bundle;");
        REQUIRE(app_meta.IsValid());
        CHECK(f.vm.StringUtf8(bundle_string(app_meta, "application.key")) == "app");
        CHECK_FALSE(bundle_string(app_meta, "service.string").IsValid());
        const auto again = query(present, 0x80);
        REQUIRE_FALSE(again.exception.IsValid());
        CHECK(again.value.ref != info);
        CHECK(ref_field(again.value.ref, "metaData", "Landroid/os/Bundle;") != metadata);
        CHECK_FALSE(ref_field(query(empty, 0x80).value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        const auto missing = f.linker.ResolveDescriptor(
            "Landroid/content/pm/PackageManager$NameNotFoundException;");
        CHECK(query(absent, 0).exception_class == missing);
        CHECK(query(foreign, 0).exception_class == missing);
        CHECK(query(hidden, 0).exception_class == missing);
        const auto include_disabled = query(hidden, 0x200);
        REQUIRE_FALSE(include_disabled.exception.IsValid());
        CHECK(field(include_disabled.value.ref, "enabled", "Z") == 0);
        const auto combined = query(present, 0x280);
        REQUIRE_FALSE(combined.exception.IsValid());
        CHECK(ref_field(combined.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        f.context->application_enabled = false;
        CHECK(query(present, 0).exception_class == missing);
        const auto disabled_app = query(present, 0x200);
        REQUIRE_FALSE(disabled_app.exception.IsValid());
        CHECK(field(ref_field(disabled_app.value.ref, "applicationInfo",
                              "Landroid/content/pm/ApplicationInfo;"), "enabled", "Z") == 0);
        const auto unsupported = query(present, 0x400);
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(unsupported.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        f.context->service_inventory_known = false;
        const auto unavailable = query(present, 0);
        REQUIRE(unavailable.exception.IsValid());
        CHECK(f.linker.Class(unavailable.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto &hit) {
            return hit.id == "dexvm.service_info" && hit.count == 2;
        }));
        static_cast<void>(f.vm.CollectGarbage("service-info"));
        CHECK(f.vm.StringUtf8(bundle_string(ref_field(info, "metaData",
                                                      "Landroid/os/Bundle;"),
                                                "service.string")) == "ready");
    }
}

TEST_CASE("PackageManager getReceiverInfo uses current Manifest receiver facts") {
    constexpr auto kQuery =
        "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;";
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->application_process_name = "org.example.game:app";
        f.context->receiver_inventory_known = true;
        f.context->application_meta_data.emplace("application.key", std::string("app"));
        f.context->arsc.entries.push_back(
            {.resource_id = 0x7f050001U, .type_name = "string",
             .entry_name = "receiver_value", .string_value = "resolved",
             .value_type = 0x03U});
        f.context->receiver_components = {
            {.name = "org.example.game.CoreReceiver", .enabled = true,
             .intent_filters = {{}}, .process_name = "org.example.game:push",
             .permission = "org.example.RECEIVE",
             .meta_data = {{"receiver.string", std::string("ready")},
                           {"receiver.bool", true}, {"receiver.int", std::int32_t{42}},
                           {"receiver.value", ogplay::loader::AndroidManifestMetaDataValueReference{0x7f050001U}},
                           {"receiver.resource", ogplay::loader::AndroidManifestMetaDataResourceReference{0x7f030001U}}}},
            {.name = "org.example.game.DisabledReceiver", .enabled = false,
             .process_name = "org.example.game:app"},
        };
        const auto manager = f.vm.NewIntrinsicInstance(
            "Landroid/content/pm/PackageManager;");
        const auto component = [&](const char* package, const char* name) {
            return f.New("Landroid/content/ComponentName;",
                         "(Ljava/lang/String;Ljava/lang/String;)V",
                         {VmValue::Ref(f.vm.NewStringUtf8(package)),
                          VmValue::Ref(f.vm.NewStringUtf8(name))});
        };
        const auto present = component("org.example.game", "org.example.game.CoreReceiver");
        const auto disabled = component("org.example.game", "org.example.game.DisabledReceiver");
        const auto absent = component("org.example.game", "org.example.game.Absent");
        const auto foreign = component("other.package", "org.example.game.CoreReceiver");
        const auto roots = f.vm.ProtectReferences(
            std::array{manager, present, disabled, absent, foreign});
        const auto query = [&](const VmObjectRef name, const int flags) {
            return f.OnOutcome(manager, "getReceiverInfo", kQuery,
                               {VmValue::Ref(name), VmValue::Int(flags)});
        };
        const auto ref_field = [&](const VmObjectRef object, const char* name,
                                   const char* descriptor) {
            const auto found = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, descriptor);
            REQUIRE(found.has_value());
            const auto slot = f.model.InstanceSlots(object)[f.linker.Field(*found).slot];
            return VmObjectRef{static_cast<std::uint32_t>(slot.bits)};
        };
        const auto bool_field = [&](const VmObjectRef object, const char* name) {
            const auto found = f.linker.FindFieldRecursive(
                f.model.ObjectClass(object), name, "Z");
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot].bits != 0;
        };
        const auto string_field = [&](const VmObjectRef object, const char* name) {
            const auto value = ref_field(object, name, "Ljava/lang/String;");
            REQUIRE(value.IsValid());
            return f.vm.StringUtf8(value);
        };
        const auto bundle_string = [&](const VmObjectRef bundle, const char* key) {
            return f.On(bundle, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                        {VmValue::Ref(f.vm.NewStringUtf8(key))}).ref;
        };
        const auto no_meta = query(present, 0);
        REQUIRE_FALSE(no_meta.exception.IsValid());
        REQUIRE(no_meta.value.ref.IsValid());
        CHECK(string_field(no_meta.value.ref, "name") == "org.example.game.CoreReceiver");
        CHECK(string_field(no_meta.value.ref, "processName") == "org.example.game:push");
        CHECK(string_field(no_meta.value.ref, "permission") == "org.example.RECEIVE");
        CHECK(bool_field(no_meta.value.ref, "enabled"));
        CHECK(bool_field(no_meta.value.ref, "exported"));
        CHECK_FALSE(ref_field(no_meta.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());

        const auto with_meta = query(present, 0x80);
        REQUIRE_FALSE(with_meta.exception.IsValid());
        const auto info = with_meta.value.ref;
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        const auto metadata = ref_field(info, "metaData", "Landroid/os/Bundle;");
        REQUIRE(metadata.IsValid());
        CHECK(f.vm.StringUtf8(bundle_string(metadata, "receiver.string")) == "ready");
        CHECK(f.vm.StringUtf8(bundle_string(metadata, "receiver.value")) == "resolved");
        CHECK_FALSE(bundle_string(metadata, "application.key").IsValid());
        CHECK(f.On(metadata, "getBoolean", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("receiver.bool"))}).AsInt() == 1);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("receiver.int"))}).AsInt() == 42);
        CHECK(f.On(metadata, "getInt", "(Ljava/lang/String;)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("receiver.resource"))}).AsInt() ==
              static_cast<std::int32_t>(0x7f030001U));
        const auto app = ref_field(info, "applicationInfo",
                                   "Landroid/content/pm/ApplicationInfo;");
        const auto app_meta = ref_field(app, "metaData", "Landroid/os/Bundle;");
        REQUIRE(app_meta.IsValid());
        CHECK(f.vm.StringUtf8(bundle_string(app_meta, "application.key")) == "app");
        CHECK_FALSE(bundle_string(app_meta, "receiver.string").IsValid());

        const auto again = query(present, 0x80);
        REQUIRE_FALSE(again.exception.IsValid());
        CHECK(again.value.ref != info);
        CHECK(ref_field(again.value.ref, "metaData", "Landroid/os/Bundle;") != metadata);
        CHECK(query(absent, 0).exception_class ==
              f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager$NameNotFoundException;"));
        CHECK(query(foreign, 0).exception_class ==
              f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager$NameNotFoundException;"));
        CHECK(query(disabled, 0).exception_class ==
              f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager$NameNotFoundException;"));
        const auto include_disabled = query(disabled, 0x200);
        REQUIRE_FALSE(include_disabled.exception.IsValid());
        CHECK_FALSE(bool_field(include_disabled.value.ref, "enabled"));
        f.context->application_enabled = false;
        CHECK(query(present, 0).exception_class ==
              f.linker.ResolveDescriptor("Landroid/content/pm/PackageManager$NameNotFoundException;"));
        const auto disabled_app = query(present, 0x200);
        REQUIRE_FALSE(disabled_app.exception.IsValid());
        CHECK_FALSE(bool_field(ref_field(disabled_app.value.ref, "applicationInfo",
                                         "Landroid/content/pm/ApplicationInfo;"), "enabled"));
        const auto unsupported = query(present, 0x400);
        REQUIRE(unsupported.exception.IsValid());
        CHECK(f.linker.Class(unsupported.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        f.context->receiver_inventory_known = false;
        const auto unavailable = query(present, 0);
        REQUIRE(unavailable.exception.IsValid());
        CHECK(f.linker.Class(unavailable.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [](const auto &hit) {
            return hit.id == "dexvm.receiver_info" && hit.count == 2;
        }));
        static_cast<void>(f.vm.CollectGarbage("receiver-info"));
        CHECK(f.vm.StringUtf8(bundle_string(ref_field(info, "metaData",
                                                      "Landroid/os/Bundle;"),
                                                "receiver.string")) == "ready");
    }
}

TEST_CASE("DVM-143 method lookup ignores unrelated unavailable signature types") {
    auto builder = IntrinsicClassBuilder::Class(
        "Ltest/SelectiveLookup;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    builder.VirtualMethod("wanted", "()I", [](IntrinsicContext&) {
        return VmValue::Int(7);
    });
    builder.VirtualMethod(
        "unrelated", "()Lmissing/UnavailableReturnType;",
        [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); });
    const std::vector<IntrinsicClassDecl> extras{
        std::move(builder).Build()};

    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, extras);
        const auto represented = f.model.ClassObject(
            f.linker.ResolveDescriptor("Ltest/SelectiveLookup;"));
        const auto lookup = [&](const char* operation) {
            return f.OnOutcome(
                represented, operation,
                "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                {VmValue::Ref(f.vm.NewStringUtf8("wanted")),
                 VmValue::Ref(VmObjectRef{})});
        };

        const auto exact = f.vm.Reflection().FindDeclaredMethodByDescriptor(
            f.linker.ResolveDescriptor("Ltest/SelectiveLookup;"), "wanted", "()I");
        REQUIRE(exact.has_value());
        CHECK(f.linker.Method(exact->method).descriptor == "()I");
        CHECK_FALSE(f.vm.Reflection().FindDeclaredMethodByDescriptor(
            exact->declaring_class, "wanted", "()J").has_value());
        const auto public_method = lookup("getMethod");
        REQUIRE_MESSAGE(!public_method.exception.IsValid(),
                        public_method.exception_message);
        const auto declared_method = lookup("getDeclaredMethod");
        REQUIRE_MESSAGE(!declared_method.exception.IsValid(),
                        declared_method.exception_message);
        CHECK(f.vm.StringUtf8(f.On(public_method.value.ref, "getName",
                                   "()Ljava/lang/String;").ref) == "wanted");
        CHECK(f.vm.Reflection().MethodMetadata(public_method.value.ref).method ==
              f.vm.Reflection().MethodMetadata(declared_method.value.ref).method);
    }
}

TEST_CASE("small framework Java values execute from BootDex") {
    constexpr std::array migrated{
        "Landroid/graphics/Point;",
        "Landroid/graphics/Rect;",
        "Landroid/util/AndroidException;",
        "Landroid/util/AndroidRuntimeException;",
        "Landroid/util/ArraySet;",
        "Landroid/util/Base64DataException;",
        "Landroid/util/LruCache;",
        "Landroid/util/MathUtils;",
        "Landroid/util/NoSuchPropertyException;",
        "Landroid/util/Patterns;",
        "Landroid/util/Pools$SimplePool;",
        "Landroid/util/Property;",
        "Landroid/util/TimeFormatException;",
    };
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor : migrated) {
            CAPTURE(descriptor);
            CHECK(f.linker.Class(f.linker.ResolveDescriptor(descriptor)).is_boot_dex);
        }

        const auto first = f.vm.NewStringUtf8("first");
        const auto second = f.vm.NewStringUtf8("second");
        const auto set = f.New("Landroid/util/ArraySet;");
        CHECK(f.On(set, "add", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);
        CHECK(f.On(set, "add", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 0);
        CHECK(f.On(set, "contains", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);

        const auto cache = f.New("Landroid/util/LruCache;", "(I)V",
                                 {VmValue::Int(1)});
        CHECK_FALSE(f.On(cache, "put",
                         "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                         {VmValue::Ref(first), VmValue::Ref(second)}).ref.IsValid());
        CHECK(f.On(cache, "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
                   {VmValue::Ref(first)}).ref == second);

        const auto pool = f.New("Landroid/util/Pools$SimplePool;", "(I)V",
                                {VmValue::Int(1)});
        CHECK(f.On(pool, "release", "(Ljava/lang/Object;)Z",
                   {VmValue::Ref(first)}).AsInt() == 1);
        CHECK(f.On(pool, "acquire", "()Ljava/lang/Object;").ref == first);

        const auto point = f.New("Landroid/graphics/Point;", "(II)V",
                                 {VmValue::Int(3), VmValue::Int(4)});
        f.On(point, "negate", "()V");
        CHECK(f.On(point, "equals", "(II)Z",
                   {VmValue::Int(-3), VmValue::Int(-4)}).AsInt() == 1);
        const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                {VmValue::Int(1), VmValue::Int(2),
                                 VmValue::Int(6), VmValue::Int(9)});
        CHECK(f.On(rect, "width", "()I").AsInt() == 5);
        CHECK(f.On(rect, "contains", "(II)Z",
                   {VmValue::Int(3), VmValue::Int(4)}).AsInt() == 1);
        CHECK(f.Static("Landroid/util/MathUtils;", "constrain", "(III)I",
                       {VmValue::Int(12), VmValue::Int(0), VmValue::Int(10)})
                  .AsInt() == 10);

    }
}

TEST_CASE("ClipData value objects execute from BootDex") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        for (const auto* descriptor : {
                 "Landroid/content/ClipData;",
                 "Landroid/content/ClipData$Item;",
                 "Landroid/content/ClipDescription;",
             }) {
            CAPTURE(descriptor);
            CHECK(f.linker.Class(f.linker.ResolveDescriptor(descriptor)).is_boot_dex);
        }

        const auto label = f.vm.NewStringUtf8("label");
        const auto first_text = f.vm.NewStringUtf8("first");
        const auto clip = f.Static(
            "Landroid/content/ClipData;", "newPlainText",
            "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
            "Landroid/content/ClipData;",
            {VmValue::Ref(label), VmValue::Ref(first_text)}).ref;
        CHECK(f.On(clip, "getItemCount", "()I").AsInt() == 1);
        const auto description = f.On(
            clip, "getDescription", "()Landroid/content/ClipDescription;").ref;
        CHECK(f.On(description, "getLabel", "()Ljava/lang/CharSequence;").ref ==
              label);
        CHECK(f.On(description, "getMimeTypeCount", "()I").AsInt() == 1);
        CHECK(f.vm.StringUtf8(f.On(description, "getMimeType", "(I)Ljava/lang/String;",
                                   {VmValue::Int(0)}).ref) == "text/plain");
        CHECK(f.On(description, "hasMimeType", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("text/*"))}).AsInt() == 1);

        const auto first = f.On(
            clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
            {VmValue::Int(0)}).ref;
        CHECK(f.On(first, "getText", "()Ljava/lang/CharSequence;").ref == first_text);
        CHECK_FALSE(f.On(first, "getIntent", "()Landroid/content/Intent;").ref.IsValid());
        CHECK_FALSE(f.On(first, "getUri", "()Landroid/net/Uri;").ref.IsValid());

        const auto second_text = f.vm.NewStringUtf8("second");
        const auto second = f.New(
            "Landroid/content/ClipData$Item;", "(Ljava/lang/CharSequence;)V",
            {VmValue::Ref(second_text)});
        f.On(clip, "addItem", "(Landroid/content/ClipData$Item;)V",
             {VmValue::Ref(second)});
        CHECK(f.On(clip, "getItemCount", "()I").AsInt() == 2);
        CHECK(f.On(clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
                   {VmValue::Int(1)}).ref == second);

        const auto intent = f.New("Landroid/content/Intent;");
        CHECK(f.linker.Class(f.model.ObjectClass(intent)).is_boot_dex);
        const auto package_name = f.vm.NewStringUtf8("org.example");
        CHECK(f.On(intent, "setPackage",
                   "(Ljava/lang/String;)Landroid/content/Intent;",
                   {VmValue::Ref(package_name)}).ref == intent);
        f.On(intent, "setClipData", "(Landroid/content/ClipData;)V",
             {VmValue::Ref(clip)});
        const auto bounds = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                  {VmValue::Int(1), VmValue::Int(2),
                                   VmValue::Int(6), VmValue::Int(9)});
        f.On(intent, "setSourceBounds", "(Landroid/graphics/Rect;)V",
             {VmValue::Ref(bounds)});
        const auto copy = f.New("Landroid/content/Intent;",
                                "(Landroid/content/Intent;)V",
                                {VmValue::Ref(intent)});
        CHECK(f.On(copy, "getPackage", "()Ljava/lang/String;").ref == package_name);
        CHECK(f.On(copy, "getClipData", "()Landroid/content/ClipData;").ref != clip);
        CHECK(f.On(f.On(copy, "getClipData", "()Landroid/content/ClipData;").ref,
                   "getItemCount", "()I").AsInt() == 2);
        const auto copied_bounds =
            f.On(copy, "getSourceBounds", "()Landroid/graphics/Rect;").ref;
        CHECK(copied_bounds != bounds);
        CHECK(f.On(copied_bounds, "width", "()I").AsInt() == 5);

        const auto roots = f.vm.ProtectReferences(std::array{clip, intent, copy});
        static_cast<void>(f.vm.CollectGarbage("clip-data-values"));
        CHECK(f.On(clip, "getItemAt", "(I)Landroid/content/ClipData$Item;",
                   {VmValue::Int(1)}).ref == second);
    }
}

TEST_CASE("IntentSender intrinsic shell publishes only its platform type") {
    AndroidValueVm f;
    const auto type = f.linker.ResolveDescriptor("Landroid/content/IntentSender;");
    const auto& klass = f.linker.Class(type);
    CHECK_FALSE(klass.is_boot_dex);
    CHECK(klass.super == f.linker.ResolveDescriptor("Ljava/lang/Object;"));
    CHECK(f.linker.IsAssignable(
        f.linker.ResolveDescriptor("Landroid/os/Parcelable;"), type));
    CHECK(klass.own_direct_methods.empty());
    CHECK(klass.own_virtual_methods.empty());
    CHECK(klass.own_instance_fields.empty());
}

TEST_CASE("DVM-120 Typeface Java cache and styles drive measured rendered text") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto owner = f.linker.ResolveDescriptor("Landroid/graphics/Typeface;");
        CHECK(f.linker.Class(owner).is_boot_dex);
        std::array<VmObjectRef, 4> defaults;
        for (int style = 0; style < 4; ++style) {
            defaults[style] = f.Static("Landroid/graphics/Typeface;", "defaultFromStyle",
                "(I)Landroid/graphics/Typeface;", {VmValue::Int(style)}).ref;
            CHECK(f.On(defaults[style], "getStyle", "()I").AsInt() == style);
            CHECK(f.On(defaults[style], "isBold", "()Z").AsInt() == (style & 1));
            CHECK(f.On(defaults[style], "isItalic", "()Z").AsInt() == ((style >> 1) & 1));
        }
        const auto normal = defaults[0];
        const auto create = [&](int style) {
            return f.Static("Landroid/graphics/Typeface;", "create",
                "(Landroid/graphics/Typeface;I)Landroid/graphics/Typeface;",
                {VmValue::Ref(normal), VmValue::Int(style)}).ref;
        };
        CHECK(create(0) == normal);
        const auto cached = create(3);
        CHECK(create(3) == cached);
        static_cast<void>(f.vm.CollectGarbage("dvm120-typeface-static-cache"));
        CHECK(create(3) == cached);
        f.Static("Landroid/graphics/Typeface;", "recreateDefaults", "()V");
        CHECK(create(3) != cached);
        CHECK(f.On(normal, "getStyle", "()I").AsInt() == 0);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto root = f.vm.ProtectReferences(std::array{text});
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("I"))});
        const auto node = *FindViewUiNode(*f.context, text.Value());
        f.context->ui_tree.Attach(f.context->ui_tree.Root(), node);
        std::array<std::vector<std::uint8_t>, 4> pixels;
        for (int style = 0; style < 4; ++style) {
            f.On(text, "setTypeface", "(Landroid/graphics/Typeface;I)V",
                 {VmValue::Ref(normal), VmValue::Int(style)});
            const auto face = f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref;
            CHECK(f.On(face, "getStyle", "()I").AsInt() == style);
            ui::LayoutUiTree(f.context->ui_tree, {32, 16});
            const int extra = ((style & 1) ? 1 : 0) + ((style & 2) ? 2 : 0);
            CHECK(f.context->ui_tree.Get(node)->measured.width == 5 + extra);
            pixels[style] = ui::RasterizeUiOverlay(ui::BuildUiRenderList(f.context->ui_tree, {}), {32, 16}).rgba8;
            if (style != 0) CHECK(pixels[style] != pixels[0]);
        }
        f.On(text, "setTypeface", "(Landroid/graphics/Typeface;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
        CHECK(f.context->ui_tree.Get(node)->text_style == 0);
        const auto factory = *f.linker.FindDirectMethod(owner, "createFromFile", "(Ljava/lang/String;)Landroid/graphics/Typeface;");
        const auto result = f.vm.Call(factory, std::array{VmValue::Ref(f.vm.NewStringUtf8("/font.ttf"))});
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.ledger.Unimplemented().back().id == "dexvm.typeface");
    }
}

TEST_CASE("DVM-121 text appearance resolves theme bags and preserves Java color values") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.fixture";
        f.context->application_theme = 0x01030007U;
        f.context->activity_themes["org.example.fixture.Light"] = 0x0103000cU;
        const auto activity = f.New("Landroid/app/Activity;");
        const auto activity_root = f.vm.ProtectReferences(std::array{activity});
        AttachAndroidActivityIdentity(f.vm, f.context, activity, "org.example.fixture.Light");
        CHECK(f.On(activity, "getThemeResId", "()I").AsInt() == 0x0103000c);
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto root = f.vm.ProtectReferences(std::array{text});
        f.On(text, "setTextColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
        f.On(text, "setTextSize", "(F)V", {VmValue::Float(16)});
        const auto apply = [&](std::uint32_t id) {
            return f.OnOutcome(text, "setTextAppearance", "(Landroid/content/Context;I)V",
                {VmValue::Ref(activity), VmValue::Int(static_cast<std::int32_t>(id))});
        };
        // A public attr id is metadata, not a reference to TextAppearance.Small.
        REQUIRE_FALSE(apply(0x01010042).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 16);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == 0xff123456U);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getHighlightColor", "()I").AsInt()) == 0x9983cc39U);
        const auto link = f.On(text, "getLinkTextColors", "()Landroid/content/res/ColorStateList;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(link)).is_boot_dex);
        CHECK(static_cast<std::uint32_t>(f.On(link, "getDefaultColor", "()I").AsInt()) == 0xff0000eeU);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentHintTextColor", "()I").AsInt()) == 0xff808080U);
        const auto wrapper = f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        f.On(text, "setTextAppearance", "(Landroid/content/Context;I)V", {VmValue::Ref(wrapper), VmValue::Int(0)});
        const auto wrapped_link = f.On(text, "getLinkTextColors", "()Landroid/content/res/ColorStateList;").ref;
        CHECK(static_cast<std::uint32_t>(f.On(wrapped_link, "getDefaultColor", "()I").AsInt()) == 0xff0000eeU);
        ogplay::loader::ArscEntry parent;
        parent.resource_id = 0x7f030001; parent.type_name = "style"; parent.is_complex = true;
        parent.bag = {{0x01010098, 0x1c, 0xff102030U, {}}, {0x01010097, 0x10, 1, {}}};
        auto child = parent;
        child.resource_id = 0x7f030002; child.parent = parent.resource_id;
        child.bag = {{0x01010095, 5, 0x00000e02, {}}, // 14sp
                     {0x01010098, 1, 0x7f040001, {}},
                     {0x0101009b, 2, 0x0101009b, {}}};
        ogplay::loader::ArscEntry color;
        color.resource_id = 0x7f040001; color.type_name = "color";
        color.value_type = 0x1c; color.value_data = 0xffabcdefU;
        f.context->arsc.entries = {parent, child, color};
        f.context->ui_scaled_density = 2;
        REQUIRE_FALSE(apply(child.resource_id).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 28);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == color.value_data);
        CHECK(f.context->ui_tree.Get(*FindViewUiNode(*f.context, text.Value()))->text_style == 1);
        const auto colors = f.On(text, "getTextColors", "()Landroid/content/res/ColorStateList;").ref;
        f.On(text, "getCurrentTextColor", "()I"); // retire the returned reference root
        CHECK(f.vm.MarkReachable().IsMarked(colors));
        static_cast<void>(f.vm.CollectGarbage("dvm121-text-colors"));
        CHECK(f.On(text, "getTextColors", "()Landroid/content/res/ColorStateList;").ref == colors);
        f.context->arsc.entries[0].parent = child.resource_id;
        CHECK(apply(child.resource_id).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 28);
        f.context->arsc.entries[0].parent = 0;
        f.context->arsc.entries[1].bag.push_back({0x01010161, 0x1c, 0xff000000, {}});
        CHECK(apply(child.resource_id).exception.IsValid());
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == color.value_data);
        CHECK(apply(0x7f030099).exception.IsValid());
        CHECK(f.OnOutcome(text, "setTextAppearance", "(Landroid/content/Context;I)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}).exception.IsValid());
    }
}

TEST_CASE("DVM-121 ColorStateList uses Java StateSet matching and rejects unsupported renderer state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto int_array = [&](std::initializer_list<std::int32_t> items) {
            const auto result = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                static_cast<JniSize>(items.size()));
            JniSize i{};
            for (const auto item : items) f.model.SetPrimitiveElement(result, i++, static_cast<std::uint32_t>(item));
            return result;
        };
        const auto states = f.model.NewObjectArray(f.linker.ResolveDescriptor("[[I"), f.linker.ResolveDescriptor("[I"), 2);
        const auto state_root = f.vm.ProtectReferences(std::array{states});
        f.model.SetObjectElement(states, 0, int_array({-16842910}));
        f.model.SetObjectElement(states, 1, int_array({}));
        const auto colors = int_array({static_cast<std::int32_t>(0xff808080U), static_cast<std::int32_t>(0xffffffffU)});
        const auto color_root = f.vm.ProtectReferences(std::array{colors});
        const auto list = f.New("Landroid/content/res/ColorStateList;", "([[I[I)V", {VmValue::Ref(states), VmValue::Ref(colors)});
        const auto root = f.vm.ProtectReferences(std::array{list});
        CHECK(f.On(list, "isStateful", "()Z").AsInt() == 1);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getDefaultColor", "()I").AsInt()) == 0xffffffffU);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getColorForState", "([II)I",
            {VmValue::Ref(int_array({})), VmValue::Int(0)}).AsInt()) == 0xff808080U);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getColorForState", "([II)I",
            {VmValue::Ref(int_array({16842910})), VmValue::Int(0)}).AsInt()) == 0xffffffffU);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto rejected = f.OnOutcome(text, "setTextColor", "(Landroid/content/res/ColorStateList;)V", {VmValue::Ref(list)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-124 PreferenceManager default preferences share the Context store") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto observer = IntrinsicClassBuilder::Class("Ltest/PreferenceObserver;", "Ljava/lang/Object;",
            {"Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;"});
        observer.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        observer.VirtualMethod("onSharedPreferenceChanged",
            "(Landroid/content/SharedPreferences;Ljava/lang/String;)V",
            [](IntrinsicContext&) { FAIL("unsupported listener must not be invoked"); return VmValue::Void(); });
        AndroidValueVm f(backend, {std::move(observer).Build()});
        f.context->package_name = "fixture";
        const auto base = f.New("Landroid/content/Context;");
        const auto activity = f.New("Landroid/app/Activity;");
        f.On(activity, "attachBaseContext", "(Landroid/content/Context;)V",
             {VmValue::Ref(base)});
        const auto prefs = f.Static(
            "Landroid/preference/PreferenceManager;",
            "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
            {VmValue::Ref(activity)}).ref;
        // The default entry and a direct open of <package>_preferences are
        // the same object backed by the same VFS store.
        CHECK(prefs == f.On(activity, "getSharedPreferences",
                            "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                            {VmValue::Ref(f.vm.NewStringUtf8("fixture_preferences")),
                             VmValue::Int(0)})
                           .ref);
        CHECK(f.context->preference_names.at(prefs.Value()) ==
              "fixture_preferences");

        // The PvZ dobyear/dobmonth reads answer defaults before any write.
        CHECK(f.On(prefs, "getInt", "(Ljava/lang/String;I)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("dobyear")),
                    VmValue::Int(0)})
                  .AsInt() == 0);
        const auto editor = f.On(prefs, "edit",
                                 "()Landroid/content/SharedPreferences$Editor;")
                                .ref;
        static_cast<void>(f.On(
            editor, "putInt",
            "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(f.vm.NewStringUtf8("dobyear")), VmValue::Int(2010)}));
        CHECK(f.On(editor, "commit", "()Z").AsInt() == 1);
        CHECK(f.On(prefs, "getInt", "(Ljava/lang/String;I)I",
                   {VmValue::Ref(f.vm.NewStringUtf8("dobyear")),
                    VmValue::Int(0)})
                  .AsInt() == 2010);
        CHECK(f.On(prefs, "contains", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("dobyear"))})
                  .AsInt() == 1);

        // The complete BootDex interface: getAll boxes the same store.
        const auto all = f.On(prefs, "getAll", "()Ljava/util/Map;").ref;
        CHECK(f.On(all, "size", "()I").AsInt() == 1);
        const auto boxed = f.On(all, "get",
                                 "(Ljava/lang/Object;)Ljava/lang/Object;",
                                 {VmValue::Ref(f.vm.NewStringUtf8("dobyear"))})
                               .ref;
        CHECK(f.On(boxed, "intValue", "()I").AsInt() == 2010);

        // Pending removal is not visible until apply/commit.
        static_cast<void>(f.On(
            editor, "remove",
            "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(f.vm.NewStringUtf8("dobyear"))}));
        CHECK(f.On(prefs, "contains", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("dobyear"))})
                  .AsInt() == 1);
        static_cast<void>(f.On(
            editor, "putLong",
            "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(f.vm.NewStringUtf8("session")),
             VmValue::Long(77)}));
        f.On(editor, "apply", "()V");
        CHECK(f.On(prefs, "contains", "(Ljava/lang/String;)Z",
                   {VmValue::Ref(f.vm.NewStringUtf8("dobyear"))}).AsInt() == 0);
        CHECK(f.On(prefs, "getLong", "(Ljava/lang/String;J)J",
                   {VmValue::Ref(f.vm.NewStringUtf8("session")),
                    VmValue::Long(0)})
                  .AsLong() == 77);
        static_cast<void>(f.On(
            editor, "clear",
            "()Landroid/content/SharedPreferences$Editor;"));
        f.On(editor, "commit", "()Z");
        CHECK(f.On(prefs, "getAll", "()Ljava/util/Map;").ref.IsValid());
        CHECK(f.On(f.On(prefs, "getAll", "()Ljava/util/Map;").ref,
                   "size", "()I")
                  .AsInt() == 0);

        // Interface surface without checked storage or callback truth fails
        // explicitly and is recorded.
        const auto string_set = f.OnOutcome(
            prefs, "getStringSet", "(Ljava/lang/String;Ljava/util/Set;)Ljava/util/Set;",
            {VmValue::Ref(f.vm.NewStringUtf8("dobyear")),
             VmValue::Ref(VmObjectRef{})});
        REQUIRE(string_set.exception.IsValid());
        CHECK(f.linker.Class(string_set.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto put_set = f.OnOutcome(
            editor, "putStringSet",
            "(Ljava/lang/String;Ljava/util/Set;)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(f.vm.NewStringUtf8("tags")),
             VmValue::Ref(VmObjectRef{})});
        REQUIRE(put_set.exception.IsValid());
        const auto listener = f.New("Ltest/PreferenceObserver;");
        const auto registered = f.OnOutcome(
            prefs, "registerOnSharedPreferenceChangeListener",
            "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V",
            {VmValue::Ref(listener)});
        REQUIRE(registered.exception.IsValid());
        CHECK(f.linker.Class(registered.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto unregistered = f.OnOutcome(prefs, "unregisterOnSharedPreferenceChangeListener",
            "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V",
            {VmValue::Ref(listener)});
        REQUIRE(unregistered.exception.IsValid());
        CHECK(f.linker.Class(unregistered.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 2);
        for (const auto& hit : hits) {
            CHECK((hit.id == "dexvm.shared_preferences.string_set" ||
                   hit.id == "dexvm.shared_preferences.change_listeners"));
            CHECK(hit.count == 2);
        }

        // A null Context surfaces the real NullPointerException from the
        // original PreferenceManager code path.
        const auto manager_class =
            f.linker.ResolveDescriptor("Landroid/preference/PreferenceManager;");
        const auto method = f.linker.FindDirectMethod(
            manager_class, "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        REQUIRE(method.has_value());
        const auto null_context =
            f.vm.Call(*method, std::vector<VmValue>{VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("DVM-124 PreferenceManager honors Context overrides and delegation") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto named = IntrinsicClassBuilder::Class("Ltest/NamedContext;",
                                                  "Landroid/content/Context;");
        named.Constructor("()V",
                          [](IntrinsicContext&) { return VmValue::Void(); });
        named.OverrideMethod(
            "getPackageName", "()Ljava/lang/String;", [](IntrinsicContext& c) {
                return VmValue::Ref(c.vm.NewStringUtf8("virtual"));
            });
        VmObjectRef expected_prefs;
        bool forwarded = false;
        auto overridden = IntrinsicClassBuilder::Class("Ltest/PreferenceContext;",
                                                       "Landroid/content/Context;");
        overridden.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        overridden.OverrideMethod("getSharedPreferences",
            "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
            [&](IntrinsicContext& c) {
                CHECK(c.vm.StringUtf8(c.arguments[0].ref) == "fixture_preferences");
                CHECK(c.arguments[1].AsInt() == 0);
                forwarded = true;
                return VmValue::Ref(expected_prefs);
            });
        std::vector<IntrinsicClassDecl> extras;
        extras.push_back(std::move(named).Build());
        extras.push_back(std::move(overridden).Build());
        AndroidValueVm f(backend, extras);
        f.context->package_name = "fixture";
        const auto named_context = f.New("Ltest/NamedContext;");
        // The virtual getPackageName override picks the default file name.
        const auto prefs = f.Static(
            "Landroid/preference/PreferenceManager;",
            "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
            {VmValue::Ref(named_context)}).ref;
        CHECK(f.context->preference_names.at(prefs.Value()) ==
              "virtual_preferences");

        // ContextWrapper delegation reaches the same base Context store.
        const auto base = f.New("Landroid/content/Context;");
        const auto wrapper = f.New("Landroid/content/ContextWrapper;",
                                   "(Landroid/content/Context;)V",
                                   {VmValue::Ref(base)});
        const auto delegated = f.Static(
            "Landroid/preference/PreferenceManager;",
            "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
            {VmValue::Ref(wrapper)}).ref;
        CHECK(f.context->preference_names.at(delegated.Value()) ==
              "fixture_preferences");
        CHECK(f.context->singletons.at("prefs:fixture_preferences") == delegated);
        expected_prefs = delegated;
        const auto custom = f.New("Ltest/PreferenceContext;");
        CHECK(f.Static("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
            {VmValue::Ref(custom)}).ref == expected_prefs);
        CHECK(forwarded);

        const auto application = f.New("Landroid/app/Application;");
        f.On(application, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        CHECK(f.Static("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;",
            {VmValue::Ref(application)}).ref == delegated);
    }
}

TEST_CASE("DVM-121 SharedPreferences editors isolate pending changes and clear before puts") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto prefs = f.Static("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;", {VmValue::Ref(base)}).ref;
        const auto roots = f.vm.ProtectReferences(std::array{prefs});
        const auto edit = [&] { return f.On(prefs, "edit", "()Landroid/content/SharedPreferences$Editor;").ref; };
        const auto first = edit();
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        const auto second = edit();
        const auto second_root = f.vm.ProtectReferences(std::array{second});
        CHECK(first != second);
        const auto key = f.vm.NewStringUtf8("value");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        const auto put = [&](VmObjectRef editor, int value) {
            return f.On(editor, "putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
                        {VmValue::Ref(key), VmValue::Int(value)}).ref;
        };
        const auto get = [&] { return f.On(prefs, "getInt", "(Ljava/lang/String;I)I",
            {VmValue::Ref(key), VmValue::Int(-1)}).AsInt(); };
        CHECK(put(first, 1) == first);
        CHECK(get() == -1);
        f.On(second, "commit", "()Z");
        CHECK(get() == -1); // another editor cannot publish first's pending value
        f.On(first, "commit", "()Z");
        CHECK(get() == 1);
        put(first, 2);
        put(second, 3);
        f.On(first, "commit", "()Z");
        CHECK(get() == 2);
        f.On(second, "commit", "()Z");
        CHECK(get() == 3);
        f.On(first, "commit", "()Z");
        CHECK(get() == 3); // committed edits are drained, not replayed
        put(first, 4);
        f.On(first, "clear", "()Landroid/content/SharedPreferences$Editor;");
        CHECK(get() == 3);
        f.On(first, "apply", "()V");
        CHECK(get() == 4); // clear runs before pending puts, regardless of call order
        f.On(first, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
             {VmValue::Ref(key), VmValue::Ref(VmObjectRef{})});
        CHECK(get() == 4);
        f.On(first, "commit", "()Z");
        CHECK(get() == -1); // null String is removal in API 19
    }
}

TEST_CASE("DVM-121 SharedPreferences complete interfaces float snapshots and editor GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto prefs = f.Static("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;", {VmValue::Ref(base)}).ref;
        const auto prefs_root = f.vm.ProtectReferences(std::array{prefs});
        const auto editor = f.On(prefs, "edit", "()Landroid/content/SharedPreferences$Editor;").ref;
        const auto editor_root = f.vm.ProtectReferences(std::array{editor});
        for (const auto& [descriptor, object] : std::array{
                 std::pair{"Landroid/content/SharedPreferences;", prefs},
                 std::pair{"Landroid/content/SharedPreferences$Editor;", editor}}) {
            const auto type = f.linker.ResolveDescriptor(descriptor);
            REQUIRE(f.linker.Class(type).is_boot_dex);
            for (const auto id : f.linker.Class(type).own_virtual_methods) {
                const auto& method = f.linker.Method(id);
                CHECK((method.access_flags & kAccAbstract) != 0);
                const auto concrete = f.model.ObjectClass(object);
                const auto slot = f.linker.FindVtableIndex(concrete, method.name, method.descriptor);
                REQUIRE(slot.has_value());
                CHECK(f.linker.Method(f.linker.Class(concrete).vtable[*slot]).kind == MethodKind::intrinsic);
            }
        }
        const auto key = f.vm.NewStringUtf8("fraction");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        CHECK(f.On(prefs, "getFloat", "(Ljava/lang/String;F)F",
                   {VmValue::Ref(key), VmValue::Float(2.5F)}).AsFloat() == 2.5F);
        f.On(editor, "putFloat", "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;",
             {VmValue::Ref(key), VmValue::Float(1.25F)});
        f.On(editor, "commit", "()Z");
        const auto all = f.On(prefs, "getAll", "()Ljava/util/Map;").ref;
        const auto all_root = f.vm.ProtectReferences(std::array{all});
        static_cast<void>(f.vm.CollectGarbage("preferences snapshot"));
        const auto boxed = f.On(all, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(key)}).ref;
        CHECK(f.On(boxed, "floatValue", "()F").AsFloat() == 1.25F);
        f.On(all, "clear", "()V");
        CHECK(f.On(prefs, "getFloat", "(Ljava/lang/String;F)F",
                   {VmValue::Ref(key), VmValue::Float(0)}).AsFloat() == 1.25F);
        const auto mismatch = f.OnOutcome(prefs, "getInt", "(Ljava/lang/String;I)I",
            {VmValue::Ref(key), VmValue::Int(0)});
        REQUIRE(mismatch.exception.IsValid());
        CHECK(f.linker.Class(mismatch.exception_class).descriptor == "Ljava/lang/ClassCastException;");
        const auto abandoned = f.On(prefs, "edit", "()Landroid/content/SharedPreferences$Editor;").ref;
        f.On(abandoned, "putFloat", "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;",
             {VmValue::Ref(key), VmValue::Float(9)});
        f.On(prefs, "contains", "(Ljava/lang/String;)Z", {VmValue::Ref(key)});
        static_cast<void>(f.vm.CollectGarbage("abandoned preference editor"));
        CHECK_FALSE(f.context->preference_editors.contains(abandoned.Value()));
        CHECK_FALSE(f.context->preference_names.contains(abandoned.Value()));
        CHECK(f.context->preference_editors.contains(editor.Value()));
        CHECK(f.On(prefs, "getFloat", "(Ljava/lang/String;F)F",
                   {VmValue::Ref(key), VmValue::Float(0)}).AsFloat() == 1.25F);
    }
}


TEST_CASE("DVM-144 Parcel exception headers preserve Java protocol and reject violations") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto p = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{p});
        f.On(p, "writeNoException", "()V");
        CHECK(f.On(p, "dataPosition", "()I").AsInt() == 4);
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(p, "readException", "()V");
        for (const auto type : {"Ljava/lang/SecurityException;", "Ljava/lang/IllegalArgumentException;",
                                "Ljava/lang/NullPointerException;", "Ljava/lang/IllegalStateException;",
                                "Landroid/os/BadParcelableException;"}) {
            f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
            const auto error = f.New(type, "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("wire error"))});
            f.On(p, "writeException", "(Ljava/lang/Exception;)V", {VmValue::Ref(error)});
            f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
            const auto result = f.OnOutcome(p, "readException", "()V");
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == type);
            CHECK(f.vm.StringUtf8(f.On(result.exception, "getMessage", "()Ljava/lang/String;").ref) == "wire error");
        }
        f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
        f.On(p, "writeInt", "(I)V", {VmValue::Int(-128)});
        f.On(p, "writeInt", "(I)V", {VmValue::Int(4)});
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto violation = f.OnOutcome(p, "readException", "()V");
        REQUIRE(violation.exception.IsValid());
        CHECK(f.linker.Class(violation.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-144 Parcel interface policy and malformed lengths are bounded") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto p = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto descriptor = f.vm.NewStringUtf8("echo");
        const auto roots = f.vm.ProtectReferences(std::array{p, descriptor});
        f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(7)});
        f.On(p, "writeInterfaceToken", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
        CHECK(f.On(p, "dataSize", "()I").AsInt() == 20);
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(p, "readInt", "()I").AsInt() == 0x107);
        f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(0)});
        f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(p, "enforceInterface", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
        CHECK(f.Static("Landroid/os/Binder;", "getThreadStrictModePolicy", "()I").AsInt() == 0x107);
        for (const auto length : {-1, 0x7fffffff, 20}) {
            f.On(p, "setDataSize", "(I)V", {VmValue::Int(0)});
            f.On(p, "writeInt", "(I)V", {VmValue::Int(0)});
            f.On(p, "writeInt", "(I)V", {VmValue::Int(length)});
            f.On(p, "setDataPosition", "(I)V", {VmValue::Int(0)});
            const auto result = f.OnOutcome(p, "enforceInterface", "(Ljava/lang/String;)V", {VmValue::Ref(descriptor)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/SecurityException;");
        }
    }
}

TEST_CASE("DVM-144 failed service binding retains a Context owned dispatcher until unbind") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int callbacks = 0;
        auto declaration = IntrinsicClassBuilder::Class("Ltest/AbsentConnection;", "Ljava/lang/Object;",
                                                        {"Landroid/content/ServiceConnection;"});
        declaration.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        const auto callback = [&callbacks](IntrinsicContext&) { ++callbacks; return VmValue::Void(); };
        declaration.VirtualMethod("onServiceConnected", "(Landroid/content/ComponentName;Landroid/os/IBinder;)V", callback);
        declaration.VirtualMethod("onServiceDisconnected", "(Landroid/content/ComponentName;)V", callback);
        AndroidValueVm f(backend, {std::move(declaration).Build()});
        const auto base = f.New("Landroid/content/Context;");
        const auto wrapper = f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto connection = f.New("Ltest/AbsentConnection;");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("example.ABSENT"))});
        const auto roots = f.vm.ProtectReferences(std::array{base, wrapper, intent});
        f.context->service_inventory_known = true;
        for (int i = 0; i < 2; ++i)
            CHECK(f.On(wrapper, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
                       {VmValue::Ref(intent), VmValue::Ref(connection), VmValue::Int(1)}).AsInt() == 0);
        CHECK(f.context->service_connections.at(base.Value()).size() == 1);
        CHECK(callbacks == 0);
        static_cast<void>(f.vm.CollectGarbage("absent-bind"));
        CHECK(f.vm.MarkReachable().IsMarked(connection));
        f.On(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
        CHECK(f.context->service_connections.empty());
        const auto twice = f.OnOutcome(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
        REQUIRE(twice.exception.IsValid());
        CHECK(f.linker.Class(twice.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        f.context->service_components = {{"example.Local", true, {{{"example.ABSENT"}, {}, false}}}};
        const auto local = f.OnOutcome(wrapper, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
                                      {VmValue::Ref(intent), VmValue::Ref(connection), VmValue::Int(1)});
        REQUIRE(local.exception.IsValid());
        CHECK(f.linker.Class(local.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.On(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
    }
}


TEST_CASE("DVM-144 Binder Parcel references survive append and release on overwrite") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto source = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto target = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{source, target});
        const auto binder = f.New("Landroid/os/Binder;");
        f.On(source, "writeStrongBinder", "(Landroid/os/IBinder;)V", {VmValue::Ref(binder)});
        CHECK(f.On(source, "dataSize", "()I").AsInt() == 16);
        const auto partial = f.OnOutcome(target, "appendFrom", "(Landroid/os/Parcel;II)V",
                                        {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(4)});
        REQUIRE(partial.exception.IsValid());
        f.On(target, "appendFrom", "(Landroid/os/Parcel;II)V",
             {VmValue::Ref(source), VmValue::Int(0), VmValue::Int(16)});
        f.On(source, "recycle", "()V");
        static_cast<void>(f.vm.CollectGarbage("binder-append"));
        CHECK(f.vm.MarkReachable().IsMarked(binder));
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(target, "readStrongBinder", "()Landroid/os/IBinder;").ref == binder);
        CHECK(f.On(binder, "pingBinder", "()Z").AsInt() == 1);
        REQUIRE(f.OnOutcome(target, "marshall", "()[B").exception.IsValid());
        f.On(target, "setDataPosition", "(I)V", {VmValue::Int(0)});
        f.On(target, "writeInt", "(I)V", {VmValue::Int(0)});
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(binder));
        f.On(target, "recycle", "()V");
    }
}

TEST_CASE("DVM-144 Binder identity and policy are isolated by execution context") {
    AndroidValueVm f;
    const auto worker = f.vm.CreateExecutionContext();
    const auto type = f.linker.ResolveDescriptor("Landroid/os/Binder;");
    const auto set = f.linker.FindDirectMethod(type, "setThreadStrictModePolicy", "(I)V");
    const auto get = f.linker.FindDirectMethod(type, "getThreadStrictModePolicy", "()I");
    REQUIRE(set);
    REQUIRE(get);
    f.Static("Landroid/os/Binder;", "setThreadStrictModePolicy", "(I)V", {VmValue::Int(7)});
    CHECK(f.vm.Call(worker, *get, {}).value.AsInt() == 0);
    const std::array args{VmValue::Int(9)};
    REQUIRE_FALSE(f.vm.Call(worker, *set, args).exception.IsValid());
    CHECK(f.Static("Landroid/os/Binder;", "getThreadStrictModePolicy", "()I").AsInt() == 7);
    CHECK(f.vm.Call(worker, *get, {}).value.AsInt() == 9);
    const auto identity = f.Static("Landroid/os/Binder;", "clearCallingIdentity", "()J").AsLong();
    f.Static("Landroid/os/Binder;", "restoreCallingIdentity", "(J)V", {VmValue::Long(identity)});
    CHECK(f.Static("Landroid/os/Binder;", "clearCallingIdentity", "()J").AsLong() == identity);
    REQUIRE(f.StaticOutcome("Landroid/os/Binder;", "joinThreadPool", "()V").exception.IsValid());
    f.vm.DiscardExecutionContext(worker);
}


TEST_CASE("DVM-151 BootDex builders own UTF16 fields capacity and immutable snapshots") {
    for (const bool bridge : {false, true})
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, bridge);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            CAPTURE(owner);
            const auto b = f.New(owner);
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto invoke = [&](const char* name, const std::string& sig, std::vector<VmValue> args = {}) {
                return f.On(b, name, sig.c_str(), args);
            };
            const auto text = [&] { return f.model.StringValue(invoke("toString", "()Ljava/lang/String;").ref); };
            CHECK(invoke("capacity", "()I").AsInt() == 16);
            const auto append = std::string("(Ljava/lang/String;)") + owner;
            const auto literal = f.vm.NewStringUtf8("/getLatest/unknown/0/terms-1.9-137%2Ctermsapi-1.2/com.popcap.pvz_na/Android/false/0/ageSensitive");
            CHECK(invoke("append", append, {VmValue::Ref(literal)}).ref == b);
            CHECK(f.vm.StringUtf8(invoke("substring", "(II)Ljava/lang/String;", {VmValue::Int(0), VmValue::Int(3)}).ref) == "/ge");
            CHECK(f.vm.StringUtf8(invoke("substring", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == f.vm.StringUtf8(literal));
            CHECK(f.vm.StringUtf8(invoke("subSequence", "(II)Ljava/lang/CharSequence;", {VmValue::Int(0), VmValue::Int(3)}).ref) == "/ge");
            invoke("setLength", "(I)V", {VmValue::Int(0)});
            invoke("trimToSize", "()V");
            CHECK(invoke("capacity", "()I").AsInt() == 0);
            invoke("ensureCapacity", "(I)V", {VmValue::Int(16)});
            invoke("append", append, {VmValue::Ref(f.vm.NewStringUtf8("abcdefghijklmnop"))});
            const auto snapshot = invoke("toString", "()Ljava/lang/String;").ref;
            const auto snapshot_root = f.vm.ProtectReferences(std::array{snapshot});
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(b), "value", "[C");
            REQUIRE(field.has_value());
            const auto backing = VmObjectRef(f.model.InstanceSlots(b)[f.linker.Field(*field).slot].bits);
            CHECK(f.vm.MarkReachable().IsMarked(backing));
            invoke("setCharAt", "(IC)V", {VmValue::Int(0), VmValue::Int('Z')});
            CHECK(f.model.StringValue(snapshot) == u"abcdefghijklmnop");
            const auto detached = VmObjectRef(f.model.InstanceSlots(b)[f.linker.Field(*field).slot].bits);
            CHECK(detached != backing);
            static_cast<void>(f.vm.CollectGarbage());
            CHECK(text() == u"Zbcdefghijklmnop");
            invoke("append", std::string("(C)") + owner, {VmValue::Int('q')});
            CHECK(invoke("capacity", "()I").AsInt() == 26); // API 19 append growth: 1.5x + 2
            invoke("ensureCapacity", "(I)V", {VmValue::Int(27)});
            CHECK(invoke("capacity", "()I").AsInt() == 54); // ensureCapacity: 2x + 2
            invoke("setLength", "(I)V", {VmValue::Int(1)});
            invoke("setLength", "(I)V", {VmValue::Int(3)});
            CHECK(text() == std::u16string({u'Z', 0, 0}));
            invoke("setLength", "(I)V", {VmValue::Int(0)});
            invoke("append", append, {VmValue::Ref(f.model.NewString(u"A😀B"))});
            CHECK(invoke("length", "()I").AsInt() == 4);
            CHECK(invoke("charAt", "(I)C", {VmValue::Int(1)}).AsInt() == 0xd83d);
            CHECK(invoke("codePointAt", "(I)I", {VmValue::Int(1)}).AsInt() == 0x1f600);
            CHECK(invoke("codePointBefore", "(I)I", {VmValue::Int(3)}).AsInt() == 0x1f600);
            CHECK(invoke("codePointCount", "(II)I", {VmValue::Int(0), VmValue::Int(4)}).AsInt() == 3);
            CHECK(invoke("offsetByCodePoints", "(II)I", {VmValue::Int(0), VmValue::Int(2)}).AsInt() == 3);
            invoke("reverse", std::string("()") + owner);
            CHECK(text() == u"B😀A");
            invoke("insert", std::string("(ILjava/lang/String;)") + owner, {VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("xy"))});
            invoke("delete", std::string("(II)") + owner, {VmValue::Int(1), VmValue::Int(3)});
            invoke("replace", std::string("(IILjava/lang/String;)") + owner, {VmValue::Int(0), VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("C"))});
            CHECK(text() == u"C😀A");
            invoke("appendCodePoint", std::string("(I)") + owner, {VmValue::Int(0x1f601)});
            CHECK(text() == u"C😀A😁");
            const auto unchanged = text();
            const auto bad = f.OnOutcome(b, "substring", "(II)Ljava/lang/String;", {VmValue::Int(-1), VmValue::Int(2)});
            REQUIRE(bad.exception.IsValid());
            CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/StringIndexOutOfBoundsException;");
            CHECK(text() == unchanged);
        }
    }
}

TEST_CASE("DVM-151 builder integer and floating appends execute original conversion classes") {
    for (const bool bridge : {false, true})
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, bridge);
        VmThreadRuntime threads(f.vm);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto b = f.New(owner);
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto check = [&](const char* type, VmValue value, const char* expected) {
                f.On(b, "setLength", "(I)V", {VmValue::Int(0)});
                const auto sig = std::string("(") + type + ")" + owner;
                CHECK(f.On(b, "append", sig.c_str(), {value}).ref == b);
                CHECK(f.vm.StringUtf8(f.On(b, "toString", "()Ljava/lang/String;").ref) == expected);
            };
            check("I", VmValue::Int(INT32_MIN), "-2147483648");
            check("I", VmValue::Int(INT32_MAX), "2147483647");
            check("J", VmValue::Long(INT64_MIN), "-9223372036854775808");
            check("J", VmValue::Long(INT64_MAX), "9223372036854775807");
            check("I", VmValue::Int(0), "0");
            check("F", VmValue::Float(1.5f), "1.5");
            check("F", VmValue::Float(std::bit_cast<float>(std::uint32_t{1})), "1.4E-45");
            check("F", VmValue::Float(std::bit_cast<float>(std::uint32_t{0x7f7fffff})), "3.4028235E38");
            check("D", VmValue::Double(-0.0), "-0.0");
            check("D", VmValue::Double(1e100), "1.0E100");
            check("D", VmValue::Double(-1e100), "-1.0E100");
            check("D", VmValue::Double(1.234123412431233E107), "1.234123412431233E107");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7fefffffffffffff})), "1.7976931348623157E308");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x0010000000000000})), "2.2250738585072014E-308");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{2})), "1.0E-323");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{1})), "4.9E-324");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7ff0000000000000})), "Infinity");
            check("D", VmValue::Double(std::bit_cast<double>(std::uint64_t{0x7ff8000000000000})), "NaN");
        }
    }
}


TEST_CASE("DVM-151 builders retain constructor null array and bridge contracts") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        VmThreadRuntime threads(f.vm);
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            const auto b = f.New(owner, "(I)V", {VmValue::Int(0)});
            const auto root = f.vm.ProtectReferences(std::array{b});
            const auto text = [&] { return f.model.StringValue(f.On(b, "toString", "()Ljava/lang/String;").ref); };
            const auto expect_exception = [&](const VmCallOutcome& result, const char* exception) {
                REQUIRE(result.exception.IsValid());
                CHECK(f.linker.Class(result.exception_class).descriptor == exception);
            };
            const auto raw = f.vm.NewIntrinsicInstance(owner);
            expect_exception(f.StaticOutcome(owner, "<init>", "(I)V", {VmValue::Ref(raw), VmValue::Int(-1)}), "Ljava/lang/NegativeArraySizeException;");
            for (auto ctor : {"(Ljava/lang/String;)V", "(Ljava/lang/CharSequence;)V"}) {
                const auto object = f.vm.NewIntrinsicInstance(owner);
                expect_exception(f.StaticOutcome(owner, "<init>", ctor, {VmValue::Ref(object), VmValue::Ref(VmObjectRef{})}), "Ljava/lang/NullPointerException;");
            }
            CHECK(f.On(b, "append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;", {VmValue::Ref(VmObjectRef{})}).ref == b);
            CHECK(text() == u"null");
            const auto chars = f.On(f.model.NewString(u"x😀y"), "toCharArray", "()[C").ref;
            const auto chars_root = f.vm.ProtectReferences(std::array{chars});
            const auto append_array = std::string("([CII)") + owner;
            f.On(b, "append", append_array.c_str(), {VmValue::Ref(chars), VmValue::Int(1), VmValue::Int(2)});
            CHECK(text() == u"null😀");
            const auto before = text();
            expect_exception(f.OnOutcome(b, "append", append_array.c_str(), {VmValue::Ref(chars), VmValue::Int(3), VmValue::Int(2)}), "Ljava/lang/ArrayIndexOutOfBoundsException;");
            CHECK(text() == before);
            f.On(b, "getChars", "(II[CI)V", {VmValue::Int(4), VmValue::Int(6), VmValue::Ref(chars), VmValue::Int(0)});
            CHECK(f.model.GetPrimitiveElement(chars, 0) == 0xd83d);
            CHECK(f.model.GetPrimitiveElement(chars, 1) == 0xde00);
            CHECK(f.On(b, "indexOf", "(Ljava/lang/String;)I", {VmValue::Ref(f.vm.NewStringUtf8("ll"))}).AsInt() == 2);
            CHECK(f.On(b, "lastIndexOf", "(Ljava/lang/String;)I", {VmValue::Ref(f.vm.NewStringUtf8("l"))}).AsInt() == 3);
            const auto copied = f.New(owner, "(Ljava/lang/CharSequence;)V", {VmValue::Ref(b)});
            CHECK(f.model.StringValue(f.On(copied, "toString", "()Ljava/lang/String;").ref) == before);
            f.On(b, "deleteCharAt", (std::string("(I)") + owner).c_str(), {VmValue::Int(4)});
            CHECK(f.On(b, "charAt", "(I)C", {VmValue::Int(4)}).AsInt() == 0xde00); // code unit, not a code point
            expect_exception(f.OnOutcome(b, "setLength", "(I)V", {VmValue::Int(-1)}), "Ljava/lang/StringIndexOutOfBoundsException;");
            expect_exception(f.OnOutcome(b, "appendCodePoint", (std::string("(I)") + owner).c_str(), {VmValue::Int(0x110000)}), "Ljava/lang/IllegalArgumentException;");
        }
    }
}

TEST_CASE("DVM-184 View.getContext preserves constructor and inflation identity") {
    constexpr auto kGetContext = "getContext";
    constexpr auto kContextSig = "()Landroid/content/Context;";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kTwoArg =
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    constexpr auto kStyleInit =
        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/BurstlyView;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            kViewInit, [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/widget/LinearLayout;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", kViewInit);
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto subclass_type =
            fixture.linker.ResolveDescriptor("Ltest/BurstlyView;");
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, kGetContext, kContextSig);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            subclass_type, kGetContext, kContextSig);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(
            fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == view_type);
        CHECK_FALSE(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) != 0);

        bool found_context_field = false;
        for (const auto field :
             fixture.linker.Class(view_type).own_instance_fields) {
            const auto& linked = fixture.linker.Field(field);
            if (linked.name != "mContext") continue;
            found_context_field = true;
            CHECK(linked.descriptor == "Landroid/content/Context;");
            CHECK((linked.access_flags & kAccProtected) != 0);
        }
        CHECK(found_context_field);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto wrapper =
            fixture.New("Landroid/view/ContextThemeWrapper;");
        const auto other = fixture.New("Landroid/content/Context;");
        REQUIRE(activity != wrapper);
        REQUIRE(activity != other);

        const auto view = fixture.New(
            "Landroid/view/View;", kViewInit, {VmValue::Ref(activity)});
        const auto layout = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(wrapper), VmValue::Ref(VmObjectRef{})});
        const auto button = fixture.New(
            "Landroid/widget/Button;", kStyleInit,
            {VmValue::Ref(other), VmValue::Ref(VmObjectRef{}),
             VmValue::Int(0)});
        const auto burstly = fixture.New(
            "Ltest/BurstlyView;", kViewInit, {VmValue::Ref(activity)});
        CHECK(fixture.On(view, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(layout, kGetContext, kContextSig).ref == wrapper);
        CHECK(fixture.On(button, kGetContext, kContextSig).ref == other);
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(view, kGetContext, kContextSig).ref !=
              fixture.On(layout, kGetContext, kContextSig).ref);

        std::vector<ogplay::loader::BinaryXmlElement> elements(1);
        elements[0].name = "LinearLayout";
        const auto inflated = InflateUiElements(
            fixture.vm, *fixture.context, elements, activity);
        CHECK(fixture.On(inflated, kGetContext, kContextSig).ref == activity);

        const auto roots = fixture.vm.ProtectReferences(std::array{burstly});
        static_cast<void>(fixture.vm.CollectGarbage("dvm184-view-context"));
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
    }
}

TEST_CASE("View scroll bar style is inherited and preserves unrelated flags") {
    constexpr auto kSetStyle = "setScrollBarStyle";
    constexpr auto kSetStyleSig = "(I)V";
    constexpr auto kGetStyle = "getScrollBarStyle";
    constexpr auto kGetStyleSig = "()I";
    constexpr std::uint32_t kStyleMask = 0x03000000U;
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto webview_type =
            fixture.linker.ResolveDescriptor("Landroid/webkit/WebView;");
        const auto setter = fixture.linker.FindVtableIndex(
            webview_type, kSetStyle, kSetStyleSig);
        const auto getter = fixture.linker.FindVtableIndex(
            webview_type, kGetStyle, kGetStyleSig);
        REQUIRE(setter.has_value());
        REQUIRE(getter.has_value());
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(webview_type).vtable[*setter]).owner ==
              view_type);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(webview_type).vtable[*setter])
                  .overridable);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto first = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto second = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() == 0);
        CHECK(fixture.On(second, kGetStyle, kGetStyleSig).AsInt() == 0);

        const auto flags_id = fixture.linker.FindFieldRecursive(
            view_type, "mViewFlags", "I");
        REQUIRE(flags_id.has_value());
        auto& flags = fixture.model.InstanceSlots(first)
                          [fixture.linker.Field(*flags_id).slot];
        flags.bits = 0x40000040U;
        flags.tag = SlotTag::cat1;
        for (const auto style :
             {0x00000000, 0x01000000, 0x02000000, 0x03000000}) {
            fixture.On(first, kSetStyle, kSetStyleSig,
                       {VmValue::Int(style)});
            CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() ==
                  style);
            CHECK((flags.bits & ~kStyleMask) == 0x40000040U);
            CHECK(fixture.On(second, kGetStyle, kGetStyleSig).AsInt() == 0);
        }
        fixture.On(first, kSetStyle, kSetStyleSig,
                   {VmValue::Int(0x7fffffff)});
        CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() ==
              0x03000000);
        CHECK((flags.bits & ~kStyleMask) == 0x40000040U);
    }
}

TEST_CASE("InputMethodManager no-session branches follow UI and window focus and reject active connections") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto base = fixture.vm.NewIntrinsicInstance(
            "Landroid/content/Context;");
        const auto manager = fixture.On(
            base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("input_method"))}).ref;
        REQUIRE(manager.IsValid());
        CHECK(fixture.On(
            base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("input_method"))}).ref == manager);
        const auto view = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)});
        const auto unrelated = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)});
        const auto node = FindViewUiNode(*fixture.context, view.Value());
        REQUIRE(node.has_value());
        fixture.On(view, "setFocusable", "(Z)V", {VmValue::Int(1)});
        const auto expect_no_session = [&] {
            REQUIRE_FALSE(fixture.OnOutcome(
                manager, "restartInput", "(Landroid/view/View;)V",
                {VmValue::Ref(view)}).exception.IsValid());
            REQUIRE_FALSE(fixture.OnOutcome(
                manager, "restartInput", "(Landroid/view/View;)V",
                {VmValue::Ref(VmObjectRef{})}).exception.IsValid());
            CHECK(fixture.On(manager, "showSoftInput", "(Landroid/view/View;I)Z",
                             {VmValue::Ref(view), VmValue::Int(0)}).AsInt() == 0);
            CHECK(fixture.On(manager, "hideSoftInputFromWindow",
                             "(Landroid/os/IBinder;I)Z",
                             {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}).AsInt() == 0);
        };
        // Initial onResume, before the window obtains input focus.
        expect_no_session();
        expect_no_session();
        fixture.context->activity = fixture.vm.NewIntrinsicInstance(
            "Landroid/app/Activity;");
        fixture.context->window_focus_activity.store(fixture.context->activity.Value());
        fixture.context->window_has_focus.store(true);
        // A window alone does not create an input connection.
        expect_no_session();
        fixture.context->ui_tree.Attach(fixture.context->ui_tree.Root(), *node);
        expect_no_session();
        REQUIRE(fixture.On(view, "requestFocus", "()Z").AsInt() == 1);
        const auto expect_gap = [&](const VmCallOutcome& outcome) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
                  "Ljava/lang/UnsupportedOperationException;");
            CHECK(outcome.exception_message ==
                  "InputConnection creation for the focused View is unsupported");
        };
        expect_gap(fixture.OnOutcome(manager, "restartInput", "(Landroid/view/View;)V",
                                     {VmValue::Ref(view)}));
        // checkFocus can start input before the caller/proxy/token guard.
        expect_gap(fixture.OnOutcome(manager, "restartInput", "(Landroid/view/View;)V",
                                     {VmValue::Ref(unrelated)}));
        expect_gap(fixture.OnOutcome(manager, "showSoftInput", "(Landroid/view/View;I)Z",
                                     {VmValue::Ref(unrelated), VmValue::Int(0)}));
        expect_gap(fixture.OnOutcome(manager, "hideSoftInputFromWindow",
                                     "(Landroid/os/IBinder;I)Z",
                                     {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}));
        const auto hits = fixture.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.input_method_sessions");
        CHECK(hits[0].count == 4);
        CHECK(fixture.context->ui_tree.Focused() == node);
        // Window focus and ownership use lifecycle facts, not a cached IMM bit.
        fixture.context->window_has_focus.store(false);
        expect_no_session();
        fixture.context->window_has_focus.store(true);
        fixture.context->window_focus_activity.store(unrelated.Value());
        expect_no_session();
        fixture.context->window_focus_activity.store(fixture.context->activity.Value());
        fixture.On(view, "clearFocus", "()V");
        expect_no_session();
        REQUIRE(fixture.On(view, "requestFocus", "()Z").AsInt() == 1);
        fixture.context->ui_tree.Detach(*node);
        expect_no_session();
        fixture.context->ui_tree.Reset();
        expect_no_session();
        CHECK(fixture.ledger.Unimplemented()[0].count == 4);
    }
}

TEST_CASE("View focus and WebView configuration keep real per-instance state") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::vector<std::int32_t> focus_events;
        auto listener_class = IntrinsicClassBuilder::Class(
            "Ltest/FocusListener;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnFocusChangeListener;"});
        listener_class.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        listener_class.VirtualMethod(
            "onFocusChange", "(Landroid/view/View;Z)V",
            [&focus_events](IntrinsicContext& call) {
                focus_events.push_back(call.arguments[1].AsInt());
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(listener_class).Build()});
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto parent = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto first = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto second = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto listener = fixture.New("Ltest/FocusListener;");
        fixture.On(first, "setOnFocusChangeListener",
                   "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(second, "setOnFocusChangeListener",
                   "(Landroid/view/View$OnFocusChangeListener;)V",
                   {VmValue::Ref(listener)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(first)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(second)});

        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z",
                         {VmValue::Int(130)}).AsInt() == 0);
        CHECK(fixture.On(first, "hasFocus", "()Z").AsInt() == 0);
        fixture.On(first, "setVisibility", "(I)V", {VmValue::Int(0)});
        CHECK(fixture.On(first, "requestFocus", "(I)Z",
                         {VmValue::Int(130)}).AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 1);
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isFocused", "()Z").AsInt() == 0);
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 1);
        fixture.On(second, "clearFocus", "()V");
        CHECK(fixture.On(parent, "hasFocus", "()Z").AsInt() == 0);
        CHECK(focus_events == std::vector<std::int32_t>{1, 0, 1, 0});
        CHECK(fixture.On(second, "requestFocus", "()Z").AsInt() == 1);
        fixture.On(parent, "removeView", "(Landroid/view/View;)V",
                   {VmValue::Ref(second)});
        CHECK(fixture.On(second, "isFocused", "()Z").AsInt() == 0);
        CHECK(focus_events ==
              std::vector<std::int32_t>{1, 0, 1, 0, 1, 0});

        fixture.On(first, "setScrollContainer", "(Z)V", {VmValue::Int(1)});
        fixture.On(first, "setHorizontalScrollBarEnabled", "(Z)V",
                   {VmValue::Int(0)});
        fixture.On(first, "setVerticalScrollBarEnabled", "(Z)V",
                   {VmValue::Int(0)});
        CHECK(fixture.On(first, "isScrollContainer", "()Z").AsInt() == 1);
        CHECK(fixture.On(first, "isHorizontalScrollBarEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(first, "isVerticalScrollBarEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(second, "isHorizontalScrollBarEnabled", "()Z").AsInt() == 1);

        const auto settings = fixture.On(
            first, "getSettings", "()Landroid/webkit/WebSettings;").ref;
        CHECK(fixture.On(settings, "getJavaScriptEnabled", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "supportZoom", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "getAllowFileAccess", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "getCacheMode", "()I").AsInt() == -1);
        fixture.On(settings, "setJavaScriptEnabled", "(Z)V", {VmValue::Int(1)});
        fixture.On(settings, "setSupportZoom", "(Z)V", {VmValue::Int(0)});
        fixture.On(settings, "setAllowFileAccess", "(Z)V", {VmValue::Int(0)});
        fixture.On(settings, "setCacheMode", "(I)V", {VmValue::Int(2)});
        CHECK(fixture.On(settings, "getJavaScriptEnabled", "()Z").AsInt() == 1);
        CHECK(fixture.On(settings, "supportZoom", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "getAllowFileAccess", "()Z").AsInt() == 0);
        CHECK(fixture.On(settings, "getCacheMode", "()I").AsInt() == 2);
        const auto high_name = fixture.vm.NewStringUtf8("HIGH");
        const auto high = fixture.Static(
            "Landroid/webkit/WebSettings$RenderPriority;", "valueOf",
            "(Ljava/lang/String;)Landroid/webkit/WebSettings$RenderPriority;",
            {VmValue::Ref(high_name)}).ref;
        fixture.On(settings, "setRenderPriority",
                   "(Landroid/webkit/WebSettings$RenderPriority;)V",
                   {VmValue::Ref(high)});

        const auto client = fixture.New("Landroid/webkit/WebViewClient;");
        fixture.On(first, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V",
                   {VmValue::Ref(client)});
        const auto js_object = fixture.New("Ljava/lang/Object;");
        const auto js_name = fixture.vm.NewStringUtf8("bridge");
        fixture.On(first, "addJavascriptInterface",
                   "(Ljava/lang/Object;Ljava/lang/String;)V",
                   {VmValue::Ref(js_object), VmValue::Ref(js_name)});
        const auto roots = fixture.vm.ProtectReferences(std::array{first});
        static_cast<void>(fixture.vm.CollectGarbage("webview-config-roots"));
        CHECK(fixture.model.IsValidRef(client));
        CHECK(fixture.model.IsValidRef(js_object));

        CHECK(fixture.On(client, "shouldOverrideUrlLoading",
                         "(Landroid/webkit/WebView;Ljava/lang/String;)Z",
                         {VmValue::Ref(first), VmValue::Ref(js_name)}).AsInt() == 0);
        fixture.On(client, "onPageFinished",
                   "(Landroid/webkit/WebView;Ljava/lang/String;)V",
                   {VmValue::Ref(first), VmValue::Ref(js_name)});
    }
}

TEST_CASE("DVM-185 WebView.destroy isolates settings and enforces thread rules") {
    constexpr auto kDestroy = "destroy";
    constexpr auto kVoid = "()V";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kGetSettings = "()Landroid/webkit/WebSettings;";
    constexpr auto kLoadUrl = "(Ljava/lang/String;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::atomic<std::int32_t> worker_runs{};
        std::string worker_exception;
        VmObjectRef worker_view;
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/OwnedWebView;", "Landroid/webkit/WebView;");
        subclass.Constructor(
            kViewInit, [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/webkit/WebView;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", kViewInit);
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto destroyer = IntrinsicClassBuilder::Class(
            "Ltest/DestroyWebView;", "Ljava/lang/Object;",
            {"Ljava/lang/Runnable;"});
        destroyer.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        destroyer.VirtualMethod(
            "run", "()V",
            [&worker_runs, &worker_exception, &worker_view](
                IntrinsicContext& call) {
                ++worker_runs;
                const auto klass = call.vm.Model().ObjectClass(worker_view);
                const auto index = call.vm.Linker().FindVtableIndex(
                    klass, kDestroy, kVoid);
                const std::array arguments{VmValue::Ref(worker_view)};
                const auto outcome = call.vm.Call(
                    call.vm.Linker().Class(klass).vtable[*index], arguments);
                if (outcome.exception.IsValid()) {
                    worker_exception =
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor;
                } else {
                    worker_exception.clear();
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(subclass).Build(), std::move(destroyer).Build()});
        VmThreadRuntime threads(fixture.vm);
        fixture.context->threads = &threads;
        RegisterAndroidSchedulerStateTable(fixture.vm, fixture.context);

        const auto web_type =
            fixture.linker.ResolveDescriptor("Landroid/webkit/WebView;");
        const auto subclass_type =
            fixture.linker.ResolveDescriptor("Ltest/OwnedWebView;");
        const auto web_slot =
            fixture.linker.FindVtableIndex(web_type, kDestroy, kVoid);
        const auto subclass_slot =
            fixture.linker.FindVtableIndex(subclass_type, kDestroy, kVoid);
        REQUIRE(web_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*web_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(
            fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == web_type);
        CHECK(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) == 0);

        fixture.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        const auto owner = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Ltest/OwnedWebView;", kViewInit, {VmValue::Ref(owner)});
        const auto second = fixture.New(
            "Landroid/webkit/WebView;", kViewInit, {VmValue::Ref(owner)});
        const auto first_settings =
            fixture.On(first, "getSettings", kGetSettings).ref;
        const auto first_again =
            fixture.On(first, "getSettings", kGetSettings).ref;
        const auto second_settings =
            fixture.On(second, "getSettings", kGetSettings).ref;
        CHECK(first_settings.IsValid());
        CHECK(first_settings == first_again);
        CHECK(second_settings.IsValid());
        CHECK(first_settings != second_settings);

        const auto url = fixture.vm.NewStringUtf8("https://example.invalid");
        fixture.context->strict_webview_errors = true;
        auto load = fixture.OnOutcome(first, "loadUrl", kLoadUrl,
                                      {VmValue::Ref(url)});
        REQUIRE(load.exception.IsValid());
        CHECK(fixture.linker.Class(load.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");

        fixture.On(first, kDestroy, kVoid);
        fixture.On(first, kDestroy, kVoid);
        auto after = fixture.OnOutcome(first, "getSettings", kGetSettings);
        REQUIRE(after.exception.IsValid());
        CHECK(fixture.linker.Class(after.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        load = fixture.OnOutcome(first, "loadUrl", kLoadUrl,
                                 {VmValue::Ref(url)});
        REQUIRE(load.exception.IsValid());
        CHECK(fixture.linker.Class(load.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref ==
              second_settings);

        const auto wait_for = [](const auto& predicate) {
            for (int attempt = 0; attempt < 2000; ++attempt) {
                if (predicate()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };
        const auto post_destroy = [&](VmObjectRef view) {
            worker_runs = 0;
            worker_exception.clear();
            worker_view = view;
            const auto thread = fixture.New("Landroid/os/HandlerThread;");
            const auto name = fixture.vm.NewStringUtf8("webview-worker");
            const auto init = fixture.linker.FindDirectMethod(
                fixture.linker.ResolveDescriptor(
                    "Landroid/os/HandlerThread;"),
                "<init>", "(Ljava/lang/String;)V");
            REQUIRE(init.has_value());
            const std::array init_args{
                VmValue::Ref(thread), VmValue::Ref(name)};
            auto outcome = fixture.vm.Call(*init, init_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                            outcome.exception_message);
            fixture.On(thread, "start", "()V");
            const auto looper =
                fixture.On(thread, "getLooper", "()Landroid/os/Looper;").ref;
            const auto handler = fixture.vm.NewIntrinsicInstance(
                "Landroid/os/Handler;");
            const auto handler_init = fixture.linker.FindDirectMethod(
                fixture.linker.ResolveDescriptor("Landroid/os/Handler;"),
                "<init>", "(Landroid/os/Looper;)V");
            REQUIRE(handler_init.has_value());
            const std::array handler_args{
                VmValue::Ref(handler), VmValue::Ref(looper)};
            outcome = fixture.vm.Call(*handler_init, handler_args);
            REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                            outcome.exception_message);
            const auto runnable = fixture.New("Ltest/DestroyWebView;");
            fixture.On(handler, "post", "(Ljava/lang/Runnable;)Z",
                       {VmValue::Ref(runnable)});
            REQUIRE(wait_for([&] { return worker_runs.load() == 1; }));
            fixture.On(thread, "quit", "()Z");
            fixture.On(thread, "join", "()V");
        };

        fixture.context->target_sdk_version = 19;
        post_destroy(second);
        CHECK(worker_exception == "Ljava/lang/RuntimeException;");
        CHECK(fixture.On(second, "getSettings", kGetSettings).ref ==
              second_settings);

        fixture.context->target_sdk_version = 13;
        post_destroy(second);
        CHECK(worker_exception.empty());
        after = fixture.OnOutcome(second, "getSettings", kGetSettings);
        REQUIRE(after.exception.IsValid());
        CHECK(fixture.linker.Class(after.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");

        ShutdownAndroidScheduler(*fixture.context);
    }
}

TEST_CASE("disabled WebView policy reports asynchronous failure and supports cancellation") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t error_count{};
        std::int32_t last_error{};
        std::string last_url;
        auto client_class = IntrinsicClassBuilder::Class(
            "Ltest/DisabledWebClient;", "Landroid/webkit/WebViewClient;");
        client_class.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        client_class.OverrideMethod(
            "onReceivedError",
            "(Landroid/webkit/WebView;ILjava/lang/String;Ljava/lang/String;)V",
            [&error_count, &last_error, &last_url](IntrinsicContext& call) {
                ++error_count;
                last_error = call.arguments[1].AsInt();
                last_url = call.vm.StringUtf8(call.arguments[3].ref);
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(client_class).Build()});
        VmThreadRuntime threads(fixture.vm);
        fixture.context->threads = &threads;
        RegisterAndroidSchedulerStateTable(fixture.vm, fixture.context);
        fixture.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto parent = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto view = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto client = fixture.New("Ltest/DisabledWebClient;");
        fixture.On(view, "setWebViewClient",
                   "(Landroid/webkit/WebViewClient;)V",
                   {VmValue::Ref(client)});
        fixture.On(parent, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(view)});
        fixture.context->ui_tree.Attach(
            fixture.context->ui_tree.Root(),
            EnsureViewUiNode(*fixture.context, parent,
                             ogplay::runtime::ui::UiClass::View));

        const auto first = fixture.vm.NewStringUtf8(
            "https://cloud.example/path/page?token=secret#fragment");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(first)});
        CHECK(error_count == 0);
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 1);
        CHECK(last_error == -10);
        CHECK(last_url ==
              "https://cloud.example/path/page?token=secret#fragment");

        const auto base = fixture.vm.NewStringUtf8("https://data.example/base?x=1");
        const auto html = fixture.vm.NewStringUtf8("<p>offline</p>");
        fixture.On(
            view, "loadDataWithBaseURL",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(base), VmValue::Ref(html), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);
        CHECK(last_error == -10);
        CHECK(last_url == "https://data.example/base?x=1");

        const auto second = fixture.vm.NewStringUtf8("https://example/stop");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(second)});
        fixture.On(view, "stopLoading", "()V");
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);

        const auto script = fixture.vm.NewStringUtf8("javascript:callback(1)");
        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(script)});
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);
        CHECK(fixture.On(view, "canGoBack", "()Z").AsInt() == 0);
        fixture.On(view, "goBack", "()V");

        fixture.On(view, "loadUrl", "(Ljava/lang/String;)V",
                   {VmValue::Ref(second)});
        fixture.On(view, "destroy", "()V");
        CHECK_FALSE(PumpJavaThreads(fixture.vm, *fixture.context).has_value());
        CHECK(error_count == 2);

        const auto records = fixture.logger.Snapshot(
            ogplay::core::LogLevel::warn, "runtime.web.disabled");
        REQUIRE(records.size() == 3);
        CHECK(records[0].fields.size() == 4);
        CHECK(std::get<std::string>(records[0].fields[1].value) ==
              "https://cloud.example/path/page?token=secret#fragment");
        CHECK(std::get<std::string>(records[0].fields[2].value) ==
              "cloud.example");
        CHECK(std::get<std::string>(records[0].fields[3].value) ==
              "/path/page");
        CHECK(std::get<std::string>(records[1].fields[0].value) ==
              "load_data");
        CHECK(std::get<std::string>(records[2].fields[0].value) ==
              "javascript");

        const auto strict_view = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        fixture.context->strict_webview_errors = true;
        const auto strict = fixture.OnOutcome(
            strict_view, "loadUrl", "(Ljava/lang/String;)V",
            {VmValue::Ref(second)});
        REQUIRE(strict.exception.IsValid());
        CHECK(fixture.linker.Class(strict.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        ShutdownAndroidScheduler(*fixture.context);
    }
}

TEST_CASE("disabled web policy intercepts only external HTTP ACTION_VIEW") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto context = fixture.New("Landroid/content/Context;");
        const auto action = fixture.vm.NewStringUtf8(
            "android.intent.action.VIEW");
        const auto text = fixture.vm.NewStringUtf8(
            "https://outside.example/open/item?auth=secret");
        const auto uri = fixture.Static(
            "Landroid/net/Uri;", "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;",
            {VmValue::Ref(text)}).ref;
        const auto intent = fixture.New(
            "Landroid/content/Intent;",
            "(Ljava/lang/String;Landroid/net/Uri;)V",
            {VmValue::Ref(action), VmValue::Ref(uri)});
        fixture.On(context, "startActivity",
                   "(Landroid/content/Intent;)V",
                   {VmValue::Ref(intent)});
        CHECK_FALSE(fixture.context->activity_switch_pending);
        CHECK(fixture.context->pending_activity_descriptor.empty());
        const auto records = fixture.logger.Snapshot(
            ogplay::core::LogLevel::warn, "runtime.web.disabled");
        REQUIRE(records.size() == 1);
        CHECK(std::get<std::string>(records[0].fields[0].value) ==
              "external_browser");
        CHECK(std::get<std::string>(records[0].fields[1].value) ==
              "https://outside.example/open/item?auth=secret");
        CHECK(std::get<std::string>(records[0].fields[2].value) ==
              "outside.example");
        CHECK(std::get<std::string>(records[0].fields[3].value) ==
              "/open/item");

        fixture.On(
            intent, "setClassName",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("external.browser")),
             VmValue::Ref(fixture.vm.NewStringUtf8("BrowserActivity"))});
        fixture.On(context, "startActivity",
                   "(Landroid/content/Intent;)V",
                   {VmValue::Ref(intent)});
        CHECK_FALSE(fixture.context->activity_switch_pending);
        CHECK(fixture.logger.Snapshot(
                  ogplay::core::LogLevel::warn,
                  "runtime.web.disabled").size() == 1);

        fixture.context->strict_webview_errors = true;
        const auto strict = fixture.OnOutcome(
            context, "startActivity", "(Landroid/content/Intent;)V",
            {VmValue::Ref(intent)});
        REQUIRE(strict.exception.IsValid());
        CHECK(fixture.linker.Class(strict.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-193 receiver queries match independent filters and return BootDex results") {
    using ogplay::loader::AndroidManifestIntentFilter;
    using ogplay::loader::AndroidManifestReceiverComponent;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->receiver_inventory_known = true;
        const AndroidManifestIntentFilter plain{
            .actions = {"RECEIVE"}, .categories = {"org.example.game", "EXTRA"},
            .priority = 5, .label = std::string("Receiver label"), .icon = 0x7f030001U};
        auto default_filter = plain;
        default_filter.categories.push_back("android.intent.category.DEFAULT");
        default_filter.label = std::uint32_t{0x7f050001U};
        auto high = plain;
        high.priority = 10;
        auto negative = plain;
        negative.priority = -7;
        auto disabled = AndroidManifestReceiverComponent{
            .name = "org.example.game.Disabled", .enabled = false,
            .intent_filters = {high}};
        f.context->receiver_components = {
            {.name = "org.example.game.First", .intent_filters = {plain, high},
             .process_name = "org.example.game:receiver", .permission = "org.example.PROTECTED"},
            {.name = "org.example.game.Tie", .intent_filters = {plain}},
            {.name = "org.example.game.Default", .exported = false,
             .intent_filters = {default_filter}},
            {.name = "org.example.game.High", .intent_filters = {high}},
            {.name = "org.example.game.Negative", .intent_filters = {negative}},
            {.name = "org.example.game.Split", .intent_filters = {
                {.actions = {"RECEIVE"}, .categories = {"org.example.game"}},
                {.actions = {"RECEIVE"}, .categories = {"EXTRA"}},
                {.actions = {"OTHER"}, .categories = {"org.example.game", "EXTRA"}}}},
            disabled};
        f.context->receiver_components.back().intent_filters.front().has_data = true;
        const auto manager = f.vm.NewIntrinsicInstance("Landroid/content/pm/PackageManager;");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                 {VmValue::Ref(f.vm.NewStringUtf8("RECEIVE"))});
        const auto roots = f.vm.ProtectReferences(std::array{manager, intent});
        const auto add_category = [&](const char* category) {
            f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                 {VmValue::Ref(f.vm.NewStringUtf8(category))});
        };
        add_category("org.example.game");
        add_category("EXTRA");
        const auto query = [&](VmObjectRef value, int flags = 0) {
            return f.OnOutcome(manager, "queryBroadcastReceivers",
                               "(Landroid/content/Intent;I)Ljava/util/List;",
                               {VmValue::Ref(value), VmValue::Int(flags)});
        };
        const auto size = [&](VmObjectRef list) { return f.On(list, "size", "()I").AsInt(); };
        const auto field = [&](VmObjectRef object, const char* name, const char* type) {
            const auto found = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot].bits;
        };
        const auto ref_field = [&](VmObjectRef object, const char* name, const char* type) {
            return VmObjectRef{static_cast<std::uint32_t>(field(object, name, type))};
        };
        const auto result = query(intent);
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        const auto list = result.value.ref;
        const auto list_root = f.vm.ProtectReferences(std::array{list});
        CHECK(f.linker.Class(f.model.ObjectClass(list)).descriptor == "Ljava/util/ArrayList;");
        REQUIRE(size(list) == 5);
        const auto item = [&](int index) {
            return f.On(list, "get", "(I)Ljava/lang/Object;", {VmValue::Int(index)}).ref;
        };
        const std::array names{"High", "Default", "First", "Tie", "Negative"};
        for (int i = 0; i < 5; ++i) {
            const auto resolve = item(i);
            CHECK(f.linker.Class(f.model.ObjectClass(resolve)).descriptor == "Landroid/content/pm/ResolveInfo;");
            const auto info = ref_field(resolve, "activityInfo", "Landroid/content/pm/ActivityInfo;");
            CHECK(f.linker.Class(f.model.ObjectClass(info)).descriptor == "Landroid/content/pm/ActivityInfo;");
            CHECK(f.vm.StringUtf8(ref_field(info, "name", "Ljava/lang/String;")) ==
                  std::string("org.example.game.") + names[static_cast<std::size_t>(i)]);
            CHECK(field(resolve, "match", "I") == 0x108000U);
            CHECK(field(resolve, "preferredOrder", "I") == 0U);
            CHECK(field(resolve, "system", "Z") == 0U);
            CHECK(static_cast<std::int32_t>(field(resolve, "specificIndex", "I")) == -1);
            CHECK_FALSE(ref_field(resolve, "filter", "Landroid/content/IntentFilter;").IsValid());
            CHECK_FALSE(ref_field(info, "metaData", "Landroid/os/Bundle;").IsValid());
            CHECK(ref_field(info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;").IsValid());
        }
        CHECK(field(item(2), "priority", "I") == 5U); // first matching filter, not max priority
        CHECK(field(item(1), "isDefault", "Z") == 1U);
        CHECK(field(item(2), "isDefault", "Z") == 0U); // broadcasts need no DEFAULT
        CHECK(field(item(1), "labelRes", "I") == 0x7f050001U);
        CHECK(field(item(2), "icon", "I") == 0x7f030001U);
        CHECK(f.vm.StringUtf8(ref_field(item(2), "nonLocalizedLabel", "Ljava/lang/CharSequence;")) == "Receiver label");
        const auto first_info = ref_field(item(2), "activityInfo", "Landroid/content/pm/ActivityInfo;");
        CHECK(f.vm.StringUtf8(ref_field(first_info, "permission", "Ljava/lang/String;")) == "org.example.PROTECTED");
        CHECK(f.vm.StringUtf8(ref_field(first_info, "processName", "Ljava/lang/String;")) == "org.example.game:receiver");
        CHECK(field(first_info, "exported", "Z") == 1U);
        CHECK(field(ref_field(item(1), "activityInfo", "Landroid/content/pm/ActivityInfo;"), "exported", "Z") == 0U);
        static_cast<void>(f.vm.CollectGarbage("receiver-query-results"));
        CHECK(size(list) == 5);
        CHECK(field(item(2), "priority", "I") == 5U);
        f.context->application_enabled = false;
        CHECK(size(query(intent).value.ref) == 0);
        f.context->application_enabled = true;
        add_category("MISSING");
        CHECK(size(query(intent).value.ref) == 0);
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V",
             {VmValue::Ref(f.vm.NewStringUtf8("MISSING"))});
        f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("ABSENT"))});
        CHECK(size(query(intent).value.ref) == 0);
        f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("RECEIVE"))});
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V",
             {VmValue::Ref(f.vm.NewStringUtf8("EXTRA"))});
        CHECK(size(query(intent).value.ref) == 6); // Split now matches its first filter

        int unsupported_count = 0;
        const auto unsupported = [&](const VmCallOutcome& outcome) {
            CHECK(outcome.exception_class == f.linker.ResolveDescriptor("Ljava/lang/UnsupportedOperationException;"));
            ++unsupported_count;
        };
        for (const int flags : {0x80, 0x200, 0x10000, -1}) unsupported(query(intent, flags));
        f.context->receiver_inventory_known = false;
        unsupported(query(intent));
        f.context->receiver_inventory_known = true;
        const auto empty_intent = f.New("Landroid/content/Intent;");
        const auto empty_root = f.vm.ProtectReferences(std::array{empty_intent});
        unsupported(query(empty_intent));
        f.On(empty_intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8(""))});
        unsupported(query(empty_intent));
        CHECK(query(VmObjectRef{}).exception_class == f.linker.ResolveDescriptor("Ljava/lang/NullPointerException;"));
        for (const auto& [setter, signature, value] :
             {std::tuple{"setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", f.vm.NewStringUtf8("other.package")},
              std::tuple{"setType", "(Ljava/lang/String;)Landroid/content/Intent;", f.vm.NewStringUtf8("text/plain")},
              std::tuple{"setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                         f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("content://example/item"))}).ref},
              std::tuple{"setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
                         f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                               {VmValue::Ref(f.vm.NewStringUtf8("org.example.game")),
                                VmValue::Ref(f.vm.NewStringUtf8("org.example.game.First"))})}}) {
            f.On(intent, setter, signature, {VmValue::Ref(value)});
            unsupported(query(intent));
            f.On(intent, setter, signature, {VmValue::Ref(VmObjectRef{})});
        }
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(empty_intent)});
        unsupported(query(intent));
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("org.example.game"))});
        CHECK(size(query(intent).value.ref) == 6);
        // An unresolved candidate must not be reported as an empty/partial list.
        f.context->receiver_components.back().enabled = true;
        f.context->receiver_components.back().intent_filters.front().has_data = true;
        unsupported(query(intent));
        f.context->receiver_components.back().intent_filters.front().actions = {"UNRELATED"};
        CHECK(size(query(intent).value.ref) == 6);
        f.context->receiver_components.clear();
        CHECK(size(query(intent).value.ref) == 0);
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(), hits.end(), [&](const auto& hit) {
            return hit.id == "dexvm.receiver_query" && hit.count == static_cast<std::uint64_t>(unsupported_count);
        }));
    }
}

TEST_CASE("DVM-195 getActivityInfo returns only the requested activity metadata") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "fixture";
        f.context->activity_inventory_known = true;
        f.context->application_meta_data.emplace("android.app.lib_name", std::string("application"));
        ogplay::loader::AndroidManifestActivityComponent component;
        component.name = "fixture.Native";
        component.meta_data.push_back({"android.app.lib_name", std::string("activity")});
        f.context->activity_components.push_back(component);
        const auto manager = f.vm.NewIntrinsicInstance("Landroid/content/pm/PackageManager;");
        const auto name = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.Native"))});
        const auto root = f.vm.ProtectReferences(std::array{manager, name});
        const auto query = [&](int flags) {
            return f.OnOutcome(manager, "getActivityInfo", "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;",
                {VmValue::Ref(name), VmValue::Int(flags)});
        };
        const auto field = [&](VmObjectRef object, const char* name, const char* type) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(id.has_value());
            return VmObjectRef(static_cast<std::uint32_t>(f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits));
        };
        const auto plain = query(0);
        REQUIRE_FALSE(plain.exception.IsValid());
        CHECK_FALSE(field(plain.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        const auto full = query(0x80);
        REQUIRE_FALSE(full.exception.IsValid());
        const auto metadata = field(full.value.ref, "metaData", "Landroid/os/Bundle;");
        const auto library = f.On(metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(f.vm.NewStringUtf8("android.app.lib_name"))}).ref;
        CHECK(f.vm.StringUtf8(library) == "activity");
        const auto app = field(full.value.ref, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
        CHECK(f.vm.StringUtf8(field(app, "nativeLibraryDir", "Ljava/lang/String;")) == "/data/app-lib");
        CHECK(query(1).exception.IsValid());
        f.context->activity_components.front().enabled = false;
        CHECK(query(0).exception.IsValid());
        CHECK_FALSE(query(0x200).exception.IsValid());
        f.context->activity_inventory_known = false;
        CHECK(query(0).exception.IsValid());
    }
}

TEST_CASE("InputDevice queries use BootDex values and the process input directory") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        constexpr auto device_class = "Landroid/view/InputDevice;";
        const auto ids = [&] { return fixture.Static(device_class, "getDeviceIds", "()[I").ref; };
        const auto device = [&](int id) {
            return fixture.Static(device_class, "getDevice", "(I)Landroid/view/InputDevice;", {VmValue::Int(id)}).ref;
        };
        CHECK(fixture.model.ArrayLength(ids()) == 0);
        CHECK_FALSE(device(404).IsValid());
        fixture.context->input_devices = {
            {-1, "keyboard", "fixture:keyboard", kAndroidKeyboardSource, 2, {}},
            {0, "touch", "fixture:touch", kAndroidTouchSource, 0,
             {{0, kAndroidTouchSource, 0, 799}, {1, kAndroidTouchSource, 0, 479},
              {2, kAndroidTouchSource, 0, 1}}}};
        const auto list = ids();
        REQUIRE(fixture.model.ArrayLength(list) == 2);
        CHECK(static_cast<std::int32_t>(fixture.model.GetPrimitiveElement(list, 0)) == -1);
        CHECK(fixture.model.GetPrimitiveElement(list, 1) == 0);
        fixture.model.SetPrimitiveElement(list, 1, 999);
        CHECK(fixture.model.GetPrimitiveElement(ids(), 1) == 0);
        const auto touch = device(0);
        REQUIRE(touch.IsValid());
        const auto roots = fixture.vm.ProtectReferences(std::array{touch});
        CHECK_FALSE(fixture.linker.Class(fixture.model.ObjectClass(touch)).is_intrinsic);
        CHECK(fixture.On(touch, "getId", "()I").AsInt() == 0);
        CHECK(fixture.vm.StringUtf8(fixture.On(touch, "getDescriptor", "()Ljava/lang/String;").ref) == "fixture:touch");
        const auto sources = fixture.On(touch, "getSources", "()I").AsInt();
        CHECK(sources == kAndroidTouchSource);
        CHECK((sources & 0x100008) != 0x100008); // no touchpad advertised
        const auto range = fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(0)}).ref;
        REQUIRE(range.IsValid());
        CHECK(fixture.On(range, "getRange", "()F").AsFloat() == 799.0f);
        CHECK_FALSE(fixture.On(touch, "getMotionRange", "(II)Landroid/view/InputDevice$MotionRange;",
            {VmValue::Int(0), VmValue::Int(0x100008)}).ref.IsValid());
        CHECK_FALSE(fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(9)}).ref.IsValid());
        const auto ranges = fixture.On(touch, "getMotionRanges", "()Ljava/util/List;").ref;
        CHECK(fixture.On(ranges, "size", "()I").AsInt() == 3);
        const auto pressure = fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(2)}).ref;
        REQUIRE(pressure.IsValid());
        CHECK(fixture.On(pressure, "getMin", "()F").AsFloat() == 0);
        CHECK(fixture.On(pressure, "getMax", "()F").AsFloat() == 1);
        const auto keyboard = device(-1);
        CHECK(fixture.On(keyboard, "getKeyboardType", "()I").AsInt() == 2);
        const auto unsupported = fixture.OnOutcome(keyboard, "hasKeys", "([I)[Z", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(unsupported.exception.IsValid());
        CHECK(fixture.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto event = MakeMotionEvent(fixture.vm, 0, 10, 20, 0);
        CHECK(fixture.On(event, "getDeviceId", "()I").AsInt() == fixture.On(touch, "getId", "()I").AsInt());
        CHECK(fixture.On(event, "getSource", "()I").AsInt() == sources);
    }
}

TEST_CASE("DVM-202 explicit local service bindings share instances and cancel queued delivery") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    int created=0, bound=0, connected=0, destroyed=0, unbound=0;
    VmObjectRef delivered;
    bool null_binder = false, fail_bind = false;
    auto service = IntrinsicClassBuilder::Class("Lexample/Local;", "Landroid/app/Service;");
    service.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    service.OverrideMethod("onCreate", "()V", [&](IntrinsicContext&) { ++created; return VmValue::Void(); });
    service.OverrideMethod("onBind", "(Landroid/content/Intent;)Landroid/os/IBinder;", [&](IntrinsicContext& c) {
      ++bound;
      if (fail_bind) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "bind failure"};
      if (null_binder) return VmValue::Ref(VmObjectRef{});
      const auto binder = c.vm.NewIntrinsicInstance("Landroid/os/Binder;");
      const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(binder), "<init>", "()V");
      REQUIRE(ctor.has_value());
      REQUIRE_FALSE(c.vm.Call(*ctor, std::array{VmValue::Ref(binder)}).exception.IsValid());
      return VmValue::Ref(binder);
    });
    service.OverrideMethod("onUnbind", "(Landroid/content/Intent;)Z", [&](IntrinsicContext&) { ++unbound; return VmValue::Int(1); });
    service.OverrideMethod("onDestroy", "()V", [&](IntrinsicContext&) { ++destroyed; return VmValue::Void(); });
    auto connection_type = IntrinsicClassBuilder::Class("Lexample/Connection;", "Ljava/lang/Object;", {"Landroid/content/ServiceConnection;"});
    connection_type.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    connection_type.VirtualMethod("onServiceConnected", "(Landroid/content/ComponentName;Landroid/os/IBinder;)V", [&](IntrinsicContext& c) {
      ++connected;
      if (delivered.IsValid()) CHECK(delivered == c.arguments[1].ref);
      delivered = c.arguments[1].ref;
      return VmValue::Void();
    });
    connection_type.VirtualMethod("onServiceDisconnected", "(Landroid/content/ComponentName;)V", [](IntrinsicContext&) { FAIL("normal unbind is not disconnection"); return VmValue::Void(); });
    AndroidValueVm f(backend, {std::move(service).Build(), std::move(connection_type).Build()});
    VmThreadRuntime threads(f.vm);
    f.context->threads = &threads;
    f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) { VisitAndroidSessionRoots(*f.context, visit); }});
    f.context->package_name = "example";
    f.context->service_inventory_known = true;
    f.context->service_components = {{"example.Local", true}};
    const auto base = f.New("Landroid/content/Context;");
    f.context->application_base_context = base;
    const auto component = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Local"))});
    const auto intent = f.New("Landroid/content/Intent;");
    f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(component)});
    const auto first = f.New("Lexample/Connection;");
    const auto second = f.New("Lexample/Connection;");
    const auto roots = f.vm.ProtectReferences(std::array{base, intent, first, second});
    const auto bind = [&](VmObjectRef c, int flags=1) { return f.OnOutcome(base, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z", {VmValue::Ref(intent), VmValue::Ref(c), VmValue::Int(flags)}); };
    const auto unbind = [&](VmObjectRef c) { f.On(base, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(c)}); };
    REQUIRE(bind(first).value.AsInt() == 1);
    REQUIRE(bind(first).value.AsInt() == 1);
    CHECK(created == 0);
    unbind(first);
    REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
    CHECK(created == 0);
    REQUIRE(bind(first).value.AsInt() == 1);
    REQUIRE(bind(second).value.AsInt() == 1);
    static_cast<void>(f.vm.CollectGarbage("queued-service"));
    REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
    CHECK(created == 1); CHECK(bound == 1); CHECK(connected == 2);
    static_cast<void>(f.vm.CollectGarbage("bound-service"));
    CHECK(f.vm.MarkReachable().IsMarked(delivered));
    unbind(first); CHECK(destroyed == 0);
    unbind(second); CHECK(destroyed == 1); CHECK(unbound == 1);
    CHECK(f.context->local_services.empty());
    CHECK(f.context->local_service_bindings.empty());
    REQUIRE(bind(first, 0).exception.IsValid());
    f.context->service_components[0].process_name = "example:remote";
    REQUIRE(bind(first).exception.IsValid());
    f.context->service_components[0].process_name.clear();
    f.context->service_components[0].flags = 0x0002U;
    REQUIRE(bind(first).exception.IsValid());
    f.context->service_components[0].flags = 0;
    f.context->service_components[0].enabled = false;
    const auto disabled = bind(first);
    REQUIRE_FALSE(disabled.exception.IsValid()); CHECK(disabled.value.AsInt() == 0);
    f.context->service_components[0].enabled = true;
    null_binder = true;
    REQUIRE(bind(first).value.AsInt() == 1);
    REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
    CHECK(connected == 2);
    unbind(first);
    CHECK(destroyed == 2);
    null_binder = false;
    fail_bind = true;
    REQUIRE(bind(first).value.AsInt() == 1);
    const auto bind_error = PumpJavaThreads(f.vm, *f.context);
    REQUIRE(bind_error.has_value());
    CHECK(bind_error->find("bind failure") != std::string::npos);
    unbind(first);
    CHECK(destroyed == 3);
    fail_bind = false;
    delivered = VmObjectRef{};
    REQUIRE(bind(first).value.AsInt() == 1);
    REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
    ShutdownLocalServices(f.vm, *f.context);
    CHECK(destroyed == 4); CHECK(unbound == 4);
  }
}


TEST_CASE("BND46 Java MotionEvent preserves snapshot axes history identity and GC ownership") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    AndroidBoundaryInput input;
    input.type = AndroidBoundaryInputType::pointer_motion;
    input.action = 5 | (1 << 8); input.source = 0x1002; input.device_id = 7;
    input.down_time_ns = 4'000'000'001LL; input.event_time_ns = 5'000'000'123LL;
    input.flags = 8; input.meta_state = 3; input.button_state = 2; input.edge_flags = 4;
    input.x_offset = 11; input.y_offset = 13; input.x_precision = 0.5F; input.y_precision = 0.25F;
    AndroidInputPointer first, second;
    first.id = 9; second.id = 3; second.tool_type = 2;
    for (std::size_t i = 0; i < 64; ++i) second.axes[i] = static_cast<float>(i) + 10.5F;
    input.pointers = {first, second};
    second.axes[0] = 100;
    input.history = {{4'500'000'001LL, {first, second}}};
    const auto event = MakeMotionEvent(f.vm, input);
    const auto root = f.vm.ProtectReferences(std::array{event});
    input.pointers[1].axes[0] = -100;
    static_cast<void>(f.vm.CollectGarbage("bnd46-motion-snapshot"));
    const auto integer = [&](const char* name) { return f.On(event, name, "()I").AsInt(); };
    CHECK(integer("getAction") == (5 | 1 << 8));
    CHECK(integer("getActionMasked") == 5); CHECK(integer("getActionIndex") == 1);
    CHECK(integer("getDeviceId") == 7); CHECK(integer("getSource") == 0x1002);
    CHECK(integer("getFlags") == 8); CHECK(integer("getMetaState") == 3);
    CHECK(integer("getButtonState") == 2); CHECK(integer("getEdgeFlags") == 4);
    CHECK(integer("getPointerCount") == 2); CHECK(integer("getHistorySize") == 1);
    CHECK(f.On(event, "getPointerId", "(I)I", {VmValue::Int(1)}).AsInt() == 3);
    CHECK(f.On(event, "getToolType", "(I)I", {VmValue::Int(1)}).AsInt() == 2);
    CHECK(f.On(event, "findPointerIndex", "(I)I", {VmValue::Int(9)}).AsInt() == 0);
    CHECK(f.On(event, "findPointerIndex", "(I)I", {VmValue::Int(4)}).AsInt() == -1);
    CHECK(f.On(event, "getDownTime", "()J").AsLong() == 4000);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5000);
    CHECK(f.On(event, "getEventTimeNano", "()J").AsLong() == 5'000'000'123LL);
    CHECK(f.On(event, "getHistoricalEventTime", "(I)J", {VmValue::Int(0)}).AsLong() == 4500);
    CHECK(f.On(event, "getHistoricalEventTimeNano", "(I)J", {VmValue::Int(0)}).AsLong() == 4'500'000'001LL);
    const std::array names{"X", "Y", "Pressure", "Size", "TouchMajor", "TouchMinor", "ToolMajor", "ToolMinor", "Orientation"};
    for (std::size_t axis = 0; axis < names.size(); ++axis) {
        const auto name = std::string("get") + names[axis];
        const auto history = std::string("getHistorical") + names[axis];
        const float offset = axis == 0 ? 11.0F : axis == 1 ? 13.0F : 0;
        CHECK(f.On(event, name.c_str(), "(I)F", {VmValue::Int(1)}).AsFloat() == static_cast<float>(axis) + 10.5F + offset);
        CHECK(f.On(event, name.c_str(), "()F").AsFloat() == offset);
        CHECK(f.On(event, history.c_str(), "(II)F", {VmValue::Int(1), VmValue::Int(0)}).AsFloat() ==
              (axis == 0 ? 111.0F : static_cast<float>(axis) + 10.5F + offset));
    }
    CHECK(f.On(event, "getAxisValue", "(II)F", {VmValue::Int(63), VmValue::Int(1)}).AsFloat() == 73.5F);
    CHECK(f.On(event, "getHistoricalAxisValue", "(III)F", {VmValue::Int(0), VmValue::Int(1), VmValue::Int(0)}).AsFloat() == 111);
    CHECK(f.On(event, "getRawX", "()F").AsFloat() == 0);
    CHECK(f.On(event, "getXPrecision", "()F").AsFloat() == 0.5F);
    CHECK(f.On(event, "getYPrecision", "()F").AsFloat() == 0.25F);
    CHECK(f.OnOutcome(event, "getX", "(I)F", {VmValue::Int(2)}).exception.IsValid());
    CHECK(f.OnOutcome(event, "getHistoricalX", "(I)F", {VmValue::Int(1)}).exception.IsValid());
    static_cast<void>(f.On(event, "recycle", "()V"));
    CHECK(f.OnOutcome(event, "getAction", "()I").exception.IsValid());
  }
}

TEST_CASE("BND46 timed KeyEvent retains metadata in both interpreters") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    const auto event = f.New("Landroid/view/KeyEvent;", "(JJIIIIIIII)V",
        {VmValue::Long(4000), VmValue::Long(5000), VmValue::Int(0), VmValue::Int(29),
         VmValue::Int(2), VmValue::Int(3), VmValue::Int(-1), VmValue::Int(4), VmValue::Int(8), VmValue::Int(0x101)});
    CHECK(f.On(event, "getDownTime", "()J").AsLong() == 4000);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5000);
    CHECK(f.On(event, "getFlags", "()I").AsInt() == 8);
    CHECK(f.On(event, "getSource", "()I").AsInt() == 0x101);
  }
}

TEST_CASE("DVM-206 virtual key character maps use API19 data and process device fallback") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    constexpr auto owner = "Landroid/view/KeyCharacterMap;";
    const auto load = [&](int device) {
        return f.Static(owner, "load", "(I)Landroid/view/KeyCharacterMap;", {VmValue::Int(device)}).ref;
    };
    const auto absent = f.StaticOutcome(owner, "load", "(I)Landroid/view/KeyCharacterMap;", {VmValue::Int(-1)});
    REQUIRE(absent.exception.IsValid());
    CHECK(f.linker.Class(absent.exception_class).descriptor == "Landroid/view/KeyCharacterMap$UnavailableException;");
    f.context->input_devices = {
        {-1, "keyboard", "fixture:keyboard", kAndroidKeyboardSource, 2, {}},
        {0, "touch", "fixture:touch", kAndroidTouchSource, 0, {}}};
    const auto map = load(-1), fallback = load(404), empty = load(0);
    const auto roots = f.vm.ProtectReferences(std::array{map, fallback, empty});
    CHECK_FALSE(f.linker.Class(f.model.ObjectClass(map)).is_intrinsic);
    const auto get = [&](VmObjectRef object, int code, int meta) {
        return f.On(object, "get", "(II)I", {VmValue::Int(code), VmValue::Int(meta)}).AsInt();
    };
    for (const auto& [code, meta, expected] : std::array<std::tuple<int, int, int>, 15>{{
        {29, 0, 'a'}, {29, 0x40, 'A'}, {29, 0x80, 'A'},
        {29, 0x100000, 'A'}, {29, 0x100001, 'A'}, {29, 0x2000, 0},
        {31, 0x10, 0xe7}, {31, 0x11, 0xc7},
        {8, 0, '1'}, {8, 1, '!'}, {66, 0, '\n'}, {61, 0, '\t'},
        {144, 0, 0}, {144, 0x200000, '0'}, {999, 0, 0}}}) {
        CHECK(get(map, code, meta) == expected);
        CHECK(get(fallback, code, meta) == expected);
        CHECK(get(empty, code, meta) == 0);
    }
    CHECK(get(map, 33, 2) == std::bit_cast<std::int32_t>(0x800000b4U)); // ALT+E dead acute
    const auto normalization = f.StaticOutcome(owner, "getDeadChar", "(II)I", {VmValue::Int(0xb4), VmValue::Int('e')});
    CHECK(f.linker.Class(normalization.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    CHECK(f.On(map, "getDisplayLabel", "(I)C", {VmValue::Int(29)}).AsInt() == 'A');
    CHECK(f.On(map, "getNumber", "(I)C", {VmValue::Int(10)}).AsInt() == '3');
    CHECK(f.On(map, "getKeyboardType", "()I").AsInt() == 4);
    CHECK(f.On(empty, "getKeyboardType", "()I").AsInt() == 5);
    CHECK(f.On(map, "getModifierBehavior", "()I").AsInt() == 0);
    const auto category = f.OnOutcome(map, "isPrintingKey", "(I)Z", {VmValue::Int(29)});
    CHECK(f.linker.Class(category.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 2);
    const auto chars_root = f.vm.ProtectReferences(std::array{chars});
    f.model.SetPrimitiveElement(chars, 0, 'a'); f.model.SetPrimitiveElement(chars, 1, 'A');
    CHECK(f.On(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(chars), VmValue::Int(0)}).AsInt() == 'a');
    CHECK(f.On(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(chars), VmValue::Int(1)}).AsInt() == 'A');
    const auto action = f.On(map, "getFallbackAction", "(II)Landroid/view/KeyCharacterMap$FallbackAction;",
        {VmValue::Int(111), VmValue::Int(0)}).ref;
    REQUIRE(action.IsValid());
    const auto action_root = f.vm.ProtectReferences(std::array{action});
    const auto key_field = f.linker.FindFieldRecursive(f.model.ObjectClass(action), "keyCode", "I");
    REQUIRE(key_field.has_value());
    CHECK(f.model.InstanceSlots(action)[f.linker.Field(*key_field).slot].bits == 4);
    f.On(action, "recycle", "()V");
    const auto device = f.Static("Landroid/view/InputDevice;", "getDevice", "(I)Landroid/view/InputDevice;", {VmValue::Int(-1)}).ref;
    const auto device_root = f.vm.ProtectReferences(std::array{device});
    const auto device_map = f.On(device, "getKeyCharacterMap", "()Landroid/view/KeyCharacterMap;").ref;
    CHECK(get(device_map, 29, 0) == 'a');
    const auto event = f.New("Landroid/view/KeyEvent;", "(II)V", {VmValue::Int(0), VmValue::Int(33)});
    const auto event_root = f.vm.ProtectReferences(std::array{event});
    CHECK(f.On(event, "getUnicodeChar", "(I)I", {VmValue::Int(2)}).AsInt() == get(map, 33, 2));
    CHECK(f.On(event, "getUnicodeChar", "()I").AsInt() == 'e');
    SetAndroidKeyEventUnicode(f.vm, event, 0x03bb);
    CHECK(f.On(event, "getUnicodeChar", "()I").AsInt() == 0x03bb);
    const auto unsupported = f.OnOutcome(map, "getEvents", "([C)[Landroid/view/KeyEvent;", {VmValue::Ref(chars)});
    CHECK(f.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    const auto invalid = f.OnOutcome(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
    CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
    f.On(map, "finalize", "()V");
    const auto disposed = f.OnOutcome(map, "get", "(II)I", {VmValue::Int(29), VmValue::Int(0)});
    CHECK(f.linker.Class(disposed.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
    CHECK(get(fallback, 29, 0) == 'a');
    static_cast<void>(f.vm.CollectGarbage("virtual-key-character-map"));
    CHECK(get(fallback, 29, 0) == 'a');
  }
}

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

TEST_CASE("DVM-211 PendingIntent identity flags cancellation and GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "example";
        f.vm.SetGcIntegration({{}, {}, [&f](const VmRootVisitor& visit) {
            VisitAndroidSessionRoots(*f.context, visit);
        }});
        const auto base = f.New("Landroid/content/Context;");
        const auto action = f.vm.NewStringUtf8("Submit");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
        const auto input_roots = f.vm.ProtectReferences(std::array{base, intent});
        constexpr auto descriptor = "Landroid/app/PendingIntent;";
        constexpr auto signature = "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;";
        constexpr int no_create=1<<29, cancel_current=1<<28, update_current=1<<27, one_shot=1<<30;
        const auto token = [&](int flags=0, int request=0, const char* kind="getService") {
            return f.Static(descriptor, kind, signature,
                {VmValue::Ref(base), VmValue::Int(request), VmValue::Ref(intent), VmValue::Int(flags)}).ref;
        };
        CHECK_FALSE(token(no_create).IsValid());
        const auto first = token();
        const auto first_root = f.vm.ProtectReferences(std::array{first});
        REQUIRE(first.IsValid());
        CHECK(token() == first);
        CHECK(token(no_create) == first);
        CHECK(token(0, 1) != first);
        CHECK(token(0, 0, "getBroadcast") != first);
        CHECK(token(one_shot) != first);
        CHECK(token(1) != first); // fill-in flags form part of API19 identity
        const auto snapshot = f.context->pending_intents.at(first.Value()).intent;
        CHECK(snapshot != intent);
        CHECK(f.On(snapshot, "filterEquals", "(Landroid/content/Intent;)Z", {VmValue::Ref(intent)}).AsInt() == 1);
        const auto extra = f.vm.NewStringUtf8("extra");
        const auto extra_root = f.vm.ProtectReferences(std::array{extra});
        const auto put = [&](int value) { f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;", {VmValue::Ref(extra), VmValue::Int(value)}); };
        const auto read = [&] { return f.On(snapshot, "getIntExtra", "(Ljava/lang/String;I)I", {VmValue::Ref(extra), VmValue::Int(-1)}).AsInt(); };
        put(7);
        CHECK(token() == first); // extras do not define token identity
        CHECK(read() == -1); // snapshot cannot observe caller mutation
        CHECK(token(update_current) == first);
        CHECK(read() == 7);
        put(9);
        CHECK(read() == 7);
        f.On(intent, "setFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x10000000)});
        CHECK(token(update_current) == first);
        CHECK(f.On(snapshot, "getFlags", "()I").AsInt() == 0); // update replaces only extras
        CHECK(read() == 9);
        const auto change_action = [&](const char* value) { f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8(value))}); };
        change_action("Sync");
        CHECK(token() != first);
        change_action("Submit");
        CHECK(token(no_create) == first);
        const auto category = f.vm.NewStringUtf8("category");
        const auto category_root = f.vm.ProtectReferences(std::array{category});
        f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(category)});
        CHECK(token() != first);
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(category)});
        CHECK(token() == first);
        const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(f.vm.NewStringUtf8("file:///value"))}).ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(uri)});
        CHECK(token() != first);
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        const auto component = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Worker"))});
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(component)});
        CHECK(token() != first);
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8("text/plain"))});
        CHECK(token() != first);
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(f.vm.NewStringUtf8("example"))});
        CHECK(token() != first);
        f.On(intent, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        CHECK(token() == first);
        const auto alarm_name = f.vm.NewStringUtf8("alarm");
        const auto alarm = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref;
        CHECK(f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(alarm_name)}).ref == alarm);
        const auto cancel_alarm = [&](VmObjectRef operation) { f.On(alarm, "cancel", "(Landroid/app/PendingIntent;)V", {VmValue::Ref(operation)}); };
        const auto other = token(0, 2);
        // Seed backend entries to verify removal independently of unsupported scheduling.
        f.context->alarm_operations = {first, other};
        cancel_alarm(first);
        REQUIRE(f.context->alarm_operations.size() == 1);
        CHECK(f.context->alarm_operations.front() == other);
        cancel_alarm(first); // canceling an absent alarm is valid
        cancel_alarm(VmObjectRef{}); // API19 null cancellation is also valid
        CHECK(token(no_create) == first); // alarm cancel never invalidates sender
        static_cast<void>(f.vm.CollectGarbage("pending-intent-owners"));
        CHECK(f.vm.MarkReachable().IsMarked(snapshot));
        CHECK(f.vm.MarkReachable().IsMarked(other)); // alarm keeps its wrapper alive
        CHECK(f.context->pending_intents.size() == 2); // other weak entries swept
        f.On(first, "cancel", "()V");
        f.On(first, "cancel", "()V");
        CHECK_FALSE(token(no_create).IsValid());
        const auto replacement = token();
        const auto replacement_root = f.vm.ProtectReferences(std::array{replacement});
        CHECK(replacement != first);
        CHECK(token(cancel_current | no_create) == replacement);
        CHECK(f.context->pending_intents.at(replacement.Value()).canceled);
        CHECK_FALSE(token(no_create).IsValid());
        const auto next = token();
        const auto next_root = f.vm.ProtectReferences(std::array{next});
        CHECK(token(cancel_current) != next);
        CHECK(f.context->pending_intents.at(next.Value()).canceled);
        const auto fail = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0x100)});
        REQUIRE(fail.exception.IsValid());
        CHECK(f.linker.Class(fail.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto null_context = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto null_intent = f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(null_intent.exception.IsValid());
        CHECK(f.linker.Class(null_intent.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto content_uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(f.vm.NewStringUtf8("content://example/value"))}).ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(content_uri)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        const auto selector = f.New("Landroid/content/Intent;");
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(selector)});
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(f.OnOutcome(next, "send", "()V").exception.IsValid());
        REQUIRE(f.OnOutcome(alarm, "set", "(IJLandroid/app/PendingIntent;)V",
            {VmValue::Int(3), VmValue::Long(1), VmValue::Ref(other)}).exception.IsValid());
        CHECK(f.ledger.Unimplemented().size() == 2);
        cancel_alarm(other);
        static_cast<void>(f.vm.CollectGarbage("pending-intent-weak-registry"));
        CHECK_FALSE(f.context->pending_intents.contains(other.Value()));
        ShutdownPendingIntents(f.vm, *f.context);
        CHECK(f.context->pending_intents.empty());
        CHECK(f.context->alarm_operations.empty());
        REQUIRE(f.StaticOutcome(descriptor, "getService", signature,
            {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)}).exception.IsValid());
    }
}

TEST_CASE("DVM-211 concurrent contexts share one PendingIntent token") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "example";
        const auto base = f.New("Landroid/content/Context;");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("work"))});
        const auto inputs = f.vm.ProtectReferences(std::array{base, intent});
        const auto klass = f.linker.ResolveDescriptor("Landroid/app/PendingIntent;");
        const auto method = f.linker.FindDirectMethod(klass, "getService",
            "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;");
        REQUIRE(method.has_value());
        const auto first = f.vm.CreateExecutionContext(), second = f.vm.CreateExecutionContext();
        std::array<VmCallOutcome, 2> results;
        const auto run = [&](std::size_t index, const InterpreterExecutionContext& context) {
            for (int repeat=0; repeat<16; ++repeat)
                results[index] = f.vm.Call(context, *method,
                    std::array{VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)});
        };
        std::thread a([&] { run(0, first); }), b([&] { run(1, second); });
        a.join(); b.join();
        REQUIRE_FALSE(results[0].exception.IsValid());
        REQUIRE_FALSE(results[1].exception.IsValid());
        CHECK(results[0].value.ref.IsValid());
        CHECK(results[0].value.ref == results[1].value.ref);
        CHECK(f.context->pending_intents.size() == 1);
        f.vm.DiscardExecutionContext(first); f.vm.DiscardExecutionContext(second);
    }
}

TEST_CASE("DVM118 resource text coerces simple values and preserves reference selection errors") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto add = [&](std::uint32_t id, std::uint8_t type, std::uint32_t data,
                             std::optional<std::string> text = {}) {
            ogplay::loader::ArscEntry entry{};
            entry.resource_id = id;
            entry.value_type = type;
            entry.value_data = data;
            entry.string_value = std::move(text);
            f.context->arsc.entries.push_back(std::move(entry));
        };
        struct Value { std::uint8_t type; std::uint32_t data; const char* expected; };
        const std::array values{
            Value{0x12, 0, "false"}, Value{0x12, 0xffffffffU, "true"},
            Value{0x10, 30, "30"}, Value{0x10, 0xfffffff9U, "-7"},
            Value{0x11, 0x80000000U, "0x80000000"}, Value{0x1c, 0xff010203U, "#ff010203"},
            Value{0x04, std::bit_cast<std::uint32_t>(3.25F), "3.25"},
            Value{0x05, 0x501, "5.0dip"}, Value{0x06, 0x40000030, "50.0%"},
            Value{0x03, 0, "hello"}};
        for (std::size_t i = 0; i < values.size(); ++i)
            add(0x7f010000U + static_cast<std::uint32_t>(i), values[i].type, values[i].data,
                values[i].type == 3 ? std::optional<std::string>{"hello"} : std::nullopt);
        add(0x7f010080, 1, 0x7f010081);
        add(0x7f010081, 1, 0x7f010002);
        for (const auto [orientation, text] : {std::pair{0, "default"}, {1, "portrait"}, {2, "landscape"}}) {
            add(0x7f010090, 3, 0, text);
            f.context->arsc.entries.back().orientation = static_cast<std::uint8_t>(orientation);
        }
        add(0x7f010091, 1, 0x7f010090);
        add(0x7f010100, 0, 0);
        add(0x7f010101, 0x0f, 0);
        add(0x7f010102, 3, 0);
        add(0x7f010103, 3, 0, "bag");
        f.context->arsc.entries.back().is_complex = true;
        add(0x7f010104, 1, 0);
        add(0x7f010105, 1, 0x7f010106);
        add(0x7f010106, 1, 0x7f010105);
        const auto context = f.vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto resources = f.On(context, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{context, resources});
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto id = VmValue::Int(static_cast<std::int32_t>(0x7f010000U + i));
            for (const auto [receiver, method, signature] : {
                std::tuple{resources, "getText", "(I)Ljava/lang/CharSequence;"},
                std::tuple{resources, "getString", "(I)Ljava/lang/String;"},
                std::tuple{context, "getString", "(I)Ljava/lang/String;"}}) {
                const auto text = f.On(receiver, method, signature, {id}).ref;
                CHECK(f.vm.StringUtf8(text) == values[i].expected);
            }
        }
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010080)}).ref) == "30");
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010091)}).ref) == "landscape");
        f.context->surface_width = 480;
        f.context->surface_height = 800;
        CHECK(f.vm.StringUtf8(f.On(resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0x7f010091)}).ref) == "portrait");
        const auto not_found = f.linker.ResolveDescriptor("Landroid/content/res/Resources$NotFoundException;");
        for (const auto id : {0, 0x7f01ffff, 0x7f010100, 0x7f010101, 0x7f010102, 0x7f010103, 0x7f010104, 0x7f010105}) {
            for (const auto [method, signature] : {
                 std::pair{"getText", "(I)Ljava/lang/CharSequence;"}, {"getString", "(I)Ljava/lang/String;"}}) {
                const auto result = f.OnOutcome(resources, method, signature, {VmValue::Int(id)});
                REQUIRE(result.exception.IsValid());
                CHECK(result.exception_class == not_found);
            }
        }
    }
}

TEST_CASE("DVM118 getString dispatches getText and CharSequence toString through Context") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto text = std::make_shared<VmObjectRef>();
        auto resources = std::make_shared<VmObjectRef>();
        auto custom = IntrinsicClassBuilder::Class("Ltest/TextResources;", "Landroid/content/res/Resources;");
        custom.OverrideMethod("getText", "(I)Ljava/lang/CharSequence;",
            [text](IntrinsicContext&) { return VmValue::Ref(*text); });
        auto owner = IntrinsicClassBuilder::Class("Ltest/TextContext;", "Landroid/content/Context;");
        owner.OverrideMethod("getResources", "()Landroid/content/res/Resources;",
            [resources](IntrinsicContext&) { return VmValue::Ref(*resources); });
        AndroidValueVm f(backend, {std::move(custom).Build(), std::move(owner).Build()});
        *text = f.New("Ljava/lang/StringBuilder;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("overridden"))});
        *resources = f.vm.NewIntrinsicInstance("Ltest/TextResources;");
        const auto context = f.vm.NewIntrinsicInstance("Ltest/TextContext;");
        const auto roots = f.vm.ProtectReferences(std::array{*text, *resources, context});
        CHECK(f.On(*resources, "getText", "(I)Ljava/lang/CharSequence;", {VmValue::Int(0)}).ref == *text);
        CHECK(f.vm.StringUtf8(f.On(*resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "overridden");
        CHECK(f.vm.StringUtf8(f.On(context, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "overridden");
        *text = VmObjectRef{};
        const auto failure = f.OnOutcome(*resources, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)});
        REQUIRE(failure.exception.IsValid());
        CHECK(f.linker.Class(failure.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
    }
}

TEST_CASE("DVM226 BootDex PrintStream captures bytes stack traces encoding and GC without logging") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto buffer = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto stream = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffer)});
        const auto roots = f.vm.ProtectReferences(std::array{stream});
        const auto klass = f.model.ObjectClass(stream);
        CHECK(f.linker.Class(klass).is_boot_dex);
        CHECK(f.linker.Class(*f.linker.Class(klass).super).descriptor == "Ljava/io/FilterOutputStream;");
        f.On(stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("中文"))});
        CHECK(f.On(stream, "append", "(C)Ljava/io/PrintStream;", {VmValue::Int('!')}).ref == stream);
        f.On(stream, "println", "(I)V", {VmValue::Int(42)});
        f.On(stream, "flush", "()V");
        CHECK(f.vm.StringUtf8(f.On(buffer, "toString", "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))}).ref) == "中文!42\n");
        CHECK(f.logger.Snapshot(std::nullopt, "runtime.dexvm.guest").empty());
        CHECK(f.vm.MarkReachable().IsMarked(buffer));
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.model.IsValidRef(buffer));
        const auto error = f.New("Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("original"))});
        const auto error_root = f.vm.ProtectReferences(std::array{error});
        const auto cause = f.New("Ljava/lang/IllegalArgumentException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("cause"))});
        f.On(error, "initCause", "(Ljava/lang/Throwable;)Ljava/lang/Throwable;", {VmValue::Ref(cause)});
        f.On(error, "printStackTrace", "(Ljava/io/PrintStream;)V", {VmValue::Ref(stream)});
        f.On(stream, "flush", "()V");
        const auto trace = f.vm.StringUtf8(f.On(buffer, "toString", "()Ljava/lang/String;").ref);
        CHECK(trace.find("java.lang.RuntimeException: original") != std::string::npos);
        CHECK(trace.find("Caused by: java.lang.IllegalArgumentException: cause") != std::string::npos);
        CHECK(f.logger.Snapshot(std::nullopt, "runtime.dexvm.guest").empty());
        f.On(stream, "close", "()V");
        f.On(stream, "close", "()V");
        f.On(stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("after-close"))});
        CHECK(f.On(stream, "checkError", "()Z").AsInt() == 1);
        CHECK(f.vm.StringUtf8(f.On(buffer, "toString", "()Ljava/lang/String;").ref) == trace);
        const auto encoded = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto latin = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;ZLjava/lang/String;)V",
            {VmValue::Ref(encoded), VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("ISO-8859-1"))});
        f.On(latin, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("é"))});
        const auto bytes = f.On(encoded, "toByteArray", "()[B").ref;
        CHECK(f.model.ArrayLength(bytes) == 1);
        CHECK((f.model.GetPrimitiveElement(bytes, 0) & 255U) == 0xe9U);
        const auto construct = [&](const char* signature, std::vector<VmValue> args) {
            const auto object = f.vm.NewIntrinsicInstance("Ljava/io/PrintStream;");
            const auto method = f.linker.FindDirectMethod(klass, "<init>", signature);
            REQUIRE(method.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            return f.vm.Call(*method, args);
        };
        const auto null_stream = construct("(Ljava/io/OutputStream;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_stream.exception.IsValid());
        CHECK(f.linker.Class(null_stream.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto bad_charset = construct("(Ljava/io/OutputStream;ZLjava/lang/String;)V",
            {VmValue::Ref(buffer), VmValue::Int(0), VmValue::Ref(f.vm.NewStringUtf8("not-an-encoding"))});
        REQUIRE(bad_charset.exception.IsValid());
        CHECK(f.linker.Class(bad_charset.exception_class).descriptor == "Ljava/io/UnsupportedEncodingException;");
        const auto input = f.Bytes("!abc?");
        CHECK(f.vm.StringUtf8(f.New("Ljava/lang/String;", "([BIILjava/lang/String;)V",
            {VmValue::Ref(input), VmValue::Int(1), VmValue::Int(3), VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))})) == "abc");
        const auto string_class = f.linker.ResolveDescriptor("Ljava/lang/String;");
        const auto string_ctor = f.linker.FindDirectMethod(string_class, "<init>", "([BIILjava/lang/String;)V");
        REQUIRE(string_ctor.has_value());
        const auto range_error = f.vm.Call(*string_ctor, std::array{
            VmValue::Ref(f.vm.NewIntrinsicInstance("Ljava/lang/String;")), VmValue::Ref(input),
            VmValue::Int(3), VmValue::Int(3), VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))});
        REQUIRE(range_error.exception.IsValid());
        CHECK(f.linker.Class(range_error.exception_class).descriptor == "Ljava/lang/StringIndexOutOfBoundsException;");
    }
}

TEST_CASE("DVM226 PrintStream owns error and autoFlush semantics and System streams use only log sinks") {
    struct Target { std::vector<char> bytes; int flushes{}; int closes{}; bool fail_write{}; };
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto target = std::make_shared<Target>();
        auto out = IntrinsicClassBuilder::Class("Ltest/PrintTarget;", "Ljava/io/OutputStream;");
        out.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        out.OverrideMethod("write", "(I)V", [target](IntrinsicContext& c) {
            if (target->fail_write) throw VmJavaThrow{"Ljava/io/IOException;", "write failed"};
            target->bytes.push_back(static_cast<char>(c.arguments[0].AsInt()));
            return VmValue::Void();
        });
        out.OverrideMethod("flush", "()V", [target](IntrinsicContext&) { ++target->flushes; return VmValue::Void(); });
        out.OverrideMethod("close", "()V", [target](IntrinsicContext&) { ++target->closes; return VmValue::Void(); });
        AndroidValueVm f(backend, {std::move(out).Build()});
        const auto sink = f.New("Ltest/PrintTarget;");
        const auto stream = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;Z)V", {VmValue::Ref(sink), VmValue::Int(1)});
        f.On(stream, "write", "(I)V", {VmValue::Int('x')});
        CHECK(target->flushes == 0);
        f.On(stream, "write", "(I)V", {VmValue::Int('\n')});
        CHECK(target->flushes == 1);
        CHECK(std::string(target->bytes.begin(), target->bytes.end()) == "x\n");
        target->fail_write = true;
        CHECK_FALSE(f.OnOutcome(stream, "write", "(I)V", {VmValue::Int('x')}).exception.IsValid());
        CHECK(f.On(stream, "checkError", "()Z").AsInt() == 1);
        f.On(stream, "close", "()V");
        f.On(stream, "close", "()V");
        CHECK(target->closes == 1);
        const auto initialized = f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor("Ljava/lang/System;"));
        REQUIRE_FALSE(initialized.exception.IsValid());
        const auto system = f.linker.ResolveDescriptor("Ljava/lang/System;");
        const auto get = [&](const char* name) {
            const auto field = f.linker.FindFieldRecursive(system, name, "Ljava/io/PrintStream;");
            REQUIRE(field.has_value());
            return VmObjectRef{f.linker.Class(system).static_storage[f.linker.Field(*field).slot]};
        };
        const auto stdout_stream = get("out");
        const auto stderr_stream = get("err");
        CHECK(stdout_stream != stderr_stream);
        f.On(stdout_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("stdout 中文"))});
        f.On(stderr_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("stderr"))});
        f.On(stdout_stream, "append", "(Ljava/lang/CharSequence;)Ljava/io/PrintStream;", {VmValue::Ref(f.vm.NewStringUtf8(""))});
        auto logs = f.logger.Snapshot();
        REQUIRE(logs.size() == 2);
        CHECK(logs[0].message == "stdout 中文");
        CHECK(logs[1].message == "stderr");
        f.On(stdout_stream, "close", "()V");
        f.On(stderr_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("still-open"))});
        logs = f.logger.Snapshot();
        CHECK(logs.back().message == "still-open");
        CHECK(f.On(stdout_stream, "checkError", "()Z").AsInt() == 0);
        f.On(stdout_stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("closed"))});
        CHECK(f.On(stdout_stream, "checkError", "()Z").AsInt() == 1);
    }
}

TEST_CASE("DVM227 notification values reject system templates with catchable recorded failure") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "notification_template.dex");
        f.context->package_name = "fixture";
        const auto base = f.New("Landroid/content/Context;");
        const auto before = f.Static("Ljava/lang/System;", "currentTimeMillis", "()J").AsLong();
        const auto notification = f.New("Landroid/app/Notification;");
        const auto after = f.Static("Ljava/lang/System;", "currentTimeMillis", "()J").AsLong();
        const auto type = f.model.ObjectClass(notification);
        CHECK(f.linker.Class(type).is_boot_dex);
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/os/Parcelable;"), type));
        const auto field = [&](const char* name, const char* descriptor) {
            const auto found = f.linker.FindFieldRecursive(type, name, descriptor);
            REQUIRE(found.has_value());
            return f.linker.Field(*found).slot;
        };
        const auto timestamp_slot = field("when", "J");
        const auto timestamp = static_cast<std::uint64_t>(f.model.InstanceSlots(notification)[timestamp_slot].bits) |
            static_cast<std::uint64_t>(f.model.InstanceSlots(notification)[timestamp_slot + 1].bits) << 32U;
        CHECK(timestamp >= static_cast<std::uint64_t>(before));
        CHECK(timestamp <= static_cast<std::uint64_t>(after));
        CHECK(f.model.InstanceSlots(notification)[field("priority", "I")].bits == 0);
        CHECK(f.model.InstanceSlots(notification)[field("audioStreamType", "I")].bits == UINT32_MAX);
        const auto extras = VmObjectRef{f.model.InstanceSlots(notification)[field("extras", "Landroid/os/Bundle;")].bits};
        REQUIRE(extras.IsValid());
        const auto title = f.vm.NewStringUtf8("title");
        const auto legacy = f.New("Landroid/app/Notification;", "(ILjava/lang/CharSequence;J)V",
            {VmValue::Int(17), VmValue::Ref(title), VmValue::Long(0x123456789LL)});
        CHECK(f.model.InstanceSlots(legacy)[field("icon", "I")].bits == 17);
        CHECK(f.model.InstanceSlots(legacy)[field("tickerText", "Ljava/lang/CharSequence;")].bits == title.Value());
        CHECK(f.model.InstanceSlots(legacy)[timestamp_slot].bits == 0x23456789U);
        CHECK(f.model.InstanceSlots(legacy)[timestamp_slot + 1].bits == 1U);
        const auto roots = f.vm.ProtectReferences(std::array{base, notification, legacy});
        CHECK(f.vm.MarkReachable().IsMarked(extras));
        CHECK(f.vm.MarkReachable().IsMarked(title));
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.model.IsValidRef(extras));
        CHECK(f.On(notification, "describeContents", "()I").AsInt() == 0);
        const auto signature = "(Landroid/content/Context;Ljava/lang/CharSequence;Ljava/lang/CharSequence;Landroid/app/PendingIntent;)V";
        const auto result = f.OnOutcome(legacy, "setLatestEventInfo", signature,
            {VmValue::Ref(base), VmValue::Ref(title), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.model.InstanceSlots(legacy)[field("icon", "I")].bits == 17);
        CHECK(f.model.InstanceSlots(legacy)[field("tickerText", "Ljava/lang/CharSequence;")].bits == title.Value());
        CHECK(f.model.InstanceSlots(legacy)[field("contentView", "Landroid/widget/RemoteViews;")].bits == 0);
        CHECK(f.model.InstanceSlots(legacy)[field("contentIntent", "Landroid/app/PendingIntent;")].bits == 0);
        CHECK(f.Static("Lfixture/NotificationTemplateProbe;", "fallback", "(Landroid/content/Context;)I", {VmValue::Ref(base)}).AsInt() == 1);
        auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.notification_template");
        CHECK(hits[0].count == 2);
        const auto null_context = f.OnOutcome(legacy, "setLatestEventInfo", signature,
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(title), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_context.exception.IsValid());
        CHECK(f.linker.Class(null_context.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        CHECK(f.ledger.Unimplemented()[0].count == 2);
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto rejected_parcel = f.OnOutcome(notification, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        REQUIRE(rejected_parcel.exception.IsValid());
        CHECK(f.linker.Class(rejected_parcel.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_parcel" && hit.count == 1; }));
        const auto parcel_ctor = f.linker.FindDirectMethod(type, "<init>", "(Landroid/os/Parcel;)V");
        REQUIRE(parcel_ctor.has_value());
        const auto from_parcel = f.vm.Call(*parcel_ctor, std::array{
            VmValue::Ref(f.vm.NewIntrinsicInstance("Landroid/app/Notification;")), VmValue::Ref(parcel)});
        REQUIRE(from_parcel.exception.IsValid());
        CHECK(f.linker.Class(from_parcel.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_parcel" && hit.count == 2; }));
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef{f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]};
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(3)}).ref;
        CHECK(f.model.ArrayLength(array) == 3);
        CHECK_FALSE(f.model.GetObjectElement(array, 0).IsValid());
        const auto manager = f.On(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            {VmValue::Ref(f.vm.NewStringUtf8("notification"))}).ref;
        const auto post = f.OnOutcome(manager, "notify", "(ILandroid/app/Notification;)V", {VmValue::Int(1), VmValue::Ref(notification)});
        REQUIRE(post.exception.IsValid());
        CHECK(f.linker.Class(post.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.On(manager, "cancelAll", "()V");
        hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) { return hit.id == "dexvm.notification_post" && hit.count == 1; }));
    }
}
