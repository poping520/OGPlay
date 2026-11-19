#pragma once

#include <mutex>

#include "ogplay/runtime/boundary/asset_boundary.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/resources/resources.h"

namespace ogplay::runtime {
struct DexVmAndroidContext;
class VirtualFileSystem;

// The NDK facade and the ROM native adapter share this manager and its lock.
class ResourceAssetManager final : public AndroidAssetManagerSource {
public:
    ResourceAssetManager(std::shared_ptr<const resources::AssetPath> framework, VirtualFileSystem* vfs);
    std::shared_ptr<const AndroidAssetSource> Open(std::string_view name) override;
    std::vector<AndroidAssetDirectoryEntry> ListDirectory(std::string_view name) override;
    std::optional<resources::ResTable_config> Configuration() const override;

    mutable std::mutex mutex;
    bool retired{};
    resources::AssetManager core;

private:
    VirtualFileSystem* vfs_;
};

template <class... Mutexes>
class AssetManagerLock final {
public:
    explicit AssetManagerLock(Mutexes&... mutexes) : lock_(mutexes...) {}

private:
    dexvm::VmYieldForbiddenScope no_vm_yield_{"AssetManager resource lock"};
    std::scoped_lock<Mutexes...> lock_;
};

[[nodiscard]] std::shared_ptr<ResourceAssetManager>
ResolveResourceAssetManager(dexvm::Interpreter& vm, DexVmAndroidContext& context, dexvm::VmObjectRef manager);
[[nodiscard]] resources::Source ReadResourceApk(DexVmAndroidContext& context, const std::string& path);
[[nodiscard]] std::shared_ptr<const resources::AssetPath> FrameworkResourceApk(const DexVmAndroidContext& context);
[[nodiscard]] std::shared_ptr<resources::Asset> ResolveResourceAsset(DexVmAndroidContext& context, std::uint32_t token);
[[nodiscard]] std::vector<std::byte> ReadResourceAsset(resources::Asset& asset, std::size_t limit);
} // namespace ogplay::runtime
