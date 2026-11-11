#include "android_skia_paint.h"

#include "include/core/SkPathUtils.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkImageFilters.h"

#include <array>

namespace ogplay::runtime::android_intrinsics {
namespace skia {
raster::Paint& PaintPeer(DexVmAndroidContext& context, int token) {
    return Require(context.skia_objects->paints, token);
}

raster::Paint CanvasPaintPeer(DexVmAndroidContext& context, const raster::Canvas& target, int token) {
    static const raster::Paint defaults;
    auto paint = token ? PaintPeer(context, token) : defaults;
    if (const auto* filter = target.DrawFilter())
        paint.SetFlags((paint.flags & ~filter->clear_bits) | filter->set_bits);
    if (paint.shader) paint.paint.setShader(paint.shader->Resolve(paint.Sampling()));
    return paint;
}
}  // namespace skia

namespace {
using namespace skia;
using V = dx::VmValue;
using Paint = raster::Paint;
constexpr auto kPublicNative = dx::kAccPublic | dx::kAccNative;

template <class Peer, class Getter, class Setter>
void AddScalar(dx::IntrinsicClassBuilder& builder, const char* getter, const char* setter, Peer peer, Getter get,
               Setter set) {
    builder.VirtualMethod(
        getter, "()F", [peer, get](dx::IntrinsicContext& c) { return V::Float(get(peer(c))); }, kPublicNative);
    builder.VirtualMethod(
        setter, "(F)V",
        [peer, set](dx::IntrinsicContext& c) {
            set(peer(c), c.arguments[0].AsFloat());
            return V::Void();
        },
        kPublicNative);
}

template <class T>
void AddEnum(dx::IntrinsicClassBuilder& builder, const Context& context, const char* getter, const char* setter,
             T (SkPaint::*get)() const, void (SkPaint::*set)(T)) {
    builder.StaticMethod(
        getter, "(I)I",
        [context, get](dx::IntrinsicContext& c) {
            return V::Int((PaintPeer(*context, c.arguments[0].AsInt()).paint.*get)());
        },
        kStaticNative);
    builder.StaticMethod(
        setter, "(II)V",
        [context, set](dx::IntrinsicContext& c) {
            const auto value = c.arguments[1].AsInt();
            if (value >= 0 && value <= 2)
                (PaintPeer(*context, c.arguments[0].AsInt()).paint.*set)(static_cast<T>(value));
            return V::Void();
        },
        kStaticNative);
}
}  // namespace

Decl Declare_android_graphics_Paint(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Paint;");
    b.MergesBootClass();
    const auto peer = [context](dx::IntrinsicContext& c) -> Paint& {
        return PaintPeer(*context, IntField(c.vm, c.receiver, "mNativePaint"));
    };
    const auto token_peer = [context](dx::IntrinsicContext& c) -> Paint& {
        return PaintPeer(*context, c.arguments[0].AsInt());
    };

    b.StaticMethod(
        "native_init", "()I",
        [context](dx::IntrinsicContext&) {
            return JavaToken(context->skia_objects->paints.Create(context->system_font_source));
        },
        kStaticNative);
    b.StaticMethod(
        "native_initWithPaint", "(I)I",
        [context, token_peer](dx::IntrinsicContext& c) {
            return JavaToken(context->skia_objects->paints.Create(token_peer(c)));
        },
        kStaticNative);
    b.StaticMethod(
        "native_reset", "(I)V",
        [context, token_peer](dx::IntrinsicContext& c) {
            token_peer(c) = Paint(context->system_font_source);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_set", "(II)V",
        [context, token_peer](dx::IntrinsicContext& c) {
            token_peer(c) = PaintPeer(*context, c.arguments[1].AsInt());
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "finalizer", "(I)V",
        [context](dx::IntrinsicContext& c) {
            context->skia_objects->paints.Retire(NativeToken<Paint>(c.arguments[0].AsInt()));
            return V::Void();
        },
        kStaticNative);

    b.VirtualMethod(
        "getFlags", "()I", [peer](dx::IntrinsicContext& c) { return V::Int(peer(c).flags); }, kPublicNative);
    b.VirtualMethod(
        "setFlags", "(I)V",
        [peer](dx::IntrinsicContext& c) {
            peer(c).SetFlags(c.arguments[0].AsInt());
            return V::Void();
        },
        kPublicNative);
    for (const auto& [name, bit] : {std::pair{"setAntiAlias", Paint::anti_alias},
                                    {"setFilterBitmap", Paint::filter_bitmap},
                                    {"setDither", Paint::dither},
                                    {"setUnderlineText", Paint::underline},
                                    {"setStrikeThruText", Paint::strike_thru},
                                    {"setFakeBoldText", Paint::fake_bold},
                                    {"setLinearText", Paint::linear_text},
                                    {"setSubpixelText", Paint::subpixel}}) {
        b.VirtualMethod(
            name, "(Z)V",
            [peer, bit](dx::IntrinsicContext& c) {
                auto& paint = peer(c);
                paint.SetFlags((paint.flags & ~bit) | (c.arguments[0].AsInt() ? bit : 0));
                return V::Void();
            },
            kPublicNative);
    }

    b.VirtualMethod(
        "getHinting", "()I", [peer](dx::IntrinsicContext& c) { return V::Int(peer(c).hinting); }, kPublicNative);
    b.VirtualMethod(
        "setHinting", "(I)V",
        [peer](dx::IntrinsicContext& c) {
            auto& paint = peer(c);
            paint.hinting = c.arguments[0].AsInt() != 0;
            paint.font.setHinting(paint.hinting ? SkFontHinting::kNormal : SkFontHinting::kNone);
            return V::Void();
        },
        kPublicNative);
    b.VirtualMethod(
        "getColor", "()I",
        [peer](dx::IntrinsicContext& c) { return V::Int(std::bit_cast<int>(peer(c).paint.getColor())); },
        kPublicNative);
    b.VirtualMethod(
        "setColor", "(I)V",
        [peer](dx::IntrinsicContext& c) {
            peer(c).paint.setColor(static_cast<SkColor>(c.arguments[0].AsInt()));
            return V::Void();
        },
        kPublicNative);
    b.VirtualMethod(
        "getAlpha", "()I", [peer](dx::IntrinsicContext& c) { return V::Int(peer(c).paint.getAlpha()); }, kPublicNative);
    b.VirtualMethod(
        "setAlpha", "(I)V",
        [peer](dx::IntrinsicContext& c) {
            peer(c).paint.setAlpha(static_cast<U8CPU>(c.arguments[0].AsInt() & 0xff));
            return V::Void();
        },
        kPublicNative);

    AddScalar(
        b, "getStrokeWidth", "setStrokeWidth", peer, [](const Paint& p) { return p.paint.getStrokeWidth(); },
        [](Paint& p, float value) { p.paint.setStrokeWidth(value); });
    AddScalar(
        b, "getStrokeMiter", "setStrokeMiter", peer, [](const Paint& p) { return p.paint.getStrokeMiter(); },
        [](Paint& p, float value) { p.paint.setStrokeMiter(value); });
    AddScalar(
        b, "getTextSize", "setTextSize", peer, [](const Paint& p) { return p.font.getSize(); },
        [](Paint& p, float value) {
            if (value >= 0) p.font.setSize(value);
        });
    AddScalar(
        b, "getTextScaleX", "setTextScaleX", peer, [](const Paint& p) { return p.font.getScaleX(); },
        [](Paint& p, float value) { p.font.setScaleX(value); });
    AddScalar(
        b, "getTextSkewX", "setTextSkewX", peer, [](const Paint& p) { return p.skew; },
        [](Paint& p, float value) {
            p.skew = value;
            p.SetTypeface(p.typeface);
        });

    AddEnum(b, context, "native_getStyle", "native_setStyle", &SkPaint::getStyle, &SkPaint::setStyle);
    AddEnum(b, context, "native_getStrokeCap", "native_setStrokeCap", &SkPaint::getStrokeCap, &SkPaint::setStrokeCap);
    AddEnum(b, context, "native_getStrokeJoin", "native_setStrokeJoin", &SkPaint::getStrokeJoin,
            &SkPaint::setStrokeJoin);
    b.StaticMethod(
        "native_getTextAlign", "(I)I", [token_peer](dx::IntrinsicContext& c) { return V::Int(token_peer(c).align); },
        kStaticNative);
    b.StaticMethod(
        "native_setTextAlign", "(II)V",
        [token_peer](dx::IntrinsicContext& c) {
            token_peer(c).align = c.arguments[1].AsInt();
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setTextLocale", "(ILjava/lang/String;)V",
        [token_peer](dx::IntrinsicContext& c) {
            token_peer(c).locale = c.vm.StringUtf8(c.arguments[1].ref);
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "native_setTypeface", "(II)I",
        [context, token_peer](dx::IntrinsicContext& c) {
            const auto token = c.arguments[1].AsInt();
            token_peer(c).SetTypeface(token ? Require(context->skia_objects->typefaces, token) : nullptr);
            return c.arguments[1];
        },
        kStaticNative);
    b.StaticMethod(
        "native_getFillPath", "(III)Z",
        [context, token_peer](dx::IntrinsicContext& c) {
            const auto source = Require(context->skia_objects->paths, c.arguments[1].AsInt()).snapshot();
            auto& target = Require(context->skia_objects->paths, c.arguments[2].AsInt());
            return V::Int(skpathutils::FillPathWithPaint(source, token_peer(c).paint, &target));
        },
        kStaticNative);

    const auto effect = [&]<class T>(const char* name, raster::Objects<T> raster::GraphicsObjects::* registry,
                                     auto set) {
        b.StaticMethod(
            name, "(II)I",
            [context, token_peer, registry, set](dx::IntrinsicContext& c) {
                const auto token = c.arguments[1].AsInt();
                auto& paint = token_peer(c);
                set(paint, token ? Require((*context->skia_objects).*registry, token) : T{});
                return c.arguments[1];
            },
            kStaticNative);
    };
    using Objects = raster::GraphicsObjects;
    effect("native_setShader", &Objects::shaders, [](Paint& p, auto v) { p.shader = std::move(v); });
    effect("native_setColorFilter", &Objects::color_filters, [](Paint& p, auto v) { p.paint.setColorFilter(v); });
    effect("native_setMaskFilter", &Objects::mask_filters, [](Paint& p, auto v) { p.paint.setMaskFilter(v); });
    effect("native_setPathEffect", &Objects::path_effects, [](Paint& p, auto v) { p.paint.setPathEffect(v); });
    b.StaticMethod(
        "native_setXfermode", "(II)I",
        [context, token_peer](dx::IntrinsicContext& c) {
            const auto token = c.arguments[1].AsInt();
            token_peer(c).paint.setBlendMode(token ? Require(context->skia_objects->xfermodes, token)
                                                   : SkBlendMode::kSrcOver);
            return c.arguments[1];
        },
        kStaticNative);
    b.StaticMethod(
        "native_setRasterizer", "(II)I",
        [](dx::IntrinsicContext& c) {
            if (c.arguments[1].AsInt()) RecordUiDegraded(c.vm, "Paint.rasterizer");
            return c.arguments[1];
        },
        kStaticNative);
    b.DirectMethod(
        "nSetShadowLayer", "(FFFI)V",
        [peer](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto sigma = raster::BlurSigma(a.Float(0));
            peer(c).paint.setImageFilter(sigma > 0 ? SkImageFilters::DropShadow(a.Float(1), a.Float(2), sigma, sigma,
                                                                                static_cast<SkColor>(a.Int(3)), nullptr)
                                                   : nullptr);
            return V::Void();
        },
        dx::kAccPrivate | dx::kAccNative);

    AddPaintTextMethods(b, context);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
