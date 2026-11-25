#include "ogplay/runtime/common/guest_address_layout.h"
#include "runtime/boundary/modules/android/android_module.h"

#include <stdexcept>

#include "ogplay/core/logger.h"

namespace ogplay::runtime {

AndroidModule::AndroidModule(BoundaryCallServices& calls, AndroidBoundaryServices& services, AndroidLooperHooks hooks,
                             core::Logger* logger, core::CapabilityLedger* ledger, AndroidObbHooks obb)
    : looper_hooks_(std::move(hooks)), calls_(calls), services_(services), obb_hooks_(obb), logger_(logger),
      ledger_(ledger) {}

BoundaryCallServices& AndroidModule::CallServices() noexcept {
    return calls_;
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
    if (asset_managers_.size() >= 1024 || next_asset_manager_.Value() == kNativeActivityWindowHandleBegin.Value())
        throw std::runtime_error("AssetManager handle budget exhausted");
    const auto handle = next_asset_manager_;
    asset_managers_.emplace(handle.Value(), std::move(source));
    next_asset_manager_ = handle.Add(1);
    return handle;
}

std::shared_ptr<const AndroidAssetSource> AndroidModule::ResolveAsset(memory::GuestAddress token) {
    std::scoped_lock lock(mutex_);
    const auto found = assets_.find(token.Value());
    return found == assets_.end() ? nullptr : found->second->source;
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

BoundaryResult AndroidModule::PostNativeActivityCommand(const A32CallFrame& call, NativeActivityCommand command) {
    const auto activity = call.Pointer<void>(0);
    const auto warn_dropped = [&] {
        if (logger_)
            logger_->Write(core::LogLevel::warn, "runtime.native_activity.command",
                           "NativeActivity command discarded: Activity or queue unavailable",
                           {.guest_thread = call.ThreadId()}, {}, {.mode = core::RateLimitMode::first_and_count});
    };
    std::function<bool(NativeActivityCommand)> post;
    {
        std::scoped_lock lock(mutex_);
        const auto found = resources_.find(activity.Address().Value());
        if (found != resources_.end()) post = found->second.resources.post_command;
    }
    if (!post) {
        warn_dropped();
        throw std::invalid_argument("ANativeActivity is unregistered, retired or has no command queue");
    }
    if (!post(command)) warn_dropped();
    return 0;
}

void AndroidModule::UnregisterNativeActivity(memory::GuestAddress activity) {
    std::scoped_lock publish(surface_publish_mutex_);
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
        found->second.cpu_locked = false;
        found->second.cpu_buffer.reset();
        if (--found->second.references == 0) windows_.erase(found);
    }
    resource.window = memory::GuestAddress{};
}

memory::GuestAddress AndroidModule::SetNativeActivityWindow(memory::GuestAddress activity, bool active,
                                                            std::optional<AndroidSurfaceWindow> surface) {
    std::scoped_lock publish(surface_publish_mutex_);
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
    const auto geometry =
        surface.value_or(AndroidSurfaceWindow{memory::GuestAddress{}, item.resources.width, item.resources.height});
    windows_.emplace(window.Value(), WindowResource{geometry.width, geometry.height, 1, true, true, geometry.identity});
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

void AndroidModule::SetMotionEventLookup(AndroidMotionEventLookup lookup) {
    std::scoped_lock lock(mutex_);
    motion_event_lookup_ = std::move(lookup);
}

std::shared_ptr<const AndroidBoundaryInput> AndroidModule::InputEvent(memory::GuestAddress handle) const {
    AndroidMotionEventLookup lookup;
    {
        std::scoped_lock lock(mutex_);
        if (!managed_activity_seen_) {
            const auto found = standalone_input_.inflight.find(handle.Value());
            if (found != standalone_input_.inflight.end()) return found->second.event;
        }
        for (const auto& [_, resource] : resources_) {
            if (!resource.input_active) continue;
            const auto found = resource.input.inflight.find(handle.Value());
            if (found != resource.input.inflight.end()) return found->second.event;
        }
        lookup = motion_event_lookup_;
    }
    // API 19 MotionEvent.mNativePtr is itself an AInputEvent*.
    if (lookup)
        if (auto event = lookup(handle)) return event;
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
        target->pending.push_back({std::make_shared<const AndroidBoundaryInput>(std::move(snapshot)), {}});
    }
    ready_.notify_all();
}

void AndroidModule::PushQueueInput(memory::GuestAddress queue, const AndroidBoundaryInput& input,
                                   std::function<void(bool)> finish) {
    auto snapshot = NormalizeAndroidInput(input);
    {
        std::scoped_lock lock(mutex_);
        auto& target = Queue(queue.Value()).input;
        if (target.pending.size() + target.inflight.size() >= 4096)
            throw std::runtime_error("input queue event budget exhausted");
        target.pending.push_back(
            {std::make_shared<const AndroidBoundaryInput>(std::move(snapshot)), std::move(finish)});
    }
    ready_.notify_all();
}

}  // namespace ogplay::runtime
