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

// ---- migrated from android_graphics_drawable_Drawable.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_Drawable(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/Drawable;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_drawable_BitmapDrawable(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/BitmapDrawable;",
                                                    "Landroid/graphics/drawable/Drawable;");
    builder.MergesBootClass();
    // mutate, alpha and getBitmap keep the ROM bodies.
    const auto bitmap_ctor = [context](const std::size_t index) {
        return dx::IntrinsicHandler([context, index](dx::IntrinsicContext& call) {
            const auto bitmap = call.arguments[index].ref;
            if (bitmap.IsValid() && !context->bitmaps.contains(bitmap.Value()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "BitmapDrawable requires a Bitmap backing"};
            const auto state_type =
                call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/BitmapDrawable$BitmapState;");
            const auto state = call.vm.NewIntrinsicInstance("Landroid/graphics/drawable/BitmapDrawable$BitmapState;");
            const auto root = call.vm.ProtectReferences(std::array{state});
            const auto state_ctor =
                call.vm.Linker().FindDirectMethod(state_type, "<init>", "(Landroid/graphics/Bitmap;)V");
            const auto initialized =
                call.vm.Call(*state_ctor, std::array{dx::VmValue::Ref(state), dx::VmValue::Ref(bitmap)});
            if (initialized.exception.IsValid()) throw dx::VmJavaThrow{"", "", initialized.exception};
            const auto ctor = call.vm.Linker().FindDirectMethod(
                call.vm.Linker().ResolveDescriptor("Landroid/graphics/drawable/BitmapDrawable;"), "<init>",
                "(Landroid/graphics/drawable/BitmapDrawable$BitmapState;Landroid/content/res/Resources;)V");
            const auto resources = index == 1 ? call.arguments[0] : dx::VmValue::Ref(dx::VmObjectRef{});
            const auto made =
                call.vm.Call(*ctor, std::array{dx::VmValue::Ref(call.receiver), dx::VmValue::Ref(state), resources});
            if (made.exception.IsValid()) throw dx::VmJavaThrow{"", "", made.exception};

            return dx::VmValue::Void();
        });
    };
    builder.Constructor("(Landroid/graphics/Bitmap;)V", bitmap_ctor(0))
        .Constructor("(Landroid/content/res/Resources;Landroid/graphics/Bitmap;)V", bitmap_ctor(1));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
namespace {
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

void AddRomDrawableQueries(dx::IntrinsicClassBuilder& b, bool container) {
    b.VirtualMethod("getOpticalInsets", "()Landroid/graphics/Insets;", [container](dx::IntrinsicContext& c) {
        if (container && RomContainerState(c).IsValid())
            return dx::VmValue::Ref(DrawableRef(c, c.receiver, "mInsets", "Landroid/graphics/Insets;"));
        if (!container && DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid())
            return dx::VmValue::Ref(DrawableRef(c, c.receiver, "mOpticalInsets", "Landroid/graphics/Insets;"));
        throw std::logic_error("ROM drawable backing unavailable");
    });
    b.VirtualMethod("getOpacity", "()I", [container](dx::IntrinsicContext& c) {
        if (container) {
            const auto state = RomContainerState(c);
            if (state.IsValid()) {
                const auto current = RomContainerCurrent(c);
                if (!current.IsValid() || !CallAndroidMethod(c.vm, current, "isVisible", "()Z").AsInt())
                    return dx::VmValue::Int(-2);
                return CallAndroidMethod(c.vm, state, "getOpacity", "()I");
            }
        }
        if (!container && DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid()) {
            const auto patch = DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;");
            const auto paint = DrawableRef(c, c.receiver, "mPaint", "Landroid/graphics/Paint;");
            const bool alpha = CallAndroidMethod(c.vm, patch, "hasAlpha", "()Z").AsInt() ||
                               (paint.IsValid() && CallAndroidMethod(c.vm, paint, "getAlpha", "()I").AsInt() < 255);
            return dx::VmValue::Int(alpha ? -3 : -1);
        }
        throw std::logic_error("ROM drawable backing unavailable");
    });
    b.VirtualMethod("getPadding", "(Landroid/graphics/Rect;)Z", [container](dx::IntrinsicContext& call) {
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
        if (!container && DrawableRef(call, call.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid()) {
            const auto padding = DrawableRef(call, call.receiver, "mPadding", "Landroid/graphics/Rect;");
            if (padding.IsValid()) {
                static_cast<void>(CallAndroidMethod(call.vm, call.arguments[0].ref, "set", "(Landroid/graphics/Rect;)V",
                                                    {dx::VmValue::Ref(padding)}));
                return dx::VmValue::Int(
                    (ReadIntField(call.vm, padding, "left") | ReadIntField(call.vm, padding, "top") |
                     ReadIntField(call.vm, padding, "right") | ReadIntField(call.vm, padding, "bottom")) != 0);
            }
            return dx::VmValue::Int(0);
        }
        throw std::logic_error("ROM drawable backing unavailable");
    });
    for (const auto width : {true, false}) {
        const auto size = [width, container](dx::IntrinsicContext& call) {
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
            if (!container && DrawableRef(call, call.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid())
                return dx::VmValue::Int(ReadIntField(call.vm, call.receiver, width ? "mBitmapWidth" : "mBitmapHeight"));
            throw std::logic_error("ROM drawable backing unavailable");
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

Decl Declare_android_graphics_drawable_DrawableContainer(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/DrawableContainer;",
                                              "Landroid/graphics/drawable/Drawable;",
                                              {"Landroid/graphics/drawable/Drawable$Callback;"});
    b.MergesBootClass();
    const auto reject_state = [](dx::IntrinsicContext& call) -> dx::VmValue {
        if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.drawable_resource", 0);
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "Drawable state is not admitted"};
    };
    b.OverrideMethod("mutate", "()Landroid/graphics/drawable/Drawable;", reject_state);
    b.OverrideMethod("getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;",
                     [](dx::IntrinsicContext& c) {
                         const auto state = RomContainerState(c);
                         if (!state.IsValid() || !CallAndroidMethod(c.vm, state, "canConstantState", "()Z").AsInt())
                             return dx::VmValue::Ref(dx::VmObjectRef{});
                         return dx::VmValue::Ref(state);
                     });
    b.OverrideMethod("getAlpha", "()I", reject_state);
    b.OverrideMethod("setAlpha", "(I)V", IgnoredUiRequest("Drawable.setAlpha"));
    b.VirtualMethod("animate", "(Z)V", IgnoredUiRequest("DrawableContainer.animate"), 0);
    AddRomDrawableQueries(b, true);
    b.VirtualMethod("setVisible", "(ZZ)Z", [](dx::IntrinsicContext& c) {
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
        }
        return out.value;
    });
    // Drawable.Callback as in the ROM: only the current child reaches the owner's
    // callback, re-addressed to this container; other children are dropped.
    for (const auto& [name, sig] : std::array<std::pair<const char*, const char*>, 3>{
             {{"invalidateDrawable", "(Landroid/graphics/drawable/Drawable;)V"},
              {"scheduleDrawable", "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;J)V"},
              {"unscheduleDrawable", "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;)V"}}}) {
        b.VirtualMethod(name, sig, [name, sig](dx::IntrinsicContext& c) {
            if (RomContainerState(c).IsValid()) {
                if (RomContainerCurrent(c) != c.arguments[0].ref) return dx::VmValue::Void();
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
    b.VirtualMethod("getCurrent", "()Landroid/graphics/drawable/Drawable;", [](dx::IntrinsicContext& c) {
        if (RomContainerState(c).IsValid()) return dx::VmValue::Ref(RomContainerCurrent(c));
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    return std::move(b).Build();
}

Decl Declare_android_graphics_drawable_NinePatchDrawable(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/NinePatchDrawable;",
                                              "Landroid/graphics/drawable/Drawable;");
    b.MergesBootClass();
    AddRomDrawableQueries(b, false);
    return std::move(b).Build();
}

Decl Declare_android_graphics_drawable_StateListDrawable(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/drawable/StateListDrawable;",
                                              "Landroid/graphics/drawable/DrawableContainer;");
    b.MergesBootClass();
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
