#include "android_skia_bitmap.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/encode/SkJpegEncoder.h"
#include "include/encode/SkPngEncoder.h"
#include "ogplay/runtime/dexvm/nio_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ogplay::runtime::android_intrinsics::skia {
namespace {
using V = dx::VmValue;
constexpr auto kBitmap = "Landroid/graphics/Bitmap;";

std::pair<dx::VmObjectRef, std::shared_ptr<raster::Bitmap>> AllocatePixels(dx::Interpreter& vm,
                                                                           const BitmapCreateInfo& info) {
    const auto length = raster::Bitmap::AllocationSize(info.width, info.height, info.config);
    const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                    static_cast<JniSize>(length));
    const auto root = vm.ProtectReferences(std::array{array});
    auto lease = vm.Model().BorrowByteArray(array);
    memory::GuestAddress address;
    try {
        address = vm.Model().PrimitiveArrayAddress(array);
    } catch (const std::logic_error&) {
        // The standalone VM fixture can provide host-only arrays.
    }
    return {array, std::make_shared<raster::Bitmap>(info.width, info.height, info.config, info.premultiplied,
                                                    info.alpha, lease->Bytes(), lease, address)};
}

void CheckRect(raster::Bitmap& bitmap, int x, int y, int w, int h) {
    const auto& sk = bitmap.SkiaBitmap();
    if (x < 0 || y < 0 || w < 0 || h < 0 || static_cast<std::int64_t>(x) + w > sk.width() ||
        static_cast<std::int64_t>(y) + h > sk.height())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Bitmap pixel region outside bitmap"};
}

void TransferColors(dx::Interpreter& vm, raster::Bitmap& bitmap, dx::VmObjectRef array, int offset, int stride, int x,
                    int y, int width, int height, bool write) {
    CheckRect(bitmap, x, y, width, height);
    if (!width || !height) return;
    const auto last = static_cast<std::int64_t>(offset) + static_cast<std::int64_t>(height - 1) * stride;
    const auto low = std::min<std::int64_t>(offset, last);
    const auto high = std::max<std::int64_t>(offset, last);
    if (low < 0 || high > std::numeric_limits<int>::max())
        throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "Bitmap color array offset"};
    CheckRange(vm, array, static_cast<int>(low), high - low + width);
    std::vector<std::uint32_t> row(static_cast<std::size_t>(width));
    for (int dy = 0; dy < height; ++dy) {
        const auto row_offset =
            static_cast<int>(static_cast<std::int64_t>(offset) + static_cast<std::int64_t>(dy) * stride);
        if (write) {
            for (int i = 0; i < width; ++i)
                row[static_cast<std::size_t>(i)] =
                    static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, row_offset + i));
            bitmap.Write(x, y + dy, width, 1, row);
        } else {
            bitmap.Read(x, y + dy, width, 1, row);
            for (int i = 0; i < width; ++i)
                vm.Model().SetPrimitiveElement(array, row_offset + i, row[static_cast<std::size_t>(i)]);
        }
    }
}

class OutputStream final : public SkWStream {
public:
    OutputStream(dx::Interpreter& vm, dx::VmObjectRef stream, dx::VmObjectRef buffer)
        : vm_(vm), stream_(stream), buffer_(buffer),
          capacity_(static_cast<std::size_t>(vm.Model().ArrayLength(buffer))) {}

    bool write(const void* data, std::size_t size) override {
        if (!capacity_) return false;
        auto bytes = std::span(static_cast<const std::byte*>(data), size);
        while (!bytes.empty()) {
            const auto count = std::min(bytes.size(), capacity_);
            vm_.Model().WriteByteRegion(buffer_, 0, bytes.first(count));
            CallVirtual(vm_, stream_, "write", "([BII)V",
                        {V::Ref(buffer_), V::Int(0), V::Int(static_cast<int>(count))});
            written_ += count;
            bytes = bytes.subspan(count);
        }
        return true;
    }

    std::size_t bytesWritten() const override { return written_; }

private:
    dx::Interpreter& vm_;
    dx::VmObjectRef stream_, buffer_;
    std::size_t capacity_, written_{};
};

void TransferBuffer(dx::Interpreter& vm, raster::Bitmap& bitmap, dx::VmObjectRef buffer, bool write) {
    auto& nio = vm.NIO();
    const auto id = vm.Model().ToIdentity(buffer);
    const auto state = nio.Snapshot(id);
    const auto elem = dx::NioRuntime::ElementSize(state.element);
    auto pixels = bitmap.RawPixels();
    if (static_cast<std::uint64_t>(state.limit - state.position) * elem < pixels.size())
        throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;", "Buffer not large enough for pixels"};
    auto index = state.position;
    while (!pixels.empty()) {
        const auto count = std::min<std::size_t>(elem, pixels.size());
        std::uint64_t bits = count < elem || write ? nio.Get(id, index) : 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto shift = 8U * (state.order == dx::NioByteOrder::little_endian ? i : elem - 1U - i);
            if (write)
                pixels[i] = static_cast<std::byte>((bits >> shift) & 255U);
            else
                bits = (bits & ~(UINT64_C(255) << shift)) | (std::to_integer<std::uint64_t>(pixels[i]) << shift);
        }
        if (!write) nio.Put(id, index, bits);
        ++index;
        pixels = pixels.subspan(count);
    }
    if (write) bitmap.SkiaBitmap().notifyPixelsChanged();
}

int ReadInt(dx::Interpreter& vm, dx::VmObjectRef parcel) {
    return CallVirtual(vm, parcel, "readInt", "()I").AsInt();
}

void WriteInt(dx::Interpreter& vm, dx::VmObjectRef parcel, int value) {
    CallVirtual(vm, parcel, "writeInt", "(I)V", {V::Int(value)});
}

}  // namespace

raster::Bitmap& BitmapPeer(DexVmAndroidContext& context, int token) {
    return *Require(context.skia_objects->bitmaps, token);
}

dx::VmObjectRef CreateBitmap(dx::Interpreter& vm, DexVmAndroidContext& context, const BitmapCreateInfo& info,
                             const SkBitmap* source) {
    const auto roots = vm.ProtectReferences(std::array{info.nine_patch, info.layout_bounds});
    const auto type = vm.Linker().ResolveDescriptor(kBitmap);
    const auto initialized = vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid()) throw dx::VmJavaThrow{"", "", initialized.exception};
    const auto object = vm.NewIntrinsicInstance(kBitmap);
    const auto object_root = vm.ProtectReferences(std::array{object});
    const auto [array, bitmap] = AllocatePixels(vm, info);
    const auto array_root = vm.ProtectReferences(std::array{array});
    if (source && !source->readPixels(bitmap->SkiaBitmap().pixmap()))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unsupported Bitmap pixel conversion"};
    auto& store = context.skia_objects->bitmaps;
    const auto token = store.Create(bitmap);
    try {
        const auto ctor = vm.Linker().FindDirectMethod(type, "<init>", "(I[BIIIZZ[B[I)V");
        if (!ctor) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Bitmap constructor");
        const auto made =
            vm.Call(*ctor, std::array{V::Ref(object), JavaToken(token), V::Ref(array), V::Int(info.width),
                                      V::Int(info.height), V::Int(info.density), V::Int(info.mutable_pixels),
                                      V::Int(info.premultiplied), V::Ref(info.nine_patch), V::Ref(info.layout_bounds)});
        if (made.exception.IsValid()) throw dx::VmJavaThrow{"", "", made.exception};
        context.bitmaps.emplace(object.Value(), DexVmAndroidContext::BitmapState{
                                                    info.width, info.height, bitmap, false, info.config,
                                                    IntField(vm, object, "mDensity"), info.mutable_pixels, info.alpha});
        return object;
    } catch (...) {
        store.Retire(token);
        throw;
    }
}

void ReconfigureBitmap(dx::Interpreter& vm, DexVmAndroidContext::BitmapState& state, dx::VmObjectRef bitmap, int width,
                       int height, int config, bool alpha, bool premultiplied) {
    try {
        state.pixels->Reconfigure(width, height, config, alpha, premultiplied);
    } catch (const std::invalid_argument& e) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", e.what()};
    }
    state.width = width;
    state.height = height;
    state.config = config;
    state.has_alpha = alpha;
    CallVirtual(vm, bitmap, "reinit", "(IIZ)V", {V::Int(width), V::Int(height), V::Int(premultiplied)});
}

}  // namespace ogplay::runtime::android_intrinsics::skia

namespace ogplay::runtime::android_intrinsics {
using namespace skia;

dx::VmObjectRef CreateAndroidBitmap(dx::Interpreter& vm, DexVmAndroidContext& context, int width, int height,
                                    std::span<const std::uint32_t> argb, bool mutable_pixels, int density) {
    const bool alpha =
        mutable_pixels || argb.empty() || std::ranges::any_of(argb, [](auto color) { return (color >> 24U) != 255; });
    const auto object = CreateBitmap(vm, context, {width, height, 5, density, mutable_pixels, true, alpha});
    if (!argb.empty()) context.bitmaps.at(object.Value()).pixels->Write(0, 0, width, height, argb);
    return object;
}

Decl Declare_android_graphics_Bitmap(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap;");
    b.MergesBootClass();
    const auto get = [context](const dx::IntrinsicCall& a, unsigned index = 0) -> raster::Bitmap& {
        return BitmapPeer(*context, a.Int(index));
    };
    // Bridge the ROM crop/scale wrapper until the Canvas phase supplies the
    // Bitmap-backed Canvas entry points used by that wrapper.
    b.StaticMethod(
        "createBitmap", "(Landroid/graphics/Bitmap;IIIILandroid/graphics/Matrix;Z)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto owner = a.NonNullRef(0, "source");
            auto& source = BitmapPeer(*context, IntField(c.vm, owner, "mNativeBitmap"));
            const int x = a.Int(1), y = a.Int(2), w = a.Int(3), h = a.Int(4);
            CheckRect(source, x, y, w, h);
            if (w <= 0 || h <= 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Bitmap crop is empty"};
            const auto matrix = a.Ref(5);
            const auto& transform =
                matrix.IsValid() ? Require(context->skia_objects->matrices, IntField(c.vm, matrix, "native_instance"))
                                 : SkMatrix::I();
            const auto& sk = source.SkiaBitmap();
            if (transform.isIdentity() && x == 0 && y == 0 && w == sk.width() && h == sk.height() &&
                !CallVirtual(c.vm, owner, "isMutable", "()Z").AsInt())
                return V::Ref(owner);
            const auto dest = SkRect::MakeWH(static_cast<float>(w), static_cast<float>(h));
            const auto bounds = transform.mapRect(dest);
            if (!bounds.isFinite() || bounds.width() >= static_cast<float>(std::numeric_limits<int>::max()) ||
                bounds.height() >= static_cast<float>(std::numeric_limits<int>::max()))
                throw std::bad_alloc{};
            const int width = static_cast<int>(std::lround(bounds.width()));
            const int height = static_cast<int>(std::lround(bounds.height()));
            if (width <= 0 || height <= 0)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Bitmap transform is empty"};
            const bool rect = transform.rectStaysRect();
            const auto config = rect && source.Config() != 4 ? source.Config() : 5;
            const auto result = CreateBitmap(
                c.vm, *context,
                {width, height, config, IntField(c.vm, owner, "mDensity"), true, true, !rect || source.HasAlpha()});
            const auto image = SkImages::RasterFromBitmap(sk);
            if (!image) throw std::bad_alloc{};
            auto& target = *context->bitmaps.at(result.Value()).pixels;
            SkCanvas canvas(target.SkiaBitmap());
            canvas.translate(-bounds.left(), -bounds.top());
            canvas.concat(transform);
            SkPaint paint;
            paint.setAntiAlias(!rect);
            const auto src = SkRect::MakeXYWH(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w),
                                              static_cast<float>(h));
            canvas.drawImageRect(image, src, dest,
                                 SkSamplingOptions(a.Int(6) ? SkFilterMode::kLinear : SkFilterMode::kNearest), &paint,
                                 SkCanvas::kStrict_SrcRectConstraint);
            target.SetAlpha(target.HasAlpha(), source.Premultiplied());
            return V::Ref(result);
        });
    b.StaticMethod(
        "nativeCreate", "([IIIIIIZ)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto config = a.Int(5) == 4 ? 5 : a.Int(5);
            const auto result = CreateBitmap(c.vm, *context, {a.Int(3), a.Int(4), config, -1, a.Int(6) != 0});
            if (a.Ref(0).IsValid())
                TransferColors(c.vm, *context->bitmaps.at(result.Value()).pixels, a.Ref(0), a.Int(1), a.Int(2), 0, 0,
                               a.Int(3), a.Int(4), true);
            return V::Ref(result);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDestructor", "(I)V",
        [context](dx::IntrinsicContext& c) {
            context->skia_objects->bitmaps.Retire(
                NativeToken<std::shared_ptr<raster::Bitmap>>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeRecycle", "(I)Z",
        [context, get](dx::IntrinsicContext& c) {
            auto& bitmap = get(dx::IntrinsicCall(c));
            bitmap.Retire();
            for (auto& [_, state] : context->bitmaps)
                if (state.pixels.get() == &bitmap) {
                    state.recycled = true;
                    break;
                }
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeReconfigure", "(IIIII)V",
        [context, get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& bitmap = get(a);
            const auto config = a.Int(3) == 4 ? 5 : a.Int(3);
            try {
                bitmap.Reconfigure(a.Int(1), a.Int(2), config, bitmap.HasAlpha(), bitmap.Premultiplied());
            } catch (const std::invalid_argument& e) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", e.what()};
            }
            for (auto& [_, state] : context->bitmaps) {
                if (state.pixels.get() != &bitmap) continue;
                state.width = a.Int(1);
                state.height = a.Int(2);
                state.config = config;
                break;
            }
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCopy", "(IIZ)Landroid/graphics/Bitmap;",
        [context, get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& source = get(a);
            const auto& sk = source.SkiaBitmap();
            return V::Ref(CreateBitmap(
                c.vm, *context,
                {sk.width(), sk.height(), a.Int(1), -1, a.Int(2) != 0, source.Premultiplied(), source.HasAlpha()},
                &sk));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeExtractAlpha", "(II[I)Landroid/graphics/Bitmap;",
        [context, get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            SkBitmap alpha;
            if (!get(a).SkiaBitmap().extractAlpha(&alpha)) throw std::bad_alloc{};
            if (a.Ref(2).IsValid() && c.vm.Model().ArrayLength(a.Ref(2)) >= 2) {
                c.vm.Model().SetPrimitiveElement(a.Ref(2), 0, 0);
                c.vm.Model().SetPrimitiveElement(a.Ref(2), 1, 0);
            }
            return V::Ref(CreateBitmap(c.vm, *context, {alpha.width(), alpha.height(), 1}, &alpha));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSameAs", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& x = get(a);
            auto& y = get(a, 1);
            return V::Int(x.Config() == y.Config() && x.SkiaBitmap().dimensions() == y.SkiaBitmap().dimensions() &&
                          std::ranges::equal(x.RawPixels(), y.RawPixels()));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeErase", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a).SkiaBitmap().eraseColor(static_cast<std::uint32_t>(a.Int(1)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeGetPixel", "(IIIZ)I",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& bitmap = get(a);
            CheckRect(bitmap, a.Int(1), a.Int(2), 1, 1);
            const auto index =
                static_cast<std::size_t>(a.Int(2)) * static_cast<std::size_t>(bitmap.SkiaBitmap().width()) +
                static_cast<std::size_t>(a.Int(1));
            return V::Int(std::bit_cast<int>(bitmap.Get(index)));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSetPixel", "(IIIIZ)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& bitmap = get(a);
            CheckRect(bitmap, a.Int(1), a.Int(2), 1, 1);
            const auto color = std::bit_cast<std::uint32_t>(a.Int(3));
            bitmap.Write(a.Int(1), a.Int(2), 1, 1, {&color, 1});
            return V::Void();
        },
        kStaticNative);
    for (const bool write : {false, true}) {
        b.StaticMethod(
            write ? "nativeSetPixels" : "nativeGetPixels", "(I[IIIIIIIZ)V",
            [get, write](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                TransferColors(c.vm, get(a), a.Ref(1), a.Int(2), a.Int(3), a.Int(4), a.Int(5), a.Int(6), a.Int(7),
                               write);
                return V::Void();
            },
            kStaticNative);
        b.StaticMethod(
            write ? "nativeCopyPixelsFromBuffer" : "nativeCopyPixelsToBuffer", "(ILjava/nio/Buffer;)V",
            [get, write](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                TransferBuffer(c.vm, get(a), a.Ref(1), write);
                return V::Void();
            },
            kStaticNative);
    }
    b.StaticMethod(
        "nativeRowBytes", "(I)I",
        [get](dx::IntrinsicContext& c) { return V::Int(static_cast<int>(get(dx::IntrinsicCall(c)).Stride())); },
        kStaticNative);
    b.StaticMethod(
        "nativeConfig", "(I)I", [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c)).Config()); },
        kStaticNative);
    b.StaticMethod(
        "nativeGenerationId", "(I)I",
        [get](dx::IntrinsicContext& c) { return V::Int(std::bit_cast<int>(get(dx::IntrinsicCall(c)).Revision())); },
        kStaticNative);
    b.StaticMethod(
        "nativeHasAlpha", "(I)Z",
        [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c)).HasAlpha()); }, kStaticNative);
    b.StaticMethod(
        "nativeSetAlphaAndPremultiplied", "(IZZ)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a).SetAlpha(a.Int(1) != 0, a.Int(2) != 0);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeHasMipMap", "(I)Z", [get](dx::IntrinsicContext& c) { return V::Int(get(dx::IntrinsicCall(c)).mipmap); },
        kStaticNative);
    b.StaticMethod(
        "nativeSetHasMipMap", "(IZ)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a).mipmap = a.Int(1) != 0;
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativePrepareToDraw", "(I)V",
        [get](dx::IntrinsicContext& c) {
            // CPU bitmaps already hold their resident pixel ref.
            static_cast<void>(get(dx::IntrinsicCall(c)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCompress", "(IIILjava/io/OutputStream;[B)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& bitmap = get(a);
            OutputStream out(c.vm, a.NonNullRef(3, "stream"), a.NonNullRef(4, "temporary storage"));
            const SkBitmap snapshot = bitmap.SkiaBitmap();
            const auto& pixmap = snapshot.pixmap();
            switch (a.Int(1)) {
            case 0: {
                SkJpegEncoder::Options options;
                options.fQuality = a.Int(2);
                return V::Int(SkJpegEncoder::Encode(&out, pixmap, options));
            }
            case 1: return V::Int(SkPngEncoder::Encode(&out, pixmap, {}));
            default:
                if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.bitmap_encode.webp", 0);
                return V::Int(false);
            }
        },
        kStaticNative);
    b.StaticMethod(
        "nativeWriteToParcel", "(IZILandroid/os/Parcel;)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto parcel = a.Ref(3);
            if (!parcel.IsValid()) return V::Int(false);
            auto& bitmap = get(a);
            const auto& sk = bitmap.SkiaBitmap();
            const auto raw = bitmap.RawPixels();
            const std::vector bytes(raw.begin(), raw.end());
            for (const int value :
                 {a.Int(1), bitmap.Config(), sk.width(), sk.height(), static_cast<int>(bitmap.Stride()), a.Int(2), 0})
                WriteInt(c.vm, parcel, value);
            for (std::size_t i = 0; i < bytes.size(); i += 4) {
                std::uint32_t word{};
                std::memcpy(&word, bytes.data() + i, std::min<std::size_t>(4, bytes.size() - i));
                WriteInt(c.vm, parcel, std::bit_cast<int>(word));
            }
            return V::Int(true);
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCreateFromParcel", "(Landroid/os/Parcel;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& c) {
            const auto parcel = dx::IntrinsicCall(c).Ref(0);
            if (!parcel.IsValid()) return V::Ref(dx::VmObjectRef{});
            const bool mutable_pixels = ReadInt(c.vm, parcel) != 0;
            const int config = ReadInt(c.vm, parcel);
            const int width = ReadInt(c.vm, parcel);
            const int height = ReadInt(c.vm, parcel);
            const int row_bytes = ReadInt(c.vm, parcel);
            const int density = ReadInt(c.vm, parcel);
            const int blob = ReadInt(c.vm, parcel);
            if (blob != 0 || width <= 0 || height <= 0 || row_bytes < 0 ||
                (config != 1 && config != 3 && config != 4 && config != 5))
                throw dx::VmJavaThrow{"Landroid/os/BadParcelableException;", "unsupported Bitmap blob"};
            const auto size = raster::Bitmap::AllocationSize(width, height, config);
            if (size / static_cast<std::size_t>(height) != static_cast<std::size_t>(row_bytes) ||
                (size + 3) / 4 * 4 > static_cast<std::size_t>(CallVirtual(c.vm, parcel, "dataAvail", "()I").AsInt()))
                throw dx::VmJavaThrow{"Landroid/os/BadParcelableException;", "invalid Bitmap parcel size"};
            std::vector<std::byte> bytes(size);
            for (std::size_t i = 0; i < size; i += 4) {
                const int word = ReadInt(c.vm, parcel);
                std::memcpy(bytes.data() + i, &word, std::min<std::size_t>(4, size - i));
            }
            const auto result = CreateBitmap(c.vm, *context, {width, height, config, density, mutable_pixels});
            std::ranges::copy(bytes, context->bitmaps.at(result.Value()).pixels->RawPixels().begin());
            return V::Ref(result);
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
