#include "catalog.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ogplay::runtime {
struct GraphicsStateStore final {
    struct Handle final {
        std::uint32_t value{};
        auto operator<=>(const Handle&) const = default;
    };

    struct Frame final {
        std::uint32_t time{};
        std::array<float, 4> blend{0.33333333F, 0.33333333F, 0.66666667F, 0.66666667F};
        std::vector<float> values;
        bool assigned{};
    };

    struct Interpolation final {
        int count{};
        std::vector<Frame> frames;
        float repeat{1};
        bool mirror{};
    };

    struct Gradient final {
        std::array<float, 4> points{};
        std::vector<std::uint32_t> colors;
        std::vector<float> positions;
        int tile{};
    };

    struct BitmapPattern final {
        std::shared_ptr<BitmapPixels> pixels;
        int tile_x{}, tile_y{};
    };

    std::map<Handle, BitmapPattern> bitmap_patterns;
    std::uint32_t next{1};
    std::map<Handle, Interpolation> interpolators;
    std::map<Handle, Gradient> gradients;
    std::map<Handle, int> xfermodes;
    // LightingColorFilter: Skia filter handle -> (mul, add); HWUI handle -> Skia handle.
    std::map<Handle, std::pair<std::int32_t, std::int32_t>> lighting_filters;
    std::map<Handle, Handle> lighting_filter_views;

    Handle New() {
        if (next == std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("graphics handle space exhausted");
        return Handle{next++};
    }
};
}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
namespace {
using Store = GraphicsStateStore;
using Handle = Store::Handle;
constexpr auto native_static = dx::kAccPrivate | dx::kAccStatic | dx::kAccNative;
constexpr auto native_instance = dx::kAccPrivate | dx::kAccNative;

void Invalid(const char* text) {
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", text};
}

Handle Id(const dx::VmValue& value) {
    return Handle{static_cast<std::uint32_t>(value.AsInt())};
}

float Element(dx::IntrinsicContext& c, dx::VmObjectRef array, int index) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(c.vm.Model().GetPrimitiveElement(array, index)));
}

void Array(dx::IntrinsicContext& c, dx::VmObjectRef array, int size) {
    if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "graphics values array is null"};
    if (c.vm.Model().ArrayLength(array) < size) Invalid("graphics values array too short");
}

Store::Interpolation NewInterpolation(int values, int frames) {
    if (values <= 0 || values > 255 || frames <= 0 || frames > 32767)
        Invalid("Interpolator dimensions exceed API19 storage");
    return {values, std::vector<Store::Frame>(static_cast<std::size_t>(frames)), 1, false};
}

// AOSP android-4.4.4_r2 SkInterpolator.cpp: SkUnitCubicInterp uses 2.14
// fixed arithmetic and thirteen binary search steps, including the default blend.
float Cubic(float x, const std::array<float, 4>& b) {
    const auto pin = [](float v) { return v <= 0 ? 0 : v >= 1 ? 16384 : static_cast<int>(v * 65536.0F) >> 2; };
    const auto mul = [](int a, int c) { return (a * c + 8192) >> 14; };
    const auto eval = [&](int t, int a, int c) {
        const int A = 3 * a, B = 3 * (c - 2 * a), C = 3 * (a - c) + 16384;
        return mul(mul(mul(C, t) + B, t) + A, t);
    };
    const int value = pin(x);
    if (!value || value == 16384) return static_cast<float>(value) / 16384.0F;
    int t = 8192, dt = 8192;
    for (int i = 0; i < 13; ++i) {
        dt >>= 1;
        t += value < eval(t, pin(b[0]), pin(b[2])) ? -dt : dt;
    }
    return static_cast<float>(eval(t, pin(b[1]), pin(b[3]))) / 16384.0F;
}

int Sample(const Store::Interpolation& state, std::uint32_t time, std::vector<float>& values) {
    const auto& frames = state.frames;
    if (std::any_of(frames.begin(), frames.end(), [](const auto& f) { return !f.assigned; }))
        Invalid("Interpolator keyframes are incomplete");
    int result = 0;
    if (state.repeat != 1) {
        const auto start = frames.front().time, duration = frames.back().time - start;
        auto offset = time - start;
        const auto end = static_cast<std::uint64_t>(std::floor(static_cast<double>(state.repeat) * duration));
        if (offset >= end) {
            const auto fraction = state.repeat - std::floor(state.repeat);
            offset = fraction == 0 && state.repeat > 0
                         ? duration
                         : static_cast<std::uint32_t>(std::floor(static_cast<double>(fraction) * duration));
            result = 2;
        } else if (duration) {
            const auto period = static_cast<std::uint64_t>(duration) * (state.mirror ? 2 : 1);
            offset = static_cast<std::uint32_t>(offset % period);
            if (offset > duration) offset = static_cast<std::uint32_t>(2ULL * duration - offset);
        }
        time = start + offset;
    }
    auto next = std::lower_bound(frames.begin(), frames.end(), time, [](const auto& f, auto t) { return f.time < t; });
    if (next == frames.end()) {
        values = frames.back().values;
        return 2;
    }
    if (next->time == time) {
        values = next->values;
        return result;
    }
    if (next == frames.begin()) {
        values = next->values;
        return 1;
    }
    const auto& previous = *(next - 1);
    const float t = Cubic(static_cast<float>(time - previous.time) / static_cast<float>(next->time - previous.time),
                          previous.blend);
    values.resize(static_cast<std::size_t>(state.count));
    for (int i = 0; i < state.count; ++i)
        values[static_cast<std::size_t>(i)] =
            previous.values[static_cast<std::size_t>(i)] +
            (next->values[static_cast<std::size_t>(i)] - previous.values[static_cast<std::size_t>(i)]) * t;
    return result;
}
}  // namespace

std::vector<Decl> Declare_android_graphics_StateObjects(const Context& context) {
    auto store = std::make_shared<Store>();
    context->graphics_state_store = store;
    std::vector<Decl> result;
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Interpolator;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeConstructor", "(II)I",
        [store](dx::IntrinsicContext& c) {
            auto state = NewInterpolation(c.arguments[0].AsInt(), c.arguments[1].AsInt());
            const auto id = store->New();
            store->interpolators.emplace(id, std::move(state));
            return dx::VmValue::Int(static_cast<int>(id.value));
        },
        native_static);
    b.StaticMethod(
        "nativeReset", "(III)V",
        [store](dx::IntrinsicContext& c) {
            store->interpolators.at(Id(c.arguments[0])) =
                NewInterpolation(c.arguments[1].AsInt(), c.arguments[2].AsInt());
            return dx::VmValue::Void();
        },
        native_static);
    b.StaticMethod(
        "nativeDestructor", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->interpolators.erase(Id(c.arguments[0]));
            return dx::VmValue::Void();
        },
        native_static);
    b.StaticMethod(
        "nativeSetKeyFrame", "(III[F[F)V",
        [store](dx::IntrinsicContext& c) {
            auto& state = store->interpolators.at(Id(c.arguments[0]));
            const auto index = c.arguments[1].AsInt();
            const auto time = static_cast<std::uint32_t>(c.arguments[2].AsInt());
            if (index < 0 || static_cast<std::size_t>(index) >= state.frames.size())
                Invalid("Interpolator frame index");
            for (int i = 0; i < index; ++i)
                if (!state.frames[static_cast<std::size_t>(i)].assigned ||
                    state.frames[static_cast<std::size_t>(i)].time >= time)
                    Invalid("Interpolator times must be increasing");
            const auto input = c.arguments[3].ref, blend = c.arguments[4].ref;
            Array(c, input, state.count);
            if (blend.IsValid()) Array(c, blend, 4);
            Store::Frame frame;
            frame.time = time;
            frame.assigned = true;
            for (int i = 0; i < state.count; ++i)
                frame.values.push_back(Element(c, input, i));
            if (blend.IsValid())
                for (int i = 0; i < 4; ++i)
                    frame.blend[static_cast<std::size_t>(i)] = Element(c, blend, i);
            state.frames[static_cast<std::size_t>(index)] = std::move(frame);
            return dx::VmValue::Void();
        },
        native_static);
    b.StaticMethod(
        "nativeSetRepeatMirror", "(IFZ)V",
        [store](dx::IntrinsicContext& c) {
            auto& state = store->interpolators.at(Id(c.arguments[0]));
            const auto repeat = c.arguments[1].AsFloat();
            if (!std::isfinite(repeat) || repeat < 0) Invalid("invalid Interpolator repeat");
            state.repeat = std::min(repeat, 32000.0F);
            state.mirror = c.arguments[2].AsInt() != 0;
            return dx::VmValue::Void();
        },
        native_static);
    b.StaticMethod(
        "nativeTimeToValues", "(II[F)I",
        [store](dx::IntrinsicContext& c) {
            const auto& state = store->interpolators.at(Id(c.arguments[0]));
            const auto output = c.arguments[2].ref;
            if (output.IsValid()) Array(c, output, state.count);
            std::vector<float> values;
            const auto result = Sample(state, static_cast<std::uint32_t>(c.arguments[1].AsInt()), values);
            if (output.IsValid())
                for (int i = 0; i < state.count; ++i)
                    c.vm.Model().SetPrimitiveElement(output, i,
                                                     std::bit_cast<std::uint32_t>(values[static_cast<std::size_t>(i)]));
            return dx::VmValue::Int(result);
        },
        native_static);
    result.push_back(std::move(b).Build());
    auto gradient = dx::IntrinsicClassBuilder::Class("Landroid/graphics/LinearGradient;", "Landroid/graphics/Shader;");
    gradient.MergesBootClass();
    for (bool array : {false, true}) {
        const char* signature = array ? "(FFFF[I[FI)I" : "(FFFFIII)I";
        gradient.DirectMethod(
            array ? "nativeCreate1" : "nativeCreate2", signature,
            [store, array](dx::IntrinsicContext& c) {
                Store::Gradient value;
                for (int i = 0; i < 4; ++i)
                    value.points[static_cast<std::size_t>(i)] = c.arguments[static_cast<std::size_t>(i)].AsFloat();
                value.tile = c.arguments[6].AsInt();
                if (value.tile < 0 || value.tile > 2) Invalid("gradient tile mode");
                if (array) {
                    const auto colors = c.arguments[4].ref, positions = c.arguments[5].ref;
                    Array(c, colors, 2);
                    const auto count = c.vm.Model().ArrayLength(colors);
                    if (positions.IsValid()) Array(c, positions, count);
                    for (int i = 0; i < count; ++i) {
                        value.colors.push_back(static_cast<std::uint32_t>(c.vm.Model().GetPrimitiveElement(colors, i)));
                        if (positions.IsValid()) value.positions.push_back(Element(c, positions, i));
                    }
                } else {
                    value.colors = {static_cast<std::uint32_t>(c.arguments[4].AsInt()),
                                    static_cast<std::uint32_t>(c.arguments[5].AsInt())};
                }
                const auto id = store->New();
                store->gradients.emplace(id, std::move(value));
                return dx::VmValue::Int(static_cast<int>(id.value));
            },
            native_instance);
        gradient.DirectMethod(
            array ? "nativePostCreate1" : "nativePostCreate2", array ? "(IFFFF[I[FI)I" : "(IFFFFIII)I",
            [store](dx::IntrinsicContext& c) {
                const auto id = Id(c.arguments[0]);
                static_cast<void>(store->gradients.at(id));
                return dx::VmValue::Int(static_cast<int>(id.value));
            },
            native_instance);
    }
    result.push_back(std::move(gradient).Build());
    auto shape = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/ShapeDrawable;",
                                                  "Landroid/graphics/drawable/Drawable;");
    shape.MergesBootClass();
    shape.DirectMethod(
        "updateShape", "()V",
        [](dx::IntrinsicContext& c) {
            const auto ref = [&c](dx::VmObjectRef owner, const char* name, const char* type) {
                const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(owner), name, type);
                if (!field) throw std::logic_error("ROM ShapeDrawable field unavailable");
                return dx::VmObjectRef{c.vm.Model().InstanceSlots(owner)[c.vm.Linker().Field(*field).slot].bits};
            };
            const auto state = ref(c.receiver, "mShapeState", "Landroid/graphics/drawable/ShapeDrawable$ShapeState;");
            if (ref(state, "mShaderFactory", "Landroid/graphics/drawable/ShapeDrawable$ShaderFactory;").IsValid()) {
                if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.progressbar_tiling", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "ShapeDrawable ShaderFactory is unsupported"};
            }
            const auto value = ref(state, "mShape", "Landroid/graphics/drawable/shapes/Shape;");
            if (value.IsValid()) {
                const auto bounds = CallAndroidMethod(c.vm, c.receiver, "getBounds", "()Landroid/graphics/Rect;").ref;
                static_cast<void>(CallAndroidMethod(
                    c.vm, value, "resize", "(FF)V",
                    {dx::VmValue::Float(static_cast<float>(CallAndroidMethod(c.vm, bounds, "width", "()I").AsInt())),
                     dx::VmValue::Float(
                         static_cast<float>(CallAndroidMethod(c.vm, bounds, "height", "()I").AsInt()))}));
            }
            static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidateSelf", "()V"));
            return dx::VmValue::Void();
        },
        dx::kAccPrivate);
    result.push_back(std::move(shape).Build());
    auto bitmap_shader =
        dx::IntrinsicClassBuilder::Class("Landroid/graphics/BitmapShader;", "Landroid/graphics/Shader;");
    bitmap_shader.MergesBootClass();
    const auto bitmap_field =
        bitmap_shader.BoundInstanceField("mBitmap", "Landroid/graphics/Bitmap;", dx::kAccPublic | dx::kAccFinal);
    const auto tile_x =
        bitmap_shader.BoundInstanceField("mTileX", "Landroid/graphics/Shader$TileMode;", dx::kAccPrivate);
    const auto tile_y =
        bitmap_shader.BoundInstanceField("mTileY", "Landroid/graphics/Shader$TileMode;", dx::kAccPrivate);
    bitmap_shader.Constructor(
        "(Landroid/graphics/Bitmap;Landroid/graphics/Shader$TileMode;Landroid/graphics/Shader$TileMode;)V",
        [context, store, bitmap_field, tile_x, tile_y](dx::IntrinsicContext& c) {
            for (const auto& arg : c.arguments)
                if (!arg.ref.IsValid())
                    throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "BitmapShader bitmap/tile mode is null"};
            const auto found = context->bitmaps.find(c.arguments[0].ref.Value());
            if (found == context->bitmaps.end() || found->second.recycled || !found->second.pixels)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "BitmapShader bitmap is unavailable"};
            const auto read_mode = [&c](dx::VmObjectRef mode) {
                const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(mode), "nativeInt", "I");
                const auto value =
                    static_cast<std::int32_t>(c.vm.Model().InstanceSlots(mode)[c.vm.Linker().Field(*field).slot].bits);
                if (value < 0 || value > 2) Invalid("BitmapShader tile mode");
                return value;
            };
            const auto x = read_mode(c.arguments[1].ref), y = read_mode(c.arguments[2].ref);
            const dx::IntrinsicCall fields(c);
            fields.SetRef(bitmap_field, c.arguments[0].ref);
            fields.SetRef(tile_x, c.arguments[1].ref);
            fields.SetRef(tile_y, c.arguments[2].ref);
            const auto handle = store->New();
            for (const auto name : {"native_instance", "native_shader"}) {
                const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(c.receiver), name, "I");
                c.vm.Model().InstanceSlots(c.receiver)[c.vm.Linker().Field(*field).slot] = {handle.value,
                                                                                            dx::SlotTag::cat1};
            }
            store->bitmap_patterns.emplace(handle, Store::BitmapPattern{found->second.pixels, x, y});
            return dx::VmValue::Void();
        });
    result.push_back(std::move(bitmap_shader).Build());
    auto shader = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Shader;");
    shader.MergesBootClass();
    shader.StaticMethod(
        "nativeDestructor", "(II)V",
        [store](dx::IntrinsicContext& c) {
            store->gradients.erase(Id(c.arguments[0]));
            store->bitmap_patterns.erase(Id(c.arguments[0]));
            return dx::VmValue::Void();
        },
        native_static);
    result.push_back(std::move(shader).Build());
    auto xfer =
        dx::IntrinsicClassBuilder::Class("Landroid/graphics/PorterDuffXfermode;", "Landroid/graphics/Xfermode;");
    xfer.MergesBootClass();
    xfer.StaticMethod(
        "nativeCreateXfermode", "(I)I",
        [store](dx::IntrinsicContext& c) {
            const auto mode = c.arguments[0].AsInt();
            if (mode < 0 || mode > 17) Invalid("PorterDuff mode");
            const auto id = store->New();
            store->xfermodes.emplace(id, mode);
            return dx::VmValue::Int(static_cast<int>(id.value));
        },
        native_static);
    result.push_back(std::move(xfer).Build());
    auto base = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Xfermode;");
    base.MergesBootClass();
    base.StaticMethod(
        "finalizer", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->xfermodes.erase(Id(c.arguments[0]));
            return dx::VmValue::Void();
        },
        native_static);
    result.push_back(std::move(base).Build());
    // API19 LightingColorFilter keeps a Skia filter and an HWUI view of it. Only
    // the parameters are retained; no color arithmetic or drawing consumes them.
    auto lighting =
        dx::IntrinsicClassBuilder::Class("Landroid/graphics/LightingColorFilter;", "Landroid/graphics/ColorFilter;");
    lighting.MergesBootClass();
    lighting.StaticMethod(
        "native_CreateLightingFilter", "(II)I",
        [store](dx::IntrinsicContext& c) {
            const auto id = store->New();
            store->lighting_filters.emplace(id, std::pair{c.arguments[0].AsInt(), c.arguments[1].AsInt()});
            return dx::VmValue::Int(static_cast<int>(id.value));
        },
        native_static);
    lighting.StaticMethod(
        "nCreateLightingFilter", "(III)I",
        [store](dx::IntrinsicContext& c) {
            const auto source = Id(c.arguments[0]);
            const auto found = store->lighting_filters.find(source);
            if (found == store->lighting_filters.end() ||
                found->second != std::pair{c.arguments[1].AsInt(), c.arguments[2].AsInt()})
                Invalid("LightingColorFilter native filter");
            const auto id = store->New();
            store->lighting_filter_views.emplace(id, source);
            return dx::VmValue::Int(static_cast<int>(id.value));
        },
        native_static);
    result.push_back(std::move(lighting).Build());
    auto filter = dx::IntrinsicClassBuilder::Class("Landroid/graphics/ColorFilter;");
    filter.MergesBootClass();
    filter.StaticMethod(
        "finalizer", "(II)V",
        [store](dx::IntrinsicContext& c) {
            store->lighting_filters.erase(Id(c.arguments[0]));
            store->lighting_filter_views.erase(Id(c.arguments[1]));
            return dx::VmValue::Void();
        },
        native_static);
    result.push_back(std::move(filter).Build());
    return result;
}

void RequirePlainCanvasPaint(dx::IntrinsicContext& call, dx::VmObjectRef paint) {
    if (!paint.IsValid()) return;
    for (const auto name : {"mShader", "mXfermode"}) {
        const auto type =
            name == std::string_view("mShader") ? "Landroid/graphics/Shader;" : "Landroid/graphics/Xfermode;";
        const auto field = call.vm.Linker().FindFieldRecursive(call.vm.Model().ObjectClass(paint), name, type);
        if (field && call.vm.Model().InstanceSlots(paint)[call.vm.Linker().Field(*field).slot].bits) {
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.canvas_draw", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "Canvas shader/xfermode drawing is unsupported"};
        }
    }
}

void RegisterGraphicsStateTables(dx::Interpreter& vm, const Context& context) {
    const auto store = context->graphics_state_store;
    vm.RegisterIntrinsicStateTable(
        {"android.graphics-state",
         [](dx::VmObjectRef, const dx::VmRootVisitor&) {},
         [store, &vm](dx::VmObjectRef object) {
             const auto type = vm.Model().ObjectClass(object);
             const auto& descriptor = vm.Linker().Class(type).descriptor;
             const bool bitmap_shader =
                 vm.Linker().IsAssignable(vm.Linker().ResolveDescriptor("Landroid/graphics/BitmapShader;"), type);
             if (descriptor != "Landroid/graphics/Interpolator;" && descriptor != "Landroid/graphics/LinearGradient;" &&
                 !bitmap_shader && descriptor != "Landroid/graphics/PorterDuffXfermode;" &&
                 descriptor != "Landroid/graphics/LightingColorFilter;")
                 return;
             const auto field = vm.Linker().FindFieldRecursive(type, "native_instance", "I");
             if (!field) return;
             const Handle handle{vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot].bits};
             if (descriptor == "Landroid/graphics/Interpolator;") store->interpolators.erase(handle);
             if (descriptor == "Landroid/graphics/LinearGradient;") store->gradients.erase(handle);
             if (bitmap_shader) store->bitmap_patterns.erase(handle);
             if (descriptor == "Landroid/graphics/PorterDuffXfermode;") store->xfermodes.erase(handle);
             if (descriptor == "Landroid/graphics/LightingColorFilter;") {
                 store->lighting_filters.erase(handle);
                 if (const auto view = vm.Linker().FindFieldRecursive(type, "nativeColorFilter", "I"))
                     store->lighting_filter_views.erase(
                         Handle{vm.Model().InstanceSlots(object)[vm.Linker().Field(*view).slot].bits});
             }
         },
         {}});
}
}  // namespace ogplay::runtime::android_intrinsics
