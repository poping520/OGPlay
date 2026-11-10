#include "android_skia_bitmap.h"
#include "android_skia_paint.h"
#include "runtime/raster/nine_patch.h"
#include "runtime/raster/bitmap_decode.h"

#include <cstring>

namespace ogplay::runtime::android_intrinsics {

std::shared_ptr<const ui::UiBitmap> DecodeNinePatchPixels(std::span<const std::byte> bytes) {
    const auto decoded = raster::DecodeBitmap(bytes, {});
    if (!decoded || decoded->nine_patch.empty()) throw std::runtime_error("invalid compiled NinePatch PNG");
    const auto patch = raster::NinePatch::Decode(decoded->nine_patch, decoded->width, decoded->height);
    auto pixels = std::make_shared<ui::UiBitmap>();
    pixels->width = decoded->width;
    pixels->height = decoded->height;
    pixels->nine_patch = true;
    pixels->stretch_x = patch.x;
    pixels->stretch_y = patch.y;
    pixels->nine_patch_padding = {decoded->padding[0], decoded->padding[1], decoded->padding[2], decoded->padding[3]};
    for (int y = 0; y < pixels->height; ++y)
        for (int x = 0; x < pixels->width; ++x) {
            const auto color = decoded->pixels.getColor(x, y);
            pixels->rgba8.push_back(static_cast<std::uint8_t>(color >> 16));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(color >> 8));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(color));
            pixels->rgba8.push_back(static_cast<std::uint8_t>(color >> 24));
        }
    return pixels;
}

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

Decl Declare_android_graphics_NinePatch(const Context& context) {
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

namespace ogplay::runtime::android_intrinsics {
dx::VmObjectRef DecodeNinePatchDrawable(dx::Interpreter& vm, DexVmAndroidContext&, dx::VmObjectRef resources,
                                        std::span<const std::byte> bytes, int density) {
    using namespace skia;
    using V = dx::VmValue;
    const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                    static_cast<int>(bytes.size()));
    const auto array_root = vm.ProtectReferences(std::array{array, resources});
    vm.Model().WriteByteRegion(array, 0, bytes);
    const auto factory = vm.Linker().ResolveDescriptor("Landroid/graphics/BitmapFactory;");
    const auto decode = vm.Linker().FindDirectMethod(factory, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");
    const auto result = vm.Call(*decode, std::array{V::Ref(array), V::Int(0), V::Int(static_cast<int>(bytes.size()))});
    if (result.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                              result.exception};
    const auto bitmap = result.value.ref;
    if (!bitmap.IsValid())
        throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "invalid NinePatch PNG"};
    const auto bitmap_root = vm.ProtectReferences(std::array{bitmap});
    Field(vm, bitmap, "mDensity", "I") = {std::bit_cast<std::uint32_t>(density), dx::SlotTag::cat1};
    const auto chunk = CallVirtual(vm, bitmap, "getNinePatchChunk", "()[B").ref;
    const auto chunk_root = vm.ProtectReferences(std::array{chunk});
    if (!chunk.IsValid())
        throw dx::VmJavaThrow{"Landroid/content/res/Resources$NotFoundException;", "missing NinePatch chunk"};
    auto raw = vm.Model().ReadByteRegion(chunk, 0, vm.Model().ArrayLength(chunk));
    const auto read_word = [&](std::size_t offset) {
        std::int32_t word;
        std::memcpy(&word, raw.data() + offset, 4);
        return V::Int(word);
    };
    const auto rect = vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
    const auto rect_root = vm.ProtectReferences(std::array{rect});
    const auto rect_ctor = vm.Linker().FindDirectMethod(vm.Model().ObjectClass(rect), "<init>", "(IIII)V");
    const auto rect_out =
        vm.Call(*rect_ctor, std::array{V::Ref(rect), read_word(12), read_word(20), read_word(16), read_word(24)});
    if (rect_out.exception.IsValid()) throw dx::VmJavaThrow{"", "", rect_out.exception};
    const auto object = vm.NewIntrinsicInstance("Landroid/graphics/drawable/NinePatchDrawable;");
    const auto object_root = vm.ProtectReferences(std::array{object});
    const auto constructor = vm.Linker().FindDirectMethod(
        vm.Model().ObjectClass(object), "<init>",
        "(Landroid/content/res/Resources;Landroid/graphics/Bitmap;[BLandroid/graphics/Rect;Ljava/lang/String;)V");
    const auto out = vm.Call(*constructor, std::array{V::Ref(object), V::Ref(resources), V::Ref(bitmap), V::Ref(chunk),
                                                      V::Ref(rect), V::Ref(dx::VmObjectRef{})});
    if (out.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(out.exception_class).descriptor, out.exception_message, out.exception};
    return object;
}
}  // namespace ogplay::runtime::android_intrinsics
