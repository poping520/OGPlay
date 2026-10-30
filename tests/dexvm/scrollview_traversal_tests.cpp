#include "container_traversal_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include <algorithm>
#include "ogplay/session/dex_activity_lifecycle.h"
#include <bit>

using ogplay::test::container_traversal::ContainerVm;

namespace {
Slot& Field(ContainerVm& f, VmObjectRef object, const char* name, const char* type = "I") {
    const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
    REQUIRE(field);
    return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
}

VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->surface_width = 200;
    f.context->surface_height = 150;
    f.context->arsc.package_id = 0x7f;
    ogplay::loader::ArscEntry theme;
    theme.resource_id = 0x7f020001;
    theme.type_name = "style";
    theme.entry_name = "scroll";
    theme.is_complex = true;
    theme.parent = 0x01030005;
    for (auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U})
        theme.bag.push_back({id, 1, 0, {}});
    theme.bag.push_back(
        {0x01010080, 1, 0x7f020002, {}});  // Explicit bounded scrollViewStyle; default overScrollMode is retained.
    auto scroll_style = theme;
    scroll_style.resource_id = 0x7f020002;
    scroll_style.parent = 0;
    scroll_style.bag.clear();
    theme.bag.push_back({0x01010353, 1, 0x7f020002, {}});
    f.context->arsc.entries = {theme, scroll_style};
    f.context->application_theme = theme.resource_id;
    const auto owner = f.New("Landroid/app/Activity;");
    f.context->activity = owner;
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<std::int32_t>(theme.resource_id))});
    return owner;
}

VmObjectRef Scroll(ContainerVm& f, VmObjectRef owner) {
    return f.New("Landroid/widget/ScrollView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
}

VmObjectRef Horizontal(ContainerVm& f, VmObjectRef owner) {
    return f.New("Landroid/widget/HorizontalScrollView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
}

VmObjectRef Leaf(ContainerVm& f, VmObjectRef owner, int w, int h) {
    const auto leaf = f.New("Ltest/ScrollLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
    Field(f, leaf, "desiredWidth").bits = static_cast<std::uint32_t>(w);
    Field(f, leaf, "desiredHeight").bits = static_cast<std::uint32_t>(h);
    return leaf;
}

VmObjectRef Params(ContainerVm& f, int w, int h) {
    return f.New("Landroid/widget/FrameLayout$LayoutParams;", "(II)V", {VmValue::Int(w), VmValue::Int(h)});
}

void Add(ContainerVm& f, VmObjectRef parent, VmObjectRef child, VmObjectRef lp) {
    f.On(parent, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
         {VmValue::Ref(child), VmValue::Ref(lp)});
}

int Get(ContainerVm& f, VmObjectRef view, const char* name) {
    return f.On(view, name, "()I").AsInt();
}
}  // namespace

TEST_CASE("DVM-300 ScrollView ROM empty single-child and second-child overload rejection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (int overload = 0; overload < 4; ++overload) {
            const auto scroll = Scroll(f, owner);
            const auto keep = f.vm.ProtectReferences(std::array{scroll});
            f.On(scroll, "measure", "(II)V", {VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
            f.On(scroll, "layout", "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(100), VmValue::Int(80)});
            CHECK(Get(f, scroll, "getChildCount") == 0);
            const auto first = Leaf(f, owner, 30, 40), second = Leaf(f, owner, 40, 50);
            const auto lp = Params(f, -2, -2);
            const auto args = [&](VmObjectRef child) {
                std::vector<VmValue> values{VmValue::Ref(child)};
                if (overload == 1 || overload == 3) values.push_back(VmValue::Int(0));
                if (overload >= 2) values.push_back(VmValue::Ref(lp));
                return values;
            };
            const char* sigs[] = {"(Landroid/view/View;)V", "(Landroid/view/View;I)V",
                                  "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                  "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V"};
            f.On(scroll, "addView", sigs[overload], args(first));
            const auto rejected = f.OnOutcome(scroll, "addView", sigs[overload], args(second));
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            CHECK(rejected.exception_message == "ScrollView can host only one direct child");
            CHECK(Get(f, scroll, "getChildCount") == 1);
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-300 ScrollView ROM measure matrix preserves API19 fill margin behavior") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (auto width_mode : {0U, 0x80000000U, 0x40000000U})
            for (auto height_mode : {0U, 0x80000000U, 0x40000000U})
                for (bool fill : {false, true})
                    for (int content : {40, 300}) {
                        const auto scroll = Scroll(f, owner), child = Leaf(f, owner, 120, content);
                        const auto keep = f.vm.ProtectReferences(std::array{scroll, child});
                        const auto lp = Params(f, -2, -2);
                        f.On(lp, "setMargins", "(IIII)V",
                             {VmValue::Int(3), VmValue::Int(5), VmValue::Int(7), VmValue::Int(9)});
                        f.On(scroll, "setPadding", "(IIII)V",
                             {VmValue::Int(10), VmValue::Int(8), VmValue::Int(14), VmValue::Int(12)});
                        Add(f, scroll, child, lp);
                        f.On(scroll, "setFillViewport", "(Z)V", {VmValue::Int(fill)});
                        const auto node = *FindViewUiNode(*f.context, child.Value());
                        const auto before = f.context->ui_tree.Get(node)->frame;
                        f.On(scroll, "measure", "(II)V",
                             {VmValue::Int(std::bit_cast<int>(width_mode | 200U)),
                              VmValue::Int(std::bit_cast<int>(height_mode | 150U))});
                        const int measured_h = height_mode == 0x40000000U   ? 150
                                               : height_mode == 0x80000000U ? std::min(150, content + 34)
                                                                            : content + 34;
                        CHECK(Get(f, scroll, "getMeasuredHeight") == measured_h);
                        const bool remeasure = fill && height_mode != 0 && content < measured_h;
                        CHECK(Get(f, child, "getMeasuredHeight") == (remeasure ? measured_h - 20 : content));
                        CHECK(Field(f, child, "measures").bits == (remeasure ? 2U : 1U));
                        CHECK((Field(f, child, "heightSpec").bits & 0xc0000000U) == (remeasure ? 0x40000000U : 0));
                        CHECK((Field(f, child, "heightSpec").bits & 0x3fffffffU) ==
                              (remeasure ? static_cast<unsigned>(measured_h - 20) : 14U));
                        CHECK((Field(f, child, "widthSpec").bits & 0xc0000000U) == (width_mode == 0 ? 0 : 0x80000000U));
                        CHECK((Field(f, child, "widthSpec").bits & 0x3fffffffU) == (width_mode == 0 ? 0U
                                                                                    : remeasure     ? 176U
                                                                                                    : 166U));
                        CHECK(f.context->ui_tree.Get(node)->frame == before);
                        const auto w = Get(f, scroll, "getMeasuredWidth");
                        f.On(scroll, "layout", "(IIII)V",
                             {VmValue::Int(0), VmValue::Int(0), VmValue::Int(w), VmValue::Int(measured_h)});
                        CHECK(Get(f, child, "getLeft") == 13);
                        CHECK(Get(f, child, "getTop") == 13);
                        CHECK(Field(f, child, "layouts").bits == 1);
                        CHECK(Field(f, scroll, "mIsLayoutDirty", "Z").bits == 0);
                    }
        const auto bare_scroll = Scroll(f, owner), bare_child = Leaf(f, owner, 120, 300);
        Add(f, bare_scroll, bare_child, Params(f, -2, -2));
        f.On(bare_scroll, "measureChild", "(Landroid/view/View;II)V",
             {VmValue::Ref(bare_child), VmValue::Int(0x400000c8), VmValue::Int(0x40000096)});
        CHECK(Field(f, bare_child, "heightSpec").bits == 0);
        CHECK(Get(f, bare_child, "getMeasuredHeight") == 300);
        const auto type = f.model.ObjectClass(bare_scroll);
        for (const auto& [name, sig] : std::array<std::pair<const char*, const char*>, 6>{
                 {{"onMeasure", "(II)V"},
                  {"onLayout", "(ZIIII)V"},
                  {"measureChild", "(Landroid/view/View;II)V"},
                  {"measureChildWithMargins", "(Landroid/view/View;IIII)V"},
                  {"scrollTo", "(II)V"},
                  {"requestLayout", "()V"}}}) {
            const auto index = f.linker.FindVtableIndex(type, name, sig);
            REQUIRE(index);
            const auto& method = f.linker.Method(f.linker.Class(type).vtable[*index]);
            CHECK(method.code.has_value());
            CHECK_FALSE(static_cast<bool>(method.implementation));
        }
        const auto trace = f.vm.Trace("", 10000);
        const auto sequence = [&](const char* owner_name, const char* method_name) {
            std::uint64_t result = 0;
            for (const auto& event : trace)
                if (event.class_descriptor == owner_name && event.method_name == method_name) result = event.sequence;
            return result;
        };
        // Retained final layout has real super-before-child callbacks; the subsequent
        // bare-child measurement is a separate explicit protected ROM entry.
        CHECK(sequence("Landroid/widget/ScrollView;", "onLayout") > 0);
        CHECK(sequence("Landroid/widget/FrameLayout;", "onLayout") >
              sequence("Landroid/widget/ScrollView;", "onLayout"));
        CHECK(sequence("Ltest/ScrollLeaf;", "onLayout") > sequence("Landroid/widget/FrameLayout;", "onLayout"));
        std::ofstream out(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-10b2/measure-" +
                          (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        out << RenderDexVmTraceJson(trace);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-300 Java scroll clamp projection raster hit and CANCEL keep child local geometry") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        const auto scroll = Scroll(f, owner), child = Leaf(f, owner, 120, 300);
        Add(f, scroll, child, Params(f, 120, 300));
        f.On(scroll, "setPadding", "(IIII)V", {VmValue::Int(10), VmValue::Int(8), VmValue::Int(14), VmValue::Int(12)});
        f.On(child, "setClickable", "(Z)V", {VmValue::Int(1)});
        const auto listener = f.New("Ltest/StateListener;");
        f.On(child, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V", {VmValue::Ref(listener)});
        f.On(child, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", {VmValue::Ref(listener)});
        f.On(child, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(scroll)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
        const auto id = *FindViewUiNode(*f.context, child.Value());
        const auto bounds = f.context->ui_tree.Get(id)->frame;
        const auto measures = Field(f, child, "measures").bits, layouts = Field(f, child, "layouts").bits;
        const auto initial = f.context->ui_tree.PublishedProjection();
        CHECK(Get(f, scroll, "getScrollY") == 0);
        auto down = DispatchDeepTouchEvent(f.vm, *f.context, 0, 20, 20, f.context->ui_touch_capture);
        REQUIRE_MESSAGE(!down.error, down.error.value_or(""));
        CHECK(down.handled);
        CHECK_FALSE(ogplay::session::ShouldInterceptScrollGesture(170, 20, 15, 1));
        CHECK(ogplay::session::ShouldInterceptScrollGesture(170, 20, 0, 1));
        auto cancel = DispatchDeepTouchEvent(f.vm, *f.context, 3, 20, 0, f.context->ui_touch_capture);
        CHECK_FALSE(cancel.error.has_value());
        {
            const AndroidInputEvent event(f.vm, *f.context);
            const auto lease = f.context->input_presentation;
            REQUIRE(lease);
            ScrollAndroidViewBy(f.vm, *f.context, scroll, 60);
            CHECK(f.context->input_presentation == lease);
            CHECK(f.context->ui_tree.PublishedProjection() == initial);
            CHECK(AcquireAndroidPresentation(*f.context) == lease);
            CHECK(ViewContainsPoint(f.vm, *f.context, child.Value(), 20, 20));
        }
        CHECK_FALSE(f.context->input_presentation);
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
        const auto snap = f.context->ui_tree.PublishedProjection();
        CHECK(Get(f, scroll, "getScrollY") == 60);
        CHECK(snap->Get(id)->screen_frame.top == bounds.top - 60);
        CHECK(snap->ProjectionEpoch() > initial->ProjectionEpoch());
        CHECK(snap->Get(id)->frame == bounds);
        CHECK(Field(f, child, "measures").bits == measures);
        CHECK(Field(f, child, "layouts").bits == layouts);
        CHECK(initial->Get(id)->screen_frame.top == bounds.top);
        CHECK(snap->HitTestProjection(20, 8) == id);
        CHECK(snap->HitTestProjection(20, 7) != id);
        CHECK(snap->HitTestProjection(20, 137) == id);
        CHECK(snap->HitTestProjection(20, 138) != id);
        auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snap, {}), {200, 150});
        for (int y : {7, 8, 137, 138})
            CHECK((pixels.rgba8[static_cast<std::size_t>((y * 200 + 20) * 4 + 3)] != 0) == (y >= 8 && y < 138));
        auto up = DispatchDeepTouchEvent(f.vm, *f.context, 1, 20, 20, f.context->ui_touch_capture);
        CHECK_FALSE(up.error.has_value());
        CHECK(Field(f, listener, "clicks").bits == 0);
        CHECK(Field(f, listener, "touches").bits == 2);  // Exactly DOWN and one CANCEL, no UP delivery.
        CHECK_FALSE(f.context->ui_bounded_scroll_dispatch);
        ScrollAndroidViewBy(f.vm, *f.context, scroll, 10000);
        CHECK(Get(f, scroll, "getScrollY") == 170);
        ScrollAndroidViewBy(f.vm, *f.context, scroll, -10000);
        CHECK(Get(f, scroll, "getScrollY") == 0);
        CHECK(Field(f, child, "measures").bits == measures);
        CHECK(Field(f, child, "layouts").bits == layouts);
        // A plain nonclickable child must still let its ancestor retain the
        // host gesture; bounded scrolling does not require a child listener.
        f.On(child, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", {VmValue::Ref(VmObjectRef{})});
        f.On(child, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V", {VmValue::Ref(VmObjectRef{})});
        f.On(child, "setClickable", "(Z)V", {VmValue::Int(0)});
        const auto plain_down = DispatchDeepTouchEvent(f.vm, *f.context, 0, 20, 20, 0);
        REQUIRE_MESSAGE(!plain_down.error, plain_down.error.value_or(""));
        CHECK(plain_down.handled);
        CHECK(plain_down.captured_view == scroll.Value());
        const auto plain_cancel = DispatchDeepTouchEvent(f.vm, *f.context, 3, 20, 0, plain_down.captured_view);
        CHECK_FALSE(plain_cancel.error);
        CHECK(f.context->ui_touch_capture == 0);
        CHECK(f.ledger.Unimplemented().empty());
        std::ofstream out(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-10b2/scroll-" +
                          (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        out << RenderDexVmTraceJson(f.vm.Trace("", 10000));
    }
}

TEST_CASE("DVM-300 default EdgeEffect stays idle and activation explicitly fails") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto scroll = Scroll(f, owner);
        const auto keep = f.vm.ProtectReferences(std::array{owner, scroll});
        CHECK(Get(f, scroll, "getOverScrollMode") != 2);
        const auto edge = VmObjectRef{Field(f, scroll, "mEdgeGlowTop", "Landroid/widget/EdgeEffect;").bits};
        REQUIRE(edge.IsValid());
        CHECK(f.On(edge, "isFinished", "()Z").AsInt() == 1);
        f.On(edge, "setSize", "(II)V", {VmValue::Int(200), VmValue::Int(150)});
        CHECK(Field(f, edge, "mWidth").bits == 200);
        CHECK(Field(f, edge, "mHeight").bits == 150);
        f.On(edge, "finish", "()V");
        f.On(edge, "onRelease", "()V");
        CHECK(Field(f, edge, "mState").bits == 0);
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto name : {"onPull", "draw", "onAbsorb"}) {
            const auto sig = std::string_view(name) == "onPull" ? "(F)V"
                             : std::string_view(name) == "draw" ? "(Landroid/graphics/Canvas;)Z"
                                                                : "(I)V";
            const auto arg = std::string_view(name) == "onPull" ? VmValue::Float(.5F)
                             : std::string_view(name) == "draw" ? VmValue::Ref(VmObjectRef{})
                                                                : VmValue::Int(1);
            const auto before = f.ledger.Unimplemented().size();
            const auto result = f.OnOutcome(edge, name, sig, {arg});
            CHECK(result.exception.IsValid());
            CHECK(f.ledger.Unimplemented().size() > before);
            CHECK(Field(f, edge, "mState").bits == 0);
        }
        static_cast<void>(f.vm.CollectGarbage("scroll-idle-edge"));
        CHECK(f.model.IsValidRef(edge));
    }
}

TEST_CASE("DVM-301 HorizontalScrollView ROM empty single-child and second-child overload rejection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (int overload = 0; overload < 4; ++overload) {
            const auto scroll = Horizontal(f, owner);
            const auto keep = f.vm.ProtectReferences(std::array{scroll});
            f.On(scroll, "measure", "(II)V", {VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
            f.On(scroll, "layout", "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(100), VmValue::Int(80)});
            CHECK(Get(f, scroll, "getChildCount") == 0);
            const auto first = Leaf(f, owner, 30, 40), second = Leaf(f, owner, 40, 50);
            const auto lp = Params(f, -2, -2);
            const auto args = [&](VmObjectRef child) {
                std::vector<VmValue> values{VmValue::Ref(child)};
                if (overload == 1 || overload == 3) values.push_back(VmValue::Int(0));
                if (overload >= 2) values.push_back(VmValue::Ref(lp));
                return values;
            };
            const char* sigs[] = {"(Landroid/view/View;)V", "(Landroid/view/View;I)V",
                                  "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                                  "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V"};
            f.On(scroll, "addView", sigs[overload], args(first));
            const auto rejected = f.OnOutcome(scroll, "addView", sigs[overload], args(second));
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            CHECK(rejected.exception_message == "HorizontalScrollView can host only one direct child");
            CHECK(Get(f, scroll, "getChildCount") == 1);
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-301 HorizontalScrollView ROM measure matrix preserves API19 fill margin behavior") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (auto width_mode : {0U, 0x80000000U, 0x40000000U})
            for (auto height_mode : {0U, 0x80000000U, 0x40000000U})
                for (bool fill : {false, true})
                    for (int content : {40, 300}) {
                        const auto scroll = Horizontal(f, owner), child = Leaf(f, owner, content, 120);
                        const auto keep = f.vm.ProtectReferences(std::array{scroll, child});
                        const auto lp = Params(f, -2, -2);
                        f.On(lp, "setMargins", "(IIII)V",
                             {VmValue::Int(3), VmValue::Int(5), VmValue::Int(7), VmValue::Int(9)});
                        f.On(scroll, "setPadding", "(IIII)V",
                             {VmValue::Int(10), VmValue::Int(8), VmValue::Int(14), VmValue::Int(12)});
                        Add(f, scroll, child, lp);
                        f.On(scroll, "setFillViewport", "(Z)V", {VmValue::Int(fill)});
                        f.On(scroll, "measure", "(II)V",
                             {VmValue::Int(std::bit_cast<int>(width_mode | 200U)),
                              VmValue::Int(std::bit_cast<int>(height_mode | 150U))});
                        const int measured_w = width_mode == 0x40000000U   ? 200
                                               : width_mode == 0x80000000U ? std::min(200, content + 34)
                                                                           : content + 34;
                        CHECK(Get(f, scroll, "getMeasuredWidth") == measured_w);
                        const bool remeasure = fill && width_mode != 0 && content < measured_w;
                        CHECK(Get(f, child, "getMeasuredWidth") == (remeasure ? measured_w - 24 : content));
                        CHECK(Field(f, child, "measures").bits == (remeasure ? 2U : 1U));
                        CHECK((Field(f, child, "widthSpec").bits & 0xc0000000U) == (remeasure ? 0x40000000U : 0));
                        CHECK((Field(f, child, "widthSpec").bits & 0x3fffffffU) ==
                              (remeasure ? static_cast<unsigned>(measured_w - 24) : 10U));
                        CHECK((Field(f, child, "heightSpec").bits & 0xc0000000U) ==
                              (height_mode == 0 ? 0 : 0x80000000U));
                        CHECK((Field(f, child, "heightSpec").bits & 0x3fffffffU) == (height_mode == 0 ? 0U
                                                                                     : remeasure      ? 130U
                                                                                                      : 116U));
                        const int h = Get(f, scroll, "getMeasuredHeight");
                        f.On(scroll, "layout", "(IIII)V",
                             {VmValue::Int(0), VmValue::Int(0), VmValue::Int(measured_w), VmValue::Int(h)});
                        CHECK(Get(f, child, "getLeft") == 13);
                        CHECK(Get(f, child, "getTop") == 13);
                        CHECK(Field(f, child, "layouts").bits == 1);
                    }
        const auto scroll = Horizontal(f, owner), child = Leaf(f, owner, 300, 120);
        Add(f, scroll, child, Params(f, -2, -2));
        f.On(scroll, "measureChild", "(Landroid/view/View;II)V",
             {VmValue::Ref(child), VmValue::Int(0x400000c8), VmValue::Int(0x40000096)});
        CHECK(Field(f, child, "widthSpec").bits == 0);
        CHECK(Get(f, child, "getMeasuredWidth") == 300);
        for (const auto& [name, sig] : std::array<std::pair<const char*, const char*>, 6>{
                 {{"onMeasure", "(II)V"},
                  {"onLayout", "(ZIIII)V"},
                  {"measureChild", "(Landroid/view/View;II)V"},
                  {"measureChildWithMargins", "(Landroid/view/View;IIII)V"},
                  {"scrollTo", "(II)V"},
                  {"requestLayout", "()V"}}}) {
            const auto index = f.linker.FindVtableIndex(f.model.ObjectClass(scroll), name, sig);
            REQUIRE(index);
            const auto& method = f.linker.Method(f.linker.Class(f.model.ObjectClass(scroll)).vtable[*index]);
            CHECK(method.code.has_value());
            CHECK_FALSE(static_cast<bool>(method.implementation));
        }
        CHECK(f.ledger.Unimplemented().empty());
        std::ofstream out(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-10b3/measure-" +
                          (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        out << RenderDexVmTraceJson(f.vm.Trace("", 10000));
    }
}

TEST_CASE("DVM-301 Horizontal ROM forceLeftGravity and initial RTL use original layout state") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        const auto info = f.On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        Field(f, info, "flags").bits |= 0x400000;  // Manifest FLAG_SUPPORTS_RTL, as in the FrameLayout fixture.

        for (int direction : {0, 1})
            for (int width : {40, 300}) {
                const auto scroll = Horizontal(f, owner), child = Leaf(f, owner, width, 50);
                const auto keep = f.vm.ProtectReferences(std::array{scroll, child});
                f.On(scroll, "setLayoutDirection", "(I)V", {VmValue::Int(direction)});
                f.On(scroll, "setPadding", "(IIII)V",
                     {VmValue::Int(10), VmValue::Int(8), VmValue::Int(14), VmValue::Int(12)});
                const auto lp = Params(f, -2, -2);
                Field(f, lp, "gravity").bits = 0x35;  // RIGHT|TOP: overflow must force LEFT even for RTL.
                f.On(lp, "setMargins", "(IIII)V", {VmValue::Int(3), VmValue::Int(5), VmValue::Int(7), VmValue::Int(9)});
                Add(f, scroll, child, lp);
                f.On(scroll, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x40000096)});
                // Real Java scrollTo before first layout gives a nonzero position.
                f.On(scroll, "scrollTo", "(II)V", {VmValue::Int(17), VmValue::Int(0)});
                const auto before = Get(f, scroll, "getScrollX");
                CHECK(before == 17);
                f.On(scroll, "layout", "(IIII)V",
                     {VmValue::Int(0), VmValue::Int(0), VmValue::Int(200), VmValue::Int(150)});
                CHECK(Get(f, child, "getLeft") == (width == 300 ? 13 : 139));
                CHECK(Get(f, child, "getTop") == 13);
                const int expected = width == 300 ? (direction == 1 ? 124 - before : before) : 0;
                CHECK(Get(f, scroll, "getScrollX") == expected);
                f.On(scroll, "layout", "(IIII)V",
                     {VmValue::Int(0), VmValue::Int(0), VmValue::Int(200), VmValue::Int(150)});
                CHECK(Get(f, scroll, "getScrollX") == expected);  // No second RTL reversal.
                CHECK(Field(f, scroll, "mIsLayoutDirty", "Z").bits == 0);
            }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-301 nested horizontal vertical Java scroll publishes one XY raster hit epoch") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (bool horizontal_outer : {false, true}) {
            ContainerVm f(backend);
            const auto owner = Owner(f);
            const auto root = f.vm.ProtectReferences(std::array{owner});
            const auto horizontal = Horizontal(f, owner), vertical = Scroll(f, owner), child = Leaf(f, owner, 400, 400);
            const auto outer = horizontal_outer ? horizontal : vertical,
                       inner = horizontal_outer ? vertical : horizontal;
            Add(f, inner, child, Params(f, 400, 400));
            Add(f, outer, inner, Params(f, horizontal_outer ? 400 : 200, horizontal_outer ? 150 : 400));
            for (auto view : {horizontal, vertical})
                f.On(view, "setPadding", "(IIII)V",
                     {VmValue::Int(4), VmValue::Int(6), VmValue::Int(8), VmValue::Int(10)});
            f.On(child, "setClickable", "(Z)V", {VmValue::Int(1)});
            const auto listener = f.New("Ltest/StateListener;");
            f.On(child, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V", {VmValue::Ref(listener)});
            f.On(child, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", {VmValue::Ref(listener)});
            f.On(child, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
            f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(outer)});
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
            const auto id = *FindViewUiNode(*f.context, child.Value());
            const auto initial = f.context->ui_tree.PublishedProjection();
            const auto bounds = initial->Get(id)->frame;
            const auto screen = initial->Get(id)->screen_frame;
            const auto measures = Field(f, child, "measures").bits, layouts = Field(f, child, "layouts").bits;
            // Same publication collects both Java fields after both guest calls.
            f.On(horizontal, "scrollTo", "(II)V", {VmValue::Int(50), VmValue::Int(0)});
            f.On(vertical, "scrollTo", "(II)V", {VmValue::Int(0), VmValue::Int(70)});
            CommitAndroidMeasuredRootProjection(f.vm, *f.context, AndroidUiTraversalRoot(f.vm, *f.context),
                                                *f.context->ui_tree.ProjectionTarget(), initial->ProjectionEpoch() + 1);
            auto snap = f.context->ui_tree.PublishedProjection();
            CHECK(snap->Get(id)->frame == bounds);
            CHECK(snap->Get(id)->screen_frame.left == screen.left - 50);
            CHECK(snap->Get(id)->screen_frame.top == screen.top - 70);
            CHECK(initial->Get(id)->screen_frame == screen);
            CHECK(snap->Get(*FindViewUiNode(*f.context, horizontal.Value()))->scroll_x == 50);
            CHECK(snap->Get(*FindViewUiNode(*f.context, vertical.Value()))->scroll_y == 70);
            CHECK(Field(f, child, "measures").bits == measures);
            CHECK(Field(f, child, "layouts").bits == layouts);
            auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snap, {}), {200, 150});
            const auto clip = snap->Get(id)->screen_clip;
            REQUIRE(clip.right > clip.left);
            REQUIRE(clip.bottom > clip.top);
            for (int y = 0; y < 150; ++y)
                for (int x = 0; x < 200; ++x) {
                    const bool inside = x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom;
                    CHECK((snap->HitTestProjection(x, y) == id) == inside);
                    CHECK((pixels.rgba8[static_cast<std::size_t>((y * 200 + x) * 4 + 3)] != 0) == inside);
                }
            const auto down = DispatchDeepTouchEvent(f.vm, *f.context, 0, 20, 20, 0);
            REQUIRE_MESSAGE(!down.error, down.error.value_or(""));
            CHECK(down.handled);
            const auto cancel = DispatchDeepTouchEvent(f.vm, *f.context, 3, 0, 20, f.context->ui_touch_capture);
            CHECK_FALSE(cancel.error);
            ScrollAndroidViewBy(f.vm, *f.context, horizontal, 0, 60);
            CHECK(Get(f, horizontal, "getScrollX") == 110);
            CHECK(Get(f, vertical, "getScrollY") == 70);
            CHECK(f.context->ui_tree.PublishedProjection()->ProjectionEpoch() > snap->ProjectionEpoch());
            const auto up = DispatchDeepTouchEvent(f.vm, *f.context, 1, 20, 20, f.context->ui_touch_capture);
            CHECK_FALSE(up.error);
            CHECK(Field(f, listener, "clicks").bits == 0);
            CHECK(Field(f, listener, "touches").bits == 2);
            ScrollAndroidViewBy(f.vm, *f.context, horizontal, 0, 10000);
            // Horizontal UNSPECIFIED measures its vertical child to 400 + 12 padding.
            CHECK(Get(f, horizontal, "getWidth") == 200);
            CHECK(Get(f, horizontal_outer ? vertical : child, "getWidth") == (horizontal_outer ? 412 : 400));
            CHECK(Get(f, horizontal, "getScrollX") == (horizontal_outer ? 224 : 212));
            ScrollAndroidViewBy(f.vm, *f.context, horizontal, 0, -10000);
            CHECK(Get(f, horizontal, "getScrollX") == 0);
            CHECK(Field(f, child, "measures").bits == measures);
            CHECK(Field(f, child, "layouts").bits == layouts);
            CHECK(f.ledger.Unimplemented().empty());
            std::ofstream out(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-10b3/nested-" +
                              (horizontal_outer ? "horizontal-" : "vertical-") +
                              (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
            out << RenderDexVmTraceJson(f.vm.Trace("", 10000));
        }
    }
}
