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
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/session/android_app_process.h"

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
            ledger, nullptr);
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
    std::unique_ptr<ogplay::session::AndroidAppProcess> app;

    explicit OrchestratedApp(const std::string& activity,
                             const bool has_launcher = true,
                             const bool with_native = true) {
        const ogplay::runtime::BionicModuleSource system{
            "libc.so", libc};
        std::vector<ogplay::loader::ApkNativeLibrary> libraries;
        if (with_native) libraries.push_back(Library("liba.so", native_a));
        ogplay::session::AndroidAppProcessRequest request;
        request.manifest = AppManifest(activity, has_launcher);
        request.native_libraries = std::move(libraries);
        request.system_libraries = std::span{&system, 1};
        request.dex_bytes = ReadDexFixture("application.dex");
        request.context = context;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(200000);
        request.filesystem = &filesystem;
        request.ledger = &ledger;
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
    try {
        static_cast<void>(libraries.LoadLibrary("alias", 3U));
        FAIL("mismatched SONAME unexpectedly succeeded");
    } catch (const runtime::NativeLibraryLoadError& error) {
        CHECK(error.Reason() ==
              runtime::NativeLibraryLoadErrorReason::soname_mismatch);
    }
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
        nullptr);
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

    CHECK_THROWS_AS(static_cast<void>(lifecycle.Start()),
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
    CHECK(fixture.app->NativeLibraries() == nullptr);
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
