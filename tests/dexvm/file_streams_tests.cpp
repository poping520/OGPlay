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

std::vector<IntrinsicClassDecl> OutputInheritanceTestIntrinsics(
    const std::shared_ptr<std::vector<std::byte>>& written) {
    auto output = IntrinsicClassBuilder::Class(
        "Ltest/CollectingOutputStream;", "Ljava/io/OutputStream;");
    output.Constructor("()V",
                       [](IntrinsicContext&) { return VmValue::Void(); });
    output.OverrideMethod(
        "write", "(I)V", [written](IntrinsicContext& call) {
            written->push_back(static_cast<std::byte>(
                call.arguments[0].AsInt() & 0xff));
            return VmValue::Void();
        });
    std::vector<IntrinsicClassDecl> result;
    result.push_back(std::move(output).Build());
    return result;
}

std::vector<IntrinsicClassDecl> InputInheritanceTestIntrinsics(
    const std::shared_ptr<std::size_t>& cursor) {
    auto input = IntrinsicClassBuilder::Class(
        "Ltest/SequenceInputStream;", "Ljava/io/InputStream;");
    input.Constructor("()V",
                      [](IntrinsicContext&) { return VmValue::Void(); });
    input.OverrideMethod(
        "read", "()I", [cursor](IntrinsicContext&) {
            constexpr std::array values{'a', 'b', 'c', 'd', 'e', 'f'};
            if (*cursor >= values.size()) return VmValue::Int(-1);
            return VmValue::Int(values[(*cursor)++]);
        });
    std::vector<IntrinsicClassDecl> result;
    result.push_back(std::move(input).Build());
    return result;
}

} // namespace

TEST_CASE("OutputStream inherited bulk writes virtual-dispatch to subclasses") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        const auto written = std::make_shared<std::vector<std::byte>>();
        const auto intrinsics = OutputInheritanceTestIntrinsics(written);
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config, intrinsics);
        const auto output = vm.interpreter.NewIntrinsicInstance(
            "Ltest/CollectingOutputStream;");
        static_cast<void>(vm.CallOn(output, "<init>", "()V"));
        const auto bytes = vm.model.NewPrimitiveArray(
            vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);
        vm.model.WriteByteRegion(
            bytes, 0,
            std::array{std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
                       std::byte{'d'}});

        static_cast<void>(vm.CallOn(output, "write", "([B)V",
                                    {VmValue::Ref(bytes)}));
        static_cast<void>(vm.CallOn(
            output, "write", "([BII)V",
            {VmValue::Ref(bytes), VmValue::Int(1), VmValue::Int(2)}));
        static_cast<void>(vm.CallOn(output, "flush", "()V"));
        static_cast<void>(vm.CallOn(output, "close", "()V"));
        CHECK(*written == std::vector<std::byte>{
            std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'},
            std::byte{'b'}, std::byte{'c'}});
    }
}

TEST_CASE("InputStream inherited reads and skip virtual-dispatch to subclasses") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        const auto cursor = std::make_shared<std::size_t>();
        const auto intrinsics = InputInheritanceTestIntrinsics(cursor);
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config, intrinsics);
        const auto input = vm.interpreter.NewIntrinsicInstance(
            "Ltest/SequenceInputStream;");
        static_cast<void>(vm.CallOn(input, "<init>", "()V"));
        const auto bytes = vm.model.NewPrimitiveArray(
            vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);

        CHECK(vm.CallOn(input, "read", "([BII)I",
                        {VmValue::Ref(bytes), VmValue::Int(1),
                         VmValue::Int(2)}).AsInt() == 2);
        CHECK(vm.model.ReadByteRegion(bytes, 0, 4) ==
              std::vector<std::byte>{std::byte{0}, std::byte{'a'},
                                     std::byte{'b'}, std::byte{0}});
        CHECK(vm.CallOn(input, "skip", "(J)J",
                        {VmValue::Long(2)}).AsLong() == 2);
        CHECK(vm.CallOn(input, "read", "([B)I",
                        {VmValue::Ref(bytes)}).AsInt() == 2);
        CHECK(vm.model.ReadByteRegion(bytes, 0, 2) ==
              std::vector<std::byte>{std::byte{'e'}, std::byte{'f'}});
        CHECK(vm.CallOn(input, "available", "()I").AsInt() == 0);
        CHECK_FALSE(vm.BoolOn(input, "markSupported"));
        static_cast<void>(vm.CallOn(input, "mark", "(I)V",
                                    {VmValue::Int(8)}));
        static_cast<void>(vm.CallOn(input, "close", "()V"));
        const auto reset = vm.CallOnOutcome(input, "reset", "()V");
        REQUIRE(reset.exception.IsValid());
        CHECK(vm.linker.Class(reset.exception_class).descriptor ==
              "Ljava/io/IOException;");
    }
}

TEST_CASE("FileOutputStream public constructors and writes match on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        vm.vfs.CreateDirectory("/sdcard");
        vm.JavaWrite("/sdcard/save.dat", "base");

        const auto bytes = vm.model.NewPrimitiveArray(
            vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);
        vm.model.WriteByteRegion(
            bytes, 0,
            std::array{std::byte{'x'}, std::byte{'A'}, std::byte{'B'},
                       std::byte{'y'}});

        const auto append = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileOutputStream;");
        static_cast<void>(vm.CallOn(
            append, "<init>", "(Ljava/lang/String;Z)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/save.dat")),
             VmValue::Int(1)}));
        const auto append_peer = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileOutputStream;");
        static_cast<void>(vm.CallOn(
            append_peer, "<init>", "(Ljava/lang/String;Z)V",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/save.dat")),
             VmValue::Int(1)}));
        static_cast<void>(vm.CallOn(append, "write", "(I)V",
                                    {VmValue::Int('!')}));
        static_cast<void>(vm.CallOn(
            append, "write", "([BII)V",
            {VmValue::Ref(bytes), VmValue::Int(1), VmValue::Int(2)}));
        static_cast<void>(vm.CallOn(append_peer, "write", "(I)V",
                                    {VmValue::Int('C')}));
        static_cast<void>(vm.CallOn(append_peer, "close", "()V"));
        static_cast<void>(vm.CallOn(append, "close", "()V"));
        CHECK(vm.NativeRead("/sdcard/save.dat") == "base!ABC");

        const auto truncate = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileOutputStream;");
        static_cast<void>(vm.CallOn(
            truncate, "<init>", "(Ljava/io/File;Z)V",
            {VmValue::Ref(vm.NewFile("/sdcard/save.dat")), VmValue::Int(0)}));
        CHECK(vm.NativeRead("/sdcard/save.dat").empty());
        static_cast<void>(vm.CallOn(truncate, "write", "(I)V",
                                    {VmValue::Int('N')}));
        const auto fd = vm.CallOn(
            truncate, "getFD", "()Ljava/io/FileDescriptor;").ref;
        REQUIRE(fd.IsValid());
        const auto* descriptor = vm.interpreter.IO().FindDescriptor(fd);
        REQUIRE(descriptor != nullptr);
        REQUIRE(descriptor->file != nullptr);
        CHECK(vm.interpreter.IO().FileOffset(fd) == 1);
        CHECK(vm.BoolOn(fd, "valid"));
        static_cast<void>(vm.CallOn(fd, "sync", "()V"));
        CHECK(vm.NativeRead("/sdcard/save.dat") == "N");

        const auto borrowed = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileOutputStream;");
        static_cast<void>(vm.CallOn(
            borrowed, "<init>", "(Ljava/io/FileDescriptor;)V",
            {VmValue::Ref(fd)}));
        static_cast<void>(vm.CallOn(borrowed, "write", "(I)V",
                                    {VmValue::Int('D')}));
        CHECK(vm.interpreter.IO().FileOffset(fd) == 2);
        static_cast<void>(vm.CallOn(borrowed, "close", "()V"));
        CHECK(vm.BoolOn(fd, "valid"));
        CHECK(vm.NativeRead("/sdcard/save.dat") == "ND");
        static_cast<void>(vm.CallOn(truncate, "close", "()V"));
        CHECK_FALSE(vm.BoolOn(fd, "valid"));
    }
}

TEST_CASE("FileOutputStream rejects null paths and read-only descriptors") {
    FileVm vm;
    vm.vfs.CreateDirectory("/sdcard");
    vm.JavaWrite("/sdcard/input.dat", "data");
    for (const auto& descriptor : {std::string{"(Ljava/io/File;)V"},
                                   std::string{"(Ljava/lang/String;)V"}}) {
        const auto output = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileOutputStream;");
        const auto outcome = vm.CallOnOutcome(
            output, "<init>", descriptor,
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(outcome.exception.IsValid());
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }

    const auto input = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    static_cast<void>(vm.CallOn(
        input, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/input.dat"))}));
    const auto fd = vm.CallOn(
        input, "getFD", "()Ljava/io/FileDescriptor;").ref;
    const auto output = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileOutputStream;");
    static_cast<void>(vm.CallOn(
        output, "<init>", "(Ljava/io/FileDescriptor;)V",
        {VmValue::Ref(fd)}));
    const auto write = vm.CallOnOutcome(
        output, "write", "(I)V", {VmValue::Int('x')});
    REQUIRE(write.exception.IsValid());
    CHECK(vm.linker.Class(write.exception_class).descriptor ==
          "Ljava/io/IOException;");
    static_cast<void>(vm.CallOn(output, "close", "()V"));
    CHECK(vm.BoolOn(fd, "valid"));

    const auto path_output = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileOutputStream;");
    static_cast<void>(vm.CallOn(
        path_output, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/null.dat"))}));
    for (const auto& descriptor : {std::string{"([B)V"},
                                   std::string{"([BII)V"}}) {
        std::vector<VmValue> arguments{VmValue::Ref(VmObjectRef{})};
        if (descriptor == "([BII)V") {
            arguments.push_back(VmValue::Int(0));
            arguments.push_back(VmValue::Int(0));
        }
        const auto outcome = vm.CallOnOutcome(
            path_output, "write", descriptor, std::move(arguments));
        REQUIRE(outcome.exception.IsValid());
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
}

TEST_CASE("FileInputStream public API shares descriptor position on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        InterpreterConfig config;
        config.backend = backend;
        FileVm vm(nullptr, true, config);
        vm.vfs.CreateDirectory("/sdcard");
        vm.JavaWrite("/sdcard/input.dat", "abcdef");

        const auto input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileInputStream;");
        static_cast<void>(vm.CallOn(
            input, "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(
                vm.interpreter.NewStringUtf8("/sdcard/input.dat"))}));
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == 'a');
        CHECK(vm.CallOn(input, "available", "()I").AsInt() == 5);
        const auto fd = vm.CallOn(
            input, "getFD", "()Ljava/io/FileDescriptor;").ref;
        REQUIRE(fd.IsValid());
        const auto* descriptor = vm.interpreter.IO().FindDescriptor(fd);
        REQUIRE(descriptor != nullptr);
        REQUIRE(descriptor->file != nullptr);
        CHECK(vm.interpreter.IO().FileOffset(fd) == 1);

        const auto borrowed = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileInputStream;");
        static_cast<void>(vm.CallOn(
            borrowed, "<init>", "(Ljava/io/FileDescriptor;)V",
            {VmValue::Ref(fd)}));
        const auto bytes = vm.model.NewPrimitiveArray(
            vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 2);
        CHECK(vm.CallOn(borrowed, "read", "([B)I",
                        {VmValue::Ref(bytes)}).AsInt() == 2);
        CHECK(vm.model.ReadByteRegion(bytes, 0, 2) ==
              std::vector<std::byte>{std::byte{'b'}, std::byte{'c'}});
        CHECK(vm.CallOn(borrowed, "skip", "(J)J",
                        {VmValue::Long(1)}).AsLong() == 1);
        CHECK(vm.interpreter.IO().FileOffset(fd) == 4);
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == 'e');
        static_cast<void>(vm.CallOn(borrowed, "close", "()V"));
        CHECK(vm.BoolOn(fd, "valid"));
        CHECK(vm.CallOn(input, "read", "()I").AsInt() == 'f');
        static_cast<void>(vm.CallOn(input, "close", "()V"));
        CHECK_FALSE(vm.BoolOn(fd, "valid"));
        const auto closed = vm.CallOnOutcome(input, "read", "()I");
        REQUIRE(closed.exception.IsValid());
        CHECK(vm.linker.Class(closed.exception_class).descriptor ==
              "Ljava/io/IOException;");

        const auto from_file = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileInputStream;");
        static_cast<void>(vm.CallOn(
            from_file, "<init>", "(Ljava/io/File;)V",
            {VmValue::Ref(vm.NewFile("/sdcard/input.dat"))}));
        CHECK(vm.CallOn(from_file, "read", "()I").AsInt() == 'a');
        const auto negative = vm.CallOnOutcome(
            from_file, "skip", "(J)J", {VmValue::Long(-1)});
        REQUIRE(negative.exception.IsValid());
        CHECK(vm.linker.Class(negative.exception_class).descriptor ==
              "Ljava/io/IOException;");

    }
}

TEST_CASE("FileChannel transferTo copies a bounded range and preserves source position") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    InterpreterConfig config;
    config.backend = backend;
    FileVm vm(nullptr, true, config);
    vm.vfs.CreateDirectory("/sdcard");
    vm.JavaWrite("/sdcard/source.dat", "abcdef");

    const auto input = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    static_cast<void>(vm.CallOn(
        input, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/source.dat"))}));
    const auto output = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileOutputStream;");
    static_cast<void>(vm.CallOn(
        output, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/target.dat"))}));
    const auto source_channel = vm.CallOn(
        input, "getChannel", "()Ljava/nio/channels/FileChannel;").ref;
    const auto target_channel = vm.CallOn(
        output, "getChannel", "()Ljava/nio/channels/FileChannel;").ref;
    CHECK(vm.CallOn(input, "getChannel",
                    "()Ljava/nio/channels/FileChannel;").ref == source_channel);
    CHECK(vm.CallOn(output, "getChannel",
                    "()Ljava/nio/channels/FileChannel;").ref == target_channel);

    CHECK(vm.CallOn(source_channel, "size", "()J").AsLong() == 6);
    CHECK(vm.CallOn(source_channel, "position", "()J").AsLong() == 0);
    CHECK(vm.CallOn(
        source_channel, "transferTo",
        "(JJLjava/nio/channels/WritableByteChannel;)J",
        {VmValue::Long(1), VmValue::Long(3),
         VmValue::Ref(target_channel)}).AsLong() == 3);
    CHECK(vm.CallOn(source_channel, "position", "()J").AsLong() == 0);
    CHECK(vm.CallOn(target_channel, "position", "()J").AsLong() == 3);
    CHECK(vm.NativeRead("/sdcard/target.dat") == "bcd");

    static_cast<void>(vm.CallOn(source_channel, "close", "()V"));
    CHECK_FALSE(vm.BoolOn(source_channel, "isOpen"));
    CHECK_FALSE(vm.BoolOn(vm.CallOn(
        input, "getFD", "()Ljava/io/FileDescriptor;").ref, "valid"));
    static_cast<void>(vm.CallOn(target_channel, "close", "()V"));
  }
}

TEST_CASE("DexVM IO adapter preserves VFS errno on descriptor failures") {
    VirtualFileSystem vfs;
    DexVmIoVfsAdapter adapter(vfs);
    const auto expect_errno = [](const std::int32_t expected,
                                 const auto& operation) {
        try {
            operation();
            FAIL("VFS operation unexpectedly succeeded");
        } catch (const IoRuntimeError& error) {
            CHECK(error.ErrorNumber() == expected);
        }
    };
    try {
        static_cast<void>(adapter.OpenHandle(
            "/missing", true, false, false, false));
        FAIL("missing VFS file unexpectedly opened");
    } catch (const IoRuntimeError& error) {
        CHECK(error.ErrorNumber() == 2);
    }
    try {
        std::byte value{};
        static_cast<void>(adapter.ReadHandle(12345, std::span{&value, 1}));
        FAIL("invalid VFS descriptor unexpectedly read");
    } catch (const IoRuntimeError& error) {
        CHECK(error.ErrorNumber() == 9);
    }

    expect_errno(2, [&] { adapter.MakeDirectory("/missing/child"); });
    vfs.CreateDirectory("/data");
    expect_errno(17, [&] { adapter.MakeDirectory("/data"); });
    expect_errno(2, [&] { adapter.Delete("/data/missing"); });
    expect_errno(2, [&] { adapter.Rename("/data/missing", "/data/new"); });

    const TemporaryRoot root("adapter-errno");
    auto store = SandboxStore::Open(root.path, kPackage, kPackage);
    vfs.AttachSandbox(*store, std::array<std::string, 1>{"/data"});
    expect_errno(13, [&] { adapter.MakeDirectory("/system/blocked"); });
}

TEST_CASE("RandomAccessFile shares VFS position and supports resize") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    InterpreterConfig config;
    config.backend = backend;
    FileVm vm(nullptr, true, config);
    vm.vfs.CreateDirectory("/sdcard");
    vm.JavaWrite("/sdcard/random.dat", "abcdef");
    const auto file = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/RandomAccessFile;");
    static_cast<void>(vm.CallOn(
        file, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/random.dat")),
         VmValue::Ref(vm.interpreter.NewStringUtf8("rw"))}));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 6);
    static_cast<void>(vm.CallOn(file, "seek", "(J)V", {VmValue::Long(2)}));
    CHECK(vm.CallOn(file, "read", "()I").AsInt() == 'c');
    static_cast<void>(vm.CallOn(file, "write", "(I)V", {VmValue::Int('X')}));
    CHECK(vm.CallOn(file, "getFilePointer", "()J").AsLong() == 4);
    static_cast<void>(vm.CallOn(file, "setLength", "(J)V", {VmValue::Long(4)}));
    CHECK(vm.CallOn(file, "length", "()J").AsLong() == 4);
    CHECK(vm.NativeRead("/sdcard/random.dat") == "abcX");
    const auto channel = vm.CallOn(
        file, "getChannel", "()Ljava/nio/channels/FileChannel;").ref;
    CHECK(vm.CallOn(channel, "position", "()J").AsLong() == 4);
    static_cast<void>(vm.CallOn(file, "close", "()V"));
    CHECK_FALSE(vm.BoolOn(channel, "isOpen"));
  }
}

TEST_CASE("FileInputStream rejects null and missing sources") {
    FileVm vm;
    vm.vfs.CreateDirectory("/sdcard");
    for (const auto& descriptor : {
             std::string{"(Ljava/io/File;)V"},
             std::string{"(Ljava/io/FileDescriptor;)V"},
             std::string{"(Ljava/lang/String;)V"}}) {
        const auto input = vm.interpreter.NewIntrinsicInstance(
            "Ljava/io/FileInputStream;");
        const auto outcome = vm.CallOnOutcome(
            input, "<init>", descriptor,
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(outcome.exception.IsValid());
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");
    }
    const auto missing = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    const auto outcome = vm.CallOnOutcome(
        missing, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(
            vm.interpreter.NewStringUtf8("/sdcard/missing.dat"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/io/FileNotFoundException;");
}

TEST_CASE("FileInputStream getFD keeps path identity independent of cursor") {
    FileVm vm;
    vm.vfs.MountHostDirectory(
        "/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} /
                       "tests/fixtures/audio");
    const auto stream = vm.interpreter.NewIntrinsicInstance(
        "Ljava/io/FileInputStream;");
    vm.CallOn(stream, "<init>", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(
                  "/sdcard/short-vorbis.ogg"))});
    const auto fd = vm.CallOn(stream, "getFD",
                              "()Ljava/io/FileDescriptor;").ref;
    REQUIRE(fd.IsValid());
    const auto* before = vm.interpreter.IO().FindDescriptor(fd);
    REQUIRE(before != nullptr);
    CHECK(before->source == "/sdcard/short-vorbis.ogg");
    CHECK(vm.CallOn(stream, "read", "()I").AsInt() >= 0);
    const auto* after = vm.interpreter.IO().FindDescriptor(fd);
    REQUIRE(after != nullptr);
    CHECK(after->source == before->source);
    CHECK(vm.BoolOn(fd, "valid"));
    vm.CallOn(stream, "close", "()V");
    CHECK_FALSE(vm.BoolOn(fd, "valid"));
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

TEST_CASE("ZipInputStream reads guest source bytes and dispatches entry operations") {
    FileVm vm(nullptr, false);
    VmThreadRuntime threads(vm.interpreter);
    const std::vector<std::byte> payload{std::byte{'h'}, std::byte{'i'}, std::byte{'o'}, std::byte{'k'}};
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
    CHECK(vm.CallOn(zip,"available","()I").AsInt()==1);
    CHECK(vm.CallOn(zip,"read","()I").AsInt()=='h');
    CHECK(vm.CallOn(zip,"skip","(J)J",{VmValue::Long(1)}).AsLong()==1);
    CHECK(vm.CallOn(zip,"markSupported","()Z").AsInt()==0);
    CHECK(vm.CallOnOutcome(zip,"reset","()V").exception.IsValid());
    const auto output = vm.model.NewPrimitiveArray(
        array_class, JniPrimitiveKind::byte, 2);
    CHECK(vm.CallOn(zip, "read", "([BII)I",
                    {VmValue::Ref(output), VmValue::Int(0), VmValue::Int(2)})
              .AsInt() == 2);
    CHECK(vm.model.ReadByteRegion(output, 0, 2) == std::vector<std::byte>{std::byte{'o'},std::byte{'k'}});
    CHECK(vm.CallOn(zip,"read","()I").AsInt()==-1);
    CHECK(vm.CallOn(zip,"available","()I").AsInt()==0);
    static_cast<void>(vm.CallOn(zip,"close","()V"));
    static_cast<void>(vm.CallOn(zip,"close","()V"));
    CHECK(vm.CallOnOutcome(zip,"read","()I").exception.IsValid());
    const auto raw=vm.interpreter.NewIntrinsicInstance("Ljava/io/FileInputStream;");
    vm.vfs.CreateDirectory("/sdcard"); vm.interpreter.IO().WriteFile("/sdcard/archive.zip",archive);
    static_cast<void>(vm.CallOn(raw,"<init>","(Ljava/lang/String;)V",{VmValue::Ref(vm.interpreter.NewStringUtf8("/sdcard/archive.zip"))}));
    const auto filezip=vm.interpreter.NewIntrinsicInstance("Ljava/util/zip/ZipInputStream;");
    static_cast<void>(vm.CallOn(filezip,"<init>","(Ljava/io/InputStream;)V",{VmValue::Ref(raw)}));
    const std::array roots{filezip}; const auto protect=vm.interpreter.ProtectReferences(roots);
    static_cast<void>(vm.interpreter.CollectGarbage());
    static_cast<void>(vm.CallOn(filezip,"close","()V"));
    CHECK(vm.CallOnOutcome(raw,"read","()I").exception.IsValid());
}

TEST_CASE("DVM-104 FileReader inherits guest Reader behavior over VFS UTF8") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        FileVm vm(nullptr, false, InterpreterConfig{.backend=backend});
        vm.vfs.CreateDirectory("/sdcard");
        const auto file=vm.NewFile("/sdcard/reader.txt");
        const auto writer=vm.interpreter.NewIntrinsicInstance("Ljava/io/FileWriter;");
        static_cast<void>(vm.CallOn(writer,"<init>","(Ljava/io/File;Z)V",{VmValue::Ref(file),VmValue::Int(0)}));
        static_cast<void>(vm.CallOn(writer,"append","(Ljava/lang/CharSequence;)Ljava/io/Writer;",{VmValue::Ref(vm.interpreter.NewStringUtf8("A世界"))}));
        static_cast<void>(vm.CallOn(writer,"close","()V"));
        const auto reader=vm.interpreter.NewIntrinsicInstance("Ljava/io/FileReader;");
        static_cast<void>(vm.CallOn(reader,"<init>","(Ljava/io/File;)V",{VmValue::Ref(file)}));
        CHECK(vm.CallOn(reader,"read","()I").AsInt()=='A');
        CHECK(vm.CallOn(reader,"skip","(J)J",{VmValue::Long(1)}).AsLong()==1);
        CHECK(vm.CallOn(reader,"read","()I").AsInt()==0x754c);
        CHECK(vm.CallOn(reader,"read","()I").AsInt()==-1);
        static_cast<void>(vm.CallOn(reader,"close","()V"));
        static_cast<void>(vm.CallOn(reader,"close","()V"));
        CHECK(vm.CallOnOutcome(reader,"read","()I").exception.IsValid());
    }
}
