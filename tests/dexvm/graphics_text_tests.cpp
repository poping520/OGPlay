#include "android_value_fixture.h"
#include "boot_dex.h"
#include "file_vfs_fixture.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathMeasure.h"
#include "include/ports/SkFontMgr_empty.h"
#include "ogplay/runtime/vfs/vfs.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <doctest/doctest.h>

namespace {
using namespace ogplay::test::android_value;
using V = VmValue;
constexpr auto kPaint = "Landroid/graphics/Paint;";
constexpr auto kTypeface = "Landroid/graphics/Typeface;";

SkFont HostFont(std::string_view filename, float size, bool aa = true) {
    const auto bytes = ogplay::test::ReadTestSystemFont(filename);
    const auto manager = SkFontMgr_New_Custom_Empty();
    auto face = manager->makeFromData(SkData::MakeWithCopy(bytes.data(), bytes.size()));
    REQUIRE(face);
    SkFont font(std::move(face), size);
    font.setEdging(aa ? SkFont::Edging::kAntiAlias : SkFont::Edging::kAlias);
    font.setEmbeddedBitmaps(true);
    return font;
}

float Width(const SkFont& font, std::u16string_view text, SkRect* bounds = nullptr) {
    return font.measureText(text.data(), text.size() * sizeof(char16_t), SkTextEncoding::kUTF16, bounds);
}

V Text(AndroidValueVm& f, std::u16string_view text) {
    return V::Ref(f.model.NewString(std::u16string(text)));
}

int Field(AndroidValueVm& f, VmObjectRef object, const char* name) {
    const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, "I");
    REQUIRE(id);
    return std::bit_cast<int>(f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits);
}

VmObjectRef NewPaint(AndroidValueVm& f, float size) {
    const auto paint = f.New(kPaint, "(I)V", {V::Int(1)});
    f.On(paint, "setTextSize", "(F)V", {V::Float(size)});
    return paint;
}

void SetFace(AndroidValueVm& f, VmObjectRef paint, VmObjectRef face) {
    f.On(paint, "setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;", {V::Ref(face)});
}

float Measure(AndroidValueVm& f, VmObjectRef paint, V text) {
    return f.On(paint, "measureText", "(Ljava/lang/String;)F", {text}).AsFloat();
}

TEST_CASE("ROM Paint compares widths bounds fallback and integer metrics with Skia") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        constexpr float size = 23.5F;
        const auto paint = NewPaint(f, size);
        const auto metrics = f.New("Landroid/graphics/Paint$FontMetricsInt;");
        const auto rect = f.New("Landroid/graphics/Rect;");
        const auto roots = f.vm.ProtectReferences(std::array{paint, metrics, rect});
        const auto roboto = HostFont("Roboto-Regular.ttf", size);
        const auto fallback = HostFont("DroidSansFallback.ttf", size);
        REQUIRE(fallback.unicharToGlyph(U'王'));
        REQUIRE_FALSE(roboto.unicharToGlyph(U'王'));
        CHECK(Measure(f, paint, Text(f, u"Hi王")) == std::ceil(Width(roboto, u"Hi") + Width(fallback, u"王")));
        const auto sample = Text(f, u"Hi");
        SkRect bounds;
        Width(roboto, u"Hi", &bounds);
        f.On(paint, "getTextBounds", "(Ljava/lang/String;IILandroid/graphics/Rect;)V",
             {sample, V::Int(0), V::Int(2), V::Ref(rect)});
        const auto rounded = bounds.roundOut();
        CHECK(Field(f, rect, "left") == rounded.left());
        CHECK(Field(f, rect, "top") == rounded.top());
        CHECK(Field(f, rect, "right") == rounded.right());
        CHECK(Field(f, rect, "bottom") == rounded.bottom());

        SkFontMetrics m;
        const auto spacing = roboto.getMetrics(&m);
        CHECK(f.On(paint, "getFontMetrics", "(Landroid/graphics/Paint$FontMetrics;)F", {V::Ref(VmObjectRef{})})
                  .AsFloat() == doctest::Approx(spacing));
        CHECK(f.On(paint, "ascent", "()F").AsFloat() == doctest::Approx(m.fAscent));
        CHECK(f.On(paint, "descent", "()F").AsFloat() == doctest::Approx(m.fDescent));
        const auto height =
            f.On(paint, "getFontMetricsInt", "(Landroid/graphics/Paint$FontMetricsInt;)I", {V::Ref(metrics)}).AsInt();
        CHECK(Field(f, metrics, "top") == SkScalarFloorToInt(m.fTop));
        CHECK(Field(f, metrics, "ascent") == SkScalarRoundToInt(m.fAscent));
        CHECK(Field(f, metrics, "descent") == SkScalarRoundToInt(m.fDescent));
        CHECK(Field(f, metrics, "bottom") == SkScalarCeilToInt(m.fBottom));
        CHECK(Field(f, metrics, "leading") == SkScalarRoundToInt(m.fLeading));
        CHECK(height ==
              SkScalarRoundToInt(m.fDescent) - SkScalarRoundToInt(m.fAscent) + SkScalarRoundToInt(m.fLeading));

        const auto pair = Text(f, u"A\U0001f600B");
        const auto pair_root = f.vm.ProtectReferences(std::array{pair.ref});
        const auto widths =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 4);
        const auto widths_root = f.vm.ProtectReferences(std::array{widths});
        CHECK(f.On(paint, "getTextWidths", "(Ljava/lang/String;[F)I", {pair, V::Ref(widths)}).AsInt() == 4);
        const auto width_at = [&](int i) {
            return std::bit_cast<float>(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(widths, i)));
        };
        CHECK(width_at(2) == 0.F);
        CHECK(width_at(3) == Width(roboto, u"B"));
        const auto advance =
            f.On(paint, "getTextRunAdvances", "(Ljava/lang/String;IIIII[FI)F",
                 {pair, V::Int(1), V::Int(3), V::Int(0), V::Int(4), V::Int(0), V::Ref(widths), V::Int(2)})
                .AsFloat();
        CHECK(width_at(2) == advance);
        CHECK(width_at(3) == 0.F);
        const auto glyphs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 4);
        CHECK(f.On(paint, "getTextGlyphs", "(Ljava/lang/String;IIIII[C)I",
                   {pair, V::Int(0), V::Int(4), V::Int(0), V::Int(4), V::Int(0), V::Ref(glyphs)})
                  .AsInt() == 3);
        CHECK(f.model.GetPrimitiveElement(glyphs, 0) == roboto.unicharToGlyph(U'A'));
        CHECK(f.model.GetPrimitiveElement(glyphs, 2) == roboto.unicharToGlyph(U'B'));

        CHECK(f.On(paint, "getTextRunCursor", "(Ljava/lang/String;IIIII)I",
                   {pair, V::Int(0), V::Int(4), V::Int(0), V::Int(2), V::Int(4)})
                  .AsInt() == -1);
        CHECK(f.On(paint, "getTextRunCursor", "(Ljava/lang/String;IIIII)I",
                   {pair, V::Int(0), V::Int(4), V::Int(0), V::Int(1), V::Int(0)})
                  .AsInt() == 3);
        CHECK(f.On(paint, "breakText", "(Ljava/lang/String;ZF[F)I",
                   {pair, V::Int(1), V::Float(width_at(0) + advance), V::Ref(widths)})
                  .AsInt() == 3);
    }
}

TEST_CASE("ROM Typeface loads APK and VFS fonts with API19 exceptions") {
    AndroidValueVm f;
    const auto font = ogplay::test::ReadTestSystemFont("DroidSerif-BoldItalic.ttf");
    f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("assets/fixture.ttf", font);
    f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
    auto& vfs = *f.context->vfs;
    vfs.PutFile("/fixture.ttf", font, false);
    vfs.PutFile("/bad.ttf", std::array{std::byte{0}}, false);
    const auto owner = f.New("Landroid/content/Context;");
    const auto manager = f.On(owner, "getAssets", "()Landroid/content/res/AssetManager;").ref;
    const auto manager_root = f.vm.ProtectReferences(std::array{manager});
    constexpr auto kAssetSig = "(Landroid/content/res/AssetManager;Ljava/lang/String;)Landroid/graphics/Typeface;";
    constexpr auto kFileSig = "(Ljava/lang/String;)Landroid/graphics/Typeface;";
    const auto asset =
        f.Static(kTypeface, "createFromAsset", kAssetSig, {V::Ref(manager), Text(f, u"fixture.ttf")}).ref;
    const auto asset_root = f.vm.ProtectReferences(std::array{asset});
    const auto file = f.Static(kTypeface, "createFromFile", kFileSig, {Text(f, u"/fixture.ttf")}).ref;
    const auto file_root = f.vm.ProtectReferences(std::array{file});
    const auto paint = NewPaint(f, 21);
    const auto paint_root = f.vm.ProtectReferences(std::array{paint});
    for (const auto face : {asset, file}) {
        CHECK(f.On(face, "getStyle", "()I").AsInt() == 3);
        SetFace(f, paint, face);
        CHECK(Measure(f, paint, Text(f, u"Hello")) ==
              std::ceil(Width(HostFont("DroidSerif-BoldItalic.ttf", 21), u"Hello")));
    }

    const auto bad_asset =
        f.StaticOutcome(kTypeface, "createFromAsset", kAssetSig, {V::Ref(manager), Text(f, u"missing.ttf")});
    const auto bad_file = f.StaticOutcome(kTypeface, "createFromFile", kFileSig, {Text(f, u"/bad.ttf")});
    const auto missing = f.StaticOutcome(kTypeface, "createFromFile", kFileSig, {Text(f, u"/missing.ttf")});
    for (const auto* failure : {&bad_asset, &bad_file, &missing}) {
        REQUIRE(failure->exception.IsValid());
        CHECK(f.linker.Class(failure->exception_class).descriptor == "Ljava/lang/RuntimeException;");
        CHECK(failure->exception_message == "native typeface cannot be made");
    }
}

TEST_CASE("ROM Paint synthesizes missing styles and copies state independently") {
    AndroidValueVm f;
    const auto paint = NewPaint(f, 26);
    const auto paint_root = f.vm.ProtectReferences(std::array{paint});
    const auto face = f.Static(kTypeface, "create", "(Ljava/lang/String;I)Landroid/graphics/Typeface;",
                               {Text(f, u"courier"), V::Int(3)})
                          .ref;
    SetFace(f, paint, face);
    f.On(paint, "setTextSkewX", "(F)V", {V::Float(-0.125F)});
    f.On(paint, "setTextScaleX", "(F)V", {V::Float(1.3F)});
    f.On(paint, "setUnderlineText", "(Z)V", {V::Int(1)});
    f.On(paint, "setColor", "(I)V", {V::Int(0x11223344)});
    const auto copy = f.New(kPaint, "(Landroid/graphics/Paint;)V", {V::Ref(paint)});
    const auto copy_root = f.vm.ProtectReferences(std::array{copy});
    const auto assigned = f.New(kPaint);
    const auto assigned_root = f.vm.ProtectReferences(std::array{assigned});
    f.On(assigned, "set", "(Landroid/graphics/Paint;)V", {V::Ref(copy)});
    f.On(paint, "reset", "()V");

    auto font = HostFont("DroidSansMono.ttf", 26);
    font.setEmbolden(true);
    font.setSkewX(-0.375F);
    font.setScaleX(1.3F);
    for (const auto clone : {copy, assigned}) {
        CHECK(f.On(clone, "getTypeface", "()Landroid/graphics/Typeface;").ref == face);
        CHECK(f.On(clone, "getFlags", "()I").AsInt() == 0x509);
        CHECK(f.On(clone, "getColor", "()I").AsInt() == 0x11223344);
        const auto bounds = f.New("Landroid/graphics/Rect;");
        f.On(clone, "getTextBounds", "(Ljava/lang/String;IILandroid/graphics/Rect;)V",
             {Text(f, u"Hi"), V::Int(0), V::Int(2), V::Ref(bounds)});
        SkRect expected;
        const auto width = Width(font, u"Hi", &expected);
        CHECK(Measure(f, clone, Text(f, u"Hi")) == std::ceil(width));
        CHECK(Field(f, bounds, "right") == expected.roundOut().right());
    }
    CHECK(f.On(paint, "getTextSize", "()F").AsFloat() == 12);
    CHECK_FALSE(f.On(paint, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
}

TEST_CASE("DEX Paint metrics to Bitmap Canvas text matches host Skia pixels") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "text_bitmap.dex");
        constexpr std::u16string_view text = u"Text bitmap";
        const auto bitmap =
            f.Static("Ltest/TextBitmap;", "raster", "(Ljava/lang/String;)Landroid/graphics/Bitmap;", {Text(f, text)})
                .ref;
        const auto root = f.vm.ProtectReferences(std::array{bitmap});
        const auto font = HostFont("Roboto-Regular.ttf", 24);
        SkFontMetrics m;
        font.getMetrics(&m);
        const int width = static_cast<int>(std::ceil(Width(font, text)));
        const int height =
            SkScalarRoundToInt(m.fDescent) - SkScalarRoundToInt(m.fAscent) + SkScalarRoundToInt(m.fLeading);
        REQUIRE(f.On(bitmap, "getWidth", "()I").AsInt() == width);
        REQUIRE(f.On(bitmap, "getHeight", "()I").AsInt() == height);
        SkBitmap expected;
        expected.allocN32Pixels(width, height);
        expected.eraseColor(SK_ColorTRANSPARENT);
        SkCanvas canvas(expected);
        SkPaint paint;
        paint.setColor(SK_ColorWHITE);
        paint.setAntiAlias(true);
        canvas.drawSimpleText(text.data(), text.size() * sizeof(char16_t), SkTextEncoding::kUTF16, 0,
                              -static_cast<float>(SkScalarRoundToInt(m.fAscent)), font, paint);
        int covered = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto pixel = f.On(bitmap, "getPixel", "(II)I", {V::Int(x), V::Int(y)}).AsInt();
                CHECK(static_cast<SkColor>(pixel) == expected.getColor(x, y));
                covered += pixel != 0;
            }
        CHECK(covered > 0);
    }
}

TEST_CASE("ROM Canvas text overloads share glyph positions and preserve paint alignment") {
    AndroidValueVm f;
    const auto config = f.Static("Landroid/graphics/Bitmap$Config;", "valueOf",
                                 "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;", {Text(f, u"ARGB_8888")});
    const auto bitmap =
        f.Static("Landroid/graphics/Bitmap;", "createBitmap",
                 "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", {V::Int(40), V::Int(30), config})
            .ref;
    const auto bitmap_root = f.vm.ProtectReferences(std::array{bitmap});
    const auto canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(bitmap)});
    const auto canvas_root = f.vm.ProtectReferences(std::array{canvas});
    const auto paint = NewPaint(f, 20);
    const auto paint_root = f.vm.ProtectReferences(std::array{paint});
    const auto path = f.New("Landroid/graphics/Path;");
    const auto path_root = f.vm.ProtectReferences(std::array{path});
    f.On(path, "moveTo", "(FF)V", {V::Float(0), V::Float(22)});
    f.On(path, "lineTo", "(FF)V", {V::Float(38), V::Float(22)});
    const auto text = Text(f, u"A");
    const auto text_root = f.vm.ProtectReferences(std::array{text.ref});
    const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 1);
    const auto chars_root = f.vm.ProtectReferences(std::array{chars});
    f.model.SetPrimitiveElement(chars, 0, u'A');
    const auto pos = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 2);
    const auto pos_root = f.vm.ProtectReferences(std::array{pos});
    f.model.SetPrimitiveElement(pos, 0, V::Float(6).cat1);
    f.model.SetPrimitiveElement(pos, 1, V::Float(22).cat1);
    const auto pixels = [&] {
        std::vector<int> result;
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 40; ++x)
                result.push_back(f.On(bitmap, "getPixel", "(II)I", {V::Int(x), V::Int(y)}).AsInt());
        return result;
    };
    const auto draw = [&](const char* name, const char* signature, std::vector<V> args) {
        f.On(bitmap, "eraseColor", "(I)V", {V::Int(0)});
        args.push_back(V::Ref(paint));
        f.On(canvas, name, signature, std::move(args));
        return pixels();
    };
    const auto expected =
        draw("drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V", {text, V::Float(6), V::Float(22)});
    REQUIRE(std::ranges::any_of(expected, [](int pixel) { return pixel != 0; }));
    CHECK(draw("drawText", "([CIIFFLandroid/graphics/Paint;)V",
               {V::Ref(chars), V::Int(0), V::Int(1), V::Float(6), V::Float(22)}) == expected);
    CHECK(draw("drawTextRun", "(Ljava/lang/CharSequence;IIIIFFILandroid/graphics/Paint;)V",
               {text, V::Int(0), V::Int(1), V::Int(0), V::Int(1), V::Float(6), V::Float(22), V::Int(0)}) == expected);
    CHECK(draw("drawTextRun", "([CIIIIFFILandroid/graphics/Paint;)V",
               {V::Ref(chars), V::Int(0), V::Int(1), V::Int(0), V::Int(1), V::Float(6), V::Float(22), V::Int(0)}) ==
          expected);
    CHECK(draw("drawPosText", "(Ljava/lang/String;[FLandroid/graphics/Paint;)V", {text, V::Ref(pos)}) == expected);
    CHECK(draw("drawPosText", "([CII[FLandroid/graphics/Paint;)V",
               {V::Ref(chars), V::Int(0), V::Int(1), V::Ref(pos)}) == expected);
    f.On(path, "reset", "()V");
    f.On(path, "moveTo", "(FF)V", {V::Float(0), V::Float(22)});
    f.On(path, "quadTo", "(FFFF)V", {V::Float(19), V::Float(0), V::Float(38), V::Float(22)});
    SkBitmap outline_pixels;
    outline_pixels.allocN32Pixels(40, 30);
    outline_pixels.eraseColor(0);
    SkCanvas outline_canvas(outline_pixels);
    auto font = HostFont("Roboto-Regular.ttf", 20);
    SkPaint outline_paint;
    outline_paint.setAntiAlias(true);
    const auto glyph = font.unicharToGlyph('A');
    const auto outline = font.getPath(glyph);
    REQUIRE(outline);
    SkPathMeasure path_measure(SkPathBuilder{}.moveTo(0, 22).quadTo(19, 0, 38, 22).detach(), false);
    const auto morph = [&](SkPoint point) {
        SkPoint position;
        SkVector tangent;
        REQUIRE(path_measure.getPosTan(point.x() + 6, &position, &tangent));
        return position + SkVector{-tangent.y(), tangent.x()} * point.y();
    };
    SkPathBuilder expected_path;
    SkPath::RawIter iter(*outline);
    SkPoint points[4];
    for (auto verb = iter.next(points); verb != SkPath::kDone_Verb; verb = iter.next(points)) {
        switch (verb) {
        case SkPath::kMove_Verb: expected_path.moveTo(morph(points[0])); break;
        case SkPath::kLine_Verb: expected_path.quadTo(morph((points[0] + points[1]) * 0.5F), morph(points[1])); break;
        case SkPath::kQuad_Verb: expected_path.quadTo(morph(points[1]), morph(points[2])); break;
        case SkPath::kConic_Verb: expected_path.conicTo(morph(points[1]), morph(points[2]), iter.conicWeight()); break;
        case SkPath::kCubic_Verb: expected_path.cubicTo(morph(points[1]), morph(points[2]), morph(points[3])); break;
        case SkPath::kClose_Verb: expected_path.close(); break;
        case SkPath::kDone_Verb: break;
        }
    }
    outline_canvas.drawPath(expected_path.detach(), outline_paint);
    std::vector<int> path_expected;
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x)
            path_expected.push_back(std::bit_cast<int>(outline_pixels.getColor(x, y)));
    CHECK(draw("drawTextOnPath", "(Ljava/lang/String;Landroid/graphics/Path;FFLandroid/graphics/Paint;)V",
               {text, V::Ref(path), V::Float(6), V::Float(0)}) == path_expected);
    CHECK(draw("drawTextOnPath", "([CIILandroid/graphics/Path;FFLandroid/graphics/Paint;)V",
               {V::Ref(chars), V::Int(0), V::Int(1), V::Ref(path), V::Float(6), V::Float(0)}) == path_expected);

    const auto center = f.Static("Landroid/graphics/Paint$Align;", "valueOf",
                                 "(Ljava/lang/String;)Landroid/graphics/Paint$Align;", {Text(f, u"CENTER")});
    f.On(paint, "setTextAlign", "(Landroid/graphics/Paint$Align;)V", {center});
    const auto advance = Width(HostFont("Roboto-Regular.ttf", 20), u"A");
    CHECK(draw("drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V",
               {text, V::Float(6 + advance / 2), V::Float(22)}) == expected);
    for (const auto name : {"CENTER", "RIGHT"}) {
        const auto alignment =
            f.Static("Landroid/graphics/Paint$Align;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Paint$Align;",
                     {V::Ref(f.vm.NewStringUtf8(name))});
        f.On(paint, "setTextAlign", "(Landroid/graphics/Paint$Align;)V", {alignment});
        const auto offset = std::string_view(name) == "CENTER" ? advance / 2 : advance;
        f.model.SetPrimitiveElement(pos, 0, V::Float(6 + offset).cat1);
        CHECK(draw("drawPosText", "(Ljava/lang/String;[FLandroid/graphics/Paint;)V", {text, V::Ref(pos)}) == expected);
    }
    // Three glyphs consume three positions even though the Java validation
    // still requires four UTF-16 position pairs for the supplementary scalar.
    const auto supplementary = Text(f, u"A\U0001f600B");
    const auto supplementary_root = f.vm.ProtectReferences(std::array{supplementary.ref});
    const auto positions =
        f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 8);
    const auto positions_root = f.vm.ProtectReferences(std::array{positions});
    for (int i = 0; i < 4; ++i) {
        f.model.SetPrimitiveElement(positions, i * 2, V::Float(10.F * static_cast<float>(i + 1)).cat1);
        f.model.SetPrimitiveElement(positions, i * 2 + 1, V::Float(i == 3 ? -100 : 22).cat1);
    }
    const auto combined =
        draw("drawPosText", "(Ljava/lang/String;[FLandroid/graphics/Paint;)V", {supplementary, V::Ref(positions)});
    // Compare against three separate drawText calls at the supplied glyph origins.
    f.On(bitmap, "eraseColor", "(I)V", {V::Int(0)});
    const std::u16string_view scalars[]{u"A", u"\U0001f600", u"B"};
    for (int i = 0; i < 3; ++i)
        f.On(canvas, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V",
             {Text(f, scalars[i]), V::Float(10.F * static_cast<float>(i + 1)), V::Float(22), V::Ref(paint)});
    CHECK(combined == pixels());
}

TEST_CASE("ROM AndroidBidi leaves CJK LTR and records strong RTL degradation") {
    AndroidValueVm f;
    const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 2);
    const auto chars_root = f.vm.ProtectReferences(std::array{chars});
    const auto info = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 2);
    const auto info_root = f.vm.ProtectReferences(std::array{info});
    const auto bidi = [&] {
        f.model.SetPrimitiveElement(info, 0, 1);
        f.model.SetPrimitiveElement(info, 1, 1);
        const auto result = f.Static("Landroid/text/AndroidBidi;", "runBidi", "(I[C[BIZ)I",
                                     {V::Int(0), V::Ref(chars), V::Ref(info), V::Int(2), V::Int(0)});
        CHECK(result.AsInt() == 0);
        CHECK(f.model.GetPrimitiveElement(info, 0) == 0);
        CHECK(f.model.GetPrimitiveElement(info, 1) == 0);
    };
    f.model.SetPrimitiveElement(chars, 0, u'A');
    f.model.SetPrimitiveElement(chars, 1, u'王');
    bidi();
    CHECK(f.ledger.Warnings().empty());
    f.model.SetPrimitiveElement(chars, 1, u'א');
    bidi();
    const auto warnings = f.ledger.Warnings();
    CHECK(std::ranges::any_of(warnings, [](const auto& hit) { return hit.id == "ui.degraded.AndroidBidi.runBidi"; }));
}
}  // namespace
