#include "boot_dex.h"
#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
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
                   const std::vector<IntrinsicClassDecl>& extras = {})
        : vm(
              [this, &extras]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  ogplay::test::RegisterBootDex(linker);
                  linker.RegisterIntrinsics(extras);
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {.backend = backend}) {
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
        const auto klass = linker.ResolveDescriptor(descriptor);
        const auto method = linker.FindDirectMethod(klass, name, signature);
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
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

TEST_CASE("DVM-117 Bundle Parcel snapshots and Java CREATOR retain ordinary references") {
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
        CHECK(f.vm.MarkReachable().IsMarked(bytes));
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
        CHECK(f.On(another, "getSerializable", "(Ljava/lang/String;)Ljava/io/Serializable;",
                   {VmValue::Ref(key)}).ref == bytes);
        f.On(parcel, "recycle", "()V");
        // Retire the nested Bundle copy constructor's last Java return root.
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(bytes));
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
    REQUIRE_FALSE(fixture.context->parcels.empty());
    REQUIRE_FALSE(fixture.context->wake_locks.empty());
    const auto result = fixture.vm.CollectGarbage("dvm86-value-state");
    CHECK(result.freed_objects >= 4U);
    CHECK(fixture.context->paths.empty());
    CHECK(fixture.context->parcels.empty());
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
        f.context->pending_activity_descriptor.clear();
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
