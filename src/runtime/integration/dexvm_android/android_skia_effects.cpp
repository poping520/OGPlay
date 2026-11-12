#include "android_skia_bitmap.h"

#include "include/core/SkImage.h"
#include "include/effects/Sk1DPathEffect.h"
#include "include/effects/SkCornerPathEffect.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkDiscretePathEffect.h"
#include "include/effects/SkGradient.h"

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;
using Shader = std::shared_ptr<raster::Shader>;

[[noreturn]] void Invalid(const char* message) {
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", message};
}

SkTileMode Tile(int mode) {
    if (mode < 0 || mode > 2) Invalid("invalid Shader.TileMode");
    return static_cast<SkTileMode>(mode);
}

SkBlendMode Blend(int mode) {
    if (mode < 0 || mode > 17) Invalid("invalid PorterDuff.Mode");
    return raster::PorterDuffBlend(mode);
}

auto SoftwareOnly() {
    // API19's post-create peers are only allocated with USE_OPENGL_RENDERER.
    return [](dx::IntrinsicContext&) { return V::Int(0); };
}

template <class T>
auto Destructor(const Context& context, raster::Objects<T> raster::GraphicsObjects::* registry) {
    return [context, registry](dx::IntrinsicContext& c) {
        ((*context->skia_objects).*registry).Retire(NativeToken<T>(c.arguments[0].AsInt()));
        return V::Void();
    };
}

V NewShader(DexVmAndroidContext& context, raster::Shader::Source source) {
    return JavaToken(context.skia_objects->shaders.Create(std::make_shared<raster::Shader>(std::move(source))));
}

enum class Gradient { linear, radial, sweep };

Decl DeclareGradientNatives(const Context& context, const char* name, Gradient kind, const char* array_signature,
                            const char* pair_signature) {
    auto b = dx::IntrinsicClassBuilder::Class(name);
    b.MergesBootClass();
    for (const bool array : {true, false}) {
        const auto sig = array ? array_signature : pair_signature;
        const std::string post = std::string("(I") + (sig + 1);
        const auto create_name = array ? "nativeCreate1" : "nativeCreate2";
        const auto post_name = array ? "nativePostCreate1" : "nativePostCreate2";
        auto create = [context, array, kind](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const std::size_t offset = kind == Gradient::linear ? 4 : kind == Gradient::radial ? 3 : 2;
            std::vector<SkColor4f> colors;
            std::vector<float> positions;
            const auto add_color = [&](auto color) {
                colors.push_back(SkColor4f::FromColor(static_cast<SkColor>(color)));
            };
            if (array) {
                const auto array_ref = a.Ref(offset);
                if (!array_ref.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "colors"};
                const auto count = c.vm.Model().ArrayLength(array_ref);
                if (count < 2) Invalid("gradient requires at least two colors");
                for (int i = 0; i < count; ++i)
                    add_color(c.vm.Model().GetPrimitiveElement(array_ref, i));
                if (const auto pos = a.Ref(offset + 1); pos.IsValid()) {
                    if (c.vm.Model().ArrayLength(pos) != count) Invalid("gradient color and position counts differ");
                    for (int i = 0; i < count; ++i)
                        positions.push_back(ReadFloat(c.vm, pos, i));
                }
            } else {
                add_color(a.Int(offset));
                add_color(a.Int(offset + 1));
            }
            const auto tile = kind == Gradient::sweep ? SkTileMode::kClamp : Tile(a.Int(offset + 2));
            const SkGradient gradient({{colors.data(), colors.size()}, {positions.data(), positions.size()}, tile}, {});
            const SkPoint origin = {a.Float(0), a.Float(1)};
            sk_sp<SkShader> shader;
            switch (kind) {
            case Gradient::linear: {
                const SkPoint points[]{origin, {a.Float(2), a.Float(3)}};
                shader = SkShaders::LinearGradient(points, gradient);
                break;
            }
            case Gradient::radial: shader = SkShaders::RadialGradient(origin, a.Float(2), gradient); break;
            case Gradient::sweep: shader = SkShaders::SweepGradient(origin, gradient); break;
            }
            if (!shader) Invalid("invalid gradient");
            return NewShader(*context, [shader = std::move(shader)](const SkSamplingOptions&) { return shader; });
        };
        if (kind == Gradient::linear) {
            b.DirectMethod(create_name, sig, std::move(create), dx::kAccPrivate | dx::kAccNative);
            b.DirectMethod(post_name, post.c_str(), SoftwareOnly(), dx::kAccPrivate | dx::kAccNative);
        } else {
            b.StaticMethod(create_name, sig, std::move(create), kStaticNative);
            b.StaticMethod(post_name, post.c_str(), SoftwareOnly(), kStaticNative);
        }
    }
    return std::move(b).Build();
}

template <class Create>
Decl DeclarePathEffectNatives(const Context& context, const char* type, const char* signature, Create create) {
    auto b = dx::IntrinsicClassBuilder::Class(type);
    b.MergesBootClass();
    b.StaticMethod(
        "nativeCreate", signature,
        [context, create](dx::IntrinsicContext& c) {
            // API19 constructs an effect even when its parameters make it a no-op.
            return JavaToken(context->skia_objects->path_effects.Create(create(c)));
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace

std::vector<Decl> Declare_android_graphics_Effects_Natives(const Context& context) {
    std::vector<Decl> result;
    auto shader = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Shader;");
    shader.MergesBootClass();
    shader.StaticMethod("nativeDestructor", "(II)V", Destructor(context, &raster::GraphicsObjects::shaders),
                        kStaticNative);
    shader.StaticMethod(
        "nativeSetLocalMatrix", "(III)V",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            if (a.Int(0))
                Require(context->skia_objects->shaders, a.Int(0))->matrix =
                    a.Int(2) ? Require(context->skia_objects->matrices, a.Int(2)) : SkMatrix::I();
            return V::Void();
        },
        kStaticNative);
    result.push_back(std::move(shader).Build());

    auto bitmap = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapShader;");
    bitmap.MergesBootClass();
    bitmap.StaticMethod(
        "nativeCreate", "(III)I",
        [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto source = Require(context->skia_objects->bitmaps, a.Int(0));
            const auto x = Tile(a.Int(1)), y = Tile(a.Int(2));
            const auto& pixels = source->SkiaBitmap();
            if (!pixels.getPixels()) Invalid("invalid BitmapShader bitmap");
            // Retain the original pixel ref, as the API19 SkBitmap copy does. It
            // shares writes and survives recycle or reconfiguration of the owner.
            return NewShader(*context, [source, pixels, x, y, image = sk_sp<SkImage>{},
                                        version = 0U](const SkSamplingOptions& sampling) mutable {
                if (source->Locks())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                          "BitmapShader pixels are locked by the NDK"};
                if (!image || version != pixels.getGenerationID()) {
                    image = SkImages::RasterFromBitmap(pixels);
                    if (!image) throw std::bad_alloc{};
                    version = pixels.getGenerationID();
                }
                return image->makeShader(x, y, sampling);
            });
        },
        kStaticNative);
    bitmap.StaticMethod("nativePostCreate", "(IIII)I", SoftwareOnly(), kStaticNative);
    result.push_back(std::move(bitmap).Build());
    result.push_back(DeclareGradientNatives(context, "Landroid/graphics/LinearGradient;", Gradient::linear,
                                            "(FFFF[I[FI)I", "(FFFFIII)I"));
    result.push_back(DeclareGradientNatives(context, "Landroid/graphics/RadialGradient;", Gradient::radial,
                                            "(FFF[I[FI)I", "(FFFIII)I"));
    result.push_back(
        DeclareGradientNatives(context, "Landroid/graphics/SweepGradient;", Gradient::sweep, "(FF[I[F)I", "(FFII)I"));

    auto compose = dx::IntrinsicClassBuilder::Class("Landroid/graphics/ComposeShader;");
    compose.MergesBootClass();
    for (bool porter_duff : {false, true}) {
        compose.StaticMethod(
            porter_duff ? "nativeCreate2" : "nativeCreate1", "(III)I",
            [context, porter_duff](dx::IntrinsicContext& c) {
                const dx::IntrinsicCall a(c);
                auto& objects = *context->skia_objects;
                const auto dst = Require(objects.shaders, a.Int(0)), src = Require(objects.shaders, a.Int(1));
                const auto mode = porter_duff ? Blend(a.Int(2))
                                  : a.Int(2)  ? Require(objects.xfermodes, a.Int(2))
                                              : SkBlendMode::kSrcOver;
                return NewShader(*context, [dst, src, mode](const SkSamplingOptions& sampling) {
                    return SkShaders::Blend(mode, dst->Resolve(sampling), src->Resolve(sampling));
                });
            },
            kStaticNative);
        compose.StaticMethod(porter_duff ? "nativePostCreate2" : "nativePostCreate1", "(IIII)I", SoftwareOnly(),
                             kStaticNative);
    }
    result.push_back(std::move(compose).Build());

    auto xfer = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Xfermode;");
    xfer.MergesBootClass();
    xfer.StaticMethod("finalizer", "(I)V", Destructor(context, &raster::GraphicsObjects::xfermodes), kStaticNative);
    result.push_back(std::move(xfer).Build());
    auto porter_duff = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PorterDuffXfermode;");
    porter_duff.MergesBootClass();
    porter_duff.StaticMethod(
        "nativeCreateXfermode", "(I)I",
        [context](dx::IntrinsicContext& c) {
            return JavaToken(context->skia_objects->xfermodes.Create(Blend(c.arguments[0].AsInt())));
        },
        kStaticNative);
    result.push_back(std::move(porter_duff).Build());

    auto filter = dx::IntrinsicClassBuilder::Class("Landroid/graphics/ColorFilter;");
    filter.MergesBootClass();
    filter.StaticMethod("finalizer", "(II)V", Destructor(context, &raster::GraphicsObjects::color_filters),
                        kStaticNative);
    result.push_back(std::move(filter).Build());
    for (const bool lighting : {false, true}) {
        auto b = dx::IntrinsicClassBuilder::Class(lighting ? "Landroid/graphics/LightingColorFilter;"
                                                           : "Landroid/graphics/PorterDuffColorFilter;");
        b.MergesBootClass();
        b.StaticMethod(
            lighting ? "native_CreateLightingFilter" : "native_CreatePorterDuffFilter", "(II)I",
            [context, lighting](dx::IntrinsicContext& c) {
                const auto a = static_cast<SkColor>(c.arguments[0].AsInt());
                const auto b = c.arguments[1].AsInt();
                return JavaToken(context->skia_objects->color_filters.Create(
                    lighting ? SkColorFilters::Lighting(a, static_cast<SkColor>(b))
                             : SkColorFilters::Blend(a, Blend(b))));
            },
            kStaticNative);
        b.StaticMethod(lighting ? "nCreateLightingFilter" : "nCreatePorterDuffFilter", "(III)I", SoftwareOnly(),
                       kStaticNative);
        result.push_back(std::move(b).Build());
    }
    auto matrix = dx::IntrinsicClassBuilder::Class("Landroid/graphics/ColorMatrixColorFilter;");
    matrix.MergesBootClass();
    matrix.StaticMethod(
        "nativeColorMatrixFilter", "([F)I",
        [context](dx::IntrinsicContext& c) {
            const auto array = c.arguments[0].ref;
            float values[20];
            CheckRange(c.vm, array, 0, 20);
            for (int i = 0; i < 20; ++i)
                values[i] = ReadFloat(c.vm, array, i) / (i % 5 == 4 ? 255.F : 1.F);
            return JavaToken(context->skia_objects->color_filters.Create(SkColorFilters::Matrix(values)));
        },
        kStaticNative);
    matrix.StaticMethod("nColorMatrixFilter", "(I[F)I", SoftwareOnly(), kStaticNative);
    result.push_back(std::move(matrix).Build());

    auto mask = dx::IntrinsicClassBuilder::Class("Landroid/graphics/MaskFilter;");
    mask.MergesBootClass();
    mask.StaticMethod("nativeDestructor", "(I)V", Destructor(context, &raster::GraphicsObjects::mask_filters),
                      kStaticNative);
    result.push_back(std::move(mask).Build());
    auto blur = dx::IntrinsicClassBuilder::Class("Landroid/graphics/BlurMaskFilter;");
    blur.MergesBootClass();
    blur.StaticMethod(
        "nativeConstructor", "(FI)I",
        [context](dx::IntrinsicContext& c) {
            const auto style = c.arguments[1].AsInt();
            if (style < 0 || style > 3) Invalid("invalid BlurMaskFilter.Blur");
            auto filter =
                SkMaskFilter::MakeBlur(static_cast<SkBlurStyle>(style), raster::BlurSigma(c.arguments[0].AsFloat()));
            if (!filter) Invalid("invalid BlurMaskFilter radius");
            return JavaToken(context->skia_objects->mask_filters.Create(std::move(filter)));
        },
        kStaticNative);
    result.push_back(std::move(blur).Build());

    auto path = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PathEffect;");
    path.MergesBootClass();
    path.StaticMethod("nativeDestructor", "(I)V", Destructor(context, &raster::GraphicsObjects::path_effects),
                      kStaticNative);
    result.push_back(std::move(path).Build());
    result.push_back(
        DeclarePathEffectNatives(context, "Landroid/graphics/CornerPathEffect;", "(F)I", [](dx::IntrinsicContext& c) {
            return SkCornerPathEffect::Make(c.arguments[0].AsFloat());
        }));
    result.push_back(DeclarePathEffectNatives(
        context, "Landroid/graphics/DiscretePathEffect;", "(FF)I", [](dx::IntrinsicContext& c) {
            return SkDiscretePathEffect::Make(c.arguments[0].AsFloat(), c.arguments[1].AsFloat());
        }));
    result.push_back(
        DeclarePathEffectNatives(context, "Landroid/graphics/DashPathEffect;", "([FF)I", [](dx::IntrinsicContext& c) {
            const auto array = c.arguments[0].ref;
            if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "intervals"};
            const int count = c.vm.Model().ArrayLength(array) & ~1;
            std::vector<float> intervals;
            for (int i = 0; i < count; ++i)
                intervals.push_back(ReadFloat(c.vm, array, i));
            return SkDashPathEffect::Make({intervals.data(), intervals.size()}, c.arguments[1].AsFloat());
        }));
    result.push_back(DeclarePathEffectNatives(
        context, "Landroid/graphics/PathDashPathEffect;", "(IFFI)I", [context](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            if (a.Int(3) < 0 || a.Int(3) > 2) Invalid("invalid PathDashPathEffect.Style");
            const auto shape = Require(context->skia_objects->paths, a.Int(0)).snapshot();
            return SkPath1DPathEffect::Make(shape, a.Float(1), a.Float(2),
                                            static_cast<SkPath1DPathEffect::Style>(a.Int(3)));
        }));
    for (const bool compose_effect : {false, true}) {
        result.push_back(DeclarePathEffectNatives(
            context, compose_effect ? "Landroid/graphics/ComposePathEffect;" : "Landroid/graphics/SumPathEffect;",
            "(II)I", [context, compose_effect](dx::IntrinsicContext& c) {
                auto& paths = context->skia_objects->path_effects;
                const auto get = [&](int bits) { return bits ? Require(paths, bits) : sk_sp<SkPathEffect>{}; };
                auto outer = get(c.arguments[0].AsInt()), inner = get(c.arguments[1].AsInt());
                return compose_effect ? SkPathEffect::MakeCompose(std::move(outer), std::move(inner))
                                      : SkPathEffect::MakeSum(std::move(outer), std::move(inner));
            }));
    }
    return result;
}
}  // namespace ogplay::runtime::android_intrinsics
