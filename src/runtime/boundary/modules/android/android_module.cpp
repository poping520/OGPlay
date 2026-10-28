#include "runtime/boundary/modules/android/android_module.h"

#include <bit>
#include <algorithm>
#include <limits>
#include <cstddef>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kFakeConfiguration = 0x6e003000U;

std::uint32_t SignedResult(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
}  // namespace

AndroidModule::AndroidModule(BoundaryCallServices& calls, AndroidBoundaryServices& services, AndroidLooperHooks hooks,
                             core::Logger* logger, core::CapabilityLedger* ledger)
    : looper_hooks_(std::move(hooks)), calls_(calls), services_(services), logger_(logger), ledger_(ledger) {}

BoundaryCallServices& AndroidModule::CallServices() noexcept {
    return calls_;
}

bool AndroidModule::NativeWindowIsCurrent(memory::GuestAddress window) {
    std::scoped_lock lock(mutex_);
    const auto found = windows_.find(window.Value());
    if (found != windows_.end()) return found->second.active;
    if (const auto alias = surface_geometries_.find(window.Value()); alias != surface_geometries_.end())
        return alias->second.width != 0 && alias->second.height != 0;
    // Java SurfaceHolder and standalone-runner identities retain their existing
    // registry path. Reserved NativeActivity identities must never fall back.
    return window.Value() < kNativeActivityWindowHandleBegin.Value() ||
           window.Value() >= kNativeActivityWindowHandleEnd.Value();
}

void AndroidModule::NotifyFileWrite() {
    ready_.notify_all();
}

void AndroidModule::SetAssetManagerHooks(AndroidAssetManagerHooks hooks) {
    std::scoped_lock guard(asset_hook_mutex_);
    asset_hooks_ = hooks;
    if (!hooks.from_java) {
        // Owner retires hooks only after guest workers have stopped. Release
        // native leases too; no resource survives until a later VM destruction.
        std::scoped_lock lock(mutex_);
        asset_directories_.clear();
        asset_managers_.clear();
        assets_.clear();
    }
}

memory::GuestAddress AndroidModule::RegisterAssetManager(std::shared_ptr<AndroidAssetManagerSource> source) {
    if (!source) throw std::invalid_argument("AssetManager source is missing");
    std::scoped_lock guard(mutex_);
    if (asset_managers_.size() >= 1024 || next_asset_manager_.Value() == 0x6e400000U)
        throw std::runtime_error("AssetManager handle budget exhausted");
    const auto handle = next_asset_manager_;
    asset_managers_.emplace(handle.Value(), std::move(source));
    next_asset_manager_ = handle.Add(1);
    return handle;
}

void AndroidModule::UnregisterAssetManager(memory::GuestAddress manager) {
    std::scoped_lock guard(mutex_);
    // Existing Assets have independent backing leases, like API19 _FileAsset.
    std::erase_if(asset_directories_, [&](const auto& item) { return item.second.manager == manager; });
    asset_managers_.erase(manager.Value());
}

void AndroidModule::RegisterNativeActivity(NativeActivityBoundaryResources resources) {
    std::scoped_lock lock(mutex_);
    if (resources.activity.IsNull() || resources.assets.IsNull() || resources.queue.IsNull())
        throw std::invalid_argument("incomplete NativeActivity boundary resources");
    managed_activity_seen_ = true;
    const auto key = resources.activity.Value();
    if (resources_.contains(key)) throw std::logic_error("duplicate NativeActivity registration");
    if (!resources_.emplace(key, Resource{std::move(resources)}).second)
        throw std::logic_error("duplicate NativeActivity boundary registration");
}

void AndroidModule::UnregisterNativeActivity(memory::GuestAddress activity) {
    std::scoped_lock lock(mutex_);
    const auto found = resources_.find(activity.Value());
    if (found == resources_.end()) return;
    RetireWindow(found->second);
    resources_.erase(found);
}

void AndroidModule::RetireWindow(Resource& resource) {
    if (resource.window.IsNull()) return;
    auto found = windows_.find(resource.window.Value());
    if (found != windows_.end()) {
        found->second.active = false;
        found->second.owned = false;
        if (--found->second.references == 0) windows_.erase(found);
    }
    resource.window = memory::GuestAddress{};
}

memory::GuestAddress AndroidModule::SetNativeActivityWindow(memory::GuestAddress activity, bool active) {
    std::scoped_lock lock(mutex_);
    auto& item = resources_.at(activity.Value());
    if (!active) {
        RetireWindow(item);
        return memory::GuestAddress{};
    }
    if (!item.window.IsNull()) throw std::logic_error("NativeActivity window is already active");
    if (next_window_.Value() >= kNativeActivityWindowHandleEnd.Value())
        throw std::runtime_error("NativeActivity window identity budget exhausted");
    const auto window = next_window_;
    next_window_ = next_window_.Add(4);
    windows_.emplace(window.Value(), WindowResource{item.resources.width, item.resources.height, 1, true, true});
    item.window = window;
    return window;
}

void AndroidModule::SetNativeActivityInput(memory::GuestAddress activity, bool active) {
    std::scoped_lock lock(mutex_);
    auto& item = resources_.at(activity.Value());
    item.input_active = active;
    if (!active) {
        item.input_attached = false;
        item.input = {};
    }
}

AndroidModule::Resource& AndroidModule::Queue(std::uint32_t handle) {
    for (auto& [_, item] : resources_)
        if (item.resources.queue.Value() == handle && item.input_active) return item;
    throw std::invalid_argument("AInputQueue is stale or unregistered");
}

AndroidModule::WindowResource& AndroidModule::Window(std::uint32_t handle) {
    const auto found = windows_.find(handle);
    if (found != windows_.end()) return found->second;
    throw std::invalid_argument("ANativeWindow is stale or unregistered");
}

AndroidModule::InputQueueState& AndroidModule::InputQueue(std::uint32_t handle) {
    return managed_activity_seen_ ? Queue(handle).input : standalone_input_;
}

const AndroidBoundaryInput& AndroidModule::InputEvent(memory::GuestAddress handle) const {
    if (!managed_activity_seen_) {
        const auto found = standalone_input_.inflight.find(handle.Value());
        if (found != standalone_input_.inflight.end()) return found->second;
    }
    for (const auto& [_, resource] : resources_) {
        if (!resource.input_active) continue;
        const auto found = resource.input.inflight.find(handle.Value());
        if (found != resource.input.inflight.end()) return found->second;
    }
    throw std::invalid_argument("AInputEvent is stale or unregistered");
}

void AndroidModule::PushInput(const AndroidBoundaryInput& input) {
    auto snapshot = NormalizeAndroidInput(input);
    {
        std::scoped_lock lock(mutex_);
        InputQueueState* target = &standalone_input_;
        if (managed_activity_seen_) {
            target = nullptr;
            for (auto& [_, resource] : resources_) {
                if (!resource.input_active) continue;
                if (target) throw std::logic_error("multiple active input queue owners");
                target = &resource.input;
            }
            if (!target) return;
        }
        if (target->pending.size() + target->inflight.size() >= 4096)
            throw std::runtime_error("input queue event budget exhausted");
        target->pending.push_back(std::move(snapshot));
    }
    ready_.notify_all();
}

#if defined(_MSC_VER)
#pragma warning(push)
// VS 18.8 reports the discarded fallback of exhaustive if-constexpr
// instantiations as unreachable.
#pragma warning(disable : 4702)
#endif
template <std::uint16_t FunctionId>
BoundaryResult AndroidModule::ExecuteExport(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    if constexpr (FunctionId == 100U) return SurfaceFromJava(call);
    if constexpr (FunctionId >= 101U && FunctionId <= 104U) return AssetExtension(call, FunctionId);
    if constexpr (FunctionId >= 85U && FunctionId <= 99U) return SensorExport(call, FunctionId);
    if constexpr (FunctionId == 83U) {
        std::scoped_lock guard(asset_hook_mutex_);
        if (!asset_hooks_.from_java) throw std::runtime_error("NDK AssetManager JNI backend is unavailable");
        return asset_hooks_
            .from_java(asset_hooks_.owner, tid, call.Pointer<void>(0).Address(), AssetManagerJavaReference{args[1]})
            .Value();
    }
    if constexpr ((FunctionId >= 13U && FunctionId <= 18U) || (FunctionId >= 37U && FunctionId <= 81U)) {
        return ReadInput<FunctionId>(call);
    }
    if constexpr (FunctionId == 0U) return kFakeConfiguration;
    if constexpr (FunctionId == 1U || FunctionId == 2U) return 0;
    if constexpr (FunctionId == 9U || FunctionId == 11U || FunctionId == 82U) {
        std::scoped_lock lock(mutex_);
        auto& queue = InputQueue(args[0]);
        if constexpr (FunctionId == 9U) {
            queue.looper = memory::GuestAddress{};
            if (managed_activity_seen_) Queue(args[0]).input_attached = false;
        }
        if constexpr (FunctionId == 11U)
            if (!queue.inflight.contains(args[1])) throw std::invalid_argument("invalid input event pre-dispatch");
        if constexpr (FunctionId == 82U) return queue.pending.empty() ? 0U : 1U;
        return 0;
    }
    if constexpr (FunctionId == 19U || (FunctionId >= 26U && FunctionId <= 30U)) {
        std::scoped_lock lock(mutex_);
        if constexpr (FunctionId == 19U) {
            if (!managed_activity_seen_ && !windows_.contains(args[0])) return 0;  // legacy standalone runner
        }
        auto& item = Window(args[0]);
        if constexpr (FunctionId == 19U) {
            // The single managed ANGLE surface has a fixed size and RGBA8 format.
            // NDK callers can reset to defaults or request that exact geometry.
            return item.active &&
                           ((args[1] == 0 && args[2] == 0) || (args[1] == item.width && args[2] == item.height)) &&
                           (args[3] == 0 || args[3] == 1)
                       ? 0U
                       : SignedResult(-22);
        }
        if constexpr (FunctionId == 26U) return item.width;
        if constexpr (FunctionId == 27U) return item.height;
        if constexpr (FunctionId == 28U) return 1;
        if constexpr (FunctionId == 29U) {
            if (item.references == UINT32_MAX) throw std::overflow_error("ANativeWindow reference overflow");
            ++item.references;
            return 0;
        }
        if constexpr (FunctionId == 30U) {
            if (item.references <= (item.owned ? 1U : 0U))
                throw std::invalid_argument("unbalanced ANativeWindow_release");
            if (--item.references == 0) windows_.erase(args[0]);
            return 0;
        }
    }
    if constexpr (FunctionId == 20U) {
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
        if (assets_.size() >= 4096 || next_asset_ == 0x6e200000U)
            throw std::runtime_error("NDK asset handle budget exhausted");
        const auto handle = next_asset_++;
        auto asset = std::make_shared<Asset>();
        asset->source = source;
        assets_.emplace(handle, std::move(asset));
        return handle;
    }
    if constexpr ((FunctionId >= 21U && FunctionId <= 25U) || FunctionId == 84U) {
        std::shared_ptr<Asset> owner;
        {
            std::scoped_lock lock(mutex_);
            const auto found = assets_.find(args[0]);
            if (found == assets_.end()) throw std::invalid_argument("AAsset is stale or unregistered");
            owner = found->second;
            if constexpr (FunctionId == 22U) {
                assets_.erase(found);
                return 0;
            }
        }
        auto& asset = *owner;
        std::scoped_lock guard(asset.mutex);
        if constexpr (FunctionId == 21U) {
            const auto count = std::min<std::uint64_t>(args[2], asset.source->Length() - asset.offset);
            if (count > INT32_MAX) return SignedResult(-1);
            const auto output = call.Pointer<std::byte>(1).Address();
            if (count != 0)
                services_.address_space.Validate(memory::GuestRange(output, count), memory::AccessType::write, tid);
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
        if constexpr (FunctionId == 23U) return static_cast<std::uint32_t>(asset.source->Length());
        if constexpr (FunctionId == 24U) return static_cast<std::uint32_t>(asset.source->Length() - asset.offset);
        if constexpr (FunctionId == 25U) {
            if (args[2] > 2) return SignedResult(-1);
            const auto base = args[2] == 0 ? 0 : args[2] == 1 ? asset.offset : asset.source->Length();
            const auto offset = static_cast<std::int64_t>(base) + std::bit_cast<std::int32_t>(args[1]);
            if (offset < 0 || static_cast<std::uint64_t>(offset) > asset.source->Length()) return SignedResult(-1);
            asset.offset = static_cast<std::uint64_t>(offset);
            return static_cast<std::uint32_t>(offset);
        }
        if constexpr (FunctionId == 84U) {
            const auto start = services_.address_space.PreflightWrite(
                memory::GuestRange(call.Pointer<std::uint32_t>(1).Address(), 4), tid);
            const auto length = services_.address_space.PreflightWrite(
                memory::GuestRange(call.Pointer<std::uint32_t>(2).Address(), 4), tid);
            const auto descriptor = asset.source->OpenFileDescriptor();
            if (descriptor.fd < 0) return SignedResult(descriptor.fd);
            const auto encode = [](std::uint64_t value) {
                std::array<std::byte, 4> bytes{};
                for (std::size_t i = 0; i < bytes.size(); ++i)
                    bytes[i] = static_cast<std::byte>(value >> (i * 8));
                return bytes;
            };
            try {
                services_.address_space.WritePrevalidated(start, encode(descriptor.start));
                services_.address_space.WritePrevalidated(length, encode(descriptor.length));
            } catch (...) {
                asset.source->CloseFileDescriptor(descriptor.fd);
                throw;
            }
            return SignedResult(descriptor.fd);
        }
    }
    if constexpr (FunctionId == 3U || FunctionId == 4U) {
        const auto output = call.Pointer<std::byte>(1);
        if (!output.IsNull()) {
            const std::array bytes{std::byte{'e'}, std::byte{'n'}};
            services_.address_space.Write(output.Address(), bytes, tid);
        }
        return 0;
    }
    if constexpr (FunctionId == 5U) return PrepareThreadLooper(tid, args[0]).Value();
    if constexpr (FunctionId == 31U) {
        std::scoped_lock lock(mutex_);
        SweepLoopers();
        const auto found = thread_loopers_.find(tid);
        return found == thread_loopers_.end() ? 0 : found->second.Value();
    }
    if constexpr (FunctionId == 32U || FunctionId == 33U || FunctionId == 35U || FunctionId == 36U) {
        std::scoped_lock lock(mutex_);
        SweepLoopers();
        auto& looper = RequireLooper(call.Pointer<void>(0).Address());
        if constexpr (FunctionId == 32U) {
            if (looper.references == UINT32_MAX) throw std::overflow_error("ALooper reference overflow");
            ++looper.references;
        }
        if constexpr (FunctionId == 33U) {
            const auto owner_reference = looper.retired ? 0U : 1U;
            if (looper.references <= owner_reference) throw std::invalid_argument("unbalanced ALooper_release");
            if (--looper.references == 0) loopers_.erase(args[0]);
        }
        if constexpr (FunctionId == 35U) {
            looper.wake = true;
            ready_.notify_all();
        }
        if constexpr (FunctionId == 36U)
            return SignedResult(
                RemoveLooperFdLocked(call.Pointer<void>(0).Address(), std::bit_cast<std::int32_t>(args[1])));
        return 0;
    }
    if constexpr (FunctionId == 6U) {
        std::scoped_lock lock(mutex_);
        SweepLoopers();
        return SignedResult(AddLooperFdLocked(call.Pointer<void>(0).Address(), std::bit_cast<std::int32_t>(args[1]),
                                              std::bit_cast<std::int32_t>(args[2]), args[3],
                                              call.Pointer<void>(4).Address(), call.Pointer<void>(5).Address()));
    }
    if constexpr (FunctionId == 7U || FunctionId == 34U) return PollAll(args, tid);
    if constexpr (FunctionId == 8U) {
        std::scoped_lock lock(mutex_);
        auto& looper = RequireLooper(call.Pointer<void>(1).Address());
        if (looper.retired || args[3] != 0 || static_cast<std::int32_t>(args[2]) < 0)
            throw std::invalid_argument("AInputQueue requires a live ident-polling looper");
        if (managed_activity_seen_) {
            auto& item = Queue(args[0]);
            item.input_attached = true;
        }
        auto& queue = InputQueue(args[0]);
        queue.looper = call.Pointer<void>(1).Address();
        queue.ident = args[2];
        queue.data = call.Argument(4);
        ready_.notify_all();
        return 0;
    }
    if constexpr (FunctionId == 10U) {
        std::scoped_lock lock(mutex_);
        auto& queue = InputQueue(args[0]);
        if (call.Pointer<void>(1).IsNull())
            throw std::invalid_argument("AInputQueue_getEvent requires an output pointer");
        if (queue.pending.empty()) return SignedResult(-11);  // EAGAIN
        if (next_input_.Value() >= 0x6e400000U) throw std::runtime_error("input event identity budget exhausted");
        const auto handle = next_input_;
        // Publish only after the guest destination and host allocation succeed.
        queue.inflight.emplace(handle.Value(), queue.pending.front());
        try {
            services_.Write32(args[1], handle.Value(), tid);
        } catch (...) {
            queue.inflight.erase(handle.Value());
            throw;
        }
        next_input_ = next_input_.Add(4);
        queue.pending.pop_front();
        return 0;
    }
    if constexpr (FunctionId == 12U) {
        std::scoped_lock lock(mutex_);
        auto& queue = InputQueue(args[0]);
        if (queue.inflight.erase(args[1]) == 0) throw std::invalid_argument("invalid input event completion");
        return 0;
    }
    throw std::logic_error("unbound concrete libandroid export");
}

#define OGPLAY_DEFINE_ANDROID(name, id, count, method)                                                                 \
    BoundaryResult AndroidModule::method(const A32CallFrame& call) {                                                   \
        return ExecuteExport<id>(call);                                                                                \
    }
OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DEFINE_ANDROID)
#undef OGPLAY_DEFINE_ANDROID
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace ogplay::runtime
