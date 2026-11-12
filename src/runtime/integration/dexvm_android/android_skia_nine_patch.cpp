#include "android_skia_bitmap.h"
#include "android_skia_paint.h"
#include "runtime/raster/nine_patch.h"
#include "runtime/raster/bitmap_decode.h"

#include <cstring>

namespace ogplay::runtime::android_intrinsics {

void RetireAndroidNinePatch(dx::Interpreter& vm, DexVmAndroidContext& context, int bits, dx::VmObjectRef owner) {
    // ROM's copy constructor shares the native chunk. Retain it until the last
    // Java owner is swept, including owners that share a pending finalizer.
    bool retained = false;
    vm.Model().VisitLiveObjects([&](dx::VmObjectRef candidate) {
        if (candidate == owner || vm.Model().Kind(candidate) != dx::VmObjectKind::vm_instance) return;
        const auto type = vm.Model().ObjectClass(candidate);
        if (!vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/graphics/NinePatch;"), type)) return;
        retained |= skia::IntField(vm, candidate, "mNativeChunk") == bits;
    });
    if (!retained)
        context.skia_objects->nine_patches.Retire(skia::NativeToken<std::shared_ptr<const raster::NinePatch>>(bits));
}

Decl Declare_android_graphics_NinePatch_Natives(const Context& context) {
    using namespace skia;
    using V = dx::VmValue;
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/NinePatch;");
    b.MergesBootClass();
    b.StaticMethod(
        "isNinePatchChunk", "([B)Z",
        [](dx::IntrinsicContext& c) {
            const auto bytes = c.arguments[0].ref;
            return V::Int(bytes.IsValid() && c.vm.Model().ArrayLength(bytes) >= 32 &&
                          static_cast<std::uint8_t>(c.vm.Model().GetPrimitiveElement(bytes, 0)) != UINT8_MAX);
        },
        dx::kAccPublic | dx::kAccNative | dx::kAccStatic);
    b.StaticMethod(
        "validateNinePatchChunk", "(I[B)I",
        [context](dx::IntrinsicContext& c) {
            const auto array = c.arguments[1].ref;
            if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "chunk"};
            const auto& bitmap = BitmapPeer(*context, c.arguments[0].AsInt()).SkiaBitmap();
            const auto size = c.vm.Model().ArrayLength(array);
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            for (int i = 0; i < size; ++i)
                bytes[static_cast<std::size_t>(i)] = static_cast<std::byte>(c.vm.Model().GetPrimitiveElement(array, i));
            try {
                return JavaToken(context->skia_objects->nine_patches.Create(std::make_shared<const raster::NinePatch>(
                    raster::NinePatch::Decode(bytes, bitmap.width(), bitmap.height()))));
            } catch (const std::invalid_argument& error) {
                throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", error.what()};
            }
        },
        kStaticNative);
    b.StaticMethod(
        "nativeFinalize", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RetireAndroidNinePatch(c.vm, *context, c.arguments[0].AsInt(), dx::VmObjectRef{});
            return V::Void();
        },
        kStaticNative);
    for (const bool floats : {false, true})
        b.StaticMethod(
            "nativeDraw", floats ? "(ILandroid/graphics/RectF;IIIII)V" : "(ILandroid/graphics/Rect;IIIII)V",
            [context, floats](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                auto& target = Require(context->skia_objects->canvases, a.Int(0));
                auto& bitmap = BitmapPeer(*context, a.Int(2));
                CheckCanvasTarget(target);
                CheckCanvasBitmap(bitmap);
                auto bounds = floats ? ReadRectF(c.vm, a.Ref(1)) : SkRect::Make(ReadRect(c.vm, a.Ref(1)));
                auto paint = CanvasPaintPeer(*context, target, a.Int(4));
                if (!a.Int(4)) paint.paint.setDither(true);
                const auto patch = Require(context->skia_objects->nine_patches, a.Int(3));
                target.Draw([&](SkCanvas& canvas) {
                    if (a.Int(5) && a.Int(6) && a.Int(5) != a.Int(6)) {
                        const auto scale = static_cast<float>(a.Int(5)) / static_cast<float>(a.Int(6));
                        canvas.translate(bounds.left(), bounds.top());
                        canvas.scale(scale, scale);
                        bounds = SkRect::MakeWH(bounds.width() / scale, bounds.height() / scale);
                    }
                    patch->Draw(&canvas, bounds, bitmap.SkiaBitmap(), paint.paint, paint.Sampling());
                });
                return V::Void();
            },
            kStaticNative);
    b.StaticMethod(
        "nativeGetTransparentRegion", "(IILandroid/graphics/Rect;)I",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& bitmap = BitmapPeer(*context, a.Int(0));
            CheckCanvasBitmap(bitmap);
            SkRegion region;
            Require(context->skia_objects->nine_patches, a.Int(1))
                ->Draw(nullptr, SkRect::Make(ReadRect(c.vm, a.Ref(2))), bitmap.SkiaBitmap(), SkPaint{}, {}, &region);
            return region.isEmpty() ? V::Int(0) : JavaToken(context->skia_objects->regions.Create(region));
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {}  // namespace ogplay::runtime::android_intrinsics
