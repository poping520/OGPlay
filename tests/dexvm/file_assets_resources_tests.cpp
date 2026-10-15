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

void Set32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

std::vector<std::byte> MakeBinaryXmlTextResource() {
    const std::vector<std::string> strings{"settings", "Level", "error"};
    std::vector<std::byte> string_data;
    std::vector<std::uint32_t> offsets;
    for (const auto& string : strings) {
        offsets.push_back(static_cast<std::uint32_t>(string_data.size()));
        string_data.push_back(static_cast<std::byte>(string.size()));
        string_data.push_back(static_cast<std::byte>(string.size()));
        for (const auto value : string) {
            string_data.push_back(static_cast<std::byte>(value));
        }
        string_data.push_back(std::byte{0});
    }
    while (string_data.size() % 4U != 0) string_data.push_back(std::byte{0});

    std::vector<std::byte> result;
    Append16(result, 0x0003);
    Append16(result, 8);
    Append32(result, 0);
    const auto string_pool = result.size();
    Append16(result, 0x0001);
    Append16(result, 28);
    Append32(result, 0);
    Append32(result, static_cast<std::uint32_t>(strings.size()));
    Append32(result, 0);
    Append32(result, 0x100U);
    Append32(result, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(result, 0);
    for (const auto offset : offsets) Append32(result, offset);
    result.insert(result.end(), string_data.begin(), string_data.end());
    Set32(result, string_pool + 4,
          static_cast<std::uint32_t>(result.size() - string_pool));

    const auto append_start = [&](const std::uint32_t name) {
        Append16(result, 0x0102);
        Append16(result, 16);
        Append32(result, 36);
        Append32(result, 1);
        Append32(result, 0xffffffffU);
        Append32(result, 0xffffffffU);
        Append32(result, name);
        Append16(result, 20);
        Append16(result, 20);
        Append16(result, 0);
        Append16(result, 0);
        Append16(result, 0);
        Append16(result, 0);
    };
    const auto append_end = [&](const std::uint32_t name) {
        Append16(result, 0x0103);
        Append16(result, 16);
        Append32(result, 24);
        Append32(result, 1);
        Append32(result, 0xffffffffU);
        Append32(result, 0xffffffffU);
        Append32(result, name);
    };
    append_start(0);
    append_start(1);
    Append16(result, 0x0104);
    Append16(result, 16);
    Append32(result, 28);
    Append32(result, 1);
    Append32(result, 0xffffffffU);
    Append32(result, 2);
    Append16(result, 8);
    result.push_back(std::byte{0});
    result.push_back(std::byte{0x03});
    Append32(result, 2);
    append_end(1);
    append_end(0);
    Set32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

} // namespace

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

TEST_CASE("AssetManager open returns a readable concrete byte stream") {
    FileVm vm;
    const std::vector<std::byte> payload{
        std::byte{0x47}, std::byte{0x41}, std::byte{0x4d}, std::byte{0x45}};
    vm.context->apk_bytes = MakeStoredZip("assets/gamecfg.bar", payload);
    vm.context->archive =
        ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/AssetManager;");
    const auto stream = vm.CallOn(
        manager, "open", "(Ljava/lang/String;)Ljava/io/InputStream;",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("gamecfg.bar"))}).ref;
    REQUIRE(stream.IsValid());
    CHECK(vm.linker.Class(vm.model.ObjectClass(stream)).descriptor ==
          "Ljava/io/ByteArrayInputStream;");
    CHECK(vm.CallOn(stream, "available", "()I").AsInt() ==
          static_cast<std::int32_t>(payload.size()));

    const auto bytes = vm.model.NewPrimitiveArray(
        vm.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
        static_cast<JniSize>(payload.size()));
    CHECK(vm.CallOn(stream, "read", "([BII)I",
                    {VmValue::Ref(bytes), VmValue::Int(0),
                     VmValue::Int(static_cast<std::int32_t>(payload.size()))})
              .AsInt() == static_cast<std::int32_t>(payload.size()));
    CHECK(vm.model.ReadByteRegion(bytes, 0,
                                  static_cast<JniSize>(payload.size())) ==
          payload);
    CHECK(vm.CallOn(stream, "read", "()I").AsInt() == -1);
}

TEST_CASE("Resources Context and PackageManager share application assets and isolate system assets") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        FileVm vm(nullptr, true, {.backend = backend});
        RegisterAndroidValueStateTables(vm.interpreter, vm.context);
        const std::vector<std::byte> payload{std::byte{0x41}};
        vm.context->apk_bytes = MakeStoredZip("assets/gamecfg.bar", payload);
        vm.context->archive = ogplay::loader::ParseApkArchive(
            vm.context->apk_bytes);
        const auto context = vm.interpreter.NewIntrinsicInstance(
            "Landroid/content/Context;");
        const auto context_root = vm.interpreter.ProtectReferences(std::array{context});
        const auto manager = vm.CallOn(
            context, "getPackageManager", "()Landroid/content/pm/PackageManager;").ref;
        const auto manager_root = vm.interpreter.ProtectReferences(std::array{manager});
        constexpr auto kPackageResources =
            "(Ljava/lang/String;)Landroid/content/res/Resources;";
        const auto package = vm.interpreter.NewStringUtf8(vm.context->package_name);
        const auto package_root = vm.interpreter.ProtectReferences(std::array{package});
        // The package query must also materialize a cold resource pair.
        const auto resources = vm.CallOn(
            manager, "getResourcesForApplication", kPackageResources,
            {VmValue::Ref(package)}).ref;
        const auto resources_root = vm.interpreter.ProtectReferences(
            std::array{resources});
        const auto direct_assets = vm.CallOn(
            context, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        const auto resource_assets = vm.CallOn(
            resources, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        REQUIRE(direct_assets.IsValid());
        CHECK(direct_assets == resource_assets);
        CHECK(vm.CallOn(context, "getResources",
                        "()Landroid/content/res/Resources;").ref == resources);
        CHECK(vm.CallOn(manager, "getResourcesForApplication", kPackageResources,
                        {VmValue::Ref(package)}).ref == resources);
        CHECK(vm.CallOn(resources, "getAssets",
                        "()Landroid/content/res/AssetManager;").ref == direct_assets);
        const auto wrapper = vm.interpreter.NewIntrinsicInstance(
            "Landroid/content/ContextWrapper;");
        static_cast<void>(vm.CallOn(
            wrapper, "<init>", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)}));
        CHECK(vm.CallOn(wrapper, "getResources",
                        "()Landroid/content/res/Resources;").ref == resources);
        CHECK(vm.CallOn(wrapper, "getAssets",
                        "()Landroid/content/res/AssetManager;").ref == direct_assets);
        static_cast<void>(vm.interpreter.CollectGarbage("resources-assets-identity"));
        CHECK(vm.CallOn(manager, "getResourcesForApplication", kPackageResources,
                        {VmValue::Ref(package)}).ref == resources);
        CHECK(vm.CallOn(resources, "getAssets",
                        "()Landroid/content/res/AssetManager;").ref == direct_assets);
        const auto name = vm.interpreter.NewStringUtf8("gamecfg.bar");
        const auto name_root = vm.interpreter.ProtectReferences(std::array{name});
        const auto stream = vm.CallOn(resource_assets, "open",
                                      "(Ljava/lang/String;)Ljava/io/InputStream;",
                                      {VmValue::Ref(name)}).ref;
        REQUIRE(stream.IsValid());
        CHECK(vm.CallOn(stream, "read", "()I").AsInt() == 0x41);
        CHECK(vm.CallOn(stream, "read", "()I").AsInt() == -1);
        const auto missing_asset = vm.CallOnOutcome(
            resource_assets, "open", "(Ljava/lang/String;)Ljava/io/InputStream;",
            {VmValue::Ref(vm.interpreter.NewStringUtf8("missing.bin"))});
        REQUIRE(missing_asset.exception.IsValid());
        CHECK(vm.linker.Class(missing_asset.exception_class).descriptor ==
              "Ljava/io/IOException;");

        for (const auto* other_package : {"org.example.other", "", "system"}) {
            const auto missing_package = vm.CallOnOutcome(
                manager, "getResourcesForApplication", kPackageResources,
                {VmValue::Ref(vm.interpreter.NewStringUtf8(other_package))});
            REQUIRE(missing_package.exception.IsValid());
            CHECK(vm.linker.Class(missing_package.exception_class).descriptor ==
                  "Landroid/content/pm/PackageManager$NameNotFoundException;");
        }
        const auto null_package = vm.CallOnOutcome(
            manager, "getResourcesForApplication", kPackageResources,
            {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_package.exception.IsValid());
        CHECK(vm.linker.Class(null_package.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");

        const auto system = vm.CallStatic(
            "Landroid/content/res/Resources;", "getSystem",
            "()Landroid/content/res/Resources;").ref;
        REQUIRE(system.IsValid());
        const auto system_root = vm.interpreter.ProtectReferences(
            std::array{system});
        CHECK(system != resources);
        CHECK(vm.CallStatic("Landroid/content/res/Resources;", "getSystem",
                            "()Landroid/content/res/Resources;").ref == system);
        const auto system_assets = vm.CallOn(
            system, "getAssets", "()Landroid/content/res/AssetManager;").ref;
        REQUIRE(system_assets.IsValid());
        CHECK(system_assets != direct_assets);
        static_cast<void>(vm.interpreter.CollectGarbage("system-resources-assets"));
        CHECK(vm.CallOn(system, "getAssets",
                        "()Landroid/content/res/AssetManager;").ref == system_assets);
        const auto missing = vm.CallOnOutcome(
            system_assets, "open", "(Ljava/lang/String;)Ljava/io/InputStream;",
            {VmValue::Ref(name)});
        REQUIRE(missing.exception.IsValid());
        CHECK(vm.linker.Class(missing.exception_class).descriptor ==
              "Ljava/io/FileNotFoundException;");
    }
}

TEST_CASE("Resources getXml exposes compiled APK XML as pull events") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        FileVm vm(nullptr, true, {.backend = backend});
        constexpr std::uint32_t kResourceId = 0x7f010000U;
        const auto xml = MakeBinaryXmlTextResource();
        vm.context->apk_bytes = MakeStoredZip("res/xml/settings.xml", xml);
        vm.context->archive =
            ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
        vm.context->arsc.entries.push_back({
            .resource_id = kResourceId,
            .type_name = "xml",
            .entry_name = "settings",
            .string_value = "res/xml/settings.xml",
            .value_type = 3,
        });

        const auto resources = vm.interpreter.NewIntrinsicInstance(
            "Landroid/content/res/Resources;");
        const auto parser = vm.CallOn(
            resources, "getXml", "(I)Landroid/content/res/XmlResourceParser;",
            {VmValue::Int(static_cast<std::int32_t>(kResourceId))}).ref;
        const auto event_type = [&] {
            return vm.CallOn(parser, "getEventType", "()I").AsInt();
        };
        const auto next = [&] {
            return vm.CallOn(parser, "next", "()I").AsInt();
        };
        const auto string_value = [&](const char* name) {
            const auto value =
                vm.CallOn(parser, name, "()Ljava/lang/String;").ref;
            return value.IsValid() ? vm.interpreter.StringUtf8(value) : std::string{};
        };

        const auto pull = vm.linker.ResolveDescriptor("Lorg/xmlpull/v1/XmlPullParser;");
        CHECK(vm.linker.Class(pull).is_boot_dex);
        CHECK(vm.linker.IsAssignable(pull, vm.model.ObjectClass(parser)));
        for (const auto method : vm.linker.Class(pull).own_virtual_methods) {
            const auto &declared = vm.linker.Method(method);
            const auto slot = vm.linker.FindVtableIndex(vm.model.ObjectClass(parser),
                                                       declared.name, declared.descriptor);
            REQUIRE_MESSAGE(slot.has_value(), declared.name);
            CHECK(vm.linker.Method(vm.linker.Class(vm.model.ObjectClass(parser)).vtable[*slot]).kind == MethodKind::intrinsic);
        }
        const auto unsupported = vm.CallOnOutcome(parser, "getAttributeCount", "()I");
        REQUIRE(unsupported.exception.IsValid());
        CHECK(vm.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
        const auto hits = vm.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.intrinsic.Landroid/content/res/XmlResourceParser$Impl;.getAttributeCount()I");

        CHECK(event_type() == 0);
        CHECK(next() == 2);
        CHECK(string_value("getName") == "settings");
        CHECK(next() == 2);
        CHECK(string_value("getName") == "Level");
        CHECK(next() == 4);
        CHECK(string_value("getText") == "error");
        CHECK(next() == 3);
        CHECK(string_value("getName") == "Level");
        CHECK(next() == 3);
        CHECK(string_value("getName") == "settings");
        CHECK(next() == 1);
        CHECK(next() == 1);
        static_cast<void>(vm.CallOn(parser, "close", "()V"));
        static_cast<void>(vm.CallOn(parser, "close", "()V"));
        const auto after_close =
            vm.CallOnOutcome(parser, "getEventType", "()I");
        REQUIRE(after_close.exception.IsValid());
        CHECK(vm.linker.Class(after_close.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");

        const auto missing = vm.CallOnOutcome(
            resources, "getXml", "(I)Landroid/content/res/XmlResourceParser;",
            {VmValue::Int(0x7f010001)});
        REQUIRE(missing.exception.IsValid());
        CHECK(vm.linker.Class(missing.exception_class).descriptor ==
              "Landroid/content/res/Resources$NotFoundException;");

        vm.context->arsc.entries.push_back({
            .resource_id = 0x7f010002U,
            .type_name = "xml",
            .entry_name = "missing",
            .string_value = "res/xml/missing.xml",
            .value_type = 3,
        });
        const auto absent_entry = vm.CallOnOutcome(
            resources, "getXml", "(I)Landroid/content/res/XmlResourceParser;",
            {VmValue::Int(0x7f010002)});
        REQUIRE(absent_entry.exception.IsValid());
        CHECK(vm.linker.Class(absent_entry.exception_class).descriptor ==
              "Landroid/content/res/Resources$NotFoundException;");
    }
}

TEST_CASE("Resources openRawResourceFd exposes a stored raw APK window") {
    FileVm vm;
    constexpr std::uint32_t kResourceId = 0x7f040001U;
    const std::vector<std::byte> payload{
        std::byte{'O'}, std::byte{'g'}, std::byte{'g'}, std::byte{'S'}};
    vm.context->apk_bytes = MakeStoredZip("res/raw/click.ogg", payload);
    vm.context->archive =
        ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
    vm.context->arsc.entries.push_back({
        .resource_id = kResourceId,
        .type_name = "raw",
        .entry_name = "click",
        .string_value = "res/raw/click.ogg",
        .value_type = 3,
    });
    const auto resources = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/res/Resources;");
    const auto afd = vm.CallOn(
        resources, "openRawResourceFd",
        "(I)Landroid/content/res/AssetFileDescriptor;",
        {VmValue::Int(static_cast<std::int32_t>(kResourceId))}).ref;
    REQUIRE(afd.IsValid());
    CHECK(vm.CallOn(afd, "getLength", "()J").AsLong() ==
          static_cast<std::int64_t>(payload.size()));
    const auto fd = vm.CallOn(afd, "getFileDescriptor",
                              "()Ljava/io/FileDescriptor;").ref;
    const auto* state = vm.interpreter.IO().FindDescriptor(fd);
    REQUIRE(state != nullptr);
    CHECK(state->kind == IoRuntime::DescriptorKind::apk_entry);
    CHECK(state->source == "res/raw/click.ogg");
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

TEST_CASE("XML Pull original factory parses BOM APK assets through KXml on both backends") {
    // A caller may supply an exact local text fixture without putting APK data
    // in the repository. The same AssetManager/Java parser path is always used.
    std::string document = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<settings><integer name=\"gles_mode\">2</integer>"
        "<bool name=\"useObb\">True</bool></settings>";
    std::optional<std::string> input_path;
#if defined(_WIN32)
    char* input_env_value{};
    std::size_t input_env_size{};
    REQUIRE(_dupenv_s(&input_env_value, &input_env_size, "OGPLAY_XML_PULL_INPUT") == 0);
    if (input_env_value != nullptr) {
        input_path = input_env_value;
        std::free(input_env_value);
    }
#else
    if (const auto* value = std::getenv("OGPLAY_XML_PULL_INPUT")) input_path = value;
#endif
    if (input_path) {
        std::ifstream stream(*input_path, std::ios::binary);
        REQUIRE_MESSAGE(stream.good(), *input_path);
        document.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
    REQUIRE(document.starts_with("\xEF\xBB\xBF"));
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        FileVm vm(nullptr, true, {.backend = backend});
        for (const auto descriptor : {
                "Lorg/xmlpull/v1/XmlPullParserFactory;", "Lorg/xmlpull/v1/XmlPullParser;",
                "Lorg/xmlpull/v1/XmlPullParserException;", "Lorg/xmlpull/v1/XmlSerializer;",
                "Lorg/kxml2/io/KXmlParser;", "Lorg/kxml2/io/KXmlParser$ContentSource;",
                "Lorg/kxml2/io/KXmlParser$ValueContext;",
                "Lorg/kxml2/io/KXmlSerializer;", "Llibcore/internal/StringPool;"}) {
            const auto type = vm.linker.ResolveDescriptor(descriptor);
            vm.linker.EnsureClassLinked(type);
            CHECK(vm.linker.Class(type).is_boot_dex);
            for (const auto method : vm.linker.Class(type).own_virtual_methods)
                CHECK(vm.linker.Method(method).kind != MethodKind::intrinsic);
            for (const auto method : vm.linker.Class(type).own_direct_methods)
                CHECK(vm.linker.Method(method).kind != MethodKind::intrinsic);
        }
        const auto factory = vm.CallStatic("Lorg/xmlpull/v1/XmlPullParserFactory;", "newInstance",
            "()Lorg/xmlpull/v1/XmlPullParserFactory;").ref;
        const auto factory_root = vm.interpreter.ProtectReferences(std::array{factory});
        static_cast<void>(vm.CallOn(factory, "setNamespaceAware", "(Z)V", {VmValue::Int(1)}));
        CHECK(vm.CallOn(factory, "isNamespaceAware", "()Z").AsInt() == 1);
        const auto parser = vm.CallOn(factory, "newPullParser", "()Lorg/xmlpull/v1/XmlPullParser;").ref;
        const auto parser_root = vm.interpreter.ProtectReferences(std::array{parser});
        CHECK(vm.linker.Class(vm.model.ObjectClass(parser)).descriptor == "Lorg/kxml2/io/KXmlParser;");
        const auto serializer = vm.CallOn(factory, "newSerializer", "()Lorg/xmlpull/v1/XmlSerializer;").ref;
        CHECK(vm.linker.Class(vm.model.ObjectClass(serializer)).descriptor == "Lorg/kxml2/io/KXmlSerializer;");
        const auto open = [&](std::string_view text) {
            std::vector<std::byte> bytes(text.size());
            std::memcpy(bytes.data(), text.data(), text.size());
            vm.context->apk_bytes = MakeStoredZip("assets/settings.xml", bytes);
            vm.context->archive = ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
            const auto manager = vm.interpreter.NewIntrinsicInstance("Landroid/content/res/AssetManager;");
            const auto manager_root = vm.interpreter.ProtectReferences(std::array{manager});
            return vm.CallOn(manager, "open", "(Ljava/lang/String;)Ljava/io/InputStream;",
                {VmValue::Ref(vm.interpreter.NewStringUtf8("settings.xml"))}).ref;
        };
        const auto input = open(document);
        const auto input_root = vm.interpreter.ProtectReferences(std::array{input});
        static_cast<void>(vm.CallOn(parser, "setInput", "(Ljava/io/InputStream;Ljava/lang/String;)V",
            {VmValue::Ref(input), VmValue::Ref(VmObjectRef{})}));
        const auto string_value = [&](std::string_view method) {
            const auto ref = vm.CallOn(parser, std::string(method), "()Ljava/lang/String;").ref;
            return ref.IsValid() ? vm.interpreter.StringUtf8(ref) : std::string{};
        };
        std::map<std::string, std::string> settings;
        std::string key;
        auto event = vm.CallOn(parser, "getEventType", "()I").AsInt();
        for (unsigned step = 0; event != 1 && step < 128; ++step) {
            if (event == 2 && vm.CallOn(parser, "getAttributeCount", "()I").AsInt() != 0) {
                const auto attribute = vm.CallOn(parser, "getAttributeName", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref;
                CHECK(vm.interpreter.StringUtf8(attribute) == "name");
                const auto value = vm.CallOn(parser, "getAttributeValue", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref;
                key = vm.interpreter.StringUtf8(value);
                const auto by_name = vm.CallOn(parser, "getAttributeValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    {VmValue::Ref(VmObjectRef{}), VmValue::Ref(vm.interpreter.NewStringUtf8("name"))}).ref;
                CHECK(vm.interpreter.StringUtf8(by_name) == key);
            } else if (event == 4 && !key.empty()) {
                settings[key] = string_value("getText");
            } else if (event == 3) key.clear();
            event = vm.CallOn(parser, "next", "()I").AsInt();
        }
        CHECK(event == 1);
        CHECK(settings["gles_mode"] == "2");
        CHECK(settings["useObb"] == "True");
        CHECK(string_value("getInputEncoding") == "UTF-8");
        static_cast<void>(vm.CallOn(input, "close", "()V"));
        const auto bad_input = open("<settings><value></settings>");
        const auto bad_root = vm.interpreter.ProtectReferences(std::array{bad_input});
        static_cast<void>(vm.CallOn(parser, "setInput", "(Ljava/io/InputStream;Ljava/lang/String;)V",
            {VmValue::Ref(bad_input), VmValue::Ref(VmObjectRef{})}));
        bool rejected = false;
        for (unsigned step = 0; step < 16; ++step) {
            const auto outcome = vm.CallOnOutcome(parser, "next", "()I");
            if (outcome.exception.IsValid()) {
                CHECK(vm.linker.Class(outcome.exception_class).descriptor == "Lorg/xmlpull/v1/XmlPullParserException;");
                rejected = true;
                break;
            }
            if (outcome.value.AsInt() == 1) break;
        }
        CHECK(rejected);
        static_cast<void>(vm.CallOn(bad_input, "close", "()V"));
    }
}
