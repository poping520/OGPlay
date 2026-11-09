#include <array>
#include <bit>
#include <doctest/doctest.h>

#include "../fixtures/bitmap_decode_data.h"
#include "android_value_fixture.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {
using namespace ogplay::test::android_value;

TEST_CASE("Skia ROM Bitmap stores premultiplied bytes in its Java buffer") {
    AndroidValueVm f;
    constexpr auto type = "Landroid/graphics/Bitmap;";
    const auto cfg =
        f.Static("Landroid/graphics/Bitmap$Config;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                 {VmValue::Ref(f.vm.NewStringUtf8("ARGB_8888"))});
    const auto bitmap = f.Static(type, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                 {VmValue::Int(2), VmValue::Int(2), cfg})
                            .ref;
    const auto root = f.vm.ProtectReferences(std::array{bitmap});
    const auto read = [&](const char* name) {
        const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(bitmap), name, "[B");
        REQUIRE(id);
        return VmObjectRef{f.model.InstanceSlots(bitmap)[f.linker.Field(*id).slot].bits};
    };
    const auto storage = read("mBuffer");
    REQUIRE(storage.IsValid());
    const auto before = f.On(bitmap, "getGenerationId", "()I");
    const auto color = VmValue::Int(std::bit_cast<int>(0x80804020U));
    f.On(bitmap, "setPixel", "(III)V", {VmValue::Int(0), VmValue::Int(0), color});
    const auto raw = f.model.ReadByteRegion(storage, 0, 4);
    CHECK(raw == std::vector<std::byte>{std::byte{64}, std::byte{32}, std::byte{16}, std::byte{128}});
    CHECK(f.On(bitmap, "getPixel", "(II)I", {VmValue::Int(0), VmValue::Int(0)}).AsInt() == color.AsInt());
    CHECK(f.On(bitmap, "getGenerationId", "()I").AsInt() != before.AsInt());
    f.On(bitmap, "reconfigure", "(IILandroid/graphics/Bitmap$Config;)V", {VmValue::Int(1), VmValue::Int(1), cfg});
    CHECK(read("mBuffer") == storage);
    CHECK(f.On(bitmap, "getAllocationByteCount", "()I").AsInt() == 16);
    CHECK(f.On(bitmap, "getByteCount", "()I").AsInt() == 4);
}

TEST_CASE("Skia ROM Bitmap copies into each native config and extracts alpha") {
    AndroidValueVm f;
    constexpr auto type = "Landroid/graphics/Bitmap;";
    const auto configs =
        f.Static("Landroid/graphics/Bitmap$Config;", "values", "()[Landroid/graphics/Bitmap$Config;").ref;
    const auto cfg_root = f.vm.ProtectReferences(std::array{configs});
    const auto cfg8888 = f.model.GetObjectElement(configs, 3);
    const auto source = f.Static(type, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                 {VmValue::Int(1), VmValue::Int(1), VmValue::Ref(cfg8888)})
                            .ref;
    const auto root = f.vm.ProtectReferences(std::array{source});
    f.On(source, "eraseColor", "(I)V", {VmValue::Int(std::bit_cast<int>(0xffff0000U))});
    for (JniSize index = 0; index < 4; ++index) {
        CAPTURE(index);
        const auto config = f.model.GetObjectElement(configs, index);
        const auto copy = f.On(source, "copy", "(Landroid/graphics/Bitmap$Config;Z)Landroid/graphics/Bitmap;",
                               {VmValue::Ref(config), VmValue::Int(false)})
                              .ref;
        REQUIRE(copy.IsValid());
        CHECK(f.On(copy, "getConfig", "()Landroid/graphics/Bitmap$Config;").ref == config);
        const auto expected = index == 0 ? 0xff000000U : 0xffff0000U;
        CHECK(f.On(copy, "getPixel", "(II)I", {VmValue::Int(0), VmValue::Int(0)}).AsInt() ==
              std::bit_cast<int>(expected));
    }
    f.On(source, "eraseColor", "(I)V", {VmValue::Int(std::bit_cast<int>(0x80ff0000U))});
    const auto alpha = f.On(source, "extractAlpha", "()Landroid/graphics/Bitmap;").ref;
    REQUIRE(alpha.IsValid());
    CHECK(f.On(alpha, "getPixel", "(II)I", {VmValue::Int(0), VmValue::Int(0)}).AsInt() ==
          std::bit_cast<int>(0x80000000U));
}

TEST_CASE("Skia ROM Bitmap encodes PNG and BitmapFactory reuses a mutable allocation") {
    AndroidValueVm f;
    constexpr auto type = "Landroid/graphics/Bitmap;";
    const auto cfg =
        f.Static("Landroid/graphics/Bitmap$Config;", "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                 {VmValue::Ref(f.vm.NewStringUtf8("ARGB_8888"))});
    const auto create = [&](int width) {
        return f
            .Static(type, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                    {VmValue::Int(width), VmValue::Int(1), cfg})
            .ref;
    };
    const auto source = create(1);
    const auto source_root = f.vm.ProtectReferences(std::array{source});
    const auto dest = create(2);
    const auto dest_root = f.vm.ProtectReferences(std::array{dest});
    const auto stream = f.New("Ljava/io/ByteArrayOutputStream;");
    const auto stream_root = f.vm.ProtectReferences(std::array{stream});
    const auto png = f.Static("Landroid/graphics/Bitmap$CompressFormat;", "valueOf",
                              "(Ljava/lang/String;)Landroid/graphics/Bitmap$CompressFormat;",
                              {VmValue::Ref(f.vm.NewStringUtf8("PNG"))});
    f.On(source, "eraseColor", "(I)V", {VmValue::Int(std::bit_cast<int>(0xff102030U))});
    CHECK(f.On(source, "compress", "(Landroid/graphics/Bitmap$CompressFormat;ILjava/io/OutputStream;)Z",
               {png, VmValue::Int(100), VmValue::Ref(stream)})
              .AsInt() == 1);
    const auto bytes = f.On(stream, "toByteArray", "()[B").ref;
    const auto bytes_root = f.vm.ProtectReferences(std::array{bytes});
    const auto opts = f.New("Landroid/graphics/BitmapFactory$Options;");
    const auto in_bitmap = f.linker.FindFieldRecursive(f.model.ObjectClass(opts), "inBitmap", type);
    REQUIRE(in_bitmap);
    f.model.InstanceSlots(opts)[f.linker.Field(*in_bitmap).slot] = {dest.Value(), SlotTag::ref};
    const auto decoded =
        f.Static("Landroid/graphics/BitmapFactory;", "decodeByteArray",
                 "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
                 {VmValue::Ref(bytes), VmValue::Int(0), VmValue::Int(f.model.ArrayLength(bytes)), VmValue::Ref(opts)})
            .ref;
    REQUIRE(decoded == dest);
    CHECK(f.On(dest, "sameAs", "(Landroid/graphics/Bitmap;)Z", {VmValue::Ref(source)}).AsInt() == 1);
    CHECK(f.On(dest, "getAllocationByteCount", "()I").AsInt() == 8);
}

TEST_CASE("Skia ROM BitmapFactory decodes a seekable descriptor at its current offset") {
    AndroidValueVm f;
    REQUIRE(f.context->vfs);
    constexpr auto path = "/data/local/tmp/bitmap-decode.png";
    auto png = ogplay::test::BitmapDecodePng();
    png.insert(png.begin(), std::byte{0x21});
    f.context->vfs->PutFile(path, png, false);
    const auto stream =
        f.New("Ljava/io/FileInputStream;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8(path))});
    const auto stream_root = f.vm.ProtectReferences(std::array{stream});
    REQUIRE(f.On(stream, "read", "()I").AsInt() == 0x21);
    const auto fd = f.On(stream, "getFD", "()Ljava/io/FileDescriptor;").ref;
    const auto bitmap =
        f.Static(
             "Landroid/graphics/BitmapFactory;", "decodeFileDescriptor",
             "(Ljava/io/FileDescriptor;Landroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
             {VmValue::Ref(fd), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})})
            .ref;
    REQUIRE(bitmap.IsValid());
    CHECK(f.On(bitmap, "getWidth", "()I").AsInt() == 2);
    CHECK(f.On(stream, "read", "()I").AsInt() == 0x89);
    f.On(stream, "close", "()V");
}

TEST_CASE("Skia ROM BitmapFactory scales PNG nine-patch metadata and output padding") {
    AndroidValueVm f;
    auto png = ogplay::test::BitmapDecodePng();
    // Compiled npTc has divs X=[0, 2], Y=[0, 1] and padding [1, 0, 0, 1].
    // npLb repeats those padding values in the device byte order.
    constexpr std::array<unsigned char, 92> chunks{
        0x00, 0x00, 0x00, 0x34, 0x6e, 0x70, 0x54, 0x63, 0x00, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x18, 0x7f, 0x7e, 0x07,
        0x00, 0x00, 0x00, 0x10, 0x6e, 0x70, 0x4c, 0x62, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x56, 0x9e, 0xb9, 0x96};
    const auto raw = std::as_bytes(std::span(chunks));
    png.insert(png.begin() + 33, raw.begin(), raw.end());
    const auto bytes = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                 static_cast<JniSize>(png.size()));
    const auto bytes_root = f.vm.ProtectReferences(std::array{bytes});
    f.model.WriteByteRegion(bytes, 0, png);
    const auto stream = f.New("Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(bytes)});
    const auto stream_root = f.vm.ProtectReferences(std::array{stream});
    const auto rect = f.New("Landroid/graphics/Rect;");
    const auto rect_root = f.vm.ProtectReferences(std::array{rect});
    const auto options = f.New("Landroid/graphics/BitmapFactory$Options;");
    for (const auto& [name, density] : {std::pair{"inDensity", 160U}, {"inTargetDensity", 320U}}) {
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(options), name, "I");
        REQUIRE(field);
        f.model.InstanceSlots(options)[f.linker.Field(*field).slot] = {density, SlotTag::cat1};
    }
    const auto bitmap =
        f.Static(
             "Landroid/graphics/BitmapFactory;", "decodeStream",
             "(Ljava/io/InputStream;Landroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
             {VmValue::Ref(stream), VmValue::Ref(rect), VmValue::Ref(options)})
            .ref;
    REQUIRE(bitmap.IsValid());
    const auto root = f.vm.ProtectReferences(std::array{bitmap});
    CHECK(f.On(bitmap, "getWidth", "()I").AsInt() == 4);
    CHECK(f.On(bitmap, "getHeight", "()I").AsInt() == 2);
    CHECK(f.On(bitmap, "getDensity", "()I").AsInt() == 320);
    const auto serialized = f.On(bitmap, "getNinePatchChunk", "()[B").ref;
    REQUIRE(serialized.IsValid());
    CHECK(f.model.ArrayLength(serialized) == 52);
    const auto layout = f.linker.FindFieldRecursive(f.model.ObjectClass(bitmap), "mLayoutBounds", "[I");
    REQUIRE(layout);
    const auto layout_values = VmObjectRef{f.model.InstanceSlots(bitmap)[f.linker.Field(*layout).slot].bits};
    REQUIRE(layout_values.IsValid());
    for (const auto& [index, expected] : {std::pair{0, 2U}, {1, 0U}, {2, 0U}, {3, 2U}})
        CHECK(f.model.GetPrimitiveElement(layout_values, index) == expected);
    for (const auto& [name, expected] : {std::pair{"left", 2U}, {"top", 0U}, {"right", 0U}, {"bottom", 2U}}) {
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(rect), name, "I");
        REQUIRE(field);
        CHECK(f.model.InstanceSlots(rect)[f.linker.Field(*field).slot].bits == expected);
    }
}
}  // namespace
