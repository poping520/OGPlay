#include <set>
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

    explicit ApplicationProcess(
        ogplay::runtime::dexvm::InterpreterBackend backend =
            ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch) {
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
            ledger, nullptr, ogplay::runtime::DexVmBridgeConfig{
                .interpreter = {.backend = backend}}, ogplay::test::ReadBootDex());
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
                             const bool with_native = true,
                             const std::string& launcher_alias = {}) {
        context->apk_bytes = {
            std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}};
        const ogplay::runtime::BionicModuleSource system{
            "libc.so", libc};
        std::vector<ogplay::loader::ApkNativeLibrary> libraries;
        if (with_native) libraries.push_back(Library("liba.so", native_a));
        ogplay::session::AndroidAppProcessRequest request;
        request.manifest = AppManifest(activity, has_launcher);
        if (!launcher_alias.empty()) {
            auto filters = request.manifest.activity_components.front().intent_filters;
            request.manifest.activity_components.front().intent_filters.clear();
            request.manifest.activity_components.push_back(
                {ogplay::loader::AndroidManifestComponentKind::activity_alias,
                 launcher_alias, activity, true, std::move(filters)});
        }
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

TEST_CASE("DVM-111 scheduled tasks use VM interface assignability in shared JNI arrays") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& vm = fixture.bridge->Vm();
        auto& linker = fixture.bridge->Linker();
        auto& model = vm.Model();
        std::vector<Interpreter::RootScope> roots;
        struct StopWorkers final {
            VmThreadRuntime& threads;
            ~StopWorkers() { threads.Shutdown(); }
        } stop_workers{fixture.bridge->Threads()};
        {
            VmExecutionLockScope lock(vm.ExecutionLock());
            auto keep = [&](VmObjectRef ref) {
                roots.push_back(vm.ProtectReferences(std::array{ref}));
                return ref;
            };
            auto direct = [&](const char* owner, const char* name, const char* signature,
                              std::vector<VmValue> args) {
                const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, signature);
                REQUIRE(method);
                const auto result = vm.Call(*method, args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            auto call = [&](VmObjectRef receiver, const char* name, const char* signature,
                            std::vector<VmValue> args = {}) {
                const auto type = model.ObjectClass(receiver);
                const auto slot = linker.FindVtableIndex(type, name, signature);
                REQUIRE(slot);
                args.insert(args.begin(), VmValue::Ref(receiver));
                const auto result = vm.Call(linker.Class(type).vtable[*slot], args);
                REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
                return result.value;
            };
            const auto pool = keep(vm.NewIntrinsicInstance("Ljava/util/concurrent/ScheduledThreadPoolExecutor;"));
            direct("Ljava/util/concurrent/ScheduledThreadPoolExecutor;", "<init>", "(I)V",
                   {VmValue::Ref(pool), VmValue::Int(1)});
            const auto runnable = keep(vm.NewIntrinsicInstance("Ljava/lang/Thread;"));
            direct("Ljava/lang/Thread;", "<init>", "()V", {VmValue::Ref(runnable)});
            const auto unit = keep(direct("Ljava/util/concurrent/TimeUnit;", "valueOf",
                "(Ljava/lang/String;)Ljava/util/concurrent/TimeUnit;",
                {VmValue::Ref(vm.NewStringUtf8("DAYS"))}).ref);
            // The real BootDex queue stores ScheduledFutureTask into RunnableScheduledFuture[].
            const auto future = keep(call(pool, "schedule",
                "(Ljava/lang/Runnable;JLjava/util/concurrent/TimeUnit;)Ljava/util/concurrent/ScheduledFuture;",
                {VmValue::Ref(runnable), VmValue::Long(1), VmValue::Ref(unit)}).ref);
            const auto task_type = model.ObjectClass(future);
            for (const auto* descriptor : {"Ljava/util/concurrent/RunnableScheduledFuture;",
                    "Ljava/util/concurrent/RunnableFuture;", "Ljava/util/concurrent/Future;",
                    "Ljava/util/concurrent/Delayed;", "Ljava/lang/Comparable;", "Ljava/lang/Runnable;"}) {
                const auto element = linker.ResolveDescriptor(descriptor);
                CHECK(linker.IsAssignable(element, task_type));
                const auto array = keep(model.NewObjectArray(
                    linker.ResolveDescriptor(std::string("[") + descriptor), element, 1));
                model.SetObjectElement(array, 0, future);
                CHECK(model.GetObjectElement(array, 0) == future);
            }
            const auto element = linker.ResolveDescriptor("Ljava/util/concurrent/RunnableScheduledFuture;");
            const auto typed = keep(model.NewObjectArray(
                linker.ResolveDescriptor("[Ljava/util/concurrent/RunnableScheduledFuture;"), element, 1));
            auto& arrays = fixture.session->Objects().ObjectArrays();
            const auto host_only = fixture.session->Classes().RegisterClass(
                {"fixture/HostOnly", "java/lang/Object", {}, {}});
            const runtime::JniObjectValue host_value{runtime::AllocateJniHostObjectIdentity(), host_only};
            const auto host_array = arrays.New(host_only, 1, host_value);
            CHECK(arrays.Get(host_array, 0) == host_value);
            CHECK_THROWS_AS(arrays.Set(model.ToIdentity(typed), 0, host_value), runtime::JniObjectArrayError);
            arrays.Delete(host_array);
            const auto task_identity = fixture.bridge->RegisteredClassIdentity(task_type);
            const auto thread_identity = fixture.bridge->RegisteredClassIdentity(model.ObjectClass(runnable));
            REQUIRE(task_identity);
            REQUIRE(thread_identity);
            const runtime::JniObjectValue value{model.ToIdentity(future), *task_identity};
            // Native-side initial values and updates follow the same authoritative VM relation.
            const auto native_array = arrays.New(arrays.ElementClass(model.ToIdentity(typed)), 1, value);
            arrays.Set(model.ToIdentity(typed), 0, value);
            CHECK_THROWS_AS(arrays.Set(model.ToIdentity(typed), 0,
                runtime::JniObjectValue{model.ToIdentity(runnable), *thread_identity}), runtime::JniObjectArrayError);
            CHECK(arrays.Get(model.ToIdentity(typed), 0) == value);
            arrays.Delete(native_array);
            static_cast<void>(vm.CollectGarbage());
            CHECK(model.GetObjectElement(typed, 0) == future);
            CHECK(call(future, "isDone", "()Z").AsInt() == 0);
            const auto pending = keep(call(pool, "shutdownNow", "()Ljava/util/List;").ref);
            CHECK(call(pending, "size", "()I").AsInt() == 1);
            CHECK(call(pending, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)}).ref == future);
            CHECK(call(future, "cancel", "(Z)Z", {VmValue::Int(0)}).AsInt() == 1);
        }
        fixture.bridge->Threads().Shutdown();
        CHECK_FALSE(fixture.bridge->Threads().TakeFailure().has_value());
    }
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

TEST_CASE("DVM-106 Certificate parses DER PEM and verifies RSA EC through guest OpenSSL") {
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
        const auto array = [&](const std::vector<std::byte>& data) {
            auto result = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"),
                                                       runtime::JniPrimitiveKind::byte,
                                                       static_cast<runtime::JniSize>(data.size()));
            vm.Model().WriteByteRegion(result, 0, data);
            return result;
        };
        const auto file = [&](const char* name) {
            std::ifstream input(
                std::string(OGPLAY_SOURCE_DIR) + "/tests/fixtures/certificates/" + name,
                std::ios::binary);
            REQUIRE(input.good());
            std::vector<char> chars{std::istreambuf_iterator<char>(input), {}};
            std::vector<std::byte> result(chars.size());
            std::transform(chars.begin(), chars.end(), result.begin(),
                           [](char b) { return static_cast<std::byte>(b); });
            return result;
        };
        const auto raw = [&](VmObjectRef object, const char* name, const char* desc,
                             std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(object);
            const auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE_MESSAGE(slot.has_value(), name);
            args.insert(args.begin(), VmValue::Ref(object));
            return vm.Call(linker.Class(type).vtable[*slot], args);
        };
        const auto factory = direct("Ljava/security/cert/CertificateFactory;", "getInstance",
                                    "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;",
                                    {VmValue::Ref(vm.NewStringUtf8("X.509"))})
                                 .ref;
        const auto factory_root = vm.ProtectReferences(std::array{factory});
        const auto stream = [&](const std::vector<std::byte>& data) {
            const auto bytes = array(data);
            const auto bytes_root = vm.ProtectReferences(std::array{bytes});
            const auto input = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V",
                   {VmValue::Ref(input), VmValue::Ref(bytes)});
            return input;
        };
        const auto parse = [&](const std::vector<std::byte>& data) {
            return invoke(factory, "generateCertificate",
                          "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;",
                          {VmValue::Ref(stream(data))})
                .ref;
        };
        const auto read = [&](VmObjectRef bytes) {
            return vm.Model().ReadByteRegion(bytes, 0, vm.Model().ArrayLength(bytes));
        };
        for (const auto stem : {"rsa", "ec"}) {
            CAPTURE(stem);
            const auto der = file((std::string(stem) + ".der").c_str());
            const auto cert = parse(der);
            const auto cert_root = vm.ProtectReferences(std::array{cert});
            const auto pem = parse(file((std::string(stem) + ".pem").c_str()));
            const auto pem_root = vm.ProtectReferences(std::array{pem});
            CHECK(invoke(cert, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(pem)}).AsInt() ==
                  1);
            CHECK(read(invoke(cert, "getEncoded", "()[B", {}).ref) == der);
            CHECK(invoke(cert, "getVersion", "()I", {}).AsInt() ==
                  (std::string(stem) == "rsa" ? 3 : 1));
            CHECK(vm.StringUtf8(invoke(cert, "getType", "()Ljava/lang/String;", {}).ref) ==
                  "X.509");
            const auto serial = invoke(cert, "getSerialNumber", "()Ljava/math/BigInteger;", {}).ref;
            const auto serial_root = vm.ProtectReferences(std::array{serial});
            CHECK(
                vm.StringUtf8(
                    invoke(serial, "toString", "(I)Ljava/lang/String;", {VmValue::Int(16)}).ref) ==
                (std::string(stem) == "rsa" ? "1234567890abcdef1234567890abcdef12345678" : "2a"));
            CHECK(
                !vm.StringUtf8(invoke(serial, "toString", "()Ljava/lang/String;", {}).ref).empty());
            const auto issuer = invoke(cert, "getIssuerX500Principal",
                                       "()Ljavax/security/auth/x500/X500Principal;", {})
                                    .ref;
            const auto issuer_root = vm.ProtectReferences(std::array{issuer});
            const auto subject = invoke(cert, "getSubjectX500Principal",
                                        "()Ljavax/security/auth/x500/X500Principal;", {})
                                     .ref;
            CHECK(invoke(issuer, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(subject)})
                      .AsInt() == 1);
            CHECK(vm.StringUtf8(invoke(cert, "getSigAlgName", "()Ljava/lang/String;", {}).ref) ==
                  (std::string(stem) == "rsa" ? "SHA256withRSA" : "SHA256withECDSA"));
            const auto pub = invoke(cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
            const auto pub_root = vm.ProtectReferences(std::array{pub});
            CHECK(vm.StringUtf8(invoke(pub, "getFormat", "()Ljava/lang/String;", {}).ref) ==
                  "X.509");
            invoke(cert, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(pub)});
            invoke(cert, "verify", "(Ljava/security/PublicKey;Ljava/lang/String;)V",
                   {VmValue::Ref(pub), VmValue::Ref(vm.NewStringUtf8("AndroidOpenSSL"))});
            const auto bad_provider =
                raw(cert, "verify", "(Ljava/security/PublicKey;Ljava/lang/String;)V",
                    {VmValue::Ref(pub), VmValue::Ref(vm.NewStringUtf8("missing"))});
            REQUIRE(bad_provider.exception.IsValid());
            CHECK(linker.Class(bad_provider.exception_class).descriptor ==
                  "Ljava/security/NoSuchProviderException;");
            auto tampered = der;
            tampered.back() ^= std::byte{1};
            const auto invalid = parse(tampered);
            const auto bad =
                raw(invalid, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(pub)});
            REQUIRE(bad.exception.IsValid());
            CHECK(linker.Class(bad.exception_class).descriptor ==
                  "Ljava/security/SignatureException;");
            for (const auto milliseconds :
                 {INT64_C(0), INT64_C(1900000000000), INT64_C(4102444800000)}) {
                const auto date = vm.NewIntrinsicInstance("Ljava/util/Date;");
                direct("Ljava/util/Date;", "<init>", "(J)V",
                       {VmValue::Ref(date), VmValue::Long(milliseconds)});
                const auto validity =
                    raw(cert, "checkValidity", "(Ljava/util/Date;)V", {VmValue::Ref(date)});
                if (milliseconds == INT64_C(1900000000000))
                    CHECK(!validity.exception.IsValid());
                else {
                    REQUIRE(validity.exception.IsValid());
                    CHECK(linker.Class(validity.exception_class).descriptor ==
                          (milliseconds == 0
                               ? "Ljava/security/cert/CertificateNotYetValidException;"
                               : "Ljava/security/cert/CertificateExpiredException;"));
                }
            }
            if (std::string(stem) == "rsa") {
                CHECK(invoke(cert, "getBasicConstraints", "()I", {}).AsInt() == 1);
                const auto usage = invoke(cert, "getKeyUsage", "()[Z", {}).ref;
                CHECK(vm.Model().GetPrimitiveElement(usage, 0) == 1);
                CHECK(vm.Model().GetPrimitiveElement(usage, 5) == 1);
                const auto names =
                    invoke(cert, "getSubjectAlternativeNames", "()Ljava/util/Collection;", {}).ref;
                CHECK(invoke(names, "size", "()I", {}).AsInt() == 2);
            }
        }
        auto bundle = file("rsa.pem");
        const auto ec = file("ec.pem");
        bundle.insert(bundle.end(), ec.begin(), ec.end());
        const auto certs =
            invoke(factory, "generateCertificates", "(Ljava/io/InputStream;)Ljava/util/Collection;",
                   {VmValue::Ref(stream(bundle))})
                .ref;
        CHECK(invoke(certs, "size", "()I", {}).AsInt() == 2);
        const auto issuer_cert = parse(file("rsa.der"));
        const auto issuer_root = vm.ProtectReferences(std::array{issuer_cert});
        const auto issuer_key =
            invoke(issuer_cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
        const auto issuer_key_root = vm.ProtectReferences(std::array{issuer_key});
        const auto leaf = parse(file("leaf.der"));
        const auto leaf_root = vm.ProtectReferences(std::array{leaf});
        invoke(leaf, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        const auto leaf_key = invoke(leaf, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
        const auto wrong_key =
            raw(leaf, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(leaf_key)});
        REQUIRE(wrong_key.exception.IsValid());
        CHECK(linker.Class(wrong_key.exception_class).descriptor ==
              "Ljava/security/SignatureException;");
        const auto legacy = direct("Ljavax/security/cert/X509Certificate;", "getInstance",
                                   "([B)Ljavax/security/cert/X509Certificate;",
                                   {VmValue::Ref(array(file("rsa.der")))})
                                .ref;
        const auto legacy_root = vm.ProtectReferences(std::array{legacy});
        CHECK(read(invoke(legacy, "getEncoded", "()[B", {}).ref) == file("rsa.der"));
        invoke(legacy, "verify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        const auto chain = vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        const auto chain_root = vm.ProtectReferences(std::array{chain});
        direct("Ljava/util/ArrayList;", "<init>", "()V", {VmValue::Ref(chain)});
        invoke(chain, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(leaf)});
        invoke(chain, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(issuer_cert)});
        const auto path =
            invoke(factory, "generateCertPath", "(Ljava/util/List;)Ljava/security/cert/CertPath;",
                   {VmValue::Ref(chain)})
                .ref;
        const auto path_root = vm.ProtectReferences(std::array{path});
        for (const auto encoding : {"PkiPath", "PKCS7"}) {
            const auto der = invoke(path, "getEncoded", "(Ljava/lang/String;)[B",
                                    {VmValue::Ref(vm.NewStringUtf8(encoding))})
                                 .ref;
            const auto der_root = vm.ProtectReferences(std::array{der});
            const auto decoded =
                invoke(factory, "generateCertPath",
                       "(Ljava/io/InputStream;Ljava/lang/String;)Ljava/security/cert/CertPath;",
                       {VmValue::Ref(stream(read(der))), VmValue::Ref(vm.NewStringUtf8(encoding))})
                    .ref;
            if (std::string_view(encoding) == "PkiPath")
                CHECK(invoke(decoded, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(path)})
                          .AsInt() == 1);
            else {  // PKCS7 contains a SET OF certificates; it does not preserve path order.
                const auto list = invoke(decoded, "getCertificates", "()Ljava/util/List;", {}).ref;
                const auto list_root = vm.ProtectReferences(std::array{list});
                CHECK(invoke(list, "size", "()I", {}).AsInt() == 2);
                CHECK(invoke(list, "contains", "(Ljava/lang/Object;)Z", {VmValue::Ref(leaf)})
                          .AsInt() == 1);
                CHECK(invoke(list, "contains", "(Ljava/lang/Object;)Z", {VmValue::Ref(issuer_cert)})
                          .AsInt() == 1);
            }
        }
        const auto bundle_der = file("chain.p7b");
        const auto pkcs7 =
            invoke(factory, "generateCertificates", "(Ljava/io/InputStream;)Ljava/util/Collection;",
                   {VmValue::Ref(stream(bundle_der))})
                .ref;
        CHECK(invoke(pkcs7, "size", "()I", {}).AsInt() == 2);
        std::ifstream vectors(std::string(OGPLAY_SOURCE_DIR) +
                              "/tests/fixtures/certificates/signatures.txt");
        REQUIRE(vectors.good());
        const std::string message = "OGPlay certificate signature fixture\n";
        std::vector<std::byte> message_bytes;
        for (char ch : message) message_bytes.push_back(static_cast<std::byte>(ch));
        std::string algorithm, signature_hex;
        int vector_count = 0;
        while (vectors >> algorithm >> signature_hex) {
            ++vector_count;
            CAPTURE(algorithm);
            const bool rsa = algorithm.ends_with("RSA");
            const auto cert = parse(file(rsa ? "rsa.der" : "ec.der"));
            const auto cert_root = vm.ProtectReferences(std::array{cert});
            const auto key = invoke(cert, "getPublicKey", "()Ljava/security/PublicKey;", {}).ref;
            const auto key_root = vm.ProtectReferences(std::array{key});
            const auto verifier = direct("Ljava/security/Signature;", "getInstance",
                                         "(Ljava/lang/String;)Ljava/security/Signature;",
                                         {VmValue::Ref(vm.NewStringUtf8(algorithm))})
                                      .ref;
            const auto verifier_root = vm.ProtectReferences(std::array{verifier});
            invoke(verifier, "initVerify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(key)});
            std::vector<std::byte> sig;
            for (std::size_t i = 0; i < signature_hex.size(); i += 2)
                sig.push_back(
                    static_cast<std::byte>(std::stoul(signature_hex.substr(i, 2), nullptr, 16)));
            invoke(verifier, "update", "(B)V",
                   {VmValue::Int(std::to_integer<int>(message_bytes[0]))});
            invoke(verifier, "update", "([BII)V",
                   {VmValue::Ref(array(message_bytes)), VmValue::Int(1),
                    VmValue::Int(static_cast<int>(message_bytes.size()) - 1)});
            static_cast<void>(vm.CollectGarbage());
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() == 1);
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(message_bytes))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() ==
                  1);  // Reset after success.
            auto bad_message = message_bytes;
            bad_message[0] ^= std::byte{1};
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(bad_message))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() == 0);
            invoke(verifier, "update", "([B)V", {VmValue::Ref(array(message_bytes))});
            CHECK(invoke(verifier, "verify", "([B)Z", {VmValue::Ref(array(sig))}).AsInt() ==
                  1);  // Reset after mismatch.
        }
        CHECK(vector_count == 10);
        const auto verifier = direct("Ljava/security/Signature;", "getInstance",
                                     "(Ljava/lang/String;)Ljava/security/Signature;",
                                     {VmValue::Ref(vm.NewStringUtf8("SHA256withRSA"))})
                                  .ref;
        const auto verifier_root = vm.ProtectReferences(std::array{verifier});
        const auto invalid_key =
            vm.NewIntrinsicInstance("Lorg/apache/harmony/security/x509/X509PublicKey;");
        direct("Lorg/apache/harmony/security/x509/X509PublicKey;", "<init>",
               "(Ljava/lang/String;[B[B)V",
               {VmValue::Ref(invalid_key), VmValue::Ref(vm.NewStringUtf8("RSA")),
                VmValue::Ref(array({std::byte{1}})), VmValue::Ref(VmObjectRef{})});
        const auto key_failure = raw(verifier, "initVerify", "(Ljava/security/PublicKey;)V",
                                     {VmValue::Ref(invalid_key)});
        REQUIRE(key_failure.exception.IsValid());
        CHECK(linker.Class(key_failure.exception_class).descriptor ==
              "Ljava/security/InvalidKeyException;");
        invoke(verifier, "initVerify", "(Ljava/security/PublicKey;)V", {VmValue::Ref(issuer_key)});
        invoke(verifier, "update", "([B)V", {VmValue::Ref(array(std::vector<std::byte>(1048576)))});
        const auto limit_failure = raw(verifier, "update", "(B)V", {VmValue::Int(1)});
        REQUIRE(limit_failure.exception.IsValid());
        CHECK(linker.Class(limit_failure.exception_class).descriptor ==
              "Ljava/security/SignatureException;");
        const auto signature_type = linker.ResolveDescriptor("Ljava/security/Signature;");
        const auto get_signature = linker.FindDirectMethod(
            signature_type, "getInstance", "(Ljava/lang/String;)Ljava/security/Signature;");
        REQUIRE(get_signature);
        for (const auto unsupported : {"SHA256withRSA/PSS", "SHA256withDSA", "Ed25519"}) {
            const auto failure =
                vm.Call(*get_signature, std::array{VmValue::Ref(vm.NewStringUtf8(unsupported))});
            REQUIRE(failure.exception.IsValid());
            CHECK(linker.Class(failure.exception_class).descriptor ==
                  "Ljava/security/NoSuchAlgorithmException;");
        }
        // JNI storage must apply VM covariance to arrays nested inside Object[].
        const auto objects =
            vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                      linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        const auto objects_root = vm.ProtectReferences(std::array{objects});
        const auto nested = vm.Model().NewObjectArray(linker.ResolveDescriptor("[[B"),
                                                      linker.ResolveDescriptor("[B"), 1);
        vm.Model().SetObjectElement(objects, 0, nested);
        vm.Model().SetObjectElement(nested, 0, array(message_bytes));
        const auto integers = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[I"),
                                                           runtime::JniPrimitiveKind::integer, 1);
        CHECK_THROWS_AS(vm.Model().SetObjectElement(nested, 0, integers),
                        runtime::JniObjectArrayError);
        for (const auto& malformed :
             {std::vector<std::byte>{}, std::vector<std::byte>{std::byte{0x30}, std::byte{0x80}},
              std::vector<std::byte>{std::byte{1}, std::byte{2}}}) {
            const auto result = raw(factory, "generateCertificate",
                                    "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;",
                                    {VmValue::Ref(stream(malformed))});
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor ==
                  "Ljava/security/cert/CertificateException;");
        }
    }
}

TEST_CASE("DVM-107 manifest launcher alias retains its component identity") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false,
                            "fixture.StartAlias");
    fixture.app->StartApplication();
    REQUIRE(fixture.app->StartLauncherActivity().state ==
            session::LifecycleRunState::running);
    auto& bridge = fixture.app->DexVm();
    const auto activity = fixture.context->activity;
    CHECK(bridge.Linker().Class(bridge.Model().ObjectClass(activity)).descriptor ==
          "Lfixture/LauncherActivity;");
    const auto invoke = [&](VmObjectRef receiver, const char* name,
                            const char* signature) {
        const auto type = bridge.Model().ObjectClass(receiver);
        const auto slot = bridge.Linker().FindVtableIndex(type, name, signature);
        REQUIRE(slot.has_value());
        const auto result = bridge.Vm().Call(bridge.Linker().Class(type).vtable[*slot],
                                             std::array{VmValue::Ref(receiver)});
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value.ref;
    };
    CHECK(bridge.Vm().StringUtf8(invoke(activity, "getLocalClassName",
                                        "()Ljava/lang/String;")) == "StartAlias");
    const auto component =
        invoke(activity, "getComponentName", "()Landroid/content/ComponentName;");
    CHECK(bridge.Vm().StringUtf8(
              invoke(component, "getClassName", "()Ljava/lang/String;")) ==
          "fixture.StartAlias");
    const auto intent = invoke(activity, "getIntent", "()Landroid/content/Intent;");
    CHECK(invoke(intent, "getComponent", "()Landroid/content/ComponentName;") ==
          component);
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}

TEST_CASE("DVM-108/109 UUID MessageDigest and serialization use BootDex with real guest crypto") {
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

        const auto raw = [&](VmObjectRef obj, const char* name, const char* desc, std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(obj);
            const auto slot = linker.FindVtableIndex(type, name, desc);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(obj));
            return vm.Call(linker.Class(type).vtable[*slot], args);
        };
        const auto expect = [&](const VmCallOutcome& result, const char* type) {
            REQUIRE(result.exception.IsValid());
            CHECK(linker.Class(result.exception_class).descriptor == type);
        };
        const auto digest = direct("Ljava/security/MessageDigest;", "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;", {VmValue::Ref(vm.NewStringUtf8("MD5"))}).ref;
        const auto digest_roots = vm.ProtectReferences(std::array{digest});
        const auto read = [&](VmObjectRef array) { return vm.Model().ReadByteRegion(array, 0, vm.Model().ArrayLength(array)); };
        // DVM-109 exercises the same original Java object streams used by apps.
        const auto roundtrip = [&](VmObjectRef value, const char* expected_wire = nullptr) {
            const auto value_root = vm.ProtectReferences(std::array{value});
            const auto sink = vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
            direct("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {VmValue::Ref(sink)});
            const auto output = vm.NewIntrinsicInstance("Ljava/io/ObjectOutputStream;");
            const auto output_roots = vm.ProtectReferences(std::array{sink, output});
            direct("Ljava/io/ObjectOutputStream;", "<init>", "(Ljava/io/OutputStream;)V", {VmValue::Ref(output), VmValue::Ref(sink)});
            invoke(output, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(value)});
            invoke(output, "flush", "()V", {});
            const auto wire = invoke(sink, "toByteArray", "()[B", {}).ref;
            if (expected_wire) CHECK(read(wire) == read(bytes(expected_wire)));
            const auto source = vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
            direct("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V", {VmValue::Ref(source), VmValue::Ref(wire)});
            const auto input = vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
            const auto input_roots = vm.ProtectReferences(std::array{source, input});
            direct("Ljava/io/ObjectInputStream;", "<init>", "(Ljava/io/InputStream;)V", {VmValue::Ref(input), VmValue::Ref(source)});
            static_cast<void>(vm.CollectGarbage("serialization-live-streams"));
            return invoke(input, "readObject", "()Ljava/lang/Object;", {}).ref;
        };
        const auto field_value = [&](VmObjectRef object, const char* owner, const char* name) {
            const auto field = linker.FindFieldRecursive(linker.ResolveDescriptor(owner), name, "I");
            REQUIRE(field);
            return static_cast<std::int32_t>(vm.Model().InstanceSlots(object)[linker.Field(*field).slot].bits);
        };
        const auto value = vm.NewIntrinsicInstance("Lfixture/SerializationValue;");
        direct("Lfixture/SerializationValue;", "<init>", "()V", {VmValue::Ref(value)});
        const auto next_field = linker.FindFieldRecursive(linker.ResolveDescriptor("Lfixture/SerializationValue;"), "next", "Ljava/lang/Object;");
        REQUIRE(next_field);
        vm.Model().InstanceSlots(value)[linker.Field(*next_field).slot] = {value.Value(), SlotTag::ref};
        auto restored_value = roundtrip(value);
        CHECK(restored_value != value);
        CHECK(field_value(restored_value, "Lfixture/SerializationBase;", "inherited") == 17);
        CHECK(field_value(restored_value, "Lfixture/SerializationValue;", "restored") == 73);
        CHECK(vm.Model().InstanceSlots(restored_value)[linker.Field(*next_field).slot].bits == restored_value.Value());
        const auto fields = vm.NewIntrinsicInstance("Lfixture/SerializationFields;");
        direct("Lfixture/SerializationFields;", "<init>", "()V", {VmValue::Ref(fields)});
        CHECK(field_value(roundtrip(fields), "Lfixture/SerializationFields;", "number") == 42);
        const auto uid = direct("Ljava/io/ObjectStreamClass;", "lookup", "(Ljava/lang/Class;)Ljava/io/ObjectStreamClass;",
            {VmValue::Ref(vm.Model().ClassObject(linker.ResolveDescriptor("Lfixture/DefaultUid;")))}).ref;
        CHECK(invoke(uid, "getSerialVersionUID", "()J", {}).AsLong() == INT64_C(5156351520895513822));
        const auto default_value = vm.NewIntrinsicInstance("Lfixture/DefaultUid;");
        direct("Lfixture/DefaultUid;", "<init>", "()V", {VmValue::Ref(default_value)});
        CHECK(vm.Model().ObjectClass(roundtrip(default_value)) == linker.ResolveDescriptor("Lfixture/DefaultUid;"));
        const auto graph = vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"), linker.ResolveDescriptor("Ljava/lang/Object;"), 3);
        vm.Model().SetObjectElement(graph, 0, graph);
        const auto shared_bytes = bytes("00ff010203");
        vm.Model().SetObjectElement(graph, 1, shared_bytes);
        vm.Model().SetObjectElement(graph, 2, shared_bytes);
        const auto graph_copy = roundtrip(graph);
        CHECK(vm.Model().GetObjectElement(graph_copy, 0) == graph_copy);
        CHECK(vm.Model().GetObjectElement(graph_copy, 1) == vm.Model().GetObjectElement(graph_copy, 2));
        CHECK(read(vm.Model().GetObjectElement(graph_copy, 1)) == read(bytes("00ff010203")));
        const auto uuid_original = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;",
            {VmValue::Ref(vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto restored_uuid = roundtrip(uuid_original, "aced00057372000e6a6176612e7574696c2e55554944bc9903f7986d852f0200024a000c6c65617374536967426974734a000b6d6f7374536967426974737870a76500a0c91e6bf6f81d4fae7dec11d0");
        CHECK(invoke(restored_uuid, "version", "()I", {}).AsInt() == 1);
        CHECK(invoke(restored_uuid, "timestamp", "()J", {}).AsLong() == INT64_C(130742845922168750));
        for (const auto& [owner, expected] : std::array{
                 std::pair{"Lfixture/SerializationResolve;", "resolved"},
                 std::pair{"Lfixture/SerializationReplace;", "replaced"}}) {
            const auto instance = vm.NewIntrinsicInstance(owner);
            direct(owner, "<init>", "()V", {VmValue::Ref(instance)});
            CHECK(vm.StringUtf8(roundtrip(instance)) == expected);
        }
        for (const auto& [descriptor, kind] : std::array{
                 std::pair{"[Z", runtime::JniPrimitiveKind::boolean},
                 std::pair{"[C", runtime::JniPrimitiveKind::character},
                 std::pair{"[S", runtime::JniPrimitiveKind::short_integer},
                 std::pair{"[I", runtime::JniPrimitiveKind::integer},
                 std::pair{"[J", runtime::JniPrimitiveKind::long_integer},
                 std::pair{"[F", runtime::JniPrimitiveKind::float_value},
                 std::pair{"[D", runtime::JniPrimitiveKind::double_value}}) {
            const auto array = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor(descriptor), kind, 2);
            vm.Model().SetPrimitiveElement(array, 0, 1);
            vm.Model().SetPrimitiveElement(array, 1, descriptor[1] == 'Z' ? 0 : UINT64_C(0x0102030405060708));
            const auto copy = roundtrip(array);
            CHECK(vm.Model().ObjectClass(copy) == linker.ResolveDescriptor(descriptor));
            CHECK(vm.Model().GetPrimitiveElement(copy, 0) == vm.Model().GetPrimitiveElement(array, 0));
            CHECK(vm.Model().GetPrimitiveElement(copy, 1) == vm.Model().GetPrimitiveElement(array, 1));
        }
        // Independent known answers, including binary input crossing many EVP blocks.
        for (const auto& vector : std::array{
            std::array{"MD5", "md5", "1.2.840.113549.2.5", "d41d8cd98f00b204e9800998ecf8427e", "900150983cd24fb0d6963f7d28e17f72", "3e2e51f419bcd80d9de0290be2de85ed"},
            std::array{"SHA-1", "SHA", "1.3.14.3.2.26", "da39a3ee5e6b4b0d3255bfef95601890afd80709", "a9993e364706816aba3e25717850c26c9cd0d89d", "4a2fb8a7e91751d887e656935210a7e4aeece658"},
            std::array{"SHA-256", "SHA256", "2.16.840.1.101.3.4.2.1", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "dd7e5c49d123e860c8bb7016bada722b5d0baa37ef8b19d5e270cf2a3000c31d"},
            std::array{"SHA-384", "SHA384", "2.16.840.1.101.3.4.2.2", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7", "5514fa38835e9e484a4ae89a248545ef400b8c91d121952eb243a42f65fd68d084c9ac680f6f4a7164d183b3997a4cf5"},
            std::array{"SHA-512", "SHA512", "2.16.840.1.101.3.4.2.3", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "2af1ca0a6a8f835b556c65eee6e7bbdda5e8251f5117351291a703f35f585e7aee225f6c21b07c9475e8ebbfbd210e120d97ddc1d8e81f10a97def1c31df9514"}}) {
            CAPTURE(vector[0]);
            const auto create_digest = [&](const char* algorithm) {
                return direct("Ljava/security/MessageDigest;", "getInstance",
                              "(Ljava/lang/String;)Ljava/security/MessageDigest;",
                              {VmValue::Ref(vm.NewStringUtf8(algorithm))}).ref;
            };
            const auto hash = create_digest(vector[0]);
            const auto hash_root = vm.ProtectReferences(std::array{hash});
            const auto length = static_cast<int>(std::string_view(vector[4]).size() / 2);
            CHECK(invoke(hash, "getDigestLength", "()I", {}).AsInt() == length);
            CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[3])));
            for (const auto* alias : {vector[1], vector[2]}) {
                CHECK(read(invoke(create_digest(alias), "digest", "([B)[B",
                                  {VmValue::Ref(bytes("616263"))}).ref) == read(bytes(vector[4])));
            }
            const auto provider_hash = direct("Ljava/security/MessageDigest;", "getInstance",
                "(Ljava/lang/String;Ljava/lang/String;)Ljava/security/MessageDigest;",
                {VmValue::Ref(vm.NewStringUtf8(vector[0])), VmValue::Ref(vm.NewStringUtf8("AndroidOpenSSL"))}).ref;
            CHECK(read(invoke(provider_hash, "digest", "([B)[B", {VmValue::Ref(bytes("616263"))}).ref) == read(bytes(vector[4])));
            invoke(hash, "update", "(B)V", {VmValue::Int(97)});
            const auto copy = invoke(hash, "clone", "()Ljava/lang/Object;", {}).ref;
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            invoke(hash, "reset", "()V", {});
            CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[3])));
            invoke(copy, "update", "([BII)V", {VmValue::Ref(bytes("00626300")), VmValue::Int(1), VmValue::Int(2)});
            const auto output = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte, length + 2);
            CHECK(invoke(copy, "digest", "([BII)I", {VmValue::Ref(output), VmValue::Int(1), VmValue::Int(length)}).AsInt() == length);
            CHECK(vm.Model().ReadByteRegion(output, 1, length) == read(bytes(vector[4])));
            CHECK(read(output).front() == std::byte{0});
            CHECK(read(output).back() == std::byte{0});
            for (int mode = 0; mode < 3; ++mode) {
                auto buffer = direct("Ljava/nio/ByteBuffer;", mode == 1 ? "allocateDirect" : "allocate",
                    "(I)Ljava/nio/ByteBuffer;", {VmValue::Int(5)}).ref;
                invoke(buffer, "put", "([B)Ljava/nio/ByteBuffer;", {VmValue::Ref(bytes("0061626300"))});
                invoke(buffer, "position", "(I)Ljava/nio/Buffer;", {VmValue::Int(1)});
                invoke(buffer, "limit", "(I)Ljava/nio/Buffer;", {VmValue::Int(4)});
                if (mode == 2) buffer = invoke(buffer, "asReadOnlyBuffer", "()Ljava/nio/ByteBuffer;", {}).ref;
                invoke(hash, "update", "(Ljava/nio/ByteBuffer;)V", {VmValue::Ref(buffer)});
                CHECK(invoke(buffer, "position", "()I", {}).AsInt() == 4);
                CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[4])));
            }
            // No total-message or single-update cap: native uses a bounded scratch buffer.
            std::vector<std::byte> binary(256 * 4097);
            for (std::size_t i = 0; i < binary.size(); ++i) binary[i] = static_cast<std::byte>(i & 255);
            const auto large = vm.Model().NewPrimitiveArray(linker.ResolveDescriptor("[B"), runtime::JniPrimitiveKind::byte, static_cast<runtime::JniSize>(binary.size()));
            vm.Model().WriteByteRegion(large, 0, binary);
            CHECK(read(invoke(hash, "digest", "([B)[B", {VmValue::Ref(large)}).ref) == read(bytes(vector[5])));
            for (const bool input : {true, false}) {
                const char* source_type = input ? "Ljava/io/ByteArrayInputStream;" : "Ljava/io/ByteArrayOutputStream;";
                const char* filter_type = input ? "Ljava/security/DigestInputStream;" : "Ljava/security/DigestOutputStream;";
                const auto source = vm.NewIntrinsicInstance(source_type);
                direct(source_type, "<init>", input ? "([B)V" : "()V",
                       input ? std::vector{VmValue::Ref(source), VmValue::Ref(bytes("61626378"))} : std::vector{VmValue::Ref(source)});
                const auto filter = vm.NewIntrinsicInstance(filter_type);
                const auto stream_roots = vm.ProtectReferences(std::array{source, filter});
                direct(filter_type, "<init>", input ? "(Ljava/io/InputStream;Ljava/security/MessageDigest;)V" : "(Ljava/io/OutputStream;Ljava/security/MessageDigest;)V",
                       {VmValue::Ref(filter), VmValue::Ref(source), VmValue::Ref(hash)});
                CHECK(invoke(filter, "getMessageDigest", "()Ljava/security/MessageDigest;", {}).ref == hash);
                if (input) {
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == 97);
                    CHECK(invoke(filter, "read", "([BII)I", {VmValue::Ref(bytes("0000")), VmValue::Int(0), VmValue::Int(2)}).AsInt() == 2);
                } else {
                    invoke(filter, "write", "(I)V", {VmValue::Int(97)});
                    invoke(filter, "write", "([BII)V", {VmValue::Ref(bytes("6263")), VmValue::Int(0), VmValue::Int(2)});
                }
                invoke(filter, "on", "(Z)V", {VmValue::Int(0)});
                if (input) {
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == 120);
                    CHECK(invoke(filter, "read", "()I", {}).AsInt() == -1);
                } else {
                    invoke(filter, "write", "(I)V", {VmValue::Int(120)});
                    CHECK(read(invoke(source, "toByteArray", "()[B", {}).ref) == read(bytes("61626378")));
                }
                CHECK(read(invoke(hash, "digest", "()[B", {}).ref) == read(bytes(vector[4])));
            }
            CHECK(vm.GuestNativeResourceCount() == 0);
        }
        const auto get_digest = linker.FindDirectMethod(linker.ResolveDescriptor("Ljava/security/MessageDigest;"), "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;");
        REQUIRE(get_digest);
        for (const auto* unavailable : {"SHA-224", "SHA3-256", "HmacSHA256", "unknown"})
            expect(vm.Call(*get_digest, std::array{VmValue::Ref(vm.NewStringUtf8(unavailable))}), "Ljava/security/NoSuchAlgorithmException;");
        for (const auto& [input, answer] : std::array{
            std::pair{"", "d41d8cd98f00b204e9800998ecf8427e"},
            std::pair{"61", "0cc175b9c0f1b6a831c399e269772661"},
            std::pair{"616263", "900150983cd24fb0d6963f7d28e17f72"}}) {
            CHECK(read(invoke(digest, "digest", "([B)[B", {VmValue::Ref(bytes(input))}).ref) == read(bytes(answer)));
            CHECK(vm.GuestNativeResourceCount() == 0);
        }
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("61"))});
        CHECK(vm.GuestNativeResourceCount() == 1);
        const auto clone = invoke(digest, "clone", "()Ljava/lang/Object;", {}).ref;
        const auto clone_roots = vm.ProtectReferences(std::array{clone});
        CHECK(vm.GuestNativeResourceCount() == 2);
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("6263"))});
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("900150983cd24fb0d6963f7d28e17f72")));
        CHECK(read(invoke(clone, "digest", "()[B", {}).ref) == read(bytes("0cc175b9c0f1b6a831c399e269772661")));
        invoke(digest, "update", "(B)V", {VmValue::Int(97)});
        invoke(digest, "reset", "()V", {});
        CHECK(vm.GuestNativeResourceCount() == 0);
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("d41d8cd98f00b204e9800998ecf8427e")));
        expect(raw(digest, "update", "([BII)V", {VmValue::Ref(bytes("61")), VmValue::Int(-1), VmValue::Int(1)}), "Ljava/lang/ArrayIndexOutOfBoundsException;");
        expect(raw(digest, "digest", "([BII)I", {VmValue::Ref(bytes("0000")), VmValue::Int(0), VmValue::Int(2)}), "Ljava/security/DigestException;");
        invoke(digest, "reset", "()V", {});
        // An abandoned shallow clone must not retire the original's shared token.
        invoke(digest, "update", "([B)V", {VmValue::Ref(bytes("61"))});
        static_cast<void>(vm.CloneObject(digest));
        static_cast<void>(vm.CollectGarbage("dvm108-shallow-clone"));
        CHECK(vm.GuestNativeResourceCount() == 1);
        CHECK(read(invoke(digest, "digest", "()[B", {}).ref) == read(bytes("0cc175b9c0f1b6a831c399e269772661")));
        const auto abandoned = direct("Ljava/security/MessageDigest;", "getInstance", "(Ljava/lang/String;)Ljava/security/MessageDigest;", {VmValue::Ref(vm.NewStringUtf8("MD5"))}).ref;
        invoke(abandoned, "update", "(B)V", {VmValue::Int(1)});
        CHECK(vm.GuestNativeResourceCount() == 1);
        static_cast<void>(vm.CollectGarbage("dvm108-digest-owner"));
        CHECK(vm.GuestNativeResourceCount() == 0);
        // Native entry points must reject opaque/stale tokens even when bypassing JCA.
        const auto native = linker.ResolveDescriptor("Lcom/android/org/conscrypt/NativeCrypto;");
        const auto lookup = *linker.FindDirectMethod(native, "EVP_get_digestbyname", "(Ljava/lang/String;)J");
        const auto size = *linker.FindDirectMethod(native, "EVP_MD_size", "(J)I");
        const auto init = *linker.FindDirectMethod(native, "EVP_DigestInit", "(J)J");
        const auto cleanup = *linker.FindDirectMethod(native, "EVP_MD_CTX_destroy", "(J)V");
        const auto copy_context = *linker.FindDirectMethod(native, "EVP_MD_CTX_copy", "(J)J");
        CHECK(vm.Call(lookup, std::array{VmValue::Ref(vm.NewStringUtf8("unknown"))}).value.AsLong() == 0);
        CHECK_THROWS_AS(static_cast<void>(vm.Call(size, std::array{VmValue::Long(0)})), VmJavaThrow);
        CHECK_THROWS_AS(static_cast<void>(vm.Call(init, std::array{VmValue::Long(99)})), VmJavaThrow);
        const auto algorithm = vm.Call(lookup, std::array{VmValue::Ref(vm.NewStringUtf8("sha512"))}).value.AsLong();
        const auto token = vm.Call(init, std::array{VmValue::Long(algorithm)}).value.AsLong();
        REQUIRE(token > 0);
        static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)}));
        CHECK_THROWS_AS(static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)})), VmJavaThrow);
        CHECK_THROWS_AS(static_cast<void>(vm.Call(copy_context, std::array{VmValue::Long(token)})), VmJavaThrow);
        const auto name_uuid = direct("Ljava/util/UUID;", "nameUUIDFromBytes", "([B)Ljava/util/UUID;", {VmValue::Ref(bytes("616263"))}).ref;
        CHECK(vm.StringUtf8(invoke(name_uuid, "toString", "()Ljava/lang/String;", {}).ref) == "90015098-3cd2-3fb0-9696-3f7d28e17f72");
        CHECK(invoke(name_uuid, "version", "()I", {}).AsInt() == 3);
        CHECK(invoke(name_uuid, "variant", "()I", {}).AsInt() == 2);
        const auto parsed = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(vm.NewStringUtf8("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"))}).ref;
        const auto uuid_roots = vm.ProtectReferences(std::array{parsed});
        CHECK(invoke(parsed, "version", "()I", {}).AsInt() == 1);
        CHECK(invoke(parsed, "timestamp", "()J", {}).AsLong() == INT64_C(130742845922168750));
        CHECK(invoke(parsed, "clockSequence", "()I", {}).AsInt() == 0x2765);
        CHECK(invoke(parsed, "node", "()J", {}).AsLong() == INT64_C(0x00a0c91e6bf6));
        const auto short_uuid = direct("Ljava/util/UUID;", "fromString", "(Ljava/lang/String;)Ljava/util/UUID;", {VmValue::Ref(vm.NewStringUtf8("1-1-1-1-1"))}).ref;
        CHECK(vm.StringUtf8(invoke(short_uuid, "toString", "()Ljava/lang/String;", {}).ref) == "00000001-0001-0001-0001-000000000001");
        std::set<std::string> randoms;
        for (int i = 0; i < 16; ++i) {
            const auto uuid = direct("Ljava/util/UUID;", "randomUUID", "()Ljava/util/UUID;", {}).ref;
            CHECK(invoke(uuid, "version", "()I", {}).AsInt() == 4);
            CHECK(invoke(uuid, "variant", "()I", {}).AsInt() == 2);
            randoms.insert(vm.StringUtf8(invoke(uuid, "toString", "()Ljava/lang/String;", {}).ref));
            expect(raw(uuid, "timestamp", "()J", {}), "Ljava/lang/UnsupportedOperationException;");
        }
        CHECK(randoms.size() == 16);
        CHECK(direct("Lfixture/UuidThreads;", "exercise", "()I", {}).AsInt() == 32);
        // Publish the real BootDex class to the JNI method registry.
        auto& bridge = app->DexVm();
        static_cast<void>(bridge.PublishLocal(vm.Model().ClassObject(linker.ResolveDescriptor("Ljava/util/UUID;"))));
        auto& process = app->NativeProcess();
        const auto uuid_class = process.Classes().FindClass("java/util/UUID");
        REQUIRE(uuid_class.has_value());
        const auto random_method = process.Classes().GetMethodId(*uuid_class, "randomUUID", "()Ljava/util/UUID;", true);
        REQUIRE(random_method.has_value());
        const auto jni_uuid = std::get<runtime::JniReference>(process.Invocations().InvokeStatic(1U, *uuid_class, *random_method, {}, runtime::JniArgumentSource::value_array));
        const auto actual = bridge.FromReference(jni_uuid);
        CHECK(vm.Model().ObjectClass(actual) == linker.ResolveDescriptor("Ljava/util/UUID;"));
        CHECK(invoke(actual, "version", "()I", {}).AsInt() == 4);
        const auto to_string = process.Classes().GetMethodId(*uuid_class, "toString", "()Ljava/lang/String;", false);
        REQUIRE(to_string.has_value());
        const auto jni_text = std::get<runtime::JniReference>(process.Invocations().InvokeVirtual(1U, jni_uuid, *uuid_class, *to_string, {}, runtime::JniArgumentSource::value_array));
        CHECK(vm.StringUtf8(bridge.FromReference(jni_text)) == vm.StringUtf8(invoke(actual, "toString", "()Ljava/lang/String;", {}).ref));
        invoke(digest, "update", "(B)V", {VmValue::Int(7)});
        vm.ReleaseGuestNativeResources(true);
        CHECK(vm.GuestNativeResourceCount() == 0);
        // The cleared field also makes a later Java reset/finalizer safe.
        invoke(digest, "reset", "()V", {});
        static_cast<void>(app->Stop());
    }
}
