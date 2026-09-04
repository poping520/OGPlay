#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
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

    AndroidValueVm()
        : vm([this]() -> DexClassLinker& {
                 linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                 linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                 linker.Link();
                 return linker;
             }(), model, nullptr, ledger, {}) {
        RegisterAndroidValueStateTables(vm, context);
    }

    VmObjectRef New(const char* descriptor,
                    const char* constructor = "()V",
                    std::vector<VmValue> arguments = {}) {
        const auto klass = linker.ResolveDescriptor(descriptor);
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
    REQUIRE_FALSE(fixture.context->sparse_arrays.empty());
    REQUIRE_FALSE(fixture.context->paths.empty());
    REQUIRE_FALSE(fixture.context->parcels.empty());
    REQUIRE_FALSE(fixture.context->wake_locks.empty());
    const auto result = fixture.vm.CollectGarbage("dvm86-value-state");
    CHECK(result.freed_objects >= 4U);
    CHECK(fixture.context->sparse_arrays.empty());
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
