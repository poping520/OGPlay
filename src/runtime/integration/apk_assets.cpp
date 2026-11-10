#include "ogplay/runtime/integration/apk_assets.h"

#include <algorithm>

#include "ogplay/hal/fs.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace res = resources;

namespace {
constexpr std::string_view kFrameworkPath = "/system/framework/framework-res.apk";

class NdkResourceAsset final : public AndroidAssetSource {
public:
    NdkResourceAsset(std::unique_ptr<res::Asset> asset, VirtualFileSystem* vfs) : asset_(std::move(asset)), vfs_(vfs) {}

    std::uint64_t Length() const override { return asset_->length(); }

    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> output) const override {
        std::scoped_lock lock(mutex_);
        if (offset >= asset_->length()) return 0;
        static_cast<void>(asset_->seek(static_cast<std::int64_t>(offset), 0));
        return asset_->read(output);
    }

    AssetFileDescriptor OpenFileDescriptor() const override {
        if (!vfs_ || !asset_->isStored()) return {};
        const auto range = asset_->openFd();
        if (range.source.isUpToDate && !range.source.isUpToDate()) return {};
        return {vfs_->Open(range.source.name, {.read = true}), range.offset, range.length};
    }

    void CloseFileDescriptor(std::int32_t fd) const override { vfs_->Close(fd); }

private:
    std::unique_ptr<res::Asset> asset_;
    VirtualFileSystem* vfs_;
    mutable std::mutex mutex_;
};
}  // namespace

ResourceAssetManager::ResourceAssetManager(std::shared_ptr<const res::AssetPath> framework, VirtualFileSystem* vfs)
    : core(std::move(framework)), vfs_(vfs) {}

std::shared_ptr<const AndroidAssetSource> ResourceAssetManager::Open(std::string_view name) {
    std::scoped_lock lock(mutex);
    if (retired) throw std::invalid_argument("AAssetManager is retired");
    auto asset = core.open(name);
    return asset ? std::make_shared<NdkResourceAsset>(std::move(asset), vfs_) : nullptr;
}

std::vector<AndroidAssetDirectoryEntry> ResourceAssetManager::ListDirectory(std::string_view name) {
    std::scoped_lock lock(mutex);
    if (retired) throw std::invalid_argument("AAssetManager is retired");
    if (name.ends_with('/') || name.find("//") != std::string_view::npos) return {};
    std::vector<AndroidAssetDirectoryEntry> result;
    std::vector<std::string> names;
    try {
        names = core.list(name);
    } catch (const res::ResourceError&) {
        return {};
    }
    for (auto& entry : names) {
        auto path = name.empty() ? entry : std::string(name) + "/" + entry;
        const auto asset = core.open(path);
        result.push_back({std::move(entry), static_cast<bool>(asset)});
    }
    return result;
}

std::optional<res::ResTable_config> ResourceAssetManager::Configuration() const {
    std::scoped_lock lock(mutex);
    if (retired) return {};
    return core.resources().parameters();
}

std::shared_ptr<const res::AssetPath> FrameworkResourceApk(const DexVmAndroidContext& context) {
    if (context.framework_resource_apk.empty()) throw res::ResourceError("framework resource APK was not injected");
    return res::sharedFramework([&] {
        const auto file = hal::OpenHostReadFile(context.framework_resource_apk);
        auto mapping = file->Map();
        return res::Source{std::move(mapping.owner), mapping.bytes, std::string(kFrameworkPath), {}};
    });
}

res::Source ReadResourceApk(DexVmAndroidContext& context, const std::string& path) {
    if (path == kFrameworkPath) return FrameworkResourceApk(context)->source();
    if (!context.vfs) throw res::ResourceError("asset paths require a guest filesystem");
    const auto info = context.vfs->Stat(path);
    auto unchanged = [vfs = context.vfs, path, generation = info.generation] {
        try {
            return vfs->Stat(path).generation == generation;
        } catch (const VfsError&) {
            return false;
        }
    };
    if (path == context.package_resource_path && context.sealed_apk_bytes && !info.writable &&
        info.size == context.sealed_apk_bytes->size()) {
        // The installed node and the resource path borrow the same immutable APK.
        return {context.sealed_apk_bytes, *context.sealed_apk_bytes, path, unchanged};
    }
    if (info.size > 1024ULL * 1024 * 1024) throw res::ResourceError("asset path exceeds APK size limit");
    const auto fd = context.vfs->Open(path, {.read = true});
    std::shared_ptr<const VfsReadLease> lease;
    try {
        lease = context.vfs->CaptureReadLease(fd, 0, info.size);
    } catch (...) {
        context.vfs->Close(fd);
        throw;
    }
    context.vfs->Close(fd);
    auto bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(lease->Size()));
    if (lease->ReadAt(0, *bytes) != bytes->size()) throw res::ResourceError("short asset path read");
    return {bytes, *bytes, path, std::move(unchanged)};
}

std::vector<std::byte> ReadResourceAsset(res::Asset& asset, std::size_t limit) {
    if (asset.length() > limit) throw res::ResourceError("asset exceeds consumer byte limit");
    static_cast<void>(asset.seek(0, 0));
    std::vector<std::byte> bytes(static_cast<std::size_t>(asset.length()));
    if (asset.read(bytes) != bytes.size()) throw res::ResourceError("short asset read");
    return bytes;
}
} // namespace ogplay::runtime
