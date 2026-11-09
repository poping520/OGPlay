#include "android_skia_bitmap.h"
#include "ogplay/runtime/boundary/asset_boundary.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/raster/image_decode.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "runtime/raster/bitmap_decode.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;
using Ref = dx::VmObjectRef;

void WriteInt(dx::Interpreter& vm, Ref owner, const char* name, int value, const char* type = "I") {
    Field(vm, owner, name, type) = {std::bit_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

bool Boolean(dx::Interpreter& vm, Ref owner, const char* name) {
    return Field(vm, owner, name, "Z").bits != 0;
}

Ref ReadRef(dx::Interpreter& vm, Ref owner, const char* name, const char* type) {
    return Ref{Field(vm, owner, name, type).bits};
}

void SetRef(dx::Interpreter& vm, Ref owner, const char* name, const char* type, Ref value) {
    Field(vm, owner, name, type) = {value.Value(), dx::SlotTag::ref};
}

void ResetOptions(dx::Interpreter& vm, Ref options) {
    if (!options.IsValid()) return;
    WriteInt(vm, options, "outWidth", -1);
    WriteInt(vm, options, "outHeight", -1);
    SetRef(vm, options, "outMimeType", "Ljava/lang/String;", Ref{});
}

bool Cancelled(dx::Interpreter& vm, Ref options) {
    return options.IsValid() && Boolean(vm, options, "mCancel");
}

V Decode(dx::Interpreter& vm, DexVmAndroidContext& context, std::span<const std::byte> bytes, Ref padding,
         Ref options) {
    if (Cancelled(vm, options)) return V::Ref(dx::VmObjectRef{});
    raster::DecodeOptions request;
    bool mutable_pixels{};
    Ref reuse;
    if (options.IsValid()) {
        const auto config = ReadRef(vm, options, "inPreferredConfig", "Landroid/graphics/Bitmap$Config;");
        if (config.IsValid()) request.config = IntField(vm, config, "nativeInt");
        request.sample = IntField(vm, options, "inSampleSize");
        request.premultiplied = Boolean(vm, options, "inPremultiplied");
        request.only_bounds = Boolean(vm, options, "inJustDecodeBounds");
        mutable_pixels = Boolean(vm, options, "inMutable");
        reuse = ReadRef(vm, options, "inBitmap", "Landroid/graphics/Bitmap;");
        const auto density = IntField(vm, options, "inDensity");
        const auto target = IntField(vm, options, "inTargetDensity");
        const auto screen = IntField(vm, options, "inScreenDensity");
        if (Boolean(vm, options, "inScaled") && density > 0 && target > 0 && density != screen)
            request.scale = static_cast<double>(target) / density;
    }
    const auto decoded = raster::DecodeBitmap(bytes, request);
    if (!decoded || Cancelled(vm, options)) return V::Ref(dx::VmObjectRef{});
    if (options.IsValid()) {
        WriteInt(vm, options, "outWidth", decoded->width);
        WriteInt(vm, options, "outHeight", decoded->height);
        const auto mime = decoded->mime ? vm.NewStringUtf8(decoded->mime) : Ref{};
        SetRef(vm, options, "outMimeType", "Ljava/lang/String;", mime);
    }
    if (request.only_bounds) return V::Ref(dx::VmObjectRef{});
    Ref patch, layout;
    if (!decoded->nine_patch.empty()) {
        patch = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                             static_cast<int>(decoded->nine_patch.size()));
        vm.Model().WriteByteRegion(patch, 0, decoded->nine_patch);
    }
    const auto patch_root = vm.ProtectReferences(std::array{patch});
    if (decoded->layout_bounds) {
        layout = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer, 4);
        for (int i = 0; i < 4; ++i)
            vm.Model().SetPrimitiveElement(
                layout, i, std::bit_cast<std::uint32_t>((*decoded->layout_bounds)[static_cast<std::size_t>(i)]));
    }
    const auto layout_root = vm.ProtectReferences(std::array{layout});
    const bool alpha = !decoded->pixels.isOpaque();
    Ref result;
    if (reuse.IsValid()) {
        const auto found = context.bitmaps.find(reuse.Value());
        if (found == context.bitmaps.end() || found->second.recycled || !found->second.mutable_pixels)
            return V::Ref(dx::VmObjectRef{});
        const auto storage = ReadRef(vm, reuse, "mBuffer", "[B");
        if (!storage.IsValid() ||
            decoded->pixels.computeByteSize() > static_cast<std::size_t>(vm.Model().ArrayLength(storage)))
            return V::Ref(dx::VmObjectRef{});
        auto& state = found->second;
        ReconfigureBitmap(vm, state, reuse, decoded->width, decoded->height, decoded->config, alpha,
                          request.premultiplied);
        if (!decoded->pixels.readPixels(state.pixels->SkiaBitmap().pixmap()))
            throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "BitmapFactory inBitmap conversion");
        state.pixels->SkiaBitmap().notifyPixelsChanged();
        SetRef(vm, reuse, "mNinePatchChunk", "[B", patch);
        SetRef(vm, reuse, "mLayoutBounds", "[I", layout);
        result = reuse;
    } else {
        result = CreateBitmap(vm, context,
                              {decoded->width, decoded->height, decoded->config,
                               static_cast<int>(std::lround(context.ui_density * 160.0F)), mutable_pixels,
                               request.premultiplied, alpha, patch, layout},
                              &decoded->pixels);
    }
    if (padding.IsValid()) {
        const auto& pad = decoded->padding;
        WriteRect(vm, padding, {pad[0], pad[1], pad[2], pad[3]});
    }
    return V::Ref(result);
}

template <class Read>
std::vector<std::byte> ReadBounded(Read&& read) {
    std::array<std::byte, 8192> buffer;
    std::vector<std::byte> bytes;
    for (;;) {
        const auto count = read(bytes.size(), std::span(buffer));
        if (count == 0) return bytes;
        if (count > buffer.size() || count > kMaximumImageEncodedBytes - bytes.size()) throw std::bad_alloc{};
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count));
    }
}
}  // namespace

Decl Declare_android_graphics_BitmapFactory_Options(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapFactory$Options;");
    b.MergesBootClass();
    // ROM sets mCancel before this call. There is no independently running decode job.
    b.DirectMethod(
        "requestCancel", "()V", [](dx::IntrinsicContext&) { return V::Void(); }, dx::kAccNative | dx::kAccPrivate);
    return std::move(b).Build();
}

Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapFactory;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeDecodeByteArray", "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto opts = a.Ref(3);
            ResetOptions(c.vm, opts);
            const auto array = a.NonNullRef(0, "data");
            CheckRange(c.vm, array, a.Int(1), a.Int(2));
            if (static_cast<std::size_t>(a.Int(2)) > kMaximumImageEncodedBytes) throw std::bad_alloc{};
            if (Cancelled(c.vm, opts)) return V::Ref(dx::VmObjectRef{});
            const auto bytes = c.vm.Model().ReadByteRegion(array, a.Int(1), a.Int(2));
            return Decode(c.vm, *context, bytes, dx::VmObjectRef{}, opts);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDecodeStream",
        "(Ljava/io/InputStream;[BLandroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto input = a.NonNullRef(0, "stream"), storage = a.NonNullRef(1, "temporary storage");
            const auto opts = a.Ref(3);
            ResetOptions(c.vm, opts);
            const int capacity = std::min(8192, c.vm.Model().ArrayLength(storage));
            if (capacity <= 0) return V::Ref(dx::VmObjectRef{});
            std::vector<std::byte> bytes;
            int stalled{};
            while (!Cancelled(c.vm, opts)) {
                const auto count =
                    CallVirtual(c.vm, input, "read", "([BII)I", {V::Ref(storage), V::Int(0), V::Int(capacity)}).AsInt();
                if (count < 0) break;
                if (count > capacity) throw dx::VmJavaThrow{"Ljava/io/IOException;", "invalid stream read length"};
                if (count == 0) {
                    if (++stalled == 8) return V::Ref(dx::VmObjectRef{});
                    continue;
                }
                stalled = 0;
                if (static_cast<std::size_t>(count) > kMaximumImageEncodedBytes - bytes.size()) throw std::bad_alloc{};
                const auto chunk = c.vm.Model().ReadByteRegion(storage, 0, count);
                bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            }
            return Decode(c.vm, *context, bytes, a.Ref(2), opts);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeIsSeekable", "(Ljava/io/FileDescriptor;)Z",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto fd = IntField(c.vm, a.NonNullRef(0, "descriptor"), "descriptor");
            if (!context->vfs) return V::Int(false);
            try {
                static_cast<void>(context->vfs->Seek(fd, 0, VfsSeekWhence::current));
                return V::Int(true);
            } catch (const std::exception&) {
                return V::Int(false);
            }
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDecodeFileDescriptor",
        "(Ljava/io/FileDescriptor;Landroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto fd = IntField(c.vm, a.NonNullRef(0, "descriptor"), "descriptor");
            const auto opts = a.Ref(2);
            ResetOptions(c.vm, opts);
            if (!context->vfs || Cancelled(c.vm, opts)) return V::Ref(dx::VmObjectRef{});
            std::vector<std::byte> bytes;
            try {
                const auto start = context->vfs->Seek(fd, 0, VfsSeekWhence::current);
                bytes = ReadBounded([&](std::uint64_t offset, std::span<std::byte> chunk) {
                    return context->vfs->ReadAt(fd, start + offset, chunk);
                });
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception&) {
                return V::Ref(dx::VmObjectRef{});
            }
            return Decode(c.vm, *context, bytes, a.Ref(1), opts);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDecodeAsset",
        "(ILandroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto opts = a.Ref(2);
            ResetOptions(c.vm, opts);
            if (!context->session || Cancelled(c.vm, opts)) return V::Ref(dx::VmObjectRef{});
            const auto source =
                context->session->Process().ResolveAsset(memory::GuestAddress{std::bit_cast<std::uint32_t>(a.Int(0))});
            if (!source) return V::Ref(dx::VmObjectRef{});
            const auto bytes = ReadBounded(
                [&](std::uint64_t offset, std::span<std::byte> chunk) { return source->ReadAt(offset, chunk); });
            return Decode(c.vm, *context, bytes, a.Ref(1), opts);
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_graphics_NinePatch(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/NinePatch;");
    b.MergesBootClass();
    b.StaticMethod(
        "isNinePatchChunk", "([B)Z",
        [](dx::IntrinsicContext& c) {
            const auto bytes = dx::IntrinsicCall(c).Ref(0);
            return V::Int(bytes.IsValid() && c.vm.Model().ArrayLength(bytes) >= 32 &&
                          c.vm.Model().GetPrimitiveElement(bytes, 0) != UINT8_MAX);
        },
        dx::kAccPublic | dx::kAccNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
