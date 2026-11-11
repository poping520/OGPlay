#include "rom_input.h"

#include <cmath>

namespace ogplay::runtime::android_intrinsics {
namespace {
using V = dx::VmValue;
using Ref = dx::VmObjectRef;
constexpr auto native = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
constexpr auto motion_class = "Landroid/view/MotionEvent;";

AndroidBoundaryInput& Motion(const Context& context, int token) {
    auto& motions = RomInput(context).motions;
    const auto found = motions.find(token);
    if (found == motions.end())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid MotionEvent native handle"};
    return found->second;
}

std::size_t Pointer(const AndroidBoundaryInput& m, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= m.pointers.size())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "pointer index out of range"};
    return static_cast<std::size_t>(index);
}

const AndroidInputPointer& SamplePointer(const AndroidBoundaryInput& m, int index, int history) {
    const auto pointer = Pointer(m, index);
    if (history == std::numeric_limits<int>::min()) return m.pointers[pointer];
    if (history < 0 || static_cast<std::size_t>(history) >= m.history.size())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "history index out of range"};
    return m.history[static_cast<std::size_t>(history)].pointers[pointer];
}

void ReadCoords(dx::Interpreter& vm, Ref array, std::vector<AndroidInputPointer>& pointers, float xo, float yo) {
    if (!array.IsValid() || vm.Model().ArrayLength(array) < static_cast<JniSize>(pointers.size()))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "pointer coordinate array is too short"};
    for (std::size_t i = 0; i < pointers.size(); ++i) {
        const auto coords = vm.Model().GetObjectElement(array, static_cast<JniSize>(i));
        for (int axis = 0; axis < 64; ++axis)
            pointers[i].axes[static_cast<std::size_t>(axis)] =
                CallAndroidMethod(vm, coords, "getAxisValue", "(I)F", {V::Int(axis)}).AsFloat();
        pointers[i].axes[0] -= xo;
        pointers[i].axes[1] -= yo;
    }
}

void Put(dx::Interpreter& vm, Ref ref, const char* name, int value) {
    WindowField(vm, ref, name, "I") = {static_cast<unsigned>(value), dx::SlotTag::cat1};
}
}  // namespace

AndroidBoundaryInput ReadMotionEvent(dx::Interpreter& vm, Ref event) {
    const auto get = [&](const char* name) { return CallAndroidMethod(vm, event, name, "()I").AsInt(); };
    AndroidBoundaryInput m;
    m.type = AndroidBoundaryInputType::pointer_motion;
    m.action = get("getAction");
    m.device_id = get("getDeviceId");
    m.source = get("getSource");
    m.flags = get("getFlags");
    m.edge_flags = get("getEdgeFlags");
    m.meta_state = get("getMetaState");
    m.button_state = get("getButtonState");
    m.event_time_ns = CallAndroidMethod(vm, event, "getEventTimeNano", "()J").AsLong();
    m.down_time_ns = CallAndroidMethod(vm, event, "getDownTime", "()J").AsLong() * 1'000'000;
    const auto count = get("getPointerCount"), history = get("getHistorySize");
    if (count < 1 || count > 16 || history < 0 || history > 4096)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid motion snapshot size"};
    for (int i = 0; i < count; ++i) {
        AndroidInputPointer p;
        p.id = CallAndroidMethod(vm, event, "getPointerId", "(I)I", {V::Int(i)}).AsInt();
        p.tool_type = CallAndroidMethod(vm, event, "getToolType", "(I)I", {V::Int(i)}).AsInt();
        for (int axis = 0; axis < 64; ++axis)
            p.axes[static_cast<std::size_t>(axis)] =
                CallAndroidMethod(vm, event, "getAxisValue", "(II)F", {V::Int(axis), V::Int(i)}).AsFloat();
        m.pointers.push_back(p);
    }
    for (int h = 0; h < history; ++h) {
        AndroidInputSample sample;
        sample.event_time_ns =
            CallAndroidMethod(vm, event, "getHistoricalEventTime", "(I)J", {V::Int(h)}).AsLong() * 1'000'000;
        sample.pointers = m.pointers;
        for (int i = 0; i < count; ++i)
            for (int a = 0; a < 64; ++a)
                sample.pointers[static_cast<std::size_t>(i)].axes[static_cast<std::size_t>(a)] =
                    CallAndroidMethod(vm, event, "getHistoricalAxisValue", "(III)F", {V::Int(a), V::Int(i), V::Int(h)})
                        .AsFloat();
        m.history.push_back(std::move(sample));
    }
    return m;
}

Ref MakeInputEvent(dx::Interpreter& vm, const Context& context, const AndroidBoundaryInput& input) {
    if (input.type == AndroidBoundaryInputType::key)
        return NewPlatformObject(vm, "Landroid/view/KeyEvent;", "(JJIIIIIIII)V",
                                 {V::Long(input.down_time_ns / 1'000'000), V::Long(input.event_time_ms),
                                  V::Int(input.action >= 0 ? input.action
                                         : input.pressed   ? 0
                                                           : 1),
                                  V::Int(input.code), V::Int(input.repeat_count), V::Int(input.meta_state),
                                  V::Int(input.device_id), V::Int(input.scan_code), V::Int(input.flags),
                                  V::Int(input.source)});
    const auto method = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor(motion_class), "obtain",
                                                     "()Landroid/view/MotionEvent;");
    if (!method) throw std::logic_error("ROM MotionEvent pool is unavailable");
    const auto result = vm.Call(*method, {});
    if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
    const auto event = result.value.ref;
    auto& s = RomInput(context);
    auto& pointer = WindowField(vm, event, "mNativePtr", "I");
    const auto id = pointer.bits ? static_cast<int>(pointer.bits) : AllocateRomInputHandle(s);
    s.motions.insert_or_assign(id, input);
    pointer = {static_cast<unsigned>(id), dx::SlotTag::cat1};
    return event;
}

void AddRomMotionNatives(dx::IntrinsicClassBuilder& b, const Context& context) {
    b.StaticMethod(
        "nativeInitialize",
        "(IIIIIIIIFFFFJJI[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;)I",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const int count = c.arguments[14].AsInt();
            if (count < 1 || count > 16 || !c.arguments[15].ref.IsValid() ||
                c.vm.Model().ArrayLength(c.arguments[15].ref) < count)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid pointer array"};
            AndroidBoundaryInput m;
            m.type = AndroidBoundaryInputType::pointer_motion;
            m.device_id = c.arguments[1].AsInt();
            m.source = c.arguments[2].AsInt();
            m.action = c.arguments[3].AsInt();
            m.flags = c.arguments[4].AsInt();
            m.edge_flags = c.arguments[5].AsInt();
            m.meta_state = c.arguments[6].AsInt();
            m.button_state = c.arguments[7].AsInt();
            m.x_offset = c.arguments[8].AsFloat();
            m.y_offset = c.arguments[9].AsFloat();
            m.x_precision = c.arguments[10].AsFloat();
            m.y_precision = c.arguments[11].AsFloat();
            m.down_time_ns = c.arguments[12].AsLong();
            m.event_time_ns = c.arguments[13].AsLong();
            std::set<int> ids;
            for (int i = 0; i < count; ++i) {
                const auto p = c.vm.Model().GetObjectElement(c.arguments[15].ref, i);
                AndroidInputPointer pointer;
                pointer.id = static_cast<int>(WindowField(c.vm, p, "id", "I").bits);
                pointer.tool_type = static_cast<int>(WindowField(c.vm, p, "toolType", "I").bits);
                if (pointer.id < 0 || pointer.id > 31 || !ids.insert(pointer.id).second)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid pointer id"};
                m.pointers.push_back(pointer);
            }
            ReadCoords(c.vm, c.arguments[16].ref, m.pointers, m.x_offset, m.y_offset);
            int id = c.arguments[0].AsInt();
            if (id)
                static_cast<void>(Motion(context, id));
            else
                id = AllocateRomInputHandle(s);
            s.motions.insert_or_assign(id, std::move(m));
            return V::Int(id);
        },
        native);
    b.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RomInput(context).motions.erase(c.arguments[0].AsInt());
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeCopy", "(IIZ)I",
        [context](dx::IntrinsicContext& c) {
            auto m = Motion(context, c.arguments[1].AsInt());
            if (!c.arguments[2].AsInt()) m.history.clear();
            auto& s = RomInput(context);
            int id = c.arguments[0].AsInt();
            if (!id) id = AllocateRomInputHandle(s);
            s.motions.insert_or_assign(id, std::move(m));
            return V::Int(id);
        },
        native);
    const std::array ints{std::pair{"DeviceId", &AndroidBoundaryInput::device_id},
                          std::pair{"Source", &AndroidBoundaryInput::source},
                          std::pair{"Action", &AndroidBoundaryInput::action},
                          std::pair{"Flags", &AndroidBoundaryInput::flags},
                          std::pair{"EdgeFlags", &AndroidBoundaryInput::edge_flags},
                          std::pair{"MetaState", &AndroidBoundaryInput::meta_state},
                          std::pair{"ButtonState", &AndroidBoundaryInput::button_state}};
    for (auto [name, member] : ints)
        b.StaticMethod(
            std::string("nativeGet") + name, "(I)I",
            [context, member](dx::IntrinsicContext& c) {
                return V::Int(Motion(context, c.arguments[0].AsInt()).*member);
            },
            native);
    for (auto [name, member] : ints)
        if (std::string_view(name) == "Source" || std::string_view(name) == "Action" ||
            std::string_view(name) == "Flags" || std::string_view(name) == "EdgeFlags")
            b.StaticMethod(
                std::string("nativeSet") + name, std::string_view(name) == "Source" ? "(II)I" : "(II)V",
                [context, member, name](dx::IntrinsicContext& c) {
                    Motion(context, c.arguments[0].AsInt()).*member = c.arguments[1].AsInt();
                    return std::string_view(name) == "Source" ? V::Int(0) : V::Void();
                },
                native);
    for (auto [name, member] : std::array{std::pair{"XOffset", &AndroidBoundaryInput::x_offset},
                                          std::pair{"YOffset", &AndroidBoundaryInput::y_offset},
                                          std::pair{"XPrecision", &AndroidBoundaryInput::x_precision},
                                          std::pair{"YPrecision", &AndroidBoundaryInput::y_precision}})
        b.StaticMethod(
            std::string("nativeGet") + name, "(I)F",
            [context, member](dx::IntrinsicContext& c) {
                return V::Float(Motion(context, c.arguments[0].AsInt()).*member);
            },
            native);
    b.StaticMethod(
        "nativeGetDownTimeNanos", "(I)J",
        [context](dx::IntrinsicContext& c) { return V::Long(Motion(context, c.arguments[0].AsInt()).down_time_ns); },
        native);
    b.StaticMethod(
        "nativeSetDownTimeNanos", "(IJ)V",
        [context](dx::IntrinsicContext& c) {
            Motion(context, c.arguments[0].AsInt()).down_time_ns = c.arguments[1].AsLong();
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeGetPointerCount", "(I)I",
        [context](dx::IntrinsicContext& c) {
            return V::Int(static_cast<int>(Motion(context, c.arguments[0].AsInt()).pointers.size()));
        },
        native);
    b.StaticMethod(
        "nativeGetHistorySize", "(I)I",
        [context](dx::IntrinsicContext& c) {
            return V::Int(static_cast<int>(Motion(context, c.arguments[0].AsInt()).history.size()));
        },
        native);
    for (bool tool : {false, true})
        b.StaticMethod(
            tool ? "nativeGetToolType" : "nativeGetPointerId", "(II)I",
            [context, tool](dx::IntrinsicContext& c) {
                const auto& m = Motion(context, c.arguments[0].AsInt());
                const auto& p = m.pointers[Pointer(m, c.arguments[1].AsInt())];
                return V::Int(tool ? p.tool_type : p.id);
            },
            native);
    b.StaticMethod(
        "nativeFindPointerIndex", "(II)I",
        [context](dx::IntrinsicContext& c) {
            const auto& m = Motion(context, c.arguments[0].AsInt());
            for (std::size_t i = 0; i < m.pointers.size(); ++i)
                if (m.pointers[i].id == c.arguments[1].AsInt()) return V::Int(static_cast<int>(i));
            return V::Int(-1);
        },
        native);
    b.StaticMethod(
        "nativeGetEventTimeNanos", "(II)J",
        [context](dx::IntrinsicContext& c) {
            const auto& m = Motion(context, c.arguments[0].AsInt());
            const int h = c.arguments[1].AsInt();
            static_cast<void>(SamplePointer(m, 0, h));
            return V::Long(h == std::numeric_limits<int>::min() ? m.event_time_ns
                                                                : m.history[static_cast<std::size_t>(h)].event_time_ns);
        },
        native);
    for (bool raw : {false, true})
        b.StaticMethod(
            raw ? "nativeGetRawAxisValue" : "nativeGetAxisValue", "(IIII)F",
            [context, raw](dx::IntrinsicContext& c) {
                const auto& m = Motion(context, c.arguments[0].AsInt());
                const auto& p = SamplePointer(m, c.arguments[2].AsInt(), c.arguments[3].AsInt());
                const auto a = c.arguments[1].AsInt();
                if (a < 0 || a >= 64) return V::Float(0);
                return V::Float(p.axes[static_cast<std::size_t>(a)] + (!raw && a == 0   ? m.x_offset
                                                                       : !raw && a == 1 ? m.y_offset
                                                                                        : 0));
            },
            native);
    b.StaticMethod(
        "nativeGetPointerProperties", "(IILandroid/view/MotionEvent$PointerProperties;)V",
        [context](dx::IntrinsicContext& c) {
            const auto& m = Motion(context, c.arguments[0].AsInt());
            const auto& p = m.pointers[Pointer(m, c.arguments[1].AsInt())];
            Put(c.vm, c.arguments[2].ref, "id", p.id);
            Put(c.vm, c.arguments[2].ref, "toolType", p.tool_type);
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeGetPointerCoords", "(IIILandroid/view/MotionEvent$PointerCoords;)V",
        [context](dx::IntrinsicContext& c) {
            const auto& m = Motion(context, c.arguments[0].AsInt());
            const auto& p = SamplePointer(m, c.arguments[1].AsInt(), c.arguments[2].AsInt());
            for (int a = 0; a < 64; ++a)
                static_cast<void>(
                    CallAndroidMethod(c.vm, c.arguments[3].ref, "setAxisValue", "(IF)V",
                                      {V::Int(a), V::Float(p.axes[static_cast<std::size_t>(a)] + (a == 0   ? m.x_offset
                                                                                                  : a == 1 ? m.y_offset
                                                                                                           : 0))}));
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeAddBatch", "(IJ[Landroid/view/MotionEvent$PointerCoords;I)V",
        [context](dx::IntrinsicContext& c) {
            auto& m = Motion(context, c.arguments[0].AsInt());
            auto pointers = m.pointers;
            ReadCoords(c.vm, c.arguments[2].ref, pointers, m.x_offset, m.y_offset);
            if (m.history.size() >= 4096)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "motion history budget exhausted"};
            m.history.push_back({m.event_time_ns, m.pointers});
            m.pointers = std::move(pointers);
            m.event_time_ns = c.arguments[1].AsLong();
            m.meta_state |= c.arguments[3].AsInt();
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeOffsetLocation", "(IFF)V",
        [context](dx::IntrinsicContext& c) {
            auto& m = Motion(context, c.arguments[0].AsInt());
            m.x_offset += c.arguments[1].AsFloat();
            m.y_offset += c.arguments[2].AsFloat();
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeIsTouchEvent", "(I)Z",
        [context](dx::IntrinsicContext& c) {
            const auto& m = Motion(context, c.arguments[0].AsInt());
            const auto a = m.action & 255;
            return V::Int((m.source & 2) && a != 7 && a != 8 && a != 9 && a != 10);
        },
        native);
    b.StaticMethod(
        "nativeScale", "(IF)V",
        [context](dx::IntrinsicContext& c) {
            auto& m = Motion(context, c.arguments[0].AsInt());
            const auto scale = c.arguments[1].AsFloat();
            m.x_offset *= scale;
            m.y_offset *= scale;
            m.x_precision *= scale;
            m.y_precision *= scale;
            const auto apply = [scale](std::vector<AndroidInputPointer>& pointers) {
                for (auto& p : pointers)
                    for (std::size_t a : {0U, 1U, 4U, 5U, 6U, 7U})
                        p.axes[a] *= scale;
            };
            apply(m.pointers);
            for (auto& h : m.history)
                apply(h.pointers);
            return V::Void();
        },
        native);
    b.StaticMethod(
        "nativeTransform", "(ILandroid/graphics/Matrix;)V",
        [context](dx::IntrinsicContext& c) {
            const auto token = static_cast<int>(WindowField(c.vm, c.arguments[1].ref, "native_instance", "I").bits);
            const auto matrix = AndroidMatrixSnapshot(*context, token);
            if (!matrix) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "invalid Matrix"};
            auto& m = Motion(context, c.arguments[0].AsInt());
            const auto& a = *matrix;
            const auto point = [&](float x, float y) {
                float z = a[6] * x + a[7] * y + a[8];
                if (z != 0) z = 1 / z;
                return std::array{(a[0] * x + a[1] * y + a[2]) * z, (a[3] * x + a[4] * y + a[5]) * z};
            };
            const auto old_x = m.x_offset, old_y = m.y_offset;
            const auto first = point(m.pointers[0].axes[0] + old_x, m.pointers[0].axes[1] + old_y);
            m.x_offset = first[0] - m.pointers[0].axes[0];
            m.y_offset = first[1] - m.pointers[0].axes[1];
            const auto origin = point(0, 0);
            const auto apply = [&](std::vector<AndroidInputPointer>& pointers) {
                for (auto& p : pointers) {
                    const auto xy = point(p.axes[0] + old_x, p.axes[1] + old_y);
                    p.axes[0] = xy[0] - m.x_offset;
                    p.axes[1] = xy[1] - m.y_offset;
                    const auto v = point(std::sin(p.axes[8]), -std::cos(p.axes[8]));
                    float angle = std::atan2(v[0] - origin[0], -(v[1] - origin[1]));
                    constexpr float pi = 3.14159265358979323846f;
                    if (angle < -pi / 2)
                        angle += pi;
                    else if (angle > pi / 2)
                        angle -= pi;
                    p.axes[8] = angle;
                }
            };
            apply(m.pointers);
            for (auto& h : m.history)
                apply(h.pointers);
            return V::Void();
        },
        native);
}
}  // namespace ogplay::runtime::android_intrinsics
