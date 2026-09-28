#include "runtime/boundary/modules/android/android_module.h"

#include <bit>
#include <algorithm>
#include <limits>
#include <chrono>
#include <cstddef>
#include <stdexcept>

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kFakeConfiguration = 0x6e003000U;
constexpr std::uint32_t kFakeLooper = 0x6e003100U;
constexpr std::uint32_t kFakeInputEvent = 0x6e003200U;

std::uint32_t SignedResult(const std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}
}  // namespace

AndroidModule::AndroidModule(BoundaryCallServices& calls,
                             AndroidBoundaryServices& services) noexcept
    : calls_(calls), services_(services) {}

BoundaryCallServices& AndroidModule::CallServices() noexcept { return calls_; }

bool AndroidModule::NativeWindowIsCurrent(memory::GuestAddress window) {
    std::scoped_lock lock(mutex_);
    const auto found = windows_.find(window.Value());
    if (found != windows_.end()) return found->second.active;
    // Java SurfaceHolder and standalone-runner identities retain their existing
    // registry path. Reserved NativeActivity identities must never fall back.
    return window.Value() < kNativeActivityWindowHandleBegin.Value() ||
           window.Value() >= kNativeActivityWindowHandleEnd.Value();
}

void AndroidModule::NotifyFileWrite() {
    {
        std::scoped_lock lock(mutex_);
        ++pending_command_writes_;
    }
    ready_.notify_all();
}

void AndroidModule::RegisterNativeActivity(NativeActivityBoundaryResources resources) {
    std::scoped_lock lock(mutex_);
    if (resources.activity.IsNull() || resources.assets.IsNull() ||
        resources.queue.IsNull() || !resources.read_asset)
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
    const auto manager = found->second.resources.assets;
    std::erase_if(assets_, [manager](const auto& pair) { return pair.second.manager == manager; });
    RetireWindow(found->second);
    resources_.erase(found);
    inputs_.clear(); active_input_.reset();
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
    if (!active) { RetireWindow(item); return memory::GuestAddress{}; }
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
        inputs_.clear(); active_input_.reset();
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

void AndroidModule::PushInput(const AndroidBoundaryInput& input) {
    {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_ && std::ranges::none_of(resources_, [](const auto& pair) {
                return pair.second.input_active;
            })) return;
        inputs_.push_back(input);
    }
    ready_.notify_all();
}

std::uint32_t AndroidModule::PollAll(
    const std::array<std::uint32_t, 4>& args,
    const std::uint64_t thread_id) {
    const auto timeout = std::bit_cast<std::int32_t>(args[0]);
    std::unique_lock lock(mutex_);
    const auto has_source = [this] {
        return pending_command_writes_ != 0 || (!inputs_.empty() &&
            (!managed_activity_seen_ || std::ranges::any_of(resources_, [](const auto& pair) {
                return pair.second.input_active && pair.second.input_attached;
            })));
    };
    if (!has_source()) {
        if (timeout < 0) {
            ready_.wait(lock, has_source);
        } else if (timeout > 0) {
            ready_.wait_for(lock, std::chrono::milliseconds(timeout), has_source);
        }
    }
    std::uint32_t ident{};
    std::uint32_t data{};
    if (pending_command_writes_ != 0) {
        --pending_command_writes_;
        ident = command_ident_;
        data = command_data_;
    } else if (has_source() && !inputs_.empty()) {
        ident = input_ident_;
        data = input_data_;
    } else {
        return SignedResult(-1);
    }
    lock.unlock();
    services_.Write32(args[1], 0, thread_id);
    services_.Write32(args[2], 1, thread_id);
    services_.Write32(args[3], data, thread_id);
    return ident;
}

#if defined(_MSC_VER)
#pragma warning(push)
// VS 18.8 reports the discarded fallback of exhaustive if-constexpr
// instantiations as unreachable.
#pragma warning(disable : 4702)
#endif
template <std::uint16_t FunctionId>
std::uint32_t AndroidModule::ExecuteExport(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    if constexpr (FunctionId >= 13U && FunctionId <= 18U) {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_) {
            if (!active_input_ || args[0] != kFakeInputEvent)
                throw std::invalid_argument("AInputEvent is stale or unregistered");
            if constexpr (FunctionId == 14U || FunctionId == 15U)
                if (active_input_->type != AndroidBoundaryInputType::key)
                    throw std::invalid_argument("AKeyEvent requires a key event");
            if constexpr (FunctionId >= 16U) {
                if (active_input_->type == AndroidBoundaryInputType::key)
                    throw std::invalid_argument("AMotionEvent requires a pointer event");
                if constexpr (FunctionId == 17U || FunctionId == 18U)
                    if (args[1] != 0) throw std::invalid_argument("AMotionEvent has only one pointer");
            }
        }
    }
    if constexpr (FunctionId == 0U) return kFakeConfiguration;
    if constexpr (FunctionId == 1U || FunctionId == 2U) return 0;
    if constexpr (FunctionId == 9U || FunctionId == 11U) {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_) {
            auto& item = Queue(args[0]);
            if constexpr (FunctionId == 9U) item.input_attached = false;
            if constexpr (FunctionId == 11U)
                if (!active_input_ || args[1] != kFakeInputEvent)
                    throw std::invalid_argument("invalid input event pre-dispatch");
        }
        return 0;
    }
    if constexpr (FunctionId == 19U || (FunctionId >= 26U && FunctionId <= 30U)) {
        std::scoped_lock lock(mutex_);
        if constexpr (FunctionId == 19U) {
            if (!managed_activity_seen_) return 0; // legacy standalone runner
        }
        auto& item = Window(args[0]);
        if constexpr (FunctionId == 19U) {
            // The single managed ANGLE surface has a fixed size and RGBA8 format.
            // NDK callers can reset to defaults or request that exact geometry.
            return item.active && ((args[1] == 0 && args[2] == 0) ||
                    (args[1] == item.width && args[2] == item.height)) &&
                    (args[3] == 0 || args[3] == 1) ? 0U : SignedResult(-22);
        }
        if constexpr (FunctionId == 26U) return item.width;
        if constexpr (FunctionId == 27U) return item.height;
        if constexpr (FunctionId == 28U) return 1;
        if constexpr (FunctionId == 29U) { ++item.references; return 0; }
        if constexpr (FunctionId == 30U) {
            if (item.references <= (item.owned ? 1U : 0U)) throw std::invalid_argument("unbalanced ANativeWindow_release");
            if (--item.references == 0) windows_.erase(args[0]);
            return 0;
        }
    }
    if constexpr (FunctionId == 20U) {
        std::function<std::optional<std::vector<std::byte>>(std::string_view)> read;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [_, item] : resources_)
                if (item.resources.assets.Value() == args[0]) read = item.resources.read_asset;
        }
        if (!read) throw std::invalid_argument("AAssetManager is stale or unregistered");
        if (args[2] > 3) return 0;
        std::string name;
        for (std::size_t i=0; i<4096; ++i) {
            std::array<std::byte, 1> byte{};
            services_.address_space.Read(call.Pointer<std::byte>(1).Address().Add(i), byte, tid);
            if (byte[0] == std::byte{}) break;
            name.push_back(static_cast<char>(byte[0]));
            if (i == 4095) throw std::invalid_argument("asset path exceeds budget");
        }
        auto bytes = read(name);
        if (!bytes) return 0;
        if (bytes->size() > 64U*1024U*1024U) throw std::runtime_error("NDK asset exceeds 64 MiB budget");
        std::scoped_lock lock(mutex_);
        if (std::ranges::none_of(resources_, [&](const auto& pair) { return pair.second.resources.assets.Value() == args[0]; }))
            throw std::invalid_argument("AAssetManager retired during open");
        if (next_asset_ == 0x6f000000U) throw std::runtime_error("NDK asset handle budget exhausted");
        const auto handle = next_asset_++;
        assets_.emplace(handle, Asset{memory::GuestAddress{args[0]}, std::move(*bytes), 0});
        return handle;
    }
    if constexpr (FunctionId >= 21U && FunctionId <= 25U) {
        std::scoped_lock lock(mutex_);
        const auto found = assets_.find(args[0]);
        if (found == assets_.end()) throw std::invalid_argument("AAsset is stale or unregistered");
        auto& asset = found->second;
        if constexpr (FunctionId == 21U) {
            const auto count = std::min<std::size_t>(args[2], asset.bytes.size() - asset.offset);
            if (count > INT32_MAX) return SignedResult(-1);
            services_.address_space.Write(call.Pointer<std::byte>(1).Address(),
                std::span(asset.bytes).subspan(asset.offset, count), tid);
            asset.offset += count;
            return static_cast<std::uint32_t>(count);
        }
        if constexpr (FunctionId == 22U) { assets_.erase(found); return 0; }
        if constexpr (FunctionId == 23U) return static_cast<std::uint32_t>(asset.bytes.size());
        if constexpr (FunctionId == 24U) return static_cast<std::uint32_t>(asset.bytes.size() - asset.offset);
        if constexpr (FunctionId == 25U) {
            if (args[2] > 2) return SignedResult(-1);
            const auto base = args[2] == 0 ? 0 : args[2] == 1 ? asset.offset : asset.bytes.size();
            const auto offset = static_cast<std::int64_t>(base) + std::bit_cast<std::int32_t>(args[1]);
            if (offset < 0 || static_cast<std::uint64_t>(offset) > asset.bytes.size()) return SignedResult(-1);
            asset.offset = static_cast<std::size_t>(offset);
            return static_cast<std::uint32_t>(offset);
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
    if constexpr (FunctionId == 5U) return kFakeLooper;
    if constexpr (FunctionId == 6U) {
        std::scoped_lock lock(mutex_);
        command_ident_ = args[2];
        command_data_ = call.Argument(5);
        return 1;
    }
    if constexpr (FunctionId == 7U) return PollAll(args, tid);
    if constexpr (FunctionId == 8U) {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_) {
            auto& item = Queue(args[0]);
            if (args[3] != 0) throw std::invalid_argument("AInputQueue callback looper is unsupported; use ident polling");
            item.input_attached = true;
        }
        input_ident_ = args[2];
        input_data_ = call.Argument(4);
        return 0;
    }
    if constexpr (FunctionId == 10U) {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_) static_cast<void>(Queue(args[0]));
        if (inputs_.empty() || active_input_.has_value()) return SignedResult(-1);
        active_input_ = inputs_.front();
        inputs_.pop_front();
        services_.Write32(args[1], kFakeInputEvent, tid);
        return 0;
    }
    if constexpr (FunctionId == 12U) {
        std::scoped_lock lock(mutex_);
        if (managed_activity_seen_) {
            static_cast<void>(Queue(args[0]));
            if (args[1] != kFakeInputEvent || !active_input_) throw std::invalid_argument("invalid input event completion");
        }
        active_input_.reset();
        return 0;
    }
    if constexpr (FunctionId == 13U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value() &&
                       active_input_->type == AndroidBoundaryInputType::key
                   ? 1U : 2U;
    }
    if constexpr (FunctionId == 14U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value() && active_input_->pressed ? 0U : 1U;
    }
    if constexpr (FunctionId == 15U) {
        std::scoped_lock lock(mutex_);
        return active_input_.has_value()
                   ? static_cast<std::uint32_t>(active_input_->code) : 0U;
    }
    if constexpr (FunctionId == 16U) {
        std::scoped_lock lock(mutex_);
        if (!active_input_.has_value() ||
            active_input_->type == AndroidBoundaryInputType::pointer_motion) {
            return 2U;
        }
        return active_input_->pressed ? 0U : 1U;
    }
    if constexpr (FunctionId == 17U || FunctionId == 18U) {
        std::scoped_lock lock(mutex_);
        const auto value = !active_input_.has_value()
                               ? 0.0F
                               : FunctionId == 17U ? active_input_->x
                                                   : active_input_->y;
        return std::bit_cast<std::uint32_t>(value);
    }
    throw std::logic_error("unbound concrete libandroid export");
}

#define OGPLAY_DEFINE_ANDROID(name, id, count, method) \
    std::uint32_t AndroidModule::method(const A32CallFrame& call) { \
        return ExecuteExport<id>(call); \
    }
OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DEFINE_ANDROID)
#undef OGPLAY_DEFINE_ANDROID
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace ogplay::runtime
