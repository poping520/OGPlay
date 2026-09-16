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
#include "ogplay/core/encoding.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/access_flags.h"
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
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/bus.h"
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
            ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
        const std::optional<std::string_view> working_directory = std::nullopt) {
        if (working_directory.has_value()) {
            filesystem.SetWorkingDirectory(*working_directory);
        }
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
        if (working_directory.has_value()) context->vfs = &filesystem;
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

TEST_CASE("DexVM bridge publishes the guest VFS working directory") {
    ApplicationProcess application{
        ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
        "/data/game"};

    CHECK(application.bridge->Vm().GetSystemProperty("user.dir") ==
          "/data/game");
}

[[nodiscard]] std::vector<std::uint8_t> ReadDexFixture(
    const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing DEX fixture: " + path);
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<std::byte> ReadPayloadBytes(
    const std::string& relative) {
    std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) +
                             "/data/android/19/" + relative,
                         std::ios::binary);
    if (!stream) throw std::runtime_error("missing payload file: " + relative);
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> result(raw.size());
    std::transform(raw.begin(), raw.end(), result.begin(),
                   [](char value) { return static_cast<std::byte>(value); });
    return result;
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
                             const std::string& launcher_alias = {},
                             const std::vector<ogplay::loader::AndroidManifestServiceComponent>& services = {},
                             const bool application_enabled = true,
                             const ogplay::runtime::dexvm::InterpreterBackend
                                 interpreter_backend =
                                     ogplay::runtime::dexvm::InterpreterBackend::switch_dispatch,
                             const std::string& application_class = "android.app.Application") {
        context->apk_bytes = {
            std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}};
        const ogplay::runtime::BionicModuleSource system{
            "libc.so", libc};
        std::vector<ogplay::loader::ApkNativeLibrary> libraries;
        if (with_native) libraries.push_back(Library("liba.so", native_a));
        ogplay::session::AndroidAppProcessRequest request;
        request.manifest = AppManifest(activity, has_launcher);
        request.manifest.application_class = application_class;
        request.manifest.service_components = services;
        request.manifest.application_enabled = application_enabled;
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
        request.platform.android_id = "0123456789abcdef";
        request.ledger = &ledger;
        request.logger = &logger;
        request.dexvm.interpreter.backend = interpreter_backend;
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

TEST_CASE("Build and SystemProperties JNI use the BootDex owner") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        auto& classes = fixture.session->Classes();
        auto& fields = fixture.session->Fields();
        const auto read = [&](JniReference ref) {
            return vm.StringUtf8(bridge.FromReference(ref));
        };
        for (const auto name : {"android/os/Build", "android/os/Build$VERSION",
                                "android/os/Build$VERSION_CODES",
                                "android/os/SystemProperties"}) {
            const auto owner = linker.ResolveDescriptor("L" + std::string(name) + ";");
            CHECK(linker.Class(owner).is_boot_dex);
            CHECK(classes.FindClass(name) == bridge.RegisteredClassIdentity(owner));
        }
        const auto build = *classes.FindClass("android/os/Build");
        const auto owner = linker.ResolveDescriptor("Landroid/os/Build;");
        CHECK(linker.Class(owner).clinit_state == ClinitState::uninitialized);
        REQUIRE(fields.EnsureClassInitialized(build, 1U));
        for (const auto name : {"BRAND", "CPU_ABI", "TAGS", "SERIAL"}) {
            const auto jni_field = classes.GetFieldId(build, name, "Ljava/lang/String;", true);
            const auto dex_field = linker.FindFieldRecursive(owner, name, "Ljava/lang/String;");
            REQUIRE(jni_field.has_value());
            REQUIRE(dex_field.has_value());
            const auto ref = std::get<JniReference>(fields.GetStatic(build, *jni_field, 1U));
            CHECK(bridge.FromReference(ref).Value() ==
                  linker.Class(owner).static_storage[linker.Field(*dex_field).slot]);
        }
        const auto version = *classes.FindClass("android/os/Build$VERSION");
        REQUIRE(fields.EnsureClassInitialized(version, 1U));
        const auto release = *classes.GetFieldId(version, "RELEASE", "Ljava/lang/String;", true);
        CHECK(read(std::get<JniReference>(fields.GetStatic(version, release, 1U))) == "4.4.4");
        const auto props = *classes.FindClass("android/os/SystemProperties");
        const auto get = *classes.GetMethodId(props, "get", "(Ljava/lang/String;)Ljava/lang/String;", true);
        CHECK(classes.ResolveMethod(get).declaration.implementation.starts_with("dexvm.m"));
        const auto props_owner = linker.ResolveDescriptor("Landroid/os/SystemProperties;");
        const auto dex_get = *linker.FindDirectMethod(props_owner, "get", "(Ljava/lang/String;)Ljava/lang/String;");
        CHECK(linker.Method(dex_get).kind != MethodKind::intrinsic);
        const auto key = vm.NewStringUtf8("ro.build.version.release");
        const std::array<JniValue, 1> args{bridge.PublishLocal(key)};
        const auto result = std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get, args, JniArgumentSource::value_array));
        CHECK(read(result) == "4.4.4");
        const std::array dex_args{VmValue::Ref(key)};
        const auto direct = vm.Call(dex_get, dex_args);
        REQUIRE_FALSE(direct.exception.IsValid());
        CHECK(read(result) == vm.StringUtf8(direct.value.ref));
        static_cast<void>(vm.CollectGarbage("build-jni"));
        CHECK(read(result) == "4.4.4");
        CHECK(read(std::get<JniReference>(fields.GetStatic(version, release, 1U))) == "4.4.4");
        const auto get_default = *classes.GetMethodId(props, "get",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", true);
        const std::array<JniValue, 2> missing{
            bridge.PublishLocal(vm.NewStringUtf8("missing.property")),
            bridge.PublishLocal(vm.NewStringUtf8("fallback"))};
        CHECK(read(std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get_default, missing, JniArgumentSource::value_array))) == "fallback");
        const auto get_int = *classes.GetMethodId(props, "getInt", "(Ljava/lang/String;I)I", true);
        const std::array<JniValue, 2> sdk_args{
            bridge.PublishLocal(vm.NewStringUtf8("ro.build.version.sdk")), JniInt{-1}};
        CHECK(std::get<JniInt>(fixture.session->Invocations().InvokeStatic(
            1U, props, get_int, sdk_args, JniArgumentSource::value_array)) == 19);
        // This validation belongs to the original Java method, before native_get.
        const std::array<JniValue, 1> too_long{
            bridge.PublishLocal(vm.NewStringUtf8(std::string(32, 'x')))};
        const auto failed = std::get<JniReference>(fixture.session->Invocations().InvokeStatic(
            1U, props, get, too_long, JniArgumentSource::value_array));
        CHECK(failed.IsNull());
        auto& environment = fixture.session->Environment();
        REQUIRE(environment.ExceptionCheck(1U));
        const auto exception_ref = environment.ExceptionOccurred(1U);
        environment.ExceptionClear(1U);
        const auto exception = bridge.FromReference(exception_ref);
        CHECK(linker.Class(bridge.Model().ObjectClass(exception)).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
    }
}

TEST_CASE("JNI framework services use VM methods objects and state") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& linker = bridge.Linker();
        auto& classes = fixture.session->Classes();
        auto& invocations = fixture.session->Invocations();
        fixture.context->device_id = "fixture-device";
        fixture.context->secure_settings["android_id"] = "0123456789abcdef";
        for (const auto name : {"android/content/Context", "android/content/ContextWrapper",
                               "android/content/ContentResolver", "android/app/Activity",
                               "android/telephony/TelephonyManager", "android/provider/Settings$Secure",
                               "android/os/Bundle", "android/media/AudioTrack"}) {
            const auto owner = linker.ResolveDescriptor("L" + std::string(name) + ";");
            const auto identity = classes.FindClass(name);
            REQUIRE(identity.has_value());
            CHECK(identity == bridge.RegisteredClassIdentity(owner));
            for (const auto id : linker.MethodsOf(owner)) {
                const auto& method = linker.Method(id);
                if (method.name == "<clinit>") continue;
                const auto jni_id = classes.GetMethodId(*identity, method.name, method.descriptor, method.is_static);
                REQUIRE(jni_id.has_value());
                CHECK(classes.ResolveMethod(*jni_id).declaration.implementation.starts_with("dexvm.m"));
            }
        }
        const auto call = [&](VmObjectRef receiver, const char* name, const char* desc,
                              std::vector<JniValue> args = {}) {
            const auto identity = *bridge.RegisteredClassIdentity(vm.Model().ObjectClass(receiver));
            const auto method = classes.GetMethodId(identity, name, desc, false);
            REQUIRE(method.has_value());
            return invocations.InvokeVirtual(1U, bridge.PublishLocal(receiver), identity, *method,
                                              args, JniArgumentSource::value_array);
        };
        const auto construct = [&](const char* desc, const char* signature,
                                   std::vector<JniValue> args = {}) {
            const auto object = vm.NewIntrinsicInstance(desc);
            const auto identity = *bridge.RegisteredClassIdentity(vm.Model().ObjectClass(object));
            const auto method = classes.GetMethodId(identity, "<init>", signature, false);
            REQUIRE(method.has_value());
            static_cast<void>(invocations.InvokeNonvirtual(1U, bridge.PublishLocal(object), identity,
                identity, *method, args, JniArgumentSource::value_array));
            REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
            return object;
        };
        const auto text = [&](const char* value) { return bridge.PublishLocal(vm.NewStringUtf8(value)); };
        const auto string_value = [&](JniValue value) { return vm.StringUtf8(bridge.FromReference(std::get<JniReference>(value))); };
        const auto activity = construct("Landroid/app/Activity;", "()V");
        const auto base = vm.NewIntrinsicInstance("Landroid/content/Context;");
        static_cast<void>(call(activity, "attachBaseContext", "(Landroid/content/Context;)V", {bridge.PublishLocal(base)}));
        REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        fixture.context->activity = activity;
        const auto legacy = classes.RegisterClass({"fixture/CurrentActivity", {},
            {{"get", "()Landroid/app/Activity;", "activity.current", true}}, {}});
        const auto current = *classes.GetMethodId(legacy, "get", "()Landroid/app/Activity;", true);
        CHECK(bridge.FromReference(std::get<JniReference>(invocations.InvokeStatic(
            1U, legacy, current, {}, JniArgumentSource::value_array))) == activity);
        const auto phone = std::get<JniReference>(call(activity, "getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;", {text("phone")}));
        CHECK(bridge.FromReference(phone).Value() == fixture.context->singletons.at("phone").Value());
        CHECK(string_value(call(bridge.FromReference(phone), "getDeviceId", "()Ljava/lang/String;")) == "fixture-device");
        const auto resolver = std::get<JniReference>(call(activity, "getContentResolver", "()Landroid/content/ContentResolver;"));
        CHECK(bridge.FromReference(resolver).Value() == fixture.context->singletons.at("content_resolver").Value());
        const auto secure = *classes.FindClass("android/provider/Settings$Secure");
        const auto get = *classes.GetMethodId(secure, "getString",
            "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;", true);
        const std::array<JniValue, 2> args{resolver, text("android_id")};
        CHECK(string_value(invocations.InvokeStatic(1U, secure, get, args, JniArgumentSource::value_array)) == "0123456789abcdef");
        const auto bundle = construct("Landroid/os/Bundle;", "()V");
        CHECK(linker.Class(vm.Model().ObjectClass(bundle)).is_boot_dex);
        static_cast<void>(call(bundle, "putInt", "(Ljava/lang/String;I)V", {text("answer"), JniInt{42}}));
        static_cast<void>(vm.CollectGarbage("jni-framework-state"));
        CHECK(std::get<JniInt>(call(bundle, "getInt", "(Ljava/lang/String;)I", {text("answer")})) == 42);
        CHECK(bridge.FromReference(std::get<JniReference>(call(activity, "getContentResolver",
            "()Landroid/content/ContentResolver;"))) == bridge.FromReference(resolver));
        fixture.context->activity = VmObjectRef{};
        CHECK(std::get<JniReference>(invocations.InvokeStatic(1U, legacy, current, {},
            JniArgumentSource::value_array)).IsNull());
    }
}

TEST_CASE("JNI AudioTrack uses the VM PCM player and lifecycle") {
    using namespace ogplay;
    using namespace runtime;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess fixture(backend);
        auto& bridge = *fixture.bridge;
        auto& vm = bridge.Vm();
        auto& classes = fixture.session->Classes();
        auto& invocations = fixture.session->Invocations();
        fixture.context->pcm_playback = &fixture.session->PcmPlayback();
        const auto owner = *classes.FindClass("android/media/AudioTrack");
        const auto host_object = AllocateJniHostObjectIdentity();
        fixture.session->Objects().Register(host_object, owner);
        const auto receiver = fixture.session->Environment().PublishLocalObject(1U, host_object);
        const auto constructor = *classes.GetMethodId(owner, "<init>", "(IIIIII)V", false);
        const auto get_minimum = *classes.GetMethodId(owner, "getMinBufferSize", "(III)I", true);
        const std::array<JniValue, 3> format{JniInt{8000}, JniInt{12}, JniInt{2}};
        const auto minimum = std::get<JniInt>(invocations.InvokeStatic(1U, owner, get_minimum,
            format, JniArgumentSource::value_array));
        REQUIRE(minimum > 0);
        const std::array<JniValue, 6> config{JniInt{3}, JniInt{8000}, JniInt{12}, JniInt{2}, minimum, JniInt{1}};
        static_cast<void>(invocations.InvokeNonvirtual(1U, receiver, owner, owner, constructor,
            config, JniArgumentSource::value_array));
        REQUIRE_FALSE(fixture.session->Environment().ExceptionCheck(1U));
        const auto track = bridge.FromReference(receiver);
        REQUIRE(fixture.context->audio_tracks.contains(track.Value()));
        const auto player = fixture.context->audio_tracks.at(track.Value()).player;
        const auto call = [&](const char* name, const char* desc, std::vector<JniValue> args = {}) {
            const auto method = classes.GetMethodId(owner, name, desc, false);
            REQUIRE(method.has_value());
            return invocations.InvokeVirtual(1U, receiver, owner, *method, args, JniArgumentSource::value_array);
        };
        auto& arrays = fixture.session->Arrays();
        const auto bytes = arrays.New(JniPrimitiveKind::byte, 8);
        arrays.SetRegion(bytes, 0, JniPrimitiveArrayData{std::vector<JniByte>{0, 1, 0, 2, 0, 3, 0, 4}});
        const auto pcm = fixture.session->Environment().PublishLocalObject(1U, bytes);
        CHECK(std::get<JniInt>(call("write", "([BII)I", {pcm, JniInt{0}, JniInt{8}})) == 8);
        CHECK(fixture.session->PcmPlayback().QueuedBytes(player) == 8U);
        static_cast<void>(call("play", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 3);
        std::array<std::int16_t, 4> mixed{};
        static_cast<void>(fixture.session->PcmPlayback().MixAdditiveStereoPcm16(mixed, 8000U));
        CHECK(mixed[0] != 0);
        CHECK(mixed[1] != 0);
        static_cast<void>(call("pause", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 2);
        static_cast<void>(call("stop", "()V"));
        CHECK(std::get<JniInt>(call("getPlayState", "()I")) == 1);
        static_cast<void>(vm.CollectGarbage("jni-audio-owner"));
        CHECK(fixture.context->audio_tracks.contains(track.Value()));
        static_cast<void>(call("release", "()V"));
        CHECK(std::get<JniInt>(call("getState", "()I")) == 0);
    }
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

TEST_CASE("DVM-141 Java Map array elements become callable JNI receivers and GC roots") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& model = f.bridge->Model();
        auto& linker = f.bridge->Linker();
        auto& session = *f.session;
        memory::AddressSpace memory;
        memory::CheckedMemoryBus bus(memory);
        cpu::InterpreterCpu cpu(bus);
        runtime::GuestJniAbi abi(memory);
        runtime::JniGuestCallDispatcher dispatcher(f.ledger);
        runtime::JniJavaVm java_vm(session.Environment());
        runtime::JniGuestBindingContext context{session.Environment(), session.Classes(),
            session.Invocations(), session.Fields(), session.Strings(), session.Arrays(),
            java_vm, session.Objects(), memory};
        runtime::BindJniGuestSlots(dispatcher, context);
        dispatcher.Seal();
        const auto call = [&](const char* name, std::uint32_t r1, std::uint32_t r2 = 0) {
            const auto slot = runtime::FindJniSlot(name);
            REQUIRE(slot.has_value());
            const auto target = bus.Read32(runtime::kJniGuestEnvironmentTable.Add(slot->Value() * 4U));
            cpu::A32State state;
            state.SetThreadId(1);
            state.SetState(cpu::ExecutionState::thumb);
            state.SetRegister(cpu::CoreRegister::pc, target & ~1U);
            state.SetRegister(cpu::CoreRegister::r0, abi.Environment().Value());
            state.SetRegister(cpu::CoreRegister::r1, r1);
            state.SetRegister(cpu::CoreRegister::r2, r2);
            cpu.SetState(state);
            const auto stopped = cpu.Run(1);
            REQUIRE(dispatcher.Handle(cpu, stopped));
            return cpu.GetState().Register(cpu::CoreRegister::r0);
        };
        const auto map_class = linker.ResolveDescriptor("Ljava/util/HashMap;");
        REQUIRE_FALSE(vm.EnsureClassInitialized(map_class).exception.IsValid());
        const auto map = vm.NewIntrinsicInstance("Ljava/util/HashMap;");
        const auto ctor = linker.FindDirectMethod(map_class, "<init>", "()V");
        REQUIRE(ctor.has_value());
        REQUIRE_FALSE(vm.Call(*ctor, std::array{VmValue::Ref(map)}).exception.IsValid());
        const auto array = model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                               linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        model.SetObjectElement(array, 0, map);
        const auto array_ref = f.bridge->PublishLocal(array);
        const auto identity = model.ToIdentity(map);
        CHECK_THROWS_AS(static_cast<void>(session.Objects().ClassOf(identity)), runtime::JniGuestBindingError);
        const auto element = call("GetObjectArrayElement", array_ref.Value());
        const auto second = call("GetObjectArrayElement", array_ref.Value());
        model.SetObjectElement(array, 0, VmObjectRef{});
        static_cast<void>(vm.CollectGarbage("dvm141-element-local-root"));
        const auto klass = session.Objects().ClassOf(identity);
        CHECK(klass == *f.bridge->RegisteredClassIdentity(map_class));
        const auto method = session.Classes().GetMethodId(klass, "entrySet", "()Ljava/util/Set;", false);
        REQUIRE(method.has_value());
        const auto entries = call("CallObjectMethodV", element, method->Value());
        CHECK(entries != 0);
        CHECK(call("GetObjectClass", element) != 0);
        static_cast<void>(call("DeleteLocalRef", element));
        static_cast<void>(call("DeleteLocalRef", second));
        static_cast<void>(call("DeleteLocalRef", entries));
        // Replace the interpreter's retained last object return with a scalar.
        const auto size_slot = linker.FindVtableIndex(map_class, "size", "()I");
        REQUIRE(size_slot.has_value());
        REQUIRE_FALSE(vm.Call(linker.Class(map_class).vtable[*size_slot],
                              std::array{VmValue::Ref(map)}).exception.IsValid());
        static_cast<void>(vm.CollectGarbage("dvm141-released-element"));
        CHECK_THROWS_AS(static_cast<void>(session.Objects().ClassOf(identity)), runtime::JniGuestBindingError);
    }
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

TEST_CASE("DVM-112 BootDex ServiceConnection links and the shared bridge takes the absent branch") {
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        f.context->service_inventory_known = true;
        auto& vm = f.bridge->Vm();
        auto& linker = f.bridge->Linker();
        const auto contract = linker.ResolveDescriptor("Landroid/content/ServiceConnection;");
        CHECK_FALSE(linker.Class(contract).is_intrinsic);
        CHECK(linker.Class(contract).is_interface);
        const auto class_flags = kAccPublic | kAccAbstract | kAccInterface;
        CHECK((linker.Class(contract).access_flags & class_flags) == class_flags);
        REQUIRE(linker.Class(contract).own_virtual_methods.size() == 2);
        for (auto method : linker.Class(contract).own_virtual_methods) {
            CHECK((linker.Method(method).access_flags & (kAccPublic | kAccAbstract)) ==
                  (kAccPublic | kAccAbstract));
        }
        auto direct = [&](const char* owner, const char* name, const char* signature, std::vector<VmValue> args) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, signature);
            REQUIRE(method);
            const auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto base = vm.NewIntrinsicInstance("Landroid/content/Context;");
        const auto intent = vm.NewIntrinsicInstance("Landroid/content/Intent;");
        const auto probe = vm.NewIntrinsicInstance("Lfixture/ServiceProbe;");
        const auto roots = vm.ProtectReferences(std::array{base, intent, probe});
        direct("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(intent), VmValue::Ref(vm.NewStringUtf8("example.ABSENT"))});
        direct("Lfixture/ServiceProbe;", "<init>", "()V", {VmValue::Ref(probe)});
        const auto probe_class = vm.Model().ObjectClass(probe);
        CHECK(linker.IsAssignable(contract, probe_class));
        const auto contract_identity = f.bridge->RegisteredClassIdentity(contract);
        const auto probe_identity = f.bridge->RegisteredClassIdentity(probe_class);
        REQUIRE(contract_identity.has_value());
        REQUIRE(probe_identity.has_value());
        CHECK(f.session->Classes().GetInterfaces(*probe_identity) ==
              std::vector{*contract_identity});
        CHECK(f.session->Classes().IsAssignableFrom(*contract_identity,
                                                    *probe_identity));
        CHECK(direct("Lfixture/ServiceProbe;", "discover",
            "(Landroid/content/Context;Landroid/content/Intent;)I",
            {VmValue::Ref(base), VmValue::Ref(intent)}).AsInt() == 1);
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(vm.CollectGarbage());
        CHECK(direct("Lfixture/ServiceProbe;", "exerciseCallbacks", "(Landroid/content/ServiceConnection;)I",
            {VmValue::Ref(probe)}).AsInt() == 2);
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
    CHECK(vm.MarkReachable().IsMarked(list));

    vm.SetGcIntegration({});
    const auto flags_index = linker.FindVtableIndex(
        intent_class, "getFlags", "()I");
    REQUIRE(flags_index.has_value());
    outcome = vm.Call(linker.Class(intent_class).vtable[*flags_index],
                      std::vector{runtime::dexvm::VmValue::Ref(intent)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    static_cast<void>(vm.CollectGarbage("intent-list-extra-owner-sweep"));
    CHECK_FALSE(vm.MarkReachable().IsMarked(intent));
    CHECK_FALSE(vm.MarkReachable().IsMarked(list));
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
            "Application onCreate failed: uncaught Java exception\n"
            "  exception: Ljava/lang/RuntimeException;\n"
            "  message: application failed\n"
            "  stack trace:\n"
            "    at Lfixture/ThrowingApplication;.onCreate (pc 7)"),
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
    CHECK(fixture.app->Context()->secure_settings.at("android_id") ==
          "0123456789abcdef");
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

TEST_CASE("DVM-125 lifecycle owns coherent Activity and View window focus") {
    using namespace ogplay;
    using runtime::dexvm::InterpreterBackend;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        OrchestratedApp fixture("fixture.WindowFocusActivity", true, false,
                                {}, {}, true, backend);
        fixture.app->StartApplication();
        const auto started = fixture.app->StartLauncherActivity();
        CHECK(started.state == session::LifecycleRunState::running);
        const auto old_activity = fixture.context->activity;
        const auto call_activity = [&](const runtime::dexvm::VmObjectRef receiver,
                                       const char* name,
                                       const char* descriptor) {
            auto& bridge = fixture.app->DexVm();
            const auto java_class = bridge.Vm().Model().ObjectClass(receiver);
            const auto index = bridge.Linker().FindVtableIndex(
                java_class, name, descriptor);
            REQUIRE(index.has_value());
            const auto outcome = bridge.Vm().Call(
                bridge.Linker().Class(java_class).vtable[*index],
                std::vector{runtime::dexvm::VmValue::Ref(receiver)});
            REQUIRE_FALSE(outcome.exception.IsValid());
            return outcome.value;
        };
        const auto call_static_void = [&](const char* owner,
                                          const char* name) {
            auto& bridge = fixture.app->DexVm();
            const auto java_class = bridge.Linker().FindClass(owner);
            REQUIRE(java_class.has_value());
            const auto method = bridge.Linker().FindDirectMethod(
                *java_class, name, "()V");
            REQUIRE(method.has_value());
            const auto outcome = bridge.Vm().Call(*method, {});
            REQUIRE_FALSE(outcome.exception.IsValid());
        };

        // onResume runs before the first post-traversal focus message.
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "hasCurrentFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "detachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "decorHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 0);

        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "hasCurrentFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "detachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "decorHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getQueried") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getQueried") == 1);
        call_static_void("Lfixture/WindowFocusActivity;", "detachAttached");
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);
        call_static_void("Lfixture/WindowFocusActivity;", "attachAttached");
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "attachedHasFocus") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 1);

        static_cast<void>(fixture.app->ActivityLifecycle().Suspend());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getLast") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getQueried") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getQueried") == 0);

        static_cast<void>(fixture.app->ActivityLifecycle().Resume());
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 3);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 3);

        static_cast<void>(call_activity(old_activity, "switchActivity", "()V"));
        static_cast<void>(fixture.app->ActivityLifecycle().StepFrame());
        CHECK(fixture.context->activity != old_activity);
        CHECK(call_activity(old_activity, "hasWindowFocus", "()Z").AsInt() == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusActivity;",
                                    "getEvents") == 4);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getResumeFocus") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getEvents") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getQueried") == 1);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 5);

        const auto stopped = fixture.app->Stop();
        CHECK(stopped.state == session::LifecycleRunState::stopped);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getEvents") == 2);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusSecondActivity;",
                                    "getQueried") == 0);
        CHECK(fixture.CallStaticInt("Lfixture/WindowFocusView;",
                                    "getEvents") == 6);
    }
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
    CHECK(cli.find("APK guest execution failed") == std::string::npos);

    const auto gui = read_source("/src/frontend/gui/import.cpp");
    CHECK(gui.find("SelectApkCompatibilityProfile") != std::string::npos);
    CHECK(gui.find("MatchApkTitleProfile") == std::string::npos);
}

TEST_CASE("DVM-126 String.format delegates Locale formatting to API19 Formatter") {
    using namespace ogplay;
    using namespace runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
        runtime::VirtualFileSystem filesystem;
        core::CapabilityLedger ledger;
        core::Logger logger;
        std::vector<std::vector<std::byte>> contents;
        std::vector<runtime::BionicModuleSource> libraries;
        for (const auto name : {
                 "libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                 "libcrypto.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                 "libstlport.so", "libogplay_jni.so"}) {
            std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) +
                                     "/data/android/19/lib/" + name,
                                 std::ios::binary);
            REQUIRE_MESSAGE(stream.good(), name);
            std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
            contents.emplace_back(data.size());
            std::transform(data.begin(), data.end(), contents.back().begin(),
                           [](const char value) {
                               return static_cast<std::byte>(value);
                           });
            libraries.push_back({name, contents.back()});
        }
        auto context = std::make_shared<runtime::DexVmAndroidContext>();
        context->apk_bytes = {
            std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
        session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = libraries;
        request.dex_bytes = ReadDexFixture("cipher.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.boot_dex_bytes = test::ReadBootDex();
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.surface_width = 64;
        request.surface_height = 36;
        request.maximum_ticks_per_call = UINT64_C(100000000);
#if defined(_WIN32)
        request.backend = {gles::AngleRenderer::d3d11,
                           gles::AngleDevice::hardware};
#elif defined(__APPLE__)
        request.backend = {gles::AngleRenderer::metal,
                           gles::AngleDevice::hardware};
#else
        request.backend = {gles::AngleRenderer::vulkan,
                           gles::AngleDevice::hardware};
#endif
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        auto app = session::AndroidAppProcess::Create(std::move(request));
        auto& vm = app->DexVm().Vm();
        auto& linker = vm.Linker();
        const auto direct = [&](const char* owner, const char* name,
                                const char* descriptor,
                                std::vector<VmValue> arguments) {
            const auto type = linker.ResolveDescriptor(owner);
            const auto method = linker.FindDirectMethod(
                type, name, descriptor);
            REQUIRE_MESSAGE(method.has_value(), name);
            return vm.Call(*method, arguments);
        };
        const auto require_string = [&](const VmCallOutcome& outcome) {
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return vm.StringUtf8(outcome.value.ref);
        };
        const auto expect_exception = [&](const VmCallOutcome& outcome,
                                          const char* descriptor) {
            INFO(outcome.exception_message);
            REQUIRE(outcome.exception.IsValid());
            CHECK(linker.Class(outcome.exception_class).descriptor == descriptor);
        };

        const auto formatter = linker.ResolveDescriptor("Ljava/util/Formatter;");
        CHECK(linker.Class(formatter).is_boot_dex);
        const auto formatter_format = linker.FindVtableIndex(
            formatter, "format",
            "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/util/Formatter;");
        REQUIRE(formatter_format.has_value());
        CHECK(linker.Method(linker.Class(formatter).vtable[*formatter_format]).kind ==
              MethodKind::interpreted);
        const auto string_class = linker.ResolveDescriptor("Ljava/lang/String;");
        const auto locale_overload = linker.FindDirectMethod(
            string_class, "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;");
        REQUIRE(locale_overload.has_value());
        CHECK(linker.Method(*locale_overload).kind == MethodKind::intrinsic);

        const auto locale_class = linker.ResolveDescriptor("Ljava/util/Locale;");
        const auto initialized = vm.EnsureClassInitialized(locale_class);
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
        const auto us_field = linker.FindFieldRecursive(
            locale_class, "US", "Ljava/util/Locale;");
        REQUIRE(us_field.has_value());
        const auto& linked_us = linker.Field(*us_field);
        const auto us = VmObjectRef(
            linker.Class(linked_us.owner).static_storage[linked_us.slot]);
        REQUIRE(us.IsValid());
        const auto calendar = direct(
            "Ljava/util/Calendar;", "getInstance",
            "()Ljava/util/Calendar;", {}).value.ref;
        REQUIRE(calendar.IsValid());
        const auto object_class = linker.ResolveDescriptor("Ljava/lang/Object;");
        const auto object_array = linker.ResolveDescriptor("[Ljava/lang/Object;");
        const auto zone_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 1);
        vm.Model().SetObjectElement(zone_arguments, 0, calendar);
        const auto zone_roots = vm.ProtectReferences(
            std::array{us, calendar, zone_arguments});
        const auto zone_result = direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%tZ")),
             VmValue::Ref(zone_arguments)});
        CHECK(require_string(zone_result) == "GMT");
        const auto result_roots = vm.ProtectReferences(
            std::array{zone_result.value.ref});
        static_cast<void>(vm.CollectGarbage("formatter-result"));
        CHECK(vm.StringUtf8(zone_result.value.ref) == "GMT");

        const auto integer = vm.NewIntrinsicInstance("Ljava/lang/Integer;");
        vm.Model().InstanceSlots(integer)[0] = {7U, SlotTag::cat1};
        const auto basic_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 2);
        vm.Model().SetObjectElement(basic_arguments, 0, integer);
        vm.Model().SetObjectElement(
            basic_arguments, 1, vm.NewStringUtf8("ok"));
        const auto basic_roots = vm.ProtectReferences(
            std::array{integer, basic_arguments});
        CHECK(require_string(direct(
                  "Ljava/lang/String;", "format",
                  "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
                  {VmValue::Ref(VmObjectRef{}),
                   VmValue::Ref(vm.NewStringUtf8("%02d-%s-%%")),
                   VmValue::Ref(basic_arguments)})) == "07-ok-%");
        CHECK(require_string(direct(
                  "Ljava/lang/String;", "format",
                  "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
                  {VmValue::Ref(vm.NewStringUtf8("%02d")),
                   VmValue::Ref(basic_arguments)})) == "07");

        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(VmObjectRef{}),
             VmValue::Ref(VmObjectRef{})}),
            "Ljava/lang/NullPointerException;");
        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%q")),
             VmValue::Ref(VmObjectRef{})}),
            "Ljava/util/UnknownFormatConversionException;");
        const auto empty_arguments = vm.Model().NewObjectArray(
            object_array, object_class, 0);
        expect_exception(direct(
            "Ljava/lang/String;", "format",
            "(Ljava/util/Locale;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/String;",
            {VmValue::Ref(us), VmValue::Ref(vm.NewStringUtf8("%d")),
             VmValue::Ref(empty_arguments)}),
            "Ljava/util/MissingFormatArgumentException;");
    }
}

TEST_CASE("DVM-105/169 AES and HmacSHA1 use BootDex and real guest libcrypto") {
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
                                "libcrypto.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                                "libstlport.so", "libogplay_jni.so"}) {
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
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
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
        const auto secure_random = direct(
            "Ljava/security/SecureRandom;", "getInstance",
            "(Ljava/lang/String;)Ljava/security/SecureRandom;",
            {VmValue::Ref(vm.NewStringUtf8("SHA1PRNG"))})
                                       .ref;
        const auto random_output = bytes("00000000000000000000000000000000");
        const auto random_roots = vm.ProtectReferences(std::array{secure_random, random_output});
        invoke(secure_random, "setSeed", "([B)V", {VmValue::Ref(bytes("01020304"))});
        invoke(secure_random, "nextBytes", "([B)V", {VmValue::Ref(random_output)});
        CHECK(vm.Model().ReadByteRegion(random_output, 0, 16) !=
              std::vector<std::byte>(16, std::byte{0}));

        const auto key_generator = direct(
            "Ljavax/crypto/KeyGenerator;", "getInstance",
            "(Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
            {VmValue::Ref(vm.NewStringUtf8("AES"))})
                                       .ref;
        const auto generator_roots = vm.ProtectReferences(std::array{key_generator});
        for (const auto bits : {128, 192, 256}) {
            invoke(key_generator, "init", "(ILjava/security/SecureRandom;)V",
                   {VmValue::Int(bits), VmValue::Ref(secure_random)});
            const auto generated_key =
                invoke(key_generator, "generateKey", "()Ljavax/crypto/SecretKey;", {}).ref;
            const auto generated_roots = vm.ProtectReferences(std::array{generated_key});
            const auto generated_bytes = invoke(generated_key, "getEncoded", "()[B", {}).ref;
            CHECK(vm.Model().ArrayLength(generated_bytes) == bits / 8);
        }

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
        const auto initialized_once = vm.EnsureClassInitialized(native);
        REQUIRE_MESSAGE(!initialized_once.exception.IsValid(),
                        initialized_once.exception_message);
        const auto initialized_twice = vm.EnsureClassInitialized(native);
        REQUIRE_MESSAGE(!initialized_twice.exception.IsValid(),
                        initialized_twice.exception_message);
        const auto supported_protocols = direct(
            "Lcom/android/org/conscrypt/NativeCrypto;", "getSupportedProtocols",
            "()[Ljava/lang/String;", {}).ref;
        CHECK(vm.Model().ArrayLength(supported_protocols) == 4);
        const auto unsupported_engine = *linker.FindDirectMethod(
            native, "ENGINE_load_dynamic", "()V");
        CHECK_THROWS_AS(static_cast<void>(vm.Call(unsupported_engine, {})), VmJavaThrow);
        const auto allocate = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_new", "()J");
        const auto cleanup = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_cleanup", "(J)V");
        const auto size = *linker.FindDirectMethod(native, "EVP_CIPHER_CTX_block_size", "(J)I");
        CHECK(linker.Method(allocate).kind == MethodKind::native);
        CHECK_FALSE(static_cast<bool>(linker.Method(allocate).implementation));
        const auto token = vm.Call(allocate, {}).value.AsLong();
        CHECK_THROWS_AS(static_cast<void>(vm.Call(
                            size, std::array{VmValue::Long(token)})),
                        VmJavaThrow);
        static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(token)}));
        CHECK_THROWS_AS(static_cast<void>(vm.Call(
                            cleanup, std::array{VmValue::Long(token)})),
                        VmJavaThrow);
        CHECK_THROWS_AS(static_cast<void>(vm.Call(
                            size, std::array{VmValue::Long(0x123456789LL)})),
                        VmJavaThrow);
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
        const auto iso = direct("Llibcore/icu/ICU;", "getISOLanguagesNative",
                                "()[Ljava/lang/String;", {}).ref;
        CHECK(vm.Model().ArrayLength(iso) == 559);
        const auto countries = direct("Llibcore/icu/ICU;", "getISOCountriesNative",
                                      "()[Ljava/lang/String;", {}).ref;
        CHECK(vm.Model().ArrayLength(countries) == 249);
        const auto locale = vm.NewStringUtf8("en_US");
        const auto usd = vm.NewStringUtf8("USD");
        const auto icu_roots = vm.ProtectReferences(std::array{locale, usd});
        CHECK_FALSE(vm.StringUtf8(direct(
            "Llibcore/icu/ICU;", "getBestDateTimePatternNative",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(vm.NewStringUtf8("yMd")), VmValue::Ref(locale)}).ref).empty());
        CHECK(vm.StringUtf8(direct(
            "Llibcore/icu/ICU;", "getCurrencyCode",
            "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(vm.NewStringUtf8("US"))}).ref) == "USD");
        CHECK_FALSE(vm.StringUtf8(direct(
            "Llibcore/icu/ICU;", "getCurrencyDisplayName",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(locale), VmValue::Ref(usd)}).ref).empty());
        CHECK(direct("Llibcore/icu/ICU;", "getCurrencyFractionDigits",
                     "(Ljava/lang/String;)I", {VmValue::Ref(usd)}).AsInt() == 2);
        CHECK_FALSE(vm.StringUtf8(direct(
            "Llibcore/icu/ICU;", "getCurrencySymbol",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(locale), VmValue::Ref(usd)}).ref).empty());
        const auto unknown_currency = vm.NewStringUtf8("ZZZ");
        const auto unknown_roots = vm.ProtectReferences(std::array{unknown_currency});
        CHECK_FALSE(direct(
            "Llibcore/icu/ICU;", "getCurrencySymbol",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(locale), VmValue::Ref(unknown_currency)}).ref.IsValid());
        CHECK(vm.StringUtf8(direct(
            "Llibcore/icu/ICU;", "getCurrencyDisplayName",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(locale), VmValue::Ref(unknown_currency)}).ref) == "ZZZ");
        CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "upperSharpS",
                                   "()Ljava/lang/String;", {}).ref) == "SS");
        CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "turkishUpperI",
                                   "()Ljava/lang/String;", {}).ref) == "İ");
        CHECK(vm.StringUtf8(direct("Lfixture/IcuRegression;", "zhCurrencyCode",
                                   "()Ljava/lang/String;", {}).ref) == "XXX");
        CHECK(direct("Lfixture/IcuRegression;", "fractionFieldPosition", "()I", {})
                  .AsInt() == 35);
        CHECK(direct("Lfixture/IcuRegression;", "groupingAttributeCount", "()I", {})
                  .AsInt() == 2);
        CHECK(direct("Lfixture/IcuRegression;", "parsePositionSemantics", "()I", {})
                  .AsInt() == 1);
        const auto string_class = linker.ResolveDescriptor("Ljava/lang/String;");
        const auto string_array_class = linker.ResolveDescriptor("[Ljava/lang/String;");
        const auto rows_class = linker.ResolveDescriptor("[[Ljava/lang/String;");
        const auto row = vm.Model().NewObjectArray(string_array_class, string_class, 5);
        vm.Model().SetObjectElement(row, 0, vm.NewStringUtf8("GMT"));
        const auto rows = vm.Model().NewObjectArray(rows_class, string_array_class, 1);
        vm.Model().SetObjectElement(rows, 0, row);
        const auto zone_roots = vm.ProtectReferences(std::array{row, rows});
        static_cast<void>(direct(
            "Llibcore/icu/TimeZoneNames;", "fillZoneStrings",
            "(Ljava/lang/String;[[Ljava/lang/String;)V",
            {VmValue::Ref(locale), VmValue::Ref(rows)}));
        CHECK(vm.Model().GetObjectElement(row, 1).IsValid());
        const auto decimal = vm.NewIntrinsicInstance("Ljava/text/DecimalFormat;");
        const auto decimal_roots = vm.ProtectReferences(std::array{decimal});
        static_cast<void>(direct(
            "Ljava/text/DecimalFormat;", "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(decimal), VmValue::Ref(vm.NewStringUtf8("0.00"))}));
        CHECK(vm.StringUtf8(invoke(decimal, "format", "(J)Ljava/lang/String;",
                                   {VmValue::Long(12)}).ref) == "12.00");
        static_cast<void>(invoke(decimal, "applyPattern", "(Ljava/lang/String;)V",
                                 {VmValue::Ref(vm.NewStringUtf8("000"))}));
        CHECK(vm.StringUtf8(invoke(decimal, "toPattern", "()Ljava/lang/String;", {}).ref) ==
              "#000");
        CHECK(vm.StringUtf8(invoke(decimal, "format", "(J)Ljava/lang/String;",
                                   {VmValue::Long(12)}).ref) == "012");
        const auto parsed = invoke(decimal, "parse",
                                   "(Ljava/lang/String;)Ljava/lang/Number;",
                                   {VmValue::Ref(vm.NewStringUtf8("034"))}).ref;
        CHECK(invoke(parsed, "longValue", "()J", {}).AsLong() == 34);
        const auto decimal_fraction = vm.NewIntrinsicInstance("Ljava/text/DecimalFormat;");
        const auto fraction_roots = vm.ProtectReferences(std::array{decimal_fraction});
        static_cast<void>(direct(
            "Ljava/text/DecimalFormat;", "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(decimal_fraction), VmValue::Ref(vm.NewStringUtf8("0.0"))}));
        const auto parsed_fraction = invoke(
            decimal_fraction, "parse", "(Ljava/lang/String;)Ljava/lang/Number;",
            {VmValue::Ref(vm.NewStringUtf8("1.5"))}).ref;
        CHECK(linker.Class(vm.Model().ObjectClass(parsed_fraction)).descriptor ==
              "Ljava/lang/Double;");
        CHECK(invoke(parsed_fraction, "doubleValue", "()D", {}).AsDouble() ==
              doctest::Approx(1.5));
        const auto cloned = invoke(decimal, "clone", "()Ljava/lang/Object;", {}).ref;
        const auto clone_roots = vm.ProtectReferences(std::array{cloned});
        CHECK(vm.StringUtf8(invoke(cloned, "format", "(J)Ljava/lang/String;",
                                   {VmValue::Long(7)}).ref) == "007");
        const auto date_format = vm.NewIntrinsicInstance("Ljava/text/SimpleDateFormat;");
        const auto date = vm.NewIntrinsicInstance("Ljava/util/Date;");
        const auto date_roots = vm.ProtectReferences(std::array{date_format, date});
        static_cast<void>(direct(
            "Ljava/text/SimpleDateFormat;", "<init>", "(Ljava/lang/String;)V",
            {VmValue::Ref(date_format), VmValue::Ref(vm.NewStringUtf8("yyyy-MM-dd"))}));
        static_cast<void>(direct("Ljava/util/Date;", "<init>", "(J)V",
                                 {VmValue::Ref(date), VmValue::Long(0)}));
        CHECK(vm.StringUtf8(invoke(date_format, "format",
                                   "(Ljava/util/Date;)Ljava/lang/String;",
                                   {VmValue::Ref(date)}).ref) == "1970-01-01");
        const auto time_zone = linker.ResolveDescriptor("Ljava/util/TimeZone;");
        const auto get_time_zone = linker.FindDirectMethod(
            time_zone, "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;");
        REQUIRE(get_time_zone.has_value());
        expect_exception(vm.Call(*get_time_zone,
                                 std::array{VmValue::Ref(vm.NewStringUtf8("PST"))}),
                         "Ljava/lang/UnsupportedOperationException;");
        const auto hmac_key_bytes = bytes(
            "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
        const auto hmac_key = vm.NewIntrinsicInstance(
            "Ljavax/crypto/spec/SecretKeySpec;");
        const auto hmac_key_roots = vm.ProtectReferences(
            std::array{hmac_key_bytes, hmac_key});
        direct("Ljavax/crypto/spec/SecretKeySpec;", "<init>",
               "([BLjava/lang/String;)V",
               {VmValue::Ref(hmac_key), VmValue::Ref(hmac_key_bytes),
                VmValue::Ref(vm.NewStringUtf8("HmacSHA1"))});
        const auto mac = direct(
            "Ljavax/crypto/Mac;", "getInstance",
            "(Ljava/lang/String;)Ljavax/crypto/Mac;",
            {VmValue::Ref(vm.NewStringUtf8("HmacSHA1"))}).ref;
        const auto mac_roots = vm.ProtectReferences(std::array{mac});
        for (const auto alias_name : {"HMAC-SHA1", "HMAC/SHA1",
                                      "1.2.840.113549.2.7"}) {
            const auto alias_mac = direct(
                "Ljavax/crypto/Mac;", "getInstance",
                "(Ljava/lang/String;)Ljavax/crypto/Mac;",
                {VmValue::Ref(vm.NewStringUtf8(alias_name))}).ref;
            const auto hmac_alias_roots =
                vm.ProtectReferences(std::array{alias_mac});
            CHECK(alias_mac.IsValid());
        }
        invoke(mac, "init", "(Ljava/security/Key;)V",
               {VmValue::Ref(hmac_key)});
        const auto hi_there = bytes("4869205468657265");
        const auto hmac_input_roots = vm.ProtectReferences(std::array{hi_there});
        invoke(mac, "update", "([BII)V",
               {VmValue::Ref(hi_there), VmValue::Int(0), VmValue::Int(3)});
        const auto hmac = invoke(mac, "doFinal", "([B)[B",
                                 {VmValue::Ref(bytes("5468657265"))}).ref;
        INFO(core::EncodeHex(read(hmac), core::HexCase::lower));
        CHECK(read(hmac) == read(bytes(
            "b617318655057264e28bc0b6fb378c8ef146be00")));
        CHECK(read(invoke(mac, "doFinal", "([B)[B",
                          {VmValue::Ref(hi_there)}).ref) == read(hmac));
        invoke(mac, "update", "([B)V", {VmValue::Ref(hi_there)});
        invoke(mac, "reset", "()V", {});
        CHECK(read(invoke(mac, "doFinal", "([B)[B",
                          {VmValue::Ref(hi_there)}).ref) == read(hmac));
        const auto mac_provider = invoke(
            mac, "getProvider", "()Ljava/security/Provider;", {}).ref;
        CHECK(vm.StringUtf8(invoke(
                  mac_provider, "getName", "()Ljava/lang/String;", {}).ref) ==
              "AndroidOpenSSL");
        CHECK(vm.StringUtf8(invoke(
                  mac_provider, "getProperty",
                  "(Ljava/lang/String;)Ljava/lang/String;",
                  {VmValue::Ref(vm.NewStringUtf8("Mac.HmacSHA1"))}).ref) ==
              "com.android.org.conscrypt.OpenSSLMac$HmacSHA1");
        vm.ReleaseGuestNativeResources(true);
        CHECK(vm.GuestNativeResourceCount() == 0);
    }
}

TEST_CASE("DVM-171 failed unified crypto JNI initialization aborts process creation") {
    using namespace ogplay;
    runtime::VirtualFileSystem filesystem;
    core::CapabilityLedger ledger;
    core::Logger logger;
    std::vector<std::vector<std::byte>> contents;
    std::vector<runtime::BionicModuleSource> libraries;
    for (const auto name : {"libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so",
                            "libcrypto.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                            "libstlport.so"}) {
        std::ifstream stream(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name,
                             std::ios::binary);
        REQUIRE_MESSAGE(stream.good(), name);
        std::vector<char> data{std::istreambuf_iterator<char>(stream), {}};
        contents.emplace_back(data.size());
        std::transform(data.begin(), data.end(), contents.back().begin(),
                       [](char c) { return static_cast<std::byte>(c); });
        libraries.push_back({name, contents.back()});
    }
    contents.push_back(AppElf(
        {"libogplay_jni.so", "", 0x00090009U, false}));
    libraries.push_back({"libogplay_jni.so", contents.back()});

    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->apk_bytes = {std::byte{0x50}, std::byte{0x4b}, std::byte{3}, std::byte{4}};
    session::AndroidAppProcessRequest request;
    request.manifest = AppManifest("fixture.MainActivity");
    request.system_libraries = libraries;
    request.dex_bytes = ReadDexFixture("cipher.dex");
    request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
    request.boot_dex_bytes = test::ReadBootDex();
    request.context = context;
    request.surface_width = 64;
    request.surface_height = 36;
    request.maximum_ticks_per_call = UINT64_C(100000000);
    request.filesystem = &filesystem;
    request.ledger = &ledger;
    request.logger = &logger;

    try {
        static_cast<void>(session::AndroidAppProcess::Create(std::move(request)));
        FAIL("invalid unified JNI initialization unexpectedly succeeded");
    } catch (const session::AndroidAppProcessError& error) {
        CHECK(std::string(error.what()).find(
                  "API 19 guest JNI initialization failed") != std::string::npos);
        CHECK(std::string(error.what()).find(
                  "unsupported JNI version") != std::string::npos);
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
                                "libcrypto.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                                "libstlport.so", "libogplay_jni.so"}) {
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
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
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
                                "libcrypto.so", "libgabi++.so", "libicui18n.so", "libicuuc.so",
                                "libstlport.so", "libogplay_jni.so"}) {
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
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
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
        // DVM-151: builder hooks require the same real SHA/serialization backend.
        for (const auto* owner : {"Ljava/lang/StringBuilder;", "Ljava/lang/StringBuffer;"}) {
            CAPTURE(owner);
            const auto builder = vm.NewIntrinsicInstance(owner);
            const auto root = vm.ProtectReferences(std::array{builder});
            const auto text = vm.Model().NewString(std::u16string_view(u"A😀 B", 5));
            direct(owner, "<init>", "(Ljava/lang/String;)V", {VmValue::Ref(builder), VmValue::Ref(text)});
            invoke(builder, "ensureCapacity", "(I)V", {VmValue::Int(80)});
            const auto capacity = invoke(builder, "capacity", "()I", {}).AsInt();
            const auto snapshot = vm.Model().StringValue(invoke(builder, "toString", "()Ljava/lang/String;", {}).ref);
            const auto copy = roundtrip(builder);
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            CHECK(copy != builder);
            CHECK(vm.Model().ObjectClass(copy) == vm.Model().ObjectClass(builder));
            CHECK(vm.Model().StringValue(invoke(copy, "toString", "()Ljava/lang/String;", {}).ref) == snapshot);
            CHECK(invoke(copy, "capacity", "()I", {}).AsInt() == capacity);
            static_cast<void>(vm.CollectGarbage("builder-roundtrip"));
            const auto signature = std::string("(C)") + owner;
            invoke(copy, "append", signature.c_str(), {VmValue::Int('!')});
            CHECK(vm.Model().StringValue(invoke(builder, "toString", "()Ljava/lang/String;", {}).ref) == snapshot);
            CHECK(vm.Model().StringValue(invoke(copy, "toString", "()Ljava/lang/String;", {}).ref) == snapshot + u"!");
        }
        // DVM-115: real Java serialization includes Throwable's private callbacks and stack arrays.
        {
            const auto exception = vm.MakeThrowable("Ljava/lang/Exception;", "outer");
            const auto exception_root = vm.ProtectReferences(std::array{exception});
            const auto cause = vm.MakeThrowable("Ljava/lang/IllegalStateException;", "inner");
            vm.InitThrowableCause(exception, cause);
            invoke(exception, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(cause)});
            const auto frame = vm.NewIntrinsicInstance("Ljava/lang/StackTraceElement;");
            const auto frame_root = vm.ProtectReferences(std::array{frame});
            direct("Ljava/lang/StackTraceElement;", "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V",
                {VmValue::Ref(frame), VmValue::Ref(vm.NewStringUtf8("Probe")), VmValue::Ref(vm.NewStringUtf8("work")),
                 VmValue::Ref(vm.NewStringUtf8("Probe.java")), VmValue::Int(42)});
            const auto trace = vm.Model().NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/StackTraceElement;"),
                linker.ResolveDescriptor("Ljava/lang/StackTraceElement;"), 1);
            vm.Model().SetObjectElement(trace, 0, frame);
            invoke(exception, "setStackTrace", "([Ljava/lang/StackTraceElement;)V", {VmValue::Ref(trace)});
            const auto copy = roundtrip(exception);
            const auto copy_root = vm.ProtectReferences(std::array{copy});
            CHECK(copy != exception);
            CHECK(vm.StringUtf8(invoke(copy, "getMessage", "()Ljava/lang/String;", {}).ref) == "outer");
            const auto copy_cause = invoke(copy, "getCause", "()Ljava/lang/Throwable;", {}).ref;
            CHECK(copy_cause != cause);
            CHECK(vm.StringUtf8(invoke(copy_cause, "getMessage", "()Ljava/lang/String;", {}).ref) == "inner");
            const auto suppressed = invoke(copy, "getSuppressed", "()[Ljava/lang/Throwable;", {}).ref;
            REQUIRE(vm.Model().ArrayLength(suppressed) == 1);
            CHECK(vm.Model().GetObjectElement(suppressed, 0) == copy_cause);
            const auto copy_trace = invoke(copy, "getStackTrace", "()[Ljava/lang/StackTraceElement;", {}).ref;
            REQUIRE(vm.Model().ArrayLength(copy_trace) == 1);
            CHECK(invoke(vm.Model().GetObjectElement(copy_trace, 0), "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(frame)}).AsInt() == 1);
            const auto sink = vm.NewIntrinsicInstance("Ljava/io/StringWriter;");
            const auto sink_root = vm.ProtectReferences(std::array{sink});
            direct("Ljava/io/StringWriter;", "<init>", "()V", {VmValue::Ref(sink)});
            const auto writer = vm.NewIntrinsicInstance("Ljava/io/PrintWriter;");
            const auto writer_root = vm.ProtectReferences(std::array{writer});
            direct("Ljava/io/PrintWriter;", "<init>", "(Ljava/io/Writer;)V", {VmValue::Ref(writer), VmValue::Ref(sink)});
            invoke(copy, "printStackTrace", "(Ljava/io/PrintWriter;)V", {VmValue::Ref(writer)});
            const auto printed = vm.StringUtf8(invoke(sink, "toString", "()Ljava/lang/String;", {}).ref);
            CHECK(printed.find("java.lang.Exception: outer\n\tat Probe.work(Probe.java:42)\n") == 0);
            CHECK(printed.find("Caused by: java.lang.IllegalStateException: inner") != std::string::npos);
        }
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

TEST_CASE("DVM-112 AndroidAppProcess installs sealed service discovery facts") {
    for (const bool enabled : {false, true}) {
        OrchestratedApp f("fixture.LauncherActivity", true, false, {},
            {{"fixture.LocalService", false, {{{"fixture.SERVICE"}, {"fixture.CATEGORY"}, true}}}}, enabled);
        const auto context = f.app->Context();
        CHECK(context->service_inventory_known);
        CHECK(context->application_enabled == enabled);
        REQUIRE(context->service_components.size() == 1);
        const auto& service = context->service_components[0];
        CHECK(service.name == "fixture.LocalService");
        CHECK_FALSE(service.enabled);
        REQUIRE(service.intent_filters.size() == 1);
        CHECK(service.intent_filters[0].actions == std::vector<std::string>{"fixture.SERVICE"});
        CHECK(service.intent_filters[0].categories == std::vector<std::string>{"fixture.CATEGORY"});
        CHECK(service.intent_filters[0].has_data);
    }
}


TEST_CASE("DVM-144 application Proxy executes BootDex Binder transact and Java reply protocol") {
    using namespace ogplay::runtime::dexvm;
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ApplicationProcess f(backend);
        auto& vm = f.bridge->Vm();
        auto& linker = f.bridge->Linker();
        const auto direct = [&](const char* owner, const char* name, const char* sig, std::vector<VmValue> args) {
            const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, sig);
            REQUIRE(method);
            const auto result = vm.Call(*method, args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        const auto on = [&](VmObjectRef receiver, const char* name, const char* sig, std::vector<VmValue> args) {
            const auto type = vm.Model().ObjectClass(receiver);
            const auto index = linker.FindVtableIndex(type, name, sig);
            REQUIRE(index);
            args.insert(args.begin(), VmValue::Ref(receiver));
            return vm.Call(linker.Class(type).vtable[*index], args);
        };
        const auto local = vm.NewIntrinsicInstance("Lfixture/EchoBinder;");
        const auto remote_shape = vm.NewIntrinsicInstance("Lfixture/ForcedProxyBinder;");
        const auto roots = vm.ProtectReferences(std::array{local, remote_shape});
        direct("Lfixture/EchoBinder;", "<init>", "()V", {VmValue::Ref(local)});
        direct("Lfixture/ForcedProxyBinder;", "<init>", "()V", {VmValue::Ref(remote_shape)});
        const auto descriptor = vm.NewStringUtf8("fixture.echo");
        CHECK(on(local, "queryLocalInterface", "(Ljava/lang/String;)Landroid/os/IInterface;", {VmValue::Ref(descriptor)}).value.ref == local);
        CHECK_FALSE(on(remote_shape, "queryLocalInterface", "(Ljava/lang/String;)Landroid/os/IInterface;", {VmValue::Ref(descriptor)}).value.ref.IsValid());
        for (const auto flags : {0, 1})
            CHECK(direct("Lfixture/EchoProxy;", "echo", "(Landroid/os/IBinder;II)I",
                         {VmValue::Ref(remote_shape), VmValue::Int(41), VmValue::Int(flags)}).AsInt() == 42);
        CHECK_FALSE(linker.Class(linker.ResolveDescriptor("Lfixture/EchoBinder;")).is_intrinsic);
        const auto binder = linker.ResolveDescriptor("Landroid/os/Binder;");
        const auto transact_index = linker.FindVtableIndex(binder, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z");
        REQUIRE(transact_index);
        CHECK(linker.Method(linker.Class(binder).vtable[*transact_index]).code.has_value());
        const auto data = direct("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;", {}).ref;
        const auto reply = direct("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;", {}).ref;
        const auto parcels = vm.ProtectReferences(std::array{data, reply});
        const auto invoke = [&](int code) {
            return on(local, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
                      {VmValue::Int(code), VmValue::Ref(data), VmValue::Ref(reply), VmValue::Int(0)});
        };
        const auto unknown = invoke(99);
        REQUIRE_FALSE(unknown.exception.IsValid());
        CHECK(unknown.value.AsInt() == 0);
        const auto failure = invoke(2);
        REQUIRE(failure.exception.IsValid());
        CHECK(linker.Class(failure.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        CHECK(on(reply, "dataSize", "()I", {}).value.AsInt() == 0);
        const auto info = invoke(0x5f4e5446);
        REQUIRE_FALSE(info.exception.IsValid());
        CHECK(info.value.AsInt() == 1);
        CHECK(on(reply, "dataPosition", "()I", {}).value.AsInt() == 0);
        CHECK(vm.StringUtf8(on(reply, "readString", "()Ljava/lang/String;", {}).value.ref) == "fixture.echo");
    }
}


TEST_CASE("DVM-150 Java exit unwinds Activity startup and stops the guest process") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.ExitActivity");
    fixture.app->StartApplication();
    const auto stopped = fixture.app->StartLauncherActivity();
    CHECK(stopped.state == session::LifecycleRunState::stopped);
    CHECK(fixture.app->State() == session::AndroidAppProcessState::stopped);
    CHECK(fixture.app->DexVm().Vm().ExitCode() == 29);
    CHECK(runtime::SessionExitRequested(*fixture.app->Context()));
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK(fixture.app->Stop().state == session::LifecycleRunState::stopped);
}

TEST_CASE("DVM-150 Java exit cannot be caught by Application onCreate") {
    using namespace ogplay;
    ApplicationProcess fixture;
    session::DexActivityLifecycleBindings bindings;
    bindings.bridge = fixture.bridge.get();
    bindings.context = fixture.context;
    bindings.application_descriptor = "Lfixture/ExitApplication;";
    bindings.launcher_descriptor = "Lfixture/LauncherActivity;";
    bool opened = false;
    bindings.open_surface = [&] { opened = true; };
    session::DexActivityLifecycle lifecycle(std::move(bindings));
    const auto state = lifecycle.Start();
    CHECK(state.state == session::LifecycleRunState::stopped);
    CHECK(fixture.bridge->Vm().ExitCode() == 31);
    CHECK(fixture.bridge->Vm().GetSystemProperty("shutdown.hook") == "done");
    CHECK(fixture.context->uptime_millis.load() >= 10);
    CHECK_FALSE(opened);
}


TEST_CASE("DVM-150 AndroidAppProcess does not launch after Application exits") {
    using namespace ogplay;
    OrchestratedApp fixture("fixture.LauncherActivity", true, false, "", {}, true,
        runtime::dexvm::InterpreterBackend::switch_dispatch, "fixture.ExitApplication");
    fixture.app->StartApplication();
    CHECK(fixture.app->State() == session::AndroidAppProcessState::stopped);
    CHECK(fixture.app->StartLauncherActivity().state == session::LifecycleRunState::stopped);
    CHECK(fixture.app->DexVm().Vm().ExitCode() == 31);
    CHECK_FALSE(fixture.app->NativeProcess().Running());
    CHECK_FALSE(fixture.app->Context()->activity.IsValid());
}

TEST_CASE("DVM-150 Runtime exit propagates through guest JNI OnLoad reentry") {
    using namespace ogplay;
    auto libc = LibcElf();
    auto a = AppElf({"liba.so", "libc.so", runtime::kJniVersion1_6, true, true});
    std::array inputs{
        loader::Elf32ModuleInput{"liba.so", a, memory::GuestAddress{0x20000000U}},
        loader::Elf32ModuleInput{"libc.so", libc, memory::GuestAddress{0x10000000U}},
    };
    runtime::VirtualFileSystem filesystem;
    auto session = runtime::AndroidGuestCallSession::Start(
        {19, "liba.so", inputs, {}, 64, 36, UINT64_C(200000), 1, &filesystem, {}});
    loader::ApkNativeLibraryInventory inventory{{Library("liba.so", a)}};
    auto selected = loader::SelectApkNativeLibraries(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader libraries(session->Process(), selected);
    auto context = std::make_shared<runtime::DexVmAndroidContext>();
    context->session = session.get();
    context->native_libraries = &libraries;
    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    auto bridge = std::make_unique<runtime::DexVmGuestBridge>(
        *session, ReadDexFixture("aps5.dex"), catalog, context, ledger,
        nullptr, runtime::DexVmBridgeConfig{}, ogplay::test::ReadBootDex());
    const auto type = bridge->Linker().ResolveDescriptor("Lfixture/Aps5;");
    const auto initialized = bridge->Vm().EnsureClassInitialized(type);
    REQUIRE_FALSE(initialized.exception.IsValid());
    bridge->Vm().SetStaticFieldBits("Lfixture/Aps5;", "exitCode", "I", 37);
    const auto start = bridge->Linker().FindDirectMethod(type, "start", "()V");
    REQUIRE(start.has_value());
    bool returned = false;
    try {
        static_cast<void>(bridge->Vm().Call(*start, {}));
        returned = true;
    } catch (const runtime::dexvm::DexVmError& error) {
        CHECK(error.Reason() == runtime::dexvm::DexVmErrorReason::thread_stopped);
    }
    CHECK_FALSE(returned);
    CHECK(bridge->Vm().ExitCode() == 37);
    CHECK(runtime::SessionExitRequested(*context));
    bridge.reset();
    session->Stop();
    CHECK_FALSE(session->Running());
}
