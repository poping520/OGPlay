#include <algorithm>
#include <array>
#include <bit>
#include <doctest/doctest.h>

#include "android_value_fixture.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"

namespace {
using namespace ogplay::test::android_value;
using V = VmValue;
constexpr auto kBitmap = "Landroid/graphics/Bitmap;";
constexpr auto kCanvas = "Landroid/graphics/Canvas;";
constexpr auto kRegionOp = "Landroid/graphics/Region$Op;";
constexpr auto kMode = "Landroid/graphics/PorterDuff$Mode;";

V Color(std::uint32_t color) {
    return V::Int(std::bit_cast<int>(color));
}

V Enum(AndroidValueVm& f, const char* type, const char* name) {
    const auto signature = std::string("(Ljava/lang/String;)") + type;
    return f.Static(type, "valueOf", signature.c_str(), {V::Ref(f.vm.NewStringUtf8(name))});
}

VmObjectRef Bitmap(AndroidValueVm& f, int width, int height) {
    return f
        .Static(kBitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                {V::Int(width), V::Int(height), Enum(f, "Landroid/graphics/Bitmap$Config;", "ARGB_8888")})
        .ref;
}

VmObjectRef Canvas(AndroidValueVm& f, VmObjectRef bitmap) {
    return f.New(kCanvas, "(Landroid/graphics/Bitmap;)V", {V::Ref(bitmap)});
}

std::uint32_t Pixel(AndroidValueVm& f, VmObjectRef bitmap, int x, int y) {
    return static_cast<std::uint32_t>(f.On(bitmap, "getPixel", "(II)I", {V::Int(x), V::Int(y)}).AsInt());
}

void DrawColor(AndroidValueVm& f, VmObjectRef canvas, std::uint32_t color) {
    f.On(canvas, "drawColor", "(I)V", {Color(color)});
}

TEST_CASE("ROM Canvas Region.Op draws each device-space Boolean clip") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto bitmap = Bitmap(f, 12, 4);
        const auto canvas = Canvas(f, bitmap);
        const auto path = f.New("Landroid/graphics/Path;");
        const auto region =
            f.New("Landroid/graphics/Region;", "(IIII)V", {V::Int(4), V::Int(0), V::Int(10), V::Int(4)});
        const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas, path, region});
        f.On(path, "addRect", "(FFFFLandroid/graphics/Path$Direction;)V",
             {V::Float(0), V::Float(0), V::Float(6), V::Float(4), Enum(f, "Landroid/graphics/Path$Direction;", "CW")});

        for (const auto& [name, expected] : {std::pair{"DIFFERENCE", std::array{true, false, false}},
                                             {"INTERSECT", {false, true, false}},
                                             {"UNION", {true, true, true}},
                                             {"XOR", {true, false, true}},
                                             {"REVERSE_DIFFERENCE", {false, false, true}},
                                             {"REPLACE", {false, true, true}}}) {
            for (int shape = 0; shape < 3; ++shape) {
                CAPTURE(name);
                CAPTURE(shape);
                f.On(bitmap, "eraseColor", "(I)V", {Color(0)});
                CHECK(f.On(canvas, "save", "()I").AsInt() == 1);
                REQUIRE(f.On(canvas, "clipRect", "(IIII)Z", {V::Int(0), V::Int(0), V::Int(6), V::Int(4)}).AsInt());
                f.On(canvas, "translate", "(FF)V", {V::Float(4), V::Float(0)});
                const auto op = Enum(f, kRegionOp, name);
                if (shape == 0) {
                    CHECK(f.On(canvas, "clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
                               {V::Float(0), V::Float(0), V::Float(6), V::Float(4), op})
                              .AsInt());
                } else if (shape == 1) {
                    CHECK(f.On(canvas, "clipPath", "(Landroid/graphics/Path;Landroid/graphics/Region$Op;)Z",
                               {V::Ref(path), op})
                              .AsInt());
                } else {
                    CHECK(f.On(canvas, "clipRegion", "(Landroid/graphics/Region;Landroid/graphics/Region$Op;)Z",
                               {V::Ref(region), op})
                              .AsInt());
                }
                DrawColor(f, canvas, 0xff458aff);
                for (int x = 0; x < 12; ++x) {
                    const bool covered = x < 4 ? expected[0] : x < 6 ? expected[1] : x < 10 && expected[2];
                    CHECK(Pixel(f, bitmap, x, 2) == (covered ? 0xff458affU : 0U));
                }
                f.On(canvas, "restoreToCount", "(I)V", {V::Int(1)});
            }
        }
    }
}

TEST_CASE("ROM Canvas saves matrix and clip independently and replays clip on setBitmap") {
    AndroidValueVm f;
    const auto first = Bitmap(f, 8, 8);
    const auto second = Bitmap(f, 16, 8);
    const auto canvas = Canvas(f, first);
    const auto roots = f.vm.ProtectReferences(std::array{first, second, canvas});
    CHECK(f.On(canvas, "getSaveCount", "()I").AsInt() == 1);
    CHECK(f.On(canvas, "save", "(I)I", {V::Int(1)}).AsInt() == 1);
    f.On(canvas, "translate", "(FF)V", {V::Float(2), V::Float(1)});
    CHECK(f.On(canvas, "clipRect", "(FFFF)Z", {V::Float(0), V::Float(0), V::Float(8), V::Float(5)}).AsInt());
    const auto bounds = [&] {
        const auto rect = f.On(canvas, "getClipBounds", "()Landroid/graphics/Rect;").ref;
        const auto field = [&](const char* name) {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(rect), name, "I");
            return f.model.InstanceSlots(rect)[f.linker.Field(*id).slot].bits;
        };
        return std::array{field("left"), field("top"), field("right"), field("bottom")};
    };
    CHECK(bounds() == std::array{0U, 0U, 6U, 5U});
    f.On(canvas, "restore", "()V");
    CHECK(bounds() == std::array{2U, 1U, 8U, 6U});

    CHECK(f.On(canvas, "save", "(I)I", {V::Int(2)}).AsInt() == 1);
    f.On(canvas, "translate", "(FF)V", {V::Float(1), V::Float(0)});
    CHECK_FALSE(f.On(canvas, "clipRect", "(IIII)Z", {V::Int(100), V::Int(100), V::Int(101), V::Int(101)}).AsInt());
    f.On(canvas, "restore", "()V");
    CHECK(bounds() == std::array{1U, 1U, 7U, 6U});
    CHECK(f.On(canvas, "quickReject", "(FFFFLandroid/graphics/Canvas$EdgeType;)Z",
               {V::Float(-10), V::Float(-10), V::Float(-5), V::Float(-5),
                Enum(f, "Landroid/graphics/Canvas$EdgeType;", "BW")})
              .AsInt());
    const auto matrix = f.On(canvas, "getMatrix", "()Landroid/graphics/Matrix;").ref;
    CHECK_FALSE(f.On(matrix, "isIdentity", "()Z").AsInt());
    f.On(canvas, "setBitmap", "(Landroid/graphics/Bitmap;)V", {V::Ref(second)});
    CHECK(bounds() == std::array{1U, 1U, 9U, 6U});
    CHECK(f.On(canvas, "getSaveCount", "()I").AsInt() == 1);
    const auto underflow = f.OnOutcome(canvas, "restore", "()V");
    REQUIRE(underflow.exception.IsValid());
    CHECK(f.linker.Class(underflow.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
    const auto bad_count = f.OnOutcome(canvas, "restoreToCount", "(I)V", {V::Int(0)});
    REQUIRE(bad_count.exception.IsValid());
    CHECK(f.linker.Class(bad_count.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
}

TEST_CASE("ROM Canvas keeps out-of-device REPLACE bounds for clip queries") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 4, 4);
    const auto canvas = Canvas(f, bitmap);
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas});
    const auto replace = Enum(f, kRegionOp, "REPLACE");
    REQUIRE(f.On(canvas, "clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
                 {V::Float(9), V::Float(7), V::Float(13), V::Float(10), replace})
                .AsInt());
    const auto bounds = f.On(canvas, "getClipBounds", "()Landroid/graphics/Rect;").ref;
    REQUIRE(bounds.IsValid());
    CHECK(f.On(bounds, "width", "()I").AsInt() == 4);
    CHECK(f.On(bounds, "height", "()I").AsInt() == 3);
    DrawColor(f, canvas, SK_ColorRED);
    CHECK(Pixel(f, bitmap, 3, 3) == 0);
}

TEST_CASE("ROM Canvas matrix concat, scale, rotate and skew match host Skia") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 10, 10);
    const auto canvas = Canvas(f, bitmap);
    const auto matrix = f.New("Landroid/graphics/Matrix;");
    const auto points = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 4);
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas, matrix, points});
    SkCanvas host(10, 10);
    f.On(matrix, "setTranslate", "(FF)V", {V::Float(4), V::Float(5)});
    f.On(canvas, "setMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
    host.setMatrix(SkMatrix::Translate(4, 5));
    f.On(canvas, "scale", "(FF)V", {V::Float(2), V::Float(3)});
    host.scale(2, 3);
    f.On(canvas, "rotate", "(F)V", {V::Float(35)});
    host.rotate(35);
    f.On(canvas, "skew", "(FF)V", {V::Float(0.25F), V::Float(0.5F)});
    host.skew(0.25F, 0.5F);
    f.On(canvas, "concat", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
    host.concat(SkMatrix::Translate(4, 5));
    f.On(canvas, "getMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
    constexpr std::array input{0.0F, 0.0F, 2.0F, 3.0F};
    for (int i = 0; i < 4; ++i)
        f.model.SetPrimitiveElement(points, i, V::Float(input[static_cast<std::size_t>(i)]).cat1);
    f.On(matrix, "mapPoints", "([F)V", {V::Ref(points)});
    const auto first = host.getLocalToDeviceAs3x3().mapPoint({0, 0});
    const auto second = host.getLocalToDeviceAs3x3().mapPoint({2, 3});
    const std::array expected{first.x(), first.y(), second.x(), second.y()};
    for (int i = 0; i < 4; ++i) {
        const auto bits = static_cast<std::uint32_t>(f.model.GetPrimitiveElement(points, i));
        CHECK(std::bit_cast<float>(bits) == doctest::Approx(expected[static_cast<std::size_t>(i)]));
    }
}

TEST_CASE("ROM Canvas saveLayerAlpha composites like host Skia and honors layer clipping") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 6, 4);
    const auto canvas = Canvas(f, bitmap);
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas});
    DrawColor(f, canvas, SK_ColorBLUE);
    CHECK(f.On(canvas, "saveLayerAlpha", "(FFFFII)I",
               {V::Float(1), V::Float(1), V::Float(4), V::Float(3), V::Int(128), V::Int(0x1f)})
              .AsInt() == 1);
    CHECK(f.On(canvas, "getSaveCount", "()I").AsInt() == 2);
    DrawColor(f, canvas, SK_ColorRED);
    CHECK(Pixel(f, bitmap, 2, 2) == SK_ColorBLUE);
    f.On(canvas, "restore", "()V");

    SkBitmap expected;
    expected.allocN32Pixels(6, 4);
    SkCanvas host(expected);
    host.clear(SK_ColorBLUE);
    host.save();
    host.clipRect(SkRect::MakeLTRB(1, 1, 4, 3));
    host.saveLayerAlpha(nullptr, 128);
    host.drawColor(SK_ColorRED);
    host.restoreToCount(1);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            CHECK(Pixel(f, bitmap, x, y) == expected.getColor(x, y));
}

TEST_CASE("ROM Canvas color modes match Skia on premultiplied Bitmap storage") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 1, 1);
    const auto canvas = Canvas(f, bitmap);
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas});
    SkBitmap expected;
    expected.allocN32Pixels(1, 1);
    SkCanvas host(expected);
    constexpr std::array<std::pair<const char*, SkBlendMode>, 18> modes{{
        std::pair{"CLEAR", SkBlendMode::kClear},
        {"SRC", SkBlendMode::kSrc},
        {"DST", SkBlendMode::kDst},
        {"SRC_OVER", SkBlendMode::kSrcOver},
        {"DST_OVER", SkBlendMode::kDstOver},
        {"SRC_IN", SkBlendMode::kSrcIn},
        {"DST_IN", SkBlendMode::kDstIn},
        {"SRC_OUT", SkBlendMode::kSrcOut},
        {"DST_OUT", SkBlendMode::kDstOut},
        {"SRC_ATOP", SkBlendMode::kSrcATop},
        {"DST_ATOP", SkBlendMode::kDstATop},
        {"XOR", SkBlendMode::kXor},
        {"DARKEN", SkBlendMode::kDarken},
        {"LIGHTEN", SkBlendMode::kLighten},
        {"MULTIPLY", SkBlendMode::kModulate},
        {"SCREEN", SkBlendMode::kScreen},
        {"ADD", SkBlendMode::kPlus},
        {"OVERLAY", SkBlendMode::kOverlay},
    }};
    constexpr std::uint32_t source = 0x8064c832, dest = 0xc8326496;
    for (const auto& [name, blend] : modes) {
        CAPTURE(name);
        f.On(bitmap, "eraseColor", "(I)V", {Color(dest)});
        f.On(canvas, "drawColor", "(ILandroid/graphics/PorterDuff$Mode;)V", {Color(source), Enum(f, kMode, name)});
        host.clear(dest);
        host.drawColor(source, blend);
        CHECK(Pixel(f, bitmap, 0, 0) == expected.getColor(0, 0));
    }
    f.On(canvas, "drawRGB", "(III)V", {V::Int(12), V::Int(34), V::Int(56)});
    CHECK(Pixel(f, bitmap, 0, 0) == 0xff0c2238U);
    f.On(canvas, "drawARGB", "(IIII)V", {V::Int(255), V::Int(90), V::Int(180), V::Int(240)});
    CHECK(Pixel(f, bitmap, 0, 0) == 0xff5ab4f0U);
}

TEST_CASE("ROM Canvas retains Bitmap pixels and rejects recycled and immutable targets") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 2, 2);
    const auto canvas = Canvas(f, bitmap);
    const auto root = f.vm.ProtectReferences(std::array{canvas});
    static_cast<void>(f.vm.CollectGarbage("Canvas Bitmap edge"));
    REQUIRE(f.model.IsValidRef(bitmap));
    const auto generation = f.On(bitmap, "getGenerationId", "()I").AsInt();
    DrawColor(f, canvas, 0xff112233);
    CHECK(Pixel(f, bitmap, 1, 1) == 0xff112233U);
    CHECK(f.On(bitmap, "getGenerationId", "()I").AsInt() != generation);

    const auto immutable = f.On(bitmap, "copy", "(Landroid/graphics/Bitmap$Config;Z)Landroid/graphics/Bitmap;",
                                {Enum(f, "Landroid/graphics/Bitmap$Config;", "ARGB_8888"), V::Int(0)})
                               .ref;
    const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(canvas), "<init>", "(Landroid/graphics/Bitmap;)V");
    REQUIRE(ctor);
    const auto construct = [&](VmObjectRef target, const char* exception) {
        const auto object = f.vm.NewIntrinsicInstance(kCanvas);
        const auto outcome = f.vm.Call(*ctor, std::array{V::Ref(object), V::Ref(target)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(f.linker.Class(outcome.exception_class).descriptor == exception);
    };
    construct(immutable, "Ljava/lang/IllegalStateException;");
    f.On(bitmap, "recycle", "()V");
    construct(bitmap, "Ljava/lang/RuntimeException;");
    const auto rejected = f.OnOutcome(canvas, "drawColor", "(I)V", {V::Int(0)});
    REQUIRE(rejected.exception.IsValid());
    CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/RuntimeException;");
}

TEST_CASE("ROM BitmapDrawable ColorDrawable and ShapeDrawable draw into a Bitmap Canvas") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 9, 4);
    const auto source = Bitmap(f, 1, 1);
    const auto canvas = Canvas(f, bitmap);
    const auto color = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V", {Color(0xff5070ac)});
    const auto shape = f.New("Landroid/graphics/drawable/ShapeDrawable;", "(Landroid/graphics/drawable/shapes/Shape;)V",
                             {V::Ref(f.New("Landroid/graphics/drawable/shapes/RectShape;"))});
    const auto drawable =
        f.New("Landroid/graphics/drawable/BitmapDrawable;", "(Landroid/graphics/Bitmap;)V", {V::Ref(source)});
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, source, canvas, color, shape, drawable});
    const auto paint = f.On(shape, "getPaint", "()Landroid/graphics/Paint;").ref;
    f.On(paint, "setColor", "(I)V", {Color(0xffd04223)});
    f.On(source, "eraseColor", "(I)V", {Color(0xff6fc851)});
    const std::array objects{color, shape, drawable};
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const int x = static_cast<int>(i) * 3;
        f.On(objects[i], "setBounds", "(IIII)V", {V::Int(x), V::Int(0), V::Int(x + 3), V::Int(4)});
        f.On(objects[i], "draw", "(Landroid/graphics/Canvas;)V", {V::Ref(canvas)});
    }
    CHECK(Pixel(f, bitmap, 1, 2) == 0xff5070acU);
    CHECK(Pixel(f, bitmap, 4, 2) == 0xffd04223U);
    CHECK(Pixel(f, bitmap, 7, 2) == 0xff6fc851U);
}

TEST_CASE("ROM Canvas draws bitmap colors, transformed bitmaps, meshes and indexed vertices") {
    AndroidValueVm f;
    const auto target = Bitmap(f, 12, 6);
    const auto source = Bitmap(f, 1, 1);
    const auto canvas = Canvas(f, target);
    const auto paint = f.New("Landroid/graphics/Paint;");
    const auto matrix = f.New("Landroid/graphics/Matrix;");
    const auto roots = f.vm.ProtectReferences(std::array{target, source, canvas, paint, matrix});
    f.On(source, "eraseColor", "(I)V", {Color(SK_ColorRED)});
    f.On(source, "setDensity", "(I)V", {V::Int(160)});
    f.On(canvas, "setDensity", "(I)V", {V::Int(320)});
    f.On(canvas, "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V",
         {V::Ref(source), V::Float(0), V::Float(0), V::Ref(paint)});
    CHECK(Pixel(f, target, 1, 1) == SK_ColorRED);
    CHECK(Pixel(f, target, 2, 1) == 0);

    f.On(matrix, "setTranslate", "(FF)V", {V::Float(2), V::Float(0)});
    f.On(canvas, "drawBitmap", "(Landroid/graphics/Bitmap;Landroid/graphics/Matrix;Landroid/graphics/Paint;)V",
         {V::Ref(source), V::Ref(matrix), V::Ref(paint)});
    CHECK(Pixel(f, target, 2, 0) == SK_ColorRED);
    CHECK(Pixel(f, target, 2, 1) == 0);

    const auto colors = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
    const auto points = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 8);
    const auto indices =
        f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[S"), JniPrimitiveKind::short_integer, 3);
    const auto arrays = f.vm.ProtectReferences(std::array{colors, points, indices});
    f.model.SetPrimitiveElement(colors, 0, SK_ColorGREEN);
    f.model.SetPrimitiveElement(colors, 1, SK_ColorBLUE);
    f.On(canvas, "drawBitmap", "([IIIFFIIZLandroid/graphics/Paint;)V",
         {V::Ref(colors), V::Int(1), V::Int(-1), V::Float(3), V::Float(0), V::Int(1), V::Int(2), V::Int(1),
          V::Ref(paint)});
    CHECK(Pixel(f, target, 3, 0) == SK_ColorBLUE);
    CHECK(Pixel(f, target, 3, 1) == SK_ColorGREEN);

    const auto set_points = [&](const std::array<float, 8>& xy) {
        for (int i = 0; i < 8; ++i)
            f.model.SetPrimitiveElement(points, i, V::Float(xy[static_cast<std::size_t>(i)]).cat1);
    };
    set_points({4, 0, 8, 0, 4, 4, 8, 4});
    f.On(canvas, "drawBitmapMesh", "(Landroid/graphics/Bitmap;II[FI[IILandroid/graphics/Paint;)V",
         {V::Ref(source), V::Int(1), V::Int(1), V::Ref(points), V::Int(0), V::Ref(VmObjectRef{}), V::Int(0),
          V::Ref(paint)});
    CHECK(Pixel(f, target, 6, 2) == SK_ColorRED);

    set_points({8, 0, 12, 0, 8, 4, 12, 4});
    for (int i = 0; i < 4; ++i)
        f.model.SetPrimitiveElement(colors, i, SK_ColorYELLOW);
    for (int i = 0; i < 3; ++i)
        f.model.SetPrimitiveElement(indices, i, static_cast<std::uint32_t>(i));
    const auto triangles = Enum(f, "Landroid/graphics/Canvas$VertexMode;", "TRIANGLES");
    f.On(canvas, "drawVertices", "(Landroid/graphics/Canvas$VertexMode;I[FI[FI[II[SIILandroid/graphics/Paint;)V",
         {triangles, V::Int(8), V::Ref(points), V::Int(0), V::Ref(VmObjectRef{}), V::Int(0), V::Ref(colors), V::Int(0),
          V::Ref(indices), V::Int(0), V::Int(3), V::Ref(paint)});
    CHECK(Pixel(f, target, 8, 1) == SK_ColorYELLOW);
    CHECK(Pixel(f, target, 11, 3) == 0);
    set_points({8, 2, 12, 2, 8, 6, 12, 6});
    f.On(paint, "setColor", "(I)V", {Color(SK_ColorBLUE)});
    f.On(canvas, "drawVertices", "(Landroid/graphics/Canvas$VertexMode;I[FI[FI[II[SIILandroid/graphics/Paint;)V",
         {triangles, V::Int(8), V::Ref(points), V::Int(0), V::Ref(VmObjectRef{}), V::Int(0), V::Ref(VmObjectRef{}),
          V::Int(0), V::Ref(indices), V::Int(0), V::Int(3), V::Ref(paint)});
    CHECK(Pixel(f, target, 10, 2) == SK_ColorBLUE);
    CHECK(Pixel(f, target, 9, 3) == 0);
}

TEST_CASE("ROM Canvas applies Paint blend state and saves DrawFilter state") {
    AndroidValueVm f;
    const auto bitmap = Bitmap(f, 8, 6);
    const auto canvas = Canvas(f, bitmap);
    const auto paint = f.New("Landroid/graphics/Paint;");
    const auto roots = f.vm.ProtectReferences(std::array{bitmap, canvas, paint});
    f.On(paint, "setColor", "(I)V", {Color(SK_ColorGREEN)});
    f.On(paint, "setStrokeWidth", "(F)V", {V::Float(2)});
    f.On(paint, "setStyle", "(Landroid/graphics/Paint$Style;)V", {Enum(f, "Landroid/graphics/Paint$Style;", "STROKE")});
    const auto rect = [&] {
        f.On(canvas, "drawRect", "(FFFFLandroid/graphics/Paint;)V",
             {V::Float(2), V::Float(2), V::Float(6), V::Float(5), V::Ref(paint)});
    };
    rect();
    CHECK(Pixel(f, bitmap, 2, 2) == SK_ColorGREEN);
    CHECK(Pixel(f, bitmap, 4, 3) == 0);
    const auto clear = f.New("Landroid/graphics/PorterDuffXfermode;", "(Landroid/graphics/PorterDuff$Mode;)V",
                             {Enum(f, kMode, "CLEAR")});
    f.On(paint, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;", {V::Ref(clear)});
    rect();
    CHECK(Pixel(f, bitmap, 2, 2) == 0);
    f.On(paint, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;", {V::Ref(VmObjectRef{})});

    const auto filter = f.New("Landroid/graphics/PaintFlagsDrawFilter;", "(II)V", {V::Int(0), V::Int(1)});
    f.On(canvas, "save", "()I");
    f.On(canvas, "setDrawFilter", "(Landroid/graphics/DrawFilter;)V", {V::Ref(filter)});
    CHECK(f.On(canvas, "getDrawFilter", "()Landroid/graphics/DrawFilter;").ref == filter);
    rect();
    f.On(canvas, "restore", "()V");
    CHECK(f.On(canvas, "getDrawFilter", "()Landroid/graphics/DrawFilter;").ref == filter);
    rect();
    const auto warnings = f.ledger.Warnings();
    CHECK(
        std::ranges::none_of(warnings, [](const auto& hit) { return hit.id.starts_with("ui.degraded.Canvas.Paint"); }));
}

}  // namespace
