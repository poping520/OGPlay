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

TEST_CASE("DVM-240 installed applications use sealed facts independent values and API19 flags") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        auto& context = *f.context;
        context.package_name = "org.example.game";
        context.package_resource_path = "/data/app/org.example.game-1.apk";
        context.application_class_name = "org.example.GameApplication";
        context.application_process_name = "org.example.game:main";
        context.application_uid = 10042;
        context.target_sdk_version = 19;
        context.application_meta_data.emplace("fixture.setting", std::string("sealed"));
        const auto base = f.New("Landroid/content/Context;");
        const auto base_root = f.vm.ProtectReferences(std::array{base});
        const auto manager = f.On(base, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        const auto manager_root = f.vm.ProtectReferences(std::array{manager});
        const auto query = [&](int flags) {
            return f.OnOutcome(manager, "getInstalledApplications", "(I)Ljava/util/List;",
                               {VmValue::Int(flags)});
        };
        const auto unsupported = [&](const VmCallOutcome& result) {
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        };
        const auto field = [&](VmObjectRef object, const char* name, const char* descriptor) -> Slot& {
            const auto found = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(found.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*found).slot];
        };
        const auto ref = [&](VmObjectRef object, const char* name) {
            return VmObjectRef{field(object, name, "Ljava/lang/String;").bits};
        };
        unsupported(query(0)); // Populated fields alone do not seal the inventory.
        context.package_inventory_known = true;
        const auto plain = query(0);
        REQUIRE_FALSE(plain.exception.IsValid());
        const auto list = plain.value.ref;
        const auto list_root = f.vm.ProtectReferences(std::array{list});
        CHECK(f.linker.Class(f.model.ObjectClass(list)).descriptor == "Ljava/util/ArrayList;");
        CHECK(f.linker.Class(f.model.ObjectClass(list)).is_boot_dex);
        CHECK(f.On(list, "size", "()I").AsInt() == 1);
        const auto iterator = f.On(list, "iterator", "()Ljava/util/Iterator;").ref;
        const auto iterator_root = f.vm.ProtectReferences(std::array{iterator});
        CHECK(f.On(iterator, "hasNext", "()Z").AsInt() == 1);
        const auto info = f.On(iterator, "next", "()Ljava/lang/Object;").ref;
        CHECK(f.On(iterator, "hasNext", "()Z").AsInt() == 0);
        CHECK(f.vm.StringUtf8(ref(info, "packageName")) == context.package_name);
        CHECK(f.vm.StringUtf8(ref(info, "sourceDir")) == context.package_resource_path);
        CHECK(f.vm.StringUtf8(ref(info, "dataDir")) == "/data/data/org.example.game");
        CHECK(field(info, "uid", "I").bits == 10042);
        CHECK(field(info, "targetSdkVersion", "I").bits == 19);
        CHECK((field(info, "flags", "I").bits & (1U << 23)) != 0);
        CHECK(field(info, "metaData", "Landroid/os/Bundle;").bits == 0);
        const auto stable = f.On(base, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        const auto package = f.vm.NewStringUtf8(context.package_name);
        const auto package_root = f.vm.ProtectReferences(std::array{package});
        const auto direct = f.On(manager, "getApplicationInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;", {VmValue::Ref(package), VmValue::Int(0)}).ref;
        const auto direct_root = f.vm.ProtectReferences(std::array{direct});
        CHECK(info != stable); CHECK(info != direct);
        for (const auto* name : {"packageName", "className", "processName", "sourceDir", "publicSourceDir", "dataDir", "nativeLibraryDir"}) {
            CHECK(f.vm.StringUtf8(ref(info, name)) == f.vm.StringUtf8(ref(stable, name)));
            CHECK(f.vm.StringUtf8(ref(info, name)) == f.vm.StringUtf8(ref(direct, name)));
        }
        CHECK(f.vm.StringUtf8(f.On(base, "getPackageCodePath", "()Ljava/lang/String;").ref) == context.package_resource_path);
        CHECK((field(stable, "flags", "I").bits & (1U << 23)) != 0);
        CHECK((field(direct, "flags", "I").bits & (1U << 23)) != 0);
        const auto replacement = f.vm.NewStringUtf8("/invalid.apk");
        field(info, "sourceDir", "Ljava/lang/String;") = {replacement.Value(), SlotTag::ref};
        f.On(list, "clear", "()V");
        context.application_enabled = false;
        const auto detailed = query(0x80);
        REQUIRE_FALSE(detailed.exception.IsValid());
        const auto detailed_list = detailed.value.ref;
        const auto detailed_root = f.vm.ProtectReferences(std::array{detailed_list});
        REQUIRE(f.On(detailed_list, "size", "()I").AsInt() == 1);
        const auto detailed_info = f.On(detailed_list, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref;
        CHECK(field(detailed_info, "enabled", "Z").bits == 0);
        CHECK(f.vm.StringUtf8(ref(detailed_info, "sourceDir")) == context.package_resource_path);
        CHECK(f.vm.StringUtf8(ref(stable, "sourceDir")) == context.package_resource_path);
        const auto metadata = VmObjectRef{field(detailed_info, "metaData", "Landroid/os/Bundle;").bits};
        REQUIRE(metadata.IsValid());
        const auto key = f.vm.NewStringUtf8("fixture.setting");
        const auto key_root = f.vm.ProtectReferences(std::array{key});
        CHECK(f.vm.StringUtf8(f.On(metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(key)}).ref) == "sealed");
        f.On(metadata, "putString", "(Ljava/lang/String;Ljava/lang/String;)V", {VmValue::Ref(key), VmValue::Ref(replacement)});
        const auto fresh = query(0x80);
        REQUIRE_FALSE(fresh.exception.IsValid());
        const auto fresh_root = f.vm.ProtectReferences(std::array{fresh.value.ref});
        const auto fresh_info = f.On(fresh.value.ref, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref;
        CHECK(fresh_info != detailed_info);
        const auto fresh_metadata = VmObjectRef{field(fresh_info, "metaData", "Landroid/os/Bundle;").bits};
        CHECK(f.vm.StringUtf8(f.On(fresh_metadata, "getString", "(Ljava/lang/String;)Ljava/lang/String;", {VmValue::Ref(key)}).ref) == "sealed");
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.vm.MarkReachable().IsMarked(fresh_info));
        CHECK(f.vm.MarkReachable().IsMarked(fresh_metadata));
        for (int flags : {1, 0x200, 0x400, 0x2000, -1}) unsupported(query(flags));
        context.package_inventory_known = false; unsupported(query(0));
        context.package_inventory_known = true;
        context.package_resource_path.clear(); unsupported(query(0));
        context.package_resource_path = "host-relative.apk"; unsupported(query(0));
        context.package_resource_path = "/data/app/org.example.game-1.apk";
        context.package_name.clear(); unsupported(query(0));
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.installed_applications");
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
        const auto query = [&](const std::string& path, std::int32_t flags = 1 /* GET_ACTIVITIES */) {
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
