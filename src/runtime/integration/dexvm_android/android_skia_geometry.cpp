#include "android_skia.h"

#include "include/core/SkColor.h"
#include "include/core/SkRRect.h"
#include "include/pathops/SkPathOps.h"

#include <array>
#include <vector>

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;

std::vector<SkPoint> ReadPoints(dx::Interpreter& vm, dx::VmObjectRef array, int offset, int count) {
    CheckRange(vm, array, offset, 2LL * count);
    std::vector<SkPoint> points;
    points.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        points.push_back({ReadFloat(vm, array, offset), ReadFloat(vm, array, offset + 1)});
        offset += 2;
    }
    return points;
}
}  // namespace

Decl Declare_android_graphics_Matrix(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Matrix;");
    b.MergesBootClass();
    auto* store = &context->skia_objects->matrices;
    const auto get = [store](int bits) -> SkMatrix& { return Require(*store, bits); };
    b.StaticMethod(
        "native_create", "(I)I",
        [store, get](dx::IntrinsicContext& c) {
            const auto source = dx::IntrinsicCall(c).Int(0);
            return JavaToken(store->Create(source == 0 ? SkMatrix::I() : get(source)));
        },
        kStaticNative);
    b.StaticMethod(
        "finalizer", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->Retire(NativeToken<SkMatrix>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_isIdentity", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c).Int(0)).isIdentity()); },
        kStaticNative);
    b.StaticMethod(
        "native_rectStaysRect", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c).Int(0)).rectStaysRect()); },
        kStaticNative);
    b.StaticMethod(
        "native_reset", "(I)V",
        [get](dx::IntrinsicContext& c) {
            get(dx::IntrinsicCall(c).Int(0)).reset();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_set", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)) = a.Int(1) ? get(a.Int(1)) : SkMatrix::I();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setTranslate", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setTranslate(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setScale", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setScale(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setScale", "(IFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setScale(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setRotate", "(IF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setRotate(a.Float(1));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setRotate", "(IFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setRotate(a.Float(1), a.Float(2), a.Float(3));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setSkew", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setSkew(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setSkew", "(IFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setSkew(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setSinCos", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setSinCos(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setSinCos", "(IFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setSinCos(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setConcat", "(III)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setConcat(get(a.Int(1)), get(a.Int(2)));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preTranslate", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preTranslate(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preScale", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preScale(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preScale", "(IFFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preScale(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preRotate", "(IF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preRotate(a.Float(1));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preRotate", "(IFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preRotate(a.Float(1), a.Float(2), a.Float(3));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preSkew", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preSkew(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preSkew", "(IFFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preSkew(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_preConcat", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).preConcat(get(a.Int(1)));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postTranslate", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postTranslate(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postScale", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postScale(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postScale", "(IFFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postScale(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postRotate", "(IF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postRotate(a.Float(1));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postRotate", "(IFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postRotate(a.Float(1), a.Float(2), a.Float(3));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postSkew", "(IFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postSkew(a.Float(1), a.Float(2));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postSkew", "(IFFFF)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postSkew(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_postConcat", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).postConcat(get(a.Int(1)));
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_equals", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)) == get(a.Int(1)));
        },
        kStaticNative);
    b.StaticMethod(
        "native_invert", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto inverted = get(a.Int(0)).invert();
            if (inverted && a.Int(1)) get(a.Int(1)) = *inverted;
            return V::Int(inverted.has_value());
        },
        kStaticNative);
    b.StaticMethod(
        "native_mapRadius", "(IF)F",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Float(get(a.Int(0)).mapRadius(a.Float(1)));
        },
        kStaticNative);
    b.StaticMethod(
        "native_mapRect", "(ILandroid/graphics/RectF;Landroid/graphics/RectF;)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            SkRect dst;
            const bool stays_rect = get(a.Int(0)).mapRect(&dst, ReadRectF(c.vm, a.Ref(2)));
            WriteRectF(c.vm, a.Ref(1), dst);
            return V::Int(stays_rect);
        },
        kStaticNative);
    b.StaticMethod(
        "native_setRectToRect", "(ILandroid/graphics/RectF;Landroid/graphics/RectF;I)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).setRectToRect(ReadRectF(c.vm, a.Ref(1)), ReadRectF(c.vm, a.Ref(2)),
                                                      static_cast<SkMatrix::ScaleToFit>(a.Int(3))));
        },
        kStaticNative);
    b.StaticMethod(
        "native_setPolyToPoly", "(I[FI[FII)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto count = a.Int(5);
            if (count < 0 || count > 4) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "point count"};
            const auto source = ReadPoints(c.vm, a.Ref(1), a.Int(2), count);
            const auto dest = ReadPoints(c.vm, a.Ref(3), a.Int(4), count);
            return V::Int(get(a.Int(0)).setPolyToPoly(source, dest));
        },
        kStaticNative);
    b.StaticMethod(
        "native_mapPoints", "(I[FI[FIIZ)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto count = a.Int(5);
            const auto dest = a.Ref(1);
            const auto index = a.Int(2);
            CheckRange(c.vm, dest, index, 2LL * count);
            auto points = ReadPoints(c.vm, a.Ref(3), a.Int(4), count);
            auto& matrix = get(a.Int(0));
            if (a.Int(6))
                matrix.mapPoints(points);
            else
                matrix.mapVectors(points);
            for (int i = 0; i < count; ++i) {
                const auto& point = points[static_cast<std::size_t>(i)];
                WriteFloat(c.vm, dest, index + 2 * i, point.x());
                WriteFloat(c.vm, dest, index + 2 * i + 1, point.y());
            }
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_getValues", "(I[F)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& matrix = get(a.Int(0));
            const auto array = a.Ref(1);
            CheckRange(c.vm, array, 0, 9);
            std::array<float, 9> values{};
            matrix.get9(values.data());
            for (int i = 0; i < 9; ++i)
                WriteFloat(c.vm, array, i, values[static_cast<std::size_t>(i)]);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setValues", "(I[F)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& matrix = get(a.Int(0));
            const auto array = a.Ref(1);
            CheckRange(c.vm, array, 0, 9);
            std::array<float, 9> values{};
            for (int i = 0; i < 9; ++i)
                values[static_cast<std::size_t>(i)] = ReadFloat(c.vm, array, i);
            matrix.set9(values.data());
            return V::Void();
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_graphics_Path(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Path;");
    b.MergesBootClass();
    auto* store = &context->skia_objects->paths;
    const auto get = [store](int bits) -> SkPathBuilder& { return Require(*store, bits); };
    auto* matrices = &context->skia_objects->matrices;
    b.StaticMethod(
        "init1", "()I",
        [store](dx::IntrinsicContext& c) {
            static_cast<void>(c);
            return JavaToken(store->Create());
        },
        kStaticNative);
    b.StaticMethod(
        "init2", "(I)I",
        [store, get](dx::IntrinsicContext& c) {
            const auto source = dx::IntrinsicCall(c).Int(0);
            return source ? JavaToken(store->Create(get(source))) : JavaToken(store->Create());
        },
        kStaticNative);
    b.StaticMethod(
        "finalizer", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->Retire(NativeToken<SkPathBuilder>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_moveTo", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).moveTo(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_lineTo", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).lineTo(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_quadTo", "(IFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).quadTo(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_cubicTo", "(IFFFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).cubicTo(a.Float(1), a.Float(2), a.Float(3), a.Float(4), a.Float(5), a.Float(6));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_rMoveTo", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).rMoveTo(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_rLineTo", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).rLineTo(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_rQuadTo", "(IFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).rQuadTo(a.Float(1), a.Float(2), a.Float(3), a.Float(4));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_rCubicTo", "(IFFFFFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).rCubicTo(a.Float(1), a.Float(2), a.Float(3), a.Float(4), a.Float(5), a.Float(6));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_close", "(I)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).close();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_reset", "(I)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).reset();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_offset", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).offset(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setLastPoint", "(IFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setLastPt(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_rewind", "(I)V",
        [get](dx::IntrinsicContext& c) {
            get(dx::IntrinsicCall(c).Int(0)).reset();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_getFillType", "(I)I",
        [get](dx::IntrinsicContext& c) {
            return V::Int(static_cast<int>(get(dx::IntrinsicCall(c).Int(0)).fillType()));
        },
        kStaticNative);
    b.StaticMethod(
        "native_setFillType", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).setFillType(static_cast<SkPathFillType>(a.Int(1)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_set", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)) = get(a.Int(1));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_isEmpty", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c).Int(0)).isEmpty()); }, kStaticNative);
    b.StaticMethod(
        "native_isRect", "(ILandroid/graphics/RectF;)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            SkRect rect;
            const auto result = get(a.Int(0)).snapshot().isRect(&rect);
            if (result && a.Ref(1).IsValid()) WriteRectF(c.vm, a.Ref(1), rect);
            return V::Int(result);
        },
        kStaticNative);
    b.StaticMethod(
        "native_computeBounds", "(ILandroid/graphics/RectF;)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            WriteRectF(c.vm, a.Ref(1), get(a.Int(0)).snapshot().getBounds());
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_incReserve", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            if (a.Int(1) < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "negative Path reserve"};
            get(a.Int(0)).incReserve(a.Int(1));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_arcTo", "(ILandroid/graphics/RectF;FFZ)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).arcTo(ReadRectF(c.vm, a.Ref(1)), a.Float(2), a.Float(3), a.Int(4) != 0);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addArc", "(ILandroid/graphics/RectF;FF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addArc(ReadRectF(c.vm, a.Ref(1)), a.Float(2), a.Float(3));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addOval", "(ILandroid/graphics/RectF;I)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addOval(ReadRectF(c.vm, a.Ref(1)), static_cast<SkPathDirection>(a.Int(2)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addCircle", "(IFFFI)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addCircle(a.Float(1), a.Float(2), a.Float(3), static_cast<SkPathDirection>(a.Int(4)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addRect", "(ILandroid/graphics/RectF;I)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addRect(ReadRectF(c.vm, a.Ref(1)), static_cast<SkPathDirection>(a.Int(2)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addRect", "(IFFFFI)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addRect(SkRect::MakeLTRB(a.Float(1), a.Float(2), a.Float(3), a.Float(4)),
                                  static_cast<SkPathDirection>(a.Int(5)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addPath", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addPath(get(a.Int(1)).snapshot());
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addPath", "(IIFF)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addPath(get(a.Int(1)).snapshot(), a.Float(2), a.Float(3));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addPath", "(III)V",
        [get, matrices](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).addPath(get(a.Int(1)).snapshot(), Require(*matrices, a.Int(2)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_transform", "(II)V",
        [get, matrices](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)).transform(Require(*matrices, a.Int(1)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addRoundRect", "(ILandroid/graphics/RectF;FFI)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto rect = SkRRect::MakeRectXY(ReadRectF(c.vm, a.Ref(1)), a.Float(2), a.Float(3));
            get(a.Int(0)).addRRect(rect, static_cast<SkPathDirection>(a.Int(4)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_addRoundRect", "(ILandroid/graphics/RectF;[FI)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto radii = ReadPoints(c.vm, a.Ref(2), 0, 4);
            SkRRect rect;
            rect.setRectRadii(ReadRectF(c.vm, a.Ref(1)), radii.data());
            get(a.Int(0)).addRRect(rect, static_cast<SkPathDirection>(a.Int(3)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_offset", "(IFFI)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& source = get(a.Int(0));
            auto& dest = a.Int(3) ? get(a.Int(3)) : source;
            if (&source != &dest) dest = source;
            dest.offset(a.Float(1), a.Float(2));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_transform", "(III)V",
        [get, matrices](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& source = get(a.Int(0));
            const auto& matrix = Require(*matrices, a.Int(1));
            if (a.Int(2))
                get(a.Int(2)) = source.snapshot(&matrix);
            else
                source.transform(matrix);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_op", "(IIII)Z",
        [get](dx::IntrinsicContext& c) {
            constexpr std::array operations{kDifference_SkPathOp, kIntersect_SkPathOp, kUnion_SkPathOp, kXOR_SkPathOp,
                                            kReverseDifference_SkPathOp};
            const dx::IntrinsicCall args(c);
            const auto index = static_cast<std::size_t>(args.Int(2));
            if (index >= operations.size()) return V::Int(false);
            const auto first = get(args.Int(0)).snapshot();
            const auto second = get(args.Int(1)).snapshot();
            auto& destination = get(args.Int(3));
            const auto result = Op(first, second, operations[index]);
            if (!result) return V::Int(false);
            destination = *result;
            return V::Int(true);
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_graphics_PathMeasure(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PathMeasure;");
    b.MergesBootClass();
    auto* store = &context->skia_objects->measures;
    const auto get = [store](int bits) -> SkPathMeasure& { return Require(*store, bits); };
    auto* paths = &context->skia_objects->paths;
    auto* matrices = &context->skia_objects->matrices;
    b.StaticMethod(
        "native_create", "(IZ)I",
        [paths, store](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto path = a.Int(0) ? Require(*paths, a.Int(0)).snapshot() : SkPath{};
            return JavaToken(store->Create(path, a.Int(1) != 0));
        },
        kStaticNative);
    b.StaticMethod(
        "native_destroy", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->Retire(NativeToken<SkPathMeasure>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setPath", "(IIZ)V",
        [paths, get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto path = a.Int(1) ? Require(*paths, a.Int(1)).snapshot() : SkPath{};
            get(a.Int(0)).setPath(a.Int(1) ? &path : nullptr, a.Int(2) != 0);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_getLength", "(I)F",
        [get](dx::IntrinsicContext& c) { return V::Float(get(dx::IntrinsicCall(c).Int(0)).getLength()); },
        kStaticNative);
    b.StaticMethod(
        "native_isClosed", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c).Int(0)).isClosed()); }, kStaticNative);
    b.StaticMethod(
        "native_nextContour", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c).Int(0)).nextContour()); },
        kStaticNative);
    b.StaticMethod(
        "native_getMatrix", "(IFII)Z",
        [get, matrices](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& matrix = Require(*matrices, a.Int(2));
            return V::Int(
                get(a.Int(0)).getMatrix(a.Float(1), &matrix, static_cast<SkPathMeasure::MatrixFlags>(a.Int(3))));
        },
        kStaticNative);
    b.StaticMethod(
        "native_getPosTan", "(IF[F[F)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto position = a.Ref(2);
            const auto tangent = a.Ref(3);
            if (position.IsValid()) CheckRange(c.vm, position, 0, 2);
            if (tangent.IsValid()) CheckRange(c.vm, tangent, 0, 2);
            SkPoint pos, tan;
            if (!get(a.Int(0)).getPosTan(a.Float(1), &pos, &tan)) return V::Int(false);
            for (const auto& [array, point] : {std::pair{position, pos}, std::pair{tangent, tan}}) {
                if (!array.IsValid()) continue;
                WriteFloat(c.vm, array, 0, point.x());
                WriteFloat(c.vm, array, 1, point.y());
            }
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "native_getSegment", "(IFFIZ)Z",
        [get, paths](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& dest = Require(*paths, a.Int(3));
            return V::Int(get(a.Int(0)).getSegment(a.Float(1), a.Float(2), &dest, a.Int(4) != 0));
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_graphics_Color(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Color;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeRGBToHSV", "(III[F)V",
        [](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto array = a.Ref(3);
            CheckRange(c.vm, array, 0, 3);
            float hsv[3];
            SkRGBToHSV(static_cast<unsigned>(a.Int(0)), static_cast<unsigned>(a.Int(1)),
                       static_cast<unsigned>(a.Int(2)), hsv);
            for (int i = 0; i < 3; ++i)
                WriteFloat(c.vm, array, i, hsv[i]);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeHSVToColor", "(I[F)I",
        [](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto array = a.Ref(1);
            CheckRange(c.vm, array, 0, 3);
            const float hsv[]{ReadFloat(c.vm, array, 0), ReadFloat(c.vm, array, 1), ReadFloat(c.vm, array, 2)};
            return V::Int(std::bit_cast<std::int32_t>(SkHSVToColor(static_cast<unsigned>(a.Int(0)), hsv)));
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
