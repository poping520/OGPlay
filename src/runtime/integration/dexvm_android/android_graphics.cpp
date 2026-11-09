// DVM-80: API-family translation unit. DVM-86 adds the high-frequency API 19
// geometry and drawable value classes here.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include "ogplay/runtime/ui/ui_leaf_measure.h"

#include "catalog.h"
#include "ogplay/runtime/raster/bitmap.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
std::int32_t ReadIntField(dx::Interpreter& vm, dx::VmObjectRef object, std::string_view name) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), std::string(name), "I");
    if (!field) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, std::string(name));
    return std::bit_cast<std::int32_t>(vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits);
}
}  // namespace
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_Canvas.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
// AOSP android-4.4_r1 external/skia/src/core/SkXfermode.cpp and
// include/core/SkColorPriv.h; framework SkPorterDuff maps MULTIPLY to MODULATE.
// CanvasState.argb is straight ARGB, so convert to premultiplied bytes for
// Skia's integer operations, then unpremultiply for the existing pixel consumers.
std::uint32_t BlendCanvasColor(std::uint32_t color, std::uint32_t destination, int mode) {
    if (mode == 0) return 0;
    if (mode == 2) return destination;
    const auto mul = [](int x, int y) { return (x * y + 127) / 255; };
    const int sa = static_cast<int>(color >> 24), da = static_cast<int>(destination >> 24);
    const auto channel = [&](int sc, int dc, bool alpha) {
        switch (mode) {
        case 1: return sc;
        case 3: return sc + ((dc * (256 - sa)) >> 8);
        case 4: return dc + ((sc * (256 - da)) >> 8);
        case 5: return (sc * (da + 1)) >> 8;
        case 6: return (dc * (sa + 1)) >> 8;
        case 7: return (sc * (256 - da)) >> 8;
        case 8: return (dc * (256 - sa)) >> 8;
        case 9: return alpha ? da : mul(sc, da) + mul(dc, 255 - sa);
        case 10: return alpha ? sa : mul(sc, 255 - da) + mul(dc, sa);
        case 11: return alpha ? sa + da - 2 * mul(sa, da) : mul(sc, 255 - da) + mul(dc, 255 - sa);
        case 12: return alpha ? sa + da - mul(sa, da) : sc + dc - (std::max(sc * da, dc * sa) + 127) / 255;
        case 13: return alpha ? sa + da - mul(sa, da) : sc + dc - (std::min(sc * da, dc * sa) + 127) / 255;
        case 14: return mul(sc, dc);
        case 15: return sc + dc - mul(sc, dc);
        case 16: return std::min(255, sc + dc);
        case 17: {
            if (alpha) return sa + da - mul(sa, da);
            const int blend = 2 * dc <= da ? 2 * sc * dc : sa * da - 2 * (da - dc) * (sa - sc);
            return std::clamp((blend + sc * (255 - da) + dc * (255 - sa) + 127) / 255, 0, 255);
        }
        default: throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid PorterDuff Mode.nativeInt"};
        }
    };
    const int a = channel(sa, da, true);
    std::uint32_t result = static_cast<std::uint32_t>(a) << 24;
    for (const auto shift : {16, 8, 0}) {
        const int sc = mul(static_cast<int>((color >> shift) & 255U), sa);
        const int dc = mul(static_cast<int>((destination >> shift) & 255U), da);
        const int value = channel(sc, dc, false);
        result |= static_cast<std::uint32_t>(a ? std::clamp((value * 255 + a / 2) / a, 0, 255) : 0) << shift;
    }
    return result;
}

dx::VmValue FillCanvasColor(const Context& context, dx::IntrinsicContext& call, std::uint32_t color, int mode) {
    const auto found = context->canvases.find(call.receiver.Value());
    if (found == context->canvases.end() || !found->second.locked)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas is not locked"};
    // Pure color fills are not constrained by clip: clipRect/save/restore remain unimplemented.
    for (auto& pixel : found->second.argb)
        pixel = BlendCanvasColor(color, pixel, mode);
    return dx::VmValue::Void();
}
}  // namespace

Decl Declare_android_graphics_Canvas(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Canvas;", "Ljava/lang/Object;");
    for (const bool width : {true, false}) {
        builder.VirtualMethod(width ? "getWidth" : "getHeight", "()I", [context, width](dx::IntrinsicContext& call) {
            const auto found = context->canvases.find(call.receiver.Value());
            if (found == context->canvases.end() || !found->second.locked)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas is not locked"};
            return dx::VmValue::Int(static_cast<std::int32_t>(width ? found->second.width : found->second.height));
        });
    }
    builder.VirtualMethod("save", "(I)I", [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.VirtualMethod("restore", "()V", GraphicsNoopHandler());
    builder.VirtualMethod("clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
                          [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.FinalMethod("getClipBounds", "()Landroid/graphics/Rect;", [context](dx::IntrinsicContext& call) {
        const auto rect = call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
        const auto root = call.vm.ProtectReferences(std::array{rect});
        const auto found = context->canvases.find(call.receiver.Value());
        if (found == context->canvases.end() || !found->second.locked)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas is not locked"};
        static_cast<void>(CallAndroidMethod(call.vm, rect, "set", "(IIII)V",
                                            {dx::VmValue::Int(0), dx::VmValue::Int(0),
                                             dx::VmValue::Int(static_cast<std::int32_t>(found->second.width)),
                                             dx::VmValue::Int(static_cast<std::int32_t>(found->second.height))}));
        return dx::VmValue::Ref(rect);
    });
    builder.VirtualMethod("drawColor", "(I)V", [context](dx::IntrinsicContext& call) {
        return FillCanvasColor(context, call, static_cast<std::uint32_t>(call.arguments[0].AsInt()), 3);
    });
    builder.VirtualMethod("drawColor", "(ILandroid/graphics/PorterDuff$Mode;)V", [context](dx::IntrinsicContext& call) {
        const auto mode = call.arguments[1].ref;
        if (!mode.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "PorterDuff mode is null"};
        return FillCanvasColor(context, call, static_cast<std::uint32_t>(call.arguments[0].AsInt()),
                               ReadIntField(call.vm, mode, "nativeInt"));
    });
    for (const bool argb : {false, true}) {
        builder.VirtualMethod(
            argb ? "drawARGB" : "drawRGB", argb ? "(IIII)V" : "(III)V", [context, argb](dx::IntrinsicContext& call) {
                const std::size_t first = argb ? 1 : 0;
                auto color = argb ? (static_cast<std::uint32_t>(call.arguments[0].AsInt()) & 255U) << 24 : 0xff000000U;
                for (std::size_t i = 0; i < 3; ++i)
                    color |= (static_cast<std::uint32_t>(call.arguments[first + i].AsInt()) & 255U) << (16 - i * 8);
                return FillCanvasColor(context, call, color, 3);
            });
    }
    builder.VirtualMethod(
        "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", [context](dx::IntrinsicContext& call) {
            RequirePlainCanvasPaint(call, call.arguments[3].ref);
            const auto canvas = context->canvases.find(call.receiver.Value());
            const auto bitmap = context->bitmaps.find(call.arguments[0].ref.Value());
            if (canvas == context->canvases.end() || !canvas->second.locked) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Canvas is not locked"};
            }
            if (!call.arguments[0].ref.IsValid() || bitmap == context->bitmaps.end() || bitmap->second.recycled) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "drawBitmap source is invalid"};
            }
            const auto left_value = call.arguments[1].AsFloat();
            const auto top_value = call.arguments[2].AsFloat();
            if (!std::isfinite(left_value) || !std::isfinite(top_value)) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "drawBitmap coordinates are not finite"};
            }
            const auto left = static_cast<std::int32_t>(std::floor(left_value));
            const auto top = static_cast<std::int32_t>(std::floor(top_value));
            auto& target = canvas->second;
            const auto& source = bitmap->second;
            for (std::int32_t y = 0; y < source.height; ++y) {
                const auto destination_y = top + y;
                if (destination_y < 0 || destination_y >= static_cast<std::int32_t>(target.height)) {
                    continue;
                }
                for (std::int32_t x = 0; x < source.width; ++x) {
                    const auto destination_x = left + x;
                    if (destination_x < 0 || destination_x >= static_cast<std::int32_t>(target.width)) {
                        continue;
                    }
                    target.argb[static_cast<std::size_t>(destination_y) * target.width +
                                static_cast<std::size_t>(destination_x)] =
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
    builder.MergesBootClass();
    builder.StaticMethod(
        "createFromXmlInner",
        "(Landroid/content/res/Resources;Lorg/xmlpull/v1/XmlPullParser;Landroid/util/AttributeSet;)Landroid/graphics/drawable/Drawable;",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            return dx::VmValue::Ref(InflateSealedRomDrawable(call, context, call.arguments[0].ref,
                                                             call.arguments[1].ref, call.arguments[2].ref));
        });
    const auto dimension = [context](const bool width) {
        return dx::IntrinsicHandler([context, width](dx::IntrinsicContext& call) {
            const auto found = context->ui_drawables.find(call.receiver.Value());
            if (found == context->ui_drawables.end() || !found->second.bitmap_drawable) return dx::VmValue::Int(-1);
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

    // AOSP 4.4 ROM line 165.

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_drawable_PaintDrawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_PaintDrawable(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/PaintDrawable;",
                                                    "Landroid/graphics/drawable/Drawable;");
    // PaintDrawable inherits AOSP ShapeDrawable's state overrides.
    const auto reject_state = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable state is not admitted"};
    };
    builder.OverrideMethod("mutate", "()Landroid/graphics/drawable/Drawable;", reject_state);
    builder.OverrideMethod("getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;", reject_state);
    builder.OverrideMethod("getAlpha", "()I", reject_state);
    builder.OverrideMethod("setAlpha", "(I)V", IgnoredUiRequest("Drawable.setAlpha"));
    builder.OverrideMethod("getOpacity", "()I", reject_state);
    builder.Constructor("(I)V", GraphicsNoopHandler());
    builder.VirtualMethod("setCornerRadius", "(F)V", GraphicsNoopHandler());
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
    {
        auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;", "Ljava/lang/Object;");
        builder.MergesBootClass();
        const auto face = builder.BoundInstanceField("mTypeface", "Landroid/graphics/Typeface;", dx::kAccPrivate);
        const auto shader = builder.BoundInstanceField("mShader", "Landroid/graphics/Shader;", dx::kAccPrivate);
        const auto xfer = builder.BoundInstanceField("mXfermode", "Landroid/graphics/Xfermode;", dx::kAccPrivate);
        const auto color_filter =
            builder.BoundInstanceField("mColorFilter", "Landroid/graphics/ColorFilter;", dx::kAccPrivate);
        const auto state = [context](dx::IntrinsicContext& call) -> DexVmAndroidContext::PaintMeasureState& {
            const auto found = context->paint_measure_states.find(call.receiver.Value());
            if (found == context->paint_measure_states.end())
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Paint measurement requires construction"};
            return found->second;
        };
        const auto flags = [](dx::IntrinsicContext& call, std::int32_t value) {
            // AA, filter-bitmap, dither and the two API19 default text flags.
            if ((value & ~0x507) != 0) RecordUiDegraded(call.vm, "Paint.flags");
            return value & 0x507;
        };
        const auto init = [context, face, flags](dx::IntrinsicContext& call) {
            DexVmAndroidContext::PaintMeasureState value;
            value.flags = flags(call, (call.arguments.empty() ? 0 : call.arguments[0].AsInt()) | 0x500);
            context->paint_measure_states.insert_or_assign(call.receiver.Value(), std::move(value));
            dx::IntrinsicCall(call).SetRef(face, dx::VmObjectRef{});
            return dx::VmValue::Void();
        };
        builder.Constructor("()V", init).Constructor("(I)V", init);
        builder.Constructor("(Landroid/graphics/Paint;)V",
                            [context, face, shader, xfer, color_filter](dx::IntrinsicContext& c) {
                                const auto source = c.arguments[0].ref;
                                if (!source.IsValid())
                                    throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "Paint source is null"};
                                const auto found = context->paint_measure_states.find(source.Value());
                                if (found == context->paint_measure_states.end())
                                    UnsupportedPaint(c, "Paint source requires construction");
                                DexVmAndroidContext::PaintMeasureState copy;
                                copy.flags = found->second.flags;
                                copy.text_size = found->second.text_size;
                                copy.alpha = found->second.alpha;
                                const dx::IntrinsicCall fields(c);
                                for (const auto field : {face, shader, xfer, color_filter})
                                    fields.SetRef(field, fields.GetRef(field, source));
                                context->paint_measure_states.insert_or_assign(c.receiver.Value(), std::move(copy));
                                return dx::VmValue::Void();
                            });
        builder.VirtualMethod(
            "getAlpha", "()I", [state](dx::IntrinsicContext& c) { return dx::VmValue::Int(state(c).alpha); },
            dx::kAccPublic | dx::kAccNative);
        builder.VirtualMethod(
            "setAlpha", "(I)V",
            [state](dx::IntrinsicContext& c) {
                const auto value = c.arguments[0].AsInt();
                if (state(c).text_view_owned) {
                    RecordUiDegraded(c.vm, "TextViewPaint.alpha");
                    return dx::VmValue::Void();
                }
                state(c).alpha = value & 0xff;  // SkPaint keeps the low byte.
                return dx::VmValue::Void();
            },
            dx::kAccPublic | dx::kAccNative);
        builder.VirtualMethod("setColorFilter", "(Landroid/graphics/ColorFilter;)Landroid/graphics/ColorFilter;",
                              [state, color_filter](dx::IntrinsicContext& c) {
                                  static_cast<void>(state(c));
                                  // Kept for getColorFilter; the fixed-function renderer never applies it.
                                  if (c.arguments[0].ref.IsValid()) RecordUiDegraded(c.vm, "Paint.colorFilter");
                                  dx::IntrinsicCall(c).SetRef(color_filter, c.arguments[0].ref);
                                  return c.arguments[0];
                              });
        for (const auto [name, signature, field] :
             {std::tuple{"setShader", "(Landroid/graphics/Shader;)Landroid/graphics/Shader;", shader},
              std::tuple{"setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;", xfer}}) {
            builder.VirtualMethod(name, signature, [state, field](dx::IntrinsicContext& c) {
                static_cast<void>(state(c));
                dx::IntrinsicCall(c).SetRef(field, c.arguments[0].ref);
                return dx::VmValue::Ref(c.arguments[0].ref);
            });
        }
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
            if (state(c).text_view_owned) {
                RecordUiDegraded(c.vm, "TextViewPaint.flags");
                return dx::VmValue::Void();
            }
            state(c).flags = flags(c, c.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
        builder.FinalMethod("isAntiAlias", "()Z",
                            [state](dx::IntrinsicContext& c) { return dx::VmValue::Int(state(c).flags & 1); });
        builder.VirtualMethod("setAntiAlias", "(Z)V", [state](dx::IntrinsicContext& c) {
            auto& value = state(c);
            if (value.text_view_owned) {
                RecordUiDegraded(c.vm, "TextViewPaint.flags");
                return dx::VmValue::Void();
            }
            value.flags = (value.flags & ~1) | (c.arguments[0].AsInt() != 0);
            return dx::VmValue::Void();
        });
        for (const auto [name, bit] : {std::pair{"setFilterBitmap", 0x02}, std::pair{"setDither", 0x04}}) {
            builder.VirtualMethod(name, "(Z)V", [state, bit](dx::IntrinsicContext& c) {
                auto& value = state(c);
                if (value.text_view_owned) {
                    RecordUiDegraded(c.vm, "TextViewPaint.flags");
                    return dx::VmValue::Void();
                }
                value.flags = (value.flags & ~bit) | (c.arguments[0].AsInt() != 0 ? bit : 0);
                return dx::VmValue::Void();
            });
        }
        builder.VirtualMethod("getTextSize", "()F",
                              [state](dx::IntrinsicContext& c) { return dx::VmValue::Float(state(c).text_size); });
        builder.VirtualMethod("setTextSize", "(F)V", [state](dx::IntrinsicContext& c) {
            const auto size = c.arguments[0].AsFloat();
            if (!std::isfinite(size) || size < 1 || size > 128 || state(c).text_view_owned) {
                RecordUiDegraded(c.vm, "Paint.textSize");
                return dx::VmValue::Void();
            }
            state(c).text_size = size;
            return dx::VmValue::Void();
        });
        builder.VirtualMethod("getTypeface", "()Landroid/graphics/Typeface;", [face, state](dx::IntrinsicContext& c) {
            static_cast<void>(state(c));
            return dx::VmValue::Ref(dx::IntrinsicCall(c).GetRef(face));
        });
        builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
                              [face, state](dx::IntrinsicContext& c) {
                                  const auto ref = c.arguments[0].ref;
                                  if (state(c).text_view_owned) {
                                      RecordUiDegraded(c.vm, "TextViewPaint.typeface");
                                      return dx::VmValue::Ref(ref);
                                  }
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
                const bool chars = kind == 2 || kind == 5;
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
                if (end - start > static_cast<std::int64_t>(ui::kMaxFixedTextUnits))
                    UnsupportedPaint(c, "Paint text exceeds fixed-font service bound");
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
                // Paint remains single-line; use the same glyph coverage as TextView.
                try {
                    for (const auto unit : text)
                        ui::RequireTextGlyph(unit, context->ui_tree.FallbackFont().get());
                } catch (const std::runtime_error& error) {
                    UnsupportedPaint(c, error.what());
                }
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
                if (kind >= 4) {
                    const auto rect = c.arguments[3].ref;
                    if (!rect.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "bounds rect"};
                    const auto metrics = ui::MeasureFixedText(text, value.text_size, static_cast<std::uint32_t>(style),
                                                              context->ui_tree.FallbackFont().get());
                    const auto top = text.empty() ? 0 : -7 * metrics.scale;
                    const auto bottom = text.empty() ? 0 : top + metrics.height;
                    return CallAndroidMethod(c.vm, rect, "set", "(IIII)V",
                                             {dx::VmValue::Int(0), dx::VmValue::Int(top),
                                              dx::VmValue::Int(metrics.width), dx::VmValue::Int(bottom)});
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
        const auto font_metrics = [context, state](dx::IntrinsicContext& c) {
            // The fixed raster cell has seven rows, baseline at its bottom and no descender.
            // Non-ASCII fallback is rasterized into this same cell by the UI font service.
            const auto metrics =
                ui::MeasureFixedText(u"M", state(c).text_size, 0, context->ui_tree.FallbackFont().get());
            const auto ascent = -static_cast<float>(metrics.height);
            if (!c.arguments.empty() && c.arguments[0].ref.IsValid()) {
                const auto object = c.arguments[0].ref;
                for (const auto name : {"top", "ascent", "descent", "bottom", "leading"}) {
                    const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(object), name, "F");
                    if (!field)
                        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "FontMetrics field missing");
                    const auto value =
                        std::string_view(name) == "top" || std::string_view(name) == "ascent" ? ascent : 0.F;
                    c.vm.Model().InstanceSlots(object)[c.vm.Linker().Field(*field).slot] = {
                        dx::VmValue::Float(value).cat1, dx::SlotTag::cat1};
                }
            }
            return dx::VmValue::Float(static_cast<float>(metrics.height));
        };
        builder.VirtualMethod("getFontMetrics", "(Landroid/graphics/Paint$FontMetrics;)F", font_metrics);
        builder.VirtualMethod("ascent", "()F", [font_metrics](dx::IntrinsicContext& c) {
            return dx::VmValue::Float(-font_metrics(c).AsFloat());
        });
        builder.VirtualMethod("descent", "()F", [state](dx::IntrinsicContext& c) {
            static_cast<void>(state(c));
            return dx::VmValue::Float(0.F);
        });
        builder.VirtualMethod("getTextBounds", "(Ljava/lang/String;IILandroid/graphics/Rect;)V", measure(4));
        builder.VirtualMethod("getTextBounds", "([CIILandroid/graphics/Rect;)V", measure(5));
        return std::move(builder).Build();
    }
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_graphics_PorterDuff.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_PorterDuff_Mode(const Context& context) {
    static_cast<void>(context);
    constexpr std::array modes{std::pair{"CLEAR", 0},    std::pair{"SRC", 1},       std::pair{"DST", 2},
                               std::pair{"SRC_OVER", 3}, std::pair{"DST_OVER", 4},  std::pair{"SRC_IN", 5},
                               std::pair{"DST_IN", 6},   std::pair{"SRC_OUT", 7},   std::pair{"DST_OUT", 8},
                               std::pair{"SRC_ATOP", 9}, std::pair{"DST_ATOP", 10}, std::pair{"XOR", 11},
                               std::pair{"DARKEN", 12},  std::pair{"LIGHTEN", 13},  std::pair{"MULTIPLY", 14},
                               std::pair{"SCREEN", 15},  std::pair{"ADD", 16},      std::pair{"OVERLAY", 17}};
    dx::IntrinsicEnumBuilder enum_builder("Landroid/graphics/PorterDuff$Mode;",
                                          {"CLEAR", "SRC", "DST", "SRC_OVER", "DST_OVER", "SRC_IN", "DST_IN", "SRC_OUT",
                                           "DST_OUT", "SRC_ATOP", "DST_ATOP", "XOR", "DARKEN", "LIGHTEN", "MULTIPLY",
                                           "SCREEN", "ADD", "OVERLAY"});
    const auto native_int =
        enum_builder.ClassBuilder().BoundInstanceField("nativeInt", "I", dx::kAccPublic | dx::kAccFinal);
    enum_builder.WithConstantInitializer([native_int, modes](dx::IntrinsicContext& context, const dx::VmObjectRef value,
                                                             std::string_view, const std::int32_t ordinal) {
        const dx::IntrinsicCall call(context);
        call.SetInt(native_int, value, modes[static_cast<std::size_t>(ordinal)].second);
    });
    return std::move(enum_builder).Build();
}

Decl Declare_android_graphics_PorterDuff(const Context& context) {
    static_cast<void>(context);
    return dx::IntrinsicClassBuilder::Class("Landroid/graphics/PorterDuff;", "Ljava/lang/Object;").Build();
}

Decl Declare_android_graphics_drawable_BitmapDrawable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/BitmapDrawable;",
                                                    "Landroid/graphics/drawable/Drawable;");
    builder.MergesBootClass();
    // mutate, alpha and getBitmap keep the ROM bodies.
    builder.OverrideMethod("getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;",
                           [](dx::IntrinsicContext& call) -> dx::VmValue {
                               if (auto* ledger = call.vm.Ledger())
                                   ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
                               throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                     "Drawable state is not admitted"};
                           });

    const auto bitmap_ctor = [context](const std::size_t index) {
        return dx::IntrinsicHandler([context, index](dx::IntrinsicContext& call) {
            const auto bitmap = call.arguments[index].ref;
            if (bitmap.IsValid() && !context->bitmaps.contains(bitmap.Value()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "BitmapDrawable requires a Bitmap backing"};
            {
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

                const auto state_type =
                    call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/BitmapDrawable$BitmapState;");
                const auto state_initialized = call.vm.EnsureClassInitialized(state_type);
                if (state_initialized.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(state_initialized.exception_class).descriptor,
                                          state_initialized.exception_message, state_initialized.exception};
                const auto state =
                    call.vm.NewIntrinsicInstance("Landroid/graphics/drawable/BitmapDrawable$BitmapState;");
                const auto state_root = call.vm.ProtectReferences(std::array{state});
                const auto state_ctor = call.vm.Linker().FindDirectMethod(
                    call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/BitmapDrawable$BitmapState;"),
                    "<init>", "(Landroid/graphics/Bitmap;)V");
                const auto initialized =
                    call.vm.Call(*state_ctor, std::array{dx::VmValue::Ref(state), dx::VmValue::Ref(bitmap)});
                if (initialized.exception.IsValid())
                    throw dx::VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                          initialized.exception_message, initialized.exception};
                write("mBitmapState", "Landroid/graphics/drawable/BitmapDrawable$BitmapState;", state.Value(),
                      dx::SlotTag::ref);
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
                    const auto density = ReadIntField(call.vm, bitmap, "mDensity");
                    return density == 0 || density == target
                               ? size
                               : static_cast<std::int32_t>((static_cast<std::int64_t>(size) * target + (density >> 1)) /
                                                           density);
                };
                write("mBitmapWidth", "I", static_cast<std::uint32_t>(dimension(true)));
                write("mBitmapHeight", "I", static_cast<std::uint32_t>(dimension(false)));
            }

            context->ui_drawables[call.receiver.Value()].bitmap_drawable = true;
            return dx::VmValue::Void();
        });
    };
    builder.Constructor("(Landroid/graphics/Bitmap;)V", bitmap_ctor(0))
        .Constructor("(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V", bitmap_ctor(1));
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
    builder.MergesBootClass();
    constexpr auto flags = dx::kAccPrivate | dx::kAccNative;
    builder.StaticMethod(
        "nativeCreate", "(Ljava/lang/String;I)I",
        [](dx::IntrinsicContext& call) {
            const auto name =
                call.arguments[0].ref.IsValid() ? call.vm.StringUtf8(call.arguments[0].ref) : std::string{};
            // API19 unknown family names fall back to the default face.
            const int family = name == "sans-serif" ? 1 : name == "serif" ? 2 : name == "monospace" ? 3 : 0;
            return dx::VmValue::Int(TypefaceToken(family, call.arguments[1].AsInt()));
        },
        flags);
    builder.StaticMethod(
        "nativeCreateFromTypeface", "(II)I",
        [](dx::IntrinsicContext& call) {
            const auto token = call.arguments[0].AsInt();
            const auto family = token == 0 ? 0 : CheckTypefaceToken(token) / 4;
            return dx::VmValue::Int(TypefaceToken(family, call.arguments[1].AsInt()));
        },
        flags);
    builder.StaticMethod(
        "nativeGetStyle", "(I)I",
        [](dx::IntrinsicContext& call) { return dx::VmValue::Int(CheckTypefaceToken(call.arguments[0].AsInt()) % 4); },
        flags);
    builder.StaticMethod(
        "nativeUnref", "(I)V",
        [](dx::IntrinsicContext& call) {
            // Finite immutable descriptors own no allocation or native reference count.
            static_cast<void>(CheckTypefaceToken(call.arguments[0].AsInt()));
            return dx::VmValue::Void();
        },
        flags);
    const auto unsupported = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.typeface", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "external font files are unsupported"};
    };
    builder.StaticMethod("nativeCreateFromAsset", "(Landroid/content/res/AssetManager;Ljava/lang/String;)I",
                         unsupported, flags);
    builder.StaticMethod("nativeCreateFromFile", "(Ljava/lang/String;)I", unsupported, flags);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
namespace {
DexVmAndroidContext::UiDrawableState& Skin(dx::IntrinsicContext& call, const Context& context) {
    const auto it = context->ui_drawables.find(call.receiver.Value());
    if (it == context->ui_drawables.end() || (!it->second.rom_skin && !it->second.selector_state))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "ROM skin backing unavailable"};
    return it->second;
}

void SkinInit(dx::IntrinsicContext& call, const Context& context) {
    const auto ctor = call.vm.Linker().FindDirectMethod(
        call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "<init>", "()V");
    const auto out = call.vm.Call(*ctor, std::array{dx::VmValue::Ref(call.receiver)});
    if (out.exception.IsValid())
        throw dx::VmJavaThrow{call.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                              out.exception};
    context->ui_drawables[call.receiver.Value()].rom_skin = true;
}

dx::VmObjectRef DrawableRef(dx::IntrinsicContext& call, dx::VmObjectRef object, const char* name,
                            const char* descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(object), name, descriptor);
    if (!field) throw std::logic_error("ROM Drawable field unavailable");
    call.vm.RequireImplementedField(*field);
    return dx::VmObjectRef{call.vm.Model().InstanceSlots(object)[call.vm.Linker().Field(*field).slot].bits};
}

dx::VmObjectRef RomContainerState(dx::IntrinsicContext& call) {
    return DrawableRef(call, call.receiver, "mDrawableContainerState",
                       "Landroid/graphics/drawable/DrawableContainer$DrawableContainerState;");
}

dx::VmObjectRef RomContainerCurrent(dx::IntrinsicContext& call) {
    return DrawableRef(call, call.receiver, "mCurrDrawable", "Landroid/graphics/drawable/Drawable;");
}

void AddSkinQueries(dx::IntrinsicClassBuilder& b, const Context& context, bool container) {
    b.VirtualMethod("getOpticalInsets", "()Landroid/graphics/Insets;", [context, container](dx::IntrinsicContext& c) {
        if (container && RomContainerState(c).IsValid())
            return dx::VmValue::Ref(DrawableRef(c, c.receiver, "mInsets", "Landroid/graphics/Insets;"));
        const auto& state = Skin(c, context);
        if (container && state.skin_current >= 0)
            return CallAndroidMethod(c.vm, state.skin_children.at(static_cast<std::size_t>(state.skin_current)),
                                     "getOpticalInsets", "()Landroid/graphics/Insets;");
        const auto klass = c.vm.Linker().ResolveDescriptor("Landroid/graphics/Insets;");
        const auto method = c.vm.Linker().FindDirectMethod(klass, "of", "(IIII)Landroid/graphics/Insets;");
        const auto init = c.vm.EnsureClassInitialized(klass);
        if (init.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(init.exception_class).descriptor, init.exception_message,
                                  init.exception};
        const auto out = c.vm.Call(
            *method, std::array{dx::VmValue::Int(state.skin_optical[0]), dx::VmValue::Int(state.skin_optical[1]),
                                dx::VmValue::Int(state.skin_optical[2]), dx::VmValue::Int(state.skin_optical[3])});
        if (out.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                  out.exception};
        return out.value;
    });
    b.VirtualMethod("getOpacity", "()I", [context, container](dx::IntrinsicContext& c) {
        if (container) {
            const auto state = RomContainerState(c);
            if (state.IsValid()) {
                const auto current = RomContainerCurrent(c);
                if (!current.IsValid() || !CallAndroidMethod(c.vm, current, "isVisible", "()Z").AsInt())
                    return dx::VmValue::Int(-2);
                return CallAndroidMethod(c.vm, state, "getOpacity", "()I");
            }
        }
        const auto& state = Skin(c, context);
        if (container) {
            if (state.skin_current < 0) return dx::VmValue::Int(-2);
            int opacity = -1;
            for (const auto child : state.skin_children) {
                const auto value = CallAndroidMethod(c.vm, child, "getOpacity", "()I").AsInt();
                if (value != -1) opacity = -3;
            }
            return dx::VmValue::Int(opacity);
        }
        if (!state.leaf_pixels)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "NinePatch has no decoded backing"};
        for (std::size_t i = 3; i < state.leaf_pixels->rgba8.size(); i += 4)
            if (state.leaf_pixels->rgba8[i] != 255) return dx::VmValue::Int(-3);
        return dx::VmValue::Int(-1);
    });
    b.VirtualMethod("getPadding", "(Landroid/graphics/Rect;)Z", [context, container](dx::IntrinsicContext& call) {
        if (container) {
            const auto state = RomContainerState(call);
            if (state.IsValid()) {
                const auto padding =
                    CallAndroidMethod(call.vm, state, "getConstantPadding", "()Landroid/graphics/Rect;").ref;
                const auto padding_root = call.vm.ProtectReferences(std::array{padding});
                if (padding.IsValid()) {
                    static_cast<void>(CallAndroidMethod(call.vm, call.arguments[0].ref, "set",
                                                        "(Landroid/graphics/Rect;)V", {dx::VmValue::Ref(padding)}));
                    return dx::VmValue::Int(
                        (ReadIntField(call.vm, padding, "left") | ReadIntField(call.vm, padding, "top") |
                         ReadIntField(call.vm, padding, "right") | ReadIntField(call.vm, padding, "bottom")) != 0);
                }
                const auto current = RomContainerCurrent(call);
                if (current.IsValid())
                    return CallAndroidMethod(call.vm, current, "getPadding", "(Landroid/graphics/Rect;)Z",
                                             {call.arguments[0]});
                static_cast<void>(CallAndroidMethod(
                    call.vm, call.arguments[0].ref, "set", "(IIII)V",
                    {dx::VmValue::Int(0), dx::VmValue::Int(0), dx::VmValue::Int(0), dx::VmValue::Int(0)}));
                return dx::VmValue::Int(0);
            }
        }
        const auto& state = Skin(call, context);
        const auto rect = call.arguments[0].ref;
        if (!rect.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "padding rect == null"};
        bool nonzero = false;
        std::size_t i = 0;
        for (const auto name : {"left", "top", "right", "bottom"}) {
            const auto f = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(rect), name, "I");
            call.vm.Model().InstanceSlots(rect)[call.vm.Linker().Field(*f).slot] = {
                static_cast<std::uint32_t>(state.skin_padding[i]), dx::SlotTag::cat1};
            nonzero |= state.skin_padding[i++] != 0;
        }
        return dx::VmValue::Int(nonzero);
    });
    for (const auto width : {true, false}) {
        const auto size = [context, width, container](dx::IntrinsicContext& call) {
            if (container) {
                const auto state = RomContainerState(call);
                if (state.IsValid()) {
                    if (CallAndroidMethod(call.vm, state, "isConstantSize", "()Z").AsInt())
                        return CallAndroidMethod(call.vm, state, width ? "getConstantWidth" : "getConstantHeight",
                                                 "()I");
                    const auto current = RomContainerCurrent(call);
                    return current.IsValid()
                               ? CallAndroidMethod(call.vm, current, width ? "getIntrinsicWidth" : "getIntrinsicHeight",
                                                   "()I")
                               : dx::VmValue::Int(-1);
                }
            }
            const auto& state = Skin(call, context);
            if (container && state.skin_current >= 0) {
                const auto child = state.skin_children.at(static_cast<std::size_t>(state.skin_current));
                return CallAndroidMethod(call.vm, child, width ? "getIntrinsicWidth" : "getIntrinsicHeight", "()I");
            }
            return dx::VmValue::Int(width ? state.skin_width : state.skin_height);
        };
        b.VirtualMethod(width ? "getIntrinsicWidth" : "getIntrinsicHeight", "()I", size);
        b.VirtualMethod(
            width ? "getMinimumWidth" : "getMinimumHeight", "()I", [size, container, width](dx::IntrinsicContext& c) {
                if (container) {
                    const auto state = RomContainerState(c);
                    if (state.IsValid()) {
                        if (CallAndroidMethod(c.vm, state, "isConstantSize", "()Z").AsInt())
                            return CallAndroidMethod(
                                c.vm, state, width ? "getConstantMinimumWidth" : "getConstantMinimumHeight", "()I");
                        const auto current = RomContainerCurrent(c);
                        return current.IsValid()
                                   ? CallAndroidMethod(c.vm, current, width ? "getMinimumWidth" : "getMinimumHeight",
                                                       "()I")
                                   : dx::VmValue::Int(0);
                    }
                }
                return dx::VmValue::Int(std::max(0, size(c).AsInt()));
            });
    }
}
}  // namespace

Decl Declare_android_graphics_drawable_DrawableContainer(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/DrawableContainer;",
                                              "Landroid/graphics/drawable/Drawable;",
                                              {"Landroid/graphics/drawable/Drawable$Callback;"});
    b.MergesBootClass();
    const auto reject_state = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable state is not admitted"};
    };
    b.OverrideMethod("mutate", "()Landroid/graphics/drawable/Drawable;", reject_state);
    b.OverrideMethod("getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;", reject_state);
    b.OverrideMethod("getAlpha", "()I", reject_state);
    b.OverrideMethod("setAlpha", "(I)V", IgnoredUiRequest("Drawable.setAlpha"));
    b.VirtualMethod("animate", "(Z)V", IgnoredUiRequest("DrawableContainer.animate"), 0);
    AddSkinQueries(b, context, true);
    b.VirtualMethod("setVisible", "(ZZ)Z", [context](dx::IntrinsicContext& c) {
        // A virtual base method is resolved explicitly to preserve its ROM field protocol.
        const auto index = c.vm.Linker().FindVtableIndex(
            c.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "setVisible", "(ZZ)Z");
        const auto id = c.vm.Linker()
                            .Class(c.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;"))
                            .vtable.at(*index);
        const auto out = c.vm.Call(id, std::array{dx::VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1]});
        if (out.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                  out.exception};
        if (RomContainerState(c).IsValid()) {
            const auto child = RomContainerCurrent(c);
            if (child.IsValid())
                static_cast<void>(
                    CallAndroidMethod(c.vm, child, "setVisible", "(ZZ)Z", {c.arguments[0], c.arguments[1]}));
        } else
            for (const auto child : Skin(c, context).skin_children)
                static_cast<void>(
                    CallAndroidMethod(c.vm, child, "setVisible", "(ZZ)Z", {c.arguments[0], c.arguments[1]}));
        return out.value;
    });
    // Drawable.Callback as in the ROM: only the current child reaches the owner's
    // callback, re-addressed to this container; other children are dropped.
    for (const auto& [name, sig] : std::array<std::pair<const char*, const char*>, 3>{
             {{"invalidateDrawable", "(Landroid/graphics/drawable/Drawable;)V"},
              {"scheduleDrawable", "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;J)V"},
              {"unscheduleDrawable", "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;)V"}}}) {
        b.VirtualMethod(name, sig, [context, name, sig](dx::IntrinsicContext& c) {
            if (RomContainerState(c).IsValid()) {
                if (RomContainerCurrent(c) != c.arguments[0].ref) return dx::VmValue::Void();
            } else {
                const auto& s = Skin(c, context);
                if (s.skin_current < 0 ||
                    s.skin_children.at(static_cast<std::size_t>(s.skin_current)) != c.arguments[0].ref)
                    return dx::VmValue::Void();
            }
            const auto callback =
                CallAndroidMethod(c.vm, c.receiver, "getCallback", "()Landroid/graphics/drawable/Drawable$Callback;")
                    .ref;
            if (!callback.IsValid()) return dx::VmValue::Void();
            std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver)};
            args.insert(args.end(), c.arguments.begin() + 1, c.arguments.end());
            static_cast<void>(CallAndroidMethod(c.vm, callback, name, sig, std::move(args)));
            return dx::VmValue::Void();
        });
    }
    b.VirtualMethod("getCurrent", "()Landroid/graphics/drawable/Drawable;", [context](dx::IntrinsicContext& c) {
        if (RomContainerState(c).IsValid()) return dx::VmValue::Ref(RomContainerCurrent(c));
        const auto& s = Skin(c, context);
        return dx::VmValue::Ref(s.skin_current < 0 ? dx::VmObjectRef{}
                                                   : s.skin_children.at(static_cast<std::size_t>(s.skin_current)));
    });
    return std::move(b).Build();
}

Decl Declare_android_graphics_drawable_NinePatchDrawable(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/NinePatchDrawable;",
                                              "Landroid/graphics/drawable/Drawable;");
    b.MergesBootClass();
    b.Constructor("()V", [context](dx::IntrinsicContext& c) {
        SkinInit(c, context);
        return dx::VmValue::Void();
    });
    const auto reject_state = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable state is not admitted"};
    };
    b.OverrideMethod("mutate", "()Landroid/graphics/drawable/Drawable;", reject_state);
    b.OverrideMethod("getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;", reject_state);
    b.OverrideMethod("getAlpha", "()I", reject_state);
    b.OverrideMethod("setAlpha", "(I)V", IgnoredUiRequest("Drawable.setAlpha"));
    b.VirtualMethod("getChangingConfigurations", "()I", [context](dx::IntrinsicContext& c) {
        // Sealed host skins have no mutable ConstantState configuration producer.
        // Resource loading contributes zero flags; the base ROM field remains observable.
        static_cast<void>(Skin(c, context));
        return dx::VmValue::Int(ReadIntField(c.vm, c.receiver, "mChangingConfigurations"));
    });
    b.VirtualMethod("setAutoMirrored", "(Z)V", [context](dx::IntrinsicContext& c) {
        static_cast<void>(Skin(c, context));
        if (c.arguments[0].AsInt()) RecordUiDegraded(c.vm, "Drawable.autoMirrored");
        return dx::VmValue::Void();
    });
    AddSkinQueries(b, context, false);
    return std::move(b).Build();
}

Decl Declare_android_graphics_drawable_StateListDrawable(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/StateListDrawable;",
                                              "Landroid/graphics/drawable/DrawableContainer;");
    b.MergesBootClass();
    b.OverrideMethod("mutate", "()Landroid/graphics/drawable/Drawable;", [](dx::IntrinsicContext& c) -> dx::VmValue {
        if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "StateListDrawable.mutate is not admitted"};
    });
    b.Constructor("()V", [context](dx::IntrinsicContext& c) {
        SkinInit(c, context);
        auto& state = context->ui_drawables.at(c.receiver.Value());
        state.rom_skin = false;
        state.selector_state = true;
        state.skin_width = state.skin_height = -1;
        return dx::VmValue::Void();
    });
    const auto state_set = [context](dx::IntrinsicContext& c, std::int32_t index) {
        auto& state = Skin(c, context);
        if (index < 0 || static_cast<std::size_t>(index) >= state.skin_children.size())
            throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "selector state index"};
        return state.skin_state_arrays[static_cast<std::size_t>(index)];
    };
    const auto state_index = [context, state_set](dx::IntrinsicContext& c, dx::VmObjectRef states) {
        const auto matcher = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/util/StateSet;"),
                                                            "stateSetMatches", "([I[I)Z");
        const auto count = Skin(c, context).skin_children.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto set = state_set(c, static_cast<std::int32_t>(i));
            const auto matched = c.vm.Call(*matcher, std::array{dx::VmValue::Ref(set), dx::VmValue::Ref(states)});
            if (matched.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(matched.exception_class).descriptor,
                                      matched.exception_message, matched.exception};
            if (matched.value.AsInt()) return static_cast<std::int32_t>(i);
        }
        return -1;
    };
    b.VirtualMethod("addState", "([ILandroid/graphics/drawable/Drawable;)V", [context](dx::IntrinsicContext& c) {
        const auto child = c.arguments[1].ref;
        if (!child.IsValid()) return dx::VmValue::Void();
        auto& state = Skin(c, context);
        // Keep the caller's actual int[] identity, including API19 zero terminators.
        state.skin_children.push_back(child);
        state.skin_state_arrays.resize(state.skin_children.size());
        state.skin_state_arrays.back() = c.arguments[0].ref;
        static_cast<void>(CallAndroidMethod(c.vm, child, "setCallback",
                                            "(Landroid/graphics/drawable/Drawable$Callback;)V",
                                            {dx::VmValue::Ref(c.receiver)}));
        const auto states = CallAndroidMethod(c.vm, c.receiver, "getState", "()[I").ref;
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "onStateChange", "([I)Z", {dx::VmValue::Ref(states)}));
        return dx::VmValue::Void();
    });
    b.VirtualMethod("getStateCount", "()I", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(static_cast<std::int32_t>(Skin(c, context).skin_children.size()));
    });
    b.VirtualMethod("getStateSet", "(I)[I", [state_set](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(state_set(c, c.arguments[0].AsInt()));
    });
    b.VirtualMethod("getStateDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                    [context, state_set](dx::IntrinsicContext& c) {
                        const auto index = c.arguments[0].AsInt();
                        static_cast<void>(state_set(c, index));
                        return dx::VmValue::Ref(Skin(c, context).skin_children[static_cast<std::size_t>(index)]);
                    });
    b.VirtualMethod("getStateDrawableIndex", "([I)I", [state_index](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(state_index(c, c.arguments[0].ref));
    });
    b.VirtualMethod("setLayoutDirection", "(I)V", [context](dx::IntrinsicContext& c) {
        const auto klass = c.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/Drawable;");
        const auto index = c.vm.Linker().FindVtableIndex(klass, "setLayoutDirection", "(I)V");
        const auto out = c.vm.Call(c.vm.Linker().Class(klass).vtable.at(*index),
                                   std::array{dx::VmValue::Ref(c.receiver), c.arguments[0]});
        if (out.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                  out.exception};
        for (const auto child : Skin(c, context).skin_children)
            static_cast<void>(CallAndroidMethod(c.vm, child, "setLayoutDirection", "(I)V", {c.arguments[0]}));
        return dx::VmValue::Void();
    });
    b.VirtualMethod("isStateful", "()Z", [context](dx::IntrinsicContext& c) {
        static_cast<void>(Skin(c, context));
        return dx::VmValue::Int(1);
    });
    b.VirtualMethod(
        "onStateChange", "([I)Z",
        [context, state_index](dx::IntrinsicContext& c) {
            auto& s = Skin(c, context);
            auto selected = state_index(c, c.arguments[0].ref);
            if (selected < 0) {
                const auto klass = c.vm.Linker().ResolveDescriptor("Landroid/util/StateSet;");
                const auto field = c.vm.Linker().FindFieldRecursive(klass, "WILD_CARD", "[I");
                const dx::VmObjectRef wildcard{
                    c.vm.Linker().Class(klass).static_storage[c.vm.Linker().Field(*field).slot]};
                selected = state_index(c, wildcard);
            }
            const bool changed = selected != s.skin_current;
            s.skin_current = selected;
            if (selected >= 0)
                static_cast<void>(CallAndroidMethod(c.vm, s.skin_children.at(static_cast<std::size_t>(selected)),
                                                    "setState", "([I)Z", {c.arguments[0]}));
            return dx::VmValue::Int(changed);
        },
        dx::kAccProtected);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_graphics_drawable_AnimationDrawable(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/AnimationDrawable;",
                                              "Landroid/graphics/drawable/DrawableContainer;");
    b.MergesBootClass();
    // Preserve the ROM unscheduled setFrame branch. Actual frame animation is not admitted.
    b.DirectMethod(
        "setFrame", "(IZZ)V",
        [](dx::IntrinsicContext& c) -> dx::VmValue {
            if (c.arguments[2].AsInt()) RecordUiDegraded(c.vm, "AnimationDrawable.schedule");
            const auto frame = c.arguments[0].AsInt();
            if (frame >= CallAndroidMethod(c.vm, c.receiver, "getNumberOfFrames", "()I").AsInt())
                return dx::VmValue::Void();
            const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(c.receiver), "mCurFrame", "I");
            c.vm.Model().InstanceSlots(c.receiver)[c.vm.Linker().Field(*field).slot] = {
                static_cast<std::uint32_t>(frame), dx::SlotTag::cat1};
            static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "selectDrawable", "(I)Z", {c.arguments[0]}));
            if (c.arguments[1].AsInt())
                static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "unscheduleSelf", "(Ljava/lang/Runnable;)V",
                                                    {dx::VmValue::Ref(c.receiver)}));
            return dx::VmValue::Void();
        },
        dx::kAccPrivate);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
