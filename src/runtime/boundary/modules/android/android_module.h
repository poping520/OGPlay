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
    AndroidModule(BoundaryCallServices& calls, AndroidBoundaryServices& services, AndroidLooperHooks hooks = {},
                  core::Logger* logger = nullptr, core::CapabilityLedger* ledger = nullptr);
    ~AndroidModule();
    void SetSurfaceHooks(AndroidSurfaceHooks hooks);
    void RetireSurfaceWindow(memory::GuestAddress identity);
    memory::GuestAddress AcquireSurfaceWindow(const AndroidSurfaceWindow& surface);
    void UpdateSurfaceWindow(const AndroidSurfaceWindow& surface);
    void SetSensorDirectory(std::shared_ptr<const AndroidSensorDirectory> directory);
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void NotifyFileWrite();
    memory::GuestAddress PrepareThreadLooper(std::uint64_t thread_id, std::uint32_t opts = 0);
    void RetireThreadLooper(std::uint64_t thread_id);
    void ShutdownLoopers();
    bool NativeWindowIsCurrent(memory::GuestAddress window);
    std::optional<AndroidSurfaceWindow> NativeWindowGeometry(memory::GuestAddress window);
    bool PublishSurfacePixels(memory::GuestAddress window, std::uint32_t width, std::uint32_t height,
                              std::vector<std::uint8_t> pixels);
    void PushInput(const AndroidBoundaryInput& input);
    void RegisterNativeActivity(NativeActivityBoundaryResources resources);
    void UnregisterNativeActivity(memory::GuestAddress activity);
    memory::GuestAddress SetNativeActivityWindow(memory::GuestAddress activity, bool active);
    void SetNativeActivityInput(memory::GuestAddress activity, bool active);
    void SetAssetManagerHooks(AndroidAssetManagerHooks hooks);
    memory::GuestAddress RegisterAssetManager(std::shared_ptr<AndroidAssetManagerSource> source);
    void UnregisterAssetManager(memory::GuestAddress manager);

#define OGPLAY_DECLARE_ANDROID(name, id, count, method) BoundaryResult method(const A32CallFrame& call);
    OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DECLARE_ANDROID)
#undef OGPLAY_DECLARE_ANDROID

private:
    std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args, std::uint64_t thread_id);

    struct Looper {
        std::uint64_t owner{};
        std::uint32_t references{1};
        bool allow_non_callbacks{}, wake{}, retired{};

        struct Request {
            std::int32_t ident{};
            std::uint32_t events{};
            memory::GuestAddress data{};
        };

        std::map<std::int32_t, Request> requests;
        std::int32_t last_fd{-1};
    };

    std::int32_t AddLooperFdLocked(memory::GuestAddress looper, std::int32_t fd, std::int32_t ident,
                                   std::uint32_t events, memory::GuestAddress callback, memory::GuestAddress data);
    std::int32_t RemoveLooperFdLocked(memory::GuestAddress looper, std::int32_t fd);
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
        InputQueueState input{};
    };

    struct WindowResource {
        std::uint32_t width{}, height{}, references{1};
        bool active{}, owned{true};
        memory::GuestAddress surface_identity{};
    };

    std::map<std::uint32_t, WindowResource> windows_;
    std::map<std::uint32_t, AndroidSurfaceWindow> surface_geometries_;
    memory::GuestAddress next_window_{kNativeActivityWindowHandleBegin};
    void RetireWindow(Resource& resource);

    struct GuestBuffer {
        memory::AddressSpace& memory;
        memory::GuestRange range;
        GuestBuffer(memory::AddressSpace& memory, std::uint64_t size);
        ~GuestBuffer();
    };

    std::unique_ptr<GuestBuffer> ReadOnlyBuffer(std::span<const std::byte> bytes, std::uint64_t thread);
    std::string AssetPath(const A32CallFrame& call, std::size_t argument);
    BoundaryResult AssetExtension(const A32CallFrame& call, std::uint16_t id);

    struct AssetDirectory {
        memory::GuestAddress manager;
        std::vector<AndroidAssetDirectoryEntry> entries;
        std::size_t index{};
        std::unique_ptr<GuestBuffer> cached_name;
    };

    std::map<std::uint32_t, AssetDirectory> asset_directories_;
    memory::GuestAddress next_asset_directory_{0x6ea00000U};

    struct Asset {
        std::shared_ptr<const AndroidAssetSource> source;
        std::uint64_t offset{};
        std::unique_ptr<GuestBuffer> buffer;
        std::mutex mutex;
    };

    bool managed_activity_seen_{};
    std::map<std::uint32_t, Resource> resources_;
    std::map<std::uint32_t, std::shared_ptr<Asset>> assets_;
    std::map<std::uint32_t, std::shared_ptr<AndroidAssetManagerSource>> asset_managers_;
    memory::GuestAddress next_asset_manager_{0x6e300000U};
    std::recursive_mutex asset_hook_mutex_;
    AndroidAssetManagerHooks asset_hooks_;
    AndroidSurfaceHooks surface_hooks_;
    std::recursive_mutex surface_hook_mutex_;
    std::mutex surface_publish_mutex_;
    BoundaryResult SurfaceFromJava(const A32CallFrame& call);
    // Asset handles are opaque tokens, never dereferenced as guest structures.
    std::uint32_t next_asset_{0x6e100000U};
    Resource& Queue(std::uint32_t handle);
    WindowResource& Window(std::uint32_t handle);

    struct SensorQueue {
        std::uint64_t owner{};
        memory::GuestAddress looper;
        std::array<std::int32_t, 2> pipe;
    };

    std::map<std::uint32_t, SensorQueue> sensor_queues_;
    memory::GuestAddress next_sensor_queue_{0x6e900000U};
    memory::GuestAddress sensor_list_{};
    std::shared_ptr<const AndroidSensorDirectory> sensor_directory_ = std::make_shared<const AndroidSensorDirectory>();
    core::Logger* logger_{};
    core::CapabilityLedger* ledger_{};
    BoundaryResult SensorExport(const A32CallFrame& call, std::uint16_t id);
    void RetireSensorQueues(std::optional<std::uint64_t> thread);
    void CloseSensorQueue(const SensorQueue& queue);
    [[noreturn]] void SensorFailure(std::string_view message, std::uint64_t thread);
    std::mutex mutex_;
    std::condition_variable ready_;
    InputQueueState standalone_input_;
    memory::GuestAddress next_input_{0x6e200000U};
};

}  // namespace ogplay::runtime
