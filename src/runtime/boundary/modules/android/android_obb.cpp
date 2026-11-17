#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"

namespace ogplay::runtime {
namespace {
constexpr std::size_t kObbTagSize = 8;
constexpr std::size_t kObbMaxFooterSize = 32768;
constexpr std::size_t kObbPackageOffset = 24;

std::uint32_t ReadLittleEndian(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t i = 0; i < 4; ++i)
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (i * 8);
    return value;
}
}  // namespace

void AndroidModule::RetireObbResources() {
    std::scoped_lock lock(mutex_);
    obb_hooks_ = {};
    obb_info_.clear();
    storage_managers_.clear();
}

BoundaryResult AndroidModule::AObbScanner_getObbInfo(const A32CallFrame& call) {
    // Both APIs take a bounded, NUL-terminated guest path. An overlong OBB
    // filename is a scan failure, as when the API 19 open() rejects it.
    std::string path;
    try {
        path = AssetPath(call, 0);
    } catch (const std::length_error&) {
        return 0;
    }
    if (!obb_hooks_.read_tail) throw std::runtime_error("NDK OBB filesystem backend is unavailable");

    std::array<std::byte, kObbMaxFooterSize + kObbTagSize> bytes{};
    const auto count = obb_hooks_.read_tail(obb_hooks_.owner, path, bytes);
    if (count <= kObbPackageOffset + kObbTagSize || count > bytes.size()) return 0;
    const auto tag = count - kObbTagSize;
    const auto footer_size = ReadLittleEndian(bytes, tag);
    if (ReadLittleEndian(bytes, tag + 4) != 0x01059983U || footer_size <= kObbPackageOffset ||
        footer_size > kObbMaxFooterSize || footer_size > tag)
        return 0;
    const auto footer = std::span(bytes).subspan(tag - footer_size, footer_size);
    if (ReadLittleEndian(footer, 0) != 1) return 0;
    const auto name_size = ReadLittleEndian(footer, 20);
    if (name_size == 0 || name_size > footer.size() - kObbPackageOffset) return 0;
    // API 19 treats flags and the fixed-width salt as opaque. There are no
    // allowed-bit or salt-content checks in ObbFile::parseObbFile.
    ObbInfo info;
    info.version = ReadLittleEndian(footer, 4);
    info.flags = ReadLittleEndian(footer, 8);
    std::copy_n(footer.begin() + 12, info.salt.size(), info.salt.begin());
    const auto package = footer.subspan(kObbPackageOffset, name_size);
    std::vector<std::byte> name(package.begin(), package.end());
    name.push_back(std::byte{});
    std::scoped_lock lock(mutex_);
    if (obb_info_.size() >= 1024 || next_obb_info_.Value() >= 0x6ec00000U)
        throw std::length_error("AObbInfo handle budget exhausted");
    info.package = ReadOnlyBuffer(name, call.ThreadId());
    const auto handle = next_obb_info_;
    obb_info_.emplace(handle.Value(), std::move(info));
    next_obb_info_ = handle.Add(4);
    return handle.Value();
}

AndroidModule::ObbInfo& AndroidModule::RequireObbInfo(GuestPtr<void> handle) {
    const auto found = obb_info_.find(handle.Address().Value());
    if (found == obb_info_.end()) throw std::invalid_argument("AObbInfo is stale or unregistered");
    return found->second;
}

BoundaryResult AndroidModule::AObbInfo_delete(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0);
    if (handle.IsNull()) return 0;
    std::scoped_lock lock(mutex_);
    if (!obb_info_.erase(handle.Address().Value())) throw std::invalid_argument("AObbInfo is stale or unregistered");
    return 0;
}

BoundaryResult AndroidModule::AObbInfo_getPackageName(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return RequireObbInfo(call.Pointer<void>(0)).package->range.Start().Value();
}

BoundaryResult AndroidModule::AObbInfo_getVersion(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return RequireObbInfo(call.Pointer<void>(0)).version;
}

BoundaryResult AndroidModule::AObbInfo_getFlags(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return RequireObbInfo(call.Pointer<void>(0)).flags;
}

BoundaryResult AndroidModule::AStorageManager_new(const A32CallFrame&) {
    std::scoped_lock lock(mutex_);
    // The process has an IMountService even though device storage has no mounted OBBs.
    if (storage_managers_.size() >= 1024 || next_storage_manager_.Value() >= 0x6ed00000U)
        throw std::length_error("AStorageManager handle budget exhausted");
    const auto handle = next_storage_manager_;
    storage_managers_.insert(handle.Value());
    next_storage_manager_ = handle.Add(4);
    return handle.Value();
}

BoundaryResult AndroidModule::AStorageManager_delete(const A32CallFrame& call) {
    const auto handle = call.Pointer<void>(0);
    if (handle.IsNull()) return 0;
    std::scoped_lock lock(mutex_);
    if (!storage_managers_.erase(handle.Address().Value()))
        throw std::invalid_argument("AStorageManager is stale or unregistered");
    return 0;
}

void AndroidModule::RequireStorageManager(const A32CallFrame& call) {
    {
        std::scoped_lock lock(mutex_);
        if (!storage_managers_.contains(call.Pointer<void>(0).Address().Value()))
            throw std::invalid_argument("AStorageManager is stale or unregistered");
    }
    static_cast<void>(AssetPath(call, 1));
}

void AndroidModule::RecordStorageStub(std::string_view name, std::uint64_t thread) {
    if (ledger_) ledger_->RecordUnimplemented("boundary.android." + std::string(name), 0);
    if (logger_)
        logger_->Write(core::LogLevel::warn, "runtime.android.obb", std::string(name) + " has no vold mount backend",
                       {.guest_thread = thread}, {}, {.mode = core::RateLimitMode::first_and_count});
}

BoundaryResult AndroidModule::AStorageManager_mountObb(const A32CallFrame& call) {
    // OGPLAY_STUB(libandroid): AOSP reports the result asynchronously on a Binder thread. OGPlay has no vold mounts.
    RequireStorageManager(call);
    if (call.Pointer<void>(3).IsNull()) throw std::invalid_argument("AStorageManager_mountObb requires a callback");
    RecordStorageStub("AStorageManager_mountObb", call.ThreadId());
    return 0;
}

BoundaryResult AndroidModule::AStorageManager_unmountObb(const A32CallFrame& call) {
    // OGPLAY_STUB(libandroid): AOSP reports the result asynchronously on a Binder thread. OGPlay has no vold mounts.
    RequireStorageManager(call);
    if (call.Pointer<void>(3).IsNull()) throw std::invalid_argument("AStorageManager_unmountObb requires a callback");
    RecordStorageStub("AStorageManager_unmountObb", call.ThreadId());
    return 0;
}

BoundaryResult AndroidModule::AStorageManager_isObbMounted(const A32CallFrame& call) {
    RequireStorageManager(call);
    // Matches the process-local IMountService device state: no OBB is mounted.
    return 0;
}

BoundaryResult AndroidModule::AStorageManager_getMountedObbPath(const A32CallFrame& call) {
    RequireStorageManager(call);
    // The same IMountService state has no mounted path for any OBB.
    return 0;
}
}  // namespace ogplay::runtime
