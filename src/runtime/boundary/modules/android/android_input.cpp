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
        input.action = !key && input.type == AndroidBoundaryInputType::pointer_motion
            ? 2 : input.pressed ? 0 : 1;
        if (input.event_time_ms < 0 ||
            input.event_time_ms > std::numeric_limits<std::int64_t>::max() / 1000000)
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
    if (input.pointers.empty() || input.pointers.size() > 16 || input.history.size() > 256 ||
        action > 10 || (input.action & ~0xffff) != 0 ||
        ((action == 5 || action == 6) &&
         static_cast<std::size_t>(input.action >> 8) >= input.pointers.size()) ||
        ((action != 5 && action != 6) && (input.action >> 8) != 0))
        throw std::invalid_argument("invalid motion event snapshot");
    if (!std::isfinite(input.x_offset) || !std::isfinite(input.y_offset) ||
        !std::isfinite(input.x_precision) || !std::isfinite(input.y_precision) ||
        input.x_precision <= 0 || input.y_precision <= 0)
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

template <std::uint16_t FunctionId>
BoundaryResult AndroidModule::ReadInput(const A32CallFrame& call) {
    constexpr auto function = FunctionId;
    std::scoped_lock lock(mutex_);
    const auto& event = InputEvent(call.Pointer<void>(0).Address());
    const bool key = event.type == AndroidBoundaryInputType::key;
    if ((function == 14 || function == 15 || (function >= 39 && function <= 44)) && !key)
        throw std::invalid_argument("AKeyEvent requires a key event");
    if ((function == 16 || function == 17 || function == 18 || function >= 45) && key)
        throw std::invalid_argument("AMotionEvent requires a motion event");
    const auto integer = [](std::int32_t value) { return std::bit_cast<std::uint32_t>(value); };
    const auto number = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    if constexpr (FunctionId == 13)
        return key ? 1U : 2U;
    if constexpr (FunctionId == 14 || FunctionId == 16)
        return integer(event.action);
    if constexpr (FunctionId == 15)
        return integer(event.code);
    if constexpr (FunctionId == 37)
        return integer(event.device_id);
    if constexpr (FunctionId == 38)
        return integer(event.source);
    if constexpr (FunctionId == 39 || FunctionId == 45)
        return integer(event.flags);
    if constexpr (FunctionId == 40)
        return integer(event.scan_code);
    if constexpr (FunctionId == 41 || FunctionId == 46)
        return integer(event.meta_state);
    if constexpr (FunctionId == 42)
        return integer(event.repeat_count);
    if constexpr (FunctionId == 43 || FunctionId == 49)
        return BoundaryResult::Wide(static_cast<std::uint64_t>(event.down_time_ns));
    if constexpr (FunctionId == 44 || FunctionId == 50)
        return BoundaryResult::Wide(static_cast<std::uint64_t>(event.event_time_ns));
    if constexpr (FunctionId == 47)
        return integer(event.button_state);
    if constexpr (FunctionId == 48)
        return integer(event.edge_flags);
    if constexpr (FunctionId == 51)
        return number(event.x_offset);
    if constexpr (FunctionId == 52)
        return number(event.y_offset);
    if constexpr (FunctionId == 53)
        return number(event.x_precision);
    if constexpr (FunctionId == 54)
        return number(event.y_precision);
    if constexpr (FunctionId == 55)
        return static_cast<std::uint32_t>(event.pointers.size());
    if constexpr (FunctionId == 56)
        return integer(event.pointers.at(call.Argument(1)).id);
    if constexpr (FunctionId == 57)
        return integer(event.pointers.at(call.Argument(1)).tool_type);
    if constexpr (FunctionId == 68)
        return static_cast<std::uint32_t>(event.history.size());
    if constexpr (FunctionId == 69)
        return BoundaryResult::Wide(static_cast<std::uint64_t>(
        event.history.at(call.Argument(1)).event_time_ns));
    std::uint32_t axis{}, pointer_index = call.Argument(1);
    bool raw = false;
    const std::vector<AndroidInputPointer>* pointers = &event.pointers;
    if (function >= 70) {
        pointers = &event.history.at(call.Argument(function == 81 ? 3 : 2)).pointers;
    }
    if (function == 67 || function == 81) {
        axis = call.Argument(1);
        pointer_index = call.Argument(2);
    } else if (function == 17 || function == 18) {
        axis = function - 17U;
    } else if (function >= 58 && function <= 66) {
        axis = function - 58U;
        raw = function <= 59;
    } else if (function >= 70 && function <= 80) {
        axis = function <= 71 ? function - 70U : function - 72U;
        raw = function <= 71;
    } else {
        throw std::logic_error("unbound input getter");
    }
    const auto& pointer = pointers->at(pointer_index);
    // AOSP PointerCoords returns zero for axes outside its 64-axis domain.
    if (axis >= pointer.axes.size()) return number(0);
    auto value = pointer.axes[axis];
    if (!raw && axis == 0) value += event.x_offset;
    if (!raw && axis == 1) value += event.y_offset;
    return number(value);
}

template BoundaryResult AndroidModule::ReadInput<13>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<14>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<15>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<16>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<17>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<18>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<37>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<38>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<39>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<40>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<41>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<42>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<43>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<44>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<45>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<46>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<47>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<48>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<49>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<50>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<51>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<52>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<53>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<54>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<55>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<56>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<57>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<58>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<59>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<60>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<61>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<62>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<63>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<64>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<65>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<66>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<67>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<68>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<69>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<70>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<71>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<72>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<73>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<74>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<75>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<76>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<77>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<78>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<79>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<80>(const A32CallFrame&);
template BoundaryResult AndroidModule::ReadInput<81>(const A32CallFrame&);

} // namespace ogplay::runtime
