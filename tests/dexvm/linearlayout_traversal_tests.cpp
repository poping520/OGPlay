#include "container_traversal_fixture.h"
#include <bit>
using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-284 ROM LinearLayout weights baseline RTL cache and one Window projection") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->target_sdk_version = 19;
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
        const auto root = f.On(owner, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        const auto slot = [&](auto object, const char* name, const char* type = "I") -> Slot& {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(field);
            return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
        };
        const auto info = f.On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        slot(info, "flags").bits |= 0x400000;
        const auto linear = [&] {
            return f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        };
        const auto leaf = [&] {
            return f.New("Ltest/BaselineLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        };
        const auto params = [&](int w, int h, float weight = 0) {
            return f.New("Landroid/widget/LinearLayout$LayoutParams;", "(IIF)V",
                         {VmValue::Int(w), VmValue::Int(h), VmValue::Float(weight)});
        };
        const auto add = [&](auto parent, auto child, auto lp) {
            f.On(parent, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
                 {VmValue::Ref(child), VmValue::Ref(lp)});
        };
        const auto outer = linear();
        CHECK(slot(outer, "mBaselineAligned", "Z").bits == 1);
        CHECK(slot(outer, "mBaselineAlignedChildIndex").bits == 0xffffffffU);
        CHECK(slot(outer, "mGravity").bits == 0x800033);
        CHECK(slot(outer, "mWeightSum", "F").bits == 0);  // Context-only constructor, unlike TypedArray default -1.
        CHECK(slot(outer, "mUseLargestChild", "Z").bits == 0);
        CHECK(slot(outer, "mDivider", "Landroid/graphics/drawable/Drawable;").bits == 0);
        f.On(outer, "setOrientation", "(I)V", {VmValue::Int(1)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(outer)});
        const auto horizontal = linear();
        const auto hp = params(-1, 60);
        add(outer, horizontal, hp);
        const auto tail = leaf();
        const auto tp = params(-1, 0, 1);
        add(outer, tail, tp);
        const auto a = leaf(), b = leaf();
        const auto ap = params(0, 20, 1), bp = params(0, 30, 3);
        slot(a, "baseline").bits = 5;
        slot(b, "baseline").bits = 15;
        add(horizontal, a, ap);
        add(horizontal, b, bp);
        f.On(a, "setClickable", "(Z)V", {VmValue::Int(1)});
        const auto aid = *FindViewUiNode(*f.context, a.Value());
        f.context->ui_tree.Get(aid)->background_color = 0x123456ff;
        std::vector<DexVmTraceEntry> entered;
        const auto capture = [&] {
            for (const auto& e : f.vm.Trace("", 10000))
                if (entered.empty() || e.sequence > entered.back().sequence) entered.push_back(e);
        };
        const auto tick = [&] {
            const auto before = f.context->ui_tree.PublishedProjection();
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
            const auto after = f.context->ui_tree.PublishedProjection();
            REQUIRE(after);
            if (before) CHECK(after->ProjectionEpoch() == before->ProjectionEpoch() + 1);
            capture();
        };
        const auto bounds = [&](auto view, ui::Rect expected) {
            const auto id = *FindViewUiNode(*f.context, view.Value());
            CHECK(f.context->ui_tree.PublishedProjection()->Get(id)->frame == expected);
            CHECK(f.On(view, "getLeft", "()I").AsInt() == expected.left);
            CHECK(f.On(view, "getTop", "()I").AsInt() == expected.top);
            CHECK(f.On(view, "getRight", "()I").AsInt() == expected.right);
            CHECK(f.On(view, "getBottom", "()I").AsInt() == expected.bottom);
        };
        const auto request = [&](auto view) { f.On(view, "requestLayout", "()V"); };
        const auto count = [&](auto view) { return f.On(view, "count", "()I").AsInt(); };
        tick();
        // EXACT 200x140. Vertical fixed 60 leaves delta=80; horizontal weights 1:3 split 200 as 50:150.
        bounds(root, {0, 0, 200, 140});
        bounds(outer, {0, 0, 200, 140});
        bounds(horizontal, {0, 0, 200, 60});
        bounds(tail, {0, 60, 200, 140});
        bounds(a, {0, 10, 50, 30});
        bounds(b, {50, 0, 200, 30});
        CHECK(f.On(horizontal, "getMeasuredState", "()I").AsInt() == 0);
        CHECK(f.On(outer, "getMeasuredState", "()I").AsInt() == 0);
        auto snapshot = f.context->ui_tree.PublishedProjection();
        CHECK(snapshot->HitTestProjection(0, 10) == aid);
        CHECK(snapshot->HitTestProjection(0, 9) != aid);
        auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snapshot, {}), {200, 140});
        CHECK(pixels.rgba8[(10 * 200) * 4 + 3] == 255);
        CHECK(pixels.rgba8[(9 * 200) * 4 + 3] == 0);
        // Baseline disabled is the negative control: top padding, with no ascent adjustment.
        f.On(horizontal, "setBaselineAligned", "(Z)V", {VmValue::Int(0)});
        request(horizontal);
        tick();
        bounds(a, {0, 0, 50, 20});
        bounds(b, {50, 0, 200, 30});
        f.On(horizontal, "setBaselineAligned", "(Z)V", {VmValue::Int(1)});
        f.On(horizontal, "setWeightSum", "(F)V", {VmValue::Float(8)});
        request(horizontal);
        tick();
        // Explicit sum 8: (1/8)*200=25, then (3/7)*175=75, leaving 100 pixels unused.
        bounds(a, {0, 10, 25, 30});
        bounds(b, {25, 0, 100, 30});
        f.On(outer, "setWeightSum", "(F)V", {VmValue::Float(2)});
        request(outer);
        tick();
        bounds(tail, {0, 60, 200, 100});  // delta80 *1/2=40, versus80 above.
        f.On(horizontal, "setLayoutDirection", "(I)V", {VmValue::Int(1)});
        request(horizontal);
        tick();
        // START resolves RIGHT; RTL reverses iteration, so b precedes a at x=100.
        bounds(b, {100, 0, 175, 30});
        bounds(a, {175, 10, 200, 30});
        f.On(horizontal, "setGravity", "(I)V", {VmValue::Int(0x800035)});
        tick();
        bounds(b, {0, 0, 75, 30});
        bounds(a, {75, 10, 100, 30});  // END resolves LEFT.
        f.On(horizontal, "setLayoutDirection", "(I)V", {VmValue::Int(0)});
        f.On(horizontal, "setGravity", "(I)V", {VmValue::Int(0x51)});
        tick();
        bounds(a, {50, 40, 75, 60});
        bounds(b, {75, 30, 150, 60});  // center_horizontal + bottom, equal descent15.
        // A clean same-spec measure skips traversal. Forcing the weighted parent alternates
        // UNSPECIFIED baseline probing and EXACT weight specs, requiring one real measure per child.
        auto ac = count(a), bc = count(b);
        f.On(horizontal, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x4000003c)});
        CHECK(count(a) == ac);
        CHECK(count(b) == bc);
        request(horizontal);
        tick();
        CHECK(count(a) == ac + 1);
        CHECK(count(b) == bc + 1);
        ac = count(a);
        request(a);
        tick();
        CHECK(count(a) > ac);
        // Vertical baseline index1 includes first child's totalLength=60.
        f.On(outer, "setBaselineAlignedChildIndex", "(I)V", {VmValue::Int(1)});
        slot(tail, "baseline").bits = 7;
        CHECK(f.On(outer, "getBaseline", "()I").AsInt() == 7);  // setter itself does not request a measure.
        request(outer);
        tick();
        CHECK(f.On(outer, "getBaseline", "()I").AsInt() == 67);
        f.On(outer, "setGravity", "(I)V", {VmValue::Int(0x800053)});
        tick();
        CHECK(f.On(outer, "getBaseline", "()I").AsInt() == 47);  // bottom: 140-totalLength100 +7 (API19 quirk).
        f.On(outer, "setGravity", "(I)V", {VmValue::Int(0x800033)});
        // Vertical minor-axis START/END use the same RTL resolver, with width40 in width200.
        slot(tp, "width").bits = 40;
        f.On(outer, "setLayoutDirection", "(I)V", {VmValue::Int(1)});
        request(tail);
        tick();
        bounds(tail, {160, 60, 200, 100});
        slot(tp, "gravity").bits = 0x800035;
        request(tail);
        tick();
        bounds(tail, {0, 60, 40, 100});
        f.On(outer, "setLayoutDirection", "(I)V", {VmValue::Int(0)});
        // New nested wrap-content container exercises forceUniformWidth / forceUniformHeight with weight.
        f.On(outer, "removeAllViews", "()V");
        const auto wrap = linear();
        const auto wp = params(-2, -2);
        add(outer, wrap, wp);
        const auto c = leaf(), d = leaf();
        const auto cp = params(-2, -1), dp = params(20, 10, 1);
        add(wrap, c, cp);
        add(wrap, d, dp);
        tick();
        // AT_MOST: MATCH_PARENT c does not contribute to alternativeMaxHeight; d sets height10.
        // forceUniformHeight remeasures c EXACT10, discarding its earlier desired90.
        bounds(wrap, {0, 0, 140, 10});
        bounds(c, {0, 0, 120, 10});
        bounds(d, {120, 0, 140, 10});
        CHECK(count(c) == 2);
        CHECK(count(d) == 1);
        // Same child with WRAP_CONTENT is the negative control for cross-axis MATCH_PARENT remeasurement.
        slot(cp, "height").bits = 0xfffffffeU;
        request(c);
        tick();
        bounds(wrap, {0, 0, 140, 90});
        bounds(c, {0, 0, 120, 90});
        slot(cp, "height").bits = 0xffffffffU;
        request(c);
        tick();
        bounds(wrap, {0, 0, 140, 10});
        // Isolated ROM measurement inputs, like foreground-padding in VD-07; no Drawable drawing is claimed.
        slot(wrap, "mDividerWidth").bits = 2;
        slot(wrap, "mDividerHeight").bits = 3;
        f.On(wrap, "setShowDividers", "(I)V", {VmValue::Int(7)});
        tick();
        bounds(wrap, {0, 0, 146, 10});
        bounds(c, {2, 0, 122, 10});
        bounds(d, {124, 0, 144, 10});
        f.On(wrap, "setShowDividers", "(I)V", {VmValue::Int(0)});
        tick();
        bounds(wrap, {0, 0, 140, 10});
        slot(cp, "width").bits = 80;
        request(c);
        tick();
        bounds(wrap, {0, 0, 100, 10});
        f.On(wrap, "setMeasureWithLargestChildEnabled", "(Z)V", {VmValue::Int(1)});
        request(wrap);
        tick();
        // Largest=80, total=2*80=160 fits AT_MOST200: weight child grows20->80 without delta redistribution.
        bounds(wrap, {0, 0, 160, 10});
        bounds(c, {0, 0, 80, 10});
        bounds(d, {80, 0, 160, 10});
        f.On(wrap, "setMeasureWithLargestChildEnabled", "(Z)V", {VmValue::Int(0)});
        request(wrap);
        tick();
        bounds(wrap, {0, 0, 100, 10});
        f.On(wrap, "setOrientation", "(I)V", {VmValue::Int(1)});
        slot(cp, "width").bits = 0xffffffffU;
        slot(cp, "height").bits = 0xfffffffeU;
        slot(dp, "width").bits = 20;
        slot(dp, "height").bits = 10;
        request(c);
        request(d);
        tick();
        bounds(wrap, {0, 0, 20, 100});
        bounds(c, {0, 0, 20, 90});
        bounds(d, {0, 90, 20, 100});
        slot(cp, "width").bits = 0xfffffffeU;
        request(c);
        tick();
        bounds(wrap, {0, 0, 120, 100});
        bounds(c, {0, 0, 120, 90});
        slot(cp, "width").bits = 0xffffffffU;
        request(c);
        tick();
        bounds(wrap, {0, 0, 20, 100});

        f.On(wrap, "setShowDividers", "(I)V", {VmValue::Int(7)});
        tick();
        bounds(wrap, {0, 0, 20, 109});
        bounds(c, {0, 3, 20, 93});
        bounds(d, {0, 96, 20, 106});
        f.On(wrap, "setShowDividers", "(I)V", {VmValue::Int(0)});
        tick();
        // Largest-child changes totalLength under AT_MOST; overflow redistributes delta using original child sizes.
        f.On(wrap, "setMeasureWithLargestChildEnabled", "(Z)V", {VmValue::Int(1)});
        request(wrap);
        tick();
        bounds(wrap, {0, 0, 20, 140});  // 2*90=180 clamped by parent140; delta=-40 shrinks d10 to0.
        bounds(d, {0, 90, 20, 90});
        CHECK(f.On(wrap, "getMeasuredHeightAndState", "()I").AsInt() == 0x0100008c);
        // Width constraint below desired causes leaf and container TOO_SMALL, unlike the state0 case.
        slot(wp, "width").bits = 70;
        slot(wp, "height").bits = 50;
        slot(cp, "width").bits = 0xfffffffeU;
        request(c);
        tick();
        CHECK((f.On(c, "getMeasuredState", "()I").AsInt() & 0x01000100) == 0x01000100);
        CHECK((f.On(wrap, "getMeasuredState", "()I").AsInt() & 0x01000000) != 0);
        // Distinct AT_MOST keys exercise the actual LongSparseLongArray cache, including state bits.
        const auto measure_c = [&](int w, int h) {
            f.On(c, "measure", "(II)V",
                 {VmValue::Int(static_cast<int>(0x80000000U | static_cast<unsigned>(w))),
                  VmValue::Int(static_cast<int>(0x80000000U | static_cast<unsigned>(h)))});
        };
        ac = count(c);
        measure_c(81, 61);
        measure_c(82, 62);
        CHECK(count(c) == ac + 2);
        measure_c(81, 61);
        CHECK(count(c) == ac + 2);
        CHECK(f.On(c, "getMeasuredWidthAndState", "()I").AsInt() == 0x01000051);
        CHECK(f.On(c, "getMeasuredHeightAndState", "()I").AsInt() == 0x0100003d);
        request(c);
        measure_c(81, 61);
        CHECK(count(c) == ac + 3);
        tick();
        capture();
        CHECK(f.ledger.Unimplemented().empty());
        REQUIRE_FALSE(entered.empty());
        CHECK(entered.front().sequence == 1);
        for (std::size_t i = 1; i < entered.size(); ++i)
            CHECK(entered[i].sequence == entered[i - 1].sequence + 1);
        std::ofstream trace(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-08/linear-" +
                            (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        trace << RenderDexVmTraceJson(entered);
    }
}

TEST_CASE("DVM-284 LinearLayout default copy parameters and explicit unsupported boundaries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto ll = f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto slot = [&](auto obj, const char* name, const char* type = "I") -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(obj), name, type);
            REQUIRE(id);
            return f.model.InstanceSlots(obj)[f.linker.Field(*id).slot];
        };
        auto lp = f.On(ll, "generateDefaultLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(slot(lp, "width").bits == 0xfffffffeU);
        CHECK(slot(lp, "height").bits == 0xfffffffeU);
        CHECK(slot(lp, "gravity").bits == 0xffffffffU);
        CHECK(slot(lp, "weight", "F").bits == 0);
        f.On(ll, "setOrientation", "(I)V", {VmValue::Int(1)});
        lp = f.On(ll, "generateDefaultLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(slot(lp, "width").bits == 0xffffffffU);
        slot(lp, "gravity").bits = 0x55;
        slot(lp, "weight", "F").bits = std::bit_cast<uint32_t>(2.5F);
        slot(lp, "leftMargin").bits = 7;
        const auto copy = f.New("Landroid/widget/LinearLayout$LayoutParams;",
                                "(Landroid/widget/LinearLayout$LayoutParams;)V", {VmValue::Ref(lp)});
        CHECK(copy != lp);
        CHECK(slot(copy, "gravity").bits == 0x55);
        CHECK(slot(copy, "leftMargin").bits == 7);
        CHECK(slot(copy, "weight", "F").bits == std::bit_cast<uint32_t>(2.5F));
        const auto generic =
            f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V", {VmValue::Int(9), VmValue::Int(6)});
        CHECK_FALSE(
            f.On(ll, "checkLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)Z", {VmValue::Ref(generic)}).AsInt());
        const auto converted = f.On(ll, "generateLayoutParams",
                                    "(Landroid/view/ViewGroup$LayoutParams;)Landroid/view/ViewGroup$LayoutParams;",
                                    {VmValue::Ref(generic)})
                                   .ref;
        CHECK(f.On(ll, "checkLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)Z", {VmValue::Ref(converted)})
                  .AsInt());
        CHECK(slot(converted, "width").bits == 9);
        CHECK(slot(converted, "height").bits == 6);
        CHECK(f.On(ll, "getBaseline", "()I").AsInt() == -1);
        CHECK(f.ledger.Unimplemented().empty());
        const auto bad_index = f.OnOutcome(ll, "setBaselineAlignedChildIndex", "(I)V", {VmValue::Int(0)});
        REQUIRE(bad_index.exception.IsValid());
        CHECK(f.linker.Class(bad_index.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        const auto child = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(ll, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        f.On(ll, "setBaselineAlignedChildIndex", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(ll, "getBaseline", "()I").AsInt() == -1);  // index0 no-baseline special case.
        const auto other = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(ll, "addView", "(Landroid/view/View;)V", {VmValue::Ref(other)});
        f.On(ll, "setBaselineAlignedChildIndex", "(I)V", {VmValue::Int(1)});
        REQUIRE(f.OnOutcome(ll, "getBaseline", "()I").exception.IsValid());
        auto child_lp = f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        slot(child_lp, "weight", "F").bits = std::bit_cast<uint32_t>(1.F);
        REQUIRE(f.OnOutcome(ll, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x4000008c)})
                    .exception.IsValid());
        CHECK(f.ledger.Unimplemented().empty());  // ROM rejects weighted predecessor, rather than an unsupported hit.
        REQUIRE(f.OnOutcome(ll, "onDraw", "(Landroid/graphics/Canvas;)V", {VmValue::Ref(VmObjectRef{})})
                    .exception.IsValid());
        CHECK_FALSE(f.OnOutcome(ll, "setDividerDrawable", "(Landroid/graphics/drawable/Drawable;)V",
                                {VmValue::Ref(VmObjectRef{})})
                        .exception.IsValid());
        CHECK(slot(ll, "mDivider", "Landroid/graphics/drawable/Drawable;").bits == 0);
        const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(ll), "<init>",
                                                    "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
        REQUIRE(ctor);
        REQUIRE(f.vm.Call(*ctor, std::array{VmValue::Ref(ll), VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})})
                    .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
