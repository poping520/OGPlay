#include "android_value_fixture.h"

#include "include/core/SkColorFilter.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkVertices.h"
#include "include/effects/Sk1DPathEffect.h"
#include "include/effects/SkCornerPathEffect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkDiscretePathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#include "ogplay/runtime/raster/bitmap.h"

#include <array>
#include <bit>
#include <doctest/doctest.h>

namespace {
using namespace ogplay::test::android_value;
using V = VmValue;
constexpr auto kPaint = "Landroid/graphics/Paint;";
constexpr auto kShader = "Landroid/graphics/Shader;";
constexpr auto kBitmap = "Landroid/graphics/Bitmap;";

V Color(SkColor color) {
    return V::Int(std::bit_cast<int>(color));
}

V Enum(AndroidValueVm& f, const char* type, const char* name) {
    return f.Static(type, "valueOf", (std::string("(Ljava/lang/String;)") + type).c_str(),
                    {V::Ref(f.vm.NewStringUtf8(name))});
}

VmObjectRef Floats(AndroidValueVm& f, std::initializer_list<float> values) {
    const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value,
                                                 static_cast<int>(values.size()));
    int i = 0;
    for (auto value : values)
        f.model.SetPrimitiveElement(array, i++, V::Float(value).cat1);
    return array;
}

struct Scene {
    AndroidValueVm f;
    VmObjectRef bitmap =
        f.Static(kBitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                 {V::Int(40), V::Int(36), Enum(f, "Landroid/graphics/Bitmap$Config;", "ARGB_8888")})
            .ref;
    Interpreter::RootScope bitmap_root = f.vm.ProtectReferences(std::array{bitmap});
    VmObjectRef canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(bitmap)});
    Interpreter::RootScope canvas_root = f.vm.ProtectReferences(std::array{canvas});
    VmObjectRef paint = f.New(kPaint);
    Interpreter::RootScope paint_root = f.vm.ProtectReferences(std::array{paint});

    void Set(const char* method, const char* type, VmObjectRef value) {
        f.On(paint, method, (std::string("(") + type + ")" + type).c_str(), {V::Ref(value)});
    }

    void Draw() {
        f.On(canvas, "drawRect", "(FFFFLandroid/graphics/Paint;)V",
             {V::Float(3), V::Float(3), V::Float(35), V::Float(31), V::Ref(paint)});
    }

    void Clear(SkColor color = 0) { f.On(bitmap, "eraseColor", "(I)V", {Color(color)}); }

    void Compare(const SkBitmap& expected) {
        const auto actual = f.context->bitmaps.at(bitmap.Value()).pixels->Snapshot();
        REQUIRE(actual.size() == 40 * 36);
        int mismatches = 0;
        for (int y = 0; y < 36; ++y)
            for (int x = 0; x < 40; ++x)
                mismatches += actual[static_cast<std::size_t>(y * 40 + x)] != expected.getColor(x, y);
        CHECK(mismatches == 0);
    }

    template <class Draw>
    void Compare(Draw draw, SkColor background = 0) {
        SkBitmap expected;
        expected.allocN32Pixels(40, 36);
        expected.eraseColor(background);
        SkCanvas host(expected);
        draw(host);
        Compare(expected);
    }
};

sk_sp<SkShader> HostGradient(int kind, SkTileMode tile) {
    const SkColor4f colors[]{SkColor4f::FromColor(SK_ColorRED), SkColor4f::FromColor(SK_ColorBLUE)};
    const SkGradient gradient({colors, tile}, {});
    const SkPoint points[]{{9, 8}, {21, 19}};
    if (kind == 0) return SkShaders::LinearGradient(points, gradient);
    if (kind == 1) return SkShaders::RadialGradient(points[0], 12, gradient);
    return SkShaders::SweepGradient(points[0], gradient);
}

VmObjectRef Gradient(Scene& s, int kind, const char* tile) {
    const auto mode = Enum(s.f, "Landroid/graphics/Shader$TileMode;", tile);
    if (kind == 0)
        return s.f.New(
            "Landroid/graphics/LinearGradient;", "(FFFFIILandroid/graphics/Shader$TileMode;)V",
            {V::Float(9), V::Float(8), V::Float(21), V::Float(19), Color(SK_ColorRED), Color(SK_ColorBLUE), mode});
    if (kind == 1)
        return s.f.New("Landroid/graphics/RadialGradient;", "(FFFIILandroid/graphics/Shader$TileMode;)V",
                       {V::Float(9), V::Float(8), V::Float(12), Color(SK_ColorRED), Color(SK_ColorBLUE), mode});
    return s.f.New("Landroid/graphics/SweepGradient;", "(FFII)V",
                   {V::Float(9), V::Float(8), Color(SK_ColorRED), Color(SK_ColorBLUE)});
}

TEST_CASE("ROM shaders tile gradients and compose with both PorterDuff constructors") {
    Scene s;
    for (int kind = 0; kind < 3; ++kind) {
        int tile_index = 0;
        for (const auto tile : {"CLAMP", "REPEAT", "MIRROR"}) {
            CAPTURE(kind);
            CAPTURE(tile);
            s.Clear();
            const auto shader = Gradient(s, kind, tile);
            s.Set("setShader", kShader, shader);
            s.Draw();
            SkPaint paint;
            paint.setShader(HostGradient(kind, static_cast<SkTileMode>(tile_index++)));
            s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
        }
    }
    const auto dst = Gradient(s, 0, "CLAMP"), src = Gradient(s, 1, "MIRROR");
    const auto roots = s.f.vm.ProtectReferences(std::array{dst, src});
    const auto mode = Enum(s.f, "Landroid/graphics/PorterDuff$Mode;", "SRC_IN");
    const auto xfer = s.f.New("Landroid/graphics/PorterDuffXfermode;", "(Landroid/graphics/PorterDuff$Mode;)V", {mode});
    const auto xfer_root = s.f.vm.ProtectReferences(std::array{xfer});
    for (const bool porter : {false, true}) {
        s.Clear();
        const auto compose =
            s.f.New("Landroid/graphics/ComposeShader;",
                    porter ? "(Landroid/graphics/Shader;Landroid/graphics/Shader;Landroid/graphics/PorterDuff$Mode;)V"
                           : "(Landroid/graphics/Shader;Landroid/graphics/Shader;Landroid/graphics/Xfermode;)V",
                    {V::Ref(dst), V::Ref(src), porter ? mode : V::Ref(xfer)});
        s.Set("setShader", kShader, compose);
        s.Draw();
        SkPaint paint;
        paint.setShader(SkShaders::Blend(SkBlendMode::kSrcIn, HostGradient(0, SkTileMode::kClamp),
                                         HostGradient(1, SkTileMode::kMirror)));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
    }
}

TEST_CASE("ROM BitmapShader sees writes and retains recycled source pixels") {
    Scene s;
    const auto source =
        s.f.Static(kBitmap, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                   {V::Int(2), V::Int(2), Enum(s.f, "Landroid/graphics/Bitmap$Config;", "ARGB_8888")})
            .ref;
    const auto source_root = s.f.vm.ProtectReferences(std::array{source});
    SkBitmap host;
    host.allocN32Pixels(2, 2);
    for (int x = 0; x < 3; ++x) {
        int y = 0;
        for (const auto tile : {"CLAMP", "REPEAT", "MIRROR"}) {
            const auto x_mode = static_cast<SkTileMode>(x), y_mode = static_cast<SkTileMode>(y++);
            const char* names[]{"CLAMP", "REPEAT", "MIRROR"};
            const auto shader = s.f.New(
                "Landroid/graphics/BitmapShader;",
                "(Landroid/graphics/Bitmap;Landroid/graphics/Shader$TileMode;Landroid/graphics/Shader$TileMode;)V",
                {V::Ref(source), Enum(s.f, "Landroid/graphics/Shader$TileMode;", names[x]),
                 Enum(s.f, "Landroid/graphics/Shader$TileMode;", tile)});
            s.Set("setShader", kShader, shader);
            for (const auto color : {SK_ColorRED, SK_ColorBLUE}) {
                s.f.On(source, "eraseColor", "(I)V", {Color(color)});
                s.f.On(source, "setPixel", "(III)V", {V::Int(1), V::Int(1), Color(SK_ColorGREEN)});
                host.eraseColor(color);
                *host.getAddr32(1, 1) = SK_ColorGREEN;
                host.notifyPixelsChanged();
                s.Clear();
                s.Draw();
                SkPaint paint;
                paint.setShader(SkImages::RasterFromBitmap(host)->makeShader(x_mode, y_mode, {}));
                s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
            }
        }
    }
    s.f.On(source, "recycle", "()V");
    s.Clear();
    s.Draw();
    SkPaint paint;
    paint.setShader(SkImages::RasterFromBitmap(host)->makeShader(SkTileMode::kMirror, SkTileMode::kMirror, {}));
    s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
}

TEST_CASE("ROM Paint applies all eighteen PorterDuff modes") {
    Scene s;
    constexpr const char* names[]{"CLEAR",  "SRC",     "DST",      "SRC_OVER", "DST_OVER", "SRC_IN",
                                  "DST_IN", "SRC_OUT", "DST_OUT",  "SRC_ATOP", "DST_ATOP", "XOR",
                                  "DARKEN", "LIGHTEN", "MULTIPLY", "SCREEN",   "ADD",      "OVERLAY"};
    constexpr SkBlendMode modes[]{
        SkBlendMode::kClear,   SkBlendMode::kSrc,   SkBlendMode::kDst,    SkBlendMode::kSrcOver, SkBlendMode::kDstOver,
        SkBlendMode::kSrcIn,   SkBlendMode::kDstIn, SkBlendMode::kSrcOut, SkBlendMode::kDstOut,  SkBlendMode::kSrcATop,
        SkBlendMode::kDstATop, SkBlendMode::kXor,   SkBlendMode::kDarken, SkBlendMode::kLighten, SkBlendMode::kModulate,
        SkBlendMode::kScreen,  SkBlendMode::kPlus,  SkBlendMode::kOverlay};
    s.f.On(s.paint, "setColor", "(I)V", {Color(0x80e06020)});
    for (std::size_t i = 0; i < std::size(names); ++i) {
        CAPTURE(names[i]);
        const auto xfer = s.f.New("Landroid/graphics/PorterDuffXfermode;", "(Landroid/graphics/PorterDuff$Mode;)V",
                                  {Enum(s.f, "Landroid/graphics/PorterDuff$Mode;", names[i])});
        s.Set("setXfermode", "Landroid/graphics/Xfermode;", xfer);
        s.Clear(0x804080c0);
        s.Draw();
        SkPaint paint;
        paint.setColor(0x80e06020);
        paint.setBlendMode(modes[i]);
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); }, 0x804080c0);
    }
}

TEST_CASE("ROM ColorFilters use API19 matrix translation scale") {
    Scene s;
    s.f.On(s.paint, "setColor", "(I)V", {Color(SK_ColorCYAN)});
    const auto values = Floats(s.f, {1, 0, 0, 0, 64, 0, 0.5F, 0, 0, 16, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0});
    const auto values_root = s.f.vm.ProtectReferences(std::array{values});
    for (int i = 0; i < 3; ++i) {
        VmObjectRef filter;
        sk_sp<SkColorFilter> host;
        if (i == 0) {
            filter = s.f.New("Landroid/graphics/PorterDuffColorFilter;", "(ILandroid/graphics/PorterDuff$Mode;)V",
                             {Color(0x80ff4000), Enum(s.f, "Landroid/graphics/PorterDuff$Mode;", "SRC_ATOP")});
            host = SkColorFilters::Blend(0x80ff4000, SkBlendMode::kSrcATop);
        } else if (i == 1) {
            filter = s.f.New("Landroid/graphics/LightingColorFilter;", "(II)V", {Color(0xff804020), Color(0xff102030)});
            host = SkColorFilters::Lighting(0xff804020, 0xff102030);
        } else {
            filter = s.f.New("Landroid/graphics/ColorMatrixColorFilter;", "([F)V", {V::Ref(values)});
            const float matrix[]{1, 0, 0, 0, 64.F / 255, 0, 0.5F, 0, 0, 16.F / 255, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
            host = SkColorFilters::Matrix(matrix);
        }
        s.Set("setColorFilter", "Landroid/graphics/ColorFilter;", filter);
        s.Clear();
        s.Draw();
        SkPaint paint;
        paint.setColor(SK_ColorCYAN);
        paint.setColorFilter(std::move(host));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
    }
}

TEST_CASE("ROM BlurMaskFilter maps four styles and converts radius to sigma") {
    Scene s;
    int style = 0;
    for (const auto name : {"NORMAL", "SOLID", "OUTER", "INNER"}) {
        CAPTURE(name);
        const auto filter = s.f.New("Landroid/graphics/BlurMaskFilter;", "(FLandroid/graphics/BlurMaskFilter$Blur;)V",
                                    {V::Float(3), Enum(s.f, "Landroid/graphics/BlurMaskFilter$Blur;", name)});
        s.Set("setMaskFilter", "Landroid/graphics/MaskFilter;", filter);
        s.Clear();
        s.Draw();
        SkPaint paint;
        paint.setMaskFilter(SkMaskFilter::MakeBlur(static_cast<SkBlurStyle>(style++), 3 * 0.57735F + 0.5F));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
    }
}

TEST_CASE("ROM PathEffects draw corners dash phase discrete path dash compose and sum") {
    Scene s;
    const auto shape = s.f.New("Landroid/graphics/Path;");
    const auto shape_root = s.f.vm.ProtectReferences(std::array{shape});
    s.f.On(
        shape, "addRect", "(FFFFLandroid/graphics/Path$Direction;)V",
        {V::Float(-1), V::Float(-1), V::Float(2), V::Float(1), Enum(s.f, "Landroid/graphics/Path$Direction;", "CW")});
    const auto intervals = Floats(s.f, {3, 2, 5, 2});
    const auto intervals_root = s.f.vm.ProtectReferences(std::array{intervals});
    const auto corner = s.f.New("Landroid/graphics/CornerPathEffect;", "(F)V", {V::Float(5)});
    const auto corner_root = s.f.vm.ProtectReferences(std::array{corner});
    const auto dash = s.f.New("Landroid/graphics/DashPathEffect;", "([FF)V", {V::Ref(intervals), V::Float(1.5F)});
    const auto dash_root = s.f.vm.ProtectReferences(std::array{dash});
    const auto discrete = s.f.New("Landroid/graphics/DiscretePathEffect;", "(FF)V", {V::Float(4), V::Float(2)});
    const auto discrete_root = s.f.vm.ProtectReferences(std::array{discrete});
    auto host_corner = SkCornerPathEffect::Make(5);
    const float dashes[]{3, 2, 5, 2};
    auto host_dash = SkDashPathEffect::Make(dashes, 1.5F);
    s.f.On(s.paint, "setStyle", "(Landroid/graphics/Paint$Style;)V",
           {Enum(s.f, "Landroid/graphics/Paint$Style;", "STROKE")});
    s.f.On(s.paint, "setStrokeWidth", "(F)V", {V::Float(2)});
    const auto compare = [&](VmObjectRef effect, sk_sp<SkPathEffect> host) {
        s.Set("setPathEffect", "Landroid/graphics/PathEffect;", effect);
        s.Clear();
        s.Draw();
        SkPaint paint;
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(2);
        paint.setPathEffect(std::move(host));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
    };
    compare(corner, host_corner);
    compare(dash, host_dash);
    compare(discrete, SkDiscretePathEffect::Make(4, 2));
    int style = 0;
    for (const auto name : {"TRANSLATE", "ROTATE", "MORPH"}) {
        const auto effect = s.f.New(
            "Landroid/graphics/PathDashPathEffect;",
            "(Landroid/graphics/Path;FFLandroid/graphics/PathDashPathEffect$Style;)V",
            {V::Ref(shape), V::Float(6), V::Float(2), Enum(s.f, "Landroid/graphics/PathDashPathEffect$Style;", name)});
        compare(effect, SkPath1DPathEffect::Make(SkPathBuilder{}.addRect(SkRect::MakeLTRB(-1, -1, 2, 1)).detach(), 6, 2,
                                                 static_cast<SkPath1DPathEffect::Style>(style++)));
    }
    for (const bool compose : {false, true}) {
        const auto effect =
            s.f.New(compose ? "Landroid/graphics/ComposePathEffect;" : "Landroid/graphics/SumPathEffect;",
                    "(Landroid/graphics/PathEffect;Landroid/graphics/PathEffect;)V", {V::Ref(corner), V::Ref(dash)});
        compare(effect, compose ? SkPathEffect::MakeCompose(host_corner, host_dash)
                                : SkPathEffect::MakeSum(host_corner, host_dash));
    }
}

TEST_CASE("ROM shadow and DrawFilter draw copies without changing Paint flags") {
    Scene s;
    s.f.On(s.paint, "setColor", "(I)V", {Color(SK_ColorRED)});
    s.f.On(s.paint, "setShadowLayer", "(FFFI)V", {V::Float(2), V::Float(3), V::Float(2), Color(SK_ColorBLUE)});
    s.Draw();
    SkPaint shadow;
    shadow.setColor(SK_ColorRED);
    shadow.setImageFilter(
        SkImageFilters::DropShadow(3, 2, 2 * 0.57735F + 0.5F, 2 * 0.57735F + 0.5F, SK_ColorBLUE, nullptr));
    s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), shadow); });
    s.f.On(s.paint, "setShadowLayer", "(FFFI)V", {V::Float(0), V::Float(3), V::Float(2), Color(SK_ColorBLUE)});
    s.Clear();
    s.Draw();
    SkPaint plain;
    plain.setColor(SK_ColorRED);
    s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), plain); });

    for (const bool aa : {true, false}) {
        s.f.On(s.paint, "setAntiAlias", "(Z)V", {V::Int(!aa)});
        const auto filter =
            s.f.New("Landroid/graphics/PaintFlagsDrawFilter;", "(II)V", {V::Int(aa ? 0 : 1), V::Int(aa ? 1 : 0)});
        s.f.On(s.canvas, "setDrawFilter", "(Landroid/graphics/DrawFilter;)V", {V::Ref(filter)});
        s.Clear();
        s.f.On(s.canvas, "drawCircle", "(FFFLandroid/graphics/Paint;)V",
               {V::Float(18.25F), V::Float(17.25F), V::Float(10.5F), V::Ref(s.paint)});
        plain.setAntiAlias(aa);
        s.Compare([&](SkCanvas& canvas) { canvas.drawCircle(18.25F, 17.25F, 10.5F, plain); });
        CHECK(s.f.On(s.paint, "isAntiAlias", "()Z").AsInt() == !aa);
    }
}

TEST_CASE("ROM Paint copies retain effects after native handle retirement and allow replacement") {
    Scene s;
    const auto shader = Gradient(s, 0, "REPEAT");
    s.Set("setShader", kShader, shader);
    const auto copy = s.f.New(kPaint, "(Landroid/graphics/Paint;)V", {V::Ref(s.paint)});
    const auto copy_root = s.f.vm.ProtectReferences(std::array{copy});
    const auto native = s.f.linker.FindFieldRecursive(s.f.model.ObjectClass(shader), "native_instance", "I");
    REQUIRE(native);
    const auto token = V::Int(std::bit_cast<int>(s.f.model.InstanceSlots(shader)[s.f.linker.Field(*native).slot].bits));
    s.f.Static(kShader, "nativeDestructor", "(II)V", {token, V::Int(0)});
    CHECK(s.f.StaticOutcome(kShader, "nativeSetLocalMatrix", "(III)V", {token, V::Int(0), V::Int(0)})
              .exception.IsValid());
    s.Set("setShader", kShader, VmObjectRef{});
    s.Clear();
    s.f.On(s.canvas, "drawPaint", "(Landroid/graphics/Paint;)V", {V::Ref(copy)});
    SkPaint host;
    host.setShader(HostGradient(0, SkTileMode::kRepeat));
    s.Compare([&](SkCanvas& canvas) { canvas.drawPaint(host); });
    s.f.On(copy, "setShader", "(Landroid/graphics/Shader;)Landroid/graphics/Shader;", {V::Ref(VmObjectRef{})});
    s.Clear();
    s.Draw();
    s.Compare([](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), SkPaint{}); });
}
}  // namespace

namespace {
TEST_CASE("ROM gradient arrays local matrices and bitmap vertex effects draw through Skia") {
    Scene s;
    const auto colors = s.f.model.NewPrimitiveArray(s.f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
    s.f.model.SetPrimitiveElement(colors, 0, SK_ColorRED);
    s.f.model.SetPrimitiveElement(colors, 1, SK_ColorBLUE);
    const auto positions = Floats(s.f, {0, 1});
    const auto array_roots = s.f.vm.ProtectReferences(std::array{colors, positions});
    const auto matrix = s.f.New("Landroid/graphics/Matrix;");
    const auto matrix_root = s.f.vm.ProtectReferences(std::array{matrix});
    s.f.On(matrix, "setTranslate", "(FF)V", {V::Float(4), V::Float(3)});
    for (int kind = 0; kind < 3; ++kind) {
        const auto tile = Enum(s.f, "Landroid/graphics/Shader$TileMode;", "REPEAT");
        VmObjectRef shader;
        if (kind == 0)
            shader = s.f.New(
                "Landroid/graphics/LinearGradient;", "(FFFF[I[FLandroid/graphics/Shader$TileMode;)V",
                {V::Float(9), V::Float(8), V::Float(21), V::Float(19), V::Ref(colors), V::Ref(positions), tile});
        else if (kind == 1)
            shader = s.f.New("Landroid/graphics/RadialGradient;", "(FFF[I[FLandroid/graphics/Shader$TileMode;)V",
                             {V::Float(9), V::Float(8), V::Float(12), V::Ref(colors), V::Ref(positions), tile});
        else
            shader = s.f.New("Landroid/graphics/SweepGradient;", "(FF[I[F)V",
                             {V::Float(9), V::Float(8), V::Ref(colors), V::Ref(positions)});
        s.f.On(shader, "setLocalMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(matrix)});
        s.Set("setShader", kShader, shader);
        s.Clear();
        s.Draw();
        SkPaint paint;
        paint.setShader(HostGradient(kind, SkTileMode::kRepeat)->makeWithLocalMatrix(SkMatrix::Translate(4, 3)));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
        s.f.On(shader, "setLocalMatrix", "(Landroid/graphics/Matrix;)V", {V::Ref(VmObjectRef{})});
        s.Clear();
        s.Draw();
        paint.setShader(HostGradient(kind, SkTileMode::kRepeat));
        s.Compare([&](SkCanvas& canvas) { canvas.drawRect(SkRect::MakeLTRB(3, 3, 35, 31), paint); });
    }
    const auto filter = s.f.New("Landroid/graphics/PorterDuffColorFilter;", "(ILandroid/graphics/PorterDuff$Mode;)V",
                                {Color(SK_ColorGREEN), Enum(s.f, "Landroid/graphics/PorterDuff$Mode;", "SRC_IN")});
    s.Set("setColorFilter", "Landroid/graphics/ColorFilter;", filter);
    const auto shader = Gradient(s, 0, "CLAMP");
    s.Set("setShader", kShader, shader);
    const auto points = Floats(s.f, {2, 2, 32, 2, 2, 32});
    const auto texture = Floats(s.f, {0, 0, 30, 0, 0, 30});
    const auto vertex_roots = s.f.vm.ProtectReferences(std::array{points, texture});
    s.Clear();
    s.f.On(s.canvas, "drawVertices", "(Landroid/graphics/Canvas$VertexMode;I[FI[FI[II[SIILandroid/graphics/Paint;)V",
           {Enum(s.f, "Landroid/graphics/Canvas$VertexMode;", "TRIANGLES"), V::Int(6), V::Ref(points), V::Int(0),
            V::Ref(texture), V::Int(0), V::Ref(VmObjectRef{}), V::Int(0), V::Ref(VmObjectRef{}), V::Int(0), V::Int(0),
            V::Ref(s.paint)});
    const SkPoint p[]{{2, 2}, {32, 2}, {2, 32}}, uv[]{{0, 0}, {30, 0}, {0, 30}};
    SkPaint host_paint;
    host_paint.setShader(HostGradient(0, SkTileMode::kClamp));
    host_paint.setColorFilter(SkColorFilters::Blend(SK_ColorGREEN, SkBlendMode::kSrcIn));
    const auto vertices = SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, 3, p, uv, nullptr);
    s.Compare([&](SkCanvas& canvas) { canvas.drawVertices(vertices, SkBlendMode::kModulate, host_paint); });
}
}  // namespace
