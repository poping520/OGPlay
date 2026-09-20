#include "ogplay/runtime/integration/dexvm_android.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "ogplay/audio/encoded_audio.h"
#include "ogplay/audio/encoded_audio_stream.h"
#include "ogplay/loader/apk.h"
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

class ApkAudioSource final : public audio::EncodedAudioDataSource {
public:
    ApkAudioSource(const std::span<const std::byte> bytes,
                   const loader::ApkArchive& archive, std::string name,
                   const std::uint64_t offset, const std::uint64_t length)
        : bytes_(bytes), archive_(&archive), name_(std::move(name)),
          offset_(offset), length_(length) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override { return length_; }
    [[nodiscard]] std::size_t ReadAt(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop) const override {
        if (stop.stop_requested() || offset >= length_) return 0;
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            destination.size(), length_ - offset));
        return loader::ReadApkEntryRange(bytes_, *archive_, name_, offset_ + offset,
                                         destination.first(count), stop);
    }
private:
    std::span<const std::byte> bytes_;
    const loader::ApkArchive* archive_{};
    std::string name_;
    std::uint64_t offset_{};
    std::uint64_t length_{};
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
    std::string apk_name;
    if (source.kind == audio::EncodedAudioSource::Kind::resource) {
        const auto* entry = context.arsc.FindById(
            static_cast<std::uint32_t>(source.resource));
        if (entry == nullptr || !entry->string_value.has_value()) return nullptr;
        apk_name = *entry->string_value;
    }
    if (source.kind == audio::EncodedAudioSource::Kind::apk_entry ||
        source.kind == audio::EncodedAudioSource::Kind::resource) {
        if (apk_name.empty()) apk_name = source.name;
        const auto found = std::find_if(
            context.archive.entries.begin(), context.archive.entries.end(),
            [&apk_name](const loader::ApkEntry& entry) {
                return entry.name == apk_name;
            });
        if (found == context.archive.entries.end() || source.offset > found->uncompressed_size)
            return nullptr;
        const auto available = found->uncompressed_size - source.offset;
        const auto length = WindowLength(available, source.length);
        if (length > available || length > audio::kMaximumEncodedAudioBytes) return nullptr;
        return std::make_shared<ApkAudioSource>(
            context.apk_bytes, context.archive, apk_name, source.offset, length);
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

}  // namespace ogplay::runtime
