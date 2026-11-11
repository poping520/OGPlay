#pragma once

#include "rom_window.h"

namespace ogplay::runtime {
namespace android_intrinsics {
class VelocityTracker;
}

struct RomInputState {
    struct Event {
        int sequence{};
        AndroidBoundaryInput input;
    };

    struct Channel {
        std::string name;
        std::deque<Event> pending;
        bool batch_notified{};
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
    int next_handle{1}, next_sequence{1};
    std::map<int, AndroidBoundaryInput> motions;
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
void CommitRomInputText(dx::Interpreter& vm, const Context& context, const std::u16string& text, bool composing);
void SetRomInputWindowFocus(dx::Interpreter& vm, const Context& context, dx::VmObjectRef window);
void VisitRomInputRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit);
void AddRomMotionNatives(dx::IntrinsicClassBuilder& builder, const Context& context);
void AddRomInputQueueNatives(dx::IntrinsicClassBuilder& builder, const Context& context);
AndroidBoundaryInput ReadMotionEvent(dx::Interpreter& vm, dx::VmObjectRef event);
dx::VmObjectRef MakeInputEvent(dx::Interpreter& vm, const AndroidBoundaryInput& input);
}  // namespace android_intrinsics
}  // namespace ogplay::runtime
