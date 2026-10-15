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

std::vector<IntrinsicClassDecl> FileFilterTestIntrinsics() {
    std::vector<IntrinsicClassDecl> result;
    auto filename = IntrinsicClassBuilder::Class(
        "Ltest/DatFilenameFilter;", "Ljava/lang/Object;",
        {"Ljava/io/FilenameFilter;"});
    filename.VirtualMethod(
        "accept", "(Ljava/io/File;Ljava/lang/String;)Z",
        [](IntrinsicContext& call) {
            return VmValue::Int(
                call.vm.StringUtf8(call.arguments[1].ref).ends_with(".dat"));
        });
    result.push_back(std::move(filename).Build());

    auto file = IntrinsicClassBuilder::Class(
        "Ltest/SavFileFilter;", "Ljava/lang/Object;",
        {"Ljava/io/FileFilter;"});
    file.VirtualMethod("accept", "(Ljava/io/File;)Z",
        [](IntrinsicContext& call) {
            const auto slots =
                call.vm.Model().InstanceSlots(call.arguments[0].ref);
            return VmValue::Int(
                call.vm.StringUtf8(VmObjectRef(slots[0].bits))
                    .ends_with(".sav"));
        });
    result.push_back(std::move(file).Build());

    auto throwing = IntrinsicClassBuilder::Class(
        "Ltest/ThrowingFilenameFilter;", "Ljava/lang/Object;",
        {"Ljava/io/FilenameFilter;"});
    throwing.VirtualMethod(
        "accept", "(Ljava/io/File;Ljava/lang/String;)Z",
        [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "filter rejected the traversal"};
        });
    result.push_back(std::move(throwing).Build());

    auto gc_filename = IntrinsicClassBuilder::Class(
        "Ltest/GcFilenameFilter;", "Ljava/lang/Object;",
        {"Ljava/io/FilenameFilter;"});
    gc_filename.VirtualMethod(
        "accept", "(Ljava/io/File;Ljava/lang/String;)Z",
        [](IntrinsicContext& call) {
            static_cast<void>(
                call.vm.CollectGarbage("file-filename-filter-callback"));
            return VmValue::Int(
                call.vm.StringUtf8(call.arguments[1].ref).ends_with(".dat"));
        });
    result.push_back(std::move(gc_filename).Build());

    auto gc_file = IntrinsicClassBuilder::Class(
        "Ltest/GcFileFilter;", "Ljava/lang/Object;",
        {"Ljava/io/FileFilter;"});
    gc_file.VirtualMethod("accept", "(Ljava/io/File;)Z",
        [](IntrinsicContext& call) {
            static_cast<void>(
                call.vm.CollectGarbage("file-file-filter-callback"));
            const auto slots =
                call.vm.Model().InstanceSlots(call.arguments[0].ref);
            return VmValue::Int(
                call.vm.StringUtf8(VmObjectRef(slots[0].bits))
                    .ends_with(".sav"));
        });
    result.push_back(std::move(gc_file).Build());

    const auto add_file_constructor = [](IntrinsicClassBuilder& builder) {
        builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext& call) {
            call.vm.Model().InstanceSlots(call.receiver)[0] = {
                call.arguments[0].ref.Value(), SlotTag::ref};
            return VmValue::Void();
        });
    };
    auto fresh_path_file = IntrinsicClassBuilder::Class(
        "Ltest/FreshPathFile;", "Ljava/io/File;");
    add_file_constructor(fresh_path_file);
    fresh_path_file.OverrideMethod(
        "getPath", "()Ljava/lang/String;", [](IntrinsicContext& call) {
            const auto path = VmObjectRef(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits);
            return VmValue::Ref(
                call.vm.NewStringUtf8(call.vm.StringUtf8(path)));
        });
    result.push_back(std::move(fresh_path_file).Build());

    auto gc_path_file = IntrinsicClassBuilder::Class(
        "Ltest/GcPathFile;", "Ljava/io/File;");
    add_file_constructor(gc_path_file);
    gc_path_file.OverrideMethod(
        "getPath", "()Ljava/lang/String;", [](IntrinsicContext& call) {
            static_cast<void>(
                call.vm.CollectGarbage("file-compare-to-callback"));
            return VmValue::Ref(VmObjectRef(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits));
        });
    result.push_back(std::move(gc_path_file).Build());
    return result;
}

} // namespace

TEST_CASE("DVM-243 original StatFs snapshots refresh guest quota and preserve errors") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        const TemporaryRoot root("statfs");
        auto store = SandboxStore::Open(root.path, kPackage, kPackage,
                                       {.byte_quota = 16 * 4096, .maximum_files = 32});
        FileVm f(store.get(), true, {.backend = backend});
        f.vfs.AddPathAlias("/storage/emulated/0", "/sdcard");
        const auto type = f.linker.ResolveDescriptor("Landroid/os/StatFs;");
        CHECK(f.linker.Class(type).is_boot_dex);
        const auto stat = f.interpreter.NewIntrinsicInstance("Landroid/os/StatFs;");
        const auto roots = f.interpreter.ProtectReferences(std::array{stat});
        const auto refresh = [&](const char* path) {
            return f.CallOnOutcome(stat, "restat", "(Ljava/lang/String;)V",
                                   {VmValue::Ref(f.interpreter.NewStringUtf8(path))});
        };
        f.CallOn(stat, "<init>", "(Ljava/lang/String;)V",
                 {VmValue::Ref(f.interpreter.NewStringUtf8("/sdcard"))});
        CHECK(f.CallOn(stat, "getBlockSizeLong", "()J").AsLong() == 4096);
        CHECK(f.CallOn(stat, "getBlockCountLong", "()J").AsLong() == 16);
        CHECK(f.CallOn(stat, "getFreeBlocks", "()I").AsInt() == 16);
        CHECK(f.CallOn(stat, "getAvailableBytes", "()J").AsLong() == 16 * 4096);
        REQUIRE_FALSE(refresh("/data").exception.IsValid()); // SQLiteGlobal page-size path
        CHECK(f.CallOn(stat, "getBlockSize", "()I").AsInt() == 4096);
        const auto fd = f.vfs.Open("/sdcard/save", {.write = true, .create = true});
        const std::vector<std::byte> data(4096);
        REQUIRE(f.vfs.Write(fd, data) == data.size());
        CHECK(f.CallOn(stat, "getFreeBlocks", "()I").AsInt() == 16); // cached snapshot
        REQUIRE_FALSE(refresh("/storage/emulated/0/save").exception.IsValid());
        CHECK(f.CallOn(stat, "getFreeBlocksLong", "()J").AsLong() == 15);
        CHECK(f.CallOn(stat, "getFreeBytes", "()J").AsLong() == 15 * 4096);
        CHECK(f.CallOn(stat, "getTotalBytes", "()J").AsLong() == 16 * 4096);
        const auto fail = [&](const VmCallOutcome& outcome, const char* exception) {
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == exception);
        };
        fail(refresh("/sdcard/absent"), "Ljava/lang/IllegalArgumentException;");
        CHECK(f.CallOn(stat, "getFreeBlocksLong", "()J").AsLong() == 15);
        f.vfs.PutFile("/system/ro", {}, false);
        fail(refresh("/system/ro"), "Ljava/lang/IllegalArgumentException;");
        CHECK(f.CallOn(stat, "getFreeBlocksLong", "()J").AsLong() == 15);
        const auto null = f.CallOnOutcome(stat, "restat", "(Ljava/lang/String;)V", {VmValue::Ref(VmObjectRef{})});
        fail(null, "Ljava/lang/NullPointerException;");
        f.interpreter.IO().SetFileSystem(nullptr);
        fail(refresh("/sdcard"), "Ljava/lang/IllegalArgumentException;");
        f.interpreter.IO().SetFileSystem(&f.io_file_system);
        f.vfs.Truncate(fd, 0);
        REQUIRE_FALSE(refresh("/data/data/com.example.game").exception.IsValid());
        CHECK(f.CallOn(stat, "getAvailableBlocks", "()I").AsInt() == 16);
        f.vfs.Close(fd); f.vfs.RemoveFile("/sdcard/save");
        const auto other = f.interpreter.NewIntrinsicInstance("Landroid/os/StatFs;");
        const auto other_root = f.interpreter.ProtectReferences(std::array{other});
        fail(f.CallOnOutcome(other, "<init>", "(Ljava/lang/String;)V",
                              {VmValue::Ref(f.interpreter.NewStringUtf8("/bad/path"))}),
             "Ljava/lang/IllegalArgumentException;");
        static_cast<void>(f.interpreter.CollectGarbage());
        CHECK(f.CallOn(stat, "getFreeBlocksLong", "()J").AsLong() == 16);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1); CHECK(hits.front().id == "runtime.vfs_filesystem_stats");
    }
}

TEST_CASE("DVM-243 original StatFs retains long values and API19 int truncation") {
    constexpr auto blocks = (UINT64_C(1) << 31) + 5;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        const TemporaryRoot root("statfs-large");
        auto store = SandboxStore::Open(root.path, kPackage, kPackage, {.byte_quota = blocks * 4096});
        FileVm f(store.get(), true, {.backend = backend});
        const auto stat = f.interpreter.NewIntrinsicInstance("Landroid/os/StatFs;");
        const auto roots = f.interpreter.ProtectReferences(std::array{stat});
        f.CallOn(stat, "<init>", "(Ljava/lang/String;)V", {VmValue::Ref(f.interpreter.NewStringUtf8("/sdcard"))});
        CHECK(f.CallOn(stat, "getAvailableBlocksLong", "()J").AsLong() == static_cast<std::int64_t>(blocks));
        CHECK(f.CallOn(stat, "getFreeBlocks", "()I").AsInt() == INT32_MIN + 5);
        CHECK(f.CallOn(stat, "getTotalBytes", "()J").AsLong() == static_cast<std::int64_t>(blocks * 4096));
    }
}

TEST_CASE("DVM-217 AtomicFile commits rolls back and recovers through VFS") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        const TemporaryRoot root("atomic-file");
        const std::string path = std::string(kWritableRoots[0]) + "/save/progress";
        InterpreterConfig config;
        config.backend = backend;
        {
            auto store = SandboxStore::Open(root.path, kPackage, kPackage);
            FileVm vm(store.get(), true, config);
            const auto type = vm.linker.ResolveDescriptor("Lcom/android/internal/os/AtomicFile;");
            CHECK(vm.linker.Class(type).is_boot_dex);
            CHECK_FALSE(vm.linker.Class(type).is_intrinsic);
            const auto file = vm.interpreter.NewIntrinsicInstance("Lcom/android/internal/os/AtomicFile;");
            const auto file_root = vm.interpreter.ProtectReferences(std::array{file});
            vm.CallOn(file, "<init>", "(Ljava/io/File;)V", {VmValue::Ref(vm.NewFile(path))});
            const auto write = [&](const std::string& text, const char* finish) {
                const auto stream = vm.CallOn(file, "startWrite", "()Ljava/io/FileOutputStream;").ref;
                const auto stream_root = vm.interpreter.ProtectReferences(std::array{stream});
                for (const char c : text)
                    vm.CallOn(stream, "write", "(I)V", {VmValue::Int(static_cast<unsigned char>(c))});
                if (finish)
                    vm.CallOn(file, finish, "(Ljava/io/FileOutputStream;)V", {VmValue::Ref(stream)});
                else
                    vm.CallOn(stream, "close", "()V");
            };
            // Save into a new directory through the normal VFS open path.
            write("committed", "finishWrite");
            CHECK(vm.NativeRead(path) == "committed");
            CHECK_FALSE(vm.BoolOn(vm.NewFile(path + ".bak"), "exists"));
            const auto permissions = vm.linker.FindDirectMethod(
                vm.linker.ResolveDescriptor("Landroid/os/FileUtils;"),
                "setPermissions", "(Ljava/lang/String;III)I");
            REQUIRE(permissions.has_value());
            const auto permission_result = vm.interpreter.Call(*permissions, std::array{
                VmValue::Ref(vm.interpreter.NewStringUtf8(path)),
                VmValue::Int(0771), VmValue::Int(-1), VmValue::Int(-1)});
            REQUIRE_FALSE(permission_result.exception.IsValid());
            CHECK(permission_result.value.AsInt() == 38);
            CHECK_FALSE(vm.logger.Snapshot(ogplay::core::LogLevel::warn).empty());
            const auto hits = vm.ledger.Unimplemented();
            REQUIRE(hits.size() == 1);
            CHECK(hits[0].id == "libcore.io.Posix.chmod");
            CHECK(hits[0].count == 1);
            write("rolled back", "failWrite");
            CHECK(vm.NativeRead(path) == "committed");
            CHECK_FALSE(vm.BoolOn(vm.NewFile(path + ".bak"), "exists"));
            write("replacement", "finishWrite");
            CHECK(vm.NativeRead(path) == "replacement");
            // Interrupt a write, persist both files, then open a new VM/store.
            write("incomplete", nullptr);
            CHECK(vm.NativeRead(path + ".bak") == "replacement");
            vm.vfs.FlushAll();
        }
        {
            auto store = SandboxStore::Open(root.path, kPackage, kPackage);
            FileVm vm(store.get(), true, config);
            const auto file = vm.interpreter.NewIntrinsicInstance("Lcom/android/internal/os/AtomicFile;");
            const auto file_root = vm.interpreter.ProtectReferences(std::array{file});
            vm.CallOn(file, "<init>", "(Ljava/io/File;)V", {VmValue::Ref(vm.NewFile(path))});
            const auto bytes = vm.CallOn(file, "readFully", "()[B").ref;
            const auto actual = vm.model.ReadByteRegion(bytes, 0, vm.model.ArrayLength(bytes));
            const std::string expected = "replacement";
            CHECK(actual.size() == expected.size());
            for (std::size_t i = 0; i < actual.size(); ++i)
                CHECK(actual[i] == static_cast<std::byte>(expected[i]));
            CHECK(vm.NativeRead(path) == expected);
            CHECK_FALSE(vm.BoolOn(vm.NewFile(path + ".bak"), "exists"));
            vm.vfs.FlushAll();
        }
    }
}

TEST_CASE("File declarations match Android class shape through second batch") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto declaration = [&](const std::string_view descriptor)
        -> const IntrinsicClassDecl& {
        const auto found = std::ranges::find(
            catalog, descriptor, &IntrinsicClassDecl::descriptor);
        REQUIRE(found != catalog.end());
        return *found;
    };
    const auto& file = declaration("Ljava/io/File;");
    CHECK(file.interfaces == std::vector<std::string>{
        "Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    const auto path = std::ranges::find(
        file.fields, std::string_view{"path"}, &IntrinsicFieldDecl::name);
    REQUIRE(path != file.fields.end());
    CHECK(path->access_flags == 0x0002U);
    for (const auto& method : file.methods) {
        if (method.name != "<init>")
            CHECK((method.access_flags & 0x0010U) == 0U);
    }
    const auto bridge = std::ranges::find_if(file.methods, [](const auto& method) {
        return method.name == "compareTo" &&
               method.descriptor == "(Ljava/lang/Object;)I";
    });
    REQUIRE(bridge != file.methods.end());
    CHECK(bridge->access_flags == 0x1041U);
    for (const auto& [name, descriptor] :
         std::vector<std::pair<std::string_view, std::string_view>>{
             {"canRead", "()Z"},
             {"canWrite", "()Z"},
             {"isFile", "()Z"},
             {"list", "(Ljava/io/FilenameFilter;)[Ljava/lang/String;"},
             {"listFiles", "()[Ljava/io/File;"},
             {"listFiles", "(Ljava/io/FilenameFilter;)[Ljava/io/File;"},
             {"listFiles", "(Ljava/io/FileFilter;)[Ljava/io/File;"},
             {"renameTo", "(Ljava/io/File;)Z"},
             {"setWritable", "(Z)Z"},
             {"setWritable", "(ZZ)Z"}}) {
        const auto method = std::ranges::find_if(
            file.methods, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor == descriptor;
            });
        REQUIRE(method != file.methods.end());
        CHECK(method->access_flags == 0x0001U);
    }

    for (const auto descriptor : {"Ljava/io/FilenameFilter;",
                                  "Ljava/io/FileFilter;"}) {
        const auto& filter = declaration(descriptor);
        CHECK(filter.is_interface);
        REQUIRE(filter.methods.size() == 1U);
        CHECK(filter.methods[0].name == "accept");
        CHECK(filter.methods[0].access_flags == 0x0401U);
    }

    const auto& output = declaration("Ljava/io/FileOutputStream;");
    const auto fd = std::ranges::find(
        output.fields, std::string_view{"fd"}, &IntrinsicFieldDecl::name);
    REQUIRE(fd != output.fields.end());
    CHECK(fd->descriptor == "Ljava/io/FileDescriptor;");
    CHECK(fd->access_flags == 0x0002U);
    const auto should_close = std::ranges::find(
        output.fields, std::string_view{"shouldClose"},
        &IntrinsicFieldDecl::name);
    REQUIRE(should_close != output.fields.end());
    CHECK(should_close->access_flags == 0x0012U);
    for (const auto& [name, descriptor, flags] :
         std::vector<std::tuple<std::string_view, std::string_view,
                                std::uint32_t>>{
             {"<init>", "(Ljava/io/File;)V", 0x10001U},
             {"<init>", "(Ljava/io/File;Z)V", 0x10001U},
             {"<init>", "(Ljava/io/FileDescriptor;)V", 0x10001U},
             {"<init>", "(Ljava/lang/String;)V", 0x10001U},
             {"<init>", "(Ljava/lang/String;Z)V", 0x10001U},
             {"write", "(I)V", 0x0001U},
             {"write", "([BII)V", 0x0001U},
             {"close", "()V", 0x0001U},
             {"getFD", "()Ljava/io/FileDescriptor;", 0x0011U}}) {
        const auto method = std::ranges::find_if(
            output.methods, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor == descriptor;
            });
        REQUIRE(method != output.methods.end());
        CHECK(method->access_flags == flags);
    }
    CHECK(std::ranges::none_of(output.methods, [](const auto& method) {
        return (method.name == "write" && method.descriptor == "([B)V") ||
               method.name == "flush" || method.name == "getChannel";
    }));

    FileVm shape;
    const auto& output_base = shape.linker.Class(shape.linker.ResolveDescriptor("Ljava/io/OutputStream;"));
    CHECK(output_base.is_boot_dex);
    CHECK(output_base.access_flags == 0x0401U);
    for (const auto* descriptor : {"Ljava/io/ByteArrayOutputStream;", "Ljava/io/DataOutputStream;", "Ljava/io/OutputStream;"}) {
        CHECK(std::ranges::none_of(catalog, [&](const auto& c) { return c.descriptor == descriptor; }));
        const auto type = shape.linker.ResolveDescriptor(descriptor);
        for (const auto method : shape.linker.Class(type).own_virtual_methods)
            CHECK(shape.linker.Method(method).kind != MethodKind::intrinsic);
    }

    const auto& input = declaration("Ljava/io/FileInputStream;");
    const auto input_fd = std::ranges::find(
        input.fields, std::string_view{"fd"}, &IntrinsicFieldDecl::name);
    REQUIRE(input_fd != input.fields.end());
    CHECK(input_fd->descriptor == "Ljava/io/FileDescriptor;");
    CHECK(input_fd->access_flags == 0x0002U);
    const auto input_should_close = std::ranges::find(
        input.fields, std::string_view{"shouldClose"},
        &IntrinsicFieldDecl::name);
    REQUIRE(input_should_close != input.fields.end());
    CHECK(input_should_close->access_flags == 0x0012U);
    for (const auto& [name, descriptor, flags] :
         std::vector<std::tuple<std::string_view, std::string_view,
                                std::uint32_t>>{
             {"<init>", "(Ljava/io/File;)V", 0x10001U},
             {"<init>", "(Ljava/io/FileDescriptor;)V", 0x10001U},
             {"<init>", "(Ljava/lang/String;)V", 0x10001U},
             {"available", "()I", 0x0001U},
             {"close", "()V", 0x0001U},
             {"getFD", "()Ljava/io/FileDescriptor;", 0x0011U},
             {"read", "()I", 0x0001U},
             {"read", "([BII)I", 0x0001U},
             {"skip", "(J)J", 0x0001U}}) {
        const auto method = std::ranges::find_if(
            input.methods, [&](const auto& candidate) {
                return candidate.name == name &&
                       candidate.descriptor == descriptor;
            });
        REQUIRE(method != input.methods.end());
        CHECK(method->access_flags == flags);
    }
    CHECK(std::ranges::none_of(input.methods, [](const auto& method) {
        return (method.name == "read" && method.descriptor == "([B)I") ||
               method.name == "getChannel";
    }));

    const auto& input_base = shape.linker.Class(shape.linker.ResolveDescriptor("Ljava/io/InputStream;"));
    CHECK(input_base.is_boot_dex);
    CHECK(input_base.access_flags == 0x0401U);
    CHECK(std::ranges::none_of(catalog, [](const auto& c) { return c.descriptor == "Ljava/io/InputStream;" || c.descriptor == "Ljava/io/DataInputStream;"; }));
    const auto input_read = shape.linker.FindVtableIndex(input_base.id, "read", "()I");
    REQUIRE(input_read);
    CHECK(shape.linker.Method(input_base.vtable[*input_read]).kind == MethodKind::abstract);

}

TEST_CASE("File first-batch path and object semantics match on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        vm.vfs.CreateDirectory("/data");
        vm.vfs.CreateDirectory("/data/game");
        vm.vfs.SetWorkingDirectory("/data/game");
        static_cast<void>(
            vm.interpreter.SetSystemProperty("user.dir", "/data/game"));

        const auto relative = vm.NewFile("saves/.slot");
        CHECK(vm.interpreter.StringUtf8(
                  vm.CallOn(relative, "getName", "()Ljava/lang/String;").ref) ==
              ".slot");
        CHECK(vm.interpreter.StringUtf8(
                  vm.CallOn(relative, "getParent", "()Ljava/lang/String;").ref) ==
              "saves");
        const auto parent = vm.CallOn(
            relative, "getParentFile", "()Ljava/io/File;").ref;
        CHECK(vm.interpreter.StringUtf8(
                  vm.CallOn(parent, "getPath", "()Ljava/lang/String;").ref) ==
              "saves");
        CHECK_FALSE(vm.BoolOn(relative, "isAbsolute"));
        CHECK(vm.BoolOn(relative, "isHidden"));
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  relative, "getAbsolutePath", "()Ljava/lang/String;").ref) ==
              "/data/game/saves/.slot");
        const auto absolute = vm.CallOn(
            relative, "getAbsoluteFile", "()Ljava/io/File;").ref;
        CHECK(vm.interpreter.StringUtf8(
                  vm.CallOn(absolute, "getPath", "()Ljava/lang/String;").ref) ==
              "/data/game/saves/.slot");

        const auto same = vm.NewFile("saves/.slot");
        const auto later = vm.NewFile("saves/z");
        CHECK(vm.CallOn(relative, "equals", "(Ljava/lang/Object;)Z",
                        {VmValue::Ref(same)}).AsInt() == 1);
        CHECK(vm.CallOn(relative, "hashCode", "()I").AsInt() ==
              vm.CallOn(same, "hashCode", "()I").AsInt());
        CHECK(vm.CallOn(relative, "compareTo", "(Ljava/io/File;)I",
                        {VmValue::Ref(later)}).AsInt() < 0);
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  relative, "toString", "()Ljava/lang/String;").ref) ==
              "saves/.slot");

        const auto not_file = vm.interpreter.NewStringUtf8("not a file");
        const auto cast = vm.CallOnOutcome(
            relative, "compareTo", "(Ljava/lang/Object;)I",
            {VmValue::Ref(not_file)});
        REQUIRE(cast.exception.IsValid());
        CHECK(vm.linker.Class(cast.exception_class).descriptor ==
              "Ljava/lang/ClassCastException;");

        const auto roots = vm.CallStatic(
            "Ljava/io/File;", "listRoots", "()[Ljava/io/File;").ref;
        REQUIRE(vm.model.ArrayLength(roots) == 1);
        const auto root = vm.model.GetObjectElement(roots, 0);
        CHECK(vm.interpreter.StringUtf8(
                  vm.CallOn(root, "getPath", "()Ljava/lang/String;").ref) ==
              "/");

        vm.vfs.CreateDirectory("/data/game/save dir");
        const auto directory = vm.NewFile("save dir");
        const auto uri = vm.CallOn(
            directory, "toURI", "()Ljava/net/URI;").ref;
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  uri, "toString", "()Ljava/lang/String;").ref) ==
              "file:/data/game/save%20dir/");

        const auto legal = vm.NewFile("save!$&'()+,;=@.dat");
        const auto legal_uri = vm.CallOn(
            legal, "toURI", "()Ljava/net/URI;").ref;
        CHECK(vm.interpreter.StringUtf8(vm.CallOn(
                  legal_uri, "toString", "()Ljava/lang/String;").ref) ==
              "file:/data/game/save!$&'()+,;=@.dat");
    }
}

TEST_CASE("File mkdir creates one level while mkdirs creates parents") {
    FileVm vm;
    vm.vfs.CreateDirectory("/data");
    const auto missing_parent = vm.NewFile("/missing/leaf");
    CHECK_FALSE(vm.BoolOn(missing_parent, "mkdir"));
    CHECK_FALSE(vm.io_file_system.Stat("/missing/leaf").has_value());

    const auto one_level = vm.NewFile("/data/leaf");
    CHECK(vm.BoolOn(one_level, "mkdir"));
    CHECK_FALSE(vm.BoolOn(one_level, "mkdir"));
    CHECK(vm.vfs.Stat("/data/leaf").is_directory);

    const auto recursive = vm.NewFile("/data/a/b");
    CHECK(vm.BoolOn(recursive, "mkdirs"));
    CHECK(vm.vfs.Stat("/data/a/b").is_directory);
}

TEST_CASE("File second-batch VFS and filter semantics match on both backends") {
    const auto filters = FileFilterTestIntrinsics();
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config, filters);
        const auto bytes = [](const std::string_view text) {
            std::vector<std::byte> result;
            for (const auto value : text)
                result.push_back(static_cast<std::byte>(value));
            return result;
        };
        vm.vfs.PutFile("/data/game/a.dat", bytes("dat"), true);
        vm.vfs.PutFile("/data/game/b.sav", bytes("save"), true);
        vm.vfs.PutFile("/data/game/c.txt", bytes("text"), false);
        vm.vfs.CreateDirectory("/data/game/sub");

        const auto directory = vm.NewFile("/data/game");
        const auto writable = vm.NewFile("/data/game/a.dat");
        const auto read_only = vm.NewFile("/data/game/c.txt");
        const auto missing = vm.NewFile("/data/game/missing");
        CHECK(vm.BoolOn(writable, "canRead"));
        CHECK(vm.BoolOn(writable, "canWrite"));
        CHECK(vm.BoolOn(writable, "isFile"));
        CHECK_FALSE(vm.BoolOn(directory, "isFile"));
        CHECK(vm.BoolOn(read_only, "canRead"));
        CHECK_FALSE(vm.BoolOn(read_only, "canWrite"));
        CHECK_FALSE(vm.BoolOn(missing, "canRead"));
        CHECK_FALSE(vm.BoolOn(missing, "canWrite"));
        CHECK_FALSE(vm.BoolOn(missing, "isFile"));

        CHECK(vm.CallOn(writable, "setWritable", "(Z)Z",
                        {VmValue::Int(1)}).AsInt() == 1);
        CHECK(vm.CallOn(writable, "setWritable", "(ZZ)Z",
                        {VmValue::Int(1), VmValue::Int(0)}).AsInt() == 1);
        CHECK(vm.CallOn(writable, "setWritable", "(Z)Z",
                        {VmValue::Int(0)}).AsInt() == 0);
        CHECK(vm.CallOn(read_only, "setWritable", "(Z)Z",
                        {VmValue::Int(1)}).AsInt() == 0);

        const auto array_strings = [&](const VmObjectRef array) {
            std::vector<std::string> result;
            for (JniSize index = 0; index < vm.model.ArrayLength(array);
                 ++index) {
                result.push_back(vm.interpreter.StringUtf8(
                    vm.model.GetObjectElement(array, index)));
            }
            return result;
        };
        const auto file_paths = [&](const VmObjectRef array) {
            std::vector<std::string> result;
            for (JniSize index = 0; index < vm.model.ArrayLength(array);
                 ++index) {
                const auto file = vm.model.GetObjectElement(array, index);
                result.push_back(vm.interpreter.StringUtf8(vm.CallOn(
                    file, "getPath", "()Ljava/lang/String;").ref));
            }
            return result;
        };

        const auto dat_filter = vm.interpreter.NewIntrinsicInstance(
            "Ltest/DatFilenameFilter;");
        const auto names = vm.CallOn(
            directory, "list",
            "(Ljava/io/FilenameFilter;)[Ljava/lang/String;",
            {VmValue::Ref(dat_filter)}).ref;
        CHECK(array_strings(names) == std::vector<std::string>{"a.dat"});
        const auto null_names = vm.CallOn(
            directory, "list",
            "(Ljava/io/FilenameFilter;)[Ljava/lang/String;",
            {VmValue::Ref(VmObjectRef{})}).ref;
        CHECK(array_strings(null_names) ==
              std::vector<std::string>{"a.dat", "b.sav", "c.txt", "sub"});

        const auto all_files = vm.CallOn(
            directory, "listFiles", "()[Ljava/io/File;").ref;
        CHECK(file_paths(all_files) == std::vector<std::string>{
            "/data/game/a.dat", "/data/game/b.sav", "/data/game/c.txt",
            "/data/game/sub"});
        const auto dat_files = vm.CallOn(
            directory, "listFiles",
            "(Ljava/io/FilenameFilter;)[Ljava/io/File;",
            {VmValue::Ref(dat_filter)}).ref;
        CHECK(file_paths(dat_files) ==
              std::vector<std::string>{"/data/game/a.dat"});
        const auto sav_filter = vm.interpreter.NewIntrinsicInstance(
            "Ltest/SavFileFilter;");
        const auto sav_files = vm.CallOn(
            directory, "listFiles", "(Ljava/io/FileFilter;)[Ljava/io/File;",
            {VmValue::Ref(sav_filter)}).ref;
        CHECK(file_paths(sav_files) ==
              std::vector<std::string>{"/data/game/b.sav"});

        const auto gc_filename_filter = vm.interpreter.NewIntrinsicInstance(
            "Ltest/GcFilenameFilter;");
        const auto gc_names = vm.CallOn(
            directory, "list",
            "(Ljava/io/FilenameFilter;)[Ljava/lang/String;",
            {VmValue::Ref(gc_filename_filter)}).ref;
        CHECK(array_strings(gc_names) ==
              std::vector<std::string>{"a.dat"});
        const auto gc_file_filter = vm.interpreter.NewIntrinsicInstance(
            "Ltest/GcFileFilter;");
        const auto gc_files = vm.CallOn(
            directory, "listFiles", "(Ljava/io/FileFilter;)[Ljava/io/File;",
            {VmValue::Ref(gc_file_filter)}).ref;
        CHECK(file_paths(gc_files) ==
              std::vector<std::string>{"/data/game/b.sav"});
        const auto non_directory = vm.NewFile("/data/game/a.dat");
        CHECK_FALSE(vm.CallOn(
            non_directory, "listFiles", "()[Ljava/io/File;").ref.IsValid());

        const auto throwing = vm.interpreter.NewIntrinsicInstance(
            "Ltest/ThrowingFilenameFilter;");
        const auto filter_error = vm.CallOnOutcome(
            directory, "list",
            "(Ljava/io/FilenameFilter;)[Ljava/lang/String;",
            {VmValue::Ref(throwing)});
        REQUIRE(filter_error.exception.IsValid());
        CHECK(vm.linker.Class(filter_error.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");

        const auto source = vm.NewFile("/data/game/a.dat");
        const auto target = vm.NewFile("/data/game/b.sav");
        CHECK(vm.CallOn(source, "renameTo", "(Ljava/io/File;)Z",
                        {VmValue::Ref(target)}).AsInt() == 1);
        CHECK_FALSE(vm.BoolOn(source, "exists"));
        CHECK(vm.NativeRead("/data/game/b.sav") == "dat");
        const auto bad_target = vm.NewFile("/missing/a.dat");
        CHECK(vm.CallOn(target, "renameTo", "(Ljava/io/File;)Z",
                        {VmValue::Ref(bad_target)}).AsInt() == 0);
        CHECK(vm.BoolOn(target, "exists"));
        CHECK(vm.CallOn(directory, "renameTo", "(Ljava/io/File;)Z",
                        {VmValue::Ref(vm.NewFile("/data/moved"))}).AsInt() == 0);
        const auto null_target = vm.CallOnOutcome(
            target, "renameTo", "(Ljava/io/File;)Z",
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_target.exception.IsValid());
        CHECK(vm.linker.Class(null_target.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");

        const auto new_path_file = [&](const std::string_view descriptor,
                                       const std::string_view path) {
            const auto file = vm.interpreter.NewIntrinsicInstance(descriptor);
            static_cast<void>(vm.CallOn(
                file, "<init>", "(Ljava/lang/String;)V",
                {VmValue::Ref(vm.interpreter.NewStringUtf8(path))}));
            return file;
        };
        const auto fresh_left =
            new_path_file("Ltest/FreshPathFile;", "left");
        const auto collecting_right =
            new_path_file("Ltest/GcPathFile;", "right");
        CHECK(vm.CallOn(fresh_left, "compareTo", "(Ljava/io/File;)I",
                        {VmValue::Ref(collecting_right)}).AsInt() < 0);
    }
}

TEST_CASE("File APIs fail explicitly when the guest filesystem is unavailable") {
    FileVm vm;
    vm.interpreter.IO().SetFileSystem(nullptr);
    const auto file = vm.NewFile("/data/game/save.dat");
    const auto target = vm.NewFile("/data/game/moved.dat");
    const auto expect_unsupported = [&](const std::string& name,
                                        const std::string& descriptor,
                                        std::vector<VmValue> arguments = {}) {
        const auto outcome = vm.CallOnOutcome(
            file, name, descriptor, std::move(arguments));
        REQUIRE(outcome.exception.IsValid());
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
    };
    expect_unsupported("canRead", "()Z");
    expect_unsupported("isFile", "()Z");
    expect_unsupported("list", "()[Ljava/lang/String;");
    expect_unsupported("mkdir", "()Z");
    expect_unsupported("renameTo", "(Ljava/io/File;)Z",
                       {VmValue::Ref(target)});
    expect_unsupported("setWritable", "(Z)Z", {VmValue::Int(1)});

    const auto created = vm.CallOnOutcome(file, "createNewFile", "()Z");
    REQUIRE(created.exception.IsValid());
    CHECK(vm.linker.Class(created.exception_class).descriptor ==
          "Ljava/io/IOException;");

    const auto input = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    const auto opened = vm.CallOnOutcome(
        input, "<init>", "(Ljava/io/File;)V", {VmValue::Ref(file)});
    REQUIRE(opened.exception.IsValid());
    CHECK(vm.linker.Class(opened.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("File relative absolute path fails without a guest working directory") {
    FileVm vm;
    const auto file = vm.NewFile("relative.dat");
    const auto outcome = vm.CallOnOutcome(
        file, "getAbsolutePath", "()Ljava/lang/String;");
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
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

TEST_CASE("File constructors and separators follow API 19 URI semantics") {
    FileVm vm;

    CHECK(vm.StaticField("Ljava/io/File;", "separatorChar", "C") == '/');
    CHECK(vm.StaticField("Ljava/io/File;", "pathSeparatorChar", "C") == ':');
    CHECK(vm.interpreter.StringUtf8(VmObjectRef(static_cast<std::uint32_t>(
              vm.StaticField("Ljava/io/File;", "separator",
                             "Ljava/lang/String;")))) == "/");
    CHECK(vm.interpreter.StringUtf8(VmObjectRef(static_cast<std::uint32_t>(
              vm.StaticField("Ljava/io/File;", "pathSeparator",
                             "Ljava/lang/String;")))) ==
          ":");

    const auto parent = vm.NewFile("/sdcard//game/");
    CHECK(vm.interpreter.StringUtf8(vm.CallOn(
              parent, "getPath", "()Ljava/lang/String;").ref) ==
          "/sdcard/game");
    const auto child = vm.NewFile(parent, "/saves//slot.dat/");
    CHECK(vm.interpreter.StringUtf8(vm.CallOn(
              child, "getPath", "()Ljava/lang/String;").ref) ==
          "/sdcard/game/saves/slot.dat");

    const auto uri = vm.NewUri("file:///sdcard/game%20save//slot.dat/");
    CHECK(vm.interpreter.StringUtf8(vm.CallOn(
              uri, "getRawPath", "()Ljava/lang/String;").ref) ==
          "/sdcard/game%20save//slot.dat/");
    const auto from_uri = vm.NewFileFromUri(uri);
    CHECK(vm.interpreter.StringUtf8(vm.CallOn(
              from_uri, "getPath", "()Ljava/lang/String;").ref) ==
          "/sdcard/game save/slot.dat");
}

TEST_CASE("File URI constructor rejects non-file URI components") {
    FileVm vm;
    for (const auto spec : {"relative/path", "http:///sdcard/file.dat",
                            "file:opaque", "file://host/sdcard/file.dat",
                            "file:///sdcard/file.dat?query",
                            "file:///sdcard/file.dat#fragment"}) {
        const auto file = vm.interpreter.NewIntrinsicInstance("Ljava/io/File;");
        const auto outcome = vm.CallOnOutcome(
            file, "<init>", "(Ljava/net/URI;)V",
            {VmValue::Ref(vm.NewUri(spec))});
        REQUIRE(outcome.exception.IsValid());
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
    }

    const auto malformed = vm.interpreter.NewIntrinsicInstance("Ljava/net/URI;");
    const auto outcome = vm.CallOnOutcome(
        malformed, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("file:///bad%2"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/net/URISyntaxException;");
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
