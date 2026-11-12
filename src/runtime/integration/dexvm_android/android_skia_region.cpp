#include "android_skia.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace ogplay::runtime::android_intrinsics {
namespace {
using namespace skia;
using V = dx::VmValue;

SkIRect RectArgs(const dx::IntrinsicCall& args, std::size_t start) {
    return SkIRect::MakeLTRB(args.Int(start), args.Int(start + 1), args.Int(start + 2), args.Int(start + 3));
}

SkRegion::Op RegionOp(int op) {
    if (op < SkRegion::kDifference_Op || op > SkRegion::kReplace_Op)
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid Region.Op"};
    return static_cast<SkRegion::Op>(op);
}

SkRegion ScaleRegion(const SkRegion& source, float scale) {
    if (!std::isfinite(scale)) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "non-finite Region scale"};
    const auto coord = [scale](int x) {
        const auto rounded = std::round(static_cast<float>(x) * scale);
        if (rounded >= static_cast<float>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
        if (rounded <= static_cast<float>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
        return static_cast<int>(rounded);
    };
    SkRegion out;
    for (SkRegion::Iterator it(source); !it.done(); it.next()) {
        const auto& r = it.rect();
        out.op(SkIRect::MakeLTRB(coord(r.left()), coord(r.top()), coord(r.right()), coord(r.bottom())),
               SkRegion::kUnion_Op);
    }
    return out;
}

std::string RegionString(const SkRegion& region) {
    std::string out{"Region("};
    for (SkRegion::Iterator it(region); !it.done(); it.next()) {
        const auto& r = it.rect();
        out += '(' + std::to_string(r.left()) + ',' + std::to_string(r.top()) + ',' + std::to_string(r.right()) + ',' +
               std::to_string(r.bottom()) + ')';
    }
    out += ')';
    return out;
}

}  // namespace

Decl Declare_android_graphics_Region_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Region;");
    b.MergesBootClass();
    auto* store = &context->skia_objects->regions;
    auto* paths = &context->skia_objects->paths;
    const auto get = [store](int token) -> SkRegion& { return Require(*store, token); };
    const auto from_object = [get](dx::Interpreter& vm, dx::VmObjectRef ref) -> SkRegion& {
        return get(IntField(vm, ref, "mNativeRegion"));
    };
    b.StaticMethod(
        "nativeConstructor", "()I", [store](dx::IntrinsicContext&) { return JavaToken(store->Create()); },
        kStaticNative);
    b.StaticMethod(
        "nativeDestructor", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->Retire(NativeToken<SkRegion>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeEquals", "(II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)) == get(a.Int(1)));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeGetBoundaryPath", "(II)Z",
        [get, paths](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto& region = get(a.Int(0));
            Require(*paths, a.Int(1)) = region.getBoundaryPath();
            return V::Int(!region.isEmpty());
        },
        kStaticNative);
    b.StaticMethod(
        "nativeGetBounds", "(ILandroid/graphics/Rect;)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto& region = get(a.Int(0));
            WriteRect(c.vm, a.Ref(1), region.getBounds());
            return V::Int(!region.isEmpty());
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSetPath", "(III)Z",
        [get, paths](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).setPath(Require(*paths, a.Int(1)).snapshot(), get(a.Int(2))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSetRect", "(IIIII)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).setRect(RectArgs(a, 1)));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeSetRegion", "(II)V",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            get(a.Int(0)) = get(a.Int(1));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeOp", "(IIII)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).op(get(a.Int(1)), get(a.Int(2)), RegionOp(a.Int(3))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeOp", "(IIIIII)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).op(RectArgs(a, 1), RegionOp(a.Int(5))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeOp", "(ILandroid/graphics/Rect;II)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(get(a.Int(0)).op(ReadRect(c.vm, a.Ref(1)), get(a.Int(2)), RegionOp(a.Int(3))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeToString", "(I)Ljava/lang/String;",
        [get](dx::IntrinsicContext& c) {
            return V::Ref(c.vm.NewStringUtf8(RegionString(get(dx::IntrinsicCall(c).Int(0)))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeCreateFromParcel", "(Landroid/os/Parcel;)I",
        [store](dx::IntrinsicContext& c) {
            const auto parcel = dx::IntrinsicCall(c).Ref(0);
            if (!parcel.IsValid()) return V::Int(0);
            const auto size = CallVirtual(c.vm, parcel, "readInt", "()I").AsInt();
            const auto available = CallVirtual(c.vm, parcel, "dataAvail", "()I").AsInt();
            if (size <= 0 || size % 4 != 0 || size > available)
                throw dx::VmJavaThrow{"Landroid/os/BadParcelableException;", "invalid Region data length"};
            std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4U);
            for (auto& word : words)
                word = std::bit_cast<std::uint32_t>(CallVirtual(c.vm, parcel, "readInt", "()I").AsInt());
            SkRegion region;
            if (region.readFromMemory(words.data(), static_cast<std::size_t>(size)) != static_cast<std::size_t>(size))
                throw dx::VmJavaThrow{"Landroid/os/BadParcelableException;", "invalid Region data"};
            return JavaToken(store->Create(region));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeWriteToParcel", "(ILandroid/os/Parcel;)Z",
        [get](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            const auto parcel = a.Ref(1);
            if (!parcel.IsValid()) return V::Int(false);
            const auto& region = get(a.Int(0));
            const auto size = region.writeToMemory(nullptr);
            if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::bad_alloc{};
            std::vector<std::uint32_t> words(size / 4U);
            region.writeToMemory(words.data());
            CallVirtual(c.vm, parcel, "writeInt", "(I)V", {V::Int(static_cast<int>(size))});
            for (auto word : words)
                CallVirtual(c.vm, parcel, "writeInt", "(I)V", {V::Int(std::bit_cast<std::int32_t>(word))});
            return V::Int(true);
        },
        kStaticNative);
    constexpr auto instance = dx::kAccPublic | dx::kAccNative;
    for (const auto& [name, query] : {std::pair{"isEmpty", &SkRegion::isEmpty},
                                      {"isRect", &SkRegion::isRect},
                                      {"isComplex", &SkRegion::isComplex}}) {
        b.VirtualMethod(
            name, "()Z",
            [from_object, query](dx::IntrinsicContext& c) { return V::Int((from_object(c.vm, c.receiver).*query)()); },
            instance);
    }
    b.VirtualMethod(
        "contains", "(II)Z",
        [from_object](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            return V::Int(from_object(c.vm, c.receiver).contains(a.Int(0), a.Int(1)));
        },
        instance);
    b.VirtualMethod(
        "quickContains", "(IIII)Z",
        [from_object](dx::IntrinsicContext& c) {
            return V::Int(from_object(c.vm, c.receiver).quickContains(RectArgs(dx::IntrinsicCall(c), 0)));
        },
        instance);
    b.VirtualMethod(
        "quickReject", "(IIII)Z",
        [from_object](dx::IntrinsicContext& c) {
            return V::Int(from_object(c.vm, c.receiver).quickReject(RectArgs(dx::IntrinsicCall(c), 0)));
        },
        instance);
    b.VirtualMethod(
        "quickReject", "(Landroid/graphics/Region;)Z",
        [from_object](dx::IntrinsicContext& c) {
            return V::Int(from_object(c.vm, c.receiver).quickReject(from_object(c.vm, dx::IntrinsicCall(c).Ref(0))));
        },
        instance);
    b.VirtualMethod(
        "translate", "(IILandroid/graphics/Region;)V",
        [from_object](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& source = from_object(c.vm, c.receiver);
            auto& dest = a.Ref(2).IsValid() ? from_object(c.vm, a.Ref(2)) : source;
            source.translate(a.Int(0), a.Int(1), &dest);
            return V::Void();
        },
        instance);
    b.VirtualMethod(
        "scale", "(FLandroid/graphics/Region;)V",
        [from_object](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& source = from_object(c.vm, c.receiver);
            auto& dest = a.Ref(1).IsValid() ? from_object(c.vm, a.Ref(1)) : source;
            dest = ScaleRegion(source, a.Float(0));
            return V::Void();
        },
        instance);
    return std::move(b).Build();
}

Decl Declare_android_graphics_RegionIterator_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/RegionIterator;");
    b.MergesBootClass();
    auto* store = &context->skia_objects->region_iterators;
    auto* regions = &context->skia_objects->regions;
    b.StaticMethod(
        "nativeConstructor", "(I)I",
        [store, regions](dx::IntrinsicContext& c) {
            return JavaToken(store->Create(Require(*regions, dx::IntrinsicCall(c).Int(0))));
        },
        kStaticNative);
    b.StaticMethod(
        "nativeDestructor", "(I)V",
        [store](dx::IntrinsicContext& c) {
            store->Retire(NativeToken<raster::RegionIterator>(dx::IntrinsicCall(c).Int(0)));
            return V::Void();
        },
        kStaticNative);
    b.StaticMethod(
        "nativeNext", "(ILandroid/graphics/Rect;)Z",
        [store](dx::IntrinsicContext& c) {
            const dx::IntrinsicCall a(c);
            auto& it = Require(*store, a.Int(0)).iterator;
            if (it.done()) return V::Int(false);
            WriteRect(c.vm, a.Ref(1), it.rect());
            it.next();
            return V::Int(true);
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_view_GLES20Canvas_Natives(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/GLES20Canvas;", "Landroid/view/HardwareCanvas;");
    b.MergesBootClass();
    b.StaticMethod("nIsAvailable", "()Z", [](dx::IntrinsicContext&) { return V::Int(false); }, kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
