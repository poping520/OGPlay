// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_view_Display.cpp ----
#include "catalog.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

#include <bit>
#include <cmath>

namespace ogplay::runtime::android_intrinsics {

namespace {

bool CallContextWrapperConstructor(dx::IntrinsicContext& context,
                                   const dx::VmObjectRef base) {
    auto& linker = context.vm.Linker();
    const auto owner = linker.FindClass("Landroid/content/ContextWrapper;");
    if (!owner.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "ContextWrapper is not linked"};
    }
    const auto constructor = linker.FindDirectMethod(
        *owner, "<init>", "(Landroid/content/Context;)V");
    if (!constructor.has_value()) {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "ContextWrapper constructor is unavailable"};
    }
    const auto outcome = context.vm.Call(
        *constructor,
        std::vector<dx::VmValue>{dx::VmValue::Ref(context.receiver),
                                 dx::VmValue::Ref(base)});
    if (!outcome.exception.IsValid()) return true;
    context.vm.SetPendingException(outcome.exception);
    return false;
}

}  // namespace

Decl Declare_android_view_ContextThemeWrapper(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/view/ContextThemeWrapper;",
        "Landroid/content/ContextWrapper;");
    const auto theme_resource =
        builder.BoundInstanceField("mThemeResource", "I", 0x0002U);
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.Constructor("(Landroid/content/Context;I)V",
        [theme_resource](dx::IntrinsicContext& context) {
            if (CallContextWrapperConstructor(context,
                                              context.arguments[0].ref)) {
                dx::IntrinsicCall(context).SetInt(
                    theme_resource, context.arguments[1].AsInt());
            }
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("setTheme", "(I)V",
        [theme_resource](dx::IntrinsicContext& context) {
            dx::IntrinsicCall(context).SetInt(
                theme_resource, context.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getThemeResId", "()I",
        [theme_resource](dx::IntrinsicContext& context) {
            return dx::VmValue::Int(
                dx::IntrinsicCall(context).GetInt(theme_resource));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_Display {
namespace {

void WriteMetric(dx::IntrinsicContext& call, const dx::VmObjectRef metrics,
                 const std::string& name, const std::uint32_t bits,
                 const std::string& descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(metrics), name, descriptor);
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "DisplayMetrics field is missing: " + name);
    }
    call.vm.Model().InstanceSlots(metrics)
        [call.vm.Linker().Field(*field).slot] = {bits, dx::SlotTag::cat1};
}

void PopulateMetrics(dx::IntrinsicContext& call, const Context& context) {
    const auto metrics = call.arguments[0].ref;
    if (!metrics.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "Display metrics output is null"};
    }
    if (!std::isfinite(context->ui_density) || context->ui_density <= 0.0F ||
        !std::isfinite(context->ui_scaled_density) ||
        context->ui_scaled_density <= 0.0F) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "display density is invalid");
    }
    const auto density_dpi = static_cast<std::int32_t>(
        std::lround(context->ui_density * 160.0F));
    const auto density_bits = std::bit_cast<std::uint32_t>(context->ui_density);
    const auto scaled_bits =
        std::bit_cast<std::uint32_t>(context->ui_scaled_density);
    const auto dpi_bits =
        std::bit_cast<std::uint32_t>(static_cast<float>(density_dpi));
    const auto write_int = [&](const std::string& name,
                               const std::int32_t value) {
        WriteMetric(call, metrics, name, static_cast<std::uint32_t>(value),
                    "I");
    };
    const auto write_float = [&](const std::string& name,
                                 const std::uint32_t bits) {
        WriteMetric(call, metrics, name, bits, "F");
    };
    write_int("widthPixels", static_cast<std::int32_t>(context->surface_width));
    write_int("heightPixels",
              static_cast<std::int32_t>(context->surface_height));
    write_float("density", density_bits);
    write_int("densityDpi", density_dpi);
    write_float("scaledDensity", scaled_bits);
    write_float("xdpi", dpi_bits);
    write_float("ydpi", dpi_bits);
    write_int("noncompatWidthPixels",
              static_cast<std::int32_t>(context->surface_width));
    write_int("noncompatHeightPixels",
              static_cast<std::int32_t>(context->surface_height));
    write_float("noncompatDensity", density_bits);
    write_int("noncompatDensityDpi", density_dpi);
    write_float("noncompatScaledDensity", scaled_bits);
    write_float("noncompatXdpi", dpi_bits);
    write_float("noncompatYdpi", dpi_bits);
}

}  // namespace

Decl Declare_android_util_DisplayMetrics(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/util/DisplayMetrics;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("widthPixels", "I");
    builder.InstanceField("heightPixels", "I");
    builder.InstanceField("density", "F");
    builder.InstanceField("densityDpi", "I");
    builder.InstanceField("scaledDensity", "F");
    builder.InstanceField("xdpi", "F");
    builder.InstanceField("ydpi", "F");
    builder.InstanceField("noncompatWidthPixels", "I");
    builder.InstanceField("noncompatHeightPixels", "I");
    builder.InstanceField("noncompatDensity", "F");
    builder.InstanceField("noncompatDensityDpi", "I");
    builder.InstanceField("noncompatScaledDensity", "F");
    builder.InstanceField("noncompatXdpi", "F");
    builder.InstanceField("noncompatYdpi", "F");
    return std::move(builder).Build();
}

Decl Declare_android_view_Display(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Display;", "Ljava/lang/Object;");
    builder.FinalMethod("getWidth", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_width));
        });
    builder.FinalMethod("getHeight", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_height));
        });
    // Managed surface coordinates are landscape-natural and never
    // rotate independently from the host window.
    const auto get_rotation = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getRotation", "()I", get_rotation);
    builder.FinalMethod("getOrientation", "()I", get_rotation);
    builder.FinalMethod("getDisplayId", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    const auto get_metrics = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            PopulateMetrics(call, context);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getMetrics", "(Landroid/util/DisplayMetrics;)V",
                        get_metrics);
    builder.FinalMethod("getRealMetrics", "(Landroid/util/DisplayMetrics;)V",
                        get_metrics);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_util_DisplayMetrics(const Context& context) {
    return dvm80_android_view_Display::Declare_android_util_DisplayMetrics(context);
}
Decl Declare_android_view_Display(const Context& context) {
    return dvm80_android_view_Display::Declare_android_view_Display(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_inputmethod_InputMethodManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_inputmethod_InputMethodManager {

Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/inputmethod/InputMethodManager;", "Ljava/lang/Object;");
    builder.FinalMethod("hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z", TelephonyFalseHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_inputmethod_InputMethodManager(const Context& context) {
    return dvm80_android_view_inputmethod_InputMethodManager::Declare_android_view_inputmethod_InputMethodManager(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_KeyEvent.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_KeyEvent {

Decl Declare_android_view_KeyEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyEvent;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_KeyEvent(const Context& context) {
    return dvm80_android_view_KeyEvent::Declare_android_view_KeyEvent(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_MotionEvent.cpp ----
// Motion events read their slots directly.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_MotionEvent {

Decl Declare_android_view_MotionEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/MotionEvent;", "Ljava/lang/Object;");
    builder.InstanceField("action", "I");
    builder.InstanceField("x", "F");
    builder.InstanceField("y", "F");
    builder.InstanceField("pointer", "I");
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
    builder.FinalMethod("getAction", "()I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits));
        });
    builder.FinalMethod("getX", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.FinalMethod("getY", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.FinalMethod("getX", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.FinalMethod("getY", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.FinalMethod("getPointerCount", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.FinalMethod("getPointerId", "(I)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[3].bits));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_MotionEvent(const Context& context) {
    return dvm80_android_view_MotionEvent::Declare_android_view_MotionEvent(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder_Callback.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_SurfaceHolder_Callback {

Decl Declare_android_view_SurfaceHolder_Callback(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder$Callback;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_SurfaceHolder_Callback(const Context& context) {
    return dvm80_android_view_SurfaceHolder_Callback::Declare_android_view_SurfaceHolder_Callback(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder_Impl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_SurfaceHolder_Impl {

namespace {

dx::IntrinsicHandler LockCanvasHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        auto& canvas = context->holder_canvases[call.receiver.Value()];
        if (!canvas.IsValid()) {
            canvas = call.vm.NewIntrinsicInstance("Landroid/graphics/Canvas;");
        }
        auto& state = context->canvases[canvas.Value()];
        if (state.locked) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "SurfaceHolder canvas is already locked"};
        }
        state.holder = call.receiver.Value();
        state.width = context->surface_width;
        state.height = context->surface_height;
        state.argb.assign(
            static_cast<std::size_t>(state.width) * state.height, 0xff000000U);
        state.locked = true;
        return dx::VmValue::Ref(canvas);
    };
}

dx::IntrinsicHandler UnlockCanvasAndPostHandler(const Context& context) {
    return [context](dx::IntrinsicContext& call) {
        const auto canvas = call.arguments[0].ref;
        const auto found = context->canvases.find(canvas.Value());
        if (!canvas.IsValid() || found == context->canvases.end() ||
            !found->second.locked ||
            found->second.holder != call.receiver.Value()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "Canvas is not locked by this SurfaceHolder"};
        }
        if (context->session == nullptr) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "SurfaceHolder has no guest session"};
        }
        auto& state = found->second;
        std::vector<std::uint8_t> rgba8;
        rgba8.reserve(state.argb.size() * 4U);
        for (const auto pixel : state.argb) {
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 16U));
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 8U));
            rgba8.push_back(static_cast<std::uint8_t>(pixel));
            rgba8.push_back(static_cast<std::uint8_t>(pixel >> 24U));
        }
        state.locked = false;
        context->session->PublishSoftwareFrame(std::move(rgba8));
        return dx::VmValue::Void();
    };
}

void AddCanvasMethods(dx::IntrinsicClassBuilder& builder,
                      const Context& context) {
    builder.FinalMethod("lockCanvas", "()Landroid/graphics/Canvas;",
                        LockCanvasHandler(context));
    builder.FinalMethod("lockCanvas",
                        "(Landroid/graphics/Rect;)Landroid/graphics/Canvas;",
                        LockCanvasHandler(context));
    builder.FinalMethod("unlockCanvasAndPost",
                        "(Landroid/graphics/Canvas;)V",
                        UnlockCanvasAndPostHandler(context));
}

}  // namespace

Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceHolder$Impl;", "Ljava/lang/Object;", {"Landroid/view/SurfaceHolder;"});
    builder.FinalMethod("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.FinalMethod("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.FinalMethod("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    AddCanvasMethods(builder, context);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    return dvm80_android_view_SurfaceHolder_Impl::Declare_android_view_SurfaceHolder_Impl(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceHolder.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_SurfaceHolder {

Decl Declare_android_view_SurfaceHolder(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder;");
    builder.FinalMethod("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.FinalMethod("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.FinalMethod("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    builder.FinalMethod("lockCanvas", "()Landroid/graphics/Canvas;",
                        dvm80_android_view_SurfaceHolder_Impl::LockCanvasHandler(context));
    builder.FinalMethod(
        "lockCanvas", "(Landroid/graphics/Rect;)Landroid/graphics/Canvas;",
        dvm80_android_view_SurfaceHolder_Impl::LockCanvasHandler(context));
    builder.FinalMethod(
        "unlockCanvasAndPost", "(Landroid/graphics/Canvas;)V",
        dvm80_android_view_SurfaceHolder_Impl::UnlockCanvasAndPostHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_SurfaceHolder(const Context& context) {
    return dvm80_android_view_SurfaceHolder::Declare_android_view_SurfaceHolder(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_SurfaceView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_SurfaceView {

Decl Declare_android_view_SurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("getHolder", "()Landroid/view/SurfaceHolder;",
        [context](dx::IntrinsicContext& call) {
            auto& holder = context->surface_holders[call.receiver.Value()];
            if (!holder.IsValid()) {
                holder = call.vm.NewIntrinsicInstance(
                    "Landroid/view/SurfaceHolder$Impl;");
            }
            return dx::VmValue::Ref(holder);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_SurfaceView(const Context& context) {
    return dvm80_android_view_SurfaceView::Declare_android_view_SurfaceView(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View_OnClickListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_View_OnClickListener {

Decl Declare_android_view_View_OnClickListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnClickListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View_OnClickListener(const Context& context) {
    return dvm80_android_view_View_OnClickListener::Declare_android_view_View_OnClickListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- android.view.View.OnFocusChangeListener (API 19) ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm89_android_view_View_OnFocusChangeListener {

Decl Declare_android_view_View_OnFocusChangeListener(const Context& context) {
    static_cast<void>(context);
    // 当前只发布 API 19 接口形状与抽象方法引用，使 DEX implements 和
    // invoke-interface 能正常链接、分派。尚未实现 View 的完整焦点监听语义：
    // set/getOnFocusChangeListener、逐 View listener 保存与 GC tracing、
    // focused/focusable 状态、requestFocus/clearFocus 状态转换，以及焦点变化时
    // 自动调用 onFocusChange(View, boolean)。在这些能力闭合前不得伪造回调。
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/view/View$OnFocusChangeListener;");
    builder.UnimplementedVirtual(
        "onFocusChange", "(Landroid/view/View;Z)V", 0x0401U);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics::dvm89_android_view_View_OnFocusChangeListener

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View_OnFocusChangeListener(const Context& context) {
    return dvm89_android_view_View_OnFocusChangeListener::
        Declare_android_view_View_OnFocusChangeListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View_OnTouchListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_View_OnTouchListener {

Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnTouchListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    return dvm80_android_view_View_OnTouchListener::Declare_android_view_View_OnTouchListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_View.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_View {
namespace {

ui::UiNodeId ViewNode(dx::IntrinsicContext& call, const Context& context) {
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(call.receiver))
                                .descriptor;
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForDescriptor(descriptor));
}

void EnsureLayout(const Context& context) {
    if (context->ui_tree.Get(context->ui_tree.Root())->layout_dirty) {
        ui::LayoutUiTree(
            context->ui_tree,
            {static_cast<std::int32_t>(context->surface_width),
             static_cast<std::int32_t>(context->surface_height)});
    }
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

Decl Declare_android_view_View(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/View;", "Ljava/lang/Object;");
    builder.Constructor("(Landroid/content/Context;)V",
                    ViewInitHandler(context));
    builder.VirtualMethod("onSizeChanged", "(IIII)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onWindowFocusChanged", "(Z)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = FindViewUiNode(*context, call.receiver.Value());
            if (!node.has_value()) return dx::VmValue::Void();
            const auto* state = context->ui_tree.Get(*node);
            if (state == nullptr || state->children.empty()) {
                return dx::VmValue::Void();
            }
            std::vector<dx::VmObjectRef> children;
            children.reserve(state->children.size());
            for (const auto child_node : state->children) {
                const auto child = ViewObjectForUiNode(*context, child_node);
                if (child.IsValid()) children.push_back(child);
            }
            auto& linker = call.vm.Linker();
            for (const auto child : children) {
                const auto child_class = call.vm.Model().ObjectClass(child);
                const auto method = linker.FindVtableIndex(
                    child_class, "onWindowFocusChanged", "(Z)V");
                if (!method.has_value()) {
                    throw dx::VmJavaThrow{
                        "Ljava/lang/IllegalStateException;",
                        "child View has no onWindowFocusChanged(boolean)"};
                }
                const auto outcome = call.vm.Call(
                    linker.Class(child_class).vtable[*method],
                    std::vector{dx::VmValue::Ref(child), call.arguments[0]});
                if (outcome.exception.IsValid()) {
                    call.vm.SetPendingException(outcome.exception);
                    return dx::VmValue::Void();
                }
            }
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    const auto noop_flag = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("setFocusable", "(Z)V", noop_flag);
    builder.FinalMethod("setFocusableInTouchMode", "(Z)V", noop_flag);
    builder.FinalMethod("requestFocus", "()Z",
        [](dx::IntrinsicContext&) {
            // The single fullscreen view always holds focus.
            return dx::VmValue::Int(1);
        });
    const auto invalidate = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("invalidate", "()V", invalidate);
    builder.FinalMethod("postInvalidate", "()V", invalidate);
    builder.FinalMethod("getId", "()I",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            return dx::VmValue::Int(
                context->ui_tree.Get(node)->android_id);
        });
    builder.FinalMethod("setId", "(I)V", ViewSetIdHandler(context));
    builder.FinalMethod("setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            const auto params = call.arguments[0].ref;
            if (!params.IsValid()) {
                context->ui_view_layout_params.erase(call.receiver.Value());
                context->ui_tree.Get(node)->layout = {};
            } else {
                const auto found =
                    context->ui_layout_params.find(params.Value());
                if (found == context->ui_layout_params.end()) {
                    throw dx::VmJavaThrow{
                        "Ljava/lang/IllegalArgumentException;",
                        "LayoutParams is not initialized"};
                }
                context->ui_view_layout_params[call.receiver.Value()] = params;
                context->ui_tree.Get(node)->layout = found->second;
            }
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->ui_view_layout_params.find(call.receiver.Value());
            return dx::VmValue::Ref(
                found == context->ui_view_layout_params.end()
                    ? dx::VmObjectRef{}
                    : found->second);
        });
    builder.FinalMethod("setPadding", "(IIII)V",
        [context](dx::IntrinsicContext& call) {
            ui::Insets padding{call.arguments[0].AsInt(),
                               call.arguments[1].AsInt(),
                               call.arguments[2].AsInt(),
                               call.arguments[3].AsInt()};
            if (padding.left < 0 || padding.top < 0 || padding.right < 0 ||
                padding.bottom < 0) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "View padding cannot be negative"};
            }
            const auto node = ViewNode(call, context);
            context->ui_tree.Get(node)->padding = padding;
            context->ui_tree.MarkLayoutDirty(node);
            return dx::VmValue::Void();
        });
    const auto geometry = [context](const auto member) {
        return dx::IntrinsicHandler(
            [context, member](dx::IntrinsicContext& call) {
                const auto node = ViewNode(call, context);
                EnsureLayout(context);
                return dx::VmValue::Int(member(*context->ui_tree.Get(node)));
            });
    };
    builder.FinalMethod("getLeft", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.left;
    }));
    builder.FinalMethod("getTop", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.top;
    }));
    builder.FinalMethod("getRight", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.right;
    }));
    builder.FinalMethod("getBottom", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.bottom;
    }));
    builder.FinalMethod("getWidth", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.right - node.frame.left;
    }));
    builder.FinalMethod("getHeight", "()I", geometry([](const ui::UiNode& node) {
        return node.frame.bottom - node.frame.top;
    }));
    builder.FinalMethod("setVisibility", "(I)V", [context](dx::IntrinsicContext& call) {
        const auto value = call.arguments[0].AsInt();
        if (value != kVisible && value != kInvisible && value != kGone) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                "setVisibility value is not one of VISIBLE/INVISIBLE/GONE: " +
                    std::to_string(value)};
        }
        const auto node = EnsureViewUiNode(
            *context, call.receiver, ui::UiClass::View);
        const auto visibility = value == kVisible
                                    ? ui::Visibility::Visible
                                    : value == kInvisible
                                          ? ui::Visibility::Invisible
                                          : ui::Visibility::Gone;
        context->ui_tree.SetVisibility(node, visibility);
        return dx::VmValue::Void();
    });
    builder.FinalMethod("getVisibility", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(VisibilityOf(*context, call.receiver.Value()));
    });
    builder.FinalMethod("setBackgroundColor", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            context->ui_tree.Get(node)->background_color =
                AndroidColorToRgba(static_cast<std::uint32_t>(
                    call.arguments[0].AsInt()));
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setBackgroundResource", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = ViewNode(call, context);
            const auto resource_id =
                static_cast<std::uint32_t>(call.arguments[0].AsInt());
            if (resource_id == 0U) {
                context->ui_tree.Get(node)->background_color.reset();
            } else {
                try {
                    context->ui_tree.Get(node)->background_color =
                        ResolveUiColor(*context, resource_id);
                } catch (const std::runtime_error& error) {
                    throw dx::VmJavaThrow{
                        "Landroid/content/res/Resources$NotFoundException;",
                        error.what()};
                }
            }
            context->ui_tree.MarkDrawDirty(node);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", WidgetNoopHandler());
    builder.FinalMethod("setOnClickListener", "(Landroid/view/View$OnClickListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            if (call.arguments[0].ref.IsValid()) {
                context->ui_click_listeners[node] = call.arguments[0].ref;
            } else {
                context->ui_click_listeners.erase(node);
            }
            if (!context->ui_tree.Get(node)->parent.has_value() &&
                call.arguments[0].ref.IsValid()) {
                GuestLog(call, core::LogLevel::warn,
                    "setOnClickListener: the view is detached; "
                    "clicks fall through to Activity.onTouchEvent (recorded gap)");
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto node = EnsureViewUiNode(
                *context, call.receiver, ui::UiClass::View);
            if (call.arguments[0].ref.IsValid()) {
                context->ui_touch_listeners[node] = call.arguments[0].ref;
            } else {
                context->ui_touch_listeners.erase(node);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("clearFocus", "()V", WidgetNoopHandler());
    builder.FinalMethod("getWindowToken", "()Landroid/os/IBinder;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getViewTreeObserver", "()Landroid/view/ViewTreeObserver;",
        [context](dx::IntrinsicContext& call) {
            auto& observer =
                context->view_tree_observers[call.receiver.Value()];
            if (!observer.IsValid()) {
                observer = call.vm.NewIntrinsicInstance(
                    "Landroid/view/ViewTreeObserver;");
            }
            return dx::VmValue::Ref(observer);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_View(const Context& context) {
    return dvm80_android_view_View::Declare_android_view_View(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewGroup_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_ViewGroup_LayoutParams {
namespace {

ui::DimensionSpec Dimension(const std::int32_t value) {
    if (value == -1) return {ui::SizeMode::MatchParent, 0};
    if (value == -2) return {ui::SizeMode::WrapContent, 0};
    if (value >= 0) return {ui::SizeMode::Fixed, value};
    throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid LayoutParams dimension: " +
                              std::to_string(value)};
}

}  // namespace

Decl Declare_android_view_ViewGroup_LayoutParams(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/ViewGroup$LayoutParams;", "Ljava/lang/Object;");
    builder.Constructor("(II)V",
        [context](dx::IntrinsicContext& call) {
            ui::LayoutParams params;
            params.width = Dimension(call.arguments[0].AsInt());
            params.height = Dimension(call.arguments[1].AsInt());
            context->ui_layout_params[call.receiver.Value()] = params;
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_ViewGroup_LayoutParams(const Context& context) {
    return dvm80_android_view_ViewGroup_LayoutParams::Declare_android_view_ViewGroup_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewGroup.cpp ----
#include "catalog.h"

#include <optional>

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_ViewGroup {
namespace {

ui::UiNodeId NodeFor(dx::IntrinsicContext& call, const Context& context,
                     const dx::VmObjectRef view) {
    if (!view.IsValid()) {
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "ViewGroup child is null"};
    }
    const auto descriptor = call.vm.Linker()
                                .Class(call.vm.Model().ObjectClass(view))
                                .descriptor;
    return EnsureViewUiNode(*context, view, UiClassForDescriptor(descriptor));
}

void ApplyParams(const Context& context, const dx::VmObjectRef view,
                 const ui::UiNodeId node,
                 const std::optional<dx::VmObjectRef> params) {
    if (params.has_value()) {
        if (!params->IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "LayoutParams is null"};
        }
        const auto found = context->ui_layout_params.find(params->Value());
        if (found == context->ui_layout_params.end()) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "LayoutParams is not initialized"};
        }
        context->ui_tree.Get(node)->layout = found->second;
        context->ui_view_layout_params[view.Value()] = *params;
        context->ui_tree.MarkLayoutDirty(node);
        return;
    }
    const auto assigned = context->ui_view_layout_params.find(view.Value());
    if (assigned == context->ui_view_layout_params.end()) return;
    const auto found = context->ui_layout_params.find(assigned->second.Value());
    if (found != context->ui_layout_params.end()) {
        context->ui_tree.Get(node)->layout = found->second;
    }
}

dx::IntrinsicHandler AddHandler(const Context& context, const bool has_index,
                                const bool has_params) {
    return [context, has_index, has_params](dx::IntrinsicContext& call) {
        const auto child = call.arguments[0].ref;
        const auto parent_node = NodeFor(call, context, call.receiver);
        const auto child_node = NodeFor(call, context, child);
        std::optional<std::size_t> index;
        std::size_t argument = 1;
        if (has_index) {
            const auto requested = call.arguments[argument++].AsInt();
            if (requested >= 0) index = static_cast<std::size_t>(requested);
        }
        std::optional<dx::VmObjectRef> params;
        if (has_params) params = call.arguments[argument].ref;
        ApplyParams(context, child, child_node, params);
        try {
            context->ui_tree.Attach(parent_node, child_node, index);
        } catch (const std::runtime_error& error) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  error.what()};
        }
        return dx::VmValue::Void();
    };
}

}  // namespace

Decl Declare_android_view_ViewGroup(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/ViewGroup;", "Landroid/view/View;");
    builder.FinalMethod("addView", "(Landroid/view/View;)V",
                    AddHandler(context, false, false));
    builder.FinalMethod("addView", "(Landroid/view/View;I)V",
                    AddHandler(context, true, false));
    builder.FinalMethod(
        "addView",
        "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V",
        AddHandler(context, true, true));
    builder.FinalMethod(
        "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        AddHandler(context, false, true));
    builder.FinalMethod("removeView", "(Landroid/view/View;)V",
        [context](dx::IntrinsicContext& call) {
            const auto child = call.arguments[0].ref;
            if (!child.IsValid()) return dx::VmValue::Void();
            const auto parent = FindViewUiNode(*context, call.receiver.Value());
            const auto node = FindViewUiNode(*context, child.Value());
            if (parent.has_value() && node.has_value() &&
                context->ui_tree.Get(*node)->parent == parent) {
                context->ui_tree.Detach(*node);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeViews", "(II)V",
        [context](dx::IntrinsicContext& call) {
            const auto parent = NodeFor(call, context, call.receiver);
            const auto start = call.arguments[0].AsInt();
            const auto count = call.arguments[1].AsInt();
            const auto children = context->ui_tree.Get(parent)->children;
            if (start < 0 || count < 0 ||
                static_cast<std::size_t>(start) > children.size() ||
                static_cast<std::size_t>(count) >
                    children.size() - static_cast<std::size_t>(start)) {
                throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                      "removeViews range is outside children"};
            }
            for (std::int32_t offset = count; offset > 0; --offset) {
                context->ui_tree.Detach(children[static_cast<std::size_t>(
                    start + offset - 1)]);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod(
        "updateViewLayout",
        "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        [context](dx::IntrinsicContext& call) {
            const auto child = call.arguments[0].ref;
            const auto parent = NodeFor(call, context, call.receiver);
            const auto node = NodeFor(call, context, child);
            if (context->ui_tree.Get(node)->parent != parent) {
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "view is not a child of this ViewGroup"};
            }
            ApplyParams(context, child, node, call.arguments[1].ref);
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_ViewGroup(const Context& context) {
    return dvm80_android_view_ViewGroup::Declare_android_view_ViewGroup(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewTreeObserver_OnGlobalLayoutListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_ViewTreeObserver_OnGlobalLayoutListener {

Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(const Context& context) {
    return dvm80_android_view_ViewTreeObserver_OnGlobalLayoutListener::Declare_android_view_ViewTreeObserver_OnGlobalLayoutListener(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_ViewTreeObserver.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_ViewTreeObserver {

Decl Declare_android_view_ViewTreeObserver(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/ViewTreeObserver;", "Ljava/lang/Object;");
    builder.FinalMethod("addOnGlobalLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        [context](dx::IntrinsicContext& call) {
            const auto listener = call.arguments[0].ref;
            if (!listener.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                    "global layout listener is null"};
            }
            context->global_layout_listeners[call.receiver.Value()] = listener;
            return dx::VmValue::Void();
        });
    const auto remove_global_listener = dx::IntrinsicHandler(
        [context](dx::IntrinsicContext& call) {
            const auto found = context->global_layout_listeners.find(
                call.receiver.Value());
            if (found != context->global_layout_listeners.end() &&
                found->second == call.arguments[0].ref) {
                context->global_layout_listeners.erase(found);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("removeGlobalOnLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        remove_global_listener);
    builder.FinalMethod("removeOnGlobalLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
        remove_global_listener);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_ViewTreeObserver(const Context& context) {
    return dvm80_android_view_ViewTreeObserver::Declare_android_view_ViewTreeObserver(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_Window.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_Window {
namespace {

[[nodiscard]] dx::VmObjectRef Attributes(dx::IntrinsicContext& call,
                                         const Context& context) {
    return Singleton(call, context, "window_attributes",
                     "Landroid/view/WindowManager$LayoutParams;");
}

[[nodiscard]] const dx::LinkedField& IntField(dx::IntrinsicContext& call,
                                              const dx::VmObjectRef object,
                                              const std::string& name) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "Window LayoutParams field is missing: " + name);
    }
    return call.vm.Linker().Field(*field);
}

[[nodiscard]] std::int32_t ReadIntField(dx::IntrinsicContext& call,
                                        const dx::VmObjectRef object,
                                        const std::string& name) {
    const auto& field = IntField(call, object, name);
    return static_cast<std::int32_t>(
        call.vm.Model().InstanceSlots(object)[field.slot].bits);
}

void WriteIntField(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                   const std::string& name, const std::int32_t value) {
    const auto& field = IntField(call, object, name);
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

}  // namespace

Decl Declare_android_view_Window(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Window;", "Ljava/lang/Object;");
    builder.FinalMethod("setFlags", "(II)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            const auto flags = call.arguments[0].AsInt();
            const auto mask = call.arguments[1].AsInt();
            const auto old = ReadIntField(call, attributes, "flags");
            WriteIntField(call, attributes, "flags",
                          (old & ~mask) | (flags & mask));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addFlags", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            const auto flags = call.arguments[0].AsInt();
            WriteIntField(call, attributes, "flags",
                          ReadIntField(call, attributes, "flags") | flags);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("clearFlags", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            WriteIntField(call, attributes, "flags",
                          ReadIntField(call, attributes, "flags") &
                              ~call.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setSoftInputMode", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto mode = call.arguments[0].AsInt();
            if (mode != 0) {
                WriteIntField(call, Attributes(call, context),
                              "softInputMode", mode);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setType", "(I)V",
        [context](dx::IntrinsicContext& call) {
            WriteIntField(call, Attributes(call, context), "type",
                          call.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getAttributes",
        "()Landroid/view/WindowManager$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Attributes(call, context));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_Window(const Context& context) {
    return dvm80_android_view_Window::Declare_android_view_Window(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManager_LayoutParams.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_WindowManager_LayoutParams {

Decl Declare_android_view_WindowManager_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManager$LayoutParams;", "Ljava/lang/Object;");
    builder.InstanceField("flags", "I");
    builder.InstanceField("windowAnimations", "I");
    builder.InstanceField("softInputMode", "I");
    builder.InstanceField("type", "I");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_WindowManager_LayoutParams(const Context& context) {
    return dvm80_android_view_WindowManager_LayoutParams::Declare_android_view_WindowManager_LayoutParams(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_WindowManager {

Decl Declare_android_view_WindowManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/WindowManager;");
    builder.FinalMethod("getDefaultDisplay", "()Landroid/view/Display;", WindowmanagerGetDefaultDisplayHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_WindowManager(const Context& context) {
    return dvm80_android_view_WindowManager::Declare_android_view_WindowManager(context);
}
}  // namespace ogplay::runtime::android_intrinsics

// ---- migrated from android_view_WindowManagerImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics::dvm80_android_view_WindowManagerImpl {

Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManagerImpl;", "Ljava/lang/Object;", {"Landroid/view/WindowManager;"});
    builder.FinalMethod("getDefaultDisplay", "()Landroid/view/Display;", WindowmanagerGetDefaultDisplayHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    return dvm80_android_view_WindowManagerImpl::Declare_android_view_WindowManagerImpl(context);
}
}  // namespace ogplay::runtime::android_intrinsics
