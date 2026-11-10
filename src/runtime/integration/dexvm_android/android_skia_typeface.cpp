#include "android_skia.h"

#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;
using Face = std::shared_ptr<const raster::Typeface>;

V MakeTypeface(DexVmAndroidContext& context, Face face) {
    if (!face) return V::Int(0);  // The ROM constructor throws the API19 exception.
    auto& objects = *context.skia_objects;
    if (face->family < 0) return JavaToken(objects.typefaces.Create(std::move(face)));
    const auto key = std::pair{face->family, face->style};
    if (const auto it = objects.system_typefaces.find(key); it != objects.system_typefaces.end())
        return JavaToken(it->second);
    const auto token = objects.typefaces.Create(std::move(face));
    objects.system_typefaces.emplace(key, token);
    return JavaToken(token);
}

Face FileTypeface(VirtualFileSystem& vfs, std::string_view path) {
    constexpr auto kMaxFontBytes = 64U * 1024U * 1024U;
    const auto descriptor = vfs.Open(path, {.read = true});

    struct OpenFile {
        VirtualFileSystem& vfs;
        int descriptor;

        ~OpenFile() { vfs.Close(descriptor); }
    } file{vfs, descriptor};

    const auto info = vfs.DescriptorInfo(descriptor);
    if (!info.size || info.size > kMaxFontBytes) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(info.size));
    const auto lease = vfs.CaptureReadLease(descriptor, 0, info.size);
    if (lease->ReadAt(0, bytes, {}) != bytes.size()) return {};
    return raster::DecodeTypeface(bytes);
}

void RequireFontPath(dx::VmObjectRef path) {
    if (!path.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "font path"};
}
}  // namespace

void RetireAndroidTypeface(DexVmAndroidContext& context, int bits) {
    auto& objects = context.skia_objects->typefaces;
    const auto token = NativeToken<Face>(bits);
    if (const auto* face = objects.Find(token); face && (*face)->family < 0) objects.Retire(token);
}

Decl Declare_android_graphics_Typeface(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Typeface;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeCreate", "(Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& c) {
            const auto name = c.arguments[0].ref.IsValid() ? c.vm.StringUtf8(c.arguments[0].ref) : std::string{};
            return MakeTypeface(*context,
                                raster::SystemTypeface(context->system_font_source, raster::SystemFontFamily(name),
                                                       c.arguments[1].AsInt()));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCreateFromTypeface", "(II)I",
        [context](dx::IntrinsicContext& c) {
            const auto token = c.arguments[0].AsInt();
            const auto style = c.arguments[1].AsInt();
            const auto& source = context->system_font_source;
            const auto face =
                token ? raster::RestyleTypeface(source, *Require(context->skia_objects->typefaces, token), style)
                      : raster::SystemTypeface(source, 0, style);
            return MakeTypeface(*context, face);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeUnref", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RetireAndroidTypeface(*context, c.arguments[0].AsInt());
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeGetStyle", "(I)I",
        [context](dx::IntrinsicContext& c) {
            return V::Int(Require(context->skia_objects->typefaces, c.arguments[0].AsInt())->style);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCreateFromAsset", "(Landroid/content/res/AssetManager;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& c) {
            const auto manager = c.arguments[0].ref;
            const auto path = c.arguments[1].ref;
            if (!manager.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "AssetManager"};
            RequireFontPath(path);
            try {
                return MakeTypeface(*context, raster::DecodeTypeface(
                                                  ReadAndroidApkAsset(c.vm, context, manager, c.vm.StringUtf8(path))));
            } catch (const dx::VmJavaThrow& error) {
                if (error.descriptor != "Ljava/io/FileNotFoundException;" &&
                    error.descriptor != "Ljava/io/IOException;")
                    throw;
                return V::Int(0);
            }
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCreateFromFile", "(Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& c) {
            const auto path = c.arguments[0].ref;
            RequireFontPath(path);
            try {
                return context->vfs ? MakeTypeface(*context, FileTypeface(*context->vfs, c.vm.StringUtf8(path)))
                                    : V::Int(0);
            } catch (const VfsError&) {
                return V::Int(0);
            }
        },
        kStaticNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
