#pragma once

#include "rom_window.h"

namespace ogplay::runtime {
namespace android_intrinsics {
class VelocityTracker;
}

// Immutable MotionEvent snapshots keyed by mNativePtr. VM-lock holders write;
// NDK getters read from any guest thread, so each change publishes a new snapshot.
class RomMotionEvents final {
public:
    [[nodiscard]] std::shared_ptr<const AndroidBoundaryInput> Find(int handle) const {
        std::scoped_lock lock(mutex_);
        const auto found = events_.find(handle);
        return found == events_.end() ? nullptr : found->second;
    }

    void Publish(int handle, AndroidBoundaryInput event) {
        auto snapshot = std::make_shared<const AndroidBoundaryInput>(std::move(event));
        std::scoped_lock lock(mutex_);
        events_.insert_or_assign(handle, std::move(snapshot));
    }

    void Erase(int handle) {
        std::scoped_lock lock(mutex_);
        events_.erase(handle);
    }

private:
    mutable std::mutex mutex_;
    std::map<int, std::shared_ptr<const AndroidBoundaryInput>> events_;
};

struct RomInputState {
    struct Event {
        int sequence{};
        AndroidBoundaryInput input;
    };

    struct Channel {
        std::string name;
        std::deque<Event> pending;
        bool batch_notified{};
        bool local_ime{};
        std::size_t inflight{};
    };

    struct Receiver {
        dexvm::VmObjectRef weak{}, queue{};
        std::uint64_t thread{};
        std::shared_ptr<Channel> channel;
        std::set<int> inflight;
    };

    struct Endpoint {
        std::shared_ptr<Channel> channel;
        bool server{};
    };

    std::map<int, Endpoint> endpoints;
    std::map<int, Receiver> receivers;

    struct ImeSender {
        dexvm::VmObjectRef weak{}, queue{};
        std::uint64_t thread{};
        std::deque<int> pending;
    };

    std::map<int, ImeSender> ime_senders;
    int next_handle{1}, next_sequence{1};
    const std::shared_ptr<RomMotionEvents> motions{std::make_shared<RomMotionEvents>()};
    std::map<int, std::shared_ptr<android_intrinsics::VelocityTracker>> velocities;

    struct NativeQueue {
        dexvm::VmObjectRef weak{}, queue{};
        std::uint64_t thread{};
        memory::GuestAddress native{};
        std::function<void(const AndroidBoundaryInput&, std::function<void(bool)>)> post;
        std::mutex mutex;
        std::deque<std::pair<int, bool>> finished;
    };

    std::map<int, std::shared_ptr<NativeQueue>> native_queues;

    struct ImeClient {
        dexvm::VmObjectRef client{}, context{};
    };

    std::map<std::uint32_t, ImeClient> ime_clients;
    dexvm::VmObjectRef active_client{}, active_context{}, focused_window{};
    dexvm::VmObjectRef window_focus{};
    dexvm::VmObjectRef ime_method{}, ime_channel{};
    bool ime_visible{};
    int ime_sequence{};
    std::function<void(bool)> text_input;
};

namespace android_intrinsics {
RomInputState& RomInput(const Context& context);
int AllocateRomInputHandle(RomInputState& state);
int AllocateRomInputSequence(RomInputState& state);
int CreateWindowInputChannel(dx::Interpreter& vm, const Context& context, dx::VmObjectRef out);
void CloseWindowInputChannel(const Context& context, int token);
void DispatchRomWindowInput(dx::Interpreter& vm, const Context& context, const AndroidBoundaryInput& input);
void QueueRomWindowInput(dx::Interpreter& vm, const Context& context, std::span<const WindowInputDelivery> deliveries);
void PumpRomInputEvents(dx::Interpreter& vm, const Context& context);
void CommitRomInputText(dx::Interpreter& vm, const Context& context, dx::VmObjectRef text, bool composing);
void CommitRomInputText(dx::Interpreter& vm, const Context& context, const std::u16string& text, bool composing);
void SetRomInputWindowFocus(dx::Interpreter& vm, const Context& context, dx::VmObjectRef window);
void VisitRomInputRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit);
void AddRomMotionNatives(dx::IntrinsicClassBuilder& builder, const Context& context);
void AddRomInputQueueNatives(dx::IntrinsicClassBuilder& builder, const Context& context);
AndroidBoundaryInput ReadMotionEvent(dx::Interpreter& vm, dx::VmObjectRef event);
dx::VmObjectRef MakeInputEvent(dx::Interpreter& vm, const Context& context, const AndroidBoundaryInput& input);
}  // namespace android_intrinsics
}  // namespace ogplay::runtime
