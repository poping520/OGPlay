#include "android_skia.h"

#include <array>
#include <vector>

namespace ogplay::runtime::android_intrinsics::skia {

dx::Slot& Field(dx::Interpreter& vm, dx::VmObjectRef owner, const char* name, const char* descriptor) {
    if (!owner.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", name};
    const auto id = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(owner), name, descriptor);
    if (!id) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, name);
    vm.RequireImplementedField(*id);
    return vm.Model().InstanceSlots(owner)[vm.Linker().Field(*id).slot];
}

std::int32_t IntField(dx::Interpreter& vm, dx::VmObjectRef owner, const char* name) {
    return std::bit_cast<std::int32_t>(Field(vm, owner, name, "I").bits);
}

SkRect ReadRectF(dx::Interpreter& vm, dx::VmObjectRef owner) {
    const auto get = [&](const char* name) { return std::bit_cast<float>(Field(vm, owner, name, "F").bits); };
    return {get("left"), get("top"), get("right"), get("bottom")};
}

SkIRect ReadRect(dx::Interpreter& vm, dx::VmObjectRef owner) {
    return {IntField(vm, owner, "left"), IntField(vm, owner, "top"), IntField(vm, owner, "right"),
            IntField(vm, owner, "bottom")};
}

void WriteRectF(dx::Interpreter& vm, dx::VmObjectRef owner, const SkRect& rect) {
    Field(vm, owner, "left", "F") = {std::bit_cast<std::uint32_t>(rect.left()), dx::SlotTag::cat1};
    Field(vm, owner, "top", "F") = {std::bit_cast<std::uint32_t>(rect.top()), dx::SlotTag::cat1};
    Field(vm, owner, "right", "F") = {std::bit_cast<std::uint32_t>(rect.right()), dx::SlotTag::cat1};
    Field(vm, owner, "bottom", "F") = {std::bit_cast<std::uint32_t>(rect.bottom()), dx::SlotTag::cat1};
}

void WriteRect(dx::Interpreter& vm, dx::VmObjectRef owner, const SkIRect& rect) {
    Field(vm, owner, "left", "I") = {std::bit_cast<std::uint32_t>(rect.left()), dx::SlotTag::cat1};
    Field(vm, owner, "top", "I") = {std::bit_cast<std::uint32_t>(rect.top()), dx::SlotTag::cat1};
    Field(vm, owner, "right", "I") = {std::bit_cast<std::uint32_t>(rect.right()), dx::SlotTag::cat1};
    Field(vm, owner, "bottom", "I") = {std::bit_cast<std::uint32_t>(rect.bottom()), dx::SlotTag::cat1};
}

void CheckRange(dx::Interpreter& vm, dx::VmObjectRef array, int start, std::int64_t length) {
    if (!array.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "graphics array"};
    if (start < 0 || length < 0 || start + length > vm.Model().ArrayLength(array))
        throw dx::VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;", "graphics array range"};
}

float ReadFloat(dx::Interpreter& vm, dx::VmObjectRef array, int index) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, index)));
}

void WriteFloat(dx::Interpreter& vm, dx::VmObjectRef array, int index, float value) {
    vm.Model().SetPrimitiveElement(array, index, std::bit_cast<std::uint32_t>(value));
}

dx::VmValue CallVirtual(dx::Interpreter& vm, dx::VmObjectRef receiver, const char* name, const char* signature,
                        std::initializer_list<dx::VmValue> arguments) {
    const auto type = vm.Model().ObjectClass(receiver);
    const auto index = vm.Linker().FindVtableIndex(type, name, signature);
    if (!index) throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, name);
    std::vector<dx::VmValue> values{dx::VmValue::Ref(receiver)};
    values.insert(values.end(), arguments);
    const auto outcome = vm.Call(vm.Linker().Class(type).vtable[*index], values);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
    return outcome.value;
}

}  // namespace ogplay::runtime::android_intrinsics::skia

namespace ogplay::runtime::android_intrinsics {

void RegisterSkiaStateTables(dx::Interpreter& vm, const Context& context) {
    vm.RegisterIntrinsicStateTable(
        {"android.skia",
         {},
         [context, &vm](dx::VmObjectRef owner) {
             if (vm.Model().Kind(owner) != dx::VmObjectKind::vm_instance) return;
             const auto retire = [&]<class T>(raster::Objects<T>& objects, const char* field) {
                 objects.Retire(skia::NativeToken<T>(skia::IntField(vm, owner, field)));
             };
             auto& objects = *context->skia_objects;
             for (auto type = std::optional{vm.Model().ObjectClass(owner)}; type && type->IsValid();) {
                 const auto& klass = vm.Linker().Class(*type);
                 const auto& name = klass.descriptor;
                 if (!name.starts_with("Landroid/graphics/")) {
                     type = klass.super;
                     continue;
                 }
                 if (name == "Landroid/graphics/Bitmap$BitmapFinalizer;")
                     return retire(objects.bitmaps, "mNativeBitmap");
                 if (name == "Landroid/graphics/Canvas$CanvasFinalizer;")
                     return retire(objects.canvases, "mNativeCanvas");
                 if (name == "Landroid/graphics/DrawFilter;") return retire(objects.draw_filters, "mNativeInt");
                 if (name == "Landroid/graphics/Typeface;")
                     return RetireAndroidTypeface(*context, skia::IntField(vm, owner, "native_instance"));
                 if (name == "Landroid/graphics/Shader;") return retire(objects.shaders, "native_instance");
                 if (name == "Landroid/graphics/ColorFilter;") return retire(objects.color_filters, "native_instance");
                 if (name == "Landroid/graphics/MaskFilter;") return retire(objects.mask_filters, "native_instance");
                 if (name == "Landroid/graphics/PathEffect;") return retire(objects.path_effects, "native_instance");
                 if (name == "Landroid/graphics/Xfermode;") return retire(objects.xfermodes, "native_instance");
                 if (name == "Landroid/graphics/NinePatch;")
                     return RetireAndroidNinePatch(vm, *context, skia::IntField(vm, owner, "mNativeChunk"), owner);
                 if (name == "Landroid/graphics/Camera;") return retire(objects.cameras, "native_instance");
                 if (name == "Landroid/graphics/Paint;") return retire(objects.paints, "mNativePaint");
                 if (name == "Landroid/graphics/Matrix;") return retire(objects.matrices, "native_instance");
                 if (name == "Landroid/graphics/Path;") return retire(objects.paths, "mNativePath");
                 if (name == "Landroid/graphics/PathMeasure;") return retire(objects.measures, "native_instance");
                 if (name == "Landroid/graphics/Region;") return retire(objects.regions, "mNativeRegion");
                 if (name == "Landroid/graphics/RegionIterator;")
                     return retire(objects.region_iterators, "mNativeIter");
                 type = klass.super;
             }
         },
         {}});
}

std::optional<std::array<float, 9>> AndroidMatrixSnapshot(DexVmAndroidContext& context, int token) {
    if (!token) return std::nullopt;
    std::array<float, 9> values;
    skia::Require(context.skia_objects->matrices, token).get9(values.data());
    return values;
}

bool AndroidMatrixIsIdentity(dx::Interpreter& vm, const DexVmAndroidContext& context, dx::VmObjectRef matrix) {
    if (!matrix.IsValid()) return false;
    const auto bits = skia::IntField(vm, matrix, "native_instance");
    const auto* native = context.skia_objects->matrices.Find(skia::NativeToken<SkMatrix>(bits));
    return native && native->isIdentity();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void ReleaseAndroidRasterResources(DexVmAndroidContext& context) {
    *context.skia_objects = {};
}

}  // namespace ogplay::runtime
