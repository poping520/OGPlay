#include "file_vfs_fixture.h"

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

namespace ogplay::test::file_vfs {

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

TemporaryRoot::TemporaryRoot(const std::string& name)
    : path(std::filesystem::temp_directory_path() /
           ("ogplay-file-vfs-" + name)) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    REQUIRE_FALSE(error);
}

TemporaryRoot::~TemporaryRoot() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

FileVm::FileVm(SandboxStore* sandbox, const bool include_android_catalog, const InterpreterConfig config,
               const std::span<const IntrinsicClassDecl> extra_intrinsics, const bool include_boot_dex,
               const bool include_skeleton)
    : model(strings, arrays), context(std::make_shared<DexVmAndroidContext>()), io_file_system(vfs),
      interpreter(
          [this, include_android_catalog, include_boot_dex, include_skeleton, extra_intrinsics]() -> DexClassLinker& {
              CoreIntrinsicServices services;
              services.current_time_millis = [] { return INT64_C(1704067200000); };
              linker.RegisterIntrinsics(CoreIntrinsicCatalog(services));
              if (include_android_catalog) {
                  linker.RegisterIntrinsics(
                      AndroidIntrinsicCatalog(context));
              }
              linker.RegisterIntrinsics(extra_intrinsics);
              if (include_boot_dex) {
                  ogplay::test::RegisterBootDex(linker);
              }
              if (include_skeleton) ogplay::test::RegisterBootDexSkeleton(linker);
              linker.Link();
              return linker;
          }(),
          model, nullptr, ledger, config) {
    interpreter.Monitors().SetTimeSource([] { return std::int64_t{1}; });
    context->vfs = &vfs;
    interpreter.IO().SetFileSystem(&io_file_system);
    context->package_name = kPackage;
    interpreter.SetLogger(&logger);
    if (sandbox != nullptr) vfs.AttachSandbox(*sandbox, kWritableRoots);
}

VmObjectRef FileVm::NewFile(const std::string& path) {
    const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
    static_cast<void>(CallOn(file, "<init>", "(Ljava/lang/String;)V",
                             {VmValue::Ref(interpreter.NewStringUtf8(
                                  path))}));
    return file;
}

VmObjectRef FileVm::NewUri(const std::string& spec) {
    const auto uri = interpreter.NewIntrinsicInstance("Ljava/net/URI;");
    static_cast<void>(CallOn(
        uri, "<init>", "(Ljava/lang/String;)V",
        {VmValue::Ref(interpreter.NewStringUtf8(spec))}));
    return uri;
}

VmObjectRef FileVm::NewFile(const VmObjectRef parent,
                                  const std::string& name) {
    const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
    static_cast<void>(CallOn(
        file, "<init>", "(Ljava/io/File;Ljava/lang/String;)V",
        {VmValue::Ref(parent),
         VmValue::Ref(interpreter.NewStringUtf8(name))}));
    return file;
}

VmObjectRef FileVm::NewFileFromUri(const VmObjectRef uri) {
    const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
    static_cast<void>(CallOn(file, "<init>", "(Ljava/net/URI;)V",
                             {VmValue::Ref(uri)}));
    return file;
}

std::uint64_t FileVm::StaticField(
    const std::string_view owner, const std::string_view name,
    const std::string_view descriptor) {
    const auto java_class = linker.ResolveDescriptor(owner);
    const auto initialized = interpreter.EnsureClassInitialized(java_class);
    REQUIRE_MESSAGE(!initialized.exception.IsValid(),
                    initialized.exception_message);
    const auto field = linker.FindFieldRecursive(
        java_class, std::string(name), std::string(descriptor));
    REQUIRE(field.has_value());
    const auto& linked = linker.Field(*field);
    return linker.Class(linked.owner).static_storage[linked.slot];
}

VmCallOutcome FileVm::CallOnOutcome(const VmObjectRef receiver,
                            const std::string& name,
                            const std::string& descriptor,
                            std::vector<VmValue> arguments) {
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

VmValue FileVm::CallOn(const VmObjectRef receiver, const std::string& name,
               const std::string& descriptor,
               std::vector<VmValue> arguments) {
    const auto outcome = CallOnOutcome(
        receiver, name, descriptor, std::move(arguments));
    REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                    outcome.exception_message);
    return outcome.value;
}

VmValue FileVm::CallStatic(const std::string& owner, const std::string& name,
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

bool FileVm::BoolOn(const VmObjectRef receiver,
                          const std::string& name) {
    return CallOn(receiver, name, "()Z").AsInt() != 0;
}

void FileVm::JavaWrite(const std::string& path, const std::string& text) {
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

std::string FileVm::NativeRead(const std::string& path) {
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

} // namespace ogplay::test::file_vfs
