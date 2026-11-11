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
        CHECK(f.On(f.On(intent, "getExtras", "()Landroid/os/Bundle;").ref, "isEmpty", "()Z").AsInt() == 1);
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;", {VmValue::Ref(key), VmValue::Int(1)});
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
             {VmValue::Ref(keep), VmValue::Int(9)});
        const auto payload = f.New("Ljava/util/HashMap;");
        f.On(source, "putSerializable", "(Ljava/lang/String;Ljava/io/Serializable;)V",
             {VmValue::Ref(key), VmValue::Ref(payload)});
        CHECK(f.On(intent, "putExtras", signature, {VmValue::Ref(source)}).ref == intent);
        f.On(source, "clear", "()V");
        static_cast<void>(f.vm.CollectGarbage("dvm140-merged-extras"));
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)})
                  .ref == payload);
        CHECK(f.On(intent, "getIntExtra", "(Ljava/lang/String;I)I", {VmValue::Ref(keep), VmValue::Int(0)}).AsInt() ==
              9);
        f.On(source, "putString", "(Ljava/lang/String;Ljava/lang/String;)V",
             {VmValue::Ref(key), VmValue::Ref(VmObjectRef{})});
        f.On(intent, "putExtras", signature, {VmValue::Ref(source)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK_FALSE(
            f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)})
                .ref.IsValid());
        for (const auto target : {intent, f.New("Landroid/content/Intent;")}) {
            // AOSP attaches a Bundle before putAll(null) throws; CM returned early.
            const auto before = f.On(target, "getExtras", "()Landroid/os/Bundle;").ref.IsValid();
            const auto outcome = f.OnOutcome(target, "putExtras", signature, {VmValue::Ref(VmObjectRef{})});
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/NullPointerException;");
            const auto after = f.On(target, "getExtras", "()Landroid/os/Bundle;").ref;
            REQUIRE(after.IsValid());
            if (!before) CHECK(f.On(after, "isEmpty", "()Z").AsInt() == 1);
        }
    }
}

TEST_CASE("DVM-117 Intent extras use BootDex Bundle identity copies and GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
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
                   {VmValue::Ref(key), VmValue::Ref(payload)})
                  .ref == intent);
        CHECK(f.vm.MarkReachable().IsMarked(child));
        static_cast<void>(f.vm.CollectGarbage("dvm117-intent-extras"));
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)})
                  .ref == payload);
        CHECK_FALSE(f.On(intent, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(key)})
                        .ref.IsValid());
        CHECK(f.On(intent, "getIntExtra", "(Ljava/lang/String;I)I", {VmValue::Ref(key), VmValue::Int(99)}).AsInt() ==
              99);
        const auto copy = f.On(intent, "getExtras", "()Landroid/os/Bundle;").ref;
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.linker.Class(f.model.ObjectClass(copy)).is_boot_dex);
        CHECK(f.On(copy, "getSerializable", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)}).ref ==
              payload);
        f.On(copy, "remove", "(Ljava/lang/String;)V", {VmValue::Ref(key)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK(f.On(copy, "isEmpty", "()Z").AsInt() == 1);
        const auto text = f.vm.NewStringUtf8("same identity");
        f.On(intent, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Ref(text)});
        CHECK(f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)})
                  .ref == text);
        CHECK_FALSE(f.vm.MarkReachable().IsMarked(payload));
        f.On(intent, "putExtra", "(Ljava/lang/String;I)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Int(42)});
        const auto boxed =
            f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)}).ref;
        CHECK(f.On(boxed, "intValue", "()I").AsInt() == 42);
        f.On(intent, "putExtra", "(Ljava/lang/String;Ljava/io/Serializable;)Landroid/content/Intent;",
             {VmValue::Ref(key), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 1);
        CHECK_FALSE(
            f.On(intent, "getSerializableExtra", "(Ljava/lang/String;)Ljava/io/Serializable;", {VmValue::Ref(key)})
                .ref.IsValid());
        f.On(intent, "removeExtra", "(Ljava/lang/String;)V", {VmValue::Ref(key)});
        CHECK(f.On(intent, "hasExtra", "(Ljava/lang/String;)Z", {VmValue::Ref(key)}).AsInt() == 0);
        // Android permits null keys. ArrayMap collision chains and live views
        // are exercised through Bundle, including the cached array reuse path.
        for (const char* name : {"Aa", "BB", "third"})
            f.On(copy, "putInt", "(Ljava/lang/String;I)V", {VmValue::Ref(f.vm.NewStringUtf8(name)), VmValue::Int(7)});
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

TEST_CASE("DVM-97 action-only Intent follows the LocalBroadcastManager match chain") {
    AndroidValueVm fixture;
    const auto action = fixture.vm.NewStringUtf8("org.example.PLANT");
    const auto intent = fixture.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
    const auto filter = fixture.New("Landroid/content/IntentFilter;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});

    CHECK(fixture.vm.StringUtf8(fixture.On(intent, "getAction", "()Ljava/lang/String;").ref) == "org.example.PLANT");
    CHECK_FALSE(fixture.On(intent, "getData", "()Landroid/net/Uri;").ref.IsValid());
    CHECK_FALSE(fixture.On(intent, "getScheme", "()Ljava/lang/String;").ref.IsValid());
    CHECK_FALSE(fixture.On(intent, "getCategories", "()Ljava/util/Set;").ref.IsValid());
    CHECK(fixture.On(intent, "getFlags", "()I").AsInt() == 0);
    CHECK_FALSE(fixture
                    .On(intent, "resolveTypeIfNeeded", "(Landroid/content/ContentResolver;)Ljava/lang/String;",
                        {VmValue::Ref(VmObjectRef{})})
                    .ref.IsValid());

    CHECK(fixture.On(filter, "countActions", "()I").AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(fixture.On(filter, "getAction", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) ==
          "org.example.PLANT");
    CHECK(fixture
              .On(filter, "match",
                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                  "Landroid/net/Uri;Ljava/util/Set;Ljava/lang/String;)I",
                  {VmValue::Ref(action), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                   VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})})
              .AsInt() == 0x00108000);

    fixture.On(intent, "setFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x04)});
    fixture.On(intent, "addFlags", "(I)Landroid/content/Intent;", {VmValue::Int(0x08)});
    CHECK(fixture.On(intent, "getFlags", "()I").AsInt() == 0x0c);
}

TEST_CASE("DVM-97 IntentFilter matches bounded MIME URI authority and categories") {
    AndroidValueVm fixture;
    const auto action = fixture.vm.NewStringUtf8("org.example.VIEW");
    const auto mime = fixture.vm.NewStringUtf8("image/png");
    const auto uri_text = fixture.vm.NewStringUtf8("content://cdn.Example.com:443/plants/pea");
    const auto uri =
        fixture.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(uri_text)})
            .ref;

    CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getScheme", "()Ljava/lang/String;").ref) == "content");
    CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getHost", "()Ljava/lang/String;").ref) == "cdn.Example.com");
    CHECK(fixture.On(uri, "getPort", "()I").AsInt() == 443);
    CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getPath", "()Ljava/lang/String;").ref) == "/plants/pea");
    CHECK(fixture.On(uri, "toString", "()Ljava/lang/String;").ref == uri_text);

    const auto intent = fixture.New("Landroid/content/Intent;", "(Ljava/lang/String;Landroid/net/Uri;)V",
                                    {VmValue::Ref(action), VmValue::Ref(uri)});
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(intent)).is_boot_dex);
    fixture.On(intent, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(uri), VmValue::Ref(mime)});
    const auto category = fixture.vm.NewStringUtf8("org.example.GREEN");
    fixture.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(category)});
    const auto categories = fixture.On(intent, "getCategories", "()Ljava/util/Set;").ref;
    REQUIRE(categories.IsValid());
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(categories)).descriptor == "Landroid/util/ArraySet;");
    CHECK(fixture.On(intent, "hasCategory", "(Ljava/lang/String;)Z", {VmValue::Ref(category)}).AsInt() == 1);
    CHECK(fixture
              .On(intent, "resolveTypeIfNeeded", "(Landroid/content/ContentResolver;)Ljava/lang/String;",
                  {VmValue::Ref(VmObjectRef{})})
              .ref == mime);

    const auto filter = fixture.New("Landroid/content/IntentFilter;", "(Ljava/lang/String;Ljava/lang/String;)V",
                                    {VmValue::Ref(action), VmValue::Ref(mime)});
    fixture.On(filter, "addDataScheme", "(Ljava/lang/String;)V", {VmValue::Ref(fixture.vm.NewStringUtf8("content"))});
    fixture.On(
        filter, "addDataAuthority", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("*.example.com")), VmValue::Ref(fixture.vm.NewStringUtf8("443"))});
    fixture.On(filter, "addCategory", "(Ljava/lang/String;)V", {VmValue::Ref(category)});
    CHECK(fixture.On(filter, "countDataTypes", "()I").AsInt() == 1);
    CHECK(fixture.On(filter, "countDataSchemes", "()I").AsInt() == 1);
    CHECK(fixture.On(filter, "countCategories", "()I").AsInt() == 1);

    const auto partial_filter = fixture.New("Landroid/content/IntentFilter;");
    fixture.On(partial_filter, "addDataType", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("image/*"))});
    CHECK(fixture
              .On(partial_filter, "hasDataType", "(Ljava/lang/String;)Z",
                  {VmValue::Ref(fixture.vm.NewStringUtf8("image/jpeg"))})
              .AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(
              fixture.On(partial_filter, "getDataType", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "image");

    const auto match = [&](const VmObjectRef requested_action, const VmObjectRef requested_type,
                           const VmObjectRef requested_uri, const VmObjectRef requested_categories) {
        const auto requested_scheme = requested_uri.IsValid()
                                          ? fixture.On(requested_uri, "getScheme", "()Ljava/lang/String;").ref
                                          : VmObjectRef{};
        return fixture
            .On(filter, "match",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                "Landroid/net/Uri;Ljava/util/Set;Ljava/lang/String;)I",
                {VmValue::Ref(requested_action), VmValue::Ref(requested_type), VmValue::Ref(requested_scheme),
                 VmValue::Ref(requested_uri), VmValue::Ref(requested_categories), VmValue::Ref(VmObjectRef{})})
            .AsInt();
    };
    CHECK(match(action, mime, uri, categories) == 0x00608000);
    CHECK(match(fixture.vm.NewStringUtf8("org.example.OTHER"), mime, uri, categories) == -3);
    CHECK(match(action, fixture.vm.NewStringUtf8("text/plain"), uri, categories) == -1);
    const auto wrong_uri = fixture
                               .Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                       {VmValue::Ref(fixture.vm.NewStringUtf8("content://example.org:443/plants/pea"))})
                               .ref;
    CHECK(match(action, mime, wrong_uri, categories) == -2);

    const auto unmatched_intent = fixture.New("Landroid/content/Intent;");
    fixture.On(unmatched_intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(fixture.vm.NewStringUtf8("org.example.BLUE"))});
    const auto unmatched_categories = fixture.On(unmatched_intent, "getCategories", "()Ljava/util/Set;").ref;
    CHECK(match(action, mime, uri, unmatched_categories) == -4);
}

TEST_CASE("DVM-132 API 19 Uri executes from BootDex on both interpreters") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto text =
            fixture.vm.NewStringUtf8("content://user@cdn.example.com:443/plants/pea%20pod?kind=snow%20pea#leaf");
        const auto uri =
            fixture.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", {VmValue::Ref(text)})
                .ref;

        const auto owner = fixture.model.ObjectClass(uri);
        CHECK(fixture.linker.Class(owner).descriptor == "Landroid/net/Uri$StringUri;");
        CHECK(fixture.linker.Class(owner).is_boot_dex);
        CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getScheme", "()Ljava/lang/String;").ref) == "content");
        CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getHost", "()Ljava/lang/String;").ref) == "cdn.example.com");
        CHECK(fixture.On(uri, "getPort", "()I").AsInt() == 443);
        CHECK(fixture.vm.StringUtf8(fixture.On(uri, "getPath", "()Ljava/lang/String;").ref) == "/plants/pea pod");
        CHECK(fixture.vm.StringUtf8(fixture
                                        .On(uri, "getQueryParameter", "(Ljava/lang/String;)Ljava/lang/String;",
                                            {VmValue::Ref(fixture.vm.NewStringUtf8("kind"))})
                                        .ref) == "snow pea");
        CHECK(fixture.vm.StringUtf8(fixture.On(uri, "toString", "()Ljava/lang/String;").ref) ==
              fixture.vm.StringUtf8(text));
        CHECK(fixture
                  .On(text, "regionMatches", "(ILjava/lang/String;II)Z",
                      {VmValue::Int(0), VmValue::Ref(text), VmValue::Int(0), VmValue::Int(-1)})
                  .AsInt() == 1);

        const auto builder = fixture.On(uri, "buildUpon", "()Landroid/net/Uri$Builder;").ref;
        REQUIRE(builder.IsValid());
        fixture.On(builder, "appendPath", "(Ljava/lang/String;)Landroid/net/Uri$Builder;",
                   {VmValue::Ref(fixture.vm.NewStringUtf8("winter mint"))});
        fixture.On(builder, "appendQueryParameter", "(Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri$Builder;",
                   {VmValue::Ref(fixture.vm.NewStringUtf8("level")), VmValue::Ref(fixture.vm.NewStringUtf8("1+2"))});
        const auto built = fixture.On(builder, "build", "()Landroid/net/Uri;").ref;
        CHECK(fixture.vm.StringUtf8(fixture.On(built, "toString", "()Ljava/lang/String;").ref) ==
              "content://user@cdn.example.com:443/plants/pea%20pod/winter%20mint"
              "?kind=snow%20pea&level=1%2B2#leaf");

        const auto encoded = fixture
                                 .Static("Landroid/net/Uri;", "encode", "(Ljava/lang/String;)Ljava/lang/String;",
                                         {VmValue::Ref(fixture.vm.NewStringUtf8("雪 pea/+"))})
                                 .ref;
        CHECK(fixture.vm.StringUtf8(encoded) == "%E9%9B%AA%20pea%2F%2B");
        CHECK(fixture.vm.StringUtf8(fixture
                                        .Static("Landroid/net/Uri;", "decode", "(Ljava/lang/String;)Ljava/lang/String;",
                                                {VmValue::Ref(encoded)})
                                        .ref) == "雪 pea/+");
    }
}

TEST_CASE("DVM-97 dynamic content MIME and malformed filters fail explicitly") {
    AndroidValueVm fixture;
    const auto content_uri = fixture
                                 .Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                         {VmValue::Ref(fixture.vm.NewStringUtf8("content://plants/1"))})
                                 .ref;
    const auto intent = fixture.New("Landroid/content/Intent;");
    fixture.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(content_uri)});
    const auto resolver =
        fixture
            .On(fixture.New("Landroid/content/Context;"), "getContentResolver", "()Landroid/content/ContentResolver;")
            .ref;
    auto outcome = fixture.OnOutcome(intent, "resolveTypeIfNeeded",
                                     "(Landroid/content/ContentResolver;)Ljava/lang/String;", {VmValue::Ref(resolver)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK_FALSE(outcome.value.ref.IsValid());  // ROM ContentResolver logs the rejected external lookup.
    CHECK_FALSE(fixture.ledger.Unimplemented().empty());

    const auto explicit_intent = fixture.New("Landroid/content/Intent;");
    fixture.On(explicit_intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(content_uri)});
    fixture.On(explicit_intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
               {VmValue::Ref(fixture.vm.NewStringUtf8("org.example")),
                VmValue::Ref(fixture.vm.NewStringUtf8("org.example.Target"))});
    outcome = fixture.OnOutcome(explicit_intent, "resolveTypeIfNeeded",
                                "(Landroid/content/ContentResolver;)Ljava/lang/String;", {VmValue::Ref(VmObjectRef{})});
    CHECK_FALSE(outcome.exception.IsValid());
    CHECK_FALSE(outcome.value.ref.IsValid());

    const auto filter = fixture.New("Landroid/content/IntentFilter;");
    outcome = fixture.OnOutcome(filter, "addDataType", "(Ljava/lang/String;)V",
                                {VmValue::Ref(fixture.vm.NewStringUtf8("not-a-mime"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
          "Landroid/content/IntentFilter$MalformedMimeTypeException;");
}

TEST_CASE("DVM-107 ComponentName Java value and Parcel roundtrip") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto component = f.New(
            "Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.sub.Main$Nested"))});
        CHECK(f.vm.StringUtf8(f.On(component, "getShortClassName", "()Ljava/lang/String;").ref) == ".sub.Main$Nested");
        CHECK(f.vm.StringUtf8(f.On(component, "flattenToShortString", "()Ljava/lang/String;").ref) ==
              "fixture/.sub.Main$Nested");
        const auto flat = f.On(component, "flattenToString", "()Ljava/lang/String;").ref;
        const auto parsed = f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                                     "(Ljava/lang/String;)Landroid/content/ComponentName;", {VmValue::Ref(flat)})
                                .ref;
        CHECK(f.On(component, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(parsed)}).AsInt() == 1);
        CHECK(f.On(component, "compareTo", "(Landroid/content/ComponentName;)I", {VmValue::Ref(parsed)}).AsInt() == 0);
        CHECK(f.On(component, "hashCode", "()I").AsInt() == f.On(parsed, "hashCode", "()I").AsInt());
        CHECK_FALSE(f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                             "(Ljava/lang/String;)Landroid/content/ComponentName;",
                             {VmValue::Ref(f.vm.NewStringUtf8("invalid"))})
                        .ref.IsValid());
        const auto shortened = f.Static("Landroid/content/ComponentName;", "unflattenFromString",
                                        "(Ljava/lang/String;)Landroid/content/ComponentName;",
                                        {VmValue::Ref(f.vm.NewStringUtf8("fixture/.Main"))})
                                   .ref;
        CHECK(f.vm.StringUtf8(f.On(shortened, "getClassName", "()Ljava/lang/String;").ref) == "fixture.Main");
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        f.On(component, "writeToParcel", "(Landroid/os/Parcel;I)V", {VmValue::Ref(parcel), VmValue::Int(0)});
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto restored =
            f.New("Landroid/content/ComponentName;", "(Landroid/os/Parcel;)V", {VmValue::Ref(parcel)});
        CHECK(f.On(component, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(restored)}).AsInt() == 1);
        CHECK(f.On(component, "describeContents", "()I").AsInt() == 0);
        f.On(parcel, "setDataPosition", "(I)V", {VmValue::Int(0)});
        const auto type = f.model.ObjectClass(component);
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field.has_value());
        const auto creator = VmObjectRef(f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]);
        const auto via_creator =
            f.On(creator, "createFromParcel", "(Landroid/os/Parcel;)Ljava/lang/Object;", {VmValue::Ref(parcel)}).ref;
        CHECK(f.On(component, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(via_creator)}).AsInt() == 1);
        const auto array = f.On(creator, "newArray", "(I)[Ljava/lang/Object;", {VmValue::Int(2)}).ref;
        CHECK(f.model.ArrayLength(array) == 2);
        CHECK(f.linker.Class(f.model.ObjectClass(array)).descriptor == "[Landroid/content/ComponentName;");
        f.On(parcel, "recycle", "()V");
        const auto output = f.New("Ljava/io/StringWriter;");
        const auto printer = f.New("Ljava/io/PrintWriter;", "(Ljava/io/Writer;)V", {VmValue::Ref(output)});
        f.Static("Landroid/content/ComponentName;", "printShortString",
                 "(Ljava/io/PrintWriter;Ljava/lang/String;Ljava/lang/String;)V",
                 {VmValue::Ref(printer), VmValue::Ref(f.vm.NewStringUtf8("fixture")),
                  VmValue::Ref(f.vm.NewStringUtf8("fixture.Main"))});
        CHECK(f.vm.StringUtf8(f.On(output, "toString", "()Ljava/lang/String;").ref) == "fixture/.Main");
        CHECK(f.On(printer, "checkError", "()Z").AsInt() == 0);
    }
}

TEST_CASE("ROM IntentSender carries the PendingIntent Binder identity") {
    AndroidValueVm f;
    f.context->package_name = "fixture";
    const auto base = f.New("Landroid/content/Context;");
    const auto base_root = f.vm.ProtectReferences(std::array{base});
    const auto intent = f.New("Landroid/content/Intent;");
    const auto intent_root = f.vm.ProtectReferences(std::array{intent});
    const auto pending = f.Static("Landroid/app/PendingIntent;", "getBroadcast",
                                  "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;",
                                  {VmValue::Ref(base), VmValue::Int(0), VmValue::Ref(intent), VmValue::Int(0)})
                             .ref;
    const auto pending_root = f.vm.ProtectReferences(std::array{pending});
    const auto sender = f.On(pending, "getIntentSender", "()Landroid/content/IntentSender;").ref;
    REQUIRE(sender.IsValid());
    const auto sender_root = f.vm.ProtectReferences(std::array{sender});
    CHECK(f.linker.Class(f.model.ObjectClass(sender)).is_boot_dex);
    const auto other = f.On(pending, "getIntentSender", "()Landroid/content/IntentSender;").ref;
    CHECK(f.On(sender, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(other)}).AsInt() == 1);
}

TEST_CASE("DVM-265 DEX IntentFilter actions survive Parcelable dispatch") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::android_value::AndroidValueVm f(backend, {}, false, "rom_java_replacement.dex");
        const auto action = f.vm.NewStringUtf8("test.ROM");
        const auto roots = f.vm.ProtectReferences(std::array{action});
        CHECK(
            f.Static("Ltest/RomJavaReplacement;", "filter", "(Ljava/lang/String;)Z", {VmValue::Ref(action)}).AsInt() ==
            1);
        CHECK(f.linker.Class(f.linker.ResolveDescriptor("Landroid/content/IntentFilter;")).is_boot_dex);
    }
}
