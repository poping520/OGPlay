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
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {

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

            context->ui_drawables[call.receiver.Value()].bitmap_drawable = true;
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
        if (!container && DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid())
            return dx::VmValue::Ref(DrawableRef(c, c.receiver, "mOpticalInsets", "Landroid/graphics/Insets;"));
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
        if (!container && DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid()) {
            const auto patch = DrawableRef(c, c.receiver, "mNinePatch", "Landroid/graphics/NinePatch;");
            const auto paint = DrawableRef(c, c.receiver, "mPaint", "Landroid/graphics/Paint;");
            const bool alpha = CallAndroidMethod(c.vm, patch, "hasAlpha", "()Z").AsInt() ||
                               (paint.IsValid() && CallAndroidMethod(c.vm, paint, "getAlpha", "()I").AsInt() < 255);
            return dx::VmValue::Int(alpha ? -3 : -1);
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
            if (!container && DrawableRef(call, call.receiver, "mNinePatch", "Landroid/graphics/NinePatch;").IsValid())
                return dx::VmValue::Int(ReadIntField(call.vm, call.receiver, width ? "mBitmapWidth" : "mBitmapHeight"));
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
            const auto current =
                selected >= 0 ? s.skin_children.at(static_cast<std::size_t>(selected)) : dx::VmObjectRef{};
            const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(c.receiver), "mCurrDrawable",
                                                                "Landroid/graphics/drawable/Drawable;");
            c.vm.Model().InstanceSlots(c.receiver)[c.vm.Linker().Field(*field).slot] = {current.Value(),
                                                                                        dx::SlotTag::ref};
            if (current.IsValid()) {
                const auto bounds = CallAndroidMethod(c.vm, c.receiver, "getBounds", "()Landroid/graphics/Rect;").ref;
                static_cast<void>(CallAndroidMethod(c.vm, current, "setBounds", "(Landroid/graphics/Rect;)V",
                                                    {dx::VmValue::Ref(bounds)}));
            }
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
