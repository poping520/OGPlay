#pragma once

#include "ogplay/runtime/common/guest_address_layout.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <map>
#include <optional>
#include <set>

#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "ogplay/hal/clock.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/services/android_boundary_services.h"

namespace ogplay::runtime {

class AndroidModule final : public AndroidSensorQueues {
public:
    AndroidModule(BoundaryCallServices& calls, AndroidBoundaryServices& services, AndroidLooperHooks hooks = {},
                  core::Logger* logger = nullptr, core::CapabilityLedger* ledger = nullptr, AndroidObbHooks obb = {});
    ~AndroidModule();
    void SetSurfaceHooks(AndroidSurfaceHooks hooks);
    void NotifySurfaceBufferQueued(memory::GuestAddress window, const AndroidSurfaceWindow& expected);
    void RetireSurfaceWindow(memory::GuestAddress identity);
    memory::GuestAddress AcquireSurfaceWindow(const AndroidSurfaceWindow& surface);
    void UpdateSurfaceWindow(const AndroidSurfaceWindow& surface);
    void SetSensorDirectory(std::shared_ptr<const AndroidSensorDirectory> directory,
                            std::shared_ptr<AndroidSensorSource> source);
    std::uint32_t OpenSensorQueue(std::uint64_t thread, std::function<void()> on_readable) override;
    void CloseSensorQueue(std::uint32_t queue) override;
    bool EnableSensor(std::uint32_t queue, std::int32_t handle, std::int32_t period_us) override;
    bool DisableSensor(std::uint32_t queue, std::int32_t handle) override;
    bool FlushSensors(std::uint32_t queue) override;
    std::size_t ReadSensors(std::uint32_t queue, std::span<AndroidSensorSample> events) override;
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void NotifyFileWrite();
    memory::GuestAddress PrepareThreadLooper(std::uint64_t thread_id, std::uint32_t opts = 0);
    void PollThreadLooper(std::uint64_t thread_id, const std::function<bool()>& interrupted);
    void WakeThreadLooper(std::uint64_t thread_id);
    void NotifyLooperPollers();

    [[nodiscard]] bool LooperCanInvokeGuest() const { return static_cast<bool>(looper_hooks_.invoke_callback); }

    void RetireThreadLooper(std::uint64_t thread_id);
    void ShutdownLoopers();
    std::optional<AndroidSurfaceWindow> NativeWindowGeometry(memory::GuestAddress window);
    bool ConnectEglWindow(memory::GuestAddress window, const AndroidSurfaceWindow& geometry);
    void DisconnectEglWindow(memory::GuestAddress window, const AndroidSurfaceWindow& geometry);
    bool PublishSurfacePixels(memory::GuestAddress window, const AndroidSurfaceWindow& expected,
                              std::shared_ptr<video::VideoFrame> pixels);
    void PushInput(const AndroidBoundaryInput& input);
    void PushQueueInput(memory::GuestAddress queue, const AndroidBoundaryInput& input,
                        std::function<void(bool)> finish);
    void RegisterNativeActivity(NativeActivityBoundaryResources resources);
    void UnregisterNativeActivity(memory::GuestAddress activity);
    memory::GuestAddress SetNativeActivityWindow(memory::GuestAddress activity, bool active,
                                                 std::optional<AndroidSurfaceWindow> surface = std::nullopt);
    void SetNativeActivityInput(memory::GuestAddress activity, bool active);
    void SetAssetManagerHooks(AndroidAssetManagerHooks hooks);
    void RetireObbResources();
    memory::GuestAddress RegisterAssetManager(std::shared_ptr<AndroidAssetManagerSource> source);
    void UnregisterAssetManager(memory::GuestAddress manager);
    [[nodiscard]] std::shared_ptr<const AndroidAssetSource> ResolveAsset(memory::GuestAddress token);

#define OGPLAY_DECLARE_ANDROID(name, id, count, method) BoundaryResult method(const A32CallFrame& call);
    OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_DECLARE_ANDROID)
#undef OGPLAY_DECLARE_ANDROID

private:
    std::uint32_t PollAll(const std::array<std::uint32_t, 4>& args, std::uint64_t thread_id,
                          const std::function<bool()>& interrupted = {}, bool all = false);

    struct Looper {
        std::uint64_t owner{};
        std::uint32_t references{1};
        bool allow_non_callbacks{}, wake{}, retired{};

        struct Request {
            std::int32_t ident{};
            std::uint32_t events{};
            memory::GuestAddress data{}, callback{};
            std::uint64_t generation{};
            std::function<void()> on_readable;
        };

        std::map<std::int32_t, Request> requests;
        std::int32_t last_fd{-1};
        std::uint64_t generation{};
    };

    std::int32_t AddLooperFdLocked(memory::GuestAddress looper, std::int32_t fd, std::int32_t ident,
                                   std::uint32_t events, memory::GuestAddress callback, memory::GuestAddress data,
                                   std::function<void()> on_readable = {});
    std::int32_t RemoveLooperFdLocked(memory::GuestAddress looper, std::int32_t fd);
    Looper& RequireLooper(memory::GuestAddress handle);
    void RetireThreadLooperLocked(std::uint64_t thread_id);
    void SweepLoopers();
    std::map<std::uint32_t, Looper> loopers_;
    std::map<std::uint64_t, memory::GuestAddress> thread_loopers_;
    memory::GuestAddress next_looper_{guest_layout::kLooperHandleBegin};
    AndroidLooperHooks looper_hooks_;
    hal::RealtimeClock looper_clock_;
    bool loopers_shutdown_{};

    BoundaryCallServices& calls_;
    AndroidBoundaryServices& services_;

    struct InputQueueState {
        struct Entry {
            AndroidBoundaryInput event;
            std::function<void(bool)> finish{};
        };

        memory::GuestAddress looper{};
        std::uint32_t ident{}, data{};
        std::deque<Entry> pending;
        std::map<std::uint32_t, Entry> inflight;
    };

    InputQueueState& InputQueue(std::uint32_t handle);
    const AndroidBoundaryInput& InputEvent(memory::GuestAddress handle) const;
    const AndroidBoundaryInput& KeyEvent(const A32CallFrame& call) const;
    const AndroidBoundaryInput& MotionEvent(const A32CallFrame& call) const;
    BoundaryResult ReadMotionAxis(const A32CallFrame& call, std::uint32_t axis, bool raw);
    BoundaryResult ReadHistoricalMotionAxis(const A32CallFrame& call, std::uint32_t axis, bool raw);

    BoundaryResult PostNativeActivityCommand(const A32CallFrame& call, NativeActivityCommand command);

    struct Resource {
        NativeActivityBoundaryResources resources;
        memory::GuestAddress window{};
        bool input_active{}, input_attached{};
        InputQueueState input{};
    };

    struct GuestBuffer;

    struct WindowResource {
        std::uint32_t width{}, height{}, references{1};
        bool active{}, owned{true};
        memory::GuestAddress surface_identity{};
        std::uint32_t format{1};
        std::unique_ptr<GuestBuffer> cpu_buffer{};
        std::uint32_t buffer_width{}, buffer_height{}, buffer_format{};
        bool cpu_locked{};
    };

    std::optional<AndroidSurfaceWindow> NativeWindowGeometryLocked(memory::GuestAddress window) const;
    using WindowConnectionKey = std::pair<memory::GuestAddress, std::uint64_t>;
    static WindowConnectionKey ProducerKey(memory::GuestAddress window, const AndroidSurfaceWindow& geometry);
    std::set<WindowConnectionKey> egl_windows_;
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
    BoundaryResult AssetLength(const A32CallFrame& call, bool remaining, bool wide);
    BoundaryResult SeekAsset(const A32CallFrame& call, bool wide);
    BoundaryResult OpenAssetFileDescriptor(const A32CallFrame& call, bool wide);

    struct ObbInfo {
        std::unique_ptr<GuestBuffer> package;
        std::uint32_t version{}, flags{};
        std::array<std::byte, 8> salt{};
    };

    ObbInfo& RequireObbInfo(GuestPtr<void> handle);
    void RequireStorageManager(const A32CallFrame& call);
    void RecordStorageStub(std::string_view name, std::uint64_t thread);
    AndroidObbHooks obb_hooks_;
    std::map<std::uint32_t, ObbInfo> obb_info_;
    std::set<std::uint32_t> storage_managers_;
    memory::GuestAddress next_obb_info_{guest_layout::kObbInfoHandleBegin};
    memory::GuestAddress next_storage_manager_{guest_layout::kStorageManagerHandleBegin};

    struct AssetDirectory {
        memory::GuestAddress manager;
        std::vector<AndroidAssetDirectoryEntry> entries;
        std::size_t index{};
        std::unique_ptr<GuestBuffer> cached_name;
    };

    AssetDirectory& RequireAssetDirectoryLocked(memory::GuestAddress handle);
    std::map<std::uint32_t, AssetDirectory> asset_directories_;
    memory::GuestAddress next_asset_directory_{guest_layout::kAssetDirectoryHandleBegin};

    struct Asset {
        std::shared_ptr<const AndroidAssetSource> source;
        std::uint64_t offset{};
        std::unique_ptr<GuestBuffer> buffer;
        std::mutex mutex;
    };

    std::shared_ptr<Asset> RequireAsset(memory::GuestAddress handle);

    bool managed_activity_seen_{};
    std::map<std::uint32_t, Resource> resources_;
    std::map<std::uint32_t, std::shared_ptr<Asset>> assets_;
    std::map<std::uint32_t, std::shared_ptr<AndroidAssetManagerSource>> asset_managers_;
    resources::ResTable_config& RequireConfiguration(GuestPtr<void> handle);
    BoundaryResult WriteConfigurationLocale(const A32CallFrame& call, char (resources::ResTable_config::*field)[2]);
    std::map<std::uint32_t, resources::ResTable_config> configurations_;
    memory::GuestAddress next_configuration_{guest_layout::kConfigurationHandleBegin};
    memory::GuestAddress next_asset_manager_{guest_layout::kAssetManagerHandleBegin};
    std::recursive_mutex asset_hook_mutex_;
    AndroidAssetManagerHooks asset_hooks_;
    AndroidSurfaceHooks surface_hooks_;
    std::recursive_mutex surface_hook_mutex_;
    std::mutex surface_publish_mutex_;
    // Asset handles are opaque tokens, never dereferenced as guest structures.
    std::uint32_t next_asset_{guest_layout::kAssetHandleBegin};
    Resource& Queue(std::uint32_t handle);
    WindowResource& Window(std::uint32_t handle);

    struct SensorQueue {
        std::uint64_t owner{};
        memory::GuestAddress looper;
        std::array<std::int32_t, 2> pipe;
        std::unique_ptr<AndroidSensorStream> stream;
        bool signaled{};
    };

    std::map<std::uint32_t, SensorQueue> sensor_queues_;
    memory::GuestAddress next_sensor_queue_{guest_layout::kSensorQueueHandleBegin};
    std::unique_ptr<GuestBuffer> sensor_list_;
    std::vector<std::array<memory::GuestAddress, 2>> sensor_strings_;
    std::shared_ptr<AndroidSensorSource> sensor_source_;
    std::shared_ptr<const AndroidSensorDirectory> sensor_directory_ = std::make_shared<const AndroidSensorDirectory>();
    core::Logger* logger_{};
    core::CapabilityLedger* ledger_{};
    void CheckSensorProcessLocked(std::uint64_t thread);
    void RequireSensorManager(const A32CallFrame& call);
    memory::GuestAddress SensorTokenLocked(std::size_t index) const;
    std::size_t SensorIndexLocked(memory::GuestAddress token, std::uint64_t thread);
    void RetireSensorQueues(std::optional<std::uint64_t> thread);
    std::uint32_t OpenSensorQueueLocked(std::uint64_t thread, memory::GuestAddress looper, int ident,
                                        memory::GuestAddress callback, memory::GuestAddress data,
                                        std::function<void()> on_readable = {});
    void CloseSensorQueueLocked(const SensorQueue& queue);
    void UpdateSensorQueueSignal(SensorQueue& queue);
    void BuildSensorList(std::uint64_t thread);
    SensorQueue& RequireSensorQueue(std::uint32_t key, std::uint64_t thread);
    [[noreturn]] void SensorFailure(std::string_view message, std::uint64_t thread);
    std::mutex mutex_;
    std::condition_variable ready_;
    InputQueueState standalone_input_;
    memory::GuestAddress next_input_{guest_layout::kInputHandleBegin};
};

}  // namespace ogplay::runtime
