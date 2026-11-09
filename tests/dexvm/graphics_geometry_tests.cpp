#include <array>
#include <bit>
#include <doctest/doctest.h>

#include "android_value_fixture.h"

namespace {
using namespace ogplay::test::android_value;

TEST_CASE("Skia ROM Matrix composes and maps in-place with an inverse") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto matrix = f.New("Landroid/graphics/Matrix;");
        const auto inverse = f.New("Landroid/graphics/Matrix;");
        const auto points =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 4);
        const auto roots = f.vm.ProtectReferences(std::array{matrix, inverse, points});
        f.On(matrix, "setScale", "(FF)V", {VmValue::Float(2), VmValue::Float(3)});
        CHECK(f.On(matrix, "postTranslate", "(FF)Z", {VmValue::Float(10), VmValue::Float(20)}).AsInt() == 1);
        for (int i = 0; i < 4; ++i)
            f.model.SetPrimitiveElement(points, i, VmValue::Float(static_cast<float>(i + 1)).cat1);
        f.On(matrix, "mapPoints", "([F)V", {VmValue::Ref(points)});
        const std::array expected{12.0F, 26.0F, 16.0F, 32.0F};
        for (int i = 0; i < 4; ++i)
            CHECK(f.model.GetPrimitiveElement(points, i) == VmValue::Float(expected[static_cast<std::size_t>(i)]).cat1);
        CHECK(f.On(matrix, "invert", "(Landroid/graphics/Matrix;)Z", {VmValue::Ref(inverse)}).AsInt() == 1);
        f.On(inverse, "mapPoints", "([F)V", {VmValue::Ref(points)});
        for (int i = 0; i < 4; ++i) {
            const auto bits = static_cast<std::uint32_t>(f.model.GetPrimitiveElement(points, i));
            CHECK(std::bit_cast<float>(bits) == doctest::Approx(i + 1));
        }
    }
}

TEST_CASE("Skia ROM Path boolean operations match API19 ordinals and support an aliased destination") {
    AndroidValueVm f;
    const auto lhs = f.New("Landroid/graphics/Path;");
    const auto rhs = f.New("Landroid/graphics/Path;");
    const auto region = f.New("Landroid/graphics/Region;");
    const auto clip = f.New("Landroid/graphics/Region;", "(IIII)V",
                            {VmValue::Int(0), VmValue::Int(0), VmValue::Int(12), VmValue::Int(4)});
    const auto roots = f.vm.ProtectReferences(std::array{lhs, rhs, region, clip});
    const auto direction =
        f.Static("Landroid/graphics/Path$Direction;", "valueOf",
                 "(Ljava/lang/String;)Landroid/graphics/Path$Direction;", {VmValue::Ref(f.vm.NewStringUtf8("CW"))});
    const auto add_rect = [&](VmObjectRef path, float left, float right) {
        f.On(path, "addRect", "(FFFFLandroid/graphics/Path$Direction;)V",
             {VmValue::Float(left), VmValue::Float(0), VmValue::Float(right), VmValue::Float(4), direction});
    };
    add_rect(rhs, 4, 10);

    for (const auto& [name, expected] : {std::pair{"DIFFERENCE", std::array{true, false, false}},
                                         {"INTERSECT", {false, true, false}},
                                         {"UNION", {true, true, true}},
                                         {"XOR", {true, false, true}},
                                         {"REVERSE_DIFFERENCE", {false, false, true}}}) {
        CAPTURE(name);
        f.On(lhs, "reset", "()V");
        add_rect(lhs, 0, 6);
        const auto op =
            f.Static("Landroid/graphics/Path$Op;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Path$Op;",
                     {VmValue::Ref(f.vm.NewStringUtf8(name))});
        REQUIRE(
            f.On(lhs, "op", "(Landroid/graphics/Path;Landroid/graphics/Path$Op;)Z", {VmValue::Ref(rhs), op}).AsInt() ==
            1);
        REQUIRE(f.On(region, "setPath", "(Landroid/graphics/Path;Landroid/graphics/Region;)Z",
                     {VmValue::Ref(lhs), VmValue::Ref(clip)})
                    .AsInt() == 1);
        for (const auto& [x, covered] : {std::pair{2, expected[0]}, {5, expected[1]}, {8, expected[2]}})
            CHECK(f.On(region, "contains", "(II)Z", {VmValue::Int(x), VmValue::Int(2)}).AsInt() == covered);
    }
}

TEST_CASE("Skia ROM PathMeasure reports contours and extracts segments") {
    AndroidValueVm f;
    const auto path = f.New("Landroid/graphics/Path;");
    const auto output = f.New("Landroid/graphics/Path;");
    const auto measure = f.New("Landroid/graphics/PathMeasure;");
    const auto roots = f.vm.ProtectReferences(std::array{path, output, measure});
    f.On(path, "lineTo", "(FF)V", {VmValue::Float(3), VmValue::Float(4)});
    f.On(path, "moveTo", "(FF)V", {VmValue::Float(20), VmValue::Float(0)});
    f.On(path, "lineTo", "(FF)V", {VmValue::Float(40), VmValue::Float(0)});
    f.On(measure, "setPath", "(Landroid/graphics/Path;Z)V", {VmValue::Ref(path), VmValue::Int(false)});
    CHECK(f.On(measure, "getLength", "()F").AsFloat() == doctest::Approx(5));
    CHECK(f.On(measure, "getSegment", "(FFLandroid/graphics/Path;Z)Z",
               {VmValue::Float(1), VmValue::Float(4), VmValue::Ref(output), VmValue::Int(true)})
              .AsInt() == 1);
    CHECK(f.On(output, "isEmpty", "()Z").AsInt() == 0);
    CHECK(f.On(measure, "nextContour", "()Z").AsInt() == 1);
    CHECK(f.On(measure, "getLength", "()F").AsFloat() == doctest::Approx(20));
    CHECK(f.On(measure, "nextContour", "()Z").AsInt() == 0);
}

TEST_CASE("Skia ROM Region difference, iterator snapshot and Parcel round trip") {
    AndroidValueVm f;
    constexpr auto region_type = "Landroid/graphics/Region;";
    constexpr auto op_type = "Landroid/graphics/Region$Op;";
    const auto region =
        f.New(region_type, "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(6), VmValue::Int(2)});
    const auto iterator =
        f.New("Landroid/graphics/RegionIterator;", "(Landroid/graphics/Region;)V", {VmValue::Ref(region)});
    const auto rect = f.New("Landroid/graphics/Rect;");
    const auto roots = f.vm.ProtectReferences(std::array{region, iterator, rect});
    const auto difference = f.Static(op_type, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Region$Op;",
                                     {VmValue::Ref(f.vm.NewStringUtf8("DIFFERENCE"))});
    CHECK(f.On(region, "op", "(IIIILandroid/graphics/Region$Op;)Z",
               {VmValue::Int(2), VmValue::Int(0), VmValue::Int(4), VmValue::Int(2), difference})
              .AsInt() == 1);
    CHECK(f.On(region, "isComplex", "()Z").AsInt() == 1);
    CHECK(f.On(region, "contains", "(II)Z", {VmValue::Int(1), VmValue::Int(1)}).AsInt() == 1);
    CHECK(f.On(region, "contains", "(II)Z", {VmValue::Int(3), VmValue::Int(1)}).AsInt() == 0);
    CHECK(f.On(iterator, "next", "(Landroid/graphics/Rect;)Z", {VmValue::Ref(rect)}).AsInt() == 1);
    CHECK(f.On(rect, "width", "()I").AsInt() == 6);
    CHECK(f.On(iterator, "next", "(Landroid/graphics/Rect;)Z", {VmValue::Ref(rect)}).AsInt() == 0);
    const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;");
    const auto parcel_root = f.vm.ProtectReferences(std::array{parcel.ref});
    f.On(region, "writeToParcel", "(Landroid/os/Parcel;I)V", {parcel, VmValue::Int(0)});
    f.On(parcel.ref, "setDataPosition", "(I)V", {VmValue::Int(0)});
    const auto copy = f.Static(region_type, "nativeCreateFromParcel", "(Landroid/os/Parcel;)I", {parcel});
    const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(region), "mNativeRegion", "I");
    REQUIRE(field);
    const auto handle = f.model.InstanceSlots(region)[f.linker.Field(*field).slot].bits;
    CHECK(f.Static(region_type, "nativeEquals", "(II)Z", {VmValue::Int(std::bit_cast<std::int32_t>(handle)), copy})
              .AsInt() == 1);
    f.Static(region_type, "nativeDestructor", "(I)V", {copy});
}

TEST_CASE("Skia ROM geometry handles reject wrong kinds and retire with the owner") {
    AndroidValueVm f;
    constexpr auto path_type = "Landroid/graphics/Path;";
    constexpr auto matrix_type = "Landroid/graphics/Matrix;";
    const auto path = f.New(path_type);
    const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(path), "mNativePath", "I");
    REQUIRE(field);
    const auto bits = f.model.InstanceSlots(path)[f.linker.Field(*field).slot].bits;
    REQUIRE(bits != 0U);
    const auto handle = VmValue::Int(std::bit_cast<std::int32_t>(bits));
    CHECK(f.Static(path_type, "native_isEmpty", "(I)Z", {handle}).AsInt() == 1);
    CHECK(f.StaticOutcome(matrix_type, "native_isIdentity", "(I)Z", {handle}).exception.IsValid());
    static_cast<void>(f.vm.CollectGarbage("Skia Path GC"));
    f.vm.RunFinalization();
    static_cast<void>(f.vm.CollectGarbage("Skia Path sweep"));
    CHECK(f.StaticOutcome(path_type, "native_isEmpty", "(I)Z", {handle}).exception.IsValid());
    CHECK_FALSE(f.StaticOutcome(path_type, "finalizer", "(I)V", {handle}).exception.IsValid());
}

TEST_CASE("Skia ROM Color converts HSV with the requested alpha") {
    AndroidValueVm f;
    const auto hsv = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 3);
    const auto roots = f.vm.ProtectReferences(std::array{hsv});
    constexpr auto color = "Landroid/graphics/Color;";
    f.Static(color, "RGBToHSV", "(III[F)V", {VmValue::Int(0), VmValue::Int(255), VmValue::Int(0), VmValue::Ref(hsv)});
    CHECK(f.model.GetPrimitiveElement(hsv, 0) == VmValue::Float(120.0F).cat1);
    CHECK(f.Static(color, "HSVToColor", "(I[F)I", {VmValue::Int(128), VmValue::Ref(hsv)}).AsInt() ==
          std::bit_cast<std::int32_t>(0x8000ff00U));
}
}  // namespace
