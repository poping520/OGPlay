// DVM-80: API-family translation unit. DVM-86 adds the high-frequency API 19
// geometry and drawable value classes here.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include "ogplay/runtime/ui/ui_leaf_measure.h"

// ---- migrated from android_graphics_Bitmap_Config.cpp ----
#include "catalog.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr std::string_view kConfigDescriptor =
    "Landroid/graphics/Bitmap$Config;";
constexpr std::array kConfigs{
    std::pair{"ALPHA_8", 1}, std::pair{"RGB_565", 3},
    std::pair{"ARGB_4444", 4}, std::pair{"ARGB_8888", 5}};

[[nodiscard]] dx::VmObjectRef NewConfigArray(
    dx::Interpreter& vm, const std::span<const dx::VmObjectRef> values) {
    const auto array_class = vm.Linker().ResolveDescriptor(
        "[Landroid/graphics/Bitmap$Config;");
    const auto element_class = vm.Linker().ResolveDescriptor(
        kConfigDescriptor);
    const auto array = vm.Model().NewObjectArray(
        array_class, element_class,
        static_cast<JniSize>(values.size()));
    for (std::size_t index = 0; index < values.size(); ++index) {
        vm.Model().SetObjectElement(
            array, static_cast<JniSize>(index), values[index]);
    }
    return array;
}

[[nodiscard]] std::int32_t ReadIntField(
    dx::Interpreter& vm, const dx::VmObjectRef object,
    const std::string_view name) {
    const auto field = vm.Linker().FindFieldRecursive(
        vm.Model().ObjectClass(object), std::string(name), "I");
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "intrinsic int field is unavailable: " +
                                 std::string(name));
    }
    const auto& linked = vm.Linker().Field(*field);
    const auto slots = vm.Model().InstanceSlots(object);
    if (linked.is_ref || linked.is_wide || linked.slot >= slots.size()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "intrinsic int field is invalid: " +
                                 std::string(name));
    }
    return static_cast<std::int32_t>(slots[linked.slot].bits);
}

[[nodiscard]] std::int32_t BitmapConfigValue(
    dx::IntrinsicContext& call, const dx::VmObjectRef config) {
    if (!config.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap config is null"};
    }
    const auto config_class = call.vm.Linker().ResolveDescriptor(
        "Landroid/graphics/Bitmap$Config;");
    if (!call.vm.Linker().IsAssignable(
            config_class, call.vm.Model().ObjectClass(config))) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "createBitmap config has the wrong type"};
    }
    return ReadIntField(call.vm, config, "nativeInt");
}

}  // namespace

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicEnumBuilder enum_builder(
        std::string(kConfigDescriptor),
        {"ALPHA_8", "RGB_565", "ARGB_4444", "ARGB_8888"});
    auto& builder = enum_builder.ClassBuilder();
    const auto configs = builder.BoundStaticField(
        "sConfigs", "[Landroid/graphics/Bitmap$Config;", dx::kAccPrivate);
    const auto native_int =
        builder.BoundInstanceField("nativeInt", "I", dx::kAccFinal);
    builder.Constructor("(Ljava/lang/String;II)V",
        [native_int](dx::IntrinsicContext& context) {
            const dx::IntrinsicCall call(context);
            dx::IntrinsicEnumBuilder::InitializeBase(
                context, context.receiver,
                context.vm.StringUtf8(call.NonNullRef(0, "name")),
                call.Int(1));
            call.SetInt(native_int, call.Int(2));
            return dx::VmValue::Void();
        }, dx::kAccPrivate);
    builder.StaticMethod(
        "nativeToConfig", "(I)Landroid/graphics/Bitmap$Config;",
        [configs](dx::IntrinsicContext& context) {
            const dx::IntrinsicCall call(context);
            const auto index = call.Int(0);
            if (index < 0 || index >= 6) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "Bitmap.Config native index " + std::to_string(index)};
            }
            return dx::VmValue::Ref(
                context.vm.Model().GetObjectElement(call.GetRef(configs), index));
        }, dx::kAccStatic);
    enum_builder.WithConstantInitializer(
        [native_int](dx::IntrinsicContext& context,
                     const dx::VmObjectRef value, std::string_view,
                     const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value,
                        kConfigs[static_cast<std::size_t>(ordinal)].second);
        });
    enum_builder.AfterConstants(
        [configs](dx::IntrinsicContext& context,
                  const std::span<const dx::VmObjectRef> values) {
            const std::array<dx::VmObjectRef, 6> native_configs{
                dx::VmObjectRef{}, values[0], dx::VmObjectRef{}, values[1],
                values[2], values[3]};
            const dx::IntrinsicCall call(context);
            call.SetRef(configs, NewConfigArray(context.vm, native_configs));
        });
    return std::move(enum_builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Bitmap.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

[[nodiscard]] DexVmAndroidContext::BitmapState& BitmapOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->bitmaps.find(call.receiver.Value());
    if (found == context->bitmaps.end() || found->second.recycled) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "bitmap is recycled or was never created"};
    }
    return found->second;
}

// Builds a Bitmap intrinsic instance from pixels copied out of a guest int
// array (offset/stride window per the createBitmap contract).
[[nodiscard]] dx::VmValue MakeBitmapFromArray(
    dx::IntrinsicContext& call, const Context& context,
    const dx::VmObjectRef array, const std::int32_t offset,
    const std::int32_t stride, const std::int32_t width,
    const std::int32_t height, const dx::VmObjectRef config) {
    auto& model = call.vm.Model();
    if (!array.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "createBitmap colors array is null"};
    }
    if (width <= 0 || height <= 0 || stride < width) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " + std::to_string(width) +
                "x" + std::to_string(height) + " stride " +
                std::to_string(stride)};
    }
    const auto length = static_cast<std::int64_t>(model.ArrayLength(array));
    const auto last = static_cast<std::int64_t>(offset) +
                      static_cast<std::int64_t>(stride) * (height - 1) +
                      width;
    if (offset < 0 || last > length) {
        throw dx::VmJavaThrow{
            "Ljava/lang/ArrayIndexOutOfBoundsException;",
            "createBitmap window exceeds the colors array"};
    }
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.config = BitmapConfigValue(call, config);
    state.pixels = std::make_shared<BitmapPixels>(width, height, state.config);
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            state.pixels->Set(static_cast<std::size_t>(row) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(column), static_cast<std::uint32_t>(model.GetPrimitiveElement(
                    array, offset + row * stride + column)));
        }
    }
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

[[nodiscard]] dx::VmValue MakeEmptyBitmap(
    dx::IntrinsicContext& call, const Context& context,
    const std::int32_t width, const std::int32_t height,
    const dx::VmObjectRef config) {
    const auto config_value = BitmapConfigValue(call, config);
    if (width <= 0 || height <= 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "createBitmap dimensions are invalid: " +
                std::to_string(width) + "x" + std::to_string(height)};
    }
    DexVmAndroidContext::BitmapState state;
    state.width = width;
    state.height = height;
    state.config = config_value;
    state.pixels = std::make_shared<BitmapPixels>(width, height, state.config);
    const auto instance =
        call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[instance.Value()] = std::move(state);
    return dx::VmValue::Ref(instance);
}

}  // namespace

Decl Declare_android_graphics_Bitmap(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Bitmap;", "Ljava/lang/Object;",
                                                    {"Landroid/os/Parcelable;"});
    builder.StaticMethod("createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            return MakeEmptyBitmap(call, context, call.arguments[0].AsInt(),
                                   call.arguments[1].AsInt(),
                                   call.arguments[2].ref);
        });
    builder.StaticMethod("createBitmap",
        "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            const auto array = call.arguments[0].ref;
            const auto width = call.arguments[1].AsInt();
            const auto height = call.arguments[2].AsInt();
            return MakeBitmapFromArray(call, context, array, 0, width, width,
                                       height, call.arguments[3].ref);
        });
    builder.StaticMethod("createBitmap",
        "([IIIIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
        [context](dx::IntrinsicContext& call) {
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto width = call.arguments[3].AsInt();
            const auto height = call.arguments[4].AsInt();
            return MakeBitmapFromArray(call, context, array, offset, stride,
                                        width, height, call.arguments[5].ref);
        });
    builder.FinalMethod("getWidth", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(BitmapOf(call, context).width);
        });
    builder.FinalMethod("getHeight", "()I",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(BitmapOf(call, context).height);
        });
    builder.VirtualMethod("getDensity", "()I", [context](dx::IntrinsicContext& call) { return dx::VmValue::Int(BitmapOf(call, context).density); });
    builder.VirtualMethod("setDensity", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto density = call.arguments[0].AsInt();
        if (density < 0) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "bitmap density cannot be negative"};
        BitmapOf(call, context).density = density; return dx::VmValue::Void();
    });
    builder.VirtualMethod("isMutable", "()Z", [context](dx::IntrinsicContext& call) { return dx::VmValue::Int(BitmapOf(call, context).mutable_pixels); });
    builder.FinalMethod("getPixels", "([IIIIIII)V",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            const auto& state = BitmapOf(call, context);
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto x = call.arguments[3].AsInt();
            const auto y = call.arguments[4].AsInt();
            const auto width = call.arguments[5].AsInt();
            const auto height = call.arguments[6].AsInt();
            if (!array.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "getPixels target array is null"};
            }
            if (x < 0 || y < 0 || width < 0 || height < 0 ||
                x + width > state.width || y + height > state.height) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "getPixels region exceeds the bitmap"};
            }
            if (width == 0 || height == 0) return dx::VmValue::Void();
            const auto length =
                static_cast<std::int64_t>(model.ArrayLength(array));
            const auto last = static_cast<std::int64_t>(offset) +
                              static_cast<std::int64_t>(stride) *
                                  (height - 1) +
                              width;
            if (offset < 0 || stride < width || last > length) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "getPixels window exceeds the target array"};
            }
            for (std::int32_t row = 0; row < height; ++row) {
                for (std::int32_t column = 0; column < width; ++column) {
                    const auto pixel =
                        state.pixels->Get(static_cast<std::size_t>(y + row) *
                                       static_cast<std::size_t>(
                                           state.width) +
                                   static_cast<std::size_t>(x + column));
                    model.SetPrimitiveElement(array,
                                              offset + row * stride + column,
                                              pixel);
                }
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setPixels", "([IIIIIII)V",
        [context](dx::IntrinsicContext& call) {
            auto& model = call.vm.Model();
            auto& state = BitmapOf(call, context);
            if (!state.mutable_pixels) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "bitmap is immutable"};
            const auto array = call.arguments[0].ref;
            const auto offset = call.arguments[1].AsInt();
            const auto stride = call.arguments[2].AsInt();
            const auto x = call.arguments[3].AsInt();
            const auto y = call.arguments[4].AsInt();
            const auto width = call.arguments[5].AsInt();
            const auto height = call.arguments[6].AsInt();
            if (!array.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "setPixels source array is null"};
            }
            if (x < 0 || y < 0 || width < 0 || height < 0 ||
                x + width > state.width || y + height > state.height) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "setPixels region exceeds the bitmap"};
            }
            if (width == 0 || height == 0) return dx::VmValue::Void();
            const auto final_row = static_cast<std::int64_t>(offset) +
                static_cast<std::int64_t>(stride) * (height - 1);
            const auto first = std::min<std::int64_t>(offset, final_row);
            const auto last = std::max<std::int64_t>(offset, final_row) + width;
            const auto absolute_stride =
                std::abs(static_cast<std::int64_t>(stride));
            if (first < 0 || last > model.ArrayLength(array) ||
                absolute_stride < width) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/ArrayIndexOutOfBoundsException;",
                    "setPixels window exceeds the source array"};
            }
            for (std::int32_t row = 0; row < height; ++row) {
                for (std::int32_t column = 0; column < width; ++column) {
                    state.pixels->Set(static_cast<std::size_t>(y + row) *
                                   static_cast<std::size_t>(state.width) +
                               static_cast<std::size_t>(x + column), static_cast<std::uint32_t>(model.GetPrimitiveElement(
                            array, offset + row * stride + column)));
                }
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("prepareToDraw", "()V", GraphicsNoopHandler());
    builder.FinalMethod("recycle", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found = context->bitmaps.find(call.receiver.Value());
            if (found != context->bitmaps.end()) {
                found->second.recycled = true;
                found->second.pixels->Retire();
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_BitmapFactory.cpp ----
#include "ogplay/runtime/integration/host_image_decode.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
enum DecodeField : std::size_t { reuse, config, sample, density, target_density, screen_density,
    scaled, bounds, mutable_flag, premultiplied, dither, purgeable, shareable, quality,
    cancel, out_width, out_height, out_mime, temp_storage };
using DecodeFields = std::vector<dx::IntrinsicFieldHandle>;

[[noreturn]] void DecodeUnsupported(dx::IntrinsicContext& call, const char* message) {
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.bitmap_decode", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}
void ValidateDecodeOptions(dx::IntrinsicContext& call, const DecodeFields& fields, dx::VmObjectRef opts) {
    if (!opts.IsValid()) return;
    const dx::IntrinsicCall api(call);
    const auto preferred = api.GetRef(fields[config], opts);
    if (api.GetRef(fields[reuse], opts).IsValid() || api.GetInt(fields[sample], opts) > 1 ||
        (preferred.IsValid() && BitmapConfigValue(call, preferred) != 5) ||
        !api.GetInt(fields[premultiplied], opts) || api.GetInt(fields[dither], opts) ||
        api.GetInt(fields[purgeable], opts) || api.GetInt(fields[quality], opts))
        DecodeUnsupported(call, "BitmapFactory supports default ARGB8888, bounds and density scaling; reuse/sampling/purge/dither/quality modes are unavailable");
    if (api.GetInt(fields[density], opts) < 0 || api.GetInt(fields[target_density], opts) < 0 || api.GetInt(fields[screen_density], opts) < 0)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "bitmap density cannot be negative"};
}
void ResetDecodeOutput(dx::IntrinsicContext& call, const DecodeFields& fields, dx::VmObjectRef opts) {
    if (!opts.IsValid()) return;
    const dx::IntrinsicCall api(call);
    api.SetInt(fields[out_width], opts, -1); api.SetInt(fields[out_height], opts, -1);
    api.SetRef(fields[out_mime], opts, dx::VmObjectRef{});
}
bool Cancelled(dx::IntrinsicContext& call, const DecodeFields& fields, dx::VmObjectRef opts) {
    return opts.IsValid() && dx::IntrinsicCall(call).GetInt(fields[cancel], opts);
}
bool HasNinePatchChunk(std::span<const std::byte> bytes) {
    if (bytes.size() < 8 || std::to_integer<unsigned>(bytes[0]) != 137) return false;
    for (std::size_t pos = 8; pos + 12 <= bytes.size();) {
        std::uint32_t size{};
        for (std::size_t i = 0; i < 4; ++i) size = (size << 8) | std::to_integer<unsigned>(bytes[pos+i]);
        if (size > bytes.size() - pos - 12) break;
        if (std::to_integer<char>(bytes[pos+4]) == 'n' && std::to_integer<char>(bytes[pos+5]) == 'p' &&
            std::to_integer<char>(bytes[pos+6]) == 'T' && std::to_integer<char>(bytes[pos+7]) == 'c') return true;
        pos += size + 12;
    }
    return false;
}
dx::VmValue DecodeBitmapBytes(dx::IntrinsicContext& call, const Context& context,
    const DecodeFields& fields, std::span<const std::byte> bytes, dx::VmObjectRef opts) {
    const dx::IntrinsicCall api(call);
    if (bytes.size() > kMaximumImageEncodedBytes)
        throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "encoded bitmap exceeds decode budget"};
    if (Cancelled(call, fields, opts)) return dx::VmValue::Ref(dx::VmObjectRef{});
    const auto info = ProbeImage(bytes);
    if (!info) return dx::VmValue::Ref(dx::VmObjectRef{});
    if (!ImageDimensionsSupported(info->width, info->height))
        throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "bitmap dimensions exceed decode budget"};
    if (HasNinePatchChunk(bytes)) DecodeUnsupported(call, "BitmapFactory resource NinePatch decoding is unavailable");
    const bool only_bounds = opts.IsValid() && api.GetInt(fields[bounds], opts);
    int width = info->width, height = info->height;
    if (!only_bounds && opts.IsValid() && api.GetInt(fields[scaled], opts)) {
        const auto source = api.GetInt(fields[density], opts), target = api.GetInt(fields[target_density], opts);
        if (source && target && source != target && source != api.GetInt(fields[screen_density], opts)) {
            const auto ratio = static_cast<float>(target) / static_cast<float>(source);
            const auto w = static_cast<double>(width) * ratio + 0.5;
            const auto h = static_cast<double>(height) * ratio + 0.5;
            if (w > 8192 || h > 8192) throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "scaled bitmap exceeds decode budget"};
            width = static_cast<int>(w); height = static_cast<int>(h);
            if (!ImageDimensionsSupported(width, height))
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "scaled bitmap dimensions are invalid"};
        }
    }
    auto image = only_bounds ? std::optional<DecodedImage>{} : DecodeImageToArgb(bytes);
    if (!only_bounds && !image) return dx::VmValue::Ref(dx::VmObjectRef{});
    if (opts.IsValid()) {
        api.SetInt(fields[out_width], opts, width); api.SetInt(fields[out_height], opts, height);
        api.SetRef(fields[out_mime], opts, info->mime_type ? call.vm.NewStringUtf8(info->mime_type) : dx::VmObjectRef{});
    }
    if (only_bounds) return dx::VmValue::Ref(dx::VmObjectRef{});
    auto result = ResizeArgbImage(std::move(*image), width, height);
    DexVmAndroidContext::BitmapState state;
    state.width = width; state.height = height;
    if (!std::isfinite(context->ui_density) || context->ui_density <= 0 || context->ui_density > 8192)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "default bitmap density is invalid"};
    state.density = static_cast<int>(std::lround(context->ui_density * 160.0F));
    state.mutable_pixels = opts.IsValid() && api.GetInt(fields[mutable_flag], opts);
    state.pixels = std::make_shared<BitmapPixels>(width, height, state.config, std::move(result.argb));
    const auto object = call.vm.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
    context->bitmaps[object.Value()] = std::move(state);
    return dx::VmValue::Ref(object);
}
} // namespace

Decl Declare_android_graphics_BitmapFactory_Options(const Context&, DecodeFields& fields) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapFactory$Options;");
    constexpr std::array<std::pair<const char*, const char*>, 19> shape{{
        {"inBitmap", "Landroid/graphics/Bitmap;"}, {"inPreferredConfig", "Landroid/graphics/Bitmap$Config;"},
        {"inSampleSize", "I"}, {"inDensity", "I"}, {"inTargetDensity", "I"}, {"inScreenDensity", "I"},
        {"inScaled", "Z"}, {"inJustDecodeBounds", "Z"}, {"inMutable", "Z"}, {"inPremultiplied", "Z"},
        {"inDither", "Z"}, {"inPurgeable", "Z"}, {"inInputShareable", "Z"}, {"inPreferQualityOverSpeed", "Z"},
        {"mCancel", "Z"}, {"outWidth", "I"}, {"outHeight", "I"}, {"outMimeType", "Ljava/lang/String;"}, {"inTempStorage", "[B"}}};
    for (const auto& [name, type] : shape) fields.push_back(builder.BoundInstanceField(name, type, dx::kAccPublic));
    // Java mCancel is authoritative. Serialized synchronous decode has no
    // separately registered Skia job; cancellation remains best effort.
    builder.DirectMethod("requestCancel", "()V", [fields](dx::IntrinsicContext& call) {
        dx::IntrinsicCall(call).SetInt(fields[cancel], call.receiver, 1);
        return dx::VmValue::Void();
    }, dx::kAccPrivate | dx::kAccNative);
    return std::move(builder).Build();
}

Decl Declare_android_graphics_BitmapFactory(const Context& context, const DecodeFields& fields) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapFactory;");
    builder.StaticMethod("nativeValidateOptions", "(Landroid/graphics/BitmapFactory$Options;)V",
        [fields](dx::IntrinsicContext& call) { ValidateDecodeOptions(call, fields, call.arguments[0].ref); return dx::VmValue::Void(); },
        dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod("nativeDecodeByteArray", "([BIILandroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context, fields](dx::IntrinsicContext& call) {
            const auto opts = call.arguments[3].ref;
            ValidateDecodeOptions(call, fields, opts); ResetDecodeOutput(call, fields, opts);
            const auto array = dx::IntrinsicCall(call).NonNullRef(0, "data");
            const auto offset = call.arguments[1].AsInt(), length = call.arguments[2].AsInt();
            if (offset < 0 || length < 0 || static_cast<std::int64_t>(offset) + length > call.vm.Model().ArrayLength(array))
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "bitmap byte range is invalid"};
            if (static_cast<std::size_t>(length) > kMaximumImageEncodedBytes)
                throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "encoded bitmap exceeds decode budget"};
            return DecodeBitmapBytes(call, context, fields, call.vm.Model().ReadByteRegion(array, offset, length), opts);
        }, dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    builder.StaticMethod("nativeDecodeStream", "(Ljava/io/InputStream;Landroid/graphics/Rect;Landroid/graphics/BitmapFactory$Options;)Landroid/graphics/Bitmap;",
        [context, fields](dx::IntrinsicContext& call) {
            const auto stream = dx::IntrinsicCall(call).NonNullRef(0, "stream");
            const auto opts = call.arguments[2].ref;
            ValidateDecodeOptions(call, fields, opts); ResetDecodeOutput(call, fields, opts);
            if (call.arguments[1].ref.IsValid())
                static_cast<void>(CallAndroidMethod(call.vm, call.arguments[1].ref, "set", "(IIII)V",
                    {dx::VmValue::Int(-1), dx::VmValue::Int(-1), dx::VmValue::Int(-1), dx::VmValue::Int(-1)}));
            const auto buffer = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4096);
            const auto root = call.vm.ProtectReferences(std::array{buffer});
            std::vector<std::byte> bytes;
            unsigned stalled{};
            while (!Cancelled(call, fields, opts)) {
                const auto count = CallAndroidMethod(call.vm, stream, "read", "([BII)I",
                    {dx::VmValue::Ref(buffer), dx::VmValue::Int(0), dx::VmValue::Int(4096)}).AsInt();
                if (count < 0) break;
                if (count > 4096) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "bitmap stream returned an invalid byte count"};
                if (!count) { if (++stalled > 8) DecodeUnsupported(call, "bitmap stream made no progress"); continue; }
                stalled = 0;
                if (bytes.size() + static_cast<std::size_t>(count) > kMaximumImageEncodedBytes)
                    throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "bitmap stream exceeds decode budget"};
                const auto chunk = call.vm.Model().ReadByteRegion(buffer, 0, count);
                bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            }
            return DecodeBitmapBytes(call, context, fields, bytes, opts);
        }, dx::kAccPrivate | dx::kAccStatic | dx::kAccNative);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Canvas.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Canvas(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Canvas;", "Ljava/lang/Object;");
    builder.VirtualMethod("save", "(I)I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.VirtualMethod("restore", "()V", GraphicsNoopHandler());
    builder.VirtualMethod("clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.FinalMethod("getClipBounds", "()Landroid/graphics/Rect;",
        [context](dx::IntrinsicContext& call) {
            const auto rect =
                call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
            const auto slots = call.vm.Model().InstanceSlots(rect);
            slots[0] = {0, dx::SlotTag::cat1};
            slots[1] = {0, dx::SlotTag::cat1};
            slots[2] = {context->surface_width, dx::SlotTag::cat1};
            slots[3] = {context->surface_height, dx::SlotTag::cat1};
            return dx::VmValue::Ref(rect);
        });
    builder.VirtualMethod("drawColor", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto found = context->canvases.find(call.receiver.Value());
        if (found == context->canvases.end() || !found->second.locked) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas is not locked"};
        }
        std::fill(found->second.argb.begin(), found->second.argb.end(),
                  static_cast<std::uint32_t>(call.arguments[0].AsInt()));
        return dx::VmValue::Void();
    });
    builder.VirtualMethod(
        "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", [context](dx::IntrinsicContext& call) {
            const auto canvas = context->canvases.find(call.receiver.Value());
            const auto bitmap = context->bitmaps.find(call.arguments[0].ref.Value());
            if (canvas == context->canvases.end() || !canvas->second.locked) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                      "Canvas is not locked"};
            }
            if (!call.arguments[0].ref.IsValid() ||
                bitmap == context->bitmaps.end() || bitmap->second.recycled) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "drawBitmap source is invalid"};
            }
            const auto left_value = call.arguments[1].AsFloat();
            const auto top_value = call.arguments[2].AsFloat();
            if (!std::isfinite(left_value) || !std::isfinite(top_value)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "drawBitmap coordinates are not finite"};
            }
            const auto left = static_cast<std::int32_t>(std::floor(left_value));
            const auto top = static_cast<std::int32_t>(std::floor(top_value));
            auto& target = canvas->second;
            const auto& source = bitmap->second;
            for (std::int32_t y = 0; y < source.height; ++y) {
                const auto destination_y = top + y;
                if (destination_y < 0 ||
                    destination_y >= static_cast<std::int32_t>(target.height)) {
                    continue;
                }
                for (std::int32_t x = 0; x < source.width; ++x) {
                    const auto destination_x = left + x;
                    if (destination_x < 0 || destination_x >=
                        static_cast<std::int32_t>(target.width)) {
                        continue;
                    }
                    target.argb[static_cast<std::size_t>(destination_y) *
                                    target.width + static_cast<std::size_t>(destination_x)] =
                        source.pixels->Get(static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
                                           static_cast<std::size_t>(x));
                }
            }
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", [](dx::IntrinsicContext& c) {
        return CallAndroidMethod(c.vm, c.receiver, "drawBitmap", "([IIIFFIIZLandroid/graphics/Paint;)V",
                                 {c.arguments[0], c.arguments[1], c.arguments[2],
                                  dx::VmValue::Float(static_cast<float>(c.arguments[3].AsInt())),
                                  dx::VmValue::Float(static_cast<float>(c.arguments[4].AsInt())), c.arguments[5],
                                  c.arguments[6], c.arguments[7], c.arguments[8]});
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_drawable_Drawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/Drawable;", "Ljava/lang/Object;");
    const auto dimension = [context](const bool width) {
        return dx::IntrinsicHandler([context, width](dx::IntrinsicContext& call) {
            const auto found = context->ui_drawables.find(call.receiver.Value());
            if (found == context->ui_drawables.end() || found->second.color) return dx::VmValue::Int(-1);
            if (found->second.bitmap.IsValid()) {
                const auto bitmap = context->bitmaps.find(found->second.bitmap.Value());
                if (bitmap == context->bitmaps.end() || bitmap->second.recycled)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Drawable bitmap is recycled"};
                return dx::VmValue::Int(width ? bitmap->second.width : bitmap->second.height);
            }
            if (found->second.resource_id != 0) {
                const auto bitmap = ResolveUiDrawable(*context, found->second.resource_id);
                return dx::VmValue::Int(width ? bitmap->width : bitmap->height);
            }
            return dx::VmValue::Int(-1);
        });
    };
    builder.VirtualMethod("getIntrinsicWidth", "()I", dimension(true));
    builder.VirtualMethod("getIntrinsicHeight", "()I", dimension(false));
    builder.VirtualMethod("setAlpha", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto alpha = call.arguments[0].AsInt();
        if (alpha < 0 || alpha > 255) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "Drawable alpha is outside 0..255"};
        }
        auto& state = context->ui_drawables[call.receiver.Value()];
        state.alpha = static_cast<std::uint8_t>(alpha);
        const auto still_bound = [&]() {
            if (!state.callback_node.has_value() ||
                context->ui_tree.Get(*state.callback_node) == nullptr) {
                return false;
            }
            const auto view = context->ui_node_to_object.find(*state.callback_node);
            if (view == context->ui_node_to_object.end()) return false;
            const auto current = context->ui_view_backgrounds.find(view->second.Value());
            return current != context->ui_view_backgrounds.end() &&
                   current->second == call.receiver;
        }();
        if (still_bound) {
            context->ui_tree.Get(*state.callback_node)->background_alpha =
                static_cast<float>(alpha) / 255.0F;
            context->ui_tree.MarkDrawDirty(*state.callback_node);
        }
        RefreshAndroidImageDrawables(*context);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setBounds", "(IIII)V", [context](dx::IntrinsicContext& call) {
        const ui::Rect bounds{call.arguments[0].AsInt(), call.arguments[1].AsInt(),
                              call.arguments[2].AsInt(), call.arguments[3].AsInt()};
        if (bounds.right < bounds.left || bounds.bottom < bounds.top) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "Drawable bounds are inverted"};
        }
        auto& state = context->ui_drawables[call.receiver.Value()];
        state.bounds = bounds;
        const auto still_bound = [&]() {
            if (!state.callback_node.has_value() ||
                context->ui_tree.Get(*state.callback_node) == nullptr) {
                return false;
            }
            const auto view = context->ui_node_to_object.find(*state.callback_node);
            if (view == context->ui_node_to_object.end()) return false;
            const auto current = context->ui_view_backgrounds.find(view->second.Value());
            return current != context->ui_view_backgrounds.end() &&
                   current->second == call.receiver;
        }();
        if (still_bound) {
            context->ui_tree.MarkDrawDirty(*state.callback_node);
        }
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 165.
    builder.VirtualMethod(
        "setBounds", "(Landroid/graphics/Rect;)V",
        [](dx::IntrinsicContext& c) {
            const auto rect = c.arguments[0].ref;
            if (!rect.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "bounds == null"};
            std::vector<dx::VmValue> args;
            for (const auto name : {"left", "top", "right", "bottom"}) {
                const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(rect), name, "I");
                args.push_back(dx::VmValue::Int(static_cast<std::int32_t>(
                    c.vm.Model().InstanceSlots(rect)[c.vm.Linker().Field(*field).slot].bits)));
            }
            return CallAndroidMethod(c.vm, c.receiver, "setBounds", "(IIII)V", args);
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_drawable_PaintDrawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/PaintDrawable;", "Landroid/graphics/drawable/Drawable;");
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.VirtualMethod("setCornerRadius", "(F)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Matrix.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Matrix(const Context& context) {
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Matrix;", "Ljava/lang/Object;");
        builder.Constructor("()V", [context](dx::IntrinsicContext& c) {
            context->identity_matrices.insert(c.receiver.Value());
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("isIdentity", "()Z", [context](dx::IntrinsicContext& c) {
            if (!context->identity_matrices.contains(c.receiver.Value()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Matrix requires construction"};
            return dx::VmValue::Int(1);
        });
        builder.VirtualMethod("getValues", "([F)V", [context](dx::IntrinsicContext& c) {
            if (!context->identity_matrices.contains(c.receiver.Value()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Matrix requires construction"};
            const auto array = c.arguments[0].ref;
            if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Matrix values"};
            if (c.vm.Model().ArrayLength(array) < 9)
                throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "Matrix values length"};
            for (int i = 0; i < 9; ++i)
                c.vm.Model().SetPrimitiveElement(array, i, dx::VmValue::Float(i % 4 == 0 ? 1.0F : 0.0F).cat1);
            return dx::VmValue::Void();
        });
        builder.VirtualMethod(
            "finalize", "()V",
            [context](dx::IntrinsicContext& c) {
                context->identity_matrices.erase(c.receiver.Value());
                const auto object = c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;");
                const auto index = c.vm.Linker().FindVtableIndex(object, "finalize", "()V");
                const auto out =
                    c.vm.Call(c.vm.Linker().Class(object).vtable.at(*index), std::array{dx::VmValue::Ref(c.receiver)});
                if (out.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                          out.exception};
                return out.value;
            },
            dx::kAccProtected);
        return std::move(builder).Build();
    }
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Matrix;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Paint.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
[[noreturn]] void UnsupportedPaint(dx::IntrinsicContext& call, const std::string& detail) {
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.paint_measure", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", detail};
}
}  // namespace

Decl Declare_android_graphics_Paint(const Context& context) {
    if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
        auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;", "Ljava/lang/Object;");
        const auto face = builder.BoundInstanceField("mTypeface", "Landroid/graphics/Typeface;", dx::kAccPrivate);
        const auto state = [context](dx::IntrinsicContext& call) -> DexVmAndroidContext::PaintMeasureState& {
            const auto found = context->paint_measure_states.find(call.receiver.Value());
            if (found == context->paint_measure_states.end())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Paint measurement requires construction"};
            return found->second;
        };
        const auto flags = [](dx::IntrinsicContext& call, std::int32_t value) {
            // Only AA and the two API19 default text flags have defined semantics here.
            if ((value & ~0x501) != 0) UnsupportedPaint(call, "unsupported Paint flags");
            return value;
        };
        const auto init = [context, face, flags](dx::IntrinsicContext& call) {
            DexVmAndroidContext::PaintMeasureState value;
            value.flags = flags(call, (call.arguments.empty() ? 0 : call.arguments[0].AsInt()) | 0x500);
            context->paint_measure_states.insert_or_assign(call.receiver.Value(), std::move(value));
            dx::IntrinsicCall(call).SetRef(face, dx::VmObjectRef{});
            return dx::VmValue::Void();
        };
        builder.Constructor("()V", init).Constructor("(I)V", init);
        builder.VirtualMethod(
            "finalize", "()V",
            [context](dx::IntrinsicContext& c) {
                context->paint_measure_states.erase(c.receiver.Value());
                const auto object = c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;");
                const auto index = c.vm.Linker().FindVtableIndex(object, "finalize", "()V");
                if (!index) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "Object.finalize missing");
                const auto outcome =
                    c.vm.Call(c.vm.Linker().Class(object).vtable[*index], std::array{dx::VmValue::Ref(c.receiver)});
                if (outcome.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(outcome.exception_class).descriptor,
                                          outcome.exception_message, outcome.exception};
                return outcome.value;
            },
            dx::kAccProtected);
        builder.VirtualMethod("getFlags", "()I",
                              [state](dx::IntrinsicContext& c) { return dx::VmValue::Int(state(c).flags); });
        builder.VirtualMethod("setFlags", "(I)V", [state, flags](dx::IntrinsicContext& c) {
            state(c).flags = flags(c, c.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
        builder.FinalMethod("isAntiAlias", "()Z",
                            [state](dx::IntrinsicContext& c) { return dx::VmValue::Int(state(c).flags & 1); });
        builder.VirtualMethod("setAntiAlias", "(Z)V", [state](dx::IntrinsicContext& c) {
            auto& value = state(c);
            value.flags = (value.flags & ~1) | (c.arguments[0].AsInt() != 0);
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("getTextSize", "()F",
                              [state](dx::IntrinsicContext& c) { return dx::VmValue::Float(state(c).text_size); });
        builder.VirtualMethod("setTextSize", "(F)V", [state](dx::IntrinsicContext& c) {
            const auto size = c.arguments[0].AsFloat();
            if (!std::isfinite(size) || size < 1 || size > 128)
                UnsupportedPaint(c, "Paint text size outside 1..128 px");
            state(c).text_size = size;
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("getTypeface", "()Landroid/graphics/Typeface;", [face, state](dx::IntrinsicContext& c) {
            static_cast<void>(state(c));
            return dx::VmValue::Ref(dx::IntrinsicCall(c).GetRef(face));
        });
        builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
                              [face, state](dx::IntrinsicContext& c) {
                                  static_cast<void>(state(c));
                                  const auto ref = c.arguments[0].ref;
                                  if (ref.IsValid()) static_cast<void>(CallAndroidMethod(c.vm, ref, "getStyle", "()I"));
                                  dx::IntrinsicCall(c).SetRef(face, ref);
                                  return dx::VmValue::Ref(ref);
                              });
        const auto measure = [context, state, face](int kind) {
            return dx::IntrinsicHandler([context, state, face, kind](dx::IntrinsicContext& c) {
                auto& value = state(c);
                const auto ref = c.arguments[0].ref;
                if (!ref.IsValid())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "text cannot be null"};
                std::int32_t length{};
                const bool chars = kind == 2;
                if (chars)
                    length = c.vm.Model().ArrayLength(ref);
                else if (kind == 3)
                    length = CallAndroidMethod(c.vm, ref, "length", "()I").AsInt();
                else
                    length = c.vm.Model().StringLength(ref);
                const auto start = kind == 0 ? 0 : c.arguments[1].AsInt();
                const std::int64_t end = kind == 0 ? length
                                         : chars   ? static_cast<std::int64_t>(start) + c.arguments[2].AsInt()
                                                   : c.arguments[2].AsInt();
                if (start < 0 || end < start || end > length)
                    throw dx::VmJavaThrow{chars ? "Ljava/lang/ArrayIndexOutOfBoundsException;"
                                                : "Ljava/lang/IndexOutOfBoundsException;",
                                          "Paint text range"};
                if (end - start > 1024) UnsupportedPaint(c, "Paint text exceeds 1024 units");
                std::u16string text;
                if (chars) {
                    for (auto i = start; i < end; ++i)
                        text.push_back(static_cast<char16_t>(c.vm.Model().GetPrimitiveElement(ref, i)));
                } else if (kind == 3) {
                    for (auto i = start; i < end; ++i)
                        text.push_back(static_cast<char16_t>(
                            CallAndroidMethod(c.vm, ref, "charAt", "(I)C", {dx::VmValue::Int(i)}).AsInt()));
                } else
                    text = c.vm.Model().StringRegion(ref, start, static_cast<JniSize>(end - start));
                // No shaping, bidi, combining marks or multi-line layout in this seam.
                for (const auto unit : text)
                    if (!((unit >= 0x20 && unit <= 0x7e) || (unit >= 0x4e00 && unit <= 0x9fff)))
                        UnsupportedPaint(c, "unsupported Paint glyph or shaping");
                const auto typeface = dx::IntrinsicCall(c).GetRef(face);
                const auto style =
                    typeface.IsValid() ? CallAndroidMethod(c.vm, typeface, "getStyle", "()I").AsInt() : 0;
                float width{};
                try {
                    width = static_cast<float>(ui::MeasureFixedText(text, value.text_size,
                                                                    static_cast<std::uint32_t>(style),
                                                                    context->ui_tree.FallbackFont().get())
                                                   .width);
                } catch (const std::runtime_error& error) {
                    UnsupportedPaint(c, error.what());
                }
                ++value.measurements;
                value.last_text = text;
                value.last_width = width;
                return dx::VmValue::Float(width);
            });
        };
        builder.VirtualMethod("measureText", "(Ljava/lang/String;)F", measure(0));
        builder.VirtualMethod("measureText", "(Ljava/lang/String;II)F", measure(1));
        builder.VirtualMethod("measureText", "([CII)F", measure(2));
        builder.VirtualMethod("measureText", "(Ljava/lang/CharSequence;II)F", measure(3));
        return std::move(builder).Build();
    }
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;", "Ljava/lang/Object;");
    builder.Constructor("()V", GraphicsNoopHandler());
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.VirtualMethod("setColor", "(I)V", GraphicsNoopHandler());
    builder.VirtualMethod("setAntiAlias", "(Z)V", GraphicsNoopHandler());
    builder.VirtualMethod("setTextSize", "(F)V", GraphicsNoopHandler());
    builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
                          [](dx::IntrinsicContext& call) {
                              // Returns the typeface that was set, per the platform contract.
                              return dx::VmValue::Ref(call.arguments[0].ref);
                          });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_graphics_Region_Op.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicEnumBuilder(
               "Landroid/graphics/Region$Op;", {"REPLACE"})
        .Build();
}

}  // namespace ogplay::runtime::android_intrinsics


namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] std::uint32_t ParseColorValue(const std::string& value) {
    if (!value.empty() && value.front() == '#') {
        const auto digits = value.substr(1);
        if (digits.size() != 6U && digits.size() != 8U) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
        }
        std::uint32_t parsed{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
        if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
        return digits.size() == 6U ? (0xff000000U | parsed) : parsed;
    }
    auto normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::array names{
        std::pair{"black", 0xff000000U}, std::pair{"darkgray", 0xff444444U},
        std::pair{"gray", 0xff888888U}, std::pair{"lightgray", 0xffccccccU},
        std::pair{"white", 0xffffffffU}, std::pair{"red", 0xffff0000U},
        std::pair{"green", 0xff00ff00U}, std::pair{"blue", 0xff0000ffU},
        std::pair{"yellow", 0xffffff00U}, std::pair{"cyan", 0xff00ffffU},
        std::pair{"magenta", 0xffff00ffU}, std::pair{"transparent", 0x00000000U}};
    const auto found = std::find_if(names.begin(), names.end(), [&normalized](const auto& item) {
        return item.first == normalized;
    });
    if (found == names.end())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "Unknown color"};
    return found->second;
}

[[nodiscard]] dx::VmValue FloatBits(const float value) {
    return dx::VmValue::Float(value);
}

}  // namespace

Decl Declare_android_graphics_Color(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Color;", "Ljava/lang/Object;");
    for (const auto [name, value] : std::array{
             std::pair{"BLACK", 0xff000000U}, std::pair{"DKGRAY", 0xff444444U},
             std::pair{"GRAY", 0xff888888U}, std::pair{"LTGRAY", 0xffccccccU},
             std::pair{"WHITE", 0xffffffffU}, std::pair{"RED", 0xffff0000U},
             std::pair{"GREEN", 0xff00ff00U}, std::pair{"BLUE", 0xff0000ffU},
             std::pair{"YELLOW", 0xffffff00U}, std::pair{"CYAN", 0xff00ffffU},
             std::pair{"MAGENTA", 0xffff00ffU}, std::pair{"TRANSPARENT", 0x00000000U}}) {
        builder.ConstantInt(
            name, "I", static_cast<std::int32_t>(value),
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    }
    builder.StaticMethod("alpha", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 24U);
    }).StaticMethod("red", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int((static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 16U) & 0xffU);
    }).StaticMethod("green", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int((static_cast<std::uint32_t>(call.arguments[0].AsInt()) >> 8U) & 0xffU);
    }).StaticMethod("blue", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::uint32_t>(call.arguments[0].AsInt()) & 0xffU);
    }).StaticMethod("rgb", "(III)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(0xff000000U |
            ((static_cast<std::uint32_t>(call.arguments[0].AsInt()) & 0xffU) << 16U) |
            ((static_cast<std::uint32_t>(call.arguments[1].AsInt()) & 0xffU) << 8U) |
            (static_cast<std::uint32_t>(call.arguments[2].AsInt()) & 0xffU)));
    }).StaticMethod("argb", "(IIII)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            ((static_cast<std::uint32_t>(call.arguments[0].AsInt()) & 0xffU) << 24U) |
            ((static_cast<std::uint32_t>(call.arguments[1].AsInt()) & 0xffU) << 16U) |
            ((static_cast<std::uint32_t>(call.arguments[2].AsInt()) & 0xffU) << 8U) |
            (static_cast<std::uint32_t>(call.arguments[3].AsInt()) & 0xffU)));
    }).StaticMethod("parseColor", "(Ljava/lang/String;)I", [](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "colorString"};
        return dx::VmValue::Int(static_cast<std::int32_t>(
            ParseColorValue(call.vm.StringUtf8(call.arguments[0].ref))));
    });
    // AOSP 4.4 ROM line 37.
    builder.Constructor(
        "()V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

Decl Declare_android_graphics_RectF(const Context& context) {
    static_cast<void>(context);
    auto builder =
        dx::IntrinsicClassBuilder::Class("Landroid/graphics/RectF;", "Ljava/lang/Object;", {"Landroid/os/Parcelable;"});

    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        for (std::size_t index = 0; index < 4U; ++index)
            slots[index] = {std::bit_cast<std::uint32_t>(call.arguments[index].AsFloat()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(FFFF)V", set).VirtualMethod("set", "(FFFF)V", set);
    builder
        .FinalMethod("width", "()F",
                     [](dx::IntrinsicContext& call) {
                         const auto slots = call.vm.Model().InstanceSlots(call.receiver);
                         return FloatBits(std::bit_cast<float>(slots[2].bits) - std::bit_cast<float>(slots[0].bits));
                     })
        .FinalMethod("height", "()F",
                     [](dx::IntrinsicContext& call) {
                         const auto slots = call.vm.Model().InstanceSlots(call.receiver);
                         return FloatBits(std::bit_cast<float>(slots[3].bits) - std::bit_cast<float>(slots[1].bits));
                     })
        .FinalMethod("centerX", "()F",
                     [](dx::IntrinsicContext& call) {
                         const auto s = call.vm.Model().InstanceSlots(call.receiver);
                         return FloatBits((std::bit_cast<float>(s[0].bits) + std::bit_cast<float>(s[2].bits)) * 0.5F);
                     })
        .FinalMethod("centerY", "()F",
                     [](dx::IntrinsicContext& call) {
                         const auto s = call.vm.Model().InstanceSlots(call.receiver);
                         return FloatBits((std::bit_cast<float>(s[1].bits) + std::bit_cast<float>(s[3].bits)) * 0.5F);
                     })
        .FinalMethod("isEmpty", "()Z",
                     [](dx::IntrinsicContext& call) {
                         const auto s = call.vm.Model().InstanceSlots(call.receiver);
                         return dx::VmValue::Int(std::bit_cast<float>(s[0].bits) >= std::bit_cast<float>(s[2].bits) ||
                                                 std::bit_cast<float>(s[1].bits) >= std::bit_cast<float>(s[3].bits));
                     })
        .VirtualMethod("contains", "(FF)Z",
                       [](dx::IntrinsicContext& call) {
                           const auto s = call.vm.Model().InstanceSlots(call.receiver);
                           const auto x = call.arguments[0].AsFloat();
                           const auto y = call.arguments[1].AsFloat();
                           return dx::VmValue::Int(
                               x >= std::bit_cast<float>(s[0].bits) && x < std::bit_cast<float>(s[2].bits) &&
                               y >= std::bit_cast<float>(s[1].bits) && y < std::bit_cast<float>(s[3].bits));
                       })
        .VirtualMethod("offset", "(FF)V", [](dx::IntrinsicContext& call) {
            const auto s = call.vm.Model().InstanceSlots(call.receiver);
            const auto dxv = call.arguments[0].AsFloat();
            const auto dyv = call.arguments[1].AsFloat();
            for (const auto index : {0U, 2U})
                s[index].bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(s[index].bits) + dxv);
            for (const auto index : {1U, 3U})
                s[index].bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(s[index].bits) + dyv);
            return dx::VmValue::Void();
        });
    const auto wu_left = builder.BoundInstanceField("left", "F");
    const auto wu_top = builder.BoundInstanceField("top", "F");
    const auto wu_right = builder.BoundInstanceField("right", "F");
    const auto wu_bottom = builder.BoundInstanceField("bottom", "F");
    // AOSP 4.4 ROM line 79.
    builder.Constructor(
        "(Landroid/graphics/Rect;)V",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            if (!source.IsValid()) {
                api.SetFloat(wu_left, 0.0F);
                api.SetFloat(wu_top, 0.0F);
                api.SetFloat(wu_right, 0.0F);
                api.SetFloat(wu_bottom, 0.0F);
                return dx::VmValue::Void();
            }
            if (!source.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "source == null"};
            const auto left_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "left", "I");
            api.SetFloat(wu_left, static_cast<float>(static_cast<std::int32_t>(
                                      c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*left_field).slot].bits)));
            const auto top_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "top", "I");
            api.SetFloat(wu_top, static_cast<float>(static_cast<std::int32_t>(
                                     c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*top_field).slot].bits)));
            const auto right_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "right", "I");
            api.SetFloat(wu_right,
                         static_cast<float>(static_cast<std::int32_t>(
                             c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*right_field).slot].bits)));
            const auto bottom_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "bottom", "I");
            api.SetFloat(wu_bottom,
                         static_cast<float>(static_cast<std::int32_t>(
                             c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*bottom_field).slot].bits)));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 68.
    builder.Constructor(
        "(Landroid/graphics/RectF;)V",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            if (!source.IsValid()) {
                api.SetFloat(wu_left, 0.0F);
                api.SetFloat(wu_top, 0.0F);
                api.SetFloat(wu_right, 0.0F);
                api.SetFloat(wu_bottom, 0.0F);
                return dx::VmValue::Void();
            }
            api.SetFloat(wu_left, api.GetFloat(wu_left, source));
            api.SetFloat(wu_top, api.GetFloat(wu_top, source));
            api.SetFloat(wu_right, api.GetFloat(wu_right, source));
            api.SetFloat(wu_bottom, api.GetFloat(wu_bottom, source));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 225.
    builder.VirtualMethod(
        "set", "(Landroid/graphics/Rect;)V",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            if (!source.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "source == null"};
            const auto left_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "left", "I");
            api.SetFloat(wu_left, static_cast<float>(static_cast<std::int32_t>(
                                      c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*left_field).slot].bits)));
            const auto top_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "top", "I");
            api.SetFloat(wu_top, static_cast<float>(static_cast<std::int32_t>(
                                     c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*top_field).slot].bits)));
            const auto right_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "right", "I");
            api.SetFloat(wu_right,
                         static_cast<float>(static_cast<std::int32_t>(
                             c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*right_field).slot].bits)));
            const auto bottom_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "bottom", "I");
            api.SetFloat(wu_bottom,
                         static_cast<float>(static_cast<std::int32_t>(
                             c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*bottom_field).slot].bits)));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 212.
    builder.VirtualMethod(
        "set", "(Landroid/graphics/RectF;)V",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            api.SetFloat(wu_left, api.GetFloat(wu_left, source));
            api.SetFloat(wu_top, api.GetFloat(wu_top, source));
            api.SetFloat(wu_right, api.GetFloat(wu_right, source));
            api.SetFloat(wu_bottom, api.GetFloat(wu_bottom, source));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 305.
    builder.VirtualMethod(
        "contains", "(FFFF)Z",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto left = api.GetFloat(wu_left);
            const auto top = api.GetFloat(wu_top);
            const auto right = api.GetFloat(wu_right);
            const auto bottom = api.GetFloat(wu_bottom);
            return dx::VmValue::Int(left < right && top < bottom && left <= c.arguments[0].AsFloat() &&
                                    top <= c.arguments[1].AsFloat() && right >= c.arguments[2].AsFloat() &&
                                    bottom >= c.arguments[3].AsFloat());
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 321.
    builder.VirtualMethod(
        "contains", "(Landroid/graphics/RectF;)Z",
        [wu_left, wu_top, wu_right, wu_bottom](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto left = api.GetFloat(wu_left);
            const auto top = api.GetFloat(wu_top);
            const auto right = api.GetFloat(wu_right);
            const auto bottom = api.GetFloat(wu_bottom);
            return dx::VmValue::Int(left < right && top < bottom && left <= api.GetFloat(wu_left, c.arguments[0].ref) &&
                                    top <= api.GetFloat(wu_top, c.arguments[0].ref) &&
                                    right >= api.GetFloat(wu_right, c.arguments[0].ref) &&
                                    bottom >= api.GetFloat(wu_bottom, c.arguments[0].ref));
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

Decl Declare_android_graphics_PointF(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PointF;", "Ljava/lang/Object;",
                                                    {"Landroid/os/Parcelable;"});

    builder.Constructor("()V", GraphicsNoopHandler());
    const auto set = [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        s[0] = {std::bit_cast<std::uint32_t>(call.arguments[0].AsFloat()), dx::SlotTag::cat1};
        s[1] = {std::bit_cast<std::uint32_t>(call.arguments[1].AsFloat()), dx::SlotTag::cat1};
        return dx::VmValue::Void();
    };
    builder.Constructor("(FF)V", set).FinalMethod("set", "(FF)V", set);
    builder.FinalMethod("length", "()F", [](dx::IntrinsicContext& call) {
        const auto s = call.vm.Model().InstanceSlots(call.receiver);
        return FloatBits(std::hypot(std::bit_cast<float>(s[0].bits), std::bit_cast<float>(s[1].bits)));
    }).StaticMethod("length", "(FF)F", [](dx::IntrinsicContext& call) {
        return FloatBits(std::hypot(call.arguments[0].AsFloat(), call.arguments[1].AsFloat()));
    });
    const auto wu_x = builder.BoundInstanceField("x", "F");
    const auto wu_y = builder.BoundInstanceField("y", "F");
    // AOSP 4.4 ROM line 38.
    builder.Constructor(
        "(Landroid/graphics/Point;)V",
        [wu_x, wu_y](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            if (!source.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "source == null"};
            const auto x_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "x", "I");
            api.SetFloat(wu_x, static_cast<float>(static_cast<std::int32_t>(
                                   c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*x_field).slot].bits)));
            const auto y_field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(source), "y", "I");
            api.SetFloat(wu_y, static_cast<float>(static_cast<std::int32_t>(
                                   c.vm.Model().InstanceSlots(source)[c.vm.Linker().Field(*y_field).slot].bits)));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    // AOSP 4.4 ROM line 55.
    builder.FinalMethod(
        "set", "(Landroid/graphics/PointF;)V",
        [wu_x, wu_y](dx::IntrinsicContext& c) {
            dx::IntrinsicCall api(c);
            const auto source = c.arguments[0].ref;
            api.SetFloat(wu_x, api.GetFloat(wu_x, source));
            api.SetFloat(wu_y, api.GetFloat(wu_y, source));
            return dx::VmValue::Void();
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

Decl Declare_android_graphics_Path_Direction(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicEnumBuilder enum_builder(
        "Landroid/graphics/Path$Direction;", {"CW", "CCW"});
    auto& builder = enum_builder.ClassBuilder();
    const auto native_int = builder.BoundInstanceField(
        "nativeInt", "I", dx::kAccPublic | dx::kAccFinal);
    builder.Constructor("(I)V", [native_int](dx::IntrinsicContext& context) {
        const dx::IntrinsicCall call(context);
        call.SetInt(native_int, call.Int(0));
        return dx::VmValue::Void();
    }, dx::kAccPrivate);
    enum_builder.WithConstantInitializer(
        [native_int](dx::IntrinsicContext& context,
                     const dx::VmObjectRef value, std::string_view,
                     const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value, ordinal);
        });
    return std::move(enum_builder).Build();
}

Decl Declare_android_graphics_Path(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Path;", "Ljava/lang/Object;");
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        context->paths.try_emplace(call.receiver.Value());
        return dx::VmValue::Void();
    });
    builder.Constructor("(Landroid/graphics/Path;)V", [context](dx::IntrinsicContext& call) {
        if (!call.arguments[0].ref.IsValid())
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "src"};
        context->paths[call.receiver.Value()] = context->paths[call.arguments[0].ref.Value()];
        return dx::VmValue::Void();
    });
    builder
        .VirtualMethod("reset", "()V",
                       [context](dx::IntrinsicContext& call) {
                           context->paths[call.receiver.Value()].commands.clear();
                           return dx::VmValue::Void();
                       })
        .VirtualMethod("rewind", "()V",
                       [context](dx::IntrinsicContext& call) {
                           context->paths[call.receiver.Value()].commands.clear();
                           return dx::VmValue::Void();
                       })
        .VirtualMethod("isEmpty", "()Z", [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->paths[call.receiver.Value()].commands.empty());
        });
    const auto point = [context](const auto verb) {
        return dx::IntrinsicHandler([context, verb](dx::IntrinsicContext& call) {
            context->paths[call.receiver.Value()].commands.push_back(
                {verb, call.arguments[0].AsFloat(), call.arguments[1].AsFloat()});
            return dx::VmValue::Void();
        });
    };
    builder.VirtualMethod("moveTo", "(FF)V", point(DexVmAndroidContext::PathState::Verb::move))
        .VirtualMethod("lineTo", "(FF)V", point(DexVmAndroidContext::PathState::Verb::line));
    builder
        .VirtualMethod("close", "()V",
                       [context](dx::IntrinsicContext& call) {
                           context->paths[call.receiver.Value()].commands.push_back(
                               {DexVmAndroidContext::PathState::Verb::close});
                           return dx::VmValue::Void();
                       })
        .VirtualMethod("addRect", "(FFFFLandroid/graphics/Path$Direction;)V", [context](dx::IntrinsicContext& call) {
            if (!call.arguments[4].ref.IsValid())
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "direction"};
            const auto direction =
                ReadIntField(call.vm, call.arguments[4].ref, "nativeInt");
            context->paths[call.receiver.Value()].commands.push_back({
                DexVmAndroidContext::PathState::Verb::rect,
                call.arguments[0].AsFloat(), call.arguments[1].AsFloat(),
                call.arguments[2].AsFloat(), call.arguments[3].AsFloat(), direction});
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_PorterDuff_Mode(const Context& context) {
    static_cast<void>(context);
    constexpr std::array modes{
        std::pair{"CLEAR", 0}, std::pair{"SRC", 1}, std::pair{"DST", 2},
        std::pair{"SRC_OVER", 3}, std::pair{"DST_OVER", 4},
        std::pair{"SRC_IN", 5}, std::pair{"DST_IN", 6}, std::pair{"SRC_OUT", 7},
        std::pair{"DST_OUT", 8}, std::pair{"SRC_ATOP", 9}, std::pair{"DST_ATOP", 10},
        std::pair{"XOR", 11}, std::pair{"DARKEN", 12}, std::pair{"LIGHTEN", 13},
        std::pair{"MULTIPLY", 14}, std::pair{"SCREEN", 15}, std::pair{"ADD", 16},
        std::pair{"OVERLAY", 17}};
    dx::IntrinsicEnumBuilder enum_builder(
        "Landroid/graphics/PorterDuff$Mode;",
        {"CLEAR", "SRC", "DST", "SRC_OVER", "DST_OVER", "SRC_IN",
         "DST_IN", "SRC_OUT", "DST_OUT", "SRC_ATOP", "DST_ATOP", "XOR",
         "DARKEN", "LIGHTEN", "MULTIPLY", "SCREEN", "ADD", "OVERLAY"});
    const auto native_int = enum_builder.ClassBuilder().BoundInstanceField(
        "nativeInt", "I", dx::kAccPublic | dx::kAccFinal);
    enum_builder.WithConstantInitializer(
        [native_int, modes](dx::IntrinsicContext& context,
                            const dx::VmObjectRef value, std::string_view,
                            const std::int32_t ordinal) {
            const dx::IntrinsicCall call(context);
            call.SetInt(native_int, value,
                        modes[static_cast<std::size_t>(ordinal)].second);
        });
    return std::move(enum_builder).Build();
}

Decl Declare_android_graphics_PorterDuff(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicClassBuilder::Class("Landroid/graphics/PorterDuff;", "Ljava/lang/Object;").Build();
}

Decl Declare_android_graphics_drawable_BitmapDrawable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/graphics/drawable/BitmapDrawable;", "Landroid/graphics/drawable/Drawable;");
    const bool leaf = context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal;
    if (!leaf) builder.InstanceField("bitmap", "Landroid/graphics/Bitmap;", dx::kAccPrivate | dx::kAccFinal);
    const auto bitmap_ctor = [context](const std::size_t index) {
        return dx::IntrinsicHandler([context, index](dx::IntrinsicContext& call) {
            const auto bitmap = call.arguments[index].ref;
            if (bitmap.IsValid() && !context->bitmaps.contains(bitmap.Value()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "BitmapDrawable requires a Bitmap backing"};
            if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
                const auto ctor = call.vm.Linker().FindDirectMethod(
                    call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "<init>", "()V");
                const auto result = call.vm.Call(*ctor, std::array{dx::VmValue::Ref(call.receiver)});
                if (result.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                          result.exception_message, result.exception};
                const auto write = [&](const char* name, const char* type, std::uint32_t bits,
                                       dx::SlotTag tag = dx::SlotTag::cat1) {
                    const auto field =
                        call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(call.receiver), name, type);
                    if (!field) throw std::logic_error("missing ROM BitmapDrawable field");
                    call.vm.Model().InstanceSlots(call.receiver)[call.vm.Linker().Field(*field).slot] = {bits, tag};
                };
                write("mBitmap", "Landroid/graphics/Bitmap;", bitmap.Value(), dx::SlotTag::ref);
                std::int32_t target = 160;
                if (index == 1 && call.arguments[0].ref.IsValid()) {
                    const auto metrics = CallAndroidMethod(call.vm, call.arguments[0].ref, "getDisplayMetrics",
                                                           "()Landroid/util/DisplayMetrics;")
                                             .ref;
                    target = ReadIntField(call.vm, metrics, "densityDpi");
                    if (target == 0) target = 160;
                }
                write("mTargetDensity", "I", static_cast<std::uint32_t>(target));
                const auto dimension = [&](bool width) {
                    if (!bitmap.IsValid()) return -1;
                    const auto& state = context->bitmaps.at(bitmap.Value());
                    const auto size = width ? state.width : state.height;
                    return state.density == 0 || state.density == target
                               ? size
                               : static_cast<std::int32_t>(
                                     (static_cast<std::int64_t>(size) * target + (state.density >> 1)) / state.density);
                };
                write("mBitmapWidth", "I", static_cast<std::uint32_t>(dimension(true)));
                write("mBitmapHeight", "I", static_cast<std::uint32_t>(dimension(false)));
            } else {
                call.vm.Model().InstanceSlots(call.receiver)[0] = {bitmap.Value(), dx::SlotTag::ref};
            }
            if (context->ui_traversal_owner != AndroidUiTraversalOwner::JavaTraversal)
                context->ui_drawables[call.receiver.Value()].bitmap = bitmap;
            context->ui_drawables[call.receiver.Value()].bitmap_drawable = true;
            return dx::VmValue::Void();
        });
    };
    builder.Constructor("(Landroid/graphics/Bitmap;)V", bitmap_ctor(0))
        .Constructor("(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V", bitmap_ctor(1));
    builder.FinalMethod("getBitmap", "()Landroid/graphics/Bitmap;", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(dx::VmObjectRef(call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    return std::move(builder).Build();
}

Decl Declare_android_graphics_drawable_ColorDrawable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/graphics/drawable/ColorDrawable;", "Landroid/graphics/drawable/Drawable;");
    const bool leaf = context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal;
    if (!leaf) builder.InstanceField("color", "I", dx::kAccPrivate);
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
            const auto ctor = call.vm.Linker().FindDirectMethod(
                call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "<init>", "()V");
            const auto result = call.vm.Call(*ctor, std::array{dx::VmValue::Ref(call.receiver)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                      result.exception_message, result.exception};
        }
        context->ui_drawables[call.receiver.Value()].color = 0;
        return dx::VmValue::Void();
    });
    const auto set = [context](dx::IntrinsicContext& call) {
        if (context->ui_traversal_owner != AndroidUiTraversalOwner::JavaTraversal)
            call.vm.Model().InstanceSlots(call.receiver)[0] = {static_cast<std::uint32_t>(call.arguments[0].AsInt()),
                                                               dx::SlotTag::cat1};
        context->ui_drawables[call.receiver.Value()].color = call.arguments[0].cat1;
        RefreshAndroidImageDrawables(*context);
        return dx::VmValue::Void();
    };
    builder
        .Constructor("(I)V",
                     [context](dx::IntrinsicContext& call) {
                         if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal) {
                             const auto ctor = call.vm.Linker().FindDirectMethod(
                                 call.vm.Model().ObjectClass(call.receiver), "<init>", "()V");
                             const auto result = call.vm.Call(*ctor, std::array{dx::VmValue::Ref(call.receiver)});
                             if (result.exception.IsValid())
                                 throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                                       result.exception_message, result.exception};
                         }
                         return CallAndroidMethod(call.vm, call.receiver, "setColor", "(I)V", {call.arguments[0]});
                     })
        .VirtualMethod("setColor", "(I)V", set);
    builder.VirtualMethod("getColor", "()I", [context](dx::IntrinsicContext& call) {
        if (context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal)
            return dx::VmValue::Int(static_cast<std::int32_t>(*context->ui_drawables.at(call.receiver.Value()).color));
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Typeface.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
// Immutable descriptors of the built-in bitmap-font backend, not guest pointers.
// Families retain identity; rendering uses the documented built-in fallback face.
int TypefaceToken(int family, int style) {
    if (style < 0 || style > 3) style = 0;
    return 1 + family * 4 + style;
}
int CheckTypefaceToken(int token) {
    if (token < 1 || token > 16)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid typeface descriptor"};
    return token - 1;
}
}  // namespace

Decl Declare_android_graphics_Typeface(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Typeface;");
    constexpr auto flags = dx::kAccPrivate | dx::kAccNative;
    builder.StaticMethod("nativeCreate", "(Ljava/lang/String;I)I", [](dx::IntrinsicContext& call) {
        const auto name = call.arguments[0].ref.IsValid()
            ? call.vm.StringUtf8(call.arguments[0].ref) : std::string{};
        // API19 unknown family names fall back to the default face.
        const int family = name == "sans-serif" ? 1 : name == "serif" ? 2 : name == "monospace" ? 3 : 0;
        return dx::VmValue::Int(TypefaceToken(family, call.arguments[1].AsInt()));
    }, flags);
    builder.StaticMethod("nativeCreateFromTypeface", "(II)I", [](dx::IntrinsicContext& call) {
        const auto token = call.arguments[0].AsInt();
        const auto family = token == 0 ? 0 : CheckTypefaceToken(token) / 4;
        return dx::VmValue::Int(TypefaceToken(family, call.arguments[1].AsInt()));
    }, flags);
    builder.StaticMethod("nativeGetStyle", "(I)I", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(CheckTypefaceToken(call.arguments[0].AsInt()) % 4);
    }, flags);
    builder.StaticMethod("nativeUnref", "(I)V", [](dx::IntrinsicContext& call) {
        // Finite immutable descriptors own no allocation or native reference count.
        static_cast<void>(CheckTypefaceToken(call.arguments[0].AsInt()));
        return dx::VmValue::Void();
    }, flags);
    builder.StaticMethod("nativeFreeCaches", "()V", [](dx::IntrinsicContext&) {
        // Pinned framework extension: this backend owns no native font cache.
        return dx::VmValue::Void();
    }, flags);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.typeface", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "external font files are unsupported"};
    };
    builder.StaticMethod("nativeCreateFromAsset", "(Landroid/content/res/AssetManager;Ljava/lang/String;)I", unsupported, flags);
    builder.StaticMethod("nativeCreateFromFile", "(Ljava/lang/String;)I", unsupported, flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
