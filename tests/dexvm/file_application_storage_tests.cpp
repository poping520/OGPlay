#include "boot_dex.h"
// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/audio/pcm_mix.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

#include "file_vfs_fixture.h"

using namespace ogplay::test::file_vfs;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {

struct PrefsDriver final {
    FileVm& vm;

    [[nodiscard]] VmObjectRef Open(const std::string& name) {
        const auto context_object =
            vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        return vm.CallOn(context_object, "getSharedPreferences",
                         "(Ljava/lang/String;I)"
                         "Landroid/content/SharedPreferences;",
                         {VmValue::Ref(vm.interpreter.NewStringUtf8(name)),
                          VmValue::Int(0)})
            .ref;
    }

    [[nodiscard]] VmObjectRef Editor(const VmObjectRef prefs) {
        return vm.CallOn(prefs, "edit",
                         "()Landroid/content/SharedPreferences$Editor;")
            .ref;
    }
};



} // namespace

TEST_CASE("Environment MEDIA_MOUNTED Java field matches the external storage state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        FileVm vm(nullptr, true, InterpreterConfig{.backend = backend});
        const auto type = vm.linker.ResolveDescriptor("Landroid/os/Environment;");
        const auto field = vm.CallOn(vm.model.ClassObject(type), "getDeclaredField",
            "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("MEDIA_MOUNTED"))}).ref;
        const auto mounted = vm.CallOn(field, "get",
            "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(VmObjectRef{})}).ref;
        REQUIRE(mounted.IsValid());
        CHECK(vm.interpreter.StringUtf8(mounted) == "mounted");
        const auto state = vm.CallStatic("Landroid/os/Environment;",
            "getExternalStorageState", "()Ljava/lang/String;").ref;
        CHECK(vm.interpreter.StringUtf8(state) == vm.interpreter.StringUtf8(mounted));
    }
}

TEST_CASE("Environment data directory is one stable guest File") {
    FileVm vm;
    const auto first = vm.CallStatic(
        "Landroid/os/Environment;", "getDataDirectory",
        "()Ljava/io/File;").ref;
    const auto repeated = vm.CallStatic(
        "Landroid/os/Environment;", "getDataDirectory",
        "()Ljava/io/File;").ref;
    REQUIRE(first.IsValid());
    CHECK(repeated == first);
    const auto path = vm.CallOn(first, "getPath", "()Ljava/lang/String;").ref;
    CHECK(vm.interpreter.StringUtf8(path) == "/data");
}

TEST_CASE("Context files and cache directories are inherited stable and VFS backed") {
    FileVm vm;
    const auto base =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    const auto activity =
        vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    static_cast<void>(vm.CallOn(
        activity, "attachBaseContext", "(Landroid/content/Context;)V",
        {VmValue::Ref(base)}));
    const auto first =
        vm.CallOn(activity, "getFilesDir", "()Ljava/io/File;").ref;
    const auto repeated =
        vm.CallOn(activity, "getFilesDir", "()Ljava/io/File;").ref;
    REQUIRE(first.IsValid());
    CHECK(repeated == first);
    const auto path = vm.CallOn(first, "getPath", "()Ljava/lang/String;").ref;
    CHECK(vm.interpreter.StringUtf8(path) ==
          "/data/data/com.example.game/files");
    CHECK(vm.vfs.Stat("/data/data/com.example.game/files").is_directory);

    CHECK(vm.CallOn(base, "getFilesDir", "()Ljava/io/File;").ref == first);

    const auto cache =
        vm.CallOn(activity, "getCacheDir", "()Ljava/io/File;").ref;
    const auto repeated_cache =
        vm.CallOn(activity, "getCacheDir", "()Ljava/io/File;").ref;
    REQUIRE(cache.IsValid());
    CHECK(repeated_cache == cache);
    const auto cache_path =
        vm.CallOn(cache, "getPath", "()Ljava/lang/String;").ref;
    CHECK(vm.interpreter.StringUtf8(cache_path) ==
          "/data/data/com.example.game/cache");
    CHECK(vm.vfs.Stat("/data/data/com.example.game/cache").is_directory);
    CHECK(vm.CallOn(base, "getCacheDir", "()Ljava/io/File;").ref == cache);
}

TEST_CASE("Context OBB directories follow API 19 external package layout") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        const auto base =
            vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto activity =
            vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        static_cast<void>(vm.CallOn(
            activity, "attachBaseContext", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)}));

        const auto obb =
            vm.CallOn(activity, "getObbDir", "()Ljava/io/File;").ref;
        REQUIRE(obb.IsValid());
        CHECK(vm.CallOn(base, "getObbDir", "()Ljava/io/File;").ref == obb);
        const auto path = vm.CallOn(
            obb, "getPath", "()Ljava/lang/String;").ref;
        CHECK(vm.interpreter.StringUtf8(path) ==
              "/sdcard/Android/obb/com.example.game");
        CHECK(vm.vfs.Stat("/sdcard/Android/obb/com.example.game")
                  .is_directory);

        const auto dirs = vm.CallOn(
            activity, "getObbDirs", "()[Ljava/io/File;").ref;
        REQUIRE(dirs.IsValid());
        CHECK(vm.model.ArrayLength(dirs) == 1);
        CHECK(vm.model.GetObjectElement(dirs, 0) == obb);
    }
}

TEST_CASE("Context private file streams use the app files directory on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        CHECK(vm.StaticField("Landroid/content/Context;", "MODE_PRIVATE",
                             "I") == 0U);
        CHECK(vm.StaticField("Landroid/content/Context;", "MODE_APPEND",
                             "I") == 0x8000U);
        const auto base =
            vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto activity =
            vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        static_cast<void>(vm.CallOn(
            activity, "attachBaseContext", "(Landroid/content/Context;)V",
            {VmValue::Ref(base)}));
        const auto name = vm.interpreter.NewStringUtf8("slot.dat");
        const auto file_path_descriptor =
            "(Ljava/lang/String;)Ljava/io/File;";
        const auto stream_file = vm.CallOn(
            activity, "getFileStreamPath", file_path_descriptor,
            {VmValue::Ref(name)}).ref;
        REQUIRE(stream_file.IsValid());
        CHECK(vm.linker.Class(vm.model.ObjectClass(stream_file)).descriptor ==
              "Ljava/io/File;");
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
            stream_file, "getPath", "()Ljava/lang/String;").ref) ==
              "/data/data/com.example.game/files/slot.dat");
        CHECK(vm.CallOn(stream_file, "exists", "()Z").AsInt() == 0);
        const auto base_stream_file = vm.CallOn(
            base, "getFileStreamPath", file_path_descriptor,
            {VmValue::Ref(name)}).ref;
        CHECK(base_stream_file != stream_file);
        CHECK(vm.CallOn(base_stream_file, "equals", "(Ljava/lang/Object;)Z",
                        {VmValue::Ref(stream_file)}).AsInt() == 1);
        const auto output_descriptor =
            "(Ljava/lang/String;I)Ljava/io/FileOutputStream;";
        const auto input_descriptor =
            "(Ljava/lang/String;)Ljava/io/FileInputStream;";

        auto output = vm.CallOn(
            activity, "openFileOutput", output_descriptor,
            {VmValue::Ref(name), VmValue::Int(0)}).ref;
        REQUIRE(output.IsValid());
        CHECK(vm.linker.Class(vm.model.ObjectClass(output)).descriptor ==
              "Ljava/io/FileOutputStream;");
        static_cast<void>(vm.CallOn(output, "write", "(I)V",
                                    {VmValue::Int('A')}));
        static_cast<void>(vm.CallOn(output, "close", "()V"));
        CHECK(vm.NativeRead(
                  "/data/data/com.example.game/files/slot.dat") == "A");
        CHECK(vm.CallOn(stream_file, "exists", "()Z").AsInt() == 1);

        output = vm.CallOn(
            activity, "openFileOutput", output_descriptor,
            {VmValue::Ref(name), VmValue::Int(0x8000)}).ref;
        static_cast<void>(vm.CallOn(output, "write", "(I)V",
                                    {VmValue::Int('B')}));
        static_cast<void>(vm.CallOn(output, "close", "()V"));
        CHECK(vm.NativeRead(
                  "/data/data/com.example.game/files/slot.dat") == "AB");

        output = vm.CallOn(
            base, "openFileOutput", output_descriptor,
            {VmValue::Ref(name), VmValue::Int(0)}).ref;
        static_cast<void>(vm.CallOn(output, "write", "(I)V",
                                    {VmValue::Int('C')}));
        static_cast<void>(vm.CallOn(output, "close", "()V"));

        const auto input = vm.CallOn(
            activity, "openFileInput", input_descriptor,
            {VmValue::Ref(name)}).ref;
        REQUIRE(input.IsValid());
        CHECK(vm.linker.Class(vm.model.ObjectClass(input)).descriptor ==
              "Ljava/io/FileInputStream;");
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == 'C');
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == -1);
        static_cast<void>(vm.CallOn(input, "close", "()V"));

        const auto expect_exception = [&](const std::string& method,
                                          const std::string& descriptor,
                                          std::vector<VmValue> arguments,
                                          const std::string_view expected) {
            const auto outcome = vm.CallOnOutcome(
                activity, method, descriptor, std::move(arguments));
            REQUIRE(outcome.exception.IsValid());
            CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
                  expected);
        };
        expect_exception(
            "openFileInput", input_descriptor,
            {VmValue::Ref(vm.interpreter.NewStringUtf8("missing.dat"))},
            "Ljava/io/FileNotFoundException;");
        expect_exception(
            "openFileInput", input_descriptor,
            {VmValue::Ref(vm.interpreter.NewStringUtf8("nested/slot.dat"))},
            "Ljava/lang/IllegalArgumentException;");
        expect_exception(
            "getFileStreamPath", file_path_descriptor,
            {VmValue::Ref(vm.interpreter.NewStringUtf8("nested/slot.dat"))},
            "Ljava/lang/IllegalArgumentException;");
        expect_exception("getFileStreamPath", file_path_descriptor,
                         {VmValue::Ref(VmObjectRef{})},
                         "Ljava/lang/NullPointerException;");
        expect_exception("openFileOutput", output_descriptor,
                         {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)},
                         "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("Context getDir private directories preserve API19 paths and VFS state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        static_cast<void>(vm.CallOn(base, "getFilesDir", "()Ljava/io/File;"));
        const auto app = vm.interpreter.NewIntrinsicInstance("Landroid/app/Application;");
        const auto activity = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        for (const auto wrapper : {app, activity})
            static_cast<void>(vm.CallOn(wrapper, "attachBaseContext",
                "(Landroid/content/Context;)V", {VmValue::Ref(base)}));
        constexpr auto signature = "(Ljava/lang/String;I)Ljava/io/File;";
        const auto get_dir = [&](const VmObjectRef receiver, const char* name) {
            return vm.CallOn(receiver, "getDir", signature,
                {VmValue::Ref(name ? vm.interpreter.NewStringUtf8(name) : VmObjectRef{}),
                 VmValue::Int(0)}).ref;
        };
        const auto first = get_dir(app, "sdk");
        REQUIRE(first.IsValid());
        const auto path = "/data/data/com.example.game/app_sdk";
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(first, "getPath",
              "()Ljava/lang/String;").ref) == path);
        CHECK(vm.BoolOn(first, "isDirectory"));
        vm.JavaWrite(std::string(path) + "/state.dat", "saved");
        const auto again = get_dir(activity, "sdk");
        CHECK(first != again);
        CHECK(vm.CallOn(first, "equals", "(Ljava/lang/Object;)Z",
                        {VmValue::Ref(again)}).AsInt() == 1);
        CHECK(vm.NativeRead(std::string(path) + "/state.dat") == "saved");
        for (const auto& [name, expected] :
             std::array<std::pair<const char*, const char*>, 4>{{
                 {nullptr, "app_null"}, {"", "app_"},
                 {"..", "app_.."}, {"other", "app_other"}}}) {
            const auto file = get_dir(base, name);
            CHECK(vm.interpreter.StringUtf8(vm.CallOn(file, "getPath",
                  "()Ljava/lang/String;").ref) ==
                  std::string("/data/data/com.example.game/") + expected);
            CHECK(vm.BoolOn(file, "isDirectory"));
        }
        const auto invalid = vm.CallOnOutcome(base, "getDir", signature,
            {VmValue::Ref(vm.interpreter.NewStringUtf8("../escape")), VmValue::Int(0)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(vm.linker.Class(invalid.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
        vm.JavaWrite("/data/data/com.example.game/app_file", "existing");
        CHECK(vm.BoolOn(get_dir(base, "file"), "isFile"));
        CHECK(vm.NativeRead("/data/data/com.example.game/app_file") == "existing");
        // An unavailable parent makes mkdir fail. AOSP still returns the File.
        vm.context->package_name = "com.example.uninstalled";
        const auto failed = get_dir(app, "sdk");
        REQUIRE(failed.IsValid());
        CHECK_FALSE(vm.BoolOn(failed, "exists"));
        for (const auto mode : {1, 2, 4, 0x8000, -1}) {
            const auto unsupported = vm.CallOnOutcome(base, "getDir", signature,
                {VmValue::Ref(vm.interpreter.NewStringUtf8("unsupported")), VmValue::Int(mode)});
            REQUIRE(unsupported.exception.IsValid());
            CHECK(vm.linker.Class(unsupported.exception_class).descriptor ==
                  "Ljava/lang/UnsupportedOperationException;");
        }
        vm.context->vfs = nullptr;
        const auto unavailable = vm.CallOnOutcome(base, "getDir", signature,
            {VmValue::Ref(vm.interpreter.NewStringUtf8("sdk")), VmValue::Int(0)});
        REQUIRE(unavailable.exception.IsValid());
        CHECK(vm.linker.Class(unavailable.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        const auto hits = vm.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "dexvm.context_private_directory");
        CHECK(hits[0].count == 6);
    }
}

TEST_CASE("Context getDir private directories persist across sessions") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        InterpreterConfig config;
        config.backend = backend;
        const TemporaryRoot root(backend == InterpreterBackend::threaded
                                 ? "getdir-threaded" : "getdir-switch");
        for (const auto reload : {false, true}) {
            auto store = SandboxStore::Open(root.path, kPackage, kPackage);
            FileVm vm(store.get(), true, config);
            const auto context = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
            const auto directory = vm.CallOn(context, "getDir",
                "(Ljava/lang/String;I)Ljava/io/File;",
                {VmValue::Ref(vm.interpreter.NewStringUtf8("sdk")), VmValue::Int(0)}).ref;
            REQUIRE(directory.IsValid());
            CHECK(vm.BoolOn(directory, "isDirectory"));
            const auto file = vm.NewFile(directory, "state.dat");
            const auto path = vm.interpreter.StringUtf8(vm.CallOn(file, "getPath",
                                                   "()Ljava/lang/String;").ref);
            if (reload) CHECK(vm.NativeRead(path) == "retained");
            else vm.JavaWrite(path, "retained");
            vm.vfs.FlushAll();
        }
    }
}

TEST_CASE("Context internal directories return null when VFS is unavailable") {
    FileVm vm;
    vm.context->vfs = nullptr;
    const auto context =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    CHECK_FALSE(vm.CallOn(context, "getFilesDir", "()Ljava/io/File;")
                    .ref.IsValid());
    CHECK_FALSE(vm.CallOn(context, "getCacheDir", "()Ljava/io/File;")
                    .ref.IsValid());
    CHECK_FALSE(vm.CallOn(context, "getObbDir", "()Ljava/io/File;")
                    .ref.IsValid());
    const auto obb_dirs =
        vm.CallOn(context, "getObbDirs", "()[Ljava/io/File;").ref;
    REQUIRE(obb_dirs.IsValid());
    CHECK(vm.model.ArrayLength(obb_dirs) == 1);
    CHECK_FALSE(vm.model.GetObjectElement(obb_dirs, 0).IsValid());
    const auto outcome = vm.CallOnOutcome(
        context, "openFileInput",
        "(Ljava/lang/String;)Ljava/io/FileInputStream;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("slot.dat"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("Java file writes survive into the next session") {
    const TemporaryRoot root("crosssession");
    {
        auto store = SandboxStore::Open(root.path, kPackage, kPackage);
        FileVm vm(store.get());
        const auto directory = vm.NewFile("/sdcard/game/saves");
        CHECK(vm.BoolOn(directory, "mkdirs"));
        vm.JavaWrite("/sdcard/game/saves/slot0.sav", "progress-42");
        vm.vfs.FlushAll();  // clean shutdown
    }

    auto store = SandboxStore::Open(root.path, kPackage, kPackage);
    FileVm vm(store.get());
    const auto file = vm.NewFile("/sdcard/game/saves/slot0.sav");
    CHECK(vm.BoolOn(file, "exists"));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 11);
    CHECK(vm.NativeRead("/sdcard/game/saves/slot0.sav") == "progress-42");
    CHECK(vm.BoolOn(vm.NewFile("/sdcard/game/saves"), "isDirectory"));
}

TEST_CASE("PreferenceManager default preferences persist across sessions") {
    const TemporaryRoot root("defaultprefs");
    const auto open_default = [](FileVm& vm) {
        vm.context->package_name = kPackage;
        const auto context_object =
            vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto manager = vm.linker.FindClass("Landroid/preference/PreferenceManager;");
        REQUIRE(manager.has_value());
        const auto method = vm.linker.FindDirectMethod(
            *manager, "getDefaultSharedPreferences",
            "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        REQUIRE(method.has_value());
        const auto outcome = vm.interpreter.Call(
            *method, std::vector<VmValue>{VmValue::Ref(context_object)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.ref;
    };
    {
        auto store = SandboxStore::Open(root.path, kPackage, kPackage);
        FileVm vm(store.get());
        const auto prefs = open_default(vm);
        const auto editor = vm.CallOn(prefs, "edit",
                                      "()Landroid/content/SharedPreferences$Editor;")
                               .ref;
        static_cast<void>(vm.CallOn(
            editor, "putInt",
            "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("dobyear")),
             VmValue::Int(2010)}));
        static_cast<void>(vm.CallOn(editor, "putFloat",
            "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("volume")), VmValue::Float(0.625F)}));
        CHECK(vm.CallOn(editor, "commit", "()Z").AsInt() == 1);
        // The default entry maps to the platform file layout.
        CHECK(vm.NativeRead("/data/data/com.example.game/shared_prefs/"
                            "com.example.game_preferences.xml")
                  .find("<int name=\"dobyear\" value=\"2010\" />") !=
              std::string::npos);
    }
    {
        auto store = SandboxStore::Open(root.path, kPackage, kPackage);
        FileVm vm(store.get());
        const auto prefs = open_default(vm);
        CHECK(vm.CallOn(prefs, "getInt", "(Ljava/lang/String;I)I",
                        {VmValue::Ref(vm.interpreter.NewStringUtf8("dobyear")),
                         VmValue::Int(0)})
                  .AsInt() == 2010);
        CHECK(vm.CallOn(prefs, "getFloat", "(Ljava/lang/String;F)F",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("volume")), VmValue::Float(0)}).AsFloat() == 0.625F);
    }
}

TEST_CASE("SharedPreferences failed load is retried without publishing an empty store") {
    const TemporaryRoot root("retry-prefs");
    auto store = SandboxStore::Open(root.path, kPackage, kPackage);
    FileVm vm(store.get());
    const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    const auto type = vm.model.ObjectClass(base);
    const auto slot = vm.linker.FindVtableIndex(type, "getSharedPreferences",
        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    REQUIRE(slot.has_value());
    const auto method = vm.linker.Class(type).vtable[*slot];
    const std::string path = "/data/data/com.example.game/shared_prefs/broken.xml";
    vm.vfs.CreateDirectory("/data/data/com.example.game/shared_prefs");
    const auto write = [&](const std::string& xml) {
        vm.interpreter.IO().WriteFile(path, std::as_bytes(std::span(xml.data(), xml.size())));
    };
    write("invalid XML");
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto outcome = vm.interpreter.Call(method, std::vector<VmValue>{VmValue::Ref(base),
            VmValue::Ref(vm.interpreter.NewStringUtf8("broken")), VmValue::Int(0)});
        REQUIRE(outcome.exception.IsValid());
        CHECK_FALSE(vm.context->preferences_loaded["broken"]);
    }
    write("<?xml version=\"1.0\" encoding=\"utf-8\"?><map><int name=\"answer\" value=\"7\" /></map>");
    PrefsDriver driver{vm};
    const auto prefs = driver.Open("broken");
    CHECK(vm.CallOn(prefs, "getInt", "(Ljava/lang/String;I)I",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("answer")), VmValue::Int(0)}).AsInt() == 7);
}

TEST_CASE("SharedPreferences persist as platform XML across sessions") {
    const TemporaryRoot root("prefs");
    {
        auto store = SandboxStore::Open(root.path, kPackage, kPackage);
        FileVm vm(store.get());
        PrefsDriver driver{vm};
        const auto prefs = driver.Open("settings");
        const auto editor = driver.Editor(prefs);
        static_cast<void>(vm.CallOn(
            editor, "putInt",
            "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("launches")),
             VmValue::Int(3)}));
        static_cast<void>(vm.CallOn(
            editor, "putString",
            "(Ljava/lang/String;Ljava/lang/String;)"
            "Landroid/content/SharedPreferences$Editor;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("user")),
             VmValue::Ref(vm.interpreter.NewStringUtf8("tester"))}));
        // Nothing is on disk until commit, which is the flush point.
        CHECK(vm.CallOn(editor, "commit", "()Z").AsInt() == 1);

        // A title that reads shared_prefs directly sees the same fact.
        CHECK(vm.NativeRead(
                  "/data/data/com.example.game/shared_prefs/settings.xml")
                  .find("<int name=\"launches\" value=\"3\" />") !=
              std::string::npos);
        vm.vfs.FlushAll();
    }

    auto store = SandboxStore::Open(root.path, kPackage, kPackage);
    FileVm vm(store.get());
    PrefsDriver driver{vm};
    const auto prefs = driver.Open("settings");
    CHECK(vm.CallOn(prefs, "getInt", "(Ljava/lang/String;I)I",
                    {VmValue::Ref(vm.interpreter.NewStringUtf8("launches")),
                     VmValue::Int(0)})
              .AsInt() == 3);
    const auto user = vm.CallOn(
        prefs, "getString",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("user")),
         VmValue::Ref(VmObjectRef{})});
    CHECK(vm.interpreter.StringUtf8(user.ref) == "tester");
    // An absent key still answers the caller's default.
    CHECK(vm.CallOn(prefs, "getInt", "(Ljava/lang/String;I)I",
                    {VmValue::Ref(vm.interpreter.NewStringUtf8("missing")),
                     VmValue::Int(42)})
              .AsInt() == 42);
}
