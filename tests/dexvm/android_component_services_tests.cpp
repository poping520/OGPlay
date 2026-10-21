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
        CHECK(f.ledger.Unimplemented()[0].count == 1);
        f.context->service_components.push_back(
            {"example.Candidate", true, {{{"example.SERVICE"}, {"example.EXTRA"}, false}}});
        fail(query(intent));  // An Intent without categories can match a filter with categories.
        fail(query(intent, 0x40));  // Projection flags do not pretend a positive match is absent.
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
        CHECK(f.ledger.Unimplemented()[0].count == 12);
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
        component.launch_mode = 2;
        component.meta_data.front().value = std::string("last declaration");
        f.context->activity_components.push_back(component);
        const auto duplicate = query(0x80);
        REQUIRE_FALSE(duplicate.exception.IsValid());
        const auto duplicate_metadata = field(duplicate.value.ref, "metaData", "Landroid/os/Bundle;");
        CHECK(f.vm.StringUtf8(f.On(duplicate_metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(f.vm.NewStringUtf8("android.app.lib_name"))}).ref) == "last declaration");
        const auto mode = f.linker.FindFieldRecursive(f.model.ObjectClass(duplicate.value.ref), "launchMode", "I");
        REQUIRE(mode.has_value());
        CHECK(f.model.InstanceSlots(duplicate.value.ref)[f.linker.Field(*mode).slot].bits == 2);
        const auto package_info = f.On(manager, "getPackageInfo", "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Int(1)}).ref;
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
        alias.launch_mode = 1; // Snapshot differs from the registered target.
        f.context->activity_components.push_back(alias);
        const auto alias_name = f.New("Landroid/content/ComponentName;", "(Ljava/lang/String;Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("fixture")), VmValue::Ref(f.vm.NewStringUtf8("fixture.Alias"))});
        const auto alias_info = f.On(manager, "getActivityInfo", "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;",
            {VmValue::Ref(alias_name), VmValue::Int(0)}).ref;
        CHECK(f.model.InstanceSlots(alias_info)[f.linker.Field(*mode).slot].bits == 1);
        f.context->activity_inventory_known = false;
        CHECK(query(0).exception.IsValid());
    }
}

TEST_CASE("duplicate Activity filters resolve once and launches use the registered configuration") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "fixture";
        f.context->activity_inventory_known = true;
        ogplay::loader::AndroidManifestActivityComponent first;
        first.name = "fixture.Main";
        first.intent_filters = {{{"fixture.OPEN", "fixture.FIRST"},
            {"android.intent.category.DEFAULT"}, false}};
        auto last = first;
        last.intent_filters.front().actions = {"fixture.OPEN", "fixture.LAST"};
        f.context->activity_components = {first, last};
        const auto base = f.New("Landroid/content/Context;");
        const auto root = f.vm.ProtectReferences(std::array{base});
        const auto launch = [&](const char* action, bool explicit_component) {
            f.context->activity_commands.clear();
            f.context->activity_switch_pending = false;
            f.context->pending_activity_descriptor.clear();
            const auto intent = f.New("Landroid/content/Intent;", "(Ljava/lang/String;)V",
                {VmValue::Ref(f.vm.NewStringUtf8(action))});
            const auto intent_root = f.vm.ProtectReferences(std::array{intent});
            if (explicit_component)
                f.On(intent, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                    {VmValue::Ref(f.vm.NewStringUtf8("fixture")),
                     VmValue::Ref(f.vm.NewStringUtf8("fixture.Main"))});
            return f.OnOutcome(base, "startActivity", "(Landroid/content/Intent;)V", {VmValue::Ref(intent)});
        };
        for (const auto* action : {"fixture.OPEN", "fixture.FIRST", "fixture.LAST"}) {
            CHECK_FALSE(launch(action, false).exception.IsValid());
            CHECK(f.context->pending_activity_descriptor == "Lfixture/Main;");
            CHECK(f.context->pending_activity_component_name == "fixture.Main");
        }
        CHECK_FALSE(launch("fixture.OPEN", true).exception.IsValid());

        // The last declaration owns launch configuration, including unsupported affinity matching.
        f.context->activity_components.back().launch_mode = 2;
        f.context->activity_components.back().task_affinity = "another.task";
        for (const bool explicit_component : {false, true}) {
            const auto blocked = launch("fixture.FIRST", explicit_component);
            REQUIRE(blocked.exception.IsValid());
            CHECK(f.linker.Class(blocked.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
            CHECK_FALSE(f.context->activity_switch_pending);
        }
        f.context->activity_components.back().launch_mode = 0;
        f.context->activity_components.back().task_affinity.reset();
        f.context->activity_components.back().enabled = false;
        for (const bool explicit_component : {false, true}) {
            const auto blocked = launch("fixture.OPEN", explicit_component);
            REQUIRE(blocked.exception.IsValid());
            CHECK(f.linker.Class(blocked.exception_class).descriptor == "Landroid/content/ActivityNotFoundException;");
        }
        f.context->activity_components.back().enabled = true;
        auto other = last;
        other.name = "fixture.Other";
        f.context->activity_components.push_back(other);
        const auto ambiguous = launch("fixture.OPEN", false);
        REQUIRE(ambiguous.exception.IsValid());
        CHECK(f.linker.Class(ambiguous.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
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
    f.context->threads = &f.threads;
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
