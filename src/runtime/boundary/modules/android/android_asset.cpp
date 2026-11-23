#include "ogplay/runtime/common/guest_address_layout.h"
#include "runtime/boundary/modules/android/android_module.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
#include "ogplay/core/logger.h"
#include "ogplay/core/capability_ledger.h"

namespace ogplay::runtime {
AndroidModule::GuestBuffer::GuestBuffer(memory::AddressSpace& space, std::uint64_t size)
    : memory(space), range(space.MapAnywhere({memory::GuestAddress{0x60000000U}, 0xa0000000ULL}, size,
                                             memory::PageProtection::read | memory::PageProtection::write),
                           size) {}

AndroidModule::GuestBuffer::~GuestBuffer() {
    memory.Unmap(range);
}

std::unique_ptr<AndroidModule::GuestBuffer> AndroidModule::ReadOnlyBuffer(std::span<const std::byte> bytes,
                                                                          std::uint64_t thread) {
    const auto page = services_.address_space.PageSize();
    const auto size = std::max<std::uint64_t>(page, (bytes.size() + page - 1) / page * page);
    auto buffer = std::unique_ptr<GuestBuffer>(new GuestBuffer(services_.address_space, size));
    if (!bytes.empty()) services_.address_space.Write(buffer->range.Start(), bytes, thread);
    services_.address_space.Protect(buffer->range, memory::PageProtection::read);
    return buffer;
}

std::string AndroidModule::AssetPath(const A32CallFrame& call, std::size_t argument) {
    if (call.CString(argument).IsNull()) throw std::invalid_argument("asset path is null");
    std::string name;
    for (std::size_t i = 0; i < 4096; ++i) {
        std::array<std::byte, 1> byte{};
        services_.address_space.Read(call.Pointer<std::byte>(argument).Address().Add(i), byte, call.ThreadId());
        if (byte[0] == std::byte{}) return name;
        name.push_back(static_cast<char>(byte[0]));
    }
    throw std::length_error("asset directory path exceeds budget");
}

BoundaryResult AndroidModule::AAssetManager_fromJava(const A32CallFrame& call) {
    std::scoped_lock guard(asset_hook_mutex_);
    if (!asset_hooks_.from_java) throw std::runtime_error("NDK AssetManager JNI backend is unavailable");
    return asset_hooks_
        .from_java(asset_hooks_.owner, call.ThreadId(), call.Pointer<void>(0).Address(),
                   AssetManagerJavaReference{call.Argument(1)})
        .Value();
}

BoundaryResult AndroidModule::AAssetManager_open(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    std::shared_ptr<AndroidAssetManagerSource> manager;
    {
        std::scoped_lock lock(mutex_);
        const auto found = asset_managers_.find(args[0]);
        if (found != asset_managers_.end()) manager = found->second;
    }
    if (!manager) throw std::invalid_argument("AAssetManager is stale or unregistered");
    if (args[2] > 3) return 0;
    std::string name;
    for (std::size_t i = 0; i < 4096; ++i) {
        std::array<std::byte, 1> byte{};
        services_.address_space.Read(call.Pointer<std::byte>(1).Address().Add(i), byte, tid);
        if (byte[0] == std::byte{}) break;
        name.push_back(static_cast<char>(byte[0]));
        if (i == 4095) throw std::invalid_argument("asset path exceeds budget");
    }
    const auto source = manager->Open(name);
    if (!source) return 0;
    std::scoped_lock lock(mutex_);
    if (!asset_managers_.contains(args[0])) throw std::invalid_argument("AAssetManager retired during open");
    if (assets_.size() >= 4096 || next_asset_ == guest_layout::kInputHandleBegin)
        throw std::runtime_error("NDK asset handle budget exhausted");
    const auto handle = next_asset_++;
    auto asset = std::make_shared<Asset>();
    asset->source = source;
    assets_.emplace(handle, std::move(asset));
    return handle;
}

BoundaryResult AndroidModule::AAssetManager_openDir(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
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
    if (asset_directories_.size() >= 4096 || next_asset_directory_.Value() >= guest_layout::kObbInfoHandleBegin)
        throw std::length_error("asset directory handle budget exhausted");
    const auto directory = next_asset_directory_;
    asset_directories_.emplace(directory.Value(), AssetDirectory{handle, std::move(entries), 0, {}});
    next_asset_directory_ = directory.Add(4);
    return directory.Value();
}

AndroidModule::AssetDirectory& AndroidModule::RequireAssetDirectoryLocked(memory::GuestAddress handle) {
    const auto found = asset_directories_.find(handle.Value());
    if (found == asset_directories_.end()) throw std::invalid_argument("AAssetDir is stale or unregistered");
    return found->second;
}

BoundaryResult AndroidModule::AAssetDir_getNextFileName(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    std::scoped_lock lock(mutex_);
    auto& directory = RequireAssetDirectoryLocked(handle);
    directory.cached_name.reset();
    auto next = directory.index;
    while (next < directory.entries.size() && !directory.entries[next].regular_file)
        ++next;
    if (next == directory.entries.size()) {
        directory.index = next;
        return 0;
    }
    const auto& name = directory.entries[next].name;
    directory.cached_name = ReadOnlyBuffer(std::as_bytes(std::span(name.c_str(), name.size() + 1)), call.ThreadId());
    directory.index = next + 1;
    return directory.cached_name->range.Start().Value();
}

BoundaryResult AndroidModule::AAssetDir_rewind(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    std::scoped_lock lock(mutex_);
    // As on API 19, the previously returned string survives rewind.
    RequireAssetDirectoryLocked(handle).index = 0;
    return 0;
}

BoundaryResult AndroidModule::AAssetDir_close(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    std::scoped_lock lock(mutex_);
    if (!asset_directories_.erase(handle.Value())) throw std::invalid_argument("AAssetDir is stale or unregistered");
    return 0;
}

std::shared_ptr<AndroidModule::Asset> AndroidModule::RequireAsset(memory::GuestAddress handle) {
    std::scoped_lock lock(mutex_);
    const auto found = assets_.find(handle.Value());
    if (found == assets_.end()) throw std::invalid_argument("AAsset is stale or unregistered");
    return found->second;
}

BoundaryResult AndroidModule::AAsset_close(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0).Address();
    std::shared_ptr<Asset> owner;
    {
        std::scoped_lock lock(mutex_);
        const auto found = assets_.find(handle.Value());
        if (found == assets_.end()) throw std::invalid_argument("AAsset is stale or unregistered");
        owner = found->second;
        assets_.erase(found);
    }
    return 0;
}

BoundaryResult AndroidModule::AAsset_read(const A32CallFrame& call) {
    const auto tid = call.ThreadId();
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    const auto count = std::min<std::uint64_t>(call.Argument(2), asset.source->Length() - asset.offset);
    if (count > INT32_MAX) return UINT32_MAX;
    const auto output = call.Pointer<std::byte>(1).Address();
    if (count != 0) services_.address_space.Validate(memory::GuestRange(output, count), memory::AccessType::write, tid);
    std::array<std::byte, 65536> bytes{};
    std::uint64_t completed{};
    while (completed < count) {
        const auto part = static_cast<std::size_t>(std::min<std::uint64_t>(bytes.size(), count - completed));
        const auto got = asset.source->ReadAt(asset.offset + completed, std::span(bytes).first(part));
        if (got > part) throw std::runtime_error("asset source returned too many bytes");
        if (got == 0) break;
        services_.address_space.Write(output.Add(completed), std::span(bytes).first(got), tid);
        completed += got;
    }
    asset.offset += completed;
    return static_cast<std::uint32_t>(completed);
}

BoundaryResult AndroidModule::AAsset_getBuffer(const A32CallFrame& call) {
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    if (!asset.buffer) {
        const auto length = asset.source->Length();
        if (length > kAndroidAssetBufferLimit) {
            if (ledger_) ledger_->RecordUnimplemented("runtime.asset_manager", 0);
            if (logger_)
                logger_->Write(core::LogLevel::error, "runtime.android.asset",
                               "AAsset_getBuffer exceeds materialization limit", {.guest_thread = call.ThreadId()},
                               {{"length", length}, {"limit", kAndroidAssetBufferLimit}},
                               {.mode = core::RateLimitMode::none});
            throw std::length_error("AAsset_getBuffer exceeds materialization limit");
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        for (std::size_t offset = 0; offset < bytes.size();) {
            const auto remaining = std::span(bytes).subspan(offset);
            const auto got = asset.source->ReadAt(offset, remaining);
            if (got == 0 || got > remaining.size())
                throw std::runtime_error("AAsset_getBuffer source disagrees with its declared length");
            offset += got;
        }
        asset.buffer = ReadOnlyBuffer(bytes, call.ThreadId());
    }
    return asset.buffer->range.Start().Value();
}

BoundaryResult AndroidModule::AAsset_isAllocated(const A32CallFrame& call) {
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    // AOSP's file and compressed assets test mBuf. A stored APK entry may
    // expose a FileMap there, and compressed seeking can inflate there.
    // Here only getBuffer creates an owned copy, for either storage method.
    return asset.buffer ? 1U : 0U;
}

namespace {
BoundaryResult AssetOffsetResult(bool wide, std::uint64_t value) {
    if (wide) return BoundaryResult::Wide(value);
    return static_cast<std::uint32_t>(value);
}
}  // namespace

BoundaryResult AndroidModule::AssetLength(const A32CallFrame& call, bool remaining, bool wide) {
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    const auto length = asset.source->Length();
    return AssetOffsetResult(wide, remaining ? length - asset.offset : length);
}

BoundaryResult AndroidModule::SeekAsset(const A32CallFrame& call, bool wide) {
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    const auto whence = call.Argument(wide ? 4 : 2);
    const auto fail = AssetOffsetResult(wide, UINT64_MAX);
    if (whence > 2) return fail;
    const auto length = asset.source->Length();
    const auto base = whence == 0 ? 0 : whence == 1 ? asset.offset : length;
    const auto delta = wide ? std::bit_cast<std::int64_t>((std::uint64_t{call.Argument(3)} << 32) | call.Argument(2))
                            : call.Scalar<std::int32_t>(1);
    // Compute the magnitude without overflowing at INT64_MIN.
    const auto magnitude =
        delta < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(delta) : static_cast<std::uint64_t>(delta);
    if (delta < 0 ? magnitude > base : magnitude > length - base) return fail;
    const auto offset = delta < 0 ? base - magnitude : base + magnitude;
    if (offset > INT64_MAX) return fail;
    asset.offset = offset;
    return AssetOffsetResult(wide, offset);
}

BoundaryResult AndroidModule::OpenAssetFileDescriptor(const A32CallFrame& call, bool wide) {
    const auto owner = RequireAsset(call.Pointer<void>(0).Address());
    auto& asset = *owner;
    std::scoped_lock lock(asset.mutex);
    const auto tid = call.ThreadId();
    const std::size_t bytes = wide ? 8 : 4;
    const auto start_pointer = call.Pointer<void>(1);
    const auto length_pointer = call.Pointer<void>(2);
    if (start_pointer.IsNull() || length_pointer.IsNull())
        throw std::invalid_argument("AAsset_openFileDescriptor requires two output pointers");
    const auto start = services_.address_space.PreflightWrite({start_pointer.Address(), bytes}, tid);
    const auto length = services_.address_space.PreflightWrite({length_pointer.Address(), bytes}, tid);
    const auto descriptor = asset.source->OpenFileDescriptor();
    if (descriptor.fd < 0) return static_cast<std::uint32_t>(descriptor.fd);
    const auto write = [&](const auto& token, std::uint64_t value) {
        std::array<std::byte, 8> encoded{};
        for (std::size_t i = 0; i < bytes; ++i)
            encoded[i] = static_cast<std::byte>(value >> (i * 8));
        services_.address_space.WritePrevalidated(token, std::span(encoded).first(bytes));
    };
    try {
        write(start, descriptor.start);
        write(length, descriptor.length);
    } catch (...) {
        asset.source->CloseFileDescriptor(descriptor.fd);
        throw;
    }
    return static_cast<std::uint32_t>(descriptor.fd);
}

BoundaryResult AndroidModule::AAsset_getLength(const A32CallFrame& call) {
    return AssetLength(call, false, false);
}

BoundaryResult AndroidModule::AAsset_getRemainingLength(const A32CallFrame& call) {
    return AssetLength(call, true, false);
}

BoundaryResult AndroidModule::AAsset_seek(const A32CallFrame& call) {
    return SeekAsset(call, false);
}

BoundaryResult AndroidModule::AAsset_openFileDescriptor(const A32CallFrame& call) {
    return OpenAssetFileDescriptor(call, false);
}

BoundaryResult AndroidModule::AAsset_getLength64(const A32CallFrame& call) {
    return AssetLength(call, false, true);
}

BoundaryResult AndroidModule::AAsset_getRemainingLength64(const A32CallFrame& call) {
    return AssetLength(call, true, true);
}

BoundaryResult AndroidModule::AAsset_seek64(const A32CallFrame& call) {
    return SeekAsset(call, true);
}

BoundaryResult AndroidModule::AAsset_openFileDescriptor64(const A32CallFrame& call) {
    return OpenAssetFileDescriptor(call, true);
}

}  // namespace ogplay::runtime
