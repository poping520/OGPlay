#include "android_value_fixture.h"
#include "file_vfs_fixture.h"
#include "ogplay/loader/apk.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"
#include "ogplay/runtime/raster/bitmap.h"

#include <array>
#include <bit>
#include <cstring>
#include <doctest/doctest.h>

namespace {
using namespace ogplay::test::android_value;
using V = VmValue;

std::vector<std::byte> Chunk(bool network = false) {
    std::vector<std::byte> bytes(32 + 4 * 13);
    bytes[0] = std::byte{1};
    bytes[1] = bytes[2] = std::byte{2};
    bytes[3] = std::byte{9};
    const auto put = [&](std::size_t offset, std::uint32_t word) {
        if (network) {
            for (int i = 0; i < 4; ++i)
                bytes[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>(word >> (24 - i * 8));
        } else {
            std::memcpy(bytes.data() + offset, &word, 4);
        }
    };
    put(12, 1);
    put(16, 2);
    put(20, 3);
    put(24, 4);
    put(32, 2);
    put(36, 4);
    put(40, 2);
    put(44, 4);
    for (std::size_t i = 0; i < 9; ++i)
        put(48 + 4 * i, 1);
    return bytes;
}

VmObjectRef Bytes(AndroidValueVm& f, std::span<const std::byte> bytes) {
    const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                 static_cast<int>(bytes.size()));
    f.model.WriteByteRegion(array, 0, bytes);
    return array;
}

VmObjectRef Bitmap(AndroidValueVm& f, int width, int height) {
    const auto config =
        f.Static("Landroid/graphics/Bitmap$Config;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                 {V::Ref(f.vm.NewStringUtf8("ARGB_8888"))});
    return f
        .Static("Landroid/graphics/Bitmap;", "createBitmap",
                "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                {V::Int(width), V::Int(height), config})
        .ref;
}

void Compare(AndroidValueVm& f, VmObjectRef bitmap, const SkBitmap& expected) {
    const auto actual = f.context->bitmaps.at(bitmap.Value()).pixels;
    int mismatches = 0;
    SkColor first_actual = 0, first_expected = 0;
    for (int y = 0; y < expected.height(); ++y)
        for (int x = 0; x < expected.width(); ++x)
            if (actual->Get(static_cast<std::size_t>(y * expected.width() + x)) != expected.getColor(x, y)) {
                if (mismatches == 0) {
                    first_actual = actual->Get(static_cast<std::size_t>(y * expected.width() + x));
                    first_expected = expected.getColor(x, y);
                }
                ++mismatches;
            }
    CAPTURE(first_actual);
    CAPTURE(first_expected);
    CHECK(mismatches == 0);
}

TEST_CASE("ROM NinePatch stretches fixed cells density and Paint against Skia") {
    AndroidValueVm f;
    const auto source = Bitmap(f, 6, 6);
    const auto source_root = f.vm.ProtectReferences(std::array{source});
    SkBitmap host;
    host.allocN32Pixels(6, 6);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x) {
            const auto color = SkColorSetRGB(x * 40, y * 40, (x + y) * 20);
            f.On(source, "setPixel", "(III)V", {V::Int(x), V::Int(y), V::Int(std::bit_cast<int>(color))});
            *host.getAddr32(x, y) = SkPreMultiplyColor(color);
        }
    const auto chunk = Bytes(f, Chunk());
    const auto roots = f.vm.ProtectReferences(std::array{chunk});
    const auto patch =
        f.New("Landroid/graphics/NinePatch;", "(Landroid/graphics/Bitmap;[B)V", {V::Ref(source), V::Ref(chunk)});
    const auto patch_root = f.vm.ProtectReferences(std::array{patch});
    for (const auto size : {std::array{18, 15}, std::array{6, 6}, std::array{3, 3}}) {
        for (const int density : {160, 320}) {
            CAPTURE(size[0]);
            CAPTURE(density);
            const auto target = Bitmap(f, 24, 24);
            const auto target_root = f.vm.ProtectReferences(std::array{target});
            const auto canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(target)});
            const auto paint = f.New("Landroid/graphics/Paint;");
            const auto draw_roots = f.vm.ProtectReferences(std::array{canvas, paint});
            f.On(source, "setDensity", "(I)V", {V::Int(160)});
            f.On(canvas, "setDensity", "(I)V", {V::Int(density)});
            f.On(paint, "setAlpha", "(I)V", {V::Int(128)});
            f.On(paint, "setFilterBitmap", "(Z)V", {V::Int(1)});
            const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                    {V::Int(1), V::Int(2), V::Int(1 + size[0]), V::Int(2 + size[1])});
            f.On(patch, "draw", "(Landroid/graphics/Canvas;Landroid/graphics/Rect;Landroid/graphics/Paint;)V",
                 {V::Ref(canvas), V::Ref(rect), V::Ref(paint)});
            SkBitmap expected;
            expected.allocN32Pixels(24, 24);
            expected.eraseColor(0);
            SkCanvas reference(expected);
            reference.translate(1, 2);
            const auto scale = static_cast<float>(density) / 160;
            reference.scale(scale, scale);
            SkPaint host_paint;
            host_paint.setAlpha(128);
            const float w = static_cast<float>(size[0]) / scale, h = static_cast<float>(size[1]) / scale;
            const int src[]{0, 2, 4, 6};
            const float dx[]{0, 2, w - 2, w}, dy[]{0, 2, h - 2, h};
            const auto image = SkImages::RasterFromBitmap(host);
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x) {
                    const auto dst = SkRect::MakeLTRB(dx[x], dy[y], dx[x + 1], dy[y + 1]);
                    if (dst.isEmpty()) continue;
                    reference.drawImageRect(image, SkRect::MakeLTRB(src[x], src[y], src[x + 1], src[y + 1]), dst,
                                            SkSamplingOptions(SkFilterMode::kLinear), &host_paint,
                                            SkCanvas::kStrict_SrcRectConstraint);
                }
            Compare(f, target, expected);
        }
    }
}

TEST_CASE("ROM BitmapFactory preserves aapt chunk for NinePatchDrawable drawing") {
    AndroidValueVm f;
    SkBitmap host;
    host.allocN32Pixels(6, 6);
    host.eraseColor(SK_ColorGREEN);
    SkDynamicMemoryWStream encoded;
    REQUIRE(SkPngEncoder::Encode(&encoded, host.pixmap(), {}));
    const auto png = encoded.detachAsData();
    const auto bytes = static_cast<const std::byte*>(png->data());
    std::vector<std::byte> image(bytes, bytes + png->size());
    auto chunk = Chunk(true);
    std::vector<std::byte> block{std::byte{0},   std::byte{0},   std::byte{0},   static_cast<std::byte>(chunk.size()),
                                 std::byte{'n'}, std::byte{'p'}, std::byte{'T'}, std::byte{'c'}};
    block.insert(block.end(), chunk.begin(), chunk.end());
    std::uint32_t crc = ~0U;
    for (std::size_t i = 4; i < block.size(); ++i) {
        crc ^= std::to_integer<std::uint32_t>(block[i]);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320U : 0U);
    }
    crc = ~crc;
    for (int shift = 24; shift >= 0; shift -= 8)
        block.push_back(static_cast<std::byte>(crc >> shift));
    image.insert(image.begin() + 33, block.begin(), block.end());
    f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("res/drawable/patch.9.png", image);
    f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
    f.context->arsc.entries = {{.resource_id = 0x7f020001,
                                .type_name = "drawable",
                                .entry_name = "patch",
                                .string_value = "res/drawable/patch.9.png",
                                .value_type = 3,
                                .density = 160}};
    const std::string path = "res/drawable/patch.9.png";
    std::vector<std::byte> pool(32);
    pool[0] = std::byte{1};
    pool[2] = std::byte{28};
    pool[8] = std::byte{1};
    pool[17] = std::byte{1};  // UTF-8 string pool.
    pool[20] = std::byte{32};
    pool.push_back(static_cast<std::byte>(path.size()));
    pool.push_back(static_cast<std::byte>(path.size()));
    for (const auto ch : path)
        pool.push_back(static_cast<std::byte>(ch));
    pool.push_back(std::byte{0});
    while (pool.size() % 4)
        pool.push_back(std::byte{0});
    for (std::size_t i = 0; i < 4; ++i)
        pool[4 + i] = static_cast<std::byte>(pool.size() >> (8 * i));
    f.context->arsc.global_string_pool = std::move(pool);
    const auto owner = f.New("Landroid/app/Activity;");
    const auto owner_root = f.vm.ProtectReferences(std::array{owner});
    ogplay::test::AttachViewTestContext(f.vm, owner);
    const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
    const auto resources_root = f.vm.ProtectReferences(std::array{resources});
    const auto resource_drawable =
        f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {V::Int(0x7f020001)}).ref;
    const auto resource_drawable_root = f.vm.ProtectReferences(std::array{resource_drawable});
    CHECK(f.linker.Class(f.model.ObjectClass(resource_drawable)).descriptor ==
          "Landroid/graphics/drawable/NinePatchDrawable;");
    const auto array = Bytes(f, image);
    const auto array_root = f.vm.ProtectReferences(std::array{array});
    const auto bitmap =
        f.Static("Landroid/graphics/BitmapFactory;", "decodeByteArray", "([BII)Landroid/graphics/Bitmap;",
                 {V::Ref(array), V::Int(0), V::Int(static_cast<int>(image.size()))})
            .ref;
    REQUIRE(bitmap.IsValid());
    const auto bitmap_root = f.vm.ProtectReferences(std::array{bitmap});
    const auto patch = f.On(bitmap, "getNinePatchChunk", "()[B").ref;
    REQUIRE(patch.IsValid());
    const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V", {V::Int(1), V::Int(3), V::Int(2), V::Int(4)});
    const auto rect_root = f.vm.ProtectReferences(std::array{rect});
    const auto drawable = f.New("Landroid/graphics/drawable/NinePatchDrawable;",
                                "(Landroid/graphics/Bitmap;[BLandroid/graphics/Rect;Ljava/lang/String;)V",
                                {V::Ref(bitmap), V::Ref(patch), V::Ref(rect), V::Ref(VmObjectRef{})});
    const auto drawable_root = f.vm.ProtectReferences(std::array{drawable});
    const auto target = Bitmap(f, 20, 20);
    const auto target_root = f.vm.ProtectReferences(std::array{target});
    const auto canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(target)});
    const auto canvas_root = f.vm.ProtectReferences(std::array{canvas});
    f.On(drawable, "setBounds", "(IIII)V", {V::Int(0), V::Int(0), V::Int(20), V::Int(20)});
    f.On(drawable, "draw", "(Landroid/graphics/Canvas;)V", {V::Ref(canvas)});
    SkBitmap expected;
    expected.allocN32Pixels(20, 20);
    expected.eraseColor(SK_ColorGREEN);
    Compare(f, target, expected);
    f.On(target, "eraseColor", "(I)V", {V::Int(0)});
    f.On(resource_drawable, "setBounds", "(IIII)V", {V::Int(0), V::Int(0), V::Int(20), V::Int(20)});
    f.On(resource_drawable, "draw", "(Landroid/graphics/Canvas;)V", {V::Ref(canvas)});
    Compare(f, target, expected);
    const auto padding = f.New("Landroid/graphics/Rect;");
    CHECK(f.On(drawable, "getPadding", "(Landroid/graphics/Rect;)Z", {V::Ref(padding)}).AsInt() == 1);
}
}  // namespace

namespace {
TEST_CASE("ROM NinePatch transparent regions copy lifetime and RectF CLEAR") {
    AndroidValueVm f;
    const auto source = Bitmap(f, 6, 6);
    const auto source_root = f.vm.ProtectReferences(std::array{source});
    f.On(source, "eraseColor", "(I)V", {V::Int(std::bit_cast<int>(SK_ColorGREEN))});
    auto raw = Chunk();
    const std::uint32_t transparent = 0;
    std::memcpy(raw.data() + 48 + 4 * 4, &transparent, 4);
    const auto chunk = Bytes(f, raw);
    const auto chunk_root = f.vm.ProtectReferences(std::array{chunk});
    const auto patch =
        f.New("Landroid/graphics/NinePatch;", "(Landroid/graphics/Bitmap;[B)V", {V::Ref(source), V::Ref(chunk)});
    const auto copy = f.New("Landroid/graphics/NinePatch;", "(Landroid/graphics/NinePatch;)V", {V::Ref(patch)});
    const auto copy_root = f.vm.ProtectReferences(std::array{copy});
    static_cast<void>(f.vm.CollectGarbage("ninepatch-copy-retains-shared-chunk"));
    f.vm.RunFinalization();
    static_cast<void>(f.vm.CollectGarbage("ninepatch-original-sweep"));
    const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V", {V::Int(0), V::Int(0), V::Int(20), V::Int(20)});
    const auto rect_root = f.vm.ProtectReferences(std::array{rect});
    const auto region =
        f.On(copy, "getTransparentRegion", "(Landroid/graphics/Rect;)Landroid/graphics/Region;", {V::Ref(rect)}).ref;
    REQUIRE(region.IsValid());
    const auto region_root = f.vm.ProtectReferences(std::array{region});
    CHECK(f.On(region, "contains", "(II)Z", {V::Int(10), V::Int(10)}).AsInt() == 1);
    CHECK(f.On(region, "contains", "(II)Z", {V::Int(1), V::Int(1)}).AsInt() == 0);
    const auto target = Bitmap(f, 24, 24);
    const auto target_root = f.vm.ProtectReferences(std::array{target});
    f.On(target, "eraseColor", "(I)V", {V::Int(std::bit_cast<int>(SK_ColorRED))});
    const auto canvas = f.New("Landroid/graphics/Canvas;", "(Landroid/graphics/Bitmap;)V", {V::Ref(target)});
    const auto paint = f.New("Landroid/graphics/Paint;");
    const auto roots = f.vm.ProtectReferences(std::array{canvas, paint});
    const auto mode =
        f.Static("Landroid/graphics/PorterDuff$Mode;", "valueOf",
                 "(Ljava/lang/String;)Landroid/graphics/PorterDuff$Mode;", {V::Ref(f.vm.NewStringUtf8("CLEAR"))});
    const auto clear = f.New("Landroid/graphics/PorterDuffXfermode;", "(Landroid/graphics/PorterDuff$Mode;)V", {mode});
    f.On(paint, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;", {V::Ref(clear)});
    const auto bounds =
        f.New("Landroid/graphics/RectF;", "(FFFF)V", {V::Float(1), V::Float(2), V::Float(21), V::Float(22)});
    f.On(copy, "setPaint", "(Landroid/graphics/Paint;)V", {V::Ref(paint)});
    f.On(copy, "draw", "(Landroid/graphics/Canvas;Landroid/graphics/RectF;)V", {V::Ref(canvas), V::Ref(bounds)});
    SkBitmap expected;
    expected.allocN32Pixels(24, 24);
    expected.eraseColor(SK_ColorRED);
    SkCanvas host(expected);
    SkPaint host_paint;
    host_paint.setBlendMode(SkBlendMode::kClear);
    host.drawRect(SkRect::MakeLTRB(1, 2, 21, 22), host_paint);
    Compare(f, target, expected);
}
}  // namespace
