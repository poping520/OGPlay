// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

constexpr const char* kPackage = "com.example.game";
const std::vector<std::string> kWritableRoots{"/data/data/com.example.game",
                                              "/sdcard"};

void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

std::vector<std::byte> MakeStoredZip(const std::string_view name,
                                     const std::span<const std::byte> payload) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : payload) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    crc = ~crc;
    const auto append_name = [&](std::vector<std::byte>& bytes) {
        for (const auto value : name) {
            bytes.push_back(static_cast<std::byte>(value));
        }
    };
    std::vector<std::byte> bytes;
    Append32(bytes, 0x04034b50U); Append16(bytes, 20); Append16(bytes, 0);
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size())); Append16(bytes, 0);
    append_name(bytes); bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    Append32(bytes, 0x02014b50U); Append16(bytes, 20); Append16(bytes, 20);
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, crc); Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append32(bytes, static_cast<std::uint32_t>(payload.size()));
    Append16(bytes, static_cast<std::uint16_t>(name.size()));
    Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0); Append16(bytes, 0);
    Append32(bytes, 0); Append32(bytes, 0); append_name(bytes);
    const auto central_size = static_cast<std::uint32_t>(bytes.size()) - central_offset;
    Append32(bytes, 0x06054b50U); Append16(bytes, 0); Append16(bytes, 0);
    Append16(bytes, 1); Append16(bytes, 1); Append32(bytes, central_size);
    Append32(bytes, central_offset); Append16(bytes, 0);
    return bytes;
}

struct TemporaryRoot final {
    std::filesystem::path path;

    explicit TemporaryRoot(const std::string& name)
        : path(std::filesystem::temp_directory_path() /
               ("ogplay-file-vfs-" + name)) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path, error);
        REQUIRE_FALSE(error);
    }
    ~TemporaryRoot() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    TemporaryRoot(const TemporaryRoot&) = delete;
    TemporaryRoot& operator=(const TemporaryRoot&) = delete;
};

// An android intrinsic VM over a VFS, optionally with a save sandbox.
struct FileVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    DexVmIoVfsAdapter io_file_system;
    Interpreter interpreter;

    explicit FileVm(SandboxStore* sandbox = nullptr)
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          io_file_system(vfs),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}) {
        context->vfs = &vfs;
        interpreter.IO().SetFileSystem(&io_file_system);
        context->package_name = kPackage;
        interpreter.SetLogger(&logger);
        if (sandbox != nullptr) vfs.AttachSandbox(*sandbox, kWritableRoots);
    }

    [[nodiscard]] VmObjectRef NewFile(const std::string& path) {
        const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
        static_cast<void>(CallOn(file, "<init>", "(Ljava/lang/String;)V",
                                 {VmValue::Ref(interpreter.NewStringUtf8(
                                      path))}));
        return file;
    }

    VmCallOutcome CallOnOutcome(const VmObjectRef receiver,
                                const std::string& name,
                                const std::string& descriptor,
                                std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index =
            linker.FindVtableIndex(receiver_class, name, descriptor);
        const auto target =
            index.has_value()
                ? linker.Class(receiver_class).vtable[*index]
                : [&] {
                      const auto direct = linker.FindDirectMethod(
                          receiver_class, name, descriptor);
                      REQUIRE_MESSAGE(direct.has_value(), name);
                      return *direct;
                  }();
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return interpreter.Call(target, arguments);
    }

    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
                   const std::string& descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto outcome = CallOnOutcome(
            receiver, name, descriptor, std::move(arguments));
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value;
    }

    VmValue CallStatic(const std::string& owner, const std::string& name,
                       const std::string& descriptor) {
        const auto java_class = linker.FindClass(owner);
        REQUIRE_MESSAGE(java_class.has_value(), owner);
        const auto method = linker.FindDirectMethod(
            *java_class, name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), name);
        const auto outcome = interpreter.Call(*method, {});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value;
    }

    [[nodiscard]] bool BoolOn(const VmObjectRef receiver,
                              const std::string& name) {
        return CallOn(receiver, name, "()Z").AsInt() != 0;
    }

    // Writes through java.io.FileOutputStream, the channel these games use.
    void JavaWrite(const std::string& path, const std::string& text) {
        const auto stream =
            interpreter.NewIntrinsicInstance("Ljava/io/FileOutputStream;");
        static_cast<void>(CallOn(stream, "<init>", "(Ljava/lang/String;)V",
                                 {VmValue::Ref(interpreter.NewStringUtf8(
                                      path))}));
        const auto array_class = linker.ResolveDescriptor("[B");
        const auto array =
            model.NewPrimitiveArray(array_class, JniPrimitiveKind::byte,
                                    static_cast<JniSize>(text.size()));
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (const auto character : text) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        model.WriteByteRegion(array, 0, bytes);
        static_cast<void>(CallOn(stream, "write", "([B)V",
                                 {VmValue::Ref(array)}));
        static_cast<void>(CallOn(stream, "close", "()V"));
    }

    // Reads the same path the way native code would: straight off the VFS.
    [[nodiscard]] std::string NativeRead(const std::string& path) {
        const auto info = vfs.Stat(path);
        const auto descriptor = vfs.Open(path, {.read = true});
        std::vector<std::byte> bytes(info.size);
        const auto count = vfs.Read(descriptor, bytes);
        vfs.Close(descriptor);
        std::string text;
        for (std::size_t index = 0; index < count; ++index) {
            text.push_back(static_cast<char>(bytes[index]));
        }
        return text;
    }
};

}  // namespace

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

TEST_CASE("Context files directory is inherited stable and VFS backed") {
    FileVm vm;
    const auto activity =
        vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
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

    const auto base =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    CHECK(vm.CallOn(base, "getFilesDir", "()Ljava/io/File;").ref == first);
}

TEST_CASE("Context files directory returns null when VFS is unavailable") {
    FileVm vm;
    vm.context->vfs = nullptr;
    const auto context =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    CHECK_FALSE(vm.CallOn(context, "getFilesDir", "()Ljava/io/File;")
                    .ref.IsValid());
}

TEST_CASE("AssetManager openFd publishes exact logical asset length") {
    FileVm vm;
    vm.context->archive.entries.push_back({
        .name = "assets/main.obb",
        .uncompressed_size = UINT32_C(0xf0000000),
    });
    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetManager;");
    const auto descriptor = vm.CallOn(
        manager, "openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("main.obb"))}).ref;
    REQUIRE(descriptor.IsValid());
    CHECK(vm.CallOn(descriptor, "getLength", "()J").AsLong() ==
          INT64_C(0xf0000000));
    static_cast<void>(vm.CallOn(descriptor, "close", "()V"));
    static_cast<void>(vm.CallOn(descriptor, "close", "()V"));
    CHECK(vm.CallOn(descriptor, "getLength", "()J").AsLong() ==
          INT64_C(0xf0000000));

    const auto missing = vm.CallOnOutcome(
        manager, "openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("missing.obb"))});
    REQUIRE(missing.exception.IsValid());
    CHECK(vm.linker.Class(missing.exception_class).descriptor ==
          "Ljava/io/FileNotFoundException;");
}

TEST_CASE("AssetManager list returns sorted unique direct children") {
    FileVm vm;
    for (const auto& name : {
             "assets/root.txt", "assets/sounds/z.ogg",
             "assets/sounds/a.ogg", "assets/sounds/sub/deep.ogg",
             "assets/sounds/sub/second.ogg", "res/not-an-asset"}) {
        vm.context->archive.entries.push_back({.name = name});
    }
    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetManager;");
    const auto list = [&](const std::string& path) {
        return vm.CallOn(
            manager, "list", "(Ljava/lang/String;)[Ljava/lang/String;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8(path))}).ref;
    };
    const auto strings = [&](const VmObjectRef array) {
        std::vector<std::string> result;
        for (JniSize index = 0; index < vm.model.ArrayLength(array); ++index) {
            result.push_back(vm.interpreter.StringUtf8(
                vm.model.GetObjectElement(array, index)));
        }
        return result;
    };

    CHECK(strings(list("")) ==
          std::vector<std::string>{"root.txt", "sounds"});
    CHECK(strings(list("sounds")) ==
          std::vector<std::string>{"a.ogg", "sub", "z.ogg"});
    const auto missing = list("missing");
    REQUIRE(missing.IsValid());
    CHECK(vm.model.ArrayLength(missing) == 0);
}

TEST_CASE("File.mkdirs creates real directories and reports the truth") {
    FileVm vm;
    const auto directory = vm.NewFile("/sdcard/game/saves");
    CHECK_FALSE(vm.BoolOn(directory, "exists"));

    CHECK(vm.BoolOn(directory, "mkdirs"));
    // If mkdirs still answered true unconditionally, none of the rest holds.
    CHECK(vm.BoolOn(directory, "exists"));
    CHECK(vm.BoolOn(directory, "isDirectory"));
    CHECK(vm.vfs.Stat("/sdcard/game/saves").is_directory);
    CHECK(vm.vfs.Stat("/sdcard/game").is_directory);

    // Java says false when the directory was already there.
    CHECK_FALSE(vm.BoolOn(directory, "mkdirs"));
}

TEST_CASE("File writes and native reads share one filesystem") {
    FileVm vm;
    vm.JavaWrite("/sdcard/game/slot0.sav", "progress-42");

    CHECK(vm.NativeRead("/sdcard/game/slot0.sav") == "progress-42");
    const auto file = vm.NewFile("/sdcard/game/slot0.sav");
    CHECK(vm.BoolOn(file, "exists"));
    CHECK_FALSE(vm.BoolOn(file, "isDirectory"));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 11);

    // A native write is visible to Java, which is the other direction.
    const auto descriptor = vm.vfs.Open(
        "/sdcard/game/native.dat",
        {.write = true, .create = true, .truncate = true});
    const std::vector<std::byte> bytes{std::byte{'h'}, std::byte{'i'}};
    CHECK(vm.vfs.Write(descriptor, bytes) == 2);
    vm.vfs.Close(descriptor);
    CHECK(vm.CallOn(vm.NewFile("/sdcard/game/native.dat"), "length", "()J")
              .AsLong() == 2);
}

TEST_CASE("File.delete removes real entries and refuses the rest") {
    FileVm vm;
    vm.JavaWrite("/sdcard/game/slot0.sav", "body");
    const auto file = vm.NewFile("/sdcard/game/slot0.sav");
    CHECK(vm.BoolOn(file, "delete"));
    CHECK_FALSE(vm.BoolOn(file, "exists"));
    // Deleting twice is false, not a silent success.
    CHECK_FALSE(vm.BoolOn(file, "delete"));

    // A non-empty directory really does refuse.
    vm.JavaWrite("/sdcard/game/keep.sav", "body");
    CHECK_FALSE(vm.BoolOn(vm.NewFile("/sdcard/game"), "delete"));
}

TEST_CASE("File.createNewFile reports whether it created anything") {
    FileVm vm;
    const auto file = vm.NewFile("/sdcard/fresh.dat");
    CHECK(vm.CallOn(file, "createNewFile", "()Z").AsInt() == 1);
    CHECK(vm.BoolOn(file, "exists"));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 0);
    CHECK(vm.CallOn(file, "createNewFile", "()Z").AsInt() == 0);
}

TEST_CASE("File.list distinguishes an empty directory from an invalid path") {
    FileVm vm;
    const auto directory = vm.NewFile("/sdcard/empty");
    REQUIRE(vm.BoolOn(directory, "mkdirs"));
    const auto empty =
        vm.CallOn(directory, "list", "()[Ljava/lang/String;").ref;
    REQUIRE(empty.IsValid());
    CHECK(vm.model.ArrayLength(empty) == 0);

    const auto missing = vm.NewFile("/sdcard/missing");
    CHECK_FALSE(vm.CallOn(missing, "list", "()[Ljava/lang/String;")
                    .ref.IsValid());
}

TEST_CASE("DataOutputStream and wrapped FileOutputStream share one close state") {
    FileVm vm;
    const auto output =
        vm.interpreter.NewIntrinsicInstance("Ljava/io/FileOutputStream;");
    static_cast<void>(vm.CallOn(
        output, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/save.dat"))}));
    const auto data =
        vm.interpreter.NewIntrinsicInstance("Ljava/io/DataOutputStream;");
    static_cast<void>(vm.CallOn(
        data, "<init>", "(Ljava/io/OutputStream;)V",
        {VmValue::Ref(output)}));
    static_cast<void>(vm.CallOn(
        data, "writeUTF", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("save"))}));

    static_cast<void>(vm.CallOn(data, "close", "()V"));
    // Java finally blocks commonly close both variables. The second close
    // must not publish the wrapped stream's former empty buffer.
    static_cast<void>(vm.CallOn(output, "close", "()V"));
    const auto saved = vm.NativeRead("/sdcard/save.dat");
    REQUIRE(saved.size() == 6);
    CHECK(static_cast<unsigned char>(saved[0]) == 0);
    CHECK(static_cast<unsigned char>(saved[1]) == 4);
    CHECK(saved.substr(2) == "save");
}

TEST_CASE("ZipInputStream adopts core input bytes and reads the entry") {
    FileVm vm;
    const std::vector<std::byte> payload{std::byte{'o'}, std::byte{'k'}};
    const auto archive = MakeStoredZip("save.dat", payload);
    const auto array_class = vm.linker.ResolveDescriptor("[B");
    const auto source_array = vm.model.NewPrimitiveArray(
        array_class, JniPrimitiveKind::byte,
        static_cast<JniSize>(archive.size()));
    vm.model.WriteByteRegion(source_array, 0, archive);
    const auto source =
        vm.interpreter.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
    static_cast<void>(vm.CallOn(source, "<init>", "([B)V",
                                {VmValue::Ref(source_array)}));
    const auto zip =
        vm.interpreter.NewIntrinsicInstance("Ljava/util/zip/ZipInputStream;");
    static_cast<void>(vm.CallOn(zip, "<init>", "(Ljava/io/InputStream;)V",
                                {VmValue::Ref(source)}));
    CHECK(vm.interpreter.IO().FindInput(source) == nullptr);

    const auto entry = vm.CallOn(
        zip, "getNextEntry", "()Ljava/util/zip/ZipEntry;").ref;
    REQUIRE(entry.IsValid());
    CHECK(vm.interpreter.StringUtf8(vm.CallOn(
              entry, "getName", "()Ljava/lang/String;").ref) == "save.dat");
    const auto output = vm.model.NewPrimitiveArray(
        array_class, JniPrimitiveKind::byte, 2);
    CHECK(vm.CallOn(zip, "read", "([BII)I",
                    {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(2)})
              .AsInt() == 2);
    CHECK(vm.model.ReadByteRegion(output, 0, 2) == payload);
}

TEST_CASE("Java file writes survive into the next session") {
    const TemporaryRoot root("crosssession");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
        FileVm vm(store.get());
        const auto directory = vm.NewFile("/sdcard/game/saves");
        CHECK(vm.BoolOn(directory, "mkdirs"));
        vm.JavaWrite("/sdcard/game/saves/slot0.sav", "progress-42");
        vm.vfs.FlushAll();  // clean shutdown
    }

    auto store = SandboxStore::Open(root.path, kPackage);
    FileVm vm(store.get());
    const auto file = vm.NewFile("/sdcard/game/saves/slot0.sav");
    CHECK(vm.BoolOn(file, "exists"));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 11);
    CHECK(vm.NativeRead("/sdcard/game/saves/slot0.sav") == "progress-42");
    CHECK(vm.BoolOn(vm.NewFile("/sdcard/game/saves"), "isDirectory"));
}

// ---- SharedPreferences (SBX-6) -------------------------------------------

namespace {

// Drives Context.getSharedPreferences -> edit -> put -> commit the way a
// title does, then reads back through the getters.
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

}  // namespace

TEST_CASE("SharedPreferences persist as platform XML across sessions") {
    const TemporaryRoot root("prefs");
    {
        auto store = SandboxStore::Open(root.path, kPackage);
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

    auto store = SandboxStore::Open(root.path, kPackage);
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
