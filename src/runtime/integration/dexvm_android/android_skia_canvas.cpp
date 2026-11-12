#include "android_skia_bitmap.h"
#include "android_skia_paint.h"

#include "include/core/SkGraphics.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkImage.h"
#include "include/core/SkVertices.h"
#include "include/core/SkShader.h"
#include "ogplay/runtime/raster/image_decode.h"

#include <algorithm>
#include <array>
#include <limits>

namespace ogplay::runtime::android_intrinsics {
namespace skia {
void CheckCanvasBitmap(const raster::Bitmap& bitmap) {
    if (bitmap.Recycled())
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "Canvas: trying to use a recycled bitmap"};
    if (bitmap.Locks())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas: Bitmap pixels are locked by the NDK"};
}

void CheckCanvasTarget(const raster::Canvas& canvas) {
    if (canvas.Backing()) CheckCanvasBitmap(*canvas.Backing());
}

}  // namespace skia

namespace {
using namespace skia;
using V = dx::VmValue;
using Canvas = raster::Canvas;
constexpr auto kPublicNative = dx::kAccPublic | dx::kAccNative;
constexpr auto kPrivateNative = dx::kAccPrivate | dx::kAccNative;

[[noreturn]] void Invalid(const char* detail) {
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", detail};
}

SkRegion::Op ClipOp(int op) {
    if (op < 0 || op > SkRegion::kReplace_Op) Invalid("invalid Region.Op");
    return static_cast<SkRegion::Op>(op);
}

SkRect RectArgs(const dx::IntrinsicCall& a, std::size_t start) {
    return SkRect::MakeLTRB(a.Float(start), a.Float(start + 1), a.Float(start + 2), a.Float(start + 3));
}

void Draw(Canvas& target, const std::function<void(SkCanvas&)>& draw) {
    CheckCanvasTarget(target);
    target.Draw(draw);
}

std::vector<SkPoint> Points(dx::Interpreter& vm, dx::VmObjectRef array, int offset, int count) {
    CheckRange(vm, array, offset, count);
    std::vector<SkPoint> points(static_cast<std::size_t>(count / 2));
    for (auto& point : points) {
        point = SkPoint::Make(ReadFloat(vm, array, offset), ReadFloat(vm, array, offset + 1));
        offset += 2;
    }
    return points;
}

std::vector<SkColor> Colors(dx::Interpreter& vm, dx::VmObjectRef array, int offset, int count) {
    if (!array.IsValid()) return {};
    CheckRange(vm, array, offset, count);
    std::vector<SkColor> colors(static_cast<std::size_t>(count));
    for (auto& color : colors)
        color = static_cast<SkColor>(vm.Model().GetPrimitiveElement(array, offset++));
    return colors;
}

sk_sp<SkImage> ImageFromBitmap(DexVmAndroidContext& context, int token) {
    auto& bitmap = BitmapPeer(context, token);
    CheckCanvasBitmap(bitmap);
    if (bitmap.Config() == 5 && bitmap.HasAlpha() && !bitmap.Premultiplied())
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "Canvas: trying to use a non-premultiplied bitmap"};
    auto image = SkImages::RasterFromBitmap(bitmap.SkiaBitmap());
    if (!image) throw std::bad_alloc{};
    return image;
}

sk_sp<SkImage> ImageFromColors(dx::Interpreter& vm, const dx::IntrinsicCall& a) {
    const int offset = a.Int(2), stride = a.Int(3), width = a.Int(6), height = a.Int(7);
    if (!ImageDimensionsSupported(width, height)) throw std::bad_alloc{};
    const auto last = offset + static_cast<std::int64_t>(height - 1) * stride;
    const auto low = std::min<std::int64_t>(offset, last), high = std::max<std::int64_t>(offset, last);
    if (low < 0 || high > std::numeric_limits<int>::max())
        throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "Canvas colors offset"};
    CheckRange(vm, a.Ref(1), static_cast<int>(low), high - low + width);
    const auto input = SkImageInfo::Make(width, 1, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
    const auto info = a.Int(8) ? SkImageInfo::MakeN32Premul(width, height)
                               : SkImageInfo::Make(width, height, kRGB_565_SkColorType, kOpaque_SkAlphaType);
    SkBitmap pixels;
    if (!pixels.tryAllocPixels(info)) throw std::bad_alloc{};
    for (int y = 0; y < height; ++y) {
        const auto start = static_cast<int>(offset + static_cast<std::int64_t>(y) * stride);
        auto row = Colors(vm, a.Ref(1), start, width);
        if (!a.Int(8))
            for (auto& color : row)
                color |= 0xff000000U;
        if (!pixels.writePixels({input, row.data(), input.minRowBytes()}, 0, y))
            throw std::logic_error("cannot convert Canvas colors");
    }
    pixels.setImmutable();
    auto image = SkImages::RasterFromBitmap(pixels);
    if (!image) throw std::bad_alloc{};
    return image;
}

sk_sp<SkVertices> BitmapMesh(dx::Interpreter& vm, const dx::IntrinsicCall& a, const SkImage& image) {
    const int width = a.Int(2), height = a.Int(3);
    if (width <= 0 || height <= 0) Invalid("invalid Canvas mesh dimensions");
    const auto count = (static_cast<std::int64_t>(width) + 1) * (static_cast<std::int64_t>(height) + 1);
    if (count > std::numeric_limits<std::uint16_t>::max() + 1LL) Invalid("Canvas mesh exceeds 16-bit indices");
    const auto points = Points(vm, a.Ref(4), a.Int(5), static_cast<int>(count * 2));
    const auto colors = Colors(vm, a.Ref(6), a.Int(7), static_cast<int>(count));
    std::vector<SkPoint> tex;
    std::vector<std::uint16_t> indices;
    tex.reserve(static_cast<std::size_t>(count));
    indices.reserve(static_cast<std::size_t>(width * height * 6));
    for (int y = 0; y <= height; ++y)
        for (int x = 0; x <= width; ++x)
            tex.push_back(
                SkPoint::Make(static_cast<float>(image.width()) * static_cast<float>(x) / static_cast<float>(width),
                              static_cast<float>(image.height()) * static_cast<float>(y) / static_cast<float>(height)));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = y * (width + 1) + x;
            for (const int index : {i, i + width + 1, i + width + 2, i, i + width + 2, i + 1})
                indices.push_back(static_cast<std::uint16_t>(index));
        }
    }
    return SkVertices::MakeCopy(SkVertices::kTriangles_VertexMode, static_cast<int>(count), points.data(), tex.data(),
                                colors.empty() ? nullptr : colors.data(), static_cast<int>(indices.size()),
                                indices.data());
}

void DrawWireframe(SkCanvas& canvas, int mode, std::span<const SkPoint> points, std::span<const std::uint16_t> indices,
                   SkPaint paint) {
    paint.setStrokeWidth(0);
    paint.setStrokeCap(SkPaint::kButt_Cap);
    const auto point = [&](std::size_t index) { return points[indices.empty() ? index : indices[index]]; };
    const auto count = indices.empty() ? points.size() : indices.size();
    const std::size_t step = mode == SkVertices::kTriangles_VertexMode ? 3 : 1;
    for (std::size_t i = 0; i + 2 < count; i += step) {
        const auto a = point(mode == SkVertices::kTriangleFan_VertexMode ? 0 : i);
        const auto b = point(i + 1);
        const auto c = point(i + 2);
        const std::array edges{a, b, b, c, c, a};
        canvas.drawPoints(SkCanvas::kLines_PointMode, edges, paint);
    }
}

}  // namespace

Decl Declare_android_graphics_Canvas_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Canvas;");
    b.MergesBootClass();
    const auto instance = [context](dx::IntrinsicContext& c) -> Canvas& {
        return Require(context->skia_objects->canvases, IntField(c.vm, c.receiver, "mNativeCanvas"));
    };
    const auto with_token = [context](dx::IntrinsicContext& c) -> Canvas& {
        return Require(context->skia_objects->canvases, c.arguments[0].AsInt());
    };
    b.StaticMethod(
        "initRaster", "(I)I",
        [context](dx::IntrinsicContext& c) {
            const auto token = c.arguments[0].AsInt();
            auto& objects = *context->skia_objects;
            if (!token) return JavaToken(objects.canvases.Create());
            auto bitmap = Require(objects.bitmaps, token);
            if (bitmap->Recycled())
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "Canvas: trying to use a recycled bitmap"};
            return JavaToken(objects.canvases.Create(std::move(bitmap)));
        },
        kStaticNative);
    b.StaticMethod(
        "finalizer", "(I)V",
        [context](dx::IntrinsicContext& c) {
            auto& canvases = context->skia_objects->canvases;
            const auto token = NativeToken<Canvas>(c.arguments[0].AsInt());
            try {
                // API19 flushes pending layers when the raster canvas is released.
                if (auto* canvas = canvases.Find(token);
                    canvas && canvas->Backing() && !canvas->Backing()->Recycled()) {
                    if (canvas->Backing()->Locks()) {
                        if (canvas->SaveCount() > 1) RecordUiDegraded(c.vm, "Canvas.releaseWhileBitmapLocked");
                    } else {
                        while (canvas->SaveCount() > 1)
                            canvas->Restore();
                    }
                }
            } catch (...) {
                canvases.Retire(token);
                throw;
            }
            canvases.Retire(token);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "copyNativeCanvasState", "(II)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const auto token = c.arguments[1].AsInt();
            if (token && c.arguments[0].AsInt())
                Require(context->skia_objects->canvases, token).CopyState(with_token(c));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "freeCaches", "()V",
        [](dx::IntrinsicContext&) {
            SkGraphics::PurgeAllCaches();
            return V::Void();
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    b.StaticMethod(
        "freeTextLayoutCaches", "()V",
        [](dx::IntrinsicContext&) {
            // There is no resident text layout cache in this runtime.
            return V::Void();
        },
        dx::kAccPublic | dx::kAccStatic | dx::kAccNative);

    for (const auto& [name, query] : {std::pair{"getWidth", &Canvas::Width}, std::pair{"getHeight", &Canvas::Height},
                                      std::pair{"getSaveCount", &Canvas::SaveCount}}) {
        b.VirtualMethod(
            name, "()I", [instance, query](dx::IntrinsicContext& c) { return V::Int((instance(c).*query)()); },
            kPublicNative);
    }
    b.VirtualMethod(
        "isOpaque", "()Z", [instance](dx::IntrinsicContext& c) { return V::Int(instance(c).IsOpaque()); },
        kPublicNative);
    b.VirtualMethod(
        "save", "()I", [instance](dx::IntrinsicContext& c) { return V::Int(instance(c).Save(0x1f)); }, kPublicNative);
    b.VirtualMethod(
        "save", "(I)I",
        [instance](dx::IntrinsicContext& c) { return V::Int(instance(c).Save(c.arguments[0].AsInt())); },
        kPublicNative);
    for (const bool alpha : {false, true}) {
        for (const bool coords : {false, true}) {
            b.StaticMethod(
                alpha ? "native_saveLayerAlpha" : "native_saveLayer",
                coords ? "(IFFFFII)I" : "(ILandroid/graphics/RectF;II)I",
                [context, with_token, alpha, coords](dx::IntrinsicContext& c) {
                    const dx::IntrinsicCall a(c);
                    auto& target = with_token(c);
                    CheckCanvasTarget(target);
                    std::optional<SkRect> bounds;
                    if (coords)
                        bounds = RectArgs(a, 1);
                    else if (a.Ref(1).IsValid())
                        bounds = ReadRectF(c.vm, a.Ref(1));
                    const std::size_t index = coords ? 5 : 2;
                    SkPaint paint;
                    if (alpha)
                        paint.setAlpha(a.Int(index) & 0xff);
                    else if (a.Int(index))
                        paint = CanvasPaintPeer(*context, target, a.Int(index)).paint;
                    else
                        paint.setDither(true);
                    return V::Int(target.SaveLayer(bounds ? &*bounds : nullptr, paint, a.Int(index + 1)));
                },
                kStaticNative);
        }
    }
    b.VirtualMethod(
        "restore", "()V",
        [instance](dx::IntrinsicContext& c) {
            auto& target = instance(c);
            if (target.SaveCount() <= 1)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Underflow in restore"};
            CheckCanvasTarget(target);
            target.Restore();
            return V::Void();
        },
        kPublicNative);
    b.VirtualMethod(
        "restoreToCount", "(I)V",
        [instance](dx::IntrinsicContext& c) {
            const auto count = c.arguments[0].AsInt();
            if (count < 1) Invalid("Underflow in restoreToCount");
            auto& target = instance(c);
            CheckCanvasTarget(target);
            while (target.SaveCount() > count)
                target.Restore();
            return V::Void();
        },
        kPublicNative);

    for (const auto& [name, transform] :
         {std::pair{"translate", &SkMatrix::preTranslate},
          std::pair{"scale", static_cast<SkMatrix& (SkMatrix::*)(float, float)>(&SkMatrix::preScale)},
          std::pair{"skew", static_cast<SkMatrix& (SkMatrix::*)(float, float)>(&SkMatrix::preSkew)}}) {
        b.VirtualMethod(
            name, "(FF)V",
            [instance, transform](dx::IntrinsicContext& c) {
                (instance(c).Matrix().*transform)(c.arguments[0].AsFloat(), c.arguments[1].AsFloat());
                return V::Void();
            },
            kPublicNative);
    }
    b.VirtualMethod(
        "rotate", "(F)V",
        [instance](dx::IntrinsicContext& c) {
            instance(c).Matrix().preRotate(c.arguments[0].AsFloat());
            return V::Void();
        },
        kPublicNative);
    const auto matrix = [context](int token) -> SkMatrix& { return Require(context->skia_objects->matrices, token); };
    b.StaticMethod(
        "native_concat", "(II)V",
        [with_token, matrix](dx::IntrinsicContext& c) {
            with_token(c).Matrix().preConcat(matrix(c.arguments[1].AsInt()));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setMatrix", "(II)V",
        [with_token, matrix](dx::IntrinsicContext& c) {
            const auto token = c.arguments[1].AsInt();
            with_token(c).Matrix() = token ? matrix(token) : SkMatrix::I();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_getCTM", "(II)V",
        [with_token, matrix](dx::IntrinsicContext& c) {
            matrix(c.arguments[1].AsInt()) = with_token(c).Matrix();
            return V::Void();
        },
        kStaticNative);

    b.VirtualMethod(
        "clipRect", "(FFFF)Z",
        [instance](dx::IntrinsicContext& c) {
            return V::Int(instance(c).ClipRect(RectArgs(dx::IntrinsicCall(c), 0), SkRegion::kIntersect_Op));
        },
        kPublicNative);
    b.VirtualMethod(
        "clipRect", "(IIII)Z",
        [instance](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(instance(c).ClipRect(SkRect::Make(SkIRect::MakeLTRB(a.Int(0), a.Int(1), a.Int(2), a.Int(3))),
                                               SkRegion::kIntersect_Op));
        },
        kPublicNative);
    for (const bool floats : {true, false}) {
        b.VirtualMethod(
            "clipRect", floats ? "(Landroid/graphics/RectF;)Z" : "(Landroid/graphics/Rect;)Z",
            [instance, floats](dx::IntrinsicContext& c) {
                const auto rect = c.arguments[0].ref;
                return V::Int(instance(c).ClipRect(floats ? ReadRectF(c.vm, rect) : SkRect::Make(ReadRect(c.vm, rect)),
                                                   SkRegion::kIntersect_Op));
            },
            kPublicNative);
    }
    const auto path = [context](int token) { return Require(context->skia_objects->paths, token).snapshot(); };
    b.StaticMethod(
        "native_clipRect", "(IFFFFI)Z",
        [with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(with_token(c).ClipRect(RectArgs(a, 1), ClipOp(a.Int(5))));
        },
        kStaticNative);
    b.StaticMethod(
        "native_clipPath", "(III)Z",
        [with_token, path](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(with_token(c).ClipPath(path(a.Int(1)), ClipOp(a.Int(2))));
        },
        kStaticNative);
    b.StaticMethod(
        "native_clipRegion", "(III)Z",
        [context, with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(
                with_token(c).ClipRegion(Require(context->skia_objects->regions, a.Int(1)), ClipOp(a.Int(2))));
        },
        kStaticNative);
    b.StaticMethod(
        "native_getClipBounds", "(ILandroid/graphics/Rect;)Z",
        [with_token](dx::IntrinsicContext& c) {
            SkIRect bounds;
            const auto result = with_token(c).ClipBounds(bounds);
            WriteRect(c.vm, c.arguments[1].ref, bounds);
            return V::Int(result);
        },
        kStaticNative);
    b.StaticMethod(
        "native_quickReject", "(IFFFF)Z",
        [with_token](dx::IntrinsicContext& c) {
            return V::Int(with_token(c).QuickReject(RectArgs(dx::IntrinsicCall(c), 1)));
        },
        kStaticNative);
    b.StaticMethod(
        "native_quickReject", "(ILandroid/graphics/RectF;)Z",
        [with_token](dx::IntrinsicContext& c) {
            return V::Int(with_token(c).QuickReject(ReadRectF(c.vm, c.arguments[1].ref)));
        },
        kStaticNative);
    b.StaticMethod(
        "native_quickReject", "(II)Z",
        [with_token, path](dx::IntrinsicContext& c) {
            const auto shape = path(c.arguments[1].AsInt());
            return V::Int(!shape.isInverseFillType() &&
                          (shape.isEmpty() || with_token(c).QuickReject(shape.getBounds())));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSetDrawFilter", "(II)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const auto token = c.arguments[1].AsInt();
            with_token(c).SetDrawFilter(token ? Require(context->skia_objects->draw_filters, token) : nullptr);
            return V::Void();
        },
        kStaticNative);

    const auto draw_native = [&b, context, with_token](const char* name, const char* signature, auto draw) {
        auto handler = [context, with_token, draw](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& target = with_token(c);
            const auto& paint = CanvasPaintPeer(*context, target, c.arguments.back().AsInt());
            Draw(target, [&](SkCanvas& canvas) { draw(c, a, canvas, paint); });
            return V::Void();
        };
        b.StaticMethod(name, signature, handler, kStaticNative);
    };
    b.StaticMethod(
        "native_drawColor", "(II)V",
        [with_token](dx::IntrinsicContext& c) {
            const auto color = static_cast<SkColor>(c.arguments[1].AsInt());
            Draw(with_token(c), [&](SkCanvas& canvas) { canvas.drawColor(color); });
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_drawColor", "(III)V",
        [with_token](dx::IntrinsicContext& c) {
            const auto color = static_cast<SkColor>(c.arguments[1].AsInt());
            const auto mode = c.arguments[2].AsInt();
            if (mode < 0 || mode > 17) Invalid("invalid PorterDuff.Mode");
            Draw(with_token(c), [&](SkCanvas& canvas) { canvas.drawColor(color, raster::PorterDuffBlend(mode)); });
            return V::Void();
        },
        kStaticNative);
    for (const bool alpha : {false, true}) {
        b.StaticMethod(
            alpha ? "native_drawARGB" : "native_drawRGB", alpha ? "(IIIII)V" : "(IIII)V",
            [with_token, alpha](dx::IntrinsicContext& c) {
                const auto component = [&](std::size_t i) {
                    return static_cast<unsigned>(c.arguments[i].AsInt()) & 0xffU;
                };
                const std::size_t rgb = alpha ? 2 : 1;
                const auto color =
                    SkColorSetARGB(alpha ? component(1) : 255, component(rgb), component(rgb + 1), component(rgb + 2));
                Draw(with_token(c), [&](SkCanvas& canvas) { canvas.drawColor(color); });
                return V::Void();
            },
            kStaticNative);
    }
    draw_native("native_drawPaint", "(II)V",
                [](auto&, const auto&, SkCanvas& canvas, const raster::Paint& p) { canvas.drawPaint(p.paint); });
    draw_native("native_drawLine", "(IFFFFI)V", [](auto&, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
        canvas.drawLine(a.Float(1), a.Float(2), a.Float(3), a.Float(4), p.paint);
    });
    draw_native("native_drawRect", "(IFFFFI)V", [](auto&, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
        canvas.drawRect(RectArgs(a, 1), p.paint);
    });
    draw_native("native_drawRect", "(ILandroid/graphics/RectF;I)V",
                [](auto& c, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
                    canvas.drawRect(ReadRectF(c.vm, a.Ref(1)), p.paint);
                });
    draw_native("native_drawRoundRect", "(ILandroid/graphics/RectF;FFI)V",
                [](auto& c, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
                    canvas.drawRoundRect(ReadRectF(c.vm, a.Ref(1)), a.Float(2), a.Float(3), p.paint);
                });
    draw_native("native_drawOval", "(ILandroid/graphics/RectF;I)V",
                [](auto& c, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
                    canvas.drawOval(ReadRectF(c.vm, a.Ref(1)), p.paint);
                });
    draw_native("native_drawCircle", "(IFFFI)V", [](auto&, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
        canvas.drawCircle(a.Float(1), a.Float(2), a.Float(3), p.paint);
    });
    draw_native("native_drawArc", "(ILandroid/graphics/RectF;FFZI)V",
                [](auto& c, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
                    canvas.drawArc(ReadRectF(c.vm, a.Ref(1)), a.Float(2), a.Float(3), a.Int(4) != 0, p.paint);
                });
    draw_native("native_drawPath", "(III)V", [path](auto&, const auto& a, SkCanvas& canvas, const raster::Paint& p) {
        canvas.drawPath(path(a.Int(1)), p.paint);
    });
    b.VirtualMethod(
        "drawPoint", "(FFLandroid/graphics/Paint;)V",
        [context, instance](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto& paint = CanvasPaintPeer(*context, instance(c), IntField(c.vm, a.Ref(2), "mNativePaint"));
            Draw(instance(c), [&](SkCanvas& canvas) { canvas.drawPoint(a.Float(0), a.Float(1), paint.paint); });
            return V::Void();
        },
        kPublicNative);
    for (const auto& [name, mode] :
         {std::pair{"drawPoints", SkCanvas::kPoints_PointMode}, std::pair{"drawLines", SkCanvas::kLines_PointMode}}) {
        b.VirtualMethod(
            name, "([FIILandroid/graphics/Paint;)V",
            [context, instance, mode](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto& paint = CanvasPaintPeer(*context, instance(c), IntField(c.vm, a.Ref(3), "mNativePaint"));
                const auto points = Points(c.vm, a.Ref(0), a.Int(1), a.Int(2));
                Draw(instance(c), [&](SkCanvas& canvas) { canvas.drawPoints(mode, points, paint.paint); });
                return V::Void();
            },
            kPublicNative);
    }

    b.DirectMethod(
        "native_drawBitmap", "(IIFFIIII)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto image = ImageFromBitmap(*context, a.Int(1));
            const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(4));
            const int density = a.Int(5), screen = a.Int(6), bitmap = a.Int(7);
            const bool scale = density && bitmap && density != bitmap;
            Draw(with_token(c), [&](SkCanvas& canvas) {
                if (scale) {
                    const auto ratio = static_cast<float>(density) / static_cast<float>(bitmap);
                    canvas.translate(a.Float(2), a.Float(3));
                    canvas.scale(ratio, ratio);
                    canvas.drawImage(image, 0, 0, paint.Sampling(true), &paint.paint);
                } else {
                    canvas.drawImage(image, a.Float(2), a.Float(3), paint.Sampling(screen && screen != bitmap),
                                     &paint.paint);
                }
            });
            return V::Void();
        },
        kPrivateNative);
    for (const bool floats : {false, true}) {
        const auto handler = [context, with_token, floats](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto image = ImageFromBitmap(*context, a.Int(1));
            const auto source = a.Ref(2).IsValid() ? ReadRect(c.vm, a.Ref(2)) : image->bounds();
            const auto dest = floats ? ReadRectF(c.vm, a.Ref(3)) : SkRect::Make(ReadRect(c.vm, a.Ref(3)));
            const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(4));
            Draw(with_token(c), [&](SkCanvas& canvas) {
                canvas.drawImageRect(image, SkRect::Make(source), dest,
                                     paint.Sampling(a.Int(5) && a.Int(5) != a.Int(6)), &paint.paint,
                                     SkCanvas::kStrict_SrcRectConstraint);
            });
            return V::Void();
        };
        if (floats)
            b.DirectMethod("native_drawBitmap", "(IILandroid/graphics/Rect;Landroid/graphics/RectF;III)V", handler,
                           kPrivateNative);
        else
            b.StaticMethod("native_drawBitmap", "(IILandroid/graphics/Rect;Landroid/graphics/Rect;III)V", handler,
                           kStaticNative);
    }
    b.StaticMethod(
        "native_drawBitmap", "(I[IIIFFIIZI)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            if (a.Int(6) <= 0 || a.Int(7) <= 0) return V::Void();
            const auto image = ImageFromColors(c.vm, a);
            const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(9));
            Draw(with_token(c), [&](SkCanvas& canvas) {
                canvas.drawImage(image, a.Float(4), a.Float(5), paint.Sampling(), &paint.paint);
            });
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDrawBitmapMatrix", "(IIII)V",
        [context, with_token, matrix](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto image = ImageFromBitmap(*context, a.Int(1));
            const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(3));
            Draw(with_token(c), [&](SkCanvas& canvas) {
                canvas.concat(matrix(a.Int(2)));
                canvas.drawImage(image, 0, 0, paint.Sampling(), &paint.paint);
            });
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDrawBitmapMesh", "(IIII[FI[III)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto image = ImageFromBitmap(*context, a.Int(1));
            auto paint = CanvasPaintPeer(*context, with_token(c), a.Int(8));
            paint.paint.setShader(image->makeShader(SkTileMode::kClamp, SkTileMode::kClamp, paint.Sampling()));
            const auto vertices = BitmapMesh(c.vm, a, *image);
            if (!vertices) throw std::bad_alloc{};
            Draw(with_token(c),
                 [&](SkCanvas& canvas) { canvas.drawVertices(vertices, SkBlendMode::kModulate, paint.paint); });
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDrawVertices", "(III[FI[FI[II[SIII)V",
        [context, with_token](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const int mode = a.Int(1), count = a.Int(2);
            if (mode < 0 || mode > 2) Invalid("invalid Canvas vertex mode");
            const auto points = Points(c.vm, a.Ref(3), a.Int(4), count);
            const auto texture = a.Ref(5).IsValid() ? Points(c.vm, a.Ref(5), a.Int(6), count) : std::vector<SkPoint>{};
            const auto colors = Colors(c.vm, a.Ref(7), a.Int(8), count / 2);
            std::vector<std::uint16_t> indices;
            if (a.Ref(9).IsValid()) {
                CheckRange(c.vm, a.Ref(9), a.Int(10), a.Int(11));
                indices.reserve(static_cast<std::size_t>(a.Int(11)));
                for (int i = 0; i < a.Int(11); ++i) {
                    const auto index =
                        static_cast<std::uint16_t>(c.vm.Model().GetPrimitiveElement(a.Ref(9), a.Int(10) + i));
                    if (index >= points.size()) Invalid("Canvas vertex index out of range");
                    indices.push_back(index);
                }
                if (indices.size() < 3) return V::Void();
            }
            if (points.size() < 3) return V::Void();
            const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(12));
            auto& target = with_token(c);
            if (colors.empty() && !paint.paint.getShader()) {
                Draw(target, [&](SkCanvas& canvas) { DrawWireframe(canvas, mode, points, indices, paint.paint); });
            } else {
                const auto vertices = SkVertices::MakeCopy(
                    static_cast<SkVertices::VertexMode>(mode), static_cast<int>(points.size()), points.data(),
                    texture.empty() ? nullptr : texture.data(), colors.empty() ? nullptr : colors.data(),
                    static_cast<int>(indices.size()), indices.empty() ? nullptr : indices.data());
                if (!vertices) throw std::bad_alloc{};
                Draw(target, [&](SkCanvas& canvas) {
                    // M157 otherwise modulates the vertex colors with the opaque Paint RGB.
                    canvas.drawVertices(vertices, paint.paint.getShader() ? SkBlendMode::kModulate : SkBlendMode::kDst,
                                        paint.paint);
                });
            }
            return V::Void();
        },
        kStaticNative);
    for (const bool array : {false, true}) {
        const auto range = array ? TextRange::count : TextRange::end;
        for (const bool run : {false, true}) {
            b.StaticMethod(
                run ? "native_drawTextRun" : "native_drawText",
                run ? (array ? "(I[CIIIIFFII)V" : "(ILjava/lang/String;IIIIFFII)V")
                    : (array ? "(I[CIIFFII)V" : "(ILjava/lang/String;IIFFII)V"),
                [context, with_token, array, range, run](dx::IntrinsicContext& c) {
                    const dx::IntrinsicCall a(c);
                    const auto text = ReadText(c.vm, a.Ref(1), array);
                    const auto selected = run ? TextRunSlice(text, a.Int(2), a.Int(3), a.Int(4), a.Int(5), range)
                                              : TextSlice(text, a.Int(2), a.Int(3), range);
                    const std::size_t xy = run ? 6 : 4;
                    const int bidi = a.Int(xy + 2);
                    const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(xy + 3));
                    const auto layout = LayoutText(c.vm, *context, paint, selected, bidi == 1 || bidi == 5);
                    Draw(with_token(c), [&](SkCanvas& canvas) {
                        raster::DrawText(canvas, paint, layout, a.Float(xy), a.Float(xy + 1));
                    });
                    return V::Void();
                },
                kStaticNative);
        }
        b.StaticMethod(
            "native_drawPosText", array ? "(I[CII[FI)V" : "(ILjava/lang/String;[FI)V",
            [context, with_token, array](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto selected =
                    array ? TextSlice(text, a.Int(2), a.Int(3), TextRange::count) : std::u16string_view(text);
                const std::size_t pos_index = array ? 4 : 2;
                const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(pos_index + 1));
                const auto layout = LayoutText(c.vm, *context, paint, selected);
                const auto count = static_cast<std::int64_t>(layout.glyphs.size()) * 2;
                CheckRange(c.vm, a.Ref(pos_index), 0, count);
                const auto positions = Points(c.vm, a.Ref(pos_index), 0, static_cast<int>(count));
                Draw(with_token(c),
                     [&](SkCanvas& canvas) { raster::DrawText(canvas, paint, layout, 0, 0, positions); });
                return V::Void();
            },
            kStaticNative);
        b.StaticMethod(
            "native_drawTextOnPath", array ? "(I[CIIIFFII)V" : "(ILjava/lang/String;IFFII)V",
            [context, with_token, array, path](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                const auto text = ReadText(c.vm, a.Ref(1), array);
                const auto selected =
                    array ? TextSlice(text, a.Int(2), a.Int(3), TextRange::count) : std::u16string_view(text);
                const std::size_t path_index = array ? 4 : 2;
                const auto outline = path(a.Int(path_index));
                const auto bidi = a.Int(path_index + 3);
                const auto& paint = CanvasPaintPeer(*context, with_token(c), a.Int(path_index + 4));
                const auto layout = LayoutText(c.vm, *context, paint, selected, bidi == 1 || bidi == 5);
                Draw(with_token(c), [&](SkCanvas& canvas) {
                    raster::DrawTextOnPath(canvas, paint, layout, outline, a.Float(path_index + 1),
                                           a.Float(path_index + 2));
                });
                return V::Void();
            },
            kStaticNative);
    }
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
