#include "ogplay/runtime/integration/apk_assets.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {
using Context = std::shared_ptr<DexVmAndroidContext>;

class ApkAsset final : public AndroidAssetSource {
public:
    ApkAsset(Context context, loader::ApkEntry entry, std::uint64_t start)
        : context_(std::move(context)), entry_(std::move(entry)), start_(start) {}
    std::uint64_t Length() const override { return entry_.uncompressed_size; }
    std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> out) const override {
        if (offset >= Length() || out.empty()) return 0;
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(out.size(), Length() - offset));
        if (entry_.compression_method == 0) {
            const auto data = std::span(context_->apk_bytes).subspan(
                static_cast<std::size_t>(start_ + offset), count);
            std::copy(data.begin(), data.end(), out.begin());
            return count;
        }
        return loader::ReadApkEntryRange(context_->apk_bytes, context_->archive,
            entry_.name, offset, out.first(count));
    }
    AssetFileDescriptor OpenFileDescriptor() const override {
        if (entry_.compression_method != 0) return {};
        if (!context_->vfs || context_->package_resource_path.empty())
            throw std::runtime_error("APK asset descriptor backing is unavailable");
        // AOSP returns a fresh descriptor for the complete APK, plus its range.
        const auto fd = context_->vfs->Open(context_->package_resource_path, {.read = true});
        return {fd, start_, Length()};
    }
    void CloseFileDescriptor(std::int32_t fd) const override { context_->vfs->Close(fd); }
private:
    Context context_;
    loader::ApkEntry entry_;
    std::uint64_t start_{};
};

class ApkAssetManager final : public AndroidAssetManagerSource {
public:
    explicit ApkAssetManager(const Context& context) : context_(context) {}
    std::shared_ptr<const AndroidAssetSource> Open(std::string_view name) override {
        if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos)
            return {};
        for (std::size_t cursor = 0; cursor <= name.size();) {
            const auto end = name.find('/', cursor);
            const auto part = name.substr(cursor, end == std::string_view::npos ? name.size() - cursor : end - cursor);
            if (part.empty() || part == "." || part == "..") return {};
            if (end == std::string_view::npos) break;
            cursor = end + 1;
        }
        return OpenEntry("assets/" + std::string(name));
    }
    std::shared_ptr<const AndroidAssetSource> OpenEntry(std::string_view name) {
        const auto context = context_.lock();
        if (!context) throw std::runtime_error("APK asset source is retired");
        const auto entry = std::ranges::find_if(context->archive.entries,
            [&](const auto& item) { return item.name == name; });
        if (entry == context->archive.entries.end()) return {};
        std::uint64_t start{};
        if (entry->compression_method == 0) {
            std::scoped_lock guard(mutex_);
            const auto found = stored_offsets_.find(entry->name);
            if (found != stored_offsets_.end()) start = found->second;
            else {
                // Full CRC/local-header validation once, with no resource copy.
                const auto bytes = loader::ValidatedStoredApkEntryData(
                    context->apk_bytes, context->archive, entry->name);
                start = static_cast<std::uint64_t>(bytes.data() - context->apk_bytes.data());
                stored_offsets_.emplace(entry->name, start);
            }
        }
        return std::make_shared<ApkAsset>(context, *entry, start);
    }
private:
    std::weak_ptr<DexVmAndroidContext> context_;
    std::mutex mutex_;
    std::map<std::string, std::uint64_t> stored_offsets_;
};
} // namespace

std::shared_ptr<AndroidAssetManagerSource> ApkAssets(const Context& context) {
    // Called while the VM lock is held; sealed production APK facts never change.
    if (!context->apk_asset_source)
        context->apk_asset_source = std::make_shared<ApkAssetManager>(context);
    return context->apk_asset_source;
}

StoredApkAssetRange StoredApkAsset(const Context& context, std::string_view entry) {
    const auto source = std::static_pointer_cast<ApkAssetManager>(ApkAssets(context))->OpenEntry(entry);
    if (!source) throw std::runtime_error("APK asset is unavailable");
    const auto item = std::ranges::find_if(context->archive.entries,
        [&](const auto& candidate) { return candidate.name == entry; });
    if (item->compression_method != 0) throw std::runtime_error("APK asset is compressed");
    return {loader::StoredApkEntryDataOffset(context->apk_bytes, context->archive, entry), source->Length()};
}
} // namespace ogplay::runtime
