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
