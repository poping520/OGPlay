#include "runtime/boundary/modules/android/android_module.h"
#include <algorithm>
#include <stdexcept>
#include "ogplay/core/logger.h"
#include "ogplay/core/capability_ledger.h"

namespace ogplay::runtime {
AndroidModule::GuestBuffer::GuestBuffer(memory::AddressSpace& space, std::uint64_t size)
    : memory(space), range(space.MapAnywhere(
        {memory::GuestAddress{0x60000000U}, 0xa0000000ULL}, size,
        memory::PageProtection::read | memory::PageProtection::write), size) {}

AndroidModule::GuestBuffer::~GuestBuffer() { memory.Unmap(range); }

std::unique_ptr<AndroidModule::GuestBuffer> AndroidModule::ReadOnlyBuffer(
    std::span<const std::byte> bytes, std::uint64_t thread) {
    const auto page = services_.address_space.PageSize();
    const auto size = std::max<std::uint64_t>(page, (bytes.size() + page - 1) / page * page);
    auto buffer = std::unique_ptr<GuestBuffer>(new GuestBuffer(services_.address_space, size));
    if (!bytes.empty()) services_.address_space.Write(buffer->range.Start(), bytes, thread);
    services_.address_space.Protect(buffer->range, memory::PageProtection::read);
    return buffer;
}

std::string AndroidModule::AssetPath(const A32CallFrame& call, std::size_t argument) {
    std::string name;
    for (std::size_t i = 0; i < 4096; ++i) {
        std::array<std::byte, 1> byte{};
        services_.address_space.Read(call.Pointer<std::byte>(argument).Address().Add(i), byte, call.ThreadId());
        if (byte[0] == std::byte{}) return name;
        name.push_back(static_cast<char>(byte[0]));
    }
    throw std::length_error("asset directory path exceeds budget");
}

BoundaryResult AndroidModule::AssetExtension(const A32CallFrame& call, std::uint16_t id) {
    const auto handle = call.Pointer<void>(0).Address();
    if (id == 101) {
        std::shared_ptr<AndroidAssetManagerSource> source;
        {
            std::scoped_lock lock(mutex_);
            const auto found = asset_managers_.find(handle.Value());
            if (found == asset_managers_.end()) throw std::invalid_argument("AAssetManager is stale or unregistered");
            source = found->second;
        }
        auto entries = source->ListDirectory(AssetPath(call, 1));
        std::uint64_t name_bytes{};
        for (const auto& entry : entries) {
            name_bytes += entry.name.size();
            if (entry.name.empty() || entry.name.find_first_of("/\\") != std::string::npos ||
                entry.name.find('\0') != std::string::npos || entry.name.size() >= 4096)
                throw std::invalid_argument("asset directory source returned an invalid name");
        }
        if (entries.size() > 4096 || name_bytes > 1024U * 1024U)
            throw std::length_error("asset directory listing exceeds budget");
        std::scoped_lock lock(mutex_);
        if (!asset_managers_.contains(handle.Value())) throw std::invalid_argument("AAssetManager retired during openDir");
        if (asset_directories_.size() >= 4096 || next_asset_directory_.Value() >= 0x6eb00000U)
            throw std::length_error("asset directory handle budget exhausted");
        const auto directory = next_asset_directory_;
        asset_directories_.emplace(directory.Value(), AssetDirectory{handle, std::move(entries), 0, {}});
        next_asset_directory_ = directory.Add(4);
        return directory.Value();
    }
    if (id == 102 || id == 103) {
        std::scoped_lock lock(mutex_);
        const auto found = asset_directories_.find(handle.Value());
        if (found == asset_directories_.end()) throw std::invalid_argument("AAssetDir is stale or unregistered");
        if (id == 103) { asset_directories_.erase(found); return 0; }
        auto& directory = found->second;
        directory.cached_name.reset();
        auto next = directory.index;
        while (next < directory.entries.size() && !directory.entries[next].regular_file) ++next;
        if (next == directory.entries.size()) { directory.index = next; return 0; }
        const auto& name = directory.entries[next].name;
        directory.cached_name = ReadOnlyBuffer(std::as_bytes(std::span(name.c_str(), name.size() + 1)), call.ThreadId());
        directory.index = next + 1;
        return directory.cached_name->range.Start().Value();
    }
    std::shared_ptr<Asset> asset;
    {
        std::scoped_lock lock(mutex_);
        const auto found = assets_.find(handle.Value());
        if (found == assets_.end()) throw std::invalid_argument("AAsset is stale or unregistered");
        asset = found->second;
    }
    std::scoped_lock lock(asset->mutex);
    if (!asset->buffer) {
        const auto length = asset->source->Length();
        if (length > kAndroidAssetBufferLimit) {
            if (ledger_) ledger_->RecordUnimplemented("runtime.asset_manager", 0);
            if (logger_) logger_->Write(core::LogLevel::error, "runtime.android.asset",
                "AAsset_getBuffer exceeds materialization limit", {.guest_thread = call.ThreadId()},
                {{"length", length}, {"limit", kAndroidAssetBufferLimit}}, {.mode = core::RateLimitMode::none});
            throw std::length_error("AAsset_getBuffer exceeds materialization limit");
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        for (std::size_t offset = 0; offset < bytes.size();) {
            const auto remaining = std::span(bytes).subspan(offset);
            const auto got = asset->source->ReadAt(offset, remaining);
            if (got == 0 || got > remaining.size())
                throw std::runtime_error("AAsset_getBuffer source disagrees with its declared length");
            offset += got;
        }
        asset->buffer = ReadOnlyBuffer(bytes, call.ThreadId());
    }
    return asset->buffer->range.Start().Value();
}
} // namespace ogplay::runtime
