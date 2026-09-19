#include "ogplay/runtime/integration/dexvm_android.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "ogplay/audio/encoded_audio.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::vector<std::byte> SliceOrEmpty(
    const std::span<const std::byte> bytes,
    const audio::EncodedAudioSource& source) {
    try {
        return audio::SliceSourceWindow(bytes, source.offset, source.length);
    } catch (...) {
        return {};
    }
}

[[nodiscard]] bool TakesRemainder(const std::uint64_t length) {
    return length == 0U ||
           length == std::numeric_limits<std::uint64_t>::max() ||
           length == static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max());
}

[[nodiscard]] std::uint64_t WindowLength(const std::uint64_t available,
                                         const std::uint64_t length) {
    if (TakesRemainder(length)) return available;
    return length;
}

}  // namespace

std::vector<std::byte> LoadEncodedAudioWindow(
    DexVmAndroidContext& context, const audio::EncodedAudioSource& source) {
    if (source.lease != 0U) {
        const auto found = context.encoded_audio_leases.find(source.lease);
        if (found == context.encoded_audio_leases.end()) return {};
        return found->second;
    }
    if (source.kind == audio::EncodedAudioSource::Kind::resource) {
        const auto* entry = context.arsc.FindById(
            static_cast<std::uint32_t>(source.resource));
        if (entry == nullptr || !entry->string_value.has_value()) return {};
        return SliceOrEmpty(
            loader::ReadApkEntry(context.apk_bytes, context.archive,
                                 *entry->string_value),
            source);
    }
    if (source.kind == audio::EncodedAudioSource::Kind::apk_entry) {
        const auto found = std::find_if(
            context.archive.entries.begin(), context.archive.entries.end(),
            [&source](const loader::ApkEntry& entry) {
                return entry.name == source.name;
            });
        if (found == context.archive.entries.end()) return {};
        if (found->uncompressed_size > audio::kMaximumEncodedAudioBytes) {
            return {};
        }
        if (found->compression_method == 0U && !TakesRemainder(source.length) &&
            source.length <= audio::kMaximumEncodedAudioBytes) {
            const auto data_offset = loader::StoredApkEntryDataOffset(
                context.apk_bytes, context.archive, source.name);
            const auto begin =
                static_cast<std::size_t>(data_offset + source.offset);
            const auto length = static_cast<std::size_t>(source.length);
            if (begin + length > context.apk_bytes.size()) return {};
            return {context.apk_bytes.begin() +
                        static_cast<std::ptrdiff_t>(begin),
                    context.apk_bytes.begin() +
                        static_cast<std::ptrdiff_t>(begin + length)};
        }
        return SliceOrEmpty(
            loader::ReadApkEntry(context.apk_bytes, context.archive,
                                 source.name),
            source);
    }
    if (context.vfs == nullptr) return {};
    try {
        const auto info = context.vfs->Stat(source.name);
        const auto offset = source.offset;
        if (offset > info.size) return {};
        const auto available = info.size - offset;
        const auto length = WindowLength(available, source.length);
        if (length > available || length > audio::kMaximumEncodedAudioBytes) {
            return {};
        }
        runtime::VfsOpenOptions options;
        options.read = true;
        const auto descriptor = context.vfs->Open(source.name, options);
        const auto skipped = context.vfs->Seek(
            descriptor, static_cast<std::int64_t>(offset),
            runtime::VfsSeekWhence::begin);
        static_cast<void>(skipped);
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        const auto got = context.vfs->Read(descriptor, bytes);
        context.vfs->Close(descriptor);
        bytes.resize(got);
        return bytes;
    } catch (const runtime::VfsError&) {
        return {};
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
    auto bytes = LoadEncodedAudioWindow(context, source);
    if (bytes.empty()) return 0U;
    const auto lease = context.next_encoded_audio_lease++;
    context.encoded_audio_leases[lease] = std::move(bytes);
    source.lease = lease;
    return lease;
}

}  // namespace ogplay::runtime
