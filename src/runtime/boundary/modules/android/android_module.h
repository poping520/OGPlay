#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <optional>

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/hal/clock.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/services/android_boundary_services.h"

namespace ogplay::runtime {

class AndroidModule final {
public:
    AndroidModule(BoundaryCallServices& calls,
                  AndroidBoundaryServices& services, AndroidLooperHooks hooks = {});
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void NotifyFileWrite();
    memory::GuestAddress PrepareThreadLooper(std::uint64_t thread_id, std::uint32_t opts = 0);
    void RetireThreadLooper(std::uint64_t thread_id);
    void ShutdownLoopers();
    bool NativeWindowIsCurrent(memory::GuestAddress window);
    void PushInput(const AndroidBoundaryInput& input);
    void RegisterNativeActivity(NativeActivityBoundaryResources resources);
    void UnregisterNativeActivity(memory::GuestAddress activity);
    memory::GuestAddress SetNativeActivityWindow(memory::GuestAddress activity, bool active);
    void SetNativeActivityInput(memory::GuestAddress activity, bool active);


#define OGPLAY_DECLARE_ANDROID(name, id, count, method) \
    BoundaryResult method(const A32CallFrame& call);
    OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DECLARE_ANDROID)
#undef OGPLAY_DECLARE_ANDROID

private:
    std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args,
                          std::uint64_t thread_id);
    struct Looper {
        std::uint64_t owner{};
        std::uint32_t references{1};
        bool allow_non_callbacks{}, wake{}, retired{};
        struct Request { std::int32_t ident{}; std::uint32_t events{}; memory::GuestAddress data{}; };
        std::map<std::int32_t, Request> requests;
        std::int32_t last_fd{-1};
    };
    Looper& RequireLooper(memory::GuestAddress handle);
    void RetireThreadLooperLocked(std::uint64_t thread_id);
    void SweepLoopers();
    std::map<std::uint32_t, Looper> loopers_;
    std::map<std::uint64_t, memory::GuestAddress> thread_loopers_;
    memory::GuestAddress next_looper_{0x6e010000U};
    AndroidLooperHooks looper_hooks_;
    hal::RealtimeClock looper_clock_;
    bool loopers_shutdown_{};
    template <std::uint16_t FunctionId>
    BoundaryResult ExecuteExport(const A32CallFrame& call);

    BoundaryCallServices& calls_;
    AndroidBoundaryServices& services_;
    struct InputQueueState {
        memory::GuestAddress looper{};
        std::uint32_t ident{}, data{};
        std::deque<AndroidBoundaryInput> pending;
        std::map<std::uint32_t, AndroidBoundaryInput> inflight;
    };
    InputQueueState& InputQueue(std::uint32_t handle);
    const AndroidBoundaryInput& InputEvent(memory::GuestAddress handle) const;
    template <std::uint16_t FunctionId>
    BoundaryResult ReadInput(const A32CallFrame& call);
    struct Resource {
        NativeActivityBoundaryResources resources;
        memory::GuestAddress window{};
        bool input_active{}, input_attached{};
        InputQueueState input;

    };
    struct WindowResource {
        std::uint32_t width{}, height{}, references{1};
        bool active{}, owned{true};
    };
    std::map<std::uint32_t, WindowResource> windows_;
    memory::GuestAddress next_window_{kNativeActivityWindowHandleBegin};
    void RetireWindow(Resource& resource);
    struct Asset {
        memory::GuestAddress manager;
        std::vector<std::byte> bytes;
        std::size_t offset{};
    };
    bool managed_activity_seen_{};
    std::map<std::uint32_t, Resource> resources_;
    std::map<std::uint32_t, Asset> assets_;
    // Asset handles are opaque tokens, never dereferenced as guest structures.
    std::uint32_t next_asset_{0x6e100000U};
    Resource& Queue(std::uint32_t handle);
    WindowResource& Window(std::uint32_t handle);
    std::mutex mutex_;
    std::condition_variable ready_;
    InputQueueState standalone_input_;
    memory::GuestAddress next_input_{0x6e200000U};
};

}  // namespace ogplay::runtime
