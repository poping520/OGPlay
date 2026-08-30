// java.io.File through the shared VFS (SBX-5, ADR-0020). Two things matter
// here: a Java save and a native read see one filesystem, and File.mkdirs
// reports the truth. The old handler returned true unconditionally without
// creating anything, so the mkdirs case below fails if that ever returns.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/audio/java_sound_pool_mixer.h"
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

    explicit FileVm(SandboxStore* sandbox = nullptr,
                    const bool include_android_catalog = true)
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          io_file_system(vfs),
          interpreter(
              [this, include_android_catalog]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  if (include_android_catalog) {
                      linker.RegisterIntrinsics(
                          AndroidIntrinsicCatalog(context));
                  }
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

    [[nodiscard]] VmObjectRef NewUri(const std::string& spec) {
        const auto uri = interpreter.NewIntrinsicInstance("Ljava/net/URI;");
        static_cast<void>(CallOn(
            uri, "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(interpreter.NewStringUtf8(spec))}));
        return uri;
    }

    [[nodiscard]] VmObjectRef NewFile(const VmObjectRef parent,
                                      const std::string& name) {
        const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
        static_cast<void>(CallOn(
            file, "<init>", "(Ljava/io/File;Ljava/lang/String;)V",
            {VmValue::Ref(parent),
             VmValue::Ref(interpreter.NewStringUtf8(name))}));
        return file;
    }

    [[nodiscard]] VmObjectRef NewFileFromUri(const VmObjectRef uri) {
        const auto file = interpreter.NewIntrinsicInstance("Ljava/io/File;");
        static_cast<void>(CallOn(file, "<init>", "(Ljava/net/URI;)V",
                                 {VmValue::Ref(uri)}));
        return file;
    }

    [[nodiscard]] std::uint64_t StaticField(
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

TEST_CASE("Context internal directories return null when VFS is unavailable") {
    FileVm vm;
    vm.context->vfs = nullptr;
    const auto context =
        vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
    CHECK_FALSE(vm.CallOn(context, "getFilesDir", "()Ljava/io/File;")
                    .ref.IsValid());
    CHECK_FALSE(vm.CallOn(context, "getCacheDir", "()Ljava/io/File;")
                    .ref.IsValid());
}

TEST_CASE("AssetManager openFd publishes exact logical asset length") {
    FileVm vm;
    const std::vector<std::byte> payload{
        std::byte{'m'}, std::byte{'u'}, std::byte{'s'}, std::byte{'i'},
        std::byte{'c'}};
    vm.context->apk_bytes = MakeStoredZip("assets/main.obb", payload);
    vm.context->archive =
        ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetManager;");
    const auto descriptor = vm.CallOn(
        manager, "openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("main.obb"))}).ref;
    REQUIRE(descriptor.IsValid());
    CHECK(vm.CallOn(descriptor, "getLength", "()J").AsLong() ==
          static_cast<std::int64_t>(payload.size()));
    const auto start =
        vm.CallOn(descriptor, "getStartOffset", "()J").AsLong();
    CHECK(start == 30 + std::string_view("assets/main.obb").size());
    const auto fd = vm.CallOn(descriptor, "getFileDescriptor",
                              "()Ljava/io/FileDescriptor;").ref;
    REQUIRE(fd.IsValid());
    CHECK(vm.BoolOn(fd, "valid"));
    const auto* state = vm.interpreter.IO().FindDescriptor(fd);
    REQUIRE(state != nullptr);
    CHECK(state->kind == IoRuntime::DescriptorKind::apk_entry);
    CHECK(state->source == "assets/main.obb");
    CHECK(state->base_offset == static_cast<std::uint64_t>(start));
    static_cast<void>(vm.CallOn(descriptor, "close", "()V"));
    static_cast<void>(vm.CallOn(descriptor, "close", "()V"));
    CHECK_FALSE(vm.BoolOn(fd, "valid"));
    CHECK(vm.CallOn(descriptor, "getLength", "()J").AsLong() ==
          static_cast<std::int64_t>(payload.size()));

    const auto missing = vm.CallOnOutcome(
        manager, "openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("missing.obb"))});
    REQUIRE(missing.exception.IsValid());
    CHECK(vm.linker.Class(missing.exception_class).descriptor ==
          "Ljava/io/FileNotFoundException;");
}

TEST_CASE("AssetManager openFd rejects a compressed APK entry") {
    FileVm vm;
    const std::string name = "assets/compressed.bin";
    vm.context->apk_bytes = MakeStoredZip(
        name, std::vector<std::byte>{std::byte{'x'}});
    vm.context->apk_bytes[8] = std::byte{8};
    const auto central = 30U + name.size() + 1U;
    vm.context->apk_bytes[central + 10U] = std::byte{8};
    vm.context->archive =
        ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetManager;");
    const auto outcome = vm.CallOnOutcome(
        manager, "openFd",
        "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("compressed.bin"))});
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/io/FileNotFoundException;");
}

TEST_CASE("Parcel and asset file descriptors preserve VFS identity and range") {
    FileVm vm;
    vm.vfs.MountHostDirectory(
        "/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} /
                       "tests/fixtures/audio");
    const auto file = vm.NewFile("/sdcard/short-vorbis.ogg");
    const auto pfd_class =
        vm.linker.ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
    const auto open = vm.linker.FindDirectMethod(
        pfd_class, "open",
        "(Ljava/io/File;I)Landroid/os/ParcelFileDescriptor;");
    REQUIRE(open.has_value());
    const auto opened = vm.interpreter.Call(
        *open, std::vector<VmValue>{VmValue::Ref(file),
                                    VmValue::Int(0x10000000)});
    REQUIRE_MESSAGE(!opened.exception.IsValid(), opened.exception_message);
    const auto pfd = opened.value.ref;
    REQUIRE(pfd.IsValid());
    const auto fd = vm.CallOn(pfd, "getFileDescriptor",
                              "()Ljava/io/FileDescriptor;").ref;
    REQUIRE(fd.IsValid());
    CHECK(vm.BoolOn(fd, "valid"));
    const auto* state = vm.interpreter.IO().FindDescriptor(fd);
    REQUIRE(state != nullptr);
    CHECK(state->kind == IoRuntime::DescriptorKind::vfs_path);
    CHECK(state->source == "/sdcard/short-vorbis.ogg");

    const auto afd = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetFileDescriptor;");
    vm.CallOn(afd, "<init>", "(Landroid/os/ParcelFileDescriptor;JJ)V",
              {VmValue::Ref(pfd), VmValue::Long(17), VmValue::Long(31)});
    CHECK(vm.CallOn(afd, "getFileDescriptor",
                    "()Ljava/io/FileDescriptor;").ref == fd);
    CHECK(vm.CallOn(afd, "getStartOffset", "()J").AsLong() == 17);
    CHECK(vm.CallOn(afd, "getLength", "()J").AsLong() == 31);
    vm.CallOn(afd, "close", "()V");
    vm.CallOn(afd, "close", "()V");
    CHECK_FALSE(vm.BoolOn(fd, "valid"));
}

TEST_CASE("ParcelFileDescriptor open rejects missing paths and invalid modes") {
    FileVm vm;
    const auto file = vm.NewFile("/sdcard/missing.bin");
    const auto klass =
        vm.linker.ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
    const auto open = vm.linker.FindDirectMethod(
        klass, "open", "(Ljava/io/File;I)Landroid/os/ParcelFileDescriptor;");
    REQUIRE(open.has_value());
    const auto missing = vm.interpreter.Call(
        *open, std::vector<VmValue>{VmValue::Ref(file),
                                    VmValue::Int(0x10000000)});
    REQUIRE(missing.exception.IsValid());
    CHECK(vm.linker.Class(missing.exception_class).descriptor ==
          "Ljava/io/FileNotFoundException;");
    const auto invalid = vm.interpreter.Call(
        *open, std::vector<VmValue>{VmValue::Ref(file), VmValue::Int(0)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(vm.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");
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

TEST_CASE("MediaPlayer decodes the selected second Ogg descriptor range") {
    const TemporaryRoot root("media-range");
    const auto fixture_path = std::filesystem::path{OGPLAY_SOURCE_DIR} /
                              "tests/fixtures/audio/short-vorbis.ogg";
    std::ifstream fixture(fixture_path, std::ios::binary);
    REQUIRE(fixture.good());
    const std::vector<char> chars{std::istreambuf_iterator<char>(fixture), {}};
    std::vector<std::byte> ogg(chars.size());
    for (std::size_t index = 0; index < chars.size(); ++index) {
        ogg[index] = static_cast<std::byte>(chars[index]);
    }
    std::vector<std::byte> joined = ogg;
    joined.insert(joined.end(), ogg.begin(), ogg.end());
    {
        std::ofstream output(root.path / "joined.ogg", std::ios::binary);
        output.write(reinterpret_cast<const char*>(joined.data()),
                     static_cast<std::streamsize>(joined.size()));
        REQUIRE(output.good());
    }

    std::optional<ogplay::audio::EncodedAudioSource> requested;
    ogplay::audio::JavaSoundPoolMixer mixer{
        [&joined, &requested](
            const ogplay::audio::EncodedAudioSource& source) {
            requested = source;
            if (source.offset > joined.size()) return std::vector<std::byte>{};
            const auto available = joined.size() -
                static_cast<std::size_t>(source.offset);
            const auto length = source.length == UINT64_MAX
                ? available
                : static_cast<std::size_t>(source.length);
            if (length > available) return std::vector<std::byte>{};
            const auto begin = joined.begin() +
                static_cast<std::ptrdiff_t>(source.offset);
            return std::vector<std::byte>(
                begin, begin + static_cast<std::ptrdiff_t>(length));
        }};
    FileVm vm;
    vm.context->encoded_audio_playback = &mixer;
    vm.vfs.MountHostDirectory("/sdcard", root.path);
    const auto file = vm.NewFile("/sdcard/joined.ogg");
    const auto pfd_class =
        vm.linker.ResolveDescriptor("Landroid/os/ParcelFileDescriptor;");
    const auto open = vm.linker.FindDirectMethod(
        pfd_class, "open",
        "(Ljava/io/File;I)Landroid/os/ParcelFileDescriptor;");
    REQUIRE(open.has_value());
    const auto opened = vm.interpreter.Call(
        *open, std::vector<VmValue>{VmValue::Ref(file),
                                    VmValue::Int(0x10000000)});
    REQUIRE_MESSAGE(!opened.exception.IsValid(), opened.exception_message);
    const auto fd = vm.CallOn(opened.value.ref, "getFileDescriptor",
                              "()Ljava/io/FileDescriptor;").ref;
    const auto player = vm.interpreter.NewIntrinsicInstance(
        "Landroid/media/MediaPlayer;");
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;JJ)V",
              {VmValue::Ref(fd), VmValue::Long(
                   static_cast<std::int64_t>(ogg.size())),
               VmValue::Long(static_cast<std::int64_t>(ogg.size()))});
    vm.CallOn(player, "prepare", "()V");
    vm.CallOn(player, "start", "()V");
    REQUIRE(requested.has_value());
    CHECK(requested->kind ==
          ogplay::audio::EncodedAudioSource::Kind::vfs_path);
    CHECK(requested->name == "/sdcard/joined.ogg");
    CHECK(requested->offset == ogg.size());
    CHECK(requested->length == ogg.size());
    std::vector<std::int16_t> pcm(2048U * 2U);
    CHECK(mixer.RenderStereoPcm16(pcm, 48000U) == 2048U);
    CHECK(std::ranges::any_of(pcm,
        [](const std::int16_t sample) { return sample != 0; }));

    vm.CallOn(player, "stop", "()V");
    vm.CallOn(player, "reset", "()V");
    vm.CallOn(player, "setDataSource", "(Ljava/io/FileDescriptor;)V",
              {VmValue::Ref(fd)});
    const auto& full_source = vm.context->media_resources.at(player.Value());
    CHECK(full_source.offset == 0U);
    CHECK(full_source.length == UINT64_MAX);
    vm.CallOn(player, "prepare", "()V");
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
    FileVm vm(nullptr, false);
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
