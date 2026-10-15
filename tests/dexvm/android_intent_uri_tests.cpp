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
