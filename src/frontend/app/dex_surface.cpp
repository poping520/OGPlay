#include "ogplay/runtime/dexvm/platform_tables.h"
#include "ogplay/app/diagnostics.h"
#include "ogplay/app/host.h"
#include "ogplay/core/json.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_link_preflight.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>

namespace ogplay::app {
std::string DumpDexSurface(const std::filesystem::path& boot_archive_path) {
    std::ifstream input(boot_archive_path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open BootDex archive");
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::byte> archive(raw.size());
    std::memcpy(archive.data(), raw.data(), raw.size());
    const auto bytes = loader::ReadApkEntry(archive, loader::ParseApkArchive(archive), "classes.dex");
    std::vector<std::uint8_t> dex(bytes.size());
    std::memcpy(dex.data(), bytes.data(), bytes.size());
    const auto context = runtime::AssembleAndroidContext(std::move(archive), {true});
    const auto sources = runtime::dexvm::DexClassLinker::BootDexMemberSources(context->boot_classpath_bytes);
    runtime::dexvm::DexClassLinker linker;
    linker.RegisterIntrinsics(runtime::BoundCoreIntrinsicCatalog(context));
    linker.RegisterIntrinsics(runtime::AndroidIntrinsicCatalog(context));
    linker.RegisterBootDex(std::move(dex), sources);
    const auto skeleton_path = BundledDataPaths().root / "dexvm/api19-skeleton.bin";
    std::ifstream skeleton(skeleton_path, std::ios::binary);
    if (!skeleton) throw std::runtime_error("cannot open skeleton surface");
    linker.RegisterSkeletonSurface(std::vector<std::uint8_t>{std::istreambuf_iterator<char>(skeleton), {}});
    core::JsonParseError error;
    const auto surface = core::JsonDocument::ParseStrict(linker.DumpSurfaceJson(), error, 64U * 1024U * 1024U);
    if (!surface) throw std::runtime_error("invalid runtime surface JSON: " + error.message);
    const auto native = core::JsonDocument::ParseStrict(runtime::DumpAndroidBoundarySurfaceJson(), error);
    if (!native) throw std::runtime_error("invalid native surface JSON: " + error.message);
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddInteger(root, "schema", 1);
    writer.AddInteger(root, "api", 19);
    writer.Add(root, "classes", writer.Copy(*surface->Root().Member("classes")));
    writer.Add(root, "startup_link_error", writer.Copy(*surface->Root().Member("startup_link_error")));
    writer.Add(root, "native_exports", writer.Copy(native->Root()));
    const auto tables = writer.Object();
    const auto services = writer.Object();
    for (const auto& entry : runtime::dexvm::kSystemServices) {
        writer.AddString(services, entry.name, entry.Provided() ? "provided" : "unsupported");
    }
    writer.Add(tables, "system_services", services);
    const auto properties = writer.Object();
    for (const auto& entry : runtime::dexvm::kSystemProperties) {
        writer.AddString(properties, entry.key,
                         entry.source.empty() ? std::string(entry.value) : "source: " + std::string(entry.source));
    }
    writer.Add(tables, "system_properties", properties);
    writer.Add(root, "platform_tables", tables);
    return writer.Serialize(root);
}
}
