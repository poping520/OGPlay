#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("InputDevice queries use BootDex values and the process input directory") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        constexpr auto device_class = "Landroid/view/InputDevice;";
        const auto ids = [&] { return fixture.Static(device_class, "getDeviceIds", "()[I").ref; };
        const auto device = [&](int id) {
            return fixture.Static(device_class, "getDevice", "(I)Landroid/view/InputDevice;", {VmValue::Int(id)}).ref;
        };
        CHECK(fixture.model.ArrayLength(ids()) == 0);
        CHECK_FALSE(device(404).IsValid());
        fixture.context->input_devices = {
            {-1, "keyboard", "fixture:keyboard", kAndroidKeyboardSource, 2, {}},
            {0, "touch", "fixture:touch", kAndroidTouchSource, 0,
             {{0, kAndroidTouchSource, 0, 799}, {1, kAndroidTouchSource, 0, 479},
              {2, kAndroidTouchSource, 0, 1}}}};
        const auto list = ids();
        REQUIRE(fixture.model.ArrayLength(list) == 2);
        CHECK(static_cast<std::int32_t>(fixture.model.GetPrimitiveElement(list, 0)) == -1);
        CHECK(fixture.model.GetPrimitiveElement(list, 1) == 0);
        fixture.model.SetPrimitiveElement(list, 1, 999);
        CHECK(fixture.model.GetPrimitiveElement(ids(), 1) == 0);
        const auto touch = device(0);
        REQUIRE(touch.IsValid());
        const auto roots = fixture.vm.ProtectReferences(std::array{touch});
        CHECK_FALSE(fixture.linker.Class(fixture.model.ObjectClass(touch)).is_intrinsic);
        CHECK(fixture.On(touch, "getId", "()I").AsInt() == 0);
        CHECK(fixture.vm.StringUtf8(fixture.On(touch, "getDescriptor", "()Ljava/lang/String;").ref) == "fixture:touch");
        const auto sources = fixture.On(touch, "getSources", "()I").AsInt();
        CHECK(sources == kAndroidTouchSource);
        CHECK((sources & 0x100008) != 0x100008); // no touchpad advertised
        const auto range = fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(0)}).ref;
        REQUIRE(range.IsValid());
        CHECK(fixture.On(range, "getRange", "()F").AsFloat() == 799.0f);
        CHECK_FALSE(fixture.On(touch, "getMotionRange", "(II)Landroid/view/InputDevice$MotionRange;",
            {VmValue::Int(0), VmValue::Int(0x100008)}).ref.IsValid());
        CHECK_FALSE(fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(9)}).ref.IsValid());
        const auto ranges = fixture.On(touch, "getMotionRanges", "()Ljava/util/List;").ref;
        CHECK(fixture.On(ranges, "size", "()I").AsInt() == 3);
        const auto pressure = fixture.On(touch, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;", {VmValue::Int(2)}).ref;
        REQUIRE(pressure.IsValid());
        CHECK(fixture.On(pressure, "getMin", "()F").AsFloat() == 0);
        CHECK(fixture.On(pressure, "getMax", "()F").AsFloat() == 1);
        const auto keyboard = device(-1);
        CHECK(fixture.On(keyboard, "getKeyboardType", "()I").AsInt() == 2);
        const auto unsupported = fixture.OnOutcome(keyboard, "hasKeys", "([I)[Z", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(unsupported.exception.IsValid());
        CHECK(fixture.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        const auto event = MakeMotionEvent(fixture.vm, 0, 10, 20, 0);
        CHECK(fixture.On(event, "getDeviceId", "()I").AsInt() == fixture.On(touch, "getId", "()I").AsInt());
        CHECK(fixture.On(event, "getSource", "()I").AsInt() == sources);
    }
}

TEST_CASE("BND46 Java MotionEvent preserves snapshot axes history identity and GC ownership") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    AndroidBoundaryInput input;
    input.type = AndroidBoundaryInputType::pointer_motion;
    input.action = 5 | (1 << 8); input.source = 0x1002; input.device_id = 7;
    input.down_time_ns = 4'000'000'001LL; input.event_time_ns = 5'000'000'123LL;
    input.flags = 8; input.meta_state = 3; input.button_state = 2; input.edge_flags = 4;
    input.x_offset = 11; input.y_offset = 13; input.x_precision = 0.5F; input.y_precision = 0.25F;
    AndroidInputPointer first, second;
    first.id = 9; second.id = 3; second.tool_type = 2;
    for (std::size_t i = 0; i < 64; ++i) second.axes[i] = static_cast<float>(i) + 10.5F;
    input.pointers = {first, second};
    second.axes[0] = 100;
    input.history = {{4'500'000'001LL, {first, second}}};
    const auto event = MakeMotionEvent(f.vm, input);
    const auto root = f.vm.ProtectReferences(std::array{event});
    input.pointers[1].axes[0] = -100;
    static_cast<void>(f.vm.CollectGarbage("bnd46-motion-snapshot"));
    const auto integer = [&](const char* name) { return f.On(event, name, "()I").AsInt(); };
    CHECK(integer("getAction") == (5 | 1 << 8));
    CHECK(integer("getActionMasked") == 5); CHECK(integer("getActionIndex") == 1);
    CHECK(integer("getDeviceId") == 7); CHECK(integer("getSource") == 0x1002);
    CHECK(integer("getFlags") == 8); CHECK(integer("getMetaState") == 3);
    CHECK(integer("getButtonState") == 2); CHECK(integer("getEdgeFlags") == 4);
    CHECK(integer("getPointerCount") == 2); CHECK(integer("getHistorySize") == 1);
    CHECK(f.On(event, "getPointerId", "(I)I", {VmValue::Int(1)}).AsInt() == 3);
    CHECK(f.On(event, "getToolType", "(I)I", {VmValue::Int(1)}).AsInt() == 2);
    CHECK(f.On(event, "findPointerIndex", "(I)I", {VmValue::Int(9)}).AsInt() == 0);
    CHECK(f.On(event, "findPointerIndex", "(I)I", {VmValue::Int(4)}).AsInt() == -1);
    CHECK(f.On(event, "getDownTime", "()J").AsLong() == 4000);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5000);
    CHECK(f.On(event, "getEventTimeNano", "()J").AsLong() == 5'000'000'123LL);
    CHECK(f.On(event, "getHistoricalEventTime", "(I)J", {VmValue::Int(0)}).AsLong() == 4500);
    CHECK(f.On(event, "getHistoricalEventTimeNano", "(I)J", {VmValue::Int(0)}).AsLong() == 4'500'000'001LL);
    const std::array names{"X", "Y", "Pressure", "Size", "TouchMajor", "TouchMinor", "ToolMajor", "ToolMinor", "Orientation"};
    for (std::size_t axis = 0; axis < names.size(); ++axis) {
        const auto name = std::string("get") + names[axis];
        const auto history = std::string("getHistorical") + names[axis];
        const float offset = axis == 0 ? 11.0F : axis == 1 ? 13.0F : 0;
        CHECK(f.On(event, name.c_str(), "(I)F", {VmValue::Int(1)}).AsFloat() == static_cast<float>(axis) + 10.5F + offset);
        CHECK(f.On(event, name.c_str(), "()F").AsFloat() == offset);
        CHECK(f.On(event, history.c_str(), "(II)F", {VmValue::Int(1), VmValue::Int(0)}).AsFloat() ==
              (axis == 0 ? 111.0F : static_cast<float>(axis) + 10.5F + offset));
    }
    CHECK(f.On(event, "getAxisValue", "(II)F", {VmValue::Int(63), VmValue::Int(1)}).AsFloat() == 73.5F);
    CHECK(f.On(event, "getHistoricalAxisValue", "(III)F", {VmValue::Int(0), VmValue::Int(1), VmValue::Int(0)}).AsFloat() == 111);
    CHECK(f.On(event, "getRawX", "()F").AsFloat() == 0);
    CHECK(f.On(event, "getXPrecision", "()F").AsFloat() == 0.5F);
    CHECK(f.On(event, "getYPrecision", "()F").AsFloat() == 0.25F);
    CHECK(f.OnOutcome(event, "getX", "(I)F", {VmValue::Int(2)}).exception.IsValid());
    CHECK(f.OnOutcome(event, "getHistoricalX", "(I)F", {VmValue::Int(1)}).exception.IsValid());
    static_cast<void>(f.On(event, "recycle", "()V"));
    CHECK(f.OnOutcome(event, "getAction", "()I").exception.IsValid());
  }
}

TEST_CASE("MotionEvent.obtain and addBatch rebuild events from BootDex pointer values") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    constexpr auto coords_type = "Landroid/view/MotionEvent$PointerCoords;";
    constexpr auto props_type = "Landroid/view/MotionEvent$PointerProperties;";
    const auto set_float = [&](VmObjectRef object, const char* name, float value) {
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, "F");
        REQUIRE(field.has_value());
        f.model.InstanceSlots(object)[f.linker.Field(*field).slot] = {std::bit_cast<std::uint32_t>(value), SlotTag::cat1};
    };
    const auto set_int = [&](VmObjectRef object, const char* name, std::int32_t value) {
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, "I");
        REQUIRE(field.has_value());
        f.model.InstanceSlots(object)[f.linker.Field(*field).slot] = {static_cast<std::uint32_t>(value), SlotTag::cat1};
    };
    const auto coords_array = [&](std::vector<std::array<float, 3>> values, std::size_t length) {
        const auto array = f.model.NewObjectArray(f.linker.ResolveDescriptor(std::string("[") + coords_type),
            f.linker.ResolveDescriptor(coords_type), static_cast<JniSize>(length));
        static_cast<void>(f.vm.ProtectReferences(std::array{array}));
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto coords = f.New(coords_type);
            static_cast<void>(f.vm.ProtectReferences(std::array{coords}));
            set_float(coords, "x", values[i][0]);
            set_float(coords, "y", values[i][1]);
            set_float(coords, "pressure", values[i][2]);
            set_float(coords, "orientation", 0.5F);
            f.model.SetObjectElement(array, static_cast<JniSize>(i), coords);
        }
        return array;
    };
    constexpr auto motion = "Landroid/view/MotionEvent;";
    constexpr auto ids_signature = "(JJII[I[Landroid/view/MotionEvent$PointerCoords;IFFIIII)Landroid/view/MotionEvent;";
    const auto ids = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
    static_cast<void>(f.vm.ProtectReferences(std::array{ids}));
    f.model.SetPrimitiveElement(ids, 0, 4);
    f.model.SetPrimitiveElement(ids, 1, 7);
    const auto coords = coords_array({{10, 20, 0.25F}, {30, 40, 0.75F}}, 3);
    const auto obtain_ids = [&](VmObjectRef pointer_ids, VmObjectRef pointer_coords, int count) {
        return f.StaticOutcome(motion, "obtain", ids_signature,
            {VmValue::Long(4000), VmValue::Long(5000), VmValue::Int(2), VmValue::Int(count),
             VmValue::Ref(pointer_ids), VmValue::Ref(pointer_coords), VmValue::Int(3),
             VmValue::Float(0.5F), VmValue::Float(0.25F), VmValue::Int(9), VmValue::Int(4), VmValue::Int(0x1002), VmValue::Int(8)});
    };
    const auto outcome = obtain_ids(ids, coords, 2);
    REQUIRE_FALSE(outcome.exception.IsValid());
    const auto event = outcome.value.ref;
    const auto root = f.vm.ProtectReferences(std::array{event});
    const auto integer = [&](const char* name) { return f.On(event, name, "()I").AsInt(); };
    CHECK(integer("getPointerCount") == 2);
    CHECK(integer("getAction") == 2);
    CHECK(integer("getDeviceId") == 9);
    CHECK(integer("getSource") == 0x1002);
    CHECK(integer("getFlags") == 8);
    CHECK(integer("getEdgeFlags") == 4);
    CHECK(integer("getMetaState") == 3);
    CHECK(integer("getHistorySize") == 0);
    CHECK(f.On(event, "getPointerId", "(I)I", {VmValue::Int(1)}).AsInt() == 7);
    CHECK(f.On(event, "getX", "(I)F", {VmValue::Int(1)}).AsFloat() == 30);
    CHECK(f.On(event, "getY", "(I)F", {VmValue::Int(0)}).AsFloat() == 20);
    CHECK(f.On(event, "getPressure", "(I)F", {VmValue::Int(1)}).AsFloat() == 0.75F);
    CHECK(f.On(event, "getOrientation", "(I)F", {VmValue::Int(0)}).AsFloat() == 0.5F);
    CHECK(f.On(event, "getDownTime", "()J").AsLong() == 4000);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5000);
    CHECK(f.On(event, "getXPrecision", "()F").AsFloat() == 0.5F);

    // addBatch: the previous current sample becomes the newest historical sample.
    for (const auto& [time, x] : std::array{std::pair{5010, 11.0F}, std::pair{5020, 12.0F}}) {
        const auto batch = coords_array({{x, x + 100, 1}, {x + 200, x + 300, 1}}, 2);
        const auto added = f.OnOutcome(event, "addBatch", "(J[Landroid/view/MotionEvent$PointerCoords;I)V",
            {VmValue::Long(time), VmValue::Ref(batch), VmValue::Int(0x10)});
        REQUIRE_FALSE(added.exception.IsValid());
    }
    CHECK(integer("getHistorySize") == 2);
    CHECK(integer("getMetaState") == 0x13);
    CHECK(f.On(event, "getX", "(I)F", {VmValue::Int(0)}).AsFloat() == 12);
    CHECK(f.On(event, "getY", "(I)F", {VmValue::Int(1)}).AsFloat() == 312);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5020);
    CHECK(f.On(event, "getHistoricalX", "(II)F", {VmValue::Int(0), VmValue::Int(0)}).AsFloat() == 10);
    CHECK(f.On(event, "getHistoricalX", "(II)F", {VmValue::Int(1), VmValue::Int(0)}).AsFloat() == 30);
    CHECK(f.On(event, "getHistoricalX", "(II)F", {VmValue::Int(0), VmValue::Int(1)}).AsFloat() == 11);
    CHECK(f.On(event, "getHistoricalEventTime", "(I)J", {VmValue::Int(0)}).AsLong() == 5000);
    CHECK(f.On(event, "getHistoricalEventTime", "(I)J", {VmValue::Int(1)}).AsLong() == 5010);
    CHECK(f.On(event, "getHistoricalPressure", "(II)F", {VmValue::Int(1), VmValue::Int(0)}).AsFloat() == 0.75F);
    CHECK(f.On(event, "getPointerId", "(I)I", {VmValue::Int(0)}).AsInt() == 4);

    // PointerProperties overload also carries buttonState and tool type.
    const auto props = f.model.NewObjectArray(f.linker.ResolveDescriptor(std::string("[") + props_type),
        f.linker.ResolveDescriptor(props_type), 2);
    static_cast<void>(f.vm.ProtectReferences(std::array{props}));
    for (std::int32_t i = 0; i < 2; ++i) {
        const auto property = f.New(props_type);
        static_cast<void>(f.vm.ProtectReferences(std::array{property}));
        set_int(property, "id", 20 + i);
        set_int(property, "toolType", 1 + i);
        f.model.SetObjectElement(props, i, property);
    }
    const auto prop_outcome = f.StaticOutcome(motion, "obtain",
        "(JJII[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;IIFFIIII)Landroid/view/MotionEvent;",
        {VmValue::Long(1), VmValue::Long(2), VmValue::Int(0), VmValue::Int(2), VmValue::Ref(props), VmValue::Ref(coords),
         VmValue::Int(0), VmValue::Int(2), VmValue::Float(1), VmValue::Float(1), VmValue::Int(0), VmValue::Int(0),
         VmValue::Int(0x1002), VmValue::Int(0)});
    REQUIRE_FALSE(prop_outcome.exception.IsValid());
    const auto typed = prop_outcome.value.ref;
    const auto typed_root = f.vm.ProtectReferences(std::array{typed});
    CHECK(f.On(typed, "getButtonState", "()I").AsInt() == 2);
    CHECK(f.On(typed, "getPointerId", "(I)I", {VmValue::Int(1)}).AsInt() == 21);
    CHECK(f.On(typed, "getToolType", "(I)I", {VmValue::Int(1)}).AsInt() == 2);
    CHECK(f.On(typed, "findPointerIndex", "(I)I", {VmValue::Int(20)}).AsInt() == 0);
    CHECK(f.On(typed, "getX", "(I)F", {VmValue::Int(1)}).AsFloat() == 30);

    // Illegal arguments follow AOSP validation.
    const auto thrown = [&](const VmCallOutcome& result) {
        return result.exception.IsValid() ? f.linker.Class(result.exception_class).descriptor : std::string();
    };
    CHECK(thrown(obtain_ids(ids, coords, 0)) == "Ljava/lang/IllegalArgumentException;");
    CHECK(thrown(obtain_ids(ids, coords_array({{1, 2, 3}}, 1), 2)) == "Ljava/lang/IllegalArgumentException;");
    CHECK(thrown(obtain_ids(ids, VmObjectRef{}, 2)) == "Ljava/lang/IllegalArgumentException;");
    CHECK(thrown(obtain_ids(ids, coords_array({{1, 2, 3}}, 2), 2)) == "Ljava/lang/NullPointerException;");
    CHECK(thrown(obtain_ids(VmObjectRef{}, coords, 2)) == "Ljava/lang/NullPointerException;");
    CHECK(thrown(f.OnOutcome(event, "addBatch", "(J[Landroid/view/MotionEvent$PointerCoords;I)V",
        {VmValue::Long(6000), VmValue::Ref(coords_array({{1, 2, 3}}, 1)), VmValue::Int(0)}))
        == "Ljava/lang/IllegalArgumentException;");
    CHECK(integer("getHistorySize") == 2);
  }
}

TEST_CASE("BND46 timed KeyEvent retains metadata in both interpreters") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    const auto event = f.New("Landroid/view/KeyEvent;", "(JJIIIIIIII)V",
        {VmValue::Long(4000), VmValue::Long(5000), VmValue::Int(0), VmValue::Int(29),
         VmValue::Int(2), VmValue::Int(3), VmValue::Int(-1), VmValue::Int(4), VmValue::Int(8), VmValue::Int(0x101)});
    CHECK(f.On(event, "getDownTime", "()J").AsLong() == 4000);
    CHECK(f.On(event, "getEventTime", "()J").AsLong() == 5000);
    CHECK(f.On(event, "getFlags", "()I").AsInt() == 8);
    CHECK(f.On(event, "getSource", "()I").AsInt() == 0x101);
  }
}

TEST_CASE("DVM-206 virtual key character maps use API19 data and process device fallback") {
  for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    AndroidValueVm f(backend);
    constexpr auto owner = "Landroid/view/KeyCharacterMap;";
    const auto load = [&](int device) {
        return f.Static(owner, "load", "(I)Landroid/view/KeyCharacterMap;", {VmValue::Int(device)}).ref;
    };
    const auto absent = f.StaticOutcome(owner, "load", "(I)Landroid/view/KeyCharacterMap;", {VmValue::Int(-1)});
    REQUIRE(absent.exception.IsValid());
    CHECK(f.linker.Class(absent.exception_class).descriptor == "Landroid/view/KeyCharacterMap$UnavailableException;");
    f.context->input_devices = {
        {-1, "keyboard", "fixture:keyboard", kAndroidKeyboardSource, 2, {}},
        {0, "touch", "fixture:touch", kAndroidTouchSource, 0, {}}};
    const auto map = load(-1), fallback = load(404), empty = load(0);
    const auto roots = f.vm.ProtectReferences(std::array{map, fallback, empty});
    CHECK_FALSE(f.linker.Class(f.model.ObjectClass(map)).is_intrinsic);
    const auto get = [&](VmObjectRef object, int code, int meta) {
        return f.On(object, "get", "(II)I", {VmValue::Int(code), VmValue::Int(meta)}).AsInt();
    };
    for (const auto& [code, meta, expected] : std::array<std::tuple<int, int, int>, 15>{{
        {29, 0, 'a'}, {29, 0x40, 'A'}, {29, 0x80, 'A'},
        {29, 0x100000, 'A'}, {29, 0x100001, 'A'}, {29, 0x2000, 0},
        {31, 0x10, 0xe7}, {31, 0x11, 0xc7},
        {8, 0, '1'}, {8, 1, '!'}, {66, 0, '\n'}, {61, 0, '\t'},
        {144, 0, 0}, {144, 0x200000, '0'}, {999, 0, 0}}}) {
        CHECK(get(map, code, meta) == expected);
        CHECK(get(fallback, code, meta) == expected);
        CHECK(get(empty, code, meta) == 0);
    }
    CHECK(get(map, 33, 2) == std::bit_cast<std::int32_t>(0x800000b4U)); // ALT+E dead acute
    const auto normalization = f.StaticOutcome(owner, "getDeadChar", "(II)I", {VmValue::Int(0xb4), VmValue::Int('e')});
    CHECK(f.linker.Class(normalization.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    CHECK(f.On(map, "getDisplayLabel", "(I)C", {VmValue::Int(29)}).AsInt() == 'A');
    CHECK(f.On(map, "getNumber", "(I)C", {VmValue::Int(10)}).AsInt() == '3');
    CHECK(f.On(map, "getKeyboardType", "()I").AsInt() == 4);
    CHECK(f.On(empty, "getKeyboardType", "()I").AsInt() == 5);
    CHECK(f.On(map, "getModifierBehavior", "()I").AsInt() == 0);
    const auto category = f.OnOutcome(map, "isPrintingKey", "(I)Z", {VmValue::Int(29)});
    CHECK(f.linker.Class(category.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 2);
    const auto chars_root = f.vm.ProtectReferences(std::array{chars});
    f.model.SetPrimitiveElement(chars, 0, 'a'); f.model.SetPrimitiveElement(chars, 1, 'A');
    CHECK(f.On(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(chars), VmValue::Int(0)}).AsInt() == 'a');
    CHECK(f.On(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(chars), VmValue::Int(1)}).AsInt() == 'A');
    const auto action = f.On(map, "getFallbackAction", "(II)Landroid/view/KeyCharacterMap$FallbackAction;",
        {VmValue::Int(111), VmValue::Int(0)}).ref;
    REQUIRE(action.IsValid());
    const auto action_root = f.vm.ProtectReferences(std::array{action});
    const auto key_field = f.linker.FindFieldRecursive(f.model.ObjectClass(action), "keyCode", "I");
    REQUIRE(key_field.has_value());
    CHECK(f.model.InstanceSlots(action)[f.linker.Field(*key_field).slot].bits == 4);
    f.On(action, "recycle", "()V");
    const auto device = f.Static("Landroid/view/InputDevice;", "getDevice", "(I)Landroid/view/InputDevice;", {VmValue::Int(-1)}).ref;
    const auto device_root = f.vm.ProtectReferences(std::array{device});
    const auto device_map = f.On(device, "getKeyCharacterMap", "()Landroid/view/KeyCharacterMap;").ref;
    CHECK(get(device_map, 29, 0) == 'a');
    const auto event = f.New("Landroid/view/KeyEvent;", "(II)V", {VmValue::Int(0), VmValue::Int(33)});
    const auto event_root = f.vm.ProtectReferences(std::array{event});
    CHECK(f.On(event, "getUnicodeChar", "(I)I", {VmValue::Int(2)}).AsInt() == get(map, 33, 2));
    CHECK(f.On(event, "getUnicodeChar", "()I").AsInt() == 'e');
    SetAndroidKeyEventUnicode(f.vm, event, 0x03bb);
    CHECK(f.On(event, "getUnicodeChar", "()I").AsInt() == 0x03bb);
    const auto unsupported = f.OnOutcome(map, "getEvents", "([C)[Landroid/view/KeyEvent;", {VmValue::Ref(chars)});
    CHECK(f.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    const auto invalid = f.OnOutcome(map, "getMatch", "(I[CI)C", {VmValue::Int(29), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
    CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
    f.On(map, "finalize", "()V");
    const auto disposed = f.OnOutcome(map, "get", "(II)I", {VmValue::Int(29), VmValue::Int(0)});
    CHECK(f.linker.Class(disposed.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
    CHECK(get(fallback, 29, 0) == 'a');
    static_cast<void>(f.vm.CollectGarbage("virtual-key-character-map"));
    CHECK(get(fallback, 29, 0) == 'a');
  }
}
