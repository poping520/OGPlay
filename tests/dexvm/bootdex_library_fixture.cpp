#include "bootdex_library_fixture.h"

#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

namespace ogplay::test::bootdex_library {

std::vector<std::uint8_t> Dvm102BootDex() {
    const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) /
        "data/android/19/framework/bootdex.jar";
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), path.string());
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
    std::vector<std::byte> archive_bytes(raw.size());
    std::memcpy(archive_bytes.data(), raw.data(), raw.size());
    const auto archive = ogplay::loader::ParseApkArchive(archive_bytes);
    const auto dex = ogplay::loader::ReadApkEntry(
        archive_bytes, archive, "classes.dex");
    std::vector<std::uint8_t> result(dex.size());
    std::memcpy(result.data(), dex.data(), dex.size());
    return result;
}



Dvm87Vm::Dvm87Vm(
    const InterpreterBackend backend,
    const std::string& language,
    const std::string& iso3_language,
    const std::string& iso3_country,
    const std::string& default_timezone,
    const std::vector<IntrinsicClassDecl>& extras)
    : vm([this, &language, &iso3_language,
          &iso3_country, &default_timezone, &extras]() -> DexClassLinker& {
          CoreIntrinsicServices services;
          services.language = language;
          services.iso3_language = iso3_language;
          services.iso3_country = iso3_country;
          services.default_timezone = default_timezone;
          services.current_time_millis = [] { return 1704067200000LL; };
          linker.RegisterIntrinsics(CoreIntrinsicCatalog(services));
          linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
          ogplay::test::BindBootDexPlatformNatives(linker);
          linker.RegisterBootDex(Dvm102BootDex());
          auto callable = IntrinsicClassBuilder::Class(
              "Ltest/Dvm87Callable;", "Ljava/lang/Object;",
              {"Ljava/util/concurrent/Callable;"});
          callable.VirtualMethod(
              "call", "()Ljava/lang/Object;",
              [](IntrinsicContext& context) {
                  return VmValue::Ref(context.vm.NewStringUtf8("done"));
              });
          std::vector<IntrinsicClassDecl> test_catalog;
          test_catalog.push_back(std::move(callable).Build());
          linker.RegisterIntrinsics(test_catalog);
          linker.RegisterIntrinsics(extras);
          linker.Link();
          return linker;
      }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}),
      threads(vm) {
    ogplay::test::InstallCaPack(vfs);
    vm.IO().SetFileSystem(&io_file_system);
}

Dvm87Vm::~Dvm87Vm() { threads.Shutdown(); }

VmCallOutcome Dvm87Vm::Static(
    const std::string_view owner, const std::string_view name,
    const std::string_view descriptor,
    std::vector<VmValue> arguments) {
    const auto method = linker.FindDirectMethod(
        linker.ResolveDescriptor(owner), std::string(name),
        std::string(descriptor));
    REQUIRE(method.has_value());
    return vm.Call(*method, arguments);
}

VmCallOutcome Dvm87Vm::Virtual(
    const VmObjectRef receiver, const std::string_view name,
    const std::string_view descriptor,
    std::vector<VmValue> arguments) {
    const auto owner = model.ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(
        owner, std::string(name), std::string(descriptor));
    REQUIRE(index.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    return vm.Call(linker.Class(owner).vtable[*index], arguments);
}

void Dvm87Vm::Construct(const VmObjectRef object, const std::string_view owner,
               const std::string_view descriptor,
               std::vector<VmValue> arguments) {
    arguments.insert(arguments.begin(), VmValue::Ref(object));
    RequireOk(Static(owner, "<init>", descriptor, arguments));
}

void Dvm87Vm::RequireOk(const VmCallOutcome& outcome) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
}

} // namespace ogplay::test::bootdex_library
