#include "android_value_fixture.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/utils/SkCamera.h"
#include "ogplay/runtime/raster/bitmap.h"

#include <array>
#include <bit>
#include <doctest/doctest.h>

namespace {
using namespace ogplay::test::android_value;
using V = VmValue;

TEST_CASE("ROM Camera matrices and Canvas pixels match Sk3DView") {
    AndroidValueVm f;
    const auto camera = f.New("Landroid/graphics/Camera;");
    const auto matrix = f.New("Landroid/graphics/Matrix;");
    const auto values = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 9);
    const auto roots = f.vm.ProtectReferences(std::array{camera, matrix, values});
    Sk3DView host;
    const auto check = [&] {
        f.On(camera, "getMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
        f.On(matrix, "getValues", "([F)V", {V::Ref(values)});
        SkMatrix expected;
        host.getMatrix(&expected);
        for (int i = 0; i < 9; ++i)
            CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(values, i))) ==
                  doctest::Approx(expected[i]));
    };
    check();
    f.On(camera, "save", "()V");
    host.save();
    f.On(camera, "translate", "(FFF)V", {V::Float(4), V::Float(5), V::Float(23)});
    host.translate(4, 5, 23);
    f.On(camera, "rotateX", "(F)V", {V::Float(17)});
    host.rotateX(17);
    f.On(camera, "rotateY", "(F)V", {V::Float(31)});
    host.rotateY(31);
    f.On(camera, "rotateZ", "(F)V", {V::Float(-8)});
    host.rotateZ(-8);
    check();
    f.On(camera, "rotate", "(FFF)V", {V::Float(1), V::Float(2), V::Float(3)});
    host.rotateX(1);
    host.rotateY(2);
    host.rotateZ(3);
    check();
    CHECK(f.On(camera, "dotWithNormal", "(FFF)F", {V::Float(2), V::Float(3), V::Float(4)}).AsFloat() ==
          doctest::Approx(host.dotWithNormal(2, 3, 4)));
    const auto config =
        f.Static("Landroid/graphics/Bitmap$Config;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                 {V::Ref(f.vm.NewStringUtf8("ARGB_8888"))});
    const auto bitmap =
        f.Static("Landroid/graphics/Bitmap;", "createBitmap",
                 "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", {V::Int(32), V::Int(32), config})
            .ref;
    const auto bitmap_root = f.vm.ProtectReferences(std::array{bitmap});
    const auto canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(bitmap)});
    const auto paint = f.New("Landroid/graphics/Paint;");
    const auto drawing_roots = f.vm.ProtectReferences(std::array{canvas, paint});
    f.On(camera, "applyToCanvas", "(Landroid/graphics/Canvas;)V", {V::Ref(canvas)});
    f.On(canvas, "drawRect", "(FFFFLandroid/graphics/Paint;)V",
         {V::Float(0), V::Float(0), V::Float(20), V::Float(20), V::Ref(paint)});
    SkBitmap expected;
    expected.allocN32Pixels(32, 32);
    expected.eraseColor(0);
    SkCanvas target(expected);
    host.applyToCanvas(&target);
    target.drawRect(SkRect::MakeWH(20, 20), SkPaint{});
    const auto actual = f.context->bitmaps.at(bitmap.Value()).pixels->Snapshot();
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            CHECK(actual[static_cast<std::size_t>(y * 32 + x)] == expected.getColor(x, y));
    f.On(camera, "restore", "()V");
    host.restore();
    check();
}

TEST_CASE("ROM Camera location projection follows Android inches and survives save restore") {
    AndroidValueVm f;
    const auto camera = f.New("Landroid/graphics/Camera;");
    const auto matrix = f.New("Landroid/graphics/Matrix;");
    const auto values = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 9);
    const auto roots = f.vm.ProtectReferences(std::array{camera, matrix, values});
    CHECK(f.On(camera, "getLocationX", "()F").AsFloat() == 0);
    CHECK(f.On(camera, "getLocationY", "()F").AsFloat() == 0);
    CHECK(f.On(camera, "getLocationZ", "()F").AsFloat() == -8);
    f.On(camera, "translate", "(FFF)V", {V::Float(4), V::Float(5), V::Float(23)});
    f.On(camera, "rotateY", "(F)V", {V::Float(31)});
    f.On(camera, "save", "()V");
    f.On(camera, "setLocation", "(FFF)V", {V::Float(1), V::Float(2), V::Float(-12)});
    CHECK(f.On(camera, "getLocationX", "()F").AsFloat() == 1);
    CHECK(f.On(camera, "getLocationY", "()F").AsFloat() == 2);
    CHECK(f.On(camera, "getLocationZ", "()F").AsFloat() == -12);
    f.On(camera, "restore", "()V");
    f.On(camera, "getMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
    f.On(matrix, "getValues", "([F)V", {V::Ref(values)});
    SkM44 transform;
    transform.preTranslate(4, 5, 23);
    transform.preConcat(SkM44::Rotate({0, -1, 0}, 31 * SK_ScalarPI / 180));
    SkPatch3D patch;
    patch.transform(transform);
    SkCamera3D projection;
    projection.fLocation = {72, 144, -864};
    projection.fObserver = {0, 0, -864};
    projection.update();
    SkMatrix expected;
    projection.patchToMatrix(patch, &expected);
    for (int i = 0; i < 9; ++i)
        CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(values, i))) ==
              doctest::Approx(expected[i]));
}
}  // namespace
