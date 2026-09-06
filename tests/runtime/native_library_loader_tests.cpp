#include "../dexvm/boot_dex.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object_array.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/session/android_app_process.h"
#include "ogplay/session/dex_activity_lifecycle.h"

namespace {

void Put16(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8U);
}

void Put32(std::vector<std::byte>& bytes, const std::size_t offset,
           const std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

void PutDynamic(std::vector<std::byte>& bytes, std::size_t& offset,
                const std::int32_t tag, const std::uint32_t value) {
    Put32(bytes, offset, static_cast<std::uint32_t>(tag));
    Put32(bytes, offset + 4U, value);
    offset += 8U;
}

[[nodiscard]] std::vector<std::byte> LibcElf() {
    std::vector<std::byte> bytes(0x300, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 2);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 88, 0x100);
    Put32(bytes, 92, 0x10100U);
    Put32(bytes, 100, 56);
    Put32(bytes, 104, 56);
    Put32(bytes, 108, 6);
    Put32(bytes, 112, 4);
    std::size_t dynamic = 0x100;
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTable,
               0x10160U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTableSize,
               34);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSoname, 1);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicHash, 0x10190U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolTable,
               0x101b0U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolEntrySize,
               16);
    const char strings[] = "\0libc.so\0__system_property_area__\0";
    for (std::size_t index = 0; index < sizeof(strings); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, 2);
    Put32(bytes, 0x198, 1);
    Put32(bytes, 0x19c, 0);
    Put32(bytes, 0x1c0, 9);
    Put32(bytes, 0x1c4, 0x10200U);
    Put32(bytes, 0x1c8, 4);
    bytes[0x1cc] = std::byte{0x11};
    Put16(bytes, 0x1ce, 1);
    return bytes;
}

struct AppElfOptions final {
    std::string soname;
    std::string dependency;
    std::optional<std::uint32_t> jni_version;
    bool constructor{true};
    bool call_aps5_callback{};
};

[[nodiscard]] std::uint32_t EnvironmentThunk(const std::string_view name) {
    const auto slot = ogplay::runtime::FindJniSlot(name);
    if (!slot.has_value()) throw std::runtime_error("missing JNI slot");
    return ogplay::runtime::kJniThunkBegin +
           static_cast<std::uint32_t>(slot->Value()) * 4U + 1U;
}

[[nodiscard]] std::uint32_t JavaVmThunk(const std::string_view name) {
    const auto slot = ogplay::runtime::FindJniInvokeSlot(name);
    if (!slot.has_value()) throw std::runtime_error("missing JavaVM slot");
    return ogplay::runtime::kJniInvokeThunkBegin +
           static_cast<std::uint32_t>(slot->Value()) * 4U + 1U;
}

void WriteAps5OnLoad(std::vector<std::byte>& bytes,
                     const std::uint32_t version) {
    constexpr std::size_t kCode = 0x1000U;
    const std::array code{
        0xe92d40f0U, 0xe24dd00cU, 0xe1a04000U, 0xe1a00004U,
        0xe28d1000U, 0xe59f2064U, 0xe59fc064U, 0xe12fff3cU,
        0xe59d4000U, 0xe1a00004U, 0xe28f1068U, 0xe59fc054U,
        0xe12fff3cU, 0xe1a05000U, 0xe1a00004U, 0xe1a01005U,
        0xe28f2060U, 0xe28f3068U, 0xe59fc03cU, 0xe12fff3cU,
        0xe1a06000U, 0xe1a00004U, 0xe1a01005U, 0xe1a02006U,
        0xe59fc028U, 0xe12fff3cU, 0xe59f0010U, 0xe28dd00cU,
        0xe8bd80f0U,
    };
    for (std::size_t index = 0; index < code.size(); ++index) {
        Put32(bytes, kCode + index * 4U, code[index]);
    }
    Put32(bytes, kCode + 0x80U, version);
    Put32(bytes, kCode + 0x84U, JavaVmThunk("GetEnv"));
    Put32(bytes, kCode + 0x88U, EnvironmentThunk("FindClass"));
    Put32(bytes, kCode + 0x8cU, EnvironmentThunk("GetStaticMethodID"));
    Put32(bytes, kCode + 0x90U,
          EnvironmentThunk("CallStaticVoidMethod"));
    const auto put_string = [&bytes](const std::size_t offset,
                                     const std::string_view value) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            bytes[offset + index] = static_cast<std::byte>(value[index]);
        }
        bytes[offset + value.size()] = std::byte{};
    };
    put_string(kCode + 0x98U, "fixture/Aps5");
    put_string(kCode + 0xa8U, "callback");
    put_string(kCode + 0xb4U, "()V");
}

[[nodiscard]] std::vector<std::byte> AppElf(const AppElfOptions& options) {
    std::vector<std::byte> bytes(0x1100, std::byte{});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    Put16(bytes, 16, 3);
    Put16(bytes, 18, 40);
    Put32(bytes, 20, 1);
    Put32(bytes, 28, 52);
    Put32(bytes, 36, 0x05000400U);
    Put16(bytes, 40, 52);
    Put16(bytes, 42, 32);
    Put16(bytes, 44, 3);
    Put32(bytes, 52, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 60, 0x10000U);
    Put32(bytes, 68, 0x300);
    Put32(bytes, 72, 0x300);
    Put32(bytes, 76, 6);
    Put32(bytes, 80, 0x1000);
    Put32(bytes, 84, ogplay::loader::kElfProgramLoad);
    Put32(bytes, 88, 0x1000);
    Put32(bytes, 92, 0x20000U);
    Put32(bytes, 100, 0x100);
    Put32(bytes, 104, 0x100);
    Put32(bytes, 108, 5);
    Put32(bytes, 112, 0x1000);
    Put32(bytes, 116, ogplay::loader::kElfProgramDynamic);
    Put32(bytes, 120, 0x100);
    Put32(bytes, 124, 0x10100U);
    Put32(bytes, 132, 0x80);
    Put32(bytes, 136, 0x80);
    Put32(bytes, 140, 6);
    Put32(bytes, 144, 4);

    std::string strings(1, '\0');
    const auto soname_offset = static_cast<std::uint32_t>(strings.size());
    strings += options.soname;
    strings.push_back('\0');
    std::optional<std::uint32_t> dependency_offset;
    if (!options.dependency.empty()) {
        dependency_offset = static_cast<std::uint32_t>(strings.size());
        strings += options.dependency;
        strings.push_back('\0');
    }
    std::optional<std::uint32_t> jni_name_offset;
    if (options.jni_version.has_value()) {
        jni_name_offset = static_cast<std::uint32_t>(strings.size());
        strings += "JNI_OnLoad";
        strings.push_back('\0');
    }
    if (strings.size() > 0x30U) {
        throw std::runtime_error("application ELF string table is too large");
    }
    for (std::size_t index = 0; index < strings.size(); ++index) {
        bytes[0x160 + index] = static_cast<std::byte>(strings[index]);
    }

    std::size_t dynamic = 0x100;
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTable,
               0x10160U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicStringTableSize,
               static_cast<std::uint32_t>(strings.size()));
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSoname,
               soname_offset);
    if (dependency_offset.has_value()) {
        PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicNeeded,
                   *dependency_offset);
    }
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicHash, 0x10190U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolTable,
               0x101b0U);
    PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicSymbolEntrySize,
               16);
    if (options.constructor) {
        PutDynamic(bytes, dynamic, ogplay::loader::kElfDynamicInit,
                   options.call_aps5_callback ? 0x200f0U : 0x20010U);
    }
    Put32(bytes, 0x190, 1);
    Put32(bytes, 0x194, options.jni_version.has_value() ? 2U : 1U);
    if (options.jni_version.has_value()) {
        Put32(bytes, 0x198, 1);
        Put32(bytes, 0x19c, 0);
        Put32(bytes, 0x1c0, *jni_name_offset);
        Put32(bytes, 0x1c4, 0x20000U);
        Put32(bytes, 0x1c8, 8);
        bytes[0x1cc] = std::byte{0x12};
        Put16(bytes, 0x1ce, 1);
        if (options.call_aps5_callback) {
            WriteAps5OnLoad(bytes, *options.jni_version);
        } else {
            Put32(bytes, 0x1000, 0xe59f0000U);
            Put32(bytes, 0x1004, 0xe12fff1eU);
            Put32(bytes, 0x1008, *options.jni_version);
        }
    }
    Put32(bytes, options.call_aps5_callback ? 0x10f0U : 0x1010U,
          0xe12fff1eU);
    return bytes;
}

[[nodiscard]] ogplay::loader::ApkNativeLibrary Library(
    std::string soname, std::vector<std::byte> image) {
    const auto logical = soname.substr(3U, soname.size() - 6U);
    return {"lib/armeabi-v7a/" + soname, soname,
            ogplay::loader::AndroidArmAbi::armeabi_v7a, "fixture",
            std::move(image), soname, logical};
}

struct FixtureProcess final {
    std::vector<std::byte> libc{LibcElf()};
    ogplay::loader::Elf32ModuleInput libc_input{
        "libc.so", libc, ogplay::memory::GuestAddress{0x10000000U}};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::unique_ptr<ogplay::runtime::AndroidGuestProcess> process{
        ogplay::runtime::AndroidGuestProcess::Start(
            {19, std::span{&libc_input, 1}, {}, 64, 36,
             UINT64_C(100000), 1, &filesystem, {}})};
};

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(
    const std::string& name);

struct ApplicationProcess final {
    std::vector<std::byte> libc{LibcElf()};
    std::vector<std::byte> native_a{
        AppElf({"liba.so", "libc.so", ogplay::runtime::kJniVersion1_6})};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::unique_ptr<ogplay::runtime::AndroidGuestCallSession> session;
    std::unique_ptr<ogplay::loader::ApkNativeLibraryInventory> inventory;
    std::unique_ptr<ogplay::loader::ApkSelectedNativeLibraries> selected;
    std::unique_ptr<ogplay::runtime::NativeLibraryLoader> libraries;
    std::shared_ptr<ogplay::runtime::DexVmAndroidContext> context;
    ogplay::core::CapabilityLedger ledger;
    std::unique_ptr<ogplay::runtime::DexVmGuestBridge> bridge;
    std::size_t globals_before_bridge{};

    ApplicationProcess() {
        const std::array inputs{
            ogplay::loader::Elf32ModuleInput{
                "liba.so", native_a,
                ogplay::memory::GuestAddress{0x20000000U}},
            ogplay::loader::Elf32ModuleInput{
                "libc.so", libc,
                ogplay::memory::GuestAddress{0x10000000U}},
        };
        session = ogplay::runtime::AndroidGuestCallSession::Start(
            {19, "liba.so", inputs, {}, 64, 36,
             UINT64_C(200000), 1, &filesystem, {}});
        inventory =
            std::make_unique<ogplay::loader::ApkNativeLibraryInventory>(
                std::vector<ogplay::loader::ApkNativeLibrary>{
                    Library("liba.so", native_a)});
        selected =
            std::make_unique<ogplay::loader::ApkSelectedNativeLibraries>(
                ogplay::loader::SelectApkNativeLibraries(
                    *inventory,
                    ogplay::loader::AndroidArmAbi::armeabi_v7a));
        libraries = std::make_unique<ogplay::runtime::NativeLibraryLoader>(
            session->Process(), *selected);
        context =
            std::make_shared<ogplay::runtime::DexVmAndroidContext>();
        context->session = session.get();
        context->native_libraries = libraries.get();
        auto catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
        globals_before_bridge = session->Environment().GlobalReferenceCount();
        bridge = std::make_unique<ogplay::runtime::DexVmGuestBridge>(
            *session, ReadDexFixture("application.dex"), catalog, context,
            ledger, nullptr, ogplay::runtime::DexVmBridgeConfig{}, ogplay::test::ReadBootDex());
        context->threads = &bridge->Threads();
    }

    ~ApplicationProcess() {
        bridge.reset();
        if (session && session->Running()) session->Stop();
    }

    [[nodiscard]] std::int32_t CallStaticInt(
        const std::string& owner, const std::string& name) {
        const auto java_class = bridge->Linker().FindClass(owner);
        if (!java_class.has_value()) {
            throw std::runtime_error("fixture class is not linked");
        }
        const auto method = bridge->Linker().FindDirectMethod(
            *java_class, name, "()I");
        if (!method.has_value()) {
            throw std::runtime_error("fixture method is not linked");
        }
        const auto outcome = bridge->Vm().Call(*method, {});
        if (outcome.exception.IsValid()) {
            throw std::runtime_error(outcome.exception_message);
        }
        return outcome.value.AsInt();
    }
};

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(
    const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing DEX fixture: " + path);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

[[nodiscard]] ogplay::loader::AndroidManifestFacts AppManifest(
    const std::string& activity, const bool has_launcher = true) {
    ogplay::loader::AndroidManifestFacts manifest;
    manifest.package = "fixture";
    manifest.version_code = 1;
    manifest.application_class = "android.app.Application";
    if (has_launcher) {
        manifest.activity_components.push_back({
            ogplay::loader::AndroidManifestComponentKind::activity,
            activity, std::nullopt, true,
            {{{"android.intent.action.MAIN"},
              {"android.intent.category.LAUNCHER"}}}});
    }
    return manifest;
}

struct OrchestratedApp final {
    std::vector<std::byte> libc{LibcElf()};
    std::vector<std::byte> native_a{
        AppElf({"liba.so", "libc.so", ogplay::runtime::kJniVersion1_6})};
    ogplay::runtime::VirtualFileSystem filesystem;
    std::shared_ptr<ogplay::runtime::DexVmAndroidContext> context{
        std::make_shared<ogplay::runtime::DexVmAndroidContext>()};
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::unique_ptr<ogplay::session::AndroidAppProcess> app;

    explicit OrchestratedApp(const std::string& activity,
                             const bool has_launcher = true,
                             const bool with_native = true) {
        context->apk_bytes = {
            std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}};
        const ogplay::runtime::BionicModuleSource system{
            "libc.so", libc};
        std::vector<ogplay::loader::ApkNativeLibrary> libraries;
        if (with_native) libraries.push_back(Library("liba.so", native_a));
        ogplay::session::AndroidAppProcessRequest request;
        request.manifest = AppManifest(activity, has_launcher);
        request.native_libraries = std::move(libraries);
        request.system_libraries = std::span{&system, 1};
        request.dex_bytes = ReadDexFixture("application.dex");
        request.boot_dex_bytes = ogplay::test::ReadBootDex();
        request.context = context;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(200000);
#if defined(_WIN32)
        request.backend = {ogplay::gles::AngleRenderer::d3d11,
                           ogplay::gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {ogplay::gles::AngleRenderer::metal,
                           ogplay::gles::AngleDevice::hardware};
#else
        request.backend = {ogplay::gles::AngleRenderer::vulkan,
                           ogplay::gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        app = ogplay::session::AndroidAppProcess::Create(
            std::move(request));
    }

    [[nodiscard]] std::int32_t CallStaticInt(
        const std::string& owner, const std::string& name) {
        auto& bridge = app->DexVm();
        const auto java_class = bridge.Linker().FindClass(owner);
        if (!java_class.has_value()) {
            throw std::runtime_error("fixture class is not linked");
        }
        const auto method = bridge.Linker().FindDirectMethod(
            *java_class, name, "()I");
        if (!method.has_value()) {
            throw std::runtime_error("fixture method is not linked");
        }
        const auto outcome = bridge.Vm().Call(*method, {});
        if (outcome.exception.IsValid()) {
            throw std::runtime_error(outcome.exception_message);
        }
        return outcome.value.AsInt();
    }
};

}  // namespace

TEST_CASE("native library loader appends dependency constructors and one explicit JNI_OnLoad") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", AppElf({"liba.so", "libb.so", 0x00010006U})),
        Library("libb.so", AppElf({"libb.so", "", 0x00010004U})),
        Library("libplain.so", AppElf({"libplain.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    const auto first = libraries.LoadLibrary("a", 7U);
    CHECK(first.initialized_modules ==
          std::vector<std::string>{"libb.so", "liba.so"});
    CHECK(first.jni_version == 0x00010006U);
    CHECK(fixture.process->ApplicationModuleCount() == 2U);
    CHECK(fixture.process->HasLoadedModule("liba.so"));
    CHECK(fixture.process->HasLoadedModule("libb.so"));

    const auto repeated = libraries.LoadPath(
        runtime::NativeLibraryLoader::SyntheticGuestPath("liba.so"), 7U);
    CHECK(repeated.handle == first.handle);
    CHECK(repeated.already_loaded);
    CHECK_FALSE(repeated.recursive);
    CHECK(repeated.initialized_modules.empty());
    auto records = libraries.Records();
    REQUIRE(records.size() == 1U);
    CHECK(records[0].jni_on_load_calls == 1U);

    const auto dependency = libraries.LoadLibrary("b", 7U);
    CHECK(dependency.initialized_modules.empty());
    CHECK(dependency.jni_version == 0x00010004U);
    records = libraries.Records();
    REQUIRE(records.size() == 2U);
    CHECK(records[0].jni_on_load_calls == 1U);
    CHECK(records[1].jni_on_load_calls == 1U);
    const auto plain = libraries.LoadLibrary("plain", 7U);
    CHECK_FALSE(plain.jni_version.has_value());
    records = libraries.Records();
    REQUIRE(records.size() == 3U);
    CHECK(records[2].jni_on_load_calls == 0U);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(libraries.LoadLibrary("a", 8U)),
        "native library is already associated with another ClassLoader: "
        "/data/app-lib/liba.so",
        runtime::NativeLibraryLoadError);

    fixture.process->Stop();
    CHECK_FALSE(fixture.process->Running());
    CHECK(fixture.process->AttachedJniThreadCount() == 0U);
}

TEST_CASE("native library loader resolves Bionic dependencies after rootless startup") {
    using namespace ogplay;
    FixtureProcess fixture;
    auto application = AppElf(
        {"libusesystem.so", "libstdc++.so", runtime::kJniVersion1_6});
    auto libstdcpp = AppElf({"libstdc++.so", "", std::nullopt});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libusesystem.so", application),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    const runtime::BionicModuleSource system[]{{"libstdc++.so", libstdcpp}};
    runtime::NativeLibraryLoader libraries(
        *fixture.process, selected, runtime::SelectBionicProfile(19), system);

    const auto loaded = libraries.LoadLibrary("usesystem", 7U);
    CHECK(loaded.initialized_modules ==
          std::vector<std::string>{"libstdc++.so", "libusesystem.so"});
    CHECK(fixture.process->HasLoadedModule("libstdc++.so"));
    CHECK(fixture.process->HasLoadedModule("libusesystem.so"));
    CHECK(loaded.jni_version == runtime::kJniVersion1_6);
}

TEST_CASE("native library loader resolves APK modules by inventory or ELF SONAME") {
    using namespace ogplay;
    FixtureProcess fixture;
    loader::ApkNativeLibraryInventory inventory{{
        Library("libconsumer-alias.so",
                AppElf({"libconsumer-alias.so", "Tales.final.so",
                        std::nullopt})),
        Library("libconsumer-inventory.so",
                AppElf({"libconsumer-inventory.so", "libTales.final.so",
                        std::nullopt})),
        Library("libTales.final.so",
                AppElf({"Tales.final.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    SUBCASE("explicit load keeps the inventory identity") {
        const auto loaded = libraries.LoadLibrary("Tales.final", 3U);
        CHECK(loaded.initialized_modules ==
              std::vector<std::string>{"libTales.final.so"});
        REQUIRE(libraries.Records().size() == 1U);
        CHECK(libraries.Records()[0].soname == "libTales.final.so");
        CHECK(fixture.process->HasLoadedModule("libTales.final.so"));
        CHECK(fixture.process->HasLoadedModule("Tales.final.so"));
    }

    SUBCASE("DT_NEEDED resolves the ELF SONAME alias") {
        const auto loaded = libraries.LoadLibrary("consumer-alias", 3U);
        CHECK(loaded.initialized_modules ==
              std::vector<std::string>{"libTales.final.so",
                                       "libconsumer-alias.so"});
        CHECK(fixture.process->ApplicationModuleCount() == 2U);
    }

    SUBCASE("DT_NEEDED resolves the inventory basename") {
        const auto loaded = libraries.LoadLibrary("consumer-inventory", 3U);
        CHECK(loaded.initialized_modules ==
              std::vector<std::string>{"libTales.final.so",
                                       "libconsumer-inventory.so"});
        CHECK(fixture.process->ApplicationModuleCount() == 2U);
    }
}

TEST_CASE("native library loader keeps malformed unresolved and bad JNI failures stable") {
    using namespace ogplay;
    FixtureProcess fixture;
    std::vector<std::byte> malformed(4, std::byte{});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libbad.so", std::move(malformed)),
        Library("libmissing.so",
                AppElf({"libmissing.so", "libabsent.so", std::nullopt})),
        Library("libversion.so",
                AppElf({"libversion.so", "", 0x00090009U})),
        Library("libalias.so",
                AppElf({"libother.so", "", std::nullopt})),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(*fixture.process, selected);

    try {
        static_cast<void>(libraries.LoadLibrary("bad", 3U));
        FAIL("malformed ELF load unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() ==
              runtime::NativeLibraryLoadErrorReason::malformed_elf);
    }
    try {
        static_cast<void>(libraries.LoadLibrary("missing", 3U));
        FAIL("unresolved dependency load unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() ==
              runtime::NativeLibraryLoadErrorReason::unresolved_dependency);
        const auto first_message = std::string(error.what());
        try {
            static_cast<void>(libraries.LoadLibrary("missing", 3U));
            FAIL("failed library retry unexpectedly succeeded");
        } catch (const runtime::NativeLibraryLoadError& repeated) {
            CHECK(repeated.Reason() == error.Reason());
            CHECK(repeated.what() == first_message);
        }
    }
    try {
        static_cast<void>(libraries.LoadLibrary("version", 3U));
        FAIL("invalid JNI version unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() ==
              runtime::NativeLibraryLoadErrorReason::invalid_jni_version);
    }
    const auto alias = libraries.LoadLibrary("alias", 3U);
    CHECK(alias.initialized_modules ==
          std::vector<std::string>{"libalias.so"});
    CHECK(fixture.process->HasLoadedModule("libalias.so"));
    CHECK(fixture.process->HasLoadedModule("libother.so"));
    CHECK_THROWS_AS(
        static_cast<void>(libraries.LoadPath("C:/host/libbad.so", 3U)),
        runtime::NativeLibraryLoadError);
    CHECK(libraries.Records().size() == 4U);
}

TEST_CASE("DexVM System load APIs support nested JNI OnLoad Java reentry") {
    using namespace ogplay;
    auto libc = LibcElf();
    auto a = AppElf(
        {"liba.so", "libc.so", runtime::kJniVersion1_6, true, true});
    auto b = AppElf({"libb.so", "", runtime::kJniVersion1_4});
    auto path = AppElf({"libpath.so", "", runtime::kJniVersion1_4});
    auto vfs = AppElf({"libvfs.so", "", runtime::kJniVersion1_6});
    std::array module_inputs{
        loader::Elf32ModuleInput{"liba.so", a,
                                 memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc,
                                 memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    filesystem.PutFile("/data/data/fixture/files/libvfs.so", vfs, false);
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "liba.so", module_inputs, {}, 64, 36, UINT64_C(200000),
         1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{
        Library("liba.so", a),
        Library("libb.so", b),
        Library("libpath.so", path),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge = std::make_unique<runtime::DexVmGuestBridge>(
        *session, ReadDexFixture("aps5.dex"), catalog, context, ledger,
        nullptr, runtime::DexVmBridgeConfig{}, test::ReadBootDex());
    context->threads = &bridge->Threads();

    const auto aps5_class = bridge->Linker().FindClass("Lfixture/Aps5;");
    REQUIRE(aps5_class.has_value());
    const auto method = [&](const std::string_view name,
                            const std::string_view descriptor) {
        const auto found = bridge->Linker().FindDirectMethod(
            *aps5_class, std::string(name), std::string(descriptor));
        REQUIRE(found.has_value());
        return *found;
    };
    const auto expect_link_error = [
        &](const runtime::dexvm::VmCallOutcome& outcome) {
        REQUIRE(outcome.exception.IsValid());
        CHECK(bridge->Linker().Class(outcome.exception_class).descriptor ==
              "Ljava/lang/UnsatisfiedLinkError;");
    };

    const auto nested = bridge->Vm().Call(method("start", "()V"), {});
    REQUIRE_MESSAGE(!nested.exception.IsValid(), nested.exception_message);
    auto records = libraries.Records();
    REQUIRE(records.size() == 2U);
    CHECK(records[0].soname == "liba.so");
    CHECK(records[0].jni_on_load_calls == 1U);
    CHECK(records[1].soname == "libb.so");
    CHECK(records[1].jni_on_load_calls == 1U);

    const auto load_path = method("loadPath", "(Ljava/lang/String;)V");
    const auto call_with_string = [&](const runtime::dexvm::VmMethodId target,
                                      const std::string& value) {
        const std::array arguments{runtime::dexvm::VmValue::Ref(
            bridge->Vm().NewStringUtf8(value))};
        return bridge->Vm().Call(target, arguments);
    };
    const auto path_result = call_with_string(
        load_path, runtime::NativeLibraryLoader::SyntheticGuestPath(
                       "libpath.so"));
    REQUIRE_MESSAGE(!path_result.exception.IsValid(),
                    path_result.exception_message);

    const auto vfs_result = call_with_string(
        load_path, "/data/data/fixture/files/./LIBVFS.so");
    REQUIRE_MESSAGE(!vfs_result.exception.IsValid(),
                    vfs_result.exception_message);
    const auto vfs_repeated = call_with_string(
        load_path, "/data/data/fixture/files/libvfs.so");
    REQUIRE_MESSAGE(!vfs_repeated.exception.IsValid(),
                    vfs_repeated.exception_message);

    const auto load_library =
        method("loadLibrary", "(Ljava/lang/String;)V");
    expect_link_error(call_with_string(load_library, "missing"));
    expect_link_error(call_with_string(
        load_path, "/data/local/tmp/libmissing.so"));

    records = libraries.Records();
    REQUIRE(records.size() == 4U);
    CHECK(records[0].jni_on_load_calls == 1U);
    CHECK(records[1].jni_on_load_calls == 1U);
    CHECK(records[2].soname == "libpath.so");
    CHECK(records[2].jni_on_load_calls == 1U);
    CHECK(records[3].canonical_path ==
          "/data/data/fixture/files/libvfs.so");
    CHECK(records[3].soname == "libvfs.so");
    CHECK(records[3].jni_on_load_calls == 1U);

    bridge.reset();
    session->Stop();
    CHECK_FALSE(session->Running());
}

TEST_CASE("DexVM preserves failure from a resolved RegisterNatives target") {
    using namespace ogplay;
    auto libc = LibcElf();
    auto root = AppElf(
        {"libroot.so", "libc.so", runtime::kJniVersion1_6});
    std::array module_inputs{
        loader::Elf32ModuleInput{"libroot.so", root,
                                 memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc,
                                 memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "libroot.so", module_inputs, {}, 64, 36,
         UINT64_C(200000), 1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{
        Library("libroot.so", root),
    }};
    const auto selected = loader::SelectApkNativeLibraries(
        inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge = std::make_unique<runtime::DexVmGuestBridge>(
        *session, ReadDexFixture("aps5.dex"), catalog, context, ledger,
        nullptr, runtime::DexVmBridgeConfig{}, test::ReadBootDex());
    context->threads = &bridge->Threads();

    const auto owner = bridge->Linker().FindClass("Lfixture/Aps5;");
    REQUIRE(owner.has_value());
    const auto method = bridge->Linker().FindDirectMethod(
        *owner, "fail", "()V");
    REQUIRE(method.has_value());
    const auto identity = bridge->RegisteredClassIdentity(*owner);
    REQUIRE(identity.has_value());
    const std::array methods{runtime::JniNativeMethod{
        "fail", "()V", memory::GuestAddress{0xdeadbee0U}}};
    session->Natives().RegisterNatives(*identity, methods);
    REQUIRE(session->Natives().Resolve(*identity, "fail", "()V")
                .has_value());

    try {
        static_cast<void>(bridge->Vm().Call(*method, {}));
        FAIL("faulting registered native unexpectedly returned");
    } catch (const runtime::AndroidGuestCallSessionError& error) {
        const std::string message = error.what();
        CHECK(message.find("registered JNI native invocation failed") !=
              std::string::npos);
        CHECK(message.find("class=fixture/Aps5") != std::string::npos);
        CHECK(message.find("method=fail") != std::string::npos);
        CHECK(message.find("descriptor=()V") != std::string::npos);
        CHECK(message.find("guest_thread=1") != std::string::npos);
        CHECK(message.find("pc=0xdeadbee0") != std::string::npos);
        CHECK(message.find("reason=memory_fault(4)") != std::string::npos);
        CHECK(message.find(
                  "native method has no registered mapping or export") ==
              std::string::npos);
    }

    bridge.reset();
    session->Stop();
}

TEST_CASE("DexVM bridge canonicalizes JNI jclass as the real Class object") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto represented = fixture.bridge->Linker().FindClass(
        "Lfixture/LauncherActivity;");
    const auto class_class = fixture.bridge->Linker().FindClass(
        "Ljava/lang/Class;");
    REQUIRE(represented.has_value());
    REQUIRE(class_class.has_value());
    const auto class_object = fixture.bridge->Model().ClassObject(*represented);

    const auto reference = fixture.bridge->PublishLocal(class_object);
    REQUIRE_FALSE(reference.IsNull());
    const auto identity = fixture.session->Environment().ResolveObjectForHle(
        1U, reference);
    REQUIRE(identity.has_value());
    CHECK(identity == fixture.bridge->RegisteredClassIdentity(*represented));
    CHECK(fixture.session->Objects().ClassOf(*identity) ==
          fixture.bridge->RegisteredClassIdentity(*class_class));

    const auto round_trip = fixture.bridge->FromReference(reference);
    CHECK(round_trip == class_object);
    CHECK(fixture.bridge->Model().ClassOfClassObject(round_trip) ==
          *represented);
}

TEST_CASE("DexVM JNI classes retain intrinsic superclasses for object arrays") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto activity = fixture.bridge->Linker().FindClass(
        "Landroid/app/Activity;");
    const auto launcher = fixture.bridge->Linker().FindClass(
        "Lfixture/LauncherActivity;");
    const auto object = fixture.bridge->Linker().FindClass(
        "Ljava/lang/Object;");
    REQUIRE(activity.has_value());
    REQUIRE(launcher.has_value());
    REQUIRE(object.has_value());

    const auto activity_identity =
        fixture.bridge->RegisteredClassIdentity(*activity);
    const auto launcher_identity =
        fixture.bridge->RegisteredClassIdentity(*launcher);
    const auto object_identity =
        fixture.bridge->RegisteredClassIdentity(*object);
    REQUIRE(activity_identity.has_value());
    REQUIRE(launcher_identity.has_value());
    REQUIRE(object_identity.has_value());
    CHECK(fixture.session->Classes().IsAssignableFrom(
        *activity_identity, *launcher_identity));

    const auto& launcher_class = fixture.bridge->Linker().Class(*launcher);
    const auto launcher_object = fixture.bridge->Model().NewInstance(
        *launcher, launcher_class.instance_slots);
    const runtime::JniObjectValue launcher_value{
        fixture.bridge->Model().ToIdentity(launcher_object),
        *launcher_identity};
    auto& arrays = fixture.session->Objects().ObjectArrays();
    const auto array = arrays.New(*activity_identity, 1, launcher_value);
    CHECK(arrays.Get(array, 0) == launcher_value);
    CHECK_THROWS_AS(
        arrays.Set(array, 0,
                   runtime::JniObjectValue{{runtime::JniObjectDomain::dex_vm,
                                            UINT64_C(0x1234)},
                                           *object_identity}),
        runtime::JniObjectArrayError);
}

TEST_CASE("DexVM imports JNI-created application objects with instance slots") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto launcher = fixture.bridge->Linker().FindClass(
        "Lfixture/LauncherActivity;");
    const auto activity = fixture.bridge->Linker().FindClass(
        "Landroid/app/Activity;");
    REQUIRE(launcher.has_value());
    REQUIRE(activity.has_value());
    const auto launcher_identity =
        fixture.bridge->RegisteredClassIdentity(*launcher);
    const auto activity_identity =
        fixture.bridge->RegisteredClassIdentity(*activity);
    REQUIRE(launcher_identity.has_value());
    REQUIRE(activity_identity.has_value());

    const auto imported_identity =
        fixture.session->Objects().Allocate(*launcher_identity);
    const auto imported_reference =
        fixture.session->Environment().PublishLocalObject(1U,
                                                          imported_identity);
    const auto imported = fixture.bridge->FromReference(imported_reference);
    CHECK(fixture.bridge->Model().Kind(imported) ==
          runtime::dexvm::VmObjectKind::vm_instance);
    CHECK(fixture.bridge->Model().ToIdentity(imported) == imported_identity);
    const auto init = fixture.bridge->Linker().FindDirectMethod(
        *launcher, "<init>", "()V");
    REQUIRE(init.has_value());
    const std::array receiver{runtime::dexvm::VmValue::Ref(imported)};
    auto outcome = fixture.bridge->Vm().Call(*init, receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    const auto getter_index = fixture.bridge->Linker().FindVtableIndex(
        *launcher, "getImported", "()I");
    REQUIRE(getter_index.has_value());
    outcome = fixture.bridge->Vm().Call(
        fixture.bridge->Linker().Class(*launcher).vtable[*getter_index],
        receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    CHECK(outcome.value.AsInt() == 7);

    const auto intrinsic_identity =
        fixture.session->Objects().Allocate(*activity_identity);
    const auto intrinsic_reference =
        fixture.session->Environment().PublishLocalObject(1U,
                                                          intrinsic_identity);
    const auto intrinsic = fixture.bridge->FromReference(intrinsic_reference);
    CHECK(fixture.bridge->Model().Kind(intrinsic) ==
          runtime::dexvm::VmObjectKind::external);
}

TEST_CASE("DexVM Intent ArrayList extras trace children and sweep with owner") {
    using namespace ogplay;
    ApplicationProcess fixture;
    auto& vm = fixture.bridge->Vm();
    auto& linker = fixture.bridge->Linker();
    const auto intent =
        vm.NewIntrinsicInstance("Landroid/content/Intent;");
    const auto list = vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
    const auto key = vm.NewStringUtf8("levels");
    const auto intent_class = linker.ResolveDescriptor(
        "Landroid/content/Intent;");
    const auto put_index = linker.FindVtableIndex(
        intent_class, "putIntegerArrayListExtra",
        "(Ljava/lang/String;Ljava/util/ArrayList;)Landroid/content/Intent;");
    REQUIRE(put_index.has_value());
    auto outcome = vm.Call(
        linker.Class(intent_class).vtable[*put_index],
        std::vector{runtime::dexvm::VmValue::Ref(intent),
                    runtime::dexvm::VmValue::Ref(key),
                    runtime::dexvm::VmValue::Ref(list)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);

    vm.SetGcIntegration({
        {}, {}, [intent](const runtime::dexvm::VmRootVisitor& visit) {
            visit(intent);
        }});
    const auto marked = vm.MarkReachable();
    CHECK(marked.IsMarked(intent));
    CHECK(marked.IsMarked(list));
    static_cast<void>(vm.CollectGarbage("intent-list-extra-edge"));
    REQUIRE(fixture.context->intent_integer_array_list_extras.contains(
        intent));

    vm.SetGcIntegration({});
    static_cast<void>(vm.CollectGarbage("intent-list-extra-owner-sweep"));
    CHECK_FALSE(fixture.context->intent_integer_array_list_extras.contains(
        intent));
}

TEST_CASE("DexVM JNI fields share interpreter storage and reference identity") {
    using namespace ogplay;
    ApplicationProcess fixture;
    const auto owner = fixture.bridge->Linker().FindClass(
        "Lfixture/LauncherActivity;");
    const auto child = fixture.bridge->Linker().FindClass(
        "Lfixture/LauncherChildActivity;");
    REQUIRE(owner.has_value());
    REQUIRE(child.has_value());
    const auto owner_identity =
        fixture.bridge->RegisteredClassIdentity(*owner);
    const auto child_identity =
        fixture.bridge->RegisteredClassIdentity(*child);
    REQUIRE(owner_identity.has_value());
    REQUIRE(child_identity.has_value());

    auto& classes = fixture.session->Classes();
    auto& fields = fixture.session->Fields();
    const auto imported_id = classes.GetFieldId(
        *owner_identity, "imported", "I", false);
    const auto inherited_id = classes.GetFieldId(
        *child_identity, "imported", "I", false);
    const auto peer_id = classes.GetFieldId(
        *owner_identity, "peer", "Ljava/lang/Object;", false);
    const auto wide_id = classes.GetFieldId(
        *owner_identity, "wide", "J", true);
    const auto stage_id = classes.GetFieldId(
        *owner_identity, "stage", "I", true);
    REQUIRE(imported_id.has_value());
    REQUIRE(inherited_id == imported_id);
    REQUIRE(peer_id.has_value());
    REQUIRE(wide_id.has_value());
    REQUIRE(stage_id.has_value());
    REQUIRE(fields.EnsureClassInitialized(*owner_identity, 1U));

    const auto instance = fixture.bridge->Model().NewInstance(
        *owner, fixture.bridge->Linker().Class(*owner).instance_slots);
    const auto instance_ref = fixture.bridge->PublishLocal(instance);
    const auto instance_identity =
        fixture.session->Environment().ResolveObjectForHle(1U, instance_ref);
    REQUIRE(instance_identity.has_value());
    fields.SetInstance(*instance_identity, *owner_identity, *imported_id,
                       runtime::JniInt{42}, 1U);

    const auto getter_index = fixture.bridge->Linker().FindVtableIndex(
        *owner, "getImported", "()I");
    REQUIRE(getter_index.has_value());
    const std::array receiver{runtime::dexvm::VmValue::Ref(instance)};
    auto outcome = fixture.bridge->Vm().Call(
        fixture.bridge->Linker().Class(*owner).vtable[*getter_index], receiver);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    CHECK(outcome.value.AsInt() == 42);

    const auto round_trip_primitive = [&](const char* name,
                                          const char* descriptor,
                                          const runtime::JniValue& value) {
        const auto id = classes.GetFieldId(
            *owner_identity, name, descriptor, false);
        REQUIRE(id.has_value());
        fields.SetInstance(*instance_identity, *owner_identity, *id, value,
                           1U);
        CHECK(fields.GetInstance(*instance_identity, *owner_identity, *id,
                                 1U) == value);
    };
    round_trip_primitive("flag", "Z", runtime::JniBoolean{1});
    round_trip_primitive("small", "B", runtime::JniByte{-7});
    round_trip_primitive("letter", "C", runtime::JniChar{0x4e2d});
    round_trip_primitive("shortValue", "S", runtime::JniShort{-1234});
    round_trip_primitive("ratio", "F", runtime::JniFloat{1.25F});
    round_trip_primitive("precise", "D", runtime::JniDouble{-3.5});

    const auto peer = fixture.bridge->Model().NewInstance(
        *owner, fixture.bridge->Linker().Class(*owner).instance_slots);
    const auto peer_ref = fixture.bridge->PublishLocal(peer);
    fields.SetInstance(*instance_identity, *owner_identity, *peer_id,
                       peer_ref, 1U);
    const auto round_trip = std::get<runtime::JniReference>(
        fields.GetInstance(*instance_identity, *owner_identity, *peer_id, 1U));
    CHECK(fixture.bridge->FromReference(round_trip) == peer);

    fields.SetStatic(*owner_identity, *wide_id,
                     runtime::JniLong{INT64_C(0x1122334455667788)}, 1U);
    CHECK(std::get<runtime::JniLong>(
              fields.GetStatic(*owner_identity, *wide_id, 1U)) ==
          INT64_C(0x1122334455667788));
    fields.SetStatic(*owner_identity, *stage_id, runtime::JniInt{9}, 1U);
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;", "getStage") ==
          9);
}

TEST_CASE("minimal Application startup preserves order identity and native loads") {
    using namespace ogplay;

    SUBCASE("framework default is a stable process root") {
        ApplicationProcess fixture;
        CHECK(fixture.session->Environment().GlobalReferenceCount() ==
              fixture.globals_before_bridge);
        const auto first = session::StartDexApplication(
            *fixture.bridge, fixture.context, "Landroid/app/Application;");
        const auto repeated = session::StartDexApplication(
            *fixture.bridge, fixture.context, "Landroid/app/Application;");
        CHECK(first == repeated);
        CHECK(fixture.context->application == first);
        CHECK(fixture.context->application_base_context.IsValid());
    }

    SUBCASE("custom startup observes clinit construct attach onCreate order") {
        ApplicationProcess fixture;
        const auto application = session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/CustomApplication;");
        CHECK(application.IsValid());
        CHECK(fixture.CallStaticInt("Lfixture/CustomApplication;",
                                    "getStage") == 4);
        CHECK(session::StartDexApplication(
                  *fixture.bridge, fixture.context,
                  "Lfixture/CustomApplication;") == application);
        CHECK_THROWS_AS(
            static_cast<void>(session::StartDexApplication(
                *fixture.bridge, fixture.context,
                "Landroid/app/Application;")),
            session::DexActivityLifecycleError);
    }

    SUBCASE("clinit may load the first native library") {
        ApplicationProcess fixture;
        static_cast<void>(session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/ClinitLoadingApplication;"));
        const auto records = fixture.libraries->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
    }

    SUBCASE("onCreate may load the first native library") {
        ApplicationProcess fixture;
        static_cast<void>(session::StartDexApplication(
            *fixture.bridge, fixture.context,
            "Lfixture/OnCreateLoadingApplication;"));
        const auto records = fixture.libraries->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
    }
}

TEST_CASE("Application failure prevents launcher construction and surface effects") {
    using namespace ogplay;
    ApplicationProcess fixture;
    bool opened{};
    session::DexActivityLifecycleBindings bindings;
    bindings.bridge = fixture.bridge.get();
    bindings.context = fixture.context;
    bindings.launcher_descriptor = "Lfixture/LauncherActivity;";
    bindings.application_descriptor = "Lfixture/ThrowingApplication;";
    bindings.open_surface = [&] { opened = true; };
    session::DexActivityLifecycle lifecycle(std::move(bindings));

    CHECK_THROWS_WITH_AS(
        static_cast<void>(lifecycle.Start()),
        doctest::Contains(
            "Application onCreate raised an uncaught Java exception: "
            "Ljava/lang/RuntimeException;: application failed"),
        session::DexActivityLifecycleError);
    CHECK_FALSE(opened);
    CHECK_FALSE(fixture.context->application.IsValid());
    CHECK_FALSE(fixture.context->activity.IsValid());
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;",
                                "getInstances") == 0);
}

TEST_CASE("AndroidAppProcess starts a manifest launcher without preloading app ELF") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity");
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::dex_vm_ready);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    CHECK(fixture.app->Context()->package_resource_path ==
          "/data/app/fixture-1.apk");
    const auto apk = fixture.filesystem.Open(
        fixture.app->Context()->package_resource_path, {.read = true});
    std::array<std::byte, 4> apk_magic{};
    CHECK(fixture.filesystem.Read(apk, apk_magic) == apk_magic.size());
    CHECK(apk_magic == std::array{
                           std::byte{0x50}, std::byte{0x4b},
                           std::byte{0x03}, std::byte{0x04}});
    CHECK_THROWS_AS(
        static_cast<void>(fixture.filesystem.Open(
            fixture.app->Context()->package_resource_path, {.write = true})),
        runtime::VfsError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.app->StartLauncherActivity()),
        session::AndroidAppProcessError);

    fixture.app->StartApplication();
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::application_started);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/LauncherActivity;", "getStage") == 3);
    CHECK(fixture.app->Context()->application.IsValid());
    CHECK(fixture.app->Context()->activity.IsValid());

    const auto stopped = fixture.app->Stop();
    CHECK(stopped.state == session::LifecycleRunState::stopped);
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK(fixture.app->State() ==
          session::AndroidAppProcessState::stopped);
}

TEST_CASE("DVM-89 initial traversal waits until an existing worker parks") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LateParkActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getWorkerPhaseAtTraversal") == 1);
    const auto threads = fixture.app->DexVm().Threads().Snapshot();
    CHECK(std::ranges::any_of(threads, [](const auto& thread) {
        return thread.wait_state ==
               runtime::dexvm::VmThreadWaitState::sleeping;
    }));
    CHECK(fixture.logger.Snapshot(
              core::LogLevel::warn, "session.dex_lifecycle").empty());
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("DVM-89 initial traversal fails open when a worker never parks") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.NeverParkActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    const auto warnings = fixture.logger.Snapshot(
        core::LogLevel::warn, "session.dex_lifecycle");
    REQUIRE(warnings.size() == 1U);
    CHECK(warnings.front().message ==
          "initial Java thread quiescence handshake reached yield limit");
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("DVM-89 initial traversal without workers keeps focus deferred") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.HandshakeActivity", true, false);
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getTraversalCount") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getFocusEvents") == 0);
    CHECK(fixture.logger.Snapshot(
              core::LogLevel::warn, "session.dex_lifecycle").empty());

    static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
    CHECK(fixture.CallStaticInt("Lfixture/HandshakeView;",
                                "getFocusEvents") == 1);
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("Activity.isTaskRoot is true for the launcher and false after a handoff") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.TaskRootLauncherActivity");
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    // The launcher recorded its own answer before requesting the handoff.
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootLauncherActivity;",
                                "getRootSeen") == 1);
    // The handoff really ran, and the arriving activity answered false.
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootChildActivity;",
                                "getInstances") == 1);
    CHECK(fixture.CallStaticInt("Lfixture/TaskRootChildActivity;",
                                "getRootSeen") == 0);
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("AndroidAppProcess supports a pure Java APK without a Profile or ABI") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false);
    CHECK_FALSE(fixture.app->SelectedAbi().has_value());
    REQUIRE(fixture.app->NativeLibraries() != nullptr);
    CHECK(fixture.app->NativeLibraries()->Records().empty());
    fixture.app->StartApplication();
    const auto started = fixture.app->StartLauncherActivity();
    CHECK(started.state == session::LifecycleRunState::running);
    CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
    static_cast<void>(fixture.app->Stop());
}

TEST_CASE("AndroidAppProcess keeps Activity native loads Java-driven") {
    using namespace ogplay;

    SUBCASE("Activity clinit loads after Application") {
        OrchestratedApp fixture("fixture.ClinitLoadingActivity");
        fixture.app->StartApplication();
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        const auto records = fixture.app->NativeLibraries()->Records();
        REQUIRE(records.size() == 1U);
        CHECK(records[0].soname == "liba.so");
        CHECK(records[0].jni_on_load_calls == 1U);
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 1U);
    }

    SUBCASE("Activity onCreate loads after construction") {
        OrchestratedApp fixture("fixture.OnCreateLoadingActivity");
        fixture.app->StartApplication();
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 0U);
        static_cast<void>(fixture.app->StartLauncherActivity());
        REQUIRE(fixture.app->NativeLibraries() != nullptr);
        CHECK(fixture.app->NativeLibraries()->Records().size() == 1U);
        CHECK(fixture.app->NativeProcess().ApplicationModuleCount() == 1U);
    }
}

TEST_CASE("AndroidAppProcess rejects a manifest without a launcher") {
    CHECK_THROWS_AS(
        static_cast<void>(OrchestratedApp("fixture.LauncherActivity", false)),
        ogplay::session::AndroidAppProcessError);
}

TEST_CASE("run-apk delegates application startup and never selects an ELF root") {
    const auto read_source = [](const std::string_view relative) {
        const auto path = std::string(OGPLAY_SOURCE_DIR) + std::string(relative);
        std::ifstream input(path, std::ios::binary);
        if (!input.good()) throw std::runtime_error("missing frontend source");
        return std::string{std::istreambuf_iterator<char>(input), {}};
    };
    const auto cli = read_source("/src/frontend/cli/run_apk.cpp");
    CHECK(cli.find("AndroidAppProcess::Create") != std::string::npos);
    CHECK(cli.find("SelectApkCompatibilityProfile") != std::string::npos);
    CHECK(cli.find("AndroidGuestCallSession::Start") == std::string::npos);
    CHECK(cli.find("PrepareApkProfileLaunch") == std::string::npos);
    CHECK(cli.find("MatchApkTitleProfile") == std::string::npos);
    CHECK(cli.find("InitializeJniLibrary") == std::string::npos);
    CHECK(cli.find("so_sha256") == std::string::npos);

    const auto gui = read_source("/src/frontend/gui/import.cpp");
    CHECK(gui.find("SelectApkCompatibilityProfile") != std::string::npos);
    CHECK(gui.find("MatchApkTitleProfile") == std::string::npos);
}

TEST_CASE("DVM-105 AES uses BootDex and real guest libcrypto") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                                "libcrypto.so", "libogplay_cipher.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name,
                                 std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](char c) { return static_cast<std::byte>(c); });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11, gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal, gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan, gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = linker.FindClass(owner);
            REQUIRE(type.has_value());
            auto method = linker.FindDirectMethod(*type, name, desc);
            REQUIRE_MESSAGE(method.has_value(), name);
            auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto invoke = [&](VmObjectRef obj, const char* name, const char* desc,
                                std::vector<VmValue> args) {
            auto type = vm.Model().ObjectClass(obj);
            auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(obj));
            auto result = vm.Call(linker.Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto bytes = [&](const char* hex) {
            std::vector<std::byte> data;
            for (std::size_t i = 0; hex[i]; i += 2)
                data.push_back(
                    static_cast<std::byte>(std::stoul(std::string(hex + i, 2), nullptr, 16)));
            auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                      runtime::JniPrimitiveKind::byte,
                                                      static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(array, 0, data);
            return array;
        };
        const auto key_bytes = bytes("000102030405060708090a0b0c0d0e0f");
        const auto key = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
        const auto key_roots = vm.ProtectReferences(std::array{key, key_bytes});
        direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
               {VmValue::Ref(key), VmValue::Ref(key_bytes), VmValue::Ref(vm.NewStringUtf8("AES"))});
        const auto cipher = direct("Ljavax/crypto/Cipher;", "getInstance",
                                   "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                                   {VmValue::Ref(vm.NewStringUtf8("AES/ECB/NoPadding"))})
                                .ref;
        const auto cipher_roots = vm.ProtectReferences(std::array{cipher});
        invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
        const auto result = invoke(cipher, "doFinal", "([B)[B",
                                   {VmValue::Ref(bytes("00112233445566778899aabbccddeeff"))})
                                .ref;
        const auto expected = bytes("69c4e0d86a7b0430d8cdb78070b4c55a");
        CHECK(vm.Model().ReadByteRegion(result, 0, 16) ==
              vm.Model().ReadByteRegion(expected, 0, 16));
        CHECK(vm.GuestNativeResourceCount() == 1);
        invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(2), VmValue::Ref(key)});
        const auto plain = invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(expected)}).ref;
        CHECK(vm.Model().ReadByteRegion(plain, 0, 16) ==
              vm.Model().ReadByteRegion(bytes("00112233445566778899aabbccddeeff"), 0, 16));
        const auto raw_invoke = [&](VmObjectRef obj, const char* name, const char* desc,
                                    std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(obj);
            const auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(obj));
            return vm.Call(linker.Class(type).vtable[*slot], args);
        };
        const auto expect_exception = [&](const VmCallOutcome& result, const char* descriptor) {
            INFO(result.exception_message);
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor == descriptor);
        };
        const auto make_cipher = [&](const std::string& transformation) {
            return direct("Ljavax/crypto/Cipher;", "getInstance",
                          "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                          {VmValue::Ref(vm.NewStringUtf8(transformation))})
                .ref;
        };
        const auto make_key = [&](const char* hex) {
            const auto object = vm.NewIntrinsicInstance("Ljavax/crypto/spec/SecretKeySpec;");
            const auto roots = vm.ProtectReferences(std::array{object});
            const auto data = bytes(hex);
            const auto data_roots = vm.ProtectReferences(std::array{data});
            direct(
                "Ljavax/crypto/spec/SecretKeySpec;", "<init>", "([BLjava/lang/String;)V",
                {VmValue::Ref(object), VmValue::Ref(data), VmValue::Ref(vm.NewStringUtf8("AES"))});
            return object;
        };
        const auto make_iv = [&](const char* hex) {
            const auto object = vm.NewIntrinsicInstance("Ljavax/crypto/spec/IvParameterSpec;");
            const auto roots = vm.ProtectReferences(std::array{object});
            direct("Ljavax/crypto/spec/IvParameterSpec;", "<init>", "([B)V",
                   {VmValue::Ref(object), VmValue::Ref(bytes(hex))});
            return object;
        };
        const auto read = [&](VmObjectRef object) {
            return object.IsValid()
                       ? vm.Model().ReadByteRegion(object, 0, vm.Model().ArrayLength(object))
                       : std::vector<std::byte>{};
        };
        struct Vector {
            const char* key;
            const char* ecb;
            const char* cbc;
            const char* ctr;
        };
        // NIST SP 800-38A F.1/F.2/F.5, first block, all three AES key lengths.
        for (const auto& vector : std::array{
                 Vector{"2b7e151628aed2a6abf7158809cf4f3c", "3ad77bb40d7a3660a89ecaf32466ef97",
                        "7649abac8119b246cee98e9b12e9197d", "874d6191b620e3261bef6864990db6ce"},
                 Vector{"8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
                        "bd334f1d6e45f25ff712a214571fa5cc", "4f021db243bc633d7178183a9fa071e8",
                        "1abc932417521ca24f2b0459fe7e6e0b"},
                 Vector{"603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4",
                        "f3eed1bdb5d2a03c064b5a7e3db181f8", "f58c4c04d6e5f1ba779eabfb5f7bfbd6",
                        "601ec313775789a5b7a7f504bbf3d228"}}) {
            const auto vector_key = make_key(vector.key);
            const auto vector_roots = vm.ProtectReferences(std::array{vector_key});
            for (const auto mode : {"ECB", "CBC", "CTR"}) {
                CAPTURE(mode);
                CAPTURE(vector.key);
                const auto aes = make_cipher(std::string("AES/") + mode + "/NoPadding");
                const auto roots = vm.ProtectReferences(std::array{aes});
                const auto iv =
                    make_iv(std::string_view(mode) == "CTR" ? "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
                                                            : "000102030405060708090a0b0c0d0e0f");
                const auto iv_roots = vm.ProtectReferences(std::array{iv});
                const auto initialize = [&](int operation) {
                    if (std::string_view(mode) == "ECB")
                        invoke(aes, "init", "(ILjava/security/Key;)V",
                               {VmValue::Int(operation), VmValue::Ref(vector_key)});
                    else
                        invoke(
                            aes, "init",
                            "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                            {VmValue::Int(operation), VmValue::Ref(vector_key), VmValue::Ref(iv)});
                };
                initialize(1);
                const auto input = bytes("6bc1bee22e409f96e93d7e117393172a");
                const auto input_roots = vm.ProtectReferences(std::array{input});
                const auto output = invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref;
                const auto output_roots = vm.ProtectReferences(std::array{output});
                const auto answer = bytes(std::string_view(mode) == "ECB"   ? vector.ecb
                                          : std::string_view(mode) == "CBC" ? vector.cbc
                                                                            : vector.ctr);
                CHECK(read(output) == read(answer));
                // doFinal resets the key/IV and allows reuse.
                CHECK(read(invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref) ==
                      read(output));
                initialize(2);
                CHECK(read(invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(output)}).ref) ==
                      read(input));
                initialize(1);
                auto split = read(invoke(aes, "update", "([BII)[B",
                                         {VmValue::Ref(input), VmValue::Int(0), VmValue::Int(5)})
                                      .ref);
                auto tail = read(invoke(aes, "doFinal", "([BII)[B",
                                        {VmValue::Ref(input), VmValue::Int(5), VmValue::Int(11)})
                                     .ref);
                split.insert(split.end(), tail.begin(), tail.end());
                CHECK(split == read(output));
                const auto provider =
                    invoke(aes, "getProvider", "()Ljava/security/Provider;", {}).ref;
                CHECK(vm.StringUtf8(invoke(provider, "getName", "()Ljava/lang/String;", {}).ref) ==
                      "AndroidOpenSSL");
            }
        }
        for (const auto mode : {"ECB", "CBC"}) {
            const auto aes = make_cipher(std::string("AES/") + mode + "/PKCS5Padding");
            const auto roots = vm.ProtectReferences(std::array{aes});
            invoke(aes, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
            const auto iv_bytes = invoke(aes, "getIV", "()[B", {}).ref;
            const auto iv_roots = vm.ProtectReferences(std::array{iv_bytes});
            for (const auto length : {0, 1, 15, 16, 17, 31, 32, 65}) {
                CAPTURE(mode);
                CAPTURE(length);
                const auto input = vm.Model().NewPrimitiveArray(
                    linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte, length);
                const auto input_roots = vm.ProtectReferences(std::array{input});
                const auto encrypted = invoke(aes, "doFinal", "([B)[B", {VmValue::Ref(input)}).ref;
                const auto encrypted_roots = vm.ProtectReferences(std::array{encrypted});
                CHECK(vm.Model().ArrayLength(encrypted) == (length / 16 + 1) * 16);
                const auto decrypt = make_cipher(std::string("AES/") + mode + "/PKCS5Padding");
                const auto decrypt_roots = vm.ProtectReferences(std::array{decrypt});
                if (iv_bytes.IsValid()) {
                    const auto spec =
                        vm.NewIntrinsicInstance("Ljavax/crypto/spec/IvParameterSpec;");
                    const auto spec_roots = vm.ProtectReferences(std::array{spec});
                    direct("Ljavax/crypto/spec/IvParameterSpec;", "<init>", "([B)V",
                           {VmValue::Ref(spec), VmValue::Ref(iv_bytes)});
                    invoke(decrypt, "init",
                           "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                           {VmValue::Int(2), VmValue::Ref(key), VmValue::Ref(spec)});
                } else
                    invoke(decrypt, "init", "(ILjava/security/Key;)V",
                           {VmValue::Int(2), VmValue::Ref(key)});
                auto restored =
                    read(invoke(decrypt, "update", "([BII)[B",
                                {VmValue::Ref(encrypted), VmValue::Int(0), VmValue::Int(7)})
                             .ref);
                auto tail = read(invoke(decrypt, "doFinal", "([BII)[B",
                                        {VmValue::Ref(encrypted), VmValue::Int(7),
                                         VmValue::Int(vm.Model().ArrayLength(encrypted) - 7)})
                                     .ref);
                restored.insert(restored.end(), tail.begin(), tail.end());
                CHECK(restored == read(input));
            }
            if (iv_bytes.IsValid()) {
                invoke(aes, "init", "(ILjava/security/Key;)V",
                       {VmValue::Int(1), VmValue::Ref(key)});
                const auto second_iv = invoke(aes, "getIV", "()[B", {}).ref;
                CHECK(read(second_iv) != read(iv_bytes));
                CHECK(read(second_iv) != std::vector<std::byte>(16));
            }
        }
        const auto bad_key = make_key("000102030405060708090a0b0c0d0e");
        expect_exception(raw_invoke(cipher, "init", "(ILjava/security/Key;)V",
                                    {VmValue::Int(1), VmValue::Ref(bad_key)}),
                         "Ljava/security/InvalidKeyException;");
        invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
        expect_exception(raw_invoke(cipher, "doFinal", "([B)[B", {VmValue::Ref(bytes("00"))}),
                         "Ljavax/crypto/IllegalBlockSizeException;");
        const auto bad_padding = make_cipher("AES/ECB/PKCS5Padding");
        const auto bad_roots = vm.ProtectReferences(std::array{bad_padding});
        invoke(bad_padding, "init", "(ILjava/security/Key;)V",
               {VmValue::Int(2), VmValue::Ref(key)});
        expect_exception(raw_invoke(bad_padding, "doFinal", "([B)[B",
                                    {VmValue::Ref(bytes("69c4e0d86a7b0430d8cdb78070b4c55a"))}),
                         "Ljavax/crypto/BadPaddingException;");
        invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
        const auto block = bytes("00112233445566778899aabbccddeeff");
        const auto block_roots = vm.ProtectReferences(std::array{block});
        expect_exception(raw_invoke(cipher, "doFinal", "([BII[BI)I",
                                    {VmValue::Ref(block), VmValue::Int(0), VmValue::Int(16),
                                     VmValue::Ref(bytes("00")), VmValue::Int(0)}),
                         "Ljavax/crypto/ShortBufferException;");
        CHECK(invoke(cipher, "doFinal", "([BII[BI)I",
                     {VmValue::Ref(block), VmValue::Int(0), VmValue::Int(16), VmValue::Ref(block),
                      VmValue::Int(0)})
                  .AsInt() == 16);
        CHECK(read(block) == read(bytes("69c4e0d86a7b0430d8cdb78070b4c55a")));
        const auto cbc = make_cipher("AES/CBC/NoPadding");
        const auto cbc_roots = vm.ProtectReferences(std::array{cbc});
        expect_exception(
            raw_invoke(cbc, "init",
                       "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                       {VmValue::Int(1), VmValue::Ref(key), VmValue::Ref(make_iv("00"))}),
            "Ljava/security/InvalidAlgorithmParameterException;");
        const auto cipher_type = *linker.FindClass("Ljavax/crypto/Cipher;");
        const auto factory = *linker.FindDirectMethod(cipher_type, "getInstance",
                                                      "(Ljava/lang/String;)Ljavax/crypto/Cipher;");
        for (const auto name : {"AES/GCM/NoPadding", "AES/CTR/PKCS5Padding", "DES/ECB/NoPadding",
                                "RSA/ECB/PKCS1Padding"}) {
            CAPTURE(std::string(name));
            expect_exception(vm.Call(factory, std::array{VmValue::Ref(vm.NewStringUtf8(name))}),
                             "Ljava/security/NoSuchAlgorithmException;");
        }
        const auto alias = make_cipher("AES");
        const auto alias_roots = vm.ProtectReferences(std::array{alias});
        invoke(alias, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
        CHECK(read(invoke(alias, "doFinal", "([B)[B", {VmValue::Ref(bytes(""))}).ref).size() == 16);
        // Tokens stay private to guest libcrypto and stale/foreign tokens fail explicitly.
        const auto native = *linker.FindClass("Lcom/android/org/conscrypt/NativeCrypto;");
        const auto allocate = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_new", "()J");
        const auto cleanup = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_cleanup", "(J)V");
        const auto size = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_block_size", "(J)I");
        CHECK(linker.Method(allocate).kind == MethodKind::native);
        CHECK_FALSE(static_cast<bool>(linker.Method(allocate).implementation));
        const auto token = vm.Call(allocate, {}).value.AsLong();
        CHECK_THROWS_AS(vm.Call(size, std::array{VmValue::Long(token)}), VmJavaThrow);
        static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)}));
        CHECK_THROWS_AS(vm.Call(cleanup, std::array{VmValue::Long(token)}), VmJavaThrow);
        CHECK_THROWS_AS(vm.Call(size, std::array{VmValue::Long(0x123456789LL)}), VmJavaThrow);
        CHECK(direct("Lfixture/CipherThreads;", "exercise", "()I", {}).AsInt() == 32);
        const auto before_gc = vm.GuestNativeResourceCount();
        CHECK(before_gc > 3);
        static_cast<void>(vm.CollectGarbage("cipher-test"));
        CHECK(vm.GuestNativeResourceCount() < before_gc);
        CHECK(vm.GuestNativeResourceCount() >= 2);
        invoke(cipher, "init", "(ILjava/security/Key;)V", {VmValue::Int(1), VmValue::Ref(key)});
        CHECK(read(invoke(cipher, "doFinal", "([B)[B",
                          {VmValue::Ref(bytes("00112233445566778899aabbccddeeff"))})
                       .ref) == read(bytes("69c4e0d86a7b0430d8cdb78070b4c55a")));
        vm.ReleaseGuestNativeResources(true);
        CHECK(vm.GuestNativeResourceCount() == 0);
    }
}
