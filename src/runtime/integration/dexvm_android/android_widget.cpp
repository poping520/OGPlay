// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_widget_AbsoluteLayout_LayoutParams.cpp ----
#include "catalog.h"
#include <array>
#include <bit>
#include <map>
#include <cmath>
#include <set>
#include <algorithm>
#include <stdexcept>

// Defined by the TextView section below: constructor handler that also
// resolves the framework default style carried by defStyleAttr.
namespace ogplay::runtime::android_intrinsics {

namespace {
Decl ExperimentalImageView(const Context& context);
}
}

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout_LayoutParams(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout$LayoutParams;",
                                                    "Landroid/view/ViewGroup$LayoutParams;");
    const auto x = builder.BoundInstanceField("x", "I");
    const auto y = builder.BoundInstanceField("y", "I");
    builder.Constructor("(IIII)V", [x, y](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup$LayoutParams;"), "<init>", "(II)V");
        const auto result =
            c.vm.Call(*target, std::array{dx::VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1]});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        dx::IntrinsicCall(c).SetInt(x, c.arguments[2].AsInt());
        dx::IntrinsicCall(c).SetInt(y, c.arguments[3].AsInt());
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 213.
    builder.Constructor(
        "(Landroid/view/ViewGroup$LayoutParams;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup$LayoutParams;"),
                                               "<init>", "(Landroid/view/ViewGroup$LayoutParams;)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_AbsoluteLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_AbsoluteLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/AbsoluteLayout;", "Landroid/view/ViewGroup;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& call) {
        const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;");
        const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        if (!constructor) throw std::logic_error("ViewGroup constructor is missing");
        const auto result = call.vm.Call(*constructor, std::array{dx::VmValue::Ref(call.receiver), call.arguments[0]});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_Button.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Button(const Context&) {
    return dx::IntrinsicClassBuilder::Class("Landroid/widget/Button;", "Landroid/widget/TextView;").Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_EditText.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_EditText(const Context& context) {
    {
        auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/EditText;", "Landroid/widget/TextView;");
        builder.VirtualMethod("getText", "()Landroid/text/Editable;", [context](dx::IntrinsicContext& c) {
            auto& refs = context->view_adapter_references.at(c.receiver.Value());
            if (!refs.text_editable.IsValid()) {
                refs.text_editable = c.vm.NewIntrinsicInstance("Landroid/text/EditableImpl;");
                context->editable_owner[refs.text_editable.Value()] = c.receiver.Value();
            }
            return dx::VmValue::Ref(refs.text_editable);
        });
        const auto selection = [context](int kind) {
            return dx::IntrinsicHandler([context, kind](dx::IntrinsicContext& c) {
                const auto node = FindViewUiNode(*context, c.receiver.Value());
                if (!node) throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "unconstructed EditText"};
                const auto length = static_cast<std::int32_t>(context->ui_tree.Get(*node)->text.size());
                auto& refs = context->view_adapter_references.at(c.receiver.Value());
                const auto start = kind == 2 ? 0 : kind == 3 ? refs.selection_start : c.arguments[0].AsInt();
                const auto end = kind == 2 ? length : kind == 0 ? c.arguments[1].AsInt() : c.arguments[0].AsInt();
                if (start < 0 || end < 0 || start > length || end > length)
                    throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "selection outside text"};
                refs.selection_start = start;
                refs.selection_end = end;
                return dx::VmValue::Void();
            });
        };
        builder.VirtualMethod("setSelection", "(II)V", selection(0));
        builder.VirtualMethod("setSelection", "(I)V", selection(1));
        builder.VirtualMethod("selectAll", "()V", selection(2));
        builder.VirtualMethod("extendSelection", "(I)V", selection(3));
        return std::move(builder).Build();
    }
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_FrameLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_FrameLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup;");

    // AOSP 4.4 ROM line 211.

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageButton.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageButton(const Context&) {
    return dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageButton;", "Landroid/widget/ImageView;").Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageView_ScaleType.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context&) {
    return dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView$ScaleType;", "Ljava/lang/Enum;").Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ImageView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView(const Context& context) {
    return ExperimentalImageView(context);
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_LinearLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_LinearLayout(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/LinearLayout;", "Landroid/view/ViewGroup;");

    // AOSP 4.4 ROM line 1802.

    {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ProgressBar.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ProgressBar(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ProgressBar;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", ViewInitHandler(context));
    builder.VirtualMethod("setMax", "(I)V", WidgetNoopHandler());
    builder.VirtualMethod("setProgress", "(I)V", WidgetNoopHandler());
    // AOSP 4.4 ROM line 242.
    builder.Constructor(
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
        [](dx::IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/ProgressBar;"),
                                               "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                                            c.arguments[1], dx::VmValue::Int(0x1010077)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_RelativeLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_RelativeLayout(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/RelativeLayout;", "Landroid/view/ViewGroup;");

    {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_ScrollView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {
// ADR-0161: Java geometry and bounded scroll position; animation remains failure.
Decl ExperimentalScrollView(const Context& context, const char* owner) {
    auto b = dx::IntrinsicClassBuilder::Class(owner, "Landroid/widget/FrameLayout;");
    const auto scroller = b.BoundInstanceField("mScroller", "Landroid/widget/OverScroller;", dx::kAccPrivate);
    std::vector<dx::IntrinsicFieldHandle> config;
    const std::array names{"mTouchSlop", "mMinimumVelocity", "mMaximumVelocity", "mOverscrollDistance",
                           "mOverflingDistance"};
    for (std::size_t i = 0; i < names.size(); ++i)
        config.push_back(b.BoundInstanceField(names[i], "I", dx::kAccPrivate));
    const bool vertical = std::string_view(owner) == "Landroid/widget/ScrollView;";
    const auto edge_a = b.BoundInstanceField(vertical ? "mEdgeGlowTop" : "mEdgeGlowLeft", "Landroid/widget/EdgeEffect;",
                                             dx::kAccPrivate);
    const auto edge_b = b.BoundInstanceField(vertical ? "mEdgeGlowBottom" : "mEdgeGlowRight",
                                             "Landroid/widget/EdgeEffect;", dx::kAccPrivate);
    {
        const auto check_event = [context](dx::IntrinsicContext& c, const char* method) {
            const auto event = c.arguments[0].ref;
            if (!context->ui_bounded_scroll_dispatch || !event.IsValid() ||
                CallAndroidMethod(c.vm, event, "getPointerCount", "()I").AsInt() != 1) {
                if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.scroll_bounded_touch", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      std::string(method) + " requires host bounded single-pointer dispatch"};
            }
            const auto action = CallAndroidMethod(c.vm, event, "getAction", "()I").AsInt() & 255;
            if (action < 0 || action > 3) {
                if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.scroll_bounded_touch", 0);
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      std::string(method) + " does not support split pointers"};
            }
            return action;
        };
        b.OverrideMethod("onInterceptTouchEvent", "(Landroid/view/MotionEvent;)Z", [=](dx::IntrinsicContext& c) {
            static_cast<void>(check_event(c, "onInterceptTouchEvent"));
            // Host slop/delta owner intercepts before dispatch and sends one CANCEL.
            // Within the Java child protocol, use the actual non-intercepting parent method.
            const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/ViewGroup;");
            const auto index =
                c.vm.Linker().FindVtableIndex(type, "onInterceptTouchEvent", "(Landroid/view/MotionEvent;)Z");
            const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable[*index],
                                          std::array{dx::VmValue::Ref(c.receiver), c.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        });
        b.OverrideMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z", [=](dx::IntrinsicContext& c) {
            const auto action = check_event(c, "onTouchEvent");
            // Accept capture for the host's bounded delta path even without a child listener.
            // No velocity, smooth animation, EdgeEffect activation or Java gesture mirror.
            return dx::VmValue::Int(action != 0 ||
                                    CallAndroidMethod(c.vm, c.receiver, "getChildCount", "()I").AsInt() != 0);
        });
    }
    // Both axes use the reviewed idle-only EdgeEffect seam.
    b.OverrideMethod("setOverScrollMode", "(I)V", [=](dx::IntrinsicContext& c) {
        dx::IntrinsicCall fields(c);
        if (c.arguments[0].AsInt() != 2 && !fields.GetRef(edge_a).IsValid()) {
            const auto context = CallAndroidMethod(c.vm, c.receiver, "getContext", "()Landroid/content/Context;").ref;
            const auto create = [&] {
                const auto object = c.vm.NewIntrinsicInstance("Landroid/widget/EdgeEffect;");
                const auto keep = c.vm.ProtectReferences(std::array{object});
                const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(object), "<init>",
                                                                 "(Landroid/content/Context;)V");
                const auto result = c.vm.Call(*ctor, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(context)});
                if (result.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor,
                                          result.exception_message, result.exception};
                return object;
            };
            const auto first = create();
            const auto keep = c.vm.ProtectReferences(std::array{first});
            const auto second = create();
            fields.SetRef(edge_a, first);
            fields.SetRef(edge_b, second);
        } else if (c.arguments[0].AsInt() == 2) {
            fields.SetRef(edge_a, dx::VmObjectRef{});
            fields.SetRef(edge_b, dx::VmObjectRef{});
        }
        const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto index = c.vm.Linker().FindVtableIndex(type, "setOverScrollMode", "(I)V");
        const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable[*index],
                                      std::array{dx::VmValue::Ref(c.receiver), c.arguments[0]});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    b.DirectMethod("initScrollView", "()V", [=](dx::IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto context = CallAndroidMethod(vm, c.receiver, "getContext", "()Landroid/content/Context;").ref;
        const auto resources = CallAndroidMethod(vm, context, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = vm.ProtectReferences(std::array{context, resources});
        const auto metrics =
            CallAndroidMethod(vm, resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref;
        const auto configuration =
            CallAndroidMethod(vm, resources, "getConfiguration", "()Landroid/content/res/Configuration;").ref;
        const auto input_roots = vm.ProtectReferences(std::array{metrics, configuration});
        const auto read = [&](dx::VmObjectRef object, const char* name, const char* type) {
            return vm.Model().InstanceSlots(object)
                [vm.Linker().Field(*vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type)).slot];
        };
        const auto density = std::bit_cast<float>(read(metrics, "density", "F").bits);
        if (!std::isfinite(density) || density <= 0 || density > 8)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid scroll metrics density"};
        const auto size_density = density * ((read(configuration, "screenLayout", "I").bits & 0xf) >= 4 ? 1.5f : 1.0f);
        // ViewConfiguration.java:276-340 and pinned config.xml: touchSlop=8dp.
        // No ViewConfiguration instance is fabricated: unrelated WMS/menu/cache facts stay unavailable.
        const std::array values{static_cast<int>(density * 8.0f + .5f), static_cast<int>(density * 50.0f + .5f),
                                static_cast<int>(density * 8000.0f + .5f), 0,
                                static_cast<int>(size_density * 6.0f + .5f)};
        const auto type = vm.Linker().ResolveDescriptor("Landroid/widget/OverScroller;");
        const auto initialized = vm.EnsureClassInitialized(type);
        if (initialized.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                  initialized.exception_message, initialized.exception};
        const auto object = vm.NewIntrinsicInstance("Landroid/widget/OverScroller;");
        const auto object_root = vm.ProtectReferences(std::array{object});
        const auto ctor = vm.Linker().FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        const auto result = vm.Call(*ctor, std::array{dx::VmValue::Ref(object), dx::VmValue::Ref(context)});
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        dx::IntrinsicCall fields(c);
        fields.SetRef(scroller, object);
        for (std::size_t i = 0; i < values.size(); ++i)
            fields.SetInt(config[i], values[i]);
        static_cast<void>(CallAndroidMethod(vm, c.receiver, "setFocusable", "(Z)V", {dx::VmValue::Int(1)}));
        static_cast<void>(
            CallAndroidMethod(vm, c.receiver, "setDescendantFocusability", "(I)V", {dx::VmValue::Int(0x40000)}));
        static_cast<void>(CallAndroidMethod(vm, c.receiver, "setWillNotDraw", "(Z)V", {dx::VmValue::Int(0)}));
        return dx::VmValue::Void();
    });
    return std::move(b).Build();
}
}  // namespace

Decl Declare_android_widget_EdgeEffect(const Context&) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/widget/EdgeEffect;", "Ljava/lang/Object;");
    const auto state = b.BoundInstanceField("mState", "I", dx::kAccPrivate);
    const auto pull = b.BoundInstanceField("mPullDistance", "F", dx::kAccPrivate);
    const auto bounds = b.BoundInstanceField("mBounds", "Landroid/graphics/Rect;", dx::kAccPrivate | dx::kAccFinal);
    const auto interpolator =
        b.BoundInstanceField("mInterpolator", "Landroid/view/animation/Interpolator;", dx::kAccPrivate | dx::kAccFinal);
    const auto min_width = b.BoundInstanceField("mMinWidth", "I", dx::kAccPrivate | dx::kAccFinal);
    b.Constructor("(Landroid/content/Context;)V", [=](dx::IntrinsicContext& c) {
        const auto resources =
            CallAndroidMethod(c.vm, c.arguments[0].ref, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = c.vm.ProtectReferences(std::array{resources});
        const auto metrics =
            CallAndroidMethod(c.vm, resources, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;").ref;
        const auto field = c.vm.Linker().FindFieldRecursive(c.vm.Model().ObjectClass(metrics), "density", "F");
        const auto density =
            std::bit_cast<float>(c.vm.Model().InstanceSlots(metrics)[c.vm.Linker().Field(*field).slot].bits);
        if (!std::isfinite(density) || density <= 0 || density > 8)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid EdgeEffect metrics density"};
        const auto create = [&](const char* name) {
            const auto object = c.vm.NewIntrinsicInstance(name);
            const auto keep = c.vm.ProtectReferences(std::array{object});
            const auto result =
                c.vm.Call(*c.vm.Linker().FindDirectMethod(c.vm.Model().ObjectClass(object), "<init>", "()V"),
                          std::array{dx::VmValue::Ref(object)});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return object;
        };
        dx::IntrinsicCall fields(c);
        fields.SetInt(state, 0);
        fields.SetRef(bounds, create("Landroid/graphics/Rect;"));
        fields.SetRef(interpolator, create("Landroid/view/animation/DecelerateInterpolator;"));
        fields.SetInt(min_width, static_cast<int>(density * 300 + .5f));
        // mEdge/mGlow and derived dimensions remain failure: unsupported glow resources.
        return dx::VmValue::Void();
    });
    b.VirtualMethod("onRelease", "()V", [=](dx::IntrinsicContext& c) {
        dx::IntrinsicCall fields(c);
        if (fields.GetInt(state) != 0) {
            if (auto* ledger = c.vm.Ledger()) ledger->RecordUnimplemented("dexvm.scroll_edge_effect", 0);
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "EdgeEffect glow resources unsupported"};
        }
        fields.SetFloat(pull, 0.0F);
        return dx::VmValue::Void();
    });
    return std::move(b).Build();
}

Decl Declare_android_widget_HorizontalScrollView(const Context& context) {
    return ExperimentalScrollView(context, "Landroid/widget/HorizontalScrollView;");
}

Decl Declare_android_widget_ScrollView(const Context& context) {
    return ExperimentalScrollView(context, "Landroid/widget/ScrollView;");
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableLayout.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableLayout(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableLayout;", "Landroid/widget/LinearLayout;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;"), "<init>", "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> arguments{dx::VmValue::Ref(c.receiver)};
        const std::vector<dx::VmValue> parameters{c.arguments[0]};
        arguments.insert(arguments.end(), parameters.begin(), parameters.end());
        const auto result = c.vm.Call(*target, arguments);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TableRow.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_TableRow(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TableRow;", "Landroid/widget/LinearLayout;");
    builder.Constructor("(Landroid/content/Context;)V", [](dx::IntrinsicContext& c) {
        const auto target = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;"), "<init>", "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> arguments{dx::VmValue::Ref(c.receiver)};
        const std::vector<dx::VmValue> parameters{c.arguments[0]};
        arguments.insert(arguments.end(), parameters.begin(), parameters.end());
        const auto result = c.vm.Call(*target, arguments);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        return result.value;
    });
    {
        // Retain these containers' own ROM failure boundary instead of inheriting
        // the experimental View/FrameLayout algorithms through a missing overlay.
        builder.UnimplementedOverride("onMeasure", "(II)V", dx::kAccProtected);
        builder.UnimplementedOverride("onLayout", "(ZIIII)V", dx::kAccProtected);
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_TextView.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

dx::VmValue TextStatic(dx::Interpreter& vm, const char* owner, const char* name,
                        const char* signature, std::vector<dx::VmValue> arguments) {
    const auto type = vm.Linker().ResolveDescriptor(owner);
    const auto method = vm.Linker().FindDirectMethod(type, name, signature);
    if (!method) throw dx::DexVmError(dx::DexVmErrorReason::unresolved_reference, name);
    const auto outcome = vm.Call(*method, arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor,
                              outcome.exception_message, outcome.exception};
    return outcome.value;
}

ui::UiNodeId TextNode(dx::IntrinsicContext& call, const Context& context) {
    return EnsureViewUiNode(
        *context, call.receiver, UiClassForObject(call.vm, call.receiver));
}

std::uint32_t AndroidColorToRgba(const std::uint32_t argb) {
    return ((argb & 0x00ffffffU) << 8U) | (argb >> 24U);
}

}  // namespace

namespace {
// Read the actual ROM R.styleable, including its index constants. No parallel
// index/default table is introduced by the adapter.
struct TextStyleable final {
    dx::VmObjectRef array;
    std::map<int, std::string> names;
};

TextStyleable RomTextAttributes(dx::Interpreter& vm, std::string_view group) {
    const auto owner = vm.Linker().ResolveDescriptor("Lcom/android/internal/R$styleable;");
    const auto initialized = vm.EnsureClassInitialized(owner);
    if (initialized.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor, initialized.exception_message,
                              initialized.exception};
    TextStyleable result;
    const auto& cls = vm.Linker().Class(owner);
    const auto prefix = std::string(group) + "_";
    for (const auto id : cls.own_static_fields) {
        const auto& f = vm.Linker().Field(id);
        if (f.name == group && f.descriptor == "[I") result.array = dx::VmObjectRef{cls.static_storage[f.slot]};
        if (f.name.starts_with(prefix) && f.descriptor == "I")
            result.names.emplace(static_cast<int>(cls.static_storage[f.slot]), f.name.substr(prefix.size()));
    }
    if (!result.array.IsValid())
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant, "ROM styleable missing");
    return result;
}

[[noreturn]] void ImageFailure(dx::Interpreter& vm, const std::string& message) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.image_leaf", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", message};
}

Decl ExperimentalImageView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/ImageView;", "Landroid/view/View;");
    const auto drawable =
        builder.BoundInstanceField("mDrawable", "Landroid/graphics/drawable/Drawable;", dx::kAccPrivate);
    const auto scale =
        builder.BoundInstanceField("mScaleType", "Landroid/widget/ImageView$ScaleType;", dx::kAccPrivate);
    const auto alpha = builder.BoundInstanceField("mAlpha", "I", dx::kAccPrivate);
    const auto matrix = builder.BoundInstanceField("mMatrix", "Landroid/graphics/Matrix;", dx::kAccPrivate);
    const auto width = builder.BoundInstanceField("mDrawableWidth", "I", dx::kAccPrivate);
    const auto height = builder.BoundInstanceField("mDrawableHeight", "I", dx::kAccPrivate);
    const auto compat = builder.BoundInstanceField("mAdjustViewBoundsCompat", "Z", dx::kAccPrivate);
    const auto maxw = builder.BoundInstanceField("mMaxWidth", "I", dx::kAccPrivate);
    const auto maxh = builder.BoundInstanceField("mMaxHeight", "I", dx::kAccPrivate);
    const auto baseline = builder.BoundInstanceField("mBaseline", "I", dx::kAccPrivate);
    const auto src_rect = builder.BoundInstanceField("mTempSrc", "Landroid/graphics/RectF;", dx::kAccPrivate);
    const auto dst_rect = builder.BoundInstanceField("mTempDst", "Landroid/graphics/RectF;", dx::kAccPrivate);
    const auto changed = [](dx::IntrinsicContext& c) {
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "requestLayout", "()V"));
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
    };
    const auto init = [=](dx::IntrinsicContext& c) {
        auto& vm = c.vm;
        const bool attributes = c.arguments.size() != 1;
        const auto view = vm.Linker().ResolveDescriptor("Landroid/view/View;");
        const auto ctor = vm.Linker().FindDirectMethod(
            view, "<init>",
            attributes ? "(Landroid/content/Context;Landroid/util/AttributeSet;I)V" : "(Landroid/content/Context;)V");
        std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver)};
        args.insert(args.end(), c.arguments.begin(), c.arguments.end());
        const auto result = vm.Call(*ctor, args);
        if (result.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
        dx::IntrinsicCall fields(c);
        fields.SetInt(alpha, 255);
        fields.SetInt(maxw, INT32_MAX);
        fields.SetInt(maxh, INT32_MAX);
        fields.SetInt(baseline, -1);
        fields.SetInt(width, 0);
        fields.SetInt(height, 0);
        const auto allocate = [&](const char* name) {
            const auto type = vm.Linker().ResolveDescriptor(name);
            const auto initialized = vm.EnsureClassInitialized(type);
            if (initialized.exception.IsValid())
                throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                      initialized.exception_message, initialized.exception};
            const auto value = vm.NewIntrinsicInstance(name);
            const auto root = vm.ProtectReferences(std::array{value});
            const auto out =
                vm.Call(*vm.Linker().FindDirectMethod(type, "<init>", "()V"), std::array{dx::VmValue::Ref(value)});
            if (out.exception.IsValid())
                throw dx::VmJavaThrow{vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                      out.exception};
            return value;
        };
        fields.SetRef(matrix, allocate("Landroid/graphics/Matrix;"));
        fields.SetRef(src_rect, allocate("Landroid/graphics/RectF;"));
        fields.SetRef(dst_rect, allocate("Landroid/graphics/RectF;"));
        const auto enum_type = vm.Linker().ResolveDescriptor("Landroid/widget/ImageView$ScaleType;");
        const auto sf = vm.Linker().FindFieldRecursive(enum_type, "FIT_CENTER", "Landroid/widget/ImageView$ScaleType;");
        fields.SetRef(scale, dx::VmObjectRef{vm.Linker().Class(enum_type).static_storage[vm.Linker().Field(*sf).slot]});
        const auto info =
            CallAndroidMethod(vm, c.arguments[0].ref, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;")
                .ref;
        const auto target = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(info), "targetSdkVersion", "I");
        fields.SetInt(compat,
                      static_cast<int>(vm.Model().InstanceSlots(info)[vm.Linker().Field(*target).slot].bits) <= 17);
        if (attributes) {
            const auto attrs = c.arguments[1].ref;
            const auto group = RomTextAttributes(vm, "ImageView");
            if (attrs.IsValid()) {
                std::set<std::uint32_t> known;
                for (auto name : {"View", "ImageView", "ViewGroup_Layout", "ViewGroup_MarginLayout",
                                  "FrameLayout_Layout", "LinearLayout_Layout", "RelativeLayout_Layout"}) {
                    const auto array = RomTextAttributes(vm, name).array;
                    for (JniSize i = 0; i < vm.Model().ArrayLength(array); ++i)
                        known.insert(static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, i)));
                }
                const auto count = CallAndroidMethod(vm, attrs, "getAttributeCount", "()I").AsInt();
                for (int i = 0; i < count; ++i) {
                    const auto id =
                        CallAndroidMethod(vm, attrs, "getAttributeNameResource", "(I)I", {dx::VmValue::Int(i)}).cat1;
                    if (id == 0 && vm.StringUtf8(CallAndroidMethod(vm, attrs, "getAttributeName",
                                                                   "(I)Ljava/lang/String;", {dx::VmValue::Int(i)})
                                                     .ref) == "style")
                        continue;
                    if (!known.contains(id)) ImageFailure(vm, "unknown ImageView XML attribute");
                }
            }
            const auto theme =
                CallAndroidMethod(vm, c.arguments[0].ref, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
            const auto theme_root = vm.ProtectReferences(std::array{theme});
            const auto array =
                CallAndroidMethod(vm, theme, "obtainStyledAttributes",
                                  "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
                                  {c.arguments[1], dx::VmValue::Ref(group.array), c.arguments[2], dx::VmValue::Int(0)})
                    .ref;
            const auto array_root = vm.ProtectReferences(std::array{array});
            std::map<std::string, int> indices;
            for (const auto& [index, name] : group.names)
                indices.emplace(name, index);
            const auto get = [&](const char* name, const char* method, int fallback) {
                return CallAndroidMethod(vm, array, method, "(II)I",
                                         {dx::VmValue::Int(indices.at(name)), dx::VmValue::Int(fallback)})
                    .AsInt();
            };
            const auto boolean = [&](const char* name) {
                return CallAndroidMethod(vm, array, "getBoolean", "(IZ)Z",
                                         {dx::VmValue::Int(indices.at(name)), dx::VmValue::Int(0)})
                    .AsInt();
            };
            if (boolean("baselineAlignBottom") || get("baseline", "getDimensionPixelSize", -1) != -1 ||
                boolean("adjustViewBounds") || get("maxWidth", "getDimensionPixelSize", INT32_MAX) != INT32_MAX ||
                get("maxHeight", "getDimensionPixelSize", INT32_MAX) != INT32_MAX || get("tint", "getInt", 0) != 0 ||
                boolean("cropToPadding"))
                ImageFailure(vm, "unsupported ImageView bounds/baseline/tint/crop request");
            const auto st = get("scaleType", "getInt", -1);
            if (st >= 0) {
                if (st >= 8) ImageFailure(vm, "invalid ImageView scaleType");
                const auto values =
                    vm.Linker().FindFieldRecursive(vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;"),
                                                   "sScaleTypeArray", "[Landroid/widget/ImageView$ScaleType;");
                const auto value_array =
                    dx::VmObjectRef{vm.Linker()
                                        .Class(vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;"))
                                        .static_storage[vm.Linker().Field(*values).slot]};
                static_cast<void>(CallAndroidMethod(vm, c.receiver, "setScaleType",
                                                    "(Landroid/widget/ImageView$ScaleType;)V",
                                                    {dx::VmValue::Ref(vm.Model().GetObjectElement(value_array, st))}));
            }
            const auto image = CallAndroidMethod(vm, array, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                                 {dx::VmValue::Int(indices.at("src"))})
                                   .ref;
            const auto image_root = vm.ProtectReferences(std::array{image});
            if (image.IsValid())
                static_cast<void>(CallAndroidMethod(vm, c.receiver, "setImageDrawable",
                                                    "(Landroid/graphics/drawable/Drawable;)V",
                                                    {dx::VmValue::Ref(image)}));
            const auto value = get("drawableAlpha", "getInt", 255);
            if (value != 255)
                static_cast<void>(
                    CallAndroidMethod(vm, c.receiver, "setImageAlpha", "(I)V", {dx::VmValue::Int(value)}));
            static_cast<void>(CallAndroidMethod(vm, array, "recycle", "()V"));
        }
        ProjectAndroidImage(vm, *context, c.receiver);
        return dx::VmValue::Void();
    };
    builder.Constructor("(Landroid/content/Context;)V", init);
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", init);
    builder.VirtualMethod("setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", [=](dx::IntrinsicContext& c) {
        const auto value = c.arguments[0].ref;
        if (value.IsValid()) {
            const auto& type = c.vm.Linker().Class(c.vm.Model().ObjectClass(value)).descriptor;
            const auto found = context->ui_drawables.find(value.Value());
            if (found == context->ui_drawables.end() || (type != "Landroid/graphics/drawable/BitmapDrawable;" &&
                                                         type != "Landroid/graphics/drawable/ColorDrawable;"))
                ImageFailure(c.vm, "unreviewed Drawable");
            const auto source = found->second.bitmap_drawable
                                    ? CallAndroidMethod(c.vm, value, "getBitmap", "()Landroid/graphics/Bitmap;").ref
                                    : dx::VmObjectRef{};
            if (source.IsValid()) {
                const auto b = context->bitmaps.find(source.Value());
                if (b == context->bitmaps.end() || b->second.recycled)
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "recycled image bitmap"};
            }
        }
        dx::IntrinsicCall f(c);
        f.SetRef(drawable, value);
        f.SetInt(width, value.IsValid() ? CallAndroidMethod(c.vm, value, "getIntrinsicWidth", "()I").AsInt() : -1);
        f.SetInt(height, value.IsValid() ? CallAndroidMethod(c.vm, value, "getIntrinsicHeight", "()I").AsInt() : -1);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        changed(c);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setImageResource", "(I)V", [](dx::IntrinsicContext& c) {
        dx::VmObjectRef image;
        if (c.arguments[0].AsInt() != 0) {
            const auto resources =
                CallAndroidMethod(c.vm, c.receiver, "getResources", "()Landroid/content/res/Resources;").ref;
            image = CallAndroidMethod(c.vm, resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                      {c.arguments[0]})
                        .ref;
        }
        const auto root = c.vm.ProtectReferences(std::array{image});
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "setImageDrawable",
                                            "(Landroid/graphics/drawable/Drawable;)V", {dx::VmValue::Ref(image)}));
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("setScaleType", "(Landroid/widget/ImageView$ScaleType;)V", [=](dx::IntrinsicContext& c) {
        const auto value = c.arguments[0].ref;
        if (!value.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "scaleType"};
        const auto ordinal = CallAndroidMethod(c.vm, value, "ordinal", "()I").AsInt();
        if (ordinal != 1 && ordinal != 3 && ordinal != 5 && ordinal != 6 && ordinal != 7)
            ImageFailure(c.vm, "unsupported ImageView scaleType");
        dx::IntrinsicCall(c).SetRef(scale, value);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        changed(c);
        return dx::VmValue::Void();
    });
    const auto set_alpha = [=](dx::IntrinsicContext& c) {
        dx::IntrinsicCall(c).SetInt(alpha, c.arguments[0].AsInt() & 255);
        ProjectAndroidImage(c.vm, *context, c.receiver);
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
        return dx::VmValue::Void();
    };
    builder.VirtualMethod("setAlpha", "(I)V", set_alpha);
    builder.VirtualMethod("setImageAlpha", "(I)V", set_alpha);
    builder.VirtualMethod("onMeasure", "(II)V", [=](dx::IntrinsicContext& c) {
        if (c.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Image measurement requires owner"};
        ProjectAndroidImage(c.vm, *context, c.receiver);
        const auto& node = *context->ui_tree.Get(*FindViewUiNode(*context, c.receiver.Value()));
        dx::IntrinsicCall fields(c);
        const bool present = fields.GetRef(drawable).IsValid();
        fields.SetInt(width, present ? node.intrinsic.width : -1);
        fields.SetInt(height, present ? node.intrinsic.height : -1);
        const ui::ImageLeafInput input{
            present ? ui::Size{std::max(1, node.intrinsic.width), std::max(1, node.intrinsic.height)} : ui::Size{},
            ReadAndroidViewPadding(c.vm, *context, c.receiver),
            {CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumWidth", "()I").AsInt(),
             CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumHeight", "()I").AsInt()}};
        const auto spec = [&](int bits) {
            const auto mode = static_cast<std::uint32_t>(bits) >> 30U;
            if (mode == 3) ImageFailure(c.vm, "invalid MeasureSpec");
            return ui::MeasureSpec{mode == 1   ? ui::MeasureMode::Exactly
                                   : mode == 2 ? ui::MeasureMode::AtMost
                                               : ui::MeasureMode::Unspecified,
                                   bits & 0x3fffffff};
        };
        const auto result = ui::MeasureImageLeaf(input, spec(c.arguments[0].AsInt()), spec(c.arguments[1].AsInt()));
        static_cast<void>(
            CallAndroidMethod(c.vm, c.receiver, "setMeasuredDimension", "(II)V",
                              {dx::VmValue::Int(static_cast<int>(static_cast<std::uint32_t>(result.constrained.width) |
                                                                 result.width_state)),
                               dx::VmValue::Int(static_cast<int>(static_cast<std::uint32_t>(result.constrained.height) |
                                                                 result.height_state))}));
        return dx::VmValue::Void();
    });
    // No Drawable layout/state/draw stack is claimed. Execute the genuine View
    // protocol for geometry and visibility; the leaf renderer consumes backing.
    for (const auto& [name, sig] :
         std::array<std::pair<const char*, const char*>, 5>{{{"setFrame", "(IIII)Z"},
                                                             {"onRtlPropertiesChanged", "(I)V"},
                                                             {"drawableStateChanged", "()V"},
                                                             {"setSelected", "(Z)V"},
                                                             {"setVisibility", "(I)V"}}}) {
        builder.VirtualMethod(name, sig, [name, sig](dx::IntrinsicContext& c) {
            if (std::string_view(name) == "onRtlPropertiesChanged" && c.arguments[0].AsInt() != 0)
                ImageFailure(c.vm, "RTL image unsupported");
            const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto index = c.vm.Linker().FindVtableIndex(type, name, sig);
            std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver)};
            args.insert(args.end(), c.arguments.begin(), c.arguments.end());
            const auto out = c.vm.Call(c.vm.Linker().Class(type).vtable.at(*index), args);
            if (out.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                      out.exception};
            return out.value;
        });
    }
    return std::move(builder).Build();
}

[[noreturn]] void TextInputFailure(dx::Interpreter& vm, std::string message) {
    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.text_constructor", 0);
    throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", std::move(message)};
}

Decl ExperimentalTextView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/TextView;", "Landroid/view/View;");
    {
        builder.OverrideMethod("onWindowFocusChanged", "(Z)V", [](dx::IntrinsicContext& call) {
            const auto type = call.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto index = call.vm.Linker().FindVtableIndex(type, "onWindowFocusChanged", "(Z)V");
            const auto result = call.vm.Call(call.vm.Linker().Class(type).vtable.at(*index),
                                             std::array{dx::VmValue::Ref(call.receiver), call.arguments[0]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor,
                                      result.exception_message, result.exception};
            return dx::VmValue::Void();
        });
    }
    const auto text_colors =
        builder.BoundInstanceField("mTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto hint_colors =
        builder.BoundInstanceField("mHintTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto link_colors =
        builder.BoundInstanceField("mLinkTextColor", "Landroid/content/res/ColorStateList;", dx::kAccPrivate);
    const auto highlight = builder.BoundInstanceField("mHighlightColor", "I", dx::kAccPrivate);
    const std::array resource_fields{builder.BoundInstanceField("mTextSelectHandleLeftRes", "I", 0),
                                     builder.BoundInstanceField("mTextSelectHandleRightRes", "I", 0),
                                     builder.BoundInstanceField("mTextSelectHandleRes", "I", 0),
                                     builder.BoundInstanceField("mCursorDrawableRes", "I", 0),
                                     builder.BoundInstanceField("mTextEditSuggestionItemLayout", "I", 0)};
    const auto update_color = [context, text_colors](dx::IntrinsicContext& c) {
        const auto colors = dx::IntrinsicCall(c).GetRef(text_colors);
        if (!colors.IsValid()) return;
        const auto roots = c.vm.ProtectReferences(std::array{colors});
        const auto states = CallAndroidMethod(c.vm, c.receiver, "getDrawableState", "()[I").ref;
        const auto color = CallAndroidMethod(c.vm, colors, "getColorForState", "([II)I",
                                             {dx::VmValue::Ref(states), dx::VmValue::Int(0)})
                               .AsInt();
        const auto node = TextNode(c, context);
        context->ui_tree.Get(node)->text_color = AndroidColorToRgba(static_cast<std::uint32_t>(color));
        context->ui_tree.MarkDrawDirty(node);
    };
    const auto chain = [](int kind) {
        return dx::IntrinsicHandler([kind](dx::IntrinsicContext& c) {
            const auto method =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Landroid/widget/TextView;"), "<init>",
                                               kind == 1 ? "(Landroid/content/Context;Landroid/util/AttributeSet;)V"
                                                         : "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
            std::vector<dx::VmValue> args{dx::VmValue::Ref(c.receiver), c.arguments[0],
                                          kind == 1 ? dx::VmValue::Ref(dx::VmObjectRef{}) : c.arguments[1]};
            // AOSP TextView.java:618 and pinned ROM constructor constant.
            if (kind == 2) args.push_back(dx::VmValue::Int(0x01010084));
            const auto result = c.vm.Call(*method, args);
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        });
    };
    builder.Constructor("(Landroid/content/Context;)V", chain(1));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;)V", chain(2));
    builder.Constructor("(Landroid/content/Context;Landroid/util/AttributeSet;I)V", [context, text_colors, hint_colors,
                                                                                     link_colors, highlight,
                                                                                     resource_fields, update_color](
                                                                                        dx::IntrinsicContext& c) {
        auto& vm = c.vm;
        const auto owner = c.arguments[0].ref, attrs = c.arguments[1].ref;
        const bool editing = UiClassForObject(vm, c.receiver) == ui::UiClass::EditText;
        const auto style = c.arguments[2];
        const auto super = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor("Landroid/view/View;"), "<init>",
                                                        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        const auto initialized =
            vm.Call(*super, std::array{dx::VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1], style});
        if (initialized.exception.IsValid())
            throw dx::VmJavaThrow{vm.Linker().Class(initialized.exception_class).descriptor,
                                  initialized.exception_message, initialized.exception};
        // Pinned ROM constructor pc210..217, before style consumption.
        dx::IntrinsicCall(c).SetInt(highlight, 0x6633b5e5);
        const auto theme = CallAndroidMethod(vm, owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto theme_root = vm.ProtectReferences(std::array{theme});
        const auto tva = RomTextAttributes(vm, "TextViewAppearance"), ta = RomTextAttributes(vm, "TextAppearance"),
                   tv = RomTextAttributes(vm, "TextView"), view = RomTextAttributes(vm, "View");
        if (attrs.IsValid()) {
            std::set<std::uint32_t> known;
            for (const auto group : {"View", "TextView", "ViewGroup_Layout", "ViewGroup_MarginLayout",
                                     "FrameLayout_Layout", "LinearLayout_Layout", "RelativeLayout_Layout"}) {
                const auto array = RomTextAttributes(vm, group).array;
                for (JniSize i = 0; i < vm.Model().ArrayLength(array); ++i)
                    known.insert(static_cast<std::uint32_t>(vm.Model().GetPrimitiveElement(array, i)));
            }
            const auto count = CallAndroidMethod(vm, attrs, "getAttributeCount", "()I").AsInt();
            for (int i = 0; i < count; ++i) {
                const auto id = static_cast<std::uint32_t>(
                    CallAndroidMethod(vm, attrs, "getAttributeNameResource", "(I)I", {dx::VmValue::Int(i)}).AsInt());
                // The unqualified XML style marker is handled by the ROM parser.
                if (id == 0 && vm.StringUtf8(CallAndroidMethod(vm, attrs, "getAttributeName", "(I)Ljava/lang/String;",
                                                               {dx::VmValue::Int(i)})
                                                 .ref) == "style")
                    continue;
                if (!known.contains(id)) TextInputFailure(vm, "unknown TextView XML attribute");
            }
        }
        const auto obtain = [&](dx::VmObjectRef array) {
            return CallAndroidMethod(vm, theme, "obtainStyledAttributes",
                                     "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;",
                                     {dx::VmValue::Ref(attrs), dx::VmValue::Ref(array), style, dx::VmValue::Int(0)})
                .ref;
        };
        const auto appearance_ref = obtain(tva.array);
        const auto appearance_root = vm.ProtectReferences(std::array{appearance_ref});
        const auto ap =
            CallAndroidMethod(vm, appearance_ref, "getResourceId", "(II)I", {dx::VmValue::Int(0), dx::VmValue::Int(-1)})
                .AsInt();
        static_cast<void>(CallAndroidMethod(vm, appearance_ref, "recycle", "()V"));
        // Audited AOSP constructor locals/default field assignments: 15px,
        // black, START|TOP resolved below, unlimited lines; not UiNode defaults.
        int size = 15, family_index = -1, style_index = -1, max_lines = 0x7fffffff, gravity = 0x33;
        std::uint32_t color = 0xff000000;
        std::u16string text;
        std::string family;
        bool has_family = false, single = false;
        bool editable = editing;
        int ellipsize = -1;
        std::array<ui::CompoundDrawable, 4> compounds{};
        std::array<dx::VmObjectRef, 4> compound_refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{},
                                                     dx::VmObjectRef{}};
        const auto retained = vm.Model().NewObjectArray(vm.Linker().ResolveDescriptor("[Ljava/lang/Object;"),
                                                        vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), 5);
        const auto retained_root = vm.ProtectReferences(std::array{retained});
        const auto consume = [&](dx::VmObjectRef values, const TextStyleable& group) {
            const auto count = CallAndroidMethod(vm, values, "getIndexCount", "()I").AsInt();
            for (int i = 0; i < count; ++i) {
                const auto index = CallAndroidMethod(vm, values, "getIndex", "(I)I", {dx::VmValue::Int(i)}).AsInt();
                const auto found = group.names.find(index);
                if (found == group.names.end()) TextInputFailure(vm, "unknown ROM TextView attribute index");
                const auto& name = found->second;
                const auto integer = [&](int fallback) {
                    return CallAndroidMethod(vm, values, "getInt", "(II)I",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(fallback)})
                        .AsInt();
                };
                const auto boolean = [&](bool fallback) {
                    return CallAndroidMethod(vm, values, "getBoolean", "(IZ)Z",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(fallback)})
                               .AsInt() != 0;
                };
                const std::array<std::string_view, 5> resource_names{"textSelectHandleLeft", "textSelectHandleRight",
                                                                     "textSelectHandle", "textCursorDrawable",
                                                                     "textEditSuggestionItemLayout"};
                const auto resource = std::find(resource_names.begin(), resource_names.end(), name);
                if (resource != resource_names.end()) {
                    const auto id = CallAndroidMethod(vm, values, "getResourceId", "(II)I",
                                                      {dx::VmValue::Int(index), dx::VmValue::Int(0)})
                                        .AsInt();
                    dx::IntrinsicCall(c).SetInt(
                        resource_fields[static_cast<std::size_t>(resource - resource_names.begin())], id);
                    continue;
                }
                if (name == "textAppearance") continue;  // Already consumed through TextViewAppearance.
                if (name == "textSize")
                    size = CallAndroidMethod(vm, values, "getDimensionPixelSize", "(II)I",
                                             {dx::VmValue::Int(index), dx::VmValue::Int(size)})
                               .AsInt();
                else if (name == "textColor") {
                    const auto list =
                        CallAndroidMethod(vm, values, "getColorStateList", "(I)Landroid/content/res/ColorStateList;",
                                          {dx::VmValue::Int(index)})
                            .ref;
                    const auto root = vm.ProtectReferences(std::array{list});
                    if (!list.IsValid()) TextInputFailure(vm, "null textColor");
                    dx::IntrinsicCall(c).SetRef(text_colors, list);
                    color = static_cast<std::uint32_t>(CallAndroidMethod(vm, list, "getDefaultColor", "()I").AsInt());
                } else if (name == "textColorHint" || name == "textColorLink") {
                    const auto list =
                        CallAndroidMethod(vm, values, "getColorStateList", "(I)Landroid/content/res/ColorStateList;",
                                          {dx::VmValue::Int(index)})
                            .ref;
                    dx::IntrinsicCall(c).SetRef(name == "textColorHint" ? hint_colors : link_colors, list);
                } else if (name == "textColorHighlight") {
                    const auto selected = CallAndroidMethod(vm, values, "getColor", "(II)I",
                                                            {dx::VmValue::Int(index), dx::VmValue::Int(0)})
                                              .AsInt();
                    dx::IntrinsicCall(c).SetInt(highlight, selected);
                } else if (name == "fontFamily") {
                    const auto ref =
                        CallAndroidMethod(vm, values, "getString", "(I)Ljava/lang/String;", {dx::VmValue::Int(index)})
                            .ref;
                    has_family = ref.IsValid();
                    family = has_family ? vm.StringUtf8(ref) : "";
                } else if (name == "typeface")
                    family_index = integer(family_index);
                else if (name == "textStyle")
                    style_index = integer(style_index);
                else if (name == "text") {
                    const auto ref = CallAndroidMethod(vm, values, "getText", "(I)Ljava/lang/CharSequence;",
                                                       {dx::VmValue::Int(index)})
                                         .ref;
                    if (ref.IsValid() &&
                        vm.Linker().Class(vm.Model().ObjectClass(ref)).descriptor != "Ljava/lang/String;")
                        TextInputFailure(vm, "plain String required");
                    text = ref.IsValid() ? vm.Model().StringValue(ref) : u"";
                } else if (name == "gravity") {
                    gravity = integer(-1);
                    if ((gravity & ~0x77) ||
                        ((gravity & 7) != 0 && (gravity & 7) != 1 && (gravity & 7) != 3 && (gravity & 7) != 5) ||
                        ((gravity & 0x70) != 0 && (gravity & 0x70) != 0x10 && (gravity & 0x70) != 0x30 &&
                         (gravity & 0x70) != 0x50))
                        TextInputFailure(vm, "unsupported text gravity");
                    if (!(gravity & 7)) gravity |= 3;
                    if (!(gravity & 0x70)) gravity |= 0x30;
                } else if (name == "maxLines") {
                    max_lines = integer(-1);
                    if (max_lines < 1) TextInputFailure(vm, "maxLines must be positive");
                } else if (name == "singleLine")
                    single = boolean(single);
                else if (name == "ellipsize") {
                    ellipsize = integer(-1);
                    if (ellipsize != 0) TextInputFailure(vm, "ellipsize is unsupported");
                } else if (name == "drawableLeft" || name == "drawableTop" || name == "drawableRight" ||
                           name == "drawableBottom") {
                    const std::size_t slot = name == "drawableLeft"    ? 0
                                             : name == "drawableTop"   ? 1
                                             : name == "drawableRight" ? 2
                                                                       : 3;
                    const auto ref =
                        CallAndroidMethod(vm, values, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                          {dx::VmValue::Int(index)})
                            .ref;
                    if (ref.IsValid()) {
                        vm.Model().SetObjectElement(retained, static_cast<JniSize>(slot), ref);
                        const auto backing = context->ui_drawables.find(ref.Value());
                        if (backing == context->ui_drawables.end() || !backing->second.bitmap_drawable ||
                            !backing->second.leaf_pixels)
                            TextInputFailure(vm, "compound requires resource BitmapDrawable");
                        const auto width = CallAndroidMethod(vm, ref, "getIntrinsicWidth", "()I").AsInt();
                        const auto height = CallAndroidMethod(vm, ref, "getIntrinsicHeight", "()I").AsInt();
                        compounds[slot] = {backing->second.resource_id, width, height};
                        compound_refs[slot] = ref;
                    }
                } else if (name == "textEditPasteWindowLayout" || name == "textEditNoPasteWindowLayout" ||
                           name == "textEditSidePasteWindowLayout" || name == "textEditSideNoPasteWindowLayout") {
                    // Fixed ROM TextView constructor pc625 sends these legacy slots
                    // to its default branch (pc628); no getter, field write or load.
                } else if (name == "enabled") {
                    // The View super constructor consumed this same value.
                } else if (name == "includeFontPadding" || name == "cursorVisible" || name == "linksClickable") {
                    if (!boolean(true)) TextInputFailure(vm, "unsupported non-default " + name);
                } else if (name == "editable") {
                    editable = boolean(editing);
                    if (editable != editing) TextInputFailure(vm, "editable override outside bounded EditText");
                } else if (name == "bufferType") {
                    const auto type = integer(0);
                    if (type != 0 && !(editing && (type == 1 || type == 2)))
                        TextInputFailure(vm, "unsupported bufferType");
                } else if (name == "textAllCaps" || name == "password" || name == "textIsSelectable" ||
                           name == "autoText" || name == "phoneNumber" || name == "selectAllOnFocus" ||
                           name == "scrollHorizontally") {
                    if (boolean(false)) TextInputFailure(vm, "unsupported " + name);
                } else if (name == "autoLink" || name == "shadowColor") {
                    if (integer(0) != 0) TextInputFailure(vm, "unsupported " + name);
                } else {
                    TextInputFailure(vm, "unsupported TextView attribute: " + name);
                }
            }
        };
        if (ap != -1) {
            const auto values =
                CallAndroidMethod(vm, theme, "obtainStyledAttributes", "(I[I)Landroid/content/res/TypedArray;",
                                  {dx::VmValue::Int(ap), dx::VmValue::Ref(ta.array)})
                    .ref;
            const auto root = vm.ProtectReferences(std::array{values});
            consume(values, ta);
            static_cast<void>(CallAndroidMethod(vm, values, "recycle", "()V"));
        }
        const auto values = obtain(tv.array);
        const auto values_root = vm.ProtectReferences(std::array{values});
        consume(values, tv);
        static_cast<void>(CallAndroidMethod(vm, values, "recycle", "()V"));
        if (single) {
            if (ellipsize < 0) TextInputFailure(vm, "singleLine implicitly requests END ellipsize");
            if (text.find(u'\r') != text.npos) TextInputFailure(vm, "singleLine CR transformation unsupported");
            max_lines = 1;
        }
        if (family_index < -1 || family_index > 3 || style_index < -1 || style_index > 3)
            TextInputFailure(vm, "unsupported typeface/style");
        dx::VmObjectRef face;
        if (has_family || family_index > 0 || style_index > 0) {
            const auto name = has_family          ? family
                              : family_index == 1 ? "sans-serif"
                              : family_index == 2 ? "serif"
                              : family_index == 3 ? "monospace"
                                                  : "";
            if (has_family && family != "sans-serif" && family != "serif" && family != "monospace")
                TextInputFailure(vm, "unsupported fontFamily");
            const auto name_ref = vm.NewStringUtf8(name);
            const auto name_root = vm.ProtectReferences(std::array{name_ref});
            face = TextStatic(vm, "Landroid/graphics/Typeface;", "create",
                              "(Ljava/lang/String;I)Landroid/graphics/Typeface;",
                              {dx::VmValue::Ref(name_ref), dx::VmValue::Int(style_index < 0 ? 0 : style_index)})
                       .ref;
            vm.Model().SetObjectElement(retained, 4, face);
        }
        const auto font_style = face.IsValid() ? CallAndroidMethod(vm, face, "getStyle", "()I").AsInt() : 0;
        if (size < 1 || size > 128 || text.size() > 1024)
            TextInputFailure(vm, "text size/length outside bounded service");
        for (const auto unit : text)
            if (!(unit == u'\n' || (unit >= 0x20 && unit <= 0x7e) || (unit >= 0x4e00 && unit <= 0x9fff)))
                TextInputFailure(vm, "unsupported text glyph/shaping");
        try {
            static_cast<void>(ui::MeasureFixedText(text, static_cast<float>(size),
                                                   static_cast<std::uint32_t>(font_style),
                                                   context->ui_tree.FallbackFont().get()));
        } catch (const std::runtime_error& error) {
            TextInputFailure(vm, error.what());
        }
        // Default START is resolved against the real View direction, not blindly
        // treated as LEFT on an RTL View.
        if (CallAndroidMethod(vm, c.receiver, "getLayoutDirection", "()I").AsInt() != 0)
            TextInputFailure(vm, "RTL text direction unsupported");
        const auto focus_values = obtain(view.array);
        const auto focus_root = vm.ProtectReferences(std::array{focus_values});
        std::array<bool, 3> focus{editing, editing, editing};
        const auto count = CallAndroidMethod(vm, focus_values, "getIndexCount", "()I").AsInt();
        for (int i = 0; i < count; ++i) {
            const auto index = CallAndroidMethod(vm, focus_values, "getIndex", "(I)I", {dx::VmValue::Int(i)}).AsInt();
            const auto& name = view.names.at(index);
            for (std::size_t j = 0; j < 3; ++j)
                if (name == std::array{"focusable", "clickable", "longClickable"}[j])
                    focus[j] = CallAndroidMethod(vm, focus_values, "getBoolean", "(IZ)Z",
                                                 {dx::VmValue::Int(index), dx::VmValue::Int(0)})
                                   .AsInt() != 0;
        }
        static_cast<void>(CallAndroidMethod(vm, focus_values, "recycle", "()V"));
        for (std::size_t j = 0; j < 3; ++j)
            static_cast<void>(CallAndroidMethod(vm, c.receiver,
                                                std::array{"setFocusable", "setClickable", "setLongClickable"}[j],
                                                "(Z)V", {dx::VmValue::Int(focus[j])}));
        const auto node = TextNode(c, context);
        auto* state = context->ui_tree.Get(node);
        state->text = std::move(text);
        state->text_size_px = static_cast<float>(size);
        state->text_color = AndroidColorToRgba(color);
        state->text_style = static_cast<std::uint32_t>(font_style);
        state->gravity = static_cast<std::uint32_t>(gravity);
        state->max_lines = max_lines;
        state->compound_drawables = compounds;
        auto& refs = context->view_adapter_references[c.receiver.Value()];
        refs.text_single_line = single;
        refs.text_typeface = face;
        refs.text_compounds = compound_refs;
        if (editing) {
            if (!CallAndroidMethod(vm, c.receiver, "getDefaultEditable", "()Z").AsInt())
                TextInputFailure(vm, "EditText requires editable default");
            refs.text_movement =
                CallAndroidMethod(vm, c.receiver, "getDefaultMovementMethod", "()Landroid/text/method/MovementMethod;")
                    .ref;
            if (!refs.text_movement.IsValid() ||
                vm.Linker().Class(vm.Model().ObjectClass(refs.text_movement)).descriptor !=
                    "Landroid/text/method/ArrowKeyMovementMethod;")
                TextInputFailure(vm, "EditText requires bounded ArrowKeyMovementMethod");
            state->max_length = 1024;
            // ArrowKeyMovementMethod.initialize places the initial cursor at 0.
            refs.selection_start = refs.selection_end = 0;
            static_cast<void>(CallAndroidMethod(vm, c.receiver, "getText", "()Landroid/text/Editable;"));
            // Exercise the actual EditText override that forces EDITABLE,
            // while the reviewed TextView endpoint owns the bounded transaction.
            const auto initial = vm.Model().NewString(state->text);
            static_cast<void>(CallAndroidMethod(vm, c.receiver, "setText", "(Ljava/lang/CharSequence;)V",
                                                {dx::VmValue::Ref(initial)}));
        }
        if (!dx::IntrinsicCall(c).GetRef(text_colors).IsValid()) {
            const auto list = TextStatic(vm, "Landroid/content/res/ColorStateList;", "valueOf",
                                         "(I)Landroid/content/res/ColorStateList;",
                                         {dx::VmValue::Int(static_cast<std::int32_t>(color))})
                                  .ref;
            dx::IntrinsicCall(c).SetRef(text_colors, list);
        }
        update_color(c);
        context->ui_tree.MarkLayoutDirty(node);
        return dx::VmValue::Void();
    });
    const auto changed = [context](dx::IntrinsicContext& c) {
        auto& refs = context->view_adapter_references.at(c.receiver.Value());
        ++refs.text_revision;
        refs.text_measure.reset();
        refs.text_measure_cache.clear();
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "requestLayout", "()V"));
        static_cast<void>(CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V"));
        return dx::VmValue::Void();
    };
    const auto validate = [](dx::IntrinsicContext& c, const ui::UiNode& node, const std::u16string& text, float size,
                             std::uint32_t style) {
        if (text.size() > 1024 || !std::isfinite(size) || size < 1 || size > 128 || style > 3)
            TextInputFailure(c.vm, "text outside bounded service");
        for (auto ch : text)
            if (!(ch == u'\n' || (ch >= 32 && ch <= 126) || (ch >= 0x4e00 && ch <= 0x9fff)))
                TextInputFailure(c.vm, "unsupported text glyph/shaping");
        try {
            static_cast<void>(ui::MeasureFixedText(text, size, style, node.fallback_font.get()));
        } catch (const std::runtime_error& e) {
            TextInputFailure(c.vm, e.what());
        }
    };
    builder.FinalMethod(
        "setText", "(Ljava/lang/CharSequence;)V", [context, changed, validate](dx::IntrinsicContext& c) {
            if (UiClassForObject(c.vm, c.receiver) == ui::UiClass::EditText) {
                const auto type = TextStatic(c.vm, "Landroid/widget/TextView$BufferType;", "valueOf",
                                             "(Ljava/lang/String;)Landroid/widget/TextView$BufferType;",
                                             {dx::VmValue::Ref(c.vm.NewStringUtf8("EDITABLE"))})
                                      .ref;
                return CallAndroidMethod(c.vm, c.receiver, "setText",
                                         "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
                                         {c.arguments[0], dx::VmValue::Ref(type)});
            }
            const auto ref = c.arguments[0].ref;
            if (ref.IsValid() && c.vm.Linker().Class(c.vm.Model().ObjectClass(ref)).descriptor != "Ljava/lang/String;")
                TextInputFailure(c.vm, "plain String required");
            auto text = ref.IsValid() ? c.vm.Model().StringValue(ref) : std::u16string{};
            auto& n = *context->ui_tree.Get(TextNode(c, context));
            validate(c, n, text, n.text_size_px, n.text_style);
            n.text = std::move(text);
            return changed(c);
        });
    builder.VirtualMethod("setText", "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
                          [context, validate](dx::IntrinsicContext& c) {
                              if (UiClassForObject(c.vm, c.receiver) != ui::UiClass::EditText ||
                                  !c.arguments[1].ref.IsValid() ||
                                  CallAndroidMethod(c.vm, c.arguments[1].ref, "ordinal", "()I").AsInt() != 2)
                                  TextInputFailure(c.vm, "only EditText EDITABLE buffer is supported");
                              const auto text = c.arguments[0].ref;
                              if (text.IsValid() && c.vm.Linker().Class(c.vm.Model().ObjectClass(text)).descriptor !=
                                                        "Ljava/lang/String;")
                                  TextInputFailure(c.vm, "plain String required");
                              const auto value = text.IsValid() ? c.vm.Model().StringValue(text) : std::u16string{};
                              const auto& n = *context->ui_tree.Get(TextNode(c, context));
                              validate(c, n, value, n.text_size_px, n.text_style);
                              static_cast<void>(ApplyTextEdit(c.vm, *context, c.receiver, 0,
                                                              static_cast<std::int32_t>(n.text.size()), value));
                              return dx::VmValue::Void();
                          });
    for (const auto& [name, end] :
         std::array<std::pair<const char*, bool>, 2>{{{"getSelectionStart", false}, {"getSelectionEnd", true}}})
        builder.VirtualMethod(name, "()I", [context, end](dx::IntrinsicContext& c) {
            const auto& refs = context->view_adapter_references.at(c.receiver.Value());
            if (UiClassForObject(c.vm, c.receiver) != ui::UiClass::EditText) return dx::VmValue::Int(-1);
            return dx::VmValue::Int(end ? refs.selection_end : refs.selection_start);
        });
    builder.VirtualMethod("addTextChangedListener", "(Landroid/text/TextWatcher;)V",
                          [context](dx::IntrinsicContext& c) {
                              if (UiClassForObject(c.vm, c.receiver) != ui::UiClass::EditText)
                                  TextInputFailure(c.vm, "watchers require bounded EditText");
                              if (!c.arguments[0].ref.IsValid())
                                  throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "null watcher"};
                              context->text_watchers[c.receiver.Value()].push_back(c.arguments[0].ref);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("removeTextChangedListener", "(Landroid/text/TextWatcher;)V",
                          [context](dx::IntrinsicContext& c) {
                              if (UiClassForObject(c.vm, c.receiver) != ui::UiClass::EditText)
                                  TextInputFailure(c.vm, "watchers require bounded EditText");
                              auto& watchers = context->text_watchers[c.receiver.Value()];
                              const auto found = std::find(watchers.begin(), watchers.end(), c.arguments[0].ref);
                              if (found != watchers.end()) watchers.erase(found);
                              return dx::VmValue::Void();
                          });
    builder.VirtualMethod("getText", "()Ljava/lang/CharSequence;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(c.vm.Model().NewString(context->ui_tree.Get(TextNode(c, context))->text));
    });
    builder.VirtualMethod("getTextSize", "()F", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Float(context->ui_tree.Get(TextNode(c, context))->text_size_px);
    });
    builder.VirtualMethod("setTextSize", "(F)V", [](dx::IntrinsicContext& c) {
        return CallAndroidMethod(c.vm, c.receiver, "setTextSize", "(IF)V", {dx::VmValue::Int(2), c.arguments[0]});
    });
    builder.VirtualMethod("setTextSize", "(IF)V", [context, changed, validate](dx::IntrinsicContext& c) {
        const auto unit = c.arguments[0].AsInt();
        if (unit < 0 || unit > 2) TextInputFailure(c.vm, "unsupported dimension unit");
        const auto size = c.arguments[1].AsFloat() * (unit == 0   ? 1.F
                                                      : unit == 1 ? context->ui_density
                                                                  : context->ui_scaled_density);
        auto& n = *context->ui_tree.Get(TextNode(c, context));
        validate(c, n, n.text, size, n.text_style);
        n.text_size_px = size;
        return changed(c);
    });
    builder.VirtualMethod(
        "setTypeface", "(Landroid/graphics/Typeface;)V", [context, changed, validate](dx::IntrinsicContext& c) {
            const auto ref = c.arguments[0].ref;
            const auto style = ref.IsValid() ? CallAndroidMethod(c.vm, ref, "getStyle", "()I").AsInt() : 0;
            auto& n = *context->ui_tree.Get(TextNode(c, context));
            validate(c, n, n.text, n.text_size_px, static_cast<std::uint32_t>(style));
            context->view_adapter_references.at(c.receiver.Value()).text_typeface = ref;
            n.text_style = static_cast<std::uint32_t>(style);
            return changed(c);
        });
    builder.VirtualMethod("setTypeface", "(Landroid/graphics/Typeface;I)V", [](dx::IntrinsicContext& c) {
        const auto style = c.arguments[1].AsInt();
        if (style < 0 || style > 3) TextInputFailure(c.vm, "unsupported Typeface style");
        const auto ref =
            TextStatic(c.vm, "Landroid/graphics/Typeface;", "create",
                       "(Landroid/graphics/Typeface;I)Landroid/graphics/Typeface;", {c.arguments[0], c.arguments[1]})
                .ref;
        const auto root = c.vm.ProtectReferences(std::array{ref});
        return CallAndroidMethod(c.vm, c.receiver, "setTypeface", "(Landroid/graphics/Typeface;)V",
                                 {dx::VmValue::Ref(ref)});
    });
    builder.VirtualMethod("getTypeface", "()Landroid/graphics/Typeface;", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Ref(context->view_adapter_references.at(c.receiver.Value()).text_typeface);
    });
    builder.VirtualMethod("setTextColor", "(I)V", [](dx::IntrinsicContext& c) {
        const auto list = TextStatic(c.vm, "Landroid/content/res/ColorStateList;", "valueOf",
                                     "(I)Landroid/content/res/ColorStateList;", {c.arguments[0]})
                              .ref;
        const auto root = c.vm.ProtectReferences(std::array{list});
        return CallAndroidMethod(c.vm, c.receiver, "setTextColor", "(Landroid/content/res/ColorStateList;)V",
                                 {dx::VmValue::Ref(list)});
    });
    builder.VirtualMethod("setTextColor", "(Landroid/content/res/ColorStateList;)V",
                          [text_colors, update_color](dx::IntrinsicContext& c) {
                              const auto ref = c.arguments[0].ref;
                              if (!ref.IsValid())
                                  throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "text colors"};
                              dx::IntrinsicCall(c).SetRef(text_colors, ref);
                              update_color(c);
                              return CallAndroidMethod(c.vm, c.receiver, "invalidate", "()V");
                          });
    builder.FinalMethod("getCurrentTextColor", "()I", [context](dx::IntrinsicContext& c) {
        const auto rgba = context->ui_tree.Get(TextNode(c, context))->text_color;
        return dx::VmValue::Int(static_cast<std::int32_t>((rgba >> 8U) | (rgba << 24U)));
    });
    builder.FinalMethod(
        "getTextColors", "()Landroid/content/res/ColorStateList;",
        [text_colors](dx::IntrinsicContext& c) { return dx::VmValue::Ref(dx::IntrinsicCall(c).GetRef(text_colors)); });
    builder.VirtualMethod("setGravity", "(I)V", [context, changed](dx::IntrinsicContext& c) {
        auto gravity = c.arguments[0].AsInt();
        if ((gravity & ~0x77) ||
            ((gravity & 7) != 0 && (gravity & 7) != 1 && (gravity & 7) != 3 && (gravity & 7) != 5) ||
            ((gravity & 0x70) != 0 && (gravity & 0x70) != 0x10 && (gravity & 0x70) != 0x30 && (gravity & 0x70) != 0x50))
            TextInputFailure(c.vm, "unsupported gravity");
        if (!(gravity & 7)) gravity |= 3;
        if (!(gravity & 0x70)) gravity |= 0x30;
        context->ui_tree.Get(TextNode(c, context))->gravity = static_cast<std::uint32_t>(gravity);
        return changed(c);
    });
    builder.VirtualMethod("getGravity", "()I", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(static_cast<std::int32_t>(context->ui_tree.Get(TextNode(c, context))->gravity));
    });
    builder.VirtualMethod("setMaxLines", "(I)V", [context, changed](dx::IntrinsicContext& c) {
        if (c.arguments[0].AsInt() < 1) TextInputFailure(c.vm, "non-positive maxLines");
        context->ui_tree.Get(TextNode(c, context))->max_lines = c.arguments[0].AsInt();
        return changed(c);
    });
    builder.VirtualMethod("getMaxLines", "()I", [context](dx::IntrinsicContext& c) {
        return dx::VmValue::Int(context->ui_tree.Get(TextNode(c, context))->max_lines);
    });
    builder.VirtualMethod("setSingleLine", "()V", [](dx::IntrinsicContext& c) {
        return CallAndroidMethod(c.vm, c.receiver, "setSingleLine", "(Z)V", {dx::VmValue::Int(1)});
    });
    builder.VirtualMethod("setSingleLine", "(Z)V", [context, changed](dx::IntrinsicContext& c) {
        const auto single = c.arguments[0].AsInt() != 0;
        auto& n = *context->ui_tree.Get(TextNode(c, context));
        context->view_adapter_references.at(c.receiver.Value()).text_single_line = single;
        n.max_lines = single ? 1 : 0x7fffffff;
        return changed(c);
    });
    builder.VirtualMethod("setCompoundDrawablesWithIntrinsicBounds", "(IIII)V", [](dx::IntrinsicContext& c) {
        const auto resources =
            CallAndroidMethod(c.vm, c.receiver, "getResources", "()Landroid/content/res/Resources;").ref;
        std::array<dx::VmObjectRef, 4> refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{}};
        const auto retained = c.vm.Model().NewObjectArray(c.vm.Linker().ResolveDescriptor("[Ljava/lang/Object;"),
                                                          c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), 4);
        const auto roots = c.vm.ProtectReferences(std::array{retained});
        std::vector<dx::VmValue> args;
        for (std::size_t i = 0; i < 4; ++i) {
            if (c.arguments[i].AsInt())
                refs[i] = CallAndroidMethod(c.vm, resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                            {c.arguments[i]})
                              .ref;
            c.vm.Model().SetObjectElement(retained, static_cast<JniSize>(i), refs[i]);
            args.push_back(dx::VmValue::Ref(refs[i]));
        }
        return CallAndroidMethod(
            c.vm, c.receiver, "setCompoundDrawablesWithIntrinsicBounds",
            "(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V",
            args);
    });
    builder.VirtualMethod(
        "setCompoundDrawablesWithIntrinsicBounds",
        "(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V",
        [context, changed](dx::IntrinsicContext& c) {
            std::array<ui::CompoundDrawable, 4> values{};
            std::array<dx::VmObjectRef, 4> refs{dx::VmObjectRef{}, dx::VmObjectRef{}, dx::VmObjectRef{},
                                                dx::VmObjectRef{}};
            for (std::size_t i = 0; i < 4; ++i) {
                const auto ref = c.arguments[i].ref;
                refs[i] = ref;
                if (!ref.IsValid()) continue;
                const auto found = context->ui_drawables.find(ref.Value());
                if (found == context->ui_drawables.end() || !found->second.bitmap_drawable ||
                    !found->second.leaf_pixels)
                    TextInputFailure(c.vm, "compound requires APK bitmap");
                values[i] = {found->second.resource_id,
                             CallAndroidMethod(c.vm, ref, "getIntrinsicWidth", "()I").AsInt(),
                             CallAndroidMethod(c.vm, ref, "getIntrinsicHeight", "()I").AsInt()};
            }
            context->ui_tree.Get(TextNode(c, context))->compound_drawables = values;
            context->view_adapter_references.at(c.receiver.Value()).text_compounds = refs;
            return changed(c);
        });
    for (std::size_t i = 0; i < 4; ++i) {
        builder.VirtualMethod(std::array{"getCompoundPaddingLeft", "getCompoundPaddingTop", "getCompoundPaddingRight",
                                         "getCompoundPaddingBottom"}[i],
                              "()I", [context, i](dx::IntrinsicContext& c) {
                                  const auto pad = ReadAndroidViewPadding(c.vm, *context, c.receiver);
                                  const auto& compound =
                                      context->ui_tree.Get(TextNode(c, context))->compound_drawables[i];
                                  return dx::VmValue::Int(std::array{pad.left, pad.top, pad.right, pad.bottom}[i] +
                                                          (i % 2 ? compound.height : compound.width));
                              });
    }
    builder.VirtualMethod("onMeasure", "(II)V", [context](dx::IntrinsicContext& c) {
        if (c.vm.CurrentContextToken() != 1U || context->ui_java_owner_thread != std::this_thread::get_id())
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Text measure requires owner"};
        const auto revision = context->view_adapter_references.at(c.receiver.Value()).text_revision;
        const auto& n = *context->ui_tree.Get(TextNode(c, context));
        ui::TextLeafInput input;
        input.text = n.text;
        if (context->view_adapter_references.at(c.receiver.Value()).text_single_line)
            std::replace(input.text.begin(), input.text.end(), u'\n', u' ');
        input.size_px = n.text_size_px;
        input.style = n.text_style;
        input.fallback_font = n.fallback_font;
        input.max_lines = n.max_lines;
        input.java_text_view = true;
        input.single_line = context->view_adapter_references.at(c.receiver.Value()).text_single_line;
        for (std::size_t i = 0; i < 4; ++i)
            input.compound[i] = {n.compound_drawables[i].width, n.compound_drawables[i].height};
        std::array<int, 4> pads{};
        for (std::size_t i = 0; i < 4; ++i)
            pads[i] = CallAndroidMethod(c.vm, c.receiver,
                                        std::array{"getCompoundPaddingLeft", "getCompoundPaddingTop",
                                                   "getCompoundPaddingRight", "getCompoundPaddingBottom"}[i],
                                        "()I")
                          .AsInt() -
                      (i % 2 ? input.compound[i].height : input.compound[i].width);
        input.padding = {pads[0], pads[1], pads[2], pads[3]};
        input.minimum = {CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumWidth", "()I").AsInt(),
                         CallAndroidMethod(c.vm, c.receiver, "getSuggestedMinimumHeight", "()I").AsInt()};
        const auto spec = [&](int bits) {
            const auto mode = static_cast<std::uint32_t>(bits) >> 30U;
            if (mode == 3) TextInputFailure(c.vm, "invalid MeasureSpec mode");
            return ui::MeasureSpec{mode == 1   ? ui::MeasureMode::Exactly
                                   : mode == 2 ? ui::MeasureMode::AtMost
                                               : ui::MeasureMode::Unspecified,
                                   bits & 0x3fffffff};
        };
        auto result = ui::MeasureTextLeaf(input, spec(c.arguments[0].AsInt()), spec(c.arguments[1].AsInt()));
        static_cast<void>(CallAndroidMethod(
            c.vm, c.receiver, "setMeasuredDimension", "(II)V",
            {dx::VmValue::Int(result.constrained.width), dx::VmValue::Int(result.constrained.height)}));
        auto& refs = context->view_adapter_references.at(c.receiver.Value());
        if (revision != refs.text_revision)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "Text changed during measurement"};
        refs.text_measure_cache[result.constrained.width] = result;
        refs.text_measure = std::move(result);
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getBaseline", "()I", [context](dx::IntrinsicContext& c) {
        const auto width = CallAndroidMethod(c.vm, c.receiver, "getMeasuredWidth", "()I").AsInt();
        const auto& cache = context->view_adapter_references.at(c.receiver.Value()).text_measure_cache;
        const auto found = cache.find(width);
        if (found == cache.end()) return dx::VmValue::Int(-1);
        const auto result = found->second;
        const auto gravity = context->ui_tree.Get(TextNode(c, context))->gravity;
        auto baseline = *result.baseline;
        const auto height = CallAndroidMethod(c.vm, c.receiver, "getMeasuredHeight", "()I").AsInt();
        const auto top = CallAndroidMethod(c.vm, c.receiver, "getCompoundPaddingTop", "()I").AsInt();
        const auto bottom = CallAndroidMethod(c.vm, c.receiver, "getCompoundPaddingBottom", "()I").AsInt();
        const auto extra = std::max(0, height - top - bottom - result.line_plan->metrics.height);
        if ((gravity & 0x70) == 0x10) baseline += extra / 2;
        if ((gravity & 0x70) == 0x50) baseline += extra;
        return dx::VmValue::Int(baseline);
    });
    builder.VirtualMethod(
        "onCreateDrawableState", "(I)[I",
        [context](dx::IntrinsicContext& c) {
            const auto extra = c.arguments[0].AsInt();
            if (extra < 0 || extra > 1023) TextInputFailure(c.vm, "drawable extraSpace outside bounds");
            const auto state = context->view_adapter_references.find(c.receiver.Value());
            // The View super constructor can ask before the bounded text owner exists.
            const bool single = state != context->view_adapter_references.end() && state->second.text_single_line;
            const auto base = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto index = c.vm.Linker().FindVtableIndex(base, "onCreateDrawableState", "(I)[I");
            const auto out =
                c.vm.Call(c.vm.Linker().Class(base).vtable.at(*index),
                          std::array{dx::VmValue::Ref(c.receiver), dx::VmValue::Int(extra + (single ? 0 : 1))});
            if (out.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(out.exception_class).descriptor, out.exception_message,
                                      out.exception};
            if (!single) {
                const auto klass = c.vm.Linker().ResolveDescriptor("Landroid/widget/TextView;");
                const auto f = c.vm.Linker().FindFieldRecursive(klass, "MULTILINE_STATE_SET", "[I");
                const dx::VmObjectRef multiline{
                    c.vm.Linker().Class(klass).static_storage[c.vm.Linker().Field(*f).slot]};
                const auto method = c.vm.Linker().FindDirectMethod(base, "mergeDrawableStates", "([I[I)[I");
                const auto merged = c.vm.Call(*method, std::array{out.value, dx::VmValue::Ref(multiline)});
                if (merged.exception.IsValid())
                    throw dx::VmJavaThrow{c.vm.Linker().Class(merged.exception_class).descriptor,
                                          merged.exception_message, merged.exception};
            }
            // Text-selectable/Editor construction remains unsupported, so its pressed-state
            // suppression cannot be requested by this plain-text leaf owner.
            return out.value;
        },
        dx::kAccProtected);
    // No Editor/Layout/Drawables object graph is installed. These callbacks
    // execute the genuine View protocol; all text work belongs to the leaf owner.
    for (const auto& [name, sig] :
         std::array<std::pair<const char*, const char*>, 9>{{{"onLayout", "(ZIIII)V"},

                                                             {"onFocusChanged", "(ZILandroid/graphics/Rect;)V"},
                                                             {"onRtlPropertiesChanged", "(I)V"},
                                                             {"drawableStateChanged", "()V"},
                                                             {"setPadding", "(IIII)V"},
                                                             {"setPaddingRelative", "(IIII)V"},
                                                             {"setFrame", "(IIII)Z"},
                                                             {"resetResolvedDrawables", "()V"},
                                                             {"onResolveDrawables", "(I)V"}}}) {
        builder.VirtualMethod(name, sig, [name, sig, update_color](dx::IntrinsicContext& c) {
            if ((std::string_view(name) == "onRtlPropertiesChanged" ||
                 std::string_view(name) == "onResolveDrawables") &&
                c.arguments[0].AsInt() != 0)
                TextInputFailure(c.vm, "RTL text unsupported");
            const auto type = c.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto method = c.vm.Linker().FindVtableIndex(type, name, sig);
            std::vector<dx::VmValue> args(c.arguments.begin(), c.arguments.end());
            args.insert(args.begin(), dx::VmValue::Ref(c.receiver));
            const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable.at(*method), args);
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            if (std::string_view(name) == "drawableStateChanged") update_color(c);
            return result.value;
        });
    }
    return std::move(builder).Build();
}
}  // namespace

Decl Declare_android_widget_TextView(const Context& context) {
    return ExperimentalTextView(context);
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_widget_Toast.cpp ----
// Toast has no on-screen surface here; show() lands in the guest log.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Toast(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/Toast;", "Ljava/lang/Object;");
    builder.StaticMethod("makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
        "Landroid/widget/Toast;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
        });
    builder.VirtualMethod("show", "()V", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
    // AOSP 4.4 ROM line 268.
    builder.StaticMethod(
        "makeText", "(Landroid/content/Context;II)Landroid/widget/Toast;",
        [](dx::IntrinsicContext& c) {
            const auto resources =
                CallAndroidMethod(c.vm, c.arguments[0].ref, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto resource_root = c.vm.ProtectReferences(std::array{resources});
            const auto text =
                CallAndroidMethod(c.vm, resources, "getText", "(I)Ljava/lang/CharSequence;", {c.arguments[1]}).ref;
            const auto text_root = c.vm.ProtectReferences(std::array{text});
            const auto target = c.vm.Linker().FindDirectMethod(
                c.vm.Linker().ResolveDescriptor("Landroid/widget/Toast;"), "makeText",
                "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
            const auto result =
                c.vm.Call(*target, std::vector<dx::VmValue>{c.arguments[0], dx::VmValue::Ref(text), c.arguments[2]});
            if (result.exception.IsValid())
                throw dx::VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                      result.exception};
            return result.value;
        },
        dx::kAccPublic);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
