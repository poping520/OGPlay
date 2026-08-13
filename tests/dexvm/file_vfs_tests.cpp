// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/sandbox_store.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

constexpr const char* kPackage = "com.example.game";
const std::vector<std::string> kWritableRoots{"/data/data/com.example.game",
                                              "/sdcard"};

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
    Interpreter interpreter;

    explicit FileVm(SandboxStore* sandbox = nullptr)
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog());
                  linker.Link();
                  return linker;
              }(),
              model,
              [this]() {
                  context->vfs = &vfs;
                  context->package_name = kPackage;
                  IntrinsicRegistry registry;
                  RegisterAndroidBuiltins(registry, context);
                  return registry;
              }(),
              nullptr, ledger, {}) {
        interpreter.RegisterCoreBuiltins();
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

    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
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
        const auto outcome = interpreter.Call(target, arguments);
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
