#include "ogplay/runtime/integration/dexvm_android.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <stdexcept>

#include "ogplay/audio/encoded_audio.h"
#include "ogplay/audio/encoded_audio_stream.h"
#include "ogplay/runtime/integration/apk_assets.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

class LeaseAudioSource final : public audio::EncodedAudioDataSource {
public:
    explicit LeaseAudioSource(std::shared_ptr<const VfsReadLease> lease)
        : lease_(std::move(lease)) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override {
        return lease_->Size();
    }
    [[nodiscard]] std::size_t ReadAt(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop) const override {
        return lease_->ReadAt(offset, destination, stop);
    }
private:
    std::shared_ptr<const VfsReadLease> lease_;
};

class ResourceAudioSource final : public audio::EncodedAudioDataSource {
public:
    ResourceAudioSource(std::unique_ptr<resources::Asset> asset, std::uint64_t offset, std::uint64_t length)
        : asset_(std::move(asset)), offset_(offset), length_(length) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override { return length_; }

    [[nodiscard]] std::size_t ReadAt(std::uint64_t offset, std::span<std::byte> destination,
                                     std::stop_token stop) const override {
        if (stop.stop_requested() || offset >= length_) return 0;
        std::scoped_lock lock(mutex_);
        static_cast<void>(asset_->seek(static_cast<std::int64_t>(offset_ + offset), 0));
        return asset_->read(
            destination.first(static_cast<std::size_t>(std::min<std::uint64_t>(destination.size(), length_ - offset))));
    }

private:
    std::unique_ptr<resources::Asset> asset_;
    std::uint64_t offset_{}, length_{};
    mutable std::mutex mutex_;
};

[[nodiscard]] bool TakesRemainder(const std::uint64_t length) {
    return length == 0U ||
           length == std::numeric_limits<std::uint64_t>::max() ||
           length == static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max()) ||
           // MediaPlayer.setDataSource(FileDescriptor) uses this AOSP sentinel.
           length == 0x7ffffffffffffffULL;
}

[[nodiscard]] std::uint64_t WindowLength(const std::uint64_t available,
                                         const std::uint64_t length) {
    if (TakesRemainder(length)) return available;
    return length;
}

}  // namespace

audio::JavaSoundPoolMixer::EncodedResource LoadEncodedAudioWindow(
    DexVmAndroidContext& context, const audio::EncodedAudioSource& source) {
    const auto data = LoadEncodedAudioSource(context, source);
    if (!data || context.vfs == nullptr ||
        data->Size() > audio::kMaximumEncodedAudioBytes) return {};
    auto reservation = context.vfs->ReserveResourceMemory(data->Size());
    std::vector<std::byte> bytes(static_cast<std::size_t>(data->Size()));
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = data->ReadAt(offset, std::span<std::byte>(bytes).subspan(offset));
        if (count == 0) return {};
        offset += count;
    }
    return {std::move(bytes), std::move(reservation)};
}

std::shared_ptr<const audio::EncodedAudioDataSource> LoadEncodedAudioSource(
    DexVmAndroidContext& context, const audio::EncodedAudioSource& source) {
    if (source.lease != 0U) {
        const auto found = context.encoded_audio_leases.find(source.lease);
        if (found == context.encoded_audio_leases.end()) return nullptr;
        return found->second;
    }
    if (source.kind == audio::EncodedAudioSource::Kind::apk_entry ||
        source.kind == audio::EncodedAudioSource::Kind::resource) {
        const auto manager = context.application_resource_manager.lock();
        if (!manager) return nullptr;
        try {
            std::scoped_lock lock(manager->mutex);
            if (manager->retired) return nullptr;
            std::unique_ptr<resources::Asset> asset;
            if (source.kind == audio::EncodedAudioSource::Kind::resource) {
                const auto& table = manager->core.resources();
                const auto value =
                    resources::loadResourceValue(table, static_cast<std::uint32_t>(source.resource), 0, true);
                if (!value || value->value.type != 3) return nullptr;
                asset =
                    manager->core.openNonAsset(table.strings(value->cookie).stringAt(value->value.data), value->cookie);
            } else {
                asset = manager->core.openNonAsset(source.name);
            }
            if (!asset || source.offset > asset->length()) return nullptr;
            const auto available = asset->length() - source.offset;
            const auto length = WindowLength(available, source.length);
            if (length > available || length > audio::kMaximumEncodedAudioBytes) return nullptr;
            return std::make_shared<ResourceAudioSource>(std::move(asset), source.offset, length);
        } catch (const std::runtime_error&) {
            return nullptr;
        }
    }
    if (context.vfs == nullptr) return nullptr;
    try {
        const auto info = context.vfs->Stat(source.name);
        const auto offset = source.offset;
        if (offset > info.size) return nullptr;
        const auto available = info.size - offset;
        const auto length = WindowLength(available, source.length);
        if (length > available || length > audio::kMaximumEncodedAudioBytes) {
            return nullptr;
        }
        runtime::VfsOpenOptions options;
        options.read = true;
        const auto descriptor = context.vfs->Open(source.name, options);
        auto lease = context.vfs->CaptureReadLease(descriptor, offset, length);
        context.vfs->Close(descriptor);
        return std::make_shared<LeaseAudioSource>(std::move(lease));
    } catch (const runtime::VfsError&) {
        return nullptr;
    }
}

std::uint64_t CaptureEncodedAudioWindow(DexVmAndroidContext& context,
                                        audio::EncodedAudioSource& source) {
    if (source.kind == audio::EncodedAudioSource::Kind::vfs_path &&
        context.vfs != nullptr) {
        try {
            source.revision = context.vfs->Stat(source.name).generation;
        } catch (const runtime::VfsError&) {
            source.revision = 0U;
        }
    }
    auto data = LoadEncodedAudioSource(context, source);
    if (!data) return 0U;
    const auto lease = context.next_encoded_audio_lease++;
    context.encoded_audio_leases[lease] = std::move(data);
    source.lease = lease;
    return lease;
}

std::optional<std::string> CaptureDescriptorAudioWindow(
    DexVmAndroidContext& context, const std::int32_t descriptor,
    const std::uint64_t offset, const std::uint64_t length,
    audio::EncodedAudioSource& source) {
    if (context.vfs == nullptr) return "guest file system is unavailable";
    try {
        const auto info = context.vfs->DescriptorInfo(descriptor);
        if (info.is_directory || info.is_character_device)
            return "descriptor is not a regular file";
        // MediaPlayerService::setDataSource(fd): offset error, then clip.
        if (offset >= info.size) return "offset is beyond the end of the file";
        const auto window = std::min(length, info.size - offset);
        if (window > audio::kMaximumEncodedAudioBytes)
            return "descriptor window exceeds the encoded media budget";
        auto lease = context.vfs->CaptureReadLease(descriptor, offset, window);
        source = {};
        source.kind = audio::EncodedAudioSource::Kind::vfs_path;
        source.name = "/proc/self/fd/" + std::to_string(descriptor);
        source.offset = offset;
        source.length = window;
        source.revision = info.generation;
        const auto handle = context.next_encoded_audio_lease++;
        context.encoded_audio_leases[handle] =
            std::make_shared<LeaseAudioSource>(std::move(lease));
        source.lease = handle;
        return std::nullopt;
    } catch (const runtime::VfsError& error) {
        return error.what();
    }
}

}  // namespace ogplay::runtime
