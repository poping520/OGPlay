#include "ogplay/runtime/common/guest_address_layout.h"
#include "runtime/boundary/modules/android/android_module.h"

#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace ogplay::runtime {

AndroidBoundaryInput NormalizeAndroidInput(AndroidBoundaryInput input) {
    const bool key = input.type == AndroidBoundaryInputType::key;
    if (input.action < 0) {
        input.action = !key && input.type == AndroidBoundaryInputType::pointer_motion ? 2 : input.pressed ? 0 : 1;
        if (input.event_time_ms < 0 || input.event_time_ms > std::numeric_limits<std::int64_t>::max() / 1000000)
            throw std::invalid_argument("input event timestamp is outside range");
        input.event_time_ns = input.event_time_ms * 1000000;
        input.down_time_ns = input.event_time_ns;
        if (!key) {
            AndroidInputPointer pointer;
            pointer.axes[0] = input.x;
            pointer.axes[1] = input.y;
            pointer.axes[2] = input.action == 1 ? 0.0F : 1.0F;
            input.pointers = {pointer};
        }
    }
    if (input.source == 0) input.source = key ? 0x101 : 0x1002;
    input.event_time_ms = input.event_time_ns / 1000000;
    if (input.down_time_ns < 0 || input.event_time_ns < input.down_time_ns)
        throw std::invalid_argument("input event time precedes down time");
    if (key) {
        if (input.action > 2 || !input.pointers.empty() || !input.history.empty())
            throw std::invalid_argument("invalid key event snapshot");
        return input;
    }
    const auto action = input.action & 0xff;
    if (input.pointers.empty() || input.pointers.size() > 16 || input.history.size() > 256 || action > 10 ||
        (input.action & ~0xffff) != 0 ||
        ((action == 5 || action == 6) && static_cast<std::size_t>(input.action >> 8) >= input.pointers.size()) ||
        ((action != 5 && action != 6) && (input.action >> 8) != 0))
        throw std::invalid_argument("invalid motion event snapshot");
    if (!std::isfinite(input.x_offset) || !std::isfinite(input.y_offset) || !std::isfinite(input.x_precision) ||
        !std::isfinite(input.y_precision) || input.x_precision <= 0 || input.y_precision <= 0)
        throw std::invalid_argument("invalid motion coordinate metadata");
    const auto validate = [](const std::vector<AndroidInputPointer>& pointers) {
        std::set<std::int32_t> ids;
        for (const auto& pointer : pointers) {
            if (pointer.id < 0 || pointer.id > 31 || !ids.insert(pointer.id).second)
                throw std::invalid_argument("invalid motion pointer identity");
            for (const auto value : pointer.axes)
                if (!std::isfinite(value)) throw std::invalid_argument("non-finite motion axis");
        }
    };
    validate(input.pointers);
    auto previous = input.down_time_ns;
    for (const auto& sample : input.history) {
        if (sample.event_time_ns < previous || sample.event_time_ns > input.event_time_ns ||
            sample.pointers.size() != input.pointers.size())
            throw std::invalid_argument("invalid motion history sample");
        validate(sample.pointers);
        for (std::size_t i = 0; i < sample.pointers.size(); ++i)
            if (sample.pointers[i].id != input.pointers[i].id ||
                sample.pointers[i].tool_type != input.pointers[i].tool_type)
                throw std::invalid_argument("motion history changes pointer identities");
        previous = sample.event_time_ns;
    }
    input.x = input.pointers.front().axes[0] + input.x_offset;
    input.y = input.pointers.front().axes[1] + input.y_offset;
    return input;
}

BoundaryResult AndroidModule::AInputQueue_attachLooper(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
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

BoundaryResult AndroidModule::AInputQueue_detachLooper(const A32CallFrame& call) {
    const auto handle = call.RegisterArguments()[0];
    std::scoped_lock lock(mutex_);
    auto& queue = InputQueue(handle);
    queue.looper = memory::GuestAddress{};
    if (managed_activity_seen_) Queue(handle).input_attached = false;
    return 0;
}

BoundaryResult AndroidModule::AInputQueue_getEvent(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    std::scoped_lock lock(mutex_);
    auto& queue = InputQueue(args[0]);
    if (call.Pointer<void>(1).IsNull()) throw std::invalid_argument("AInputQueue_getEvent requires an output pointer");
    if (queue.pending.empty()) return std::bit_cast<std::uint32_t>(std::int32_t{-11});  // EAGAIN
    if (next_input_.Value() >= kNativeActivityWindowHandleBegin.Value())
        throw std::runtime_error("input event identity budget exhausted");
    const auto handle = next_input_;
    // Publish only after the guest destination and host allocation succeed.
    queue.inflight.emplace(handle.Value(), queue.pending.front());
    try {
        services_.Write32(args[1], handle.Value(), call.ThreadId());
    } catch (...) {
        queue.inflight.erase(handle.Value());
        throw;
    }
    next_input_ = next_input_.Add(4);
    queue.pending.pop_front();
    return 0;
}

BoundaryResult AndroidModule::AInputQueue_preDispatchEvent(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    std::function<void(bool)> finish;
    {
        std::scoped_lock lock(mutex_);
        auto& queue = InputQueue(args[0]);
        const auto event = queue.inflight.find(args[1]);
        if (event == queue.inflight.end()) throw std::invalid_argument("invalid input event pre-dispatch");
        if (event->second.event.type != AndroidBoundaryInputType::key ||
            !(event->second.event.flags & kAndroidKeyPreDispatchFlag))
            return 0;
        finish = std::move(event->second.finish);
        queue.inflight.erase(event);
    }
    if (finish) finish(false);
    return 1;
}

BoundaryResult AndroidModule::AInputQueue_finishEvent(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    std::function<void(bool)> finish;
    {
        std::scoped_lock lock(mutex_);
        auto& queue = InputQueue(args[0]);
        const auto event = queue.inflight.find(args[1]);
        if (event == queue.inflight.end()) throw std::invalid_argument("invalid input event completion");
        finish = std::move(event->second.finish);
        queue.inflight.erase(event);
    }
    if (finish) finish(args[2] != 0);
    return 0;
}

BoundaryResult AndroidModule::AInputQueue_hasEvents(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return InputQueue(call.RegisterArguments()[0]).pending.empty() ? 0U : 1U;
}

const AndroidBoundaryInput& AndroidModule::KeyEvent(const A32CallFrame& call) const {
    const auto& event = InputEvent(call.Pointer<void>(0).Address());
    if (event.type != AndroidBoundaryInputType::key) throw std::invalid_argument("AKeyEvent requires a key event");
    return event;
}

const AndroidBoundaryInput& AndroidModule::MotionEvent(const A32CallFrame& call) const {
    const auto& event = InputEvent(call.Pointer<void>(0).Address());
    if (event.type == AndroidBoundaryInputType::key)
        throw std::invalid_argument("AMotionEvent requires a motion event");
    return event;
}

BoundaryResult AndroidModule::AInputEvent_getType(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto& event = InputEvent(call.Pointer<void>(0).Address());
    return event.type == AndroidBoundaryInputType::key ? 1U : 2U;
}

#define OGPLAY_INPUT_WORD_GETTER(name, field)                                                                          \
    BoundaryResult AndroidModule::name(const A32CallFrame& call) {                                                     \
        std::scoped_lock lock(mutex_);                                                                                 \
        return std::bit_cast<std::uint32_t>(field);                                                                    \
    }

OGPLAY_INPUT_WORD_GETTER(AInputEvent_getDeviceId, InputEvent(call.Pointer<void>(0).Address()).device_id)
OGPLAY_INPUT_WORD_GETTER(AInputEvent_getSource, InputEvent(call.Pointer<void>(0).Address()).source)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getAction, KeyEvent(call).action)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getKeyCode, KeyEvent(call).code)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getFlags, KeyEvent(call).flags)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getScanCode, KeyEvent(call).scan_code)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getMetaState, KeyEvent(call).meta_state)
OGPLAY_INPUT_WORD_GETTER(AKeyEvent_getRepeatCount, KeyEvent(call).repeat_count)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getAction, MotionEvent(call).action)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getFlags, MotionEvent(call).flags)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getMetaState, MotionEvent(call).meta_state)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getButtonState, MotionEvent(call).button_state)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getEdgeFlags, MotionEvent(call).edge_flags)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getXOffset, MotionEvent(call).x_offset)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getYOffset, MotionEvent(call).y_offset)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getXPrecision, MotionEvent(call).x_precision)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getYPrecision, MotionEvent(call).y_precision)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getPointerId, MotionEvent(call).pointers.at(call.Argument(1)).id)
OGPLAY_INPUT_WORD_GETTER(AMotionEvent_getToolType, MotionEvent(call).pointers.at(call.Argument(1)).tool_type)

#undef OGPLAY_INPUT_WORD_GETTER

#define OGPLAY_INPUT_TIME_GETTER(name, time)                                                                           \
    BoundaryResult AndroidModule::name(const A32CallFrame& call) {                                                     \
        std::scoped_lock lock(mutex_);                                                                                 \
        return BoundaryResult::Wide(static_cast<std::uint64_t>(time));                                                 \
    }

OGPLAY_INPUT_TIME_GETTER(AKeyEvent_getDownTime, KeyEvent(call).down_time_ns)
OGPLAY_INPUT_TIME_GETTER(AKeyEvent_getEventTime, KeyEvent(call).event_time_ns)
OGPLAY_INPUT_TIME_GETTER(AMotionEvent_getDownTime, MotionEvent(call).down_time_ns)
OGPLAY_INPUT_TIME_GETTER(AMotionEvent_getEventTime, MotionEvent(call).event_time_ns)
OGPLAY_INPUT_TIME_GETTER(AMotionEvent_getHistoricalEventTime,
                         MotionEvent(call).history.at(call.Argument(1)).event_time_ns)

#undef OGPLAY_INPUT_TIME_GETTER

BoundaryResult AndroidModule::AMotionEvent_getPointerCount(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return static_cast<std::uint32_t>(MotionEvent(call).pointers.size());
}

BoundaryResult AndroidModule::AMotionEvent_getHistorySize(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    return static_cast<std::uint32_t>(MotionEvent(call).history.size());
}

namespace {
std::uint32_t MotionAxis(const AndroidBoundaryInput& event, const AndroidInputPointer& pointer, std::uint32_t axis,
                         bool raw) {
    // AOSP PointerCoords returns zero for axes outside its 64-axis domain.
    if (axis >= pointer.axes.size()) return std::bit_cast<std::uint32_t>(0.0F);
    auto value = pointer.axes[axis];
    if (!raw && axis == 0) value += event.x_offset;
    if (!raw && axis == 1) value += event.y_offset;
    return std::bit_cast<std::uint32_t>(value);
}
}  // namespace

BoundaryResult AndroidModule::ReadMotionAxis(const A32CallFrame& call, std::uint32_t axis, bool raw) {
    std::scoped_lock lock(mutex_);
    const auto& event = MotionEvent(call);
    return MotionAxis(event, event.pointers.at(call.Argument(1)), axis, raw);
}

BoundaryResult AndroidModule::ReadHistoricalMotionAxis(const A32CallFrame& call, std::uint32_t axis, bool raw) {
    std::scoped_lock lock(mutex_);
    const auto& event = MotionEvent(call);
    const auto pointer_index = call.Argument(1);
    const auto& sample = event.history.at(call.Argument(2));
    return MotionAxis(event, sample.pointers.at(pointer_index), axis, raw);
}

#define OGPLAY_MOTION_AXIS_GETTER(name, getter, axis, raw)                                                             \
    BoundaryResult AndroidModule::name(const A32CallFrame& call) {                                                     \
        return getter(call, axis, raw);                                                                                \
    }

OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getX, ReadMotionAxis, 0, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getY, ReadMotionAxis, 1, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getRawX, ReadMotionAxis, 0, true)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getRawY, ReadMotionAxis, 1, true)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getPressure, ReadMotionAxis, 2, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getSize, ReadMotionAxis, 3, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getTouchMajor, ReadMotionAxis, 4, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getTouchMinor, ReadMotionAxis, 5, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getToolMajor, ReadMotionAxis, 6, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getToolMinor, ReadMotionAxis, 7, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getOrientation, ReadMotionAxis, 8, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalRawX, ReadHistoricalMotionAxis, 0, true)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalRawY, ReadHistoricalMotionAxis, 1, true)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalX, ReadHistoricalMotionAxis, 0, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalY, ReadHistoricalMotionAxis, 1, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalPressure, ReadHistoricalMotionAxis, 2, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalSize, ReadHistoricalMotionAxis, 3, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalTouchMajor, ReadHistoricalMotionAxis, 4, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalTouchMinor, ReadHistoricalMotionAxis, 5, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalToolMajor, ReadHistoricalMotionAxis, 6, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalToolMinor, ReadHistoricalMotionAxis, 7, false)
OGPLAY_MOTION_AXIS_GETTER(AMotionEvent_getHistoricalOrientation, ReadHistoricalMotionAxis, 8, false)

#undef OGPLAY_MOTION_AXIS_GETTER

BoundaryResult AndroidModule::AMotionEvent_getAxisValue(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto& event = MotionEvent(call);
    const auto axis = call.Argument(1);
    return MotionAxis(event, event.pointers.at(call.Argument(2)), axis, false);
}

BoundaryResult AndroidModule::AMotionEvent_getHistoricalAxisValue(const A32CallFrame& call) {
    std::scoped_lock lock(mutex_);
    const auto& event = MotionEvent(call);
    const auto axis = call.Argument(1);
    const auto& sample = event.history.at(call.Argument(3));
    return MotionAxis(event, sample.pointers.at(call.Argument(2)), axis, false);
}

}  // namespace ogplay::runtime
