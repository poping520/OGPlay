#include "runtime/integration/dexvm_android/shared.h"
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

constexpr int kMatchDefaultOnly = 0x10000;
constexpr int kGetDisabledComponents = 0x200;

void SetIntentComponent(AndroidValueVm& f, const VmObjectRef intent, const char* package, const char* name) {
    f.On(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
         {VmValue::Ref(f.vm.NewStringUtf8(package)), VmValue::Ref(f.vm.NewStringUtf8(name))});
}

int QueryListSize(AndroidValueVm& f, const VmCallOutcome& outcome) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return f.On(outcome.value.ref, "size", "()I").AsInt();
}

VmObjectRef OnlyQueryResult(AndroidValueVm& f, const VmCallOutcome& outcome) {
    const auto root = f.vm.ProtectReferences(std::array{outcome.value.ref});
    REQUIRE(QueryListSize(f, outcome) == 1);
    return f.On(outcome.value.ref, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref;
}

void CheckExplicitResolveInfo(AndroidValueVm& f, const VmObjectRef result, const char* expected_name,
                              const bool service = false) {
    REQUIRE(result.IsValid());
    const auto root = f.vm.ProtectReferences(std::array{result});
    const auto type = f.model.ObjectClass(result);
    CHECK(f.linker.Class(type).descriptor == "Landroid/content/pm/ResolveInfo;");
    const auto field =
        f.linker.FindFieldRecursive(type, service ? "serviceInfo" : "activityInfo",
                                    service ? "Landroid/content/pm/ServiceInfo;" : "Landroid/content/pm/ActivityInfo;");
    REQUIRE(field.has_value());
    const auto info_slot = f.linker.Field(*field).slot;
    const auto defaults = f.New("Landroid/content/pm/ResolveInfo;");
    const auto actual = f.model.InstanceSlots(result);
    const auto expected = f.model.InstanceSlots(defaults);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (i != info_slot) CHECK(actual[i].bits == expected[i].bits);
    }
    const auto info = VmObjectRef{static_cast<std::uint32_t>(actual[info_slot].bits)};
    REQUIRE(info.IsValid());
    const auto name = f.linker.FindFieldRecursive(f.model.ObjectClass(info), "name", "Ljava/lang/String;");
    REQUIRE(name.has_value());
    CHECK(f.vm.StringUtf8(VmObjectRef{static_cast<std::uint32_t>(
              f.model.InstanceSlots(info)[f.linker.Field(*name).slot].bits)}) == expected_name);
}

}  // namespace

TEST_CASE("DVM-112 resolveService distinguishes absent candidates from unsupported queries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto base = f.New("Landroid/content/Context;");
        const auto manager = f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                  {VmValue::Ref(f.vm.NewStringUtf8("example.SERVICE"))});
        const auto roots = f.vm.ProtectReferences(std::array{base, manager, intent});
        auto query = [&](VmObjectRef value, int flags = 0) {
            return f.OnOutcome(manager, "resolveService", "(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;",
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
        const auto query_all = [&](VmObjectRef value, int flags = 0) {
            return f.OnOutcome(manager, "queryIntentServices", "(Landroid/content/Intent;I)Ljava/util/List;",
                               {VmValue::Ref(value), VmValue::Int(flags)});
        };
        fail(query_all(VmObjectRef{}), "Ljava/lang/NullPointerException;");
        fail(query(VmObjectRef{}), "Ljava/lang/NullPointerException;");
        fail(query(intent));  // Missing inventory is not an empty installed-service list.
        f.context->service_inventory_known = true;
        absent();
        const auto services = query_all(intent, 0x40);
        REQUIRE_FALSE(services.exception.IsValid());
        CHECK(f.On(services.value.ref, "size", "()I").AsInt() == 0);
        fail(query_all(intent, 1));
        const auto filtered = query(intent, 0x40);
        REQUIRE_FALSE(filtered.exception.IsValid());
        CHECK_FALSE(filtered.value.ref.IsValid());
        f.context->service_components = {
            {"example.Plain", true, {}},
            {"example.Other", true, {{{"example.OTHER"}, {}, false}}},
            {"example.Disabled", false, {{{"example.SERVICE"}, {}, false}}},
        };
        absent();
        CHECK(f.ledger.Unimplemented().size() == 1);
        CHECK(f.ledger.Unimplemented()[0].id == "dexvm.service_resolution");
        CHECK(f.ledger.Unimplemented()[0].count == 2);
        f.context->service_components.push_back(
            {"example.Candidate", true, {{{"example.SERVICE"}, {"example.EXTRA"}, false}}});
        fail(query_all(intent));
        fail(query(intent));        // An Intent without categories can match a filter with categories.
        fail(query(intent, 0x40));  // Projection flags do not pretend a positive match is absent.
        f.context->service_components.back().intent_filters[0].has_data = true;
        fail(query(intent));  // Data constraints cannot be silently discarded.
        f.context->application_enabled = false;
        absent();
        f.context->application_enabled = true;
        f.context->service_components.clear();
        for (const int flags : {1, 128, -1})
            fail(query(intent, flags));
        const auto empty = f.New("Landroid/content/Intent;");
        fail(query(empty));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("text/plain"))});
        fail(query(intent));
        f.On(intent, "setType", "(Ljava/lang/String;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("example.CATEGORY"))});
        fail(query(intent));
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("example.CATEGORY"))});
        const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("content://example/value"))})
                             .ref;
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(uri)});
        fail(query(intent));
        f.On(intent, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", {VmValue::Ref(VmObjectRef{})});
        f.On(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("example")), VmValue::Ref(f.vm.NewStringUtf8("example.Local"))});
        fail(query(intent));
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
             {VmValue::Ref(VmObjectRef{})});
        static_cast<void>(f.vm.CollectGarbage());
        absent();
        CHECK(std::ranges::any_of(f.ledger.Unimplemented(), [](const auto& hit) {
            return hit.id == "dexvm.service_resolution" && hit.count == 14;
        }));

        f.context->package_name = "example";
        f.context->service_components = {{"example.Local", true, {}}, {"example.Disabled", false, {}}};
        SetIntentComponent(f, intent, "example", "example.Local");
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query_all(intent, kMatchDefaultOnly)), "example.Local", true);
        const auto resolved = query(intent, kMatchDefaultOnly);
        REQUIRE_MESSAGE(!resolved.exception.IsValid(), resolved.exception_message);
        CheckExplicitResolveInfo(f, resolved.value.ref, "example.Local", true);
        fail(query(intent, kMatchDefaultOnly | 0x40));
        SetIntentComponent(f, intent, "example", "example.Absent");
        CHECK(QueryListSize(f, query_all(intent, kMatchDefaultOnly)) == 0);
        absent();
        SetIntentComponent(f, intent, "example", "example.Disabled");
        CHECK(QueryListSize(f, query_all(intent, kMatchDefaultOnly)) == 0);
        absent();
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query_all(intent, kMatchDefaultOnly | kGetDisabledComponents)),
                                 "example.Disabled", true);
        f.context->service_inventory_known = false;
        fail(query_all(intent, kMatchDefaultOnly));
        f.context->service_inventory_known = true;
        SetIntentComponent(f, intent, "external", "external.Service");
        const auto external = query_all(intent, kMatchDefaultOnly);
        fail(external);
        CHECK(external.exception_message == "external package inventory is unavailable");
    }
}

TEST_CASE("DVM-144 failed service binding retains a Context owned dispatcher until unbind") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        int callbacks = 0;
        auto declaration = IntrinsicClassBuilder::Class("Ltest/AbsentConnection;", "Ljava/lang/Object;",
                                                        {"Landroid/content/ServiceConnection;"});
        declaration.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        const auto callback = [&callbacks](IntrinsicContext&) {
            ++callbacks;
            return VmValue::Void();
        };
        declaration.VirtualMethod("onServiceConnected", "(Landroid/content/ComponentName;Landroid/os/IBinder;)V",
                                  callback);
        declaration.VirtualMethod("onServiceDisconnected", "(Landroid/content/ComponentName;)V", callback);
        AndroidValueVm f(backend, {std::move(declaration).Build()});
        const auto base = f.New("Landroid/content/Context;");
        const auto wrapper =
            f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto connection = f.New("Ltest/AbsentConnection;");
        const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
                                  {VmValue::Ref(f.vm.NewStringUtf8("example.ABSENT"))});
        const auto roots = f.vm.ProtectReferences(std::array{base, wrapper, intent});
        f.context->service_inventory_known = true;
        for (int i = 0; i < 2; ++i)
            CHECK(f.On(wrapper, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
                       {VmValue::Ref(intent), VmValue::Ref(connection), VmValue::Int(1)})
                      .AsInt() == 0);
        CHECK(f.vm.MarkReachable().IsMarked(connection));
        CHECK(callbacks == 0);
        static_cast<void>(f.vm.CollectGarbage("absent-bind"));
        CHECK(f.vm.MarkReachable().IsMarked(connection));
        f.On(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});

        const auto twice =
            f.OnOutcome(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
        REQUIRE(twice.exception.IsValid());
        CHECK(f.linker.Class(twice.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        f.context->service_components = {{"example.Local", true, {{{"example.ABSENT"}, {}, false}}}};
        const auto local =
            f.OnOutcome(wrapper, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
                        {VmValue::Ref(intent), VmValue::Ref(connection), VmValue::Int(1)});
        REQUIRE(local.exception.IsValid());
        CHECK(f.linker.Class(local.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.On(wrapper, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
    }
}

TEST_CASE("DVM-193 receiver queries match independent filters and return BootDex results") {
    using ogplay::loader::AndroidManifestIntentFilter;
    using ogplay::loader::AndroidManifestReceiverComponent;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.game";
        f.context->receiver_inventory_known = true;
        const AndroidManifestIntentFilter plain{.actions = {"RECEIVE"},
                                                .categories = {"org.example.game", "EXTRA"},
                                                .priority = 5,
                                                .label = std::string("Receiver label"),
                                                .icon = 0x7f030001U};
        auto default_filter = plain;
        default_filter.categories.push_back("android.intent.category.DEFAULT");
        default_filter.label = std::uint32_t{0x7f050001U};
        auto high = plain;
        high.priority = 10;
        auto negative = plain;
        negative.priority = -7;
        auto disabled = AndroidManifestReceiverComponent{
            .name = "org.example.game.Disabled", .enabled = false, .intent_filters = {high}};
        f.context->receiver_components = {
            {.name = "org.example.game.First",
             .intent_filters = {plain, high},
             .process_name = "org.example.game:receiver",
             .permission = "org.example.PROTECTED"},
            {.name = "org.example.game.Tie", .intent_filters = {plain}},
            {.name = "org.example.game.Default", .exported = false, .intent_filters = {default_filter}},
            {.name = "org.example.game.High", .intent_filters = {high}},
            {.name = "org.example.game.Negative", .intent_filters = {negative}},
            {.name = "org.example.game.Split",
             .intent_filters = {{.actions = {"RECEIVE"}, .categories = {"org.example.game"}},
                                {.actions = {"RECEIVE"}, .categories = {"EXTRA"}},
                                {.actions = {"OTHER"}, .categories = {"org.example.game", "EXTRA"}}}},
            disabled};
        f.context->receiver_components.back().intent_filters.front().has_data = true;
        auto repeated = f.context->receiver_components.front();
        repeated.intent_filters = {high};
        repeated.intent_filters.front().has_data = true;
        repeated.process_name = "org.example.game:later";
        f.context->receiver_components.insert(f.context->receiver_components.end() - 1, repeated);
        const auto manager = ogplay::runtime::android_intrinsics::CallAndroidMethod(
                                 f.vm, ogplay::test::MakeRomAppContext(f.vm, f.context), "getPackageManager",
                                 "()Landroid/content/pm/PackageManager;")
                                 .ref;
        const auto intent =
            f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("RECEIVE"))});
        const auto roots = f.vm.ProtectReferences(std::array{manager, intent});
        const auto add_category = [&](const char* category) {
            f.On(intent, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;",
                 {VmValue::Ref(f.vm.NewStringUtf8(category))});
        };
        add_category("org.example.game");
        add_category("EXTRA");
        const auto query = [&](VmObjectRef value, int flags = 0) {
            return f.OnOutcome(manager, "queryBroadcastReceivers", "(Landroid/content/Intent;I)Ljava/util/List;",
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
        CHECK(field(item(2), "priority", "I") == 5U);  // first matching filter, not max priority
        CHECK(field(item(1), "isDefault", "Z") == 1U);
        CHECK(field(item(2), "isDefault", "Z") == 0U);  // broadcasts need no DEFAULT
        CHECK(field(item(1), "labelRes", "I") == 0x7f050001U);
        CHECK(field(item(2), "icon", "I") == 0x7f030001U);
        CHECK(f.vm.StringUtf8(ref_field(item(2), "nonLocalizedLabel", "Ljava/lang/CharSequence;")) == "Receiver label");
        const auto first_info = ref_field(item(2), "activityInfo", "Landroid/content/pm/ActivityInfo;");
        CHECK(f.vm.StringUtf8(ref_field(first_info, "permission", "Ljava/lang/String;")) == "org.example.PROTECTED");
        CHECK(f.vm.StringUtf8(ref_field(first_info, "processName", "Ljava/lang/String;")) ==
              "org.example.game:receiver");
        CHECK(field(first_info, "exported", "Z") == 1U);
        CHECK(field(ref_field(item(1), "activityInfo", "Landroid/content/pm/ActivityInfo;"), "exported", "Z") == 0U);
        const auto default_only = OnlyQueryResult(f, query(intent, kMatchDefaultOnly));
        const auto default_info = ref_field(default_only, "activityInfo", "Landroid/content/pm/ActivityInfo;");
        CHECK(f.vm.StringUtf8(ref_field(default_info, "name", "Ljava/lang/String;")) == "org.example.game.Default");
        CHECK(field(default_only, "isDefault", "Z") == 1U);
        static_cast<void>(f.vm.CollectGarbage("receiver-query-results"));
        CHECK(size(list) == 5);
        CHECK(field(item(2), "priority", "I") == 5U);
        f.context->application_enabled = false;
        CHECK(size(query(intent).value.ref) == 0);
        f.context->application_enabled = true;
        add_category("MISSING");
        CHECK(size(query(intent).value.ref) == 0);
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("MISSING"))});
        f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("ABSENT"))});
        CHECK(size(query(intent).value.ref) == 0);
        f.On(intent, "setAction", "(Ljava/lang/String;)Landroid/content/Intent;",
             {VmValue::Ref(f.vm.NewStringUtf8("RECEIVE"))});
        f.On(intent, "removeCategory", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("EXTRA"))});
        CHECK(size(query(intent).value.ref) == 6);  // Split now matches its first filter

        const auto unsupported = [&](const VmCallOutcome& outcome) {
            CHECK(outcome.exception_class == f.linker.ResolveDescriptor("Ljava/lang/UnsupportedOperationException;"));
        };
        for (const int flags : {0x80, 0x200, kMatchDefaultOnly | 0x80, -1})
            unsupported(query(intent, flags));
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
             {std::tuple{"setPackage", "(Ljava/lang/String;)Landroid/content/Intent;",
                         f.vm.NewStringUtf8("other.package")},
              std::tuple{"setType", "(Ljava/lang/String;)Landroid/content/Intent;", f.vm.NewStringUtf8("text/plain")},
              std::tuple{"setData", "(Landroid/net/Uri;)Landroid/content/Intent;",
                         f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("content://example/item"))})
                             .ref}}) {
            f.On(intent, setter, signature, {VmValue::Ref(value)});
            unsupported(query(intent));
            f.On(intent, setter, signature, {VmValue::Ref(VmObjectRef{})});
        }
        f.On(intent, "setSelector", "(Landroid/content/Intent;)V", {VmValue::Ref(empty_intent)});
        unsupported(query(intent));
        SetIntentComponent(f, empty_intent, "org.example.game", "org.example.game.Default");
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query(intent, kMatchDefaultOnly)), "org.example.game.Default");
        SetIntentComponent(f, intent, "org.example.game", "org.example.game.First");
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query(intent, kMatchDefaultOnly)), "org.example.game.First");
        SetIntentComponent(f, intent, "org.example.game", "org.example.game.Missing");
        CHECK(QueryListSize(f, query(intent, kMatchDefaultOnly)) == 0);  // Outer component wins over selector.
        SetIntentComponent(f, intent, "org.example.game", "org.example.game.Disabled");
        CHECK(QueryListSize(f, query(intent, kMatchDefaultOnly)) == 0);
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query(intent, kMatchDefaultOnly | kGetDisabledComponents)),
                                 "org.example.game.Disabled");
        f.context->receiver_inventory_known = false;
        unsupported(query(intent, kMatchDefaultOnly));
        f.context->receiver_inventory_known = true;
        SetIntentComponent(f, intent, "external", "external.Receiver");
        const auto external = query(intent, kMatchDefaultOnly);
        unsupported(external);
        CHECK(external.exception_message == "external package inventory is unavailable");
        f.On(intent, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
             {VmValue::Ref(VmObjectRef{})});
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
        CHECK(std::any_of(hits.begin(), hits.end(),
                          [&](const auto& hit) { return hit.id == "dexvm.receiver_query" && hit.count > 0; }));
    }
}

TEST_CASE("PackageManager queryIntentActivities resolves explicit local components") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "fixture";
        f.context->activity_inventory_known = true;
        f.context->activity_components = {{.name = "fixture.Local"}, {.name = "fixture.Disabled", .enabled = false}};
        const auto manager = ogplay::runtime::android_intrinsics::CallAndroidMethod(
                                 f.vm, ogplay::test::MakeRomAppContext(f.vm, f.context), "getPackageManager",
                                 "()Landroid/content/pm/PackageManager;")
                                 .ref;
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto intent = f.New("Landroid/content/Intent;");
        const auto intent_root = f.vm.ProtectReferences(std::array{intent});
        const auto query = [&](int flags = kMatchDefaultOnly) {
            return f.OnOutcome(manager, "queryIntentActivities", "(Landroid/content/Intent;I)Ljava/util/List;",
                               {VmValue::Ref(intent), VmValue::Int(flags)});
        };
        const auto unsupported = [&](const VmCallOutcome& outcome) {
            CHECK(outcome.exception_class == f.linker.ResolveDescriptor("Ljava/lang/UnsupportedOperationException;"));
        };
        const auto implicit = query();
        unsupported(implicit);
        CHECK(implicit.exception_message == "queryIntentActivities requires an explicit component");
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.activity_query");
        CHECK(hits[0].count == 1);

        SetIntentComponent(f, intent, "fixture", "fixture.Local");
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query()), "fixture.Local");
        unsupported(query(kMatchDefaultOnly | 1));
        f.context->activity_inventory_known = false;
        unsupported(query());
        f.context->activity_inventory_known = true;
        SetIntentComponent(f, intent, "fixture", "fixture.Missing");
        CHECK(QueryListSize(f, query()) == 0);
        SetIntentComponent(f, intent, "fixture", "fixture.Disabled");
        CHECK(QueryListSize(f, query()) == 0);
        CheckExplicitResolveInfo(f, OnlyQueryResult(f, query(kMatchDefaultOnly | kGetDisabledComponents)),
                                 "fixture.Disabled");
        SetIntentComponent(f, intent, "external", "external.Activity");
        const auto external = query();
        unsupported(external);
        CHECK(external.exception_message == "external package inventory is unavailable");
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
        const auto manager = ogplay::runtime::android_intrinsics::CallAndroidMethod(
                                 f.vm, ogplay::test::MakeRomAppContext(f.vm, f.context), "getPackageManager",
                                 "()Landroid/content/pm/PackageManager;")
                                 .ref;
        const auto name =
            f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                  {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.Native"))});
        const auto root = f.vm.ProtectReferences(std::array{manager, name});
        const auto query = [&](int flags) {
            return f.OnOutcome(manager, "getActivityInfo",
                               "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;",
                               {VmValue::Ref(name), VmValue::Int(flags)});
        };
        const auto field = [&](VmObjectRef object, const char* name, const char* type) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(id.has_value());
            return VmObjectRef(
                static_cast<std::uint32_t>(f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits));
        };
        const auto plain = query(0);
        REQUIRE_FALSE(plain.exception.IsValid());
        CHECK_FALSE(field(plain.value.ref, "metaData", "Landroid/os/Bundle;").IsValid());
        const auto full = query(0x80);
        REQUIRE_FALSE(full.exception.IsValid());
        const auto metadata = field(full.value.ref, "metaData", "Landroid/os/Bundle;");
        const auto library = f.On(metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                                  {VmValue::Ref(f.vm.NewStringUtf8("android.app.lib_name"))})
                                 .ref;
        CHECK(f.vm.StringUtf8(library) == "activity");
        const auto app = field(full.value.ref, "applicationInfo", "Landroid/content/pm/ApplicationInfo;");
        CHECK(f.vm.StringUtf8(field(app, "nativeLibraryDir", "Ljava/lang/String;")) == "/data/app-lib");
        component.launch_mode = 2;
        component.meta_data.front().value = std::string("last declaration");
        f.context->activity_components.push_back(component);
        const auto duplicate = query(0x80);
        REQUIRE_FALSE(duplicate.exception.IsValid());
        const auto duplicate_metadata = field(duplicate.value.ref, "metaData", "Landroid/os/Bundle;");
        CHECK(f.vm.StringUtf8(f.On(duplicate_metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                                   {VmValue::Ref(f.vm.NewStringUtf8("android.app.lib_name"))})
                                  .ref) == "last declaration");
        const auto mode = f.linker.FindFieldRecursive(f.model.ObjectClass(duplicate.value.ref), "launchMode", "I");
        REQUIRE(mode.has_value());
        CHECK(f.model.InstanceSlots(duplicate.value.ref)[f.linker.Field(*mode).slot].bits == 2);
        const auto package_info =
            f.On(manager, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
                 {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Int(1)})
                .ref;
        const auto activities = field(package_info, "activities", "[Landroid/content/pm/ActivityInfo;");
        REQUIRE(f.model.ArrayLength(activities) == 2);
        CHECK(f.model.InstanceSlots(f.model.GetObjectElement(activities, 0))[f.linker.Field(*mode).slot].bits == 0);
        CHECK(f.model.InstanceSlots(f.model.GetObjectElement(activities, 1))[f.linker.Field(*mode).slot].bits == 2);
        CHECK(query(1).exception.IsValid());
        f.context->activity_components.back().enabled = false;
        CHECK(query(0).exception.IsValid());
        CHECK_FALSE(query(0x200).exception.IsValid());
        auto alias = component;
        alias.kind = ogplay::loader::AndroidManifestComponentKind::activity_alias;
        alias.name = "fixture.Alias";
        alias.target_activity = component.name;
        alias.launch_mode = 1;  // Snapshot differs from the registered target.
        f.context->activity_components.push_back(alias);
        const auto alias_name =
            f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
                  {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.Alias"))});
        const auto alias_info =
            f.On(manager, "getActivityInfo", "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;",
                 {VmValue::Ref(alias_name), VmValue::Int(0)})
                .ref;
        CHECK(f.model.InstanceSlots(alias_info)[f.linker.Field(*mode).slot].bits == 1);
        f.context->activity_inventory_known = false;
        CHECK(query(0).exception.IsValid());
    }
}

TEST_CASE("DVM-263 ROM Application keeps component callbacks on the guest object") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto app = f.New("Landroid/app/Application;");
        const auto root = f.vm.ProtectReferences(std::array{app});
        CHECK(f.linker.Class(f.model.ObjectClass(app)).is_boot_dex);
        for (const auto method : {"registerComponentCallbacks", "unregisterComponentCallbacks"})
            REQUIRE_FALSE(f.OnOutcome(app, method, "(Landroid/content/ComponentCallbacks;)V", {VmValue::Ref(app)})
                              .exception.IsValid());
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("ROM ActivityThread initializes its application thread and instrumentation state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "test.application";
        const auto thread = f.New("Landroid/app/ActivityThread;");
        const auto root = f.vm.ProtectReferences(std::array{thread});
        CHECK(f.linker.Class(f.model.ObjectClass(thread)).is_boot_dex);
        const auto app_thread =
            f.On(thread, "getApplicationThread", "()Landroid/app/ActivityThread$ApplicationThread;").ref;
        REQUIRE(app_thread.IsValid());
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/app/IApplicationThread;"),
                                    f.model.ObjectClass(app_thread)));
        const auto base = f.New("Landroid/content/Context;");
        const auto info = f.On(base, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        const auto info_root = f.vm.ProtectReferences(std::array{info});
        const auto compat_type = f.linker.ResolveDescriptor("Landroid/content/res/CompatibilityInfo;");
        REQUIRE_FALSE(f.vm.EnsureClassInitialized(compat_type).exception.IsValid());
        const auto compat_field = f.linker.FindFieldRecursive(compat_type, "DEFAULT_COMPATIBILITY_INFO",
                                                              "Landroid/content/res/CompatibilityInfo;");
        REQUIRE(compat_field);
        const auto compat = VmObjectRef{f.linker.Class(compat_type).static_storage[f.linker.Field(*compat_field).slot]};
        const auto loaded = f.New("Landroid/app/LoadedApk;",
                                  "(Landroid/app/ActivityThread;Landroid/content/pm/ApplicationInfo;"
                                  "Landroid/content/res/CompatibilityInfo;Ljava/lang/ClassLoader;ZZ)V",
                                  {VmValue::Ref(thread), VmValue::Ref(info), VmValue::Ref(compat),
                                   VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Int(0)});
        const auto loaded_root = f.vm.ProtectReferences(std::array{loaded});
        const auto app_context =
            f.Static("Landroid/app/ContextImpl;", "createAppContext",
                     "(Landroid/app/ActivityThread;Landroid/app/LoadedApk;)Landroid/app/ContextImpl;",
                     {VmValue::Ref(thread), VmValue::Ref(loaded)})
                .ref;
        REQUIRE(app_context.IsValid());
        const auto app_root = f.vm.ProtectReferences(std::array{app_context});
        CHECK(f.vm.StringUtf8(f.On(app_context, "getPackageName", "()Ljava/lang/String;").ref) ==
              f.context->package_name);
        CHECK(f.On(app_context, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref == info);
        const auto files = f.On(app_context, "getFilesDir", "()Ljava/io/File;").ref;
        CHECK(f.vm.StringUtf8(f.On(files, "getPath", "()Ljava/lang/String;").ref) ==
              "/data/data/" + f.context->package_name + "/files");
    }
}

TEST_CASE("ROM ActivityThread attaches to the local activity service and binds an APK") {
    int received = 0;
    int sticky_received = 0;
    int created = 0, started = 0, destroyed = 0, bound = 0, unbound = 0, connected = 0;
    auto service_class = IntrinsicClassBuilder::Class("Ltest/RomService;", "Landroid/app/Service;");
    service_class.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
    service_class.OverrideMethod("onCreate", "()V", [&](IntrinsicContext& call) {
        const auto slot = call.vm.Linker().FindVtableIndex(call.vm.Model().ObjectClass(call.receiver), "getBaseContext",
                                                           "()Landroid/content/Context;");
        REQUIRE(slot);
        const auto outcome =
            call.vm.Call(call.vm.Linker().Class(call.vm.Model().ObjectClass(call.receiver)).vtable[*slot],
                         std::array{VmValue::Ref(call.receiver)});
        REQUIRE_FALSE(outcome.exception.IsValid());
        CHECK(call.vm.Linker().Class(call.vm.Model().ObjectClass(outcome.value.ref)).descriptor ==
              "Landroid/app/ContextImpl;");
        ++created;
        return VmValue::Void();
    });
    service_class.OverrideMethod("onStartCommand", "(Landroid/content/Intent;II)I", [&](IntrinsicContext& call) {
        CHECK(call.arguments[2].AsInt() == ++started);
        return VmValue::Int(2);
    });
    service_class.OverrideMethod("onDestroy", "()V", [&](IntrinsicContext&) {
        ++destroyed;
        return VmValue::Void();
    });
    service_class.OverrideMethod(
        "onBind", "(Landroid/content/Intent;)Landroid/os/IBinder;", [&](IntrinsicContext& call) {
            ++bound;
            const auto binder = call.vm.NewIntrinsicInstance("Landroid/os/Binder;");
            const auto root = call.vm.ProtectReferences(std::array{binder});
            const auto init = call.vm.Linker().FindDirectMethod(call.vm.Model().ObjectClass(binder), "<init>", "()V");
            REQUIRE(init);
            const auto result = call.vm.Call(*init, std::array{VmValue::Ref(binder)});
            REQUIRE_FALSE(result.exception.IsValid());
            return VmValue::Ref(binder);
        });
    service_class.OverrideMethod("onUnbind", "(Landroid/content/Intent;)Z", [&](IntrinsicContext&) {
        ++unbound;
        return VmValue::Int(0);
    });
    auto connection_class = IntrinsicClassBuilder::Class("Ltest/RomConnection;", "Ljava/lang/Object;",
                                                         {"Landroid/content/ServiceConnection;"});
    connection_class.VirtualMethod("onServiceConnected", "(Landroid/content/ComponentName;Landroid/os/IBinder;)V",
                                   [&](IntrinsicContext& call) {
                                       CHECK(call.arguments[1].ref.IsValid());
                                       ++connected;
                                       return VmValue::Void();
                                   });
    auto receiver_class = IntrinsicClassBuilder::Class("Ltest/RomReceiver;", "Landroid/content/BroadcastReceiver;");
    receiver_class.VirtualMethod(
        "onReceive", "(Landroid/content/Context;Landroid/content/Intent;)V", [&](IntrinsicContext& call) {
            const auto type = call.vm.Model().ObjectClass(call.receiver);
            const auto method = call.vm.Linker().FindVtableIndex(type, "isInitialStickyBroadcast", "()Z");
            REQUIRE(method);
            const auto result =
                call.vm.Call(call.vm.Linker().Class(type).vtable[*method], std::array{VmValue::Ref(call.receiver)});
            REQUIRE_FALSE(result.exception.IsValid());
            if (result.value.AsInt()) ++sticky_received;
            ++received;
            return VmValue::Void();
        });
    int provider_creates = 0;
    auto provider_class = IntrinsicClassBuilder::Class("Ltest/RomProvider;", "Landroid/content/ContentProvider;");
    provider_class.Constructor("()V", [](IntrinsicContext& call) {
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/content/ContentProvider;");
        const auto init = call.vm.Linker().FindDirectMethod(type, "<init>", "()V");
        REQUIRE(init);
        const auto result = call.vm.Call(*init, std::array{VmValue::Ref(call.receiver)});
        if (result.exception.IsValid()) call.vm.SetPendingException(result.exception);
        return VmValue::Void();
    });
    provider_class.OverrideMethod("onCreate", "()Z", [&](IntrinsicContext& call) {
        const auto type = call.vm.Model().ObjectClass(call.receiver);
        const auto slot = call.vm.Linker().FindVtableIndex(type, "getContext", "()Landroid/content/Context;");
        REQUIRE(slot);
        const auto result =
            call.vm.Call(call.vm.Linker().Class(type).vtable[*slot], std::array{VmValue::Ref(call.receiver)});
        REQUIRE_FALSE(result.exception.IsValid());
        const auto app = result.value.ref;
        const auto app_root = call.vm.ProtectReferences(std::array{app});
        const auto base_slot = call.vm.Linker().FindVtableIndex(call.vm.Model().ObjectClass(app), "getBaseContext",
                                                                "()Landroid/content/Context;");
        REQUIRE(base_slot);
        const auto base = call.vm.Call(call.vm.Linker().Class(call.vm.Model().ObjectClass(app)).vtable[*base_slot],
                                       std::array{VmValue::Ref(app)});
        REQUIRE_FALSE(base.exception.IsValid());
        CHECK(call.vm.Linker().Class(call.vm.Model().ObjectClass(base.value.ref)).descriptor ==
              "Landroid/app/ContextImpl;");
        ++provider_creates;
        return VmValue::Int(1);
    });
    provider_class.OverrideMethod("getType", "(Landroid/net/Uri;)Ljava/lang/String;", [](IntrinsicContext& call) {
        return VmValue::Ref(call.vm.NewStringUtf8("vnd.android.cursor.item/vnd.test"));
    });
    AndroidValueVm f(InterpreterBackend::switch_dispatch,
                     {std::move(receiver_class).Build(), std::move(service_class).Build(),
                      std::move(provider_class).Build(), std::move(connection_class).Build()});
    f.context->package_name = "test.application";
    f.context->application_class_name = "android.app.Application";
    f.context->package_resource_path = "/data/app/test.application-1.apk";
    f.context->package_inventory_known = true;
    f.context->service_inventory_known = true;
    f.context->service_components = {{"test.RomService", true}};
    f.context->provider_components = {
        {.name = "test.RomProvider", .authority = "test.application.data;test.alias", .grant_uri_permissions = true}};
    const auto thread = f.New("Landroid/app/ActivityThread;");
    const auto root = f.vm.ProtectReferences(std::array{thread});
    const auto attach = f.linker.FindDirectMethod(f.model.ObjectClass(thread), "attach", "(Z)V");
    REQUIRE(attach);
    const auto outcome = f.vm.Call(*attach, std::array{VmValue::Ref(thread), VmValue::Int(0)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message,
                    f.vm.DescribeThrowableCauses(outcome.exception));
    const auto error = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
    REQUIRE_MESSAGE(!error.has_value(), error.value_or(""));
    const auto application =
        f.Static("Landroid/app/ActivityThread;", "currentApplication", "()Landroid/app/Application;").ref;
    REQUIRE(application.IsValid());
    CHECK(f.context->guest_process_name == f.context->package_name);
    const auto manager = f.On(application, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                              {VmValue::Ref(f.vm.NewStringUtf8("activity"))})
                             .ref;
    const auto manager_root = f.vm.ProtectReferences(std::array{manager});
    const auto device = f.On(manager, "getDeviceConfigurationInfo", "()Landroid/content/pm/ConfigurationInfo;").ref;
    REQUIRE(device.IsValid());
    CHECK(f.vm.StringUtf8(f.On(device, "getGlEsVersion", "()Ljava/lang/String;").ref) == "2.0");
    const auto pm = f.On(application, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
    const auto info = f.On(pm, "resolveContentProvider", "(Ljava/lang/String;I)Landroid/content/pm/ProviderInfo;",
                           {VmValue::Ref(f.vm.NewStringUtf8("test.alias")), VmValue::Int(0)})
                          .ref;
    REQUIRE(info.IsValid());
    const auto grant_field = f.linker.FindFieldRecursive(f.model.ObjectClass(info), "grantUriPermissions", "Z");
    REQUIRE(grant_field);
    CHECK(f.model.InstanceSlots(info)[f.linker.Field(*grant_field).slot].bits == 1);
    CHECK(provider_creates == 1);
    REQUIRE(f.context->local_provider_holders.contains("test.application.data"));
    CHECK(f.context->local_provider_holders.at("test.application.data") ==
          f.context->local_provider_holders.at("test.alias"));
    const auto resolver = f.On(application, "getContentResolver", "()Landroid/content/ContentResolver;").ref;
    const auto resolver_root = f.vm.ProtectReferences(std::array{resolver});
    const auto uri = f.Static("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                              {VmValue::Ref(f.vm.NewStringUtf8("content://test.alias/item"))})
                         .ref;
    const auto uri_root = f.vm.ProtectReferences(std::array{uri});
    CHECK(
        f.vm.StringUtf8(f.On(resolver, "getType", "(Landroid/net/Uri;)Ljava/lang/String;", {VmValue::Ref(uri)}).ref) ==
        "vnd.android.cursor.item/vnd.test");
    const auto receiver = f.vm.NewIntrinsicInstance("Ltest/RomReceiver;");
    const auto receiver_root = f.vm.ProtectReferences(std::array{receiver});
    const auto action = f.vm.NewStringUtf8("test.application.EVENT");
    const auto action_root = f.vm.ProtectReferences(std::array{action});
    const auto filter = f.New("Landroid/content/IntentFilter;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
    const auto filter_root = f.vm.ProtectReferences(std::array{filter});
    const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V", {VmValue::Ref(action)});
    const auto intent_root = f.vm.ProtectReferences(std::array{intent});
    CHECK_FALSE(f.On(application, "registerReceiver",
                     "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
                     {VmValue::Ref(receiver), VmValue::Ref(filter)})
                    .ref.IsValid());
    f.On(application, "sendBroadcast", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
    REQUIRE_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
    CHECK(received == 1);
    f.On(application, "unregisterReceiver", "(Landroid/content/BroadcastReceiver;)V", {VmValue::Ref(receiver)});
    f.On(application, "sendBroadcast", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
    REQUIRE_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
    CHECK(received == 1);
    constexpr auto register_signature =
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;";
    const auto battery_filter = f.New("Landroid/content/IntentFilter;", "(Ljava/lang/String;)V",
                                      {VmValue::Ref(f.vm.NewStringUtf8("android.intent.action.BATTERY_CHANGED"))});
    const auto battery_root = f.vm.ProtectReferences(std::array{battery_filter});
    const auto battery = f.On(application, "registerReceiver", register_signature,
                              {VmValue::Ref(VmObjectRef{}), VmValue::Ref(battery_filter)})
                             .ref;
    REQUIRE(battery.IsValid());
    CHECK(f.On(battery, "getBooleanExtra", "(Ljava/lang/String;Z)Z",
               {VmValue::Ref(f.vm.NewStringUtf8("present")), VmValue::Int(1)})
              .AsInt() == 0);
    f.context->granted_permissions.insert("android.permission.BROADCAST_STICKY");
    f.On(application, "sendStickyBroadcast", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
    CHECK(f.On(application, "registerReceiver", register_signature, {VmValue::Ref(receiver), VmValue::Ref(filter)})
              .ref.IsValid());
    REQUIRE_FALSE(ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context).has_value());
    CHECK(received == 2);
    CHECK(sticky_received == 1);
    f.On(application, "unregisterReceiver", "(Landroid/content/BroadcastReceiver;)V", {VmValue::Ref(receiver)});
    f.On(application, "removeStickyBroadcast", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
    CHECK_FALSE(
        f.On(application, "registerReceiver", register_signature, {VmValue::Ref(VmObjectRef{}), VmValue::Ref(filter)})
            .ref.IsValid());
    const auto component = f.New(
        "Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(f.vm.NewStringUtf8("test.application")), VmValue::Ref(f.vm.NewStringUtf8("test.RomService"))});
    const auto component_root = f.vm.ProtectReferences(std::array{component});
    const auto request = f.New("Landroid/content/Intent;");
    const auto request_root = f.vm.ProtectReferences(std::array{request});
    f.On(request, "setComponent", "(Landroid/content/ComponentName;)Landroid/content/Intent;",
         {VmValue::Ref(component)});
    for (int i = 0; i < 2; ++i)
        CHECK(f.On(application, "startService", "(Landroid/content/Intent;)Landroid/content/ComponentName;",
                   {VmValue::Ref(request)})
                  .ref.IsValid());
    const auto starts = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
    REQUIRE_MESSAGE(!starts.has_value(), starts.value_or(""));
    CHECK(created == 1);
    CHECK(started == 2);
    const auto connection = f.vm.NewIntrinsicInstance("Ltest/RomConnection;");
    const auto connection_root = f.vm.ProtectReferences(std::array{connection});
    CHECK(f.On(application, "bindService", "(Landroid/content/Intent;Landroid/content/ServiceConnection;I)Z",
               {VmValue::Ref(request), VmValue::Ref(connection), VmValue::Int(1)})
              .AsInt() == 1);
    const auto binding = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
    REQUIRE_MESSAGE(!binding.has_value(), binding.value_or(""));
    CHECK(bound == 1);
    CHECK(connected == 1);
    CHECK(f.On(application, "stopService", "(Landroid/content/Intent;)Z", {VmValue::Ref(request)}).AsInt() == 1);
    const auto stop = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
    REQUIRE_MESSAGE(!stop.has_value(), stop.value_or(""));
    CHECK(destroyed == 0);
    f.On(application, "unbindService", "(Landroid/content/ServiceConnection;)V", {VmValue::Ref(connection)});
    const auto unbinding = ogplay::test::android_value::DispatchFixtureMessages(f.vm, *f.context);
    REQUIRE_MESSAGE(!unbinding.has_value(), unbinding.value_or(""));
    CHECK(unbound == 1);
    CHECK(destroyed == 1);
}
