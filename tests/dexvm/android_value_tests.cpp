#include "boot_dex.h"
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

struct AndroidValueVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;

    AndroidValueVm(InterpreterBackend backend = InterpreterBackend::switch_dispatch,
                   const std::vector<IntrinsicClassDecl>& extras = {},
                   bool force_all_bridge = false)
        : vm(
              [this, &extras]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog(
                      AndroidCoreIntrinsicServices(context)));
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  ogplay::test::RegisterBootDex(linker);
                  linker.RegisterIntrinsics(extras);
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {.backend = backend, .force_all_bridge = force_all_bridge}) {
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
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).argb ==
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
    CHECK(fixture.context->bitmaps.at(bitmap.Value()).argb ==
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
