#include "container_traversal_fixture.h"
using ogplay::test::container_traversal::ContainerVm;

namespace {
// API19 RelativeLayout.java:378-1060. All expectations below come from its two
// sorted passes and cached LayoutParams edges, not from the Legacy host solver.
struct RelativeWindow : ContainerVm {
    VmObjectRef owner, root, relative;
    std::vector<DexVmTraceEntry> entered;

    explicit RelativeWindow(InterpreterBackend backend) : ContainerVm(backend) {
        context->target_sdk_version = 19;
        context->ui_test_accessibility_disabled = true;
        owner = New("Landroid/app/Activity;");
        context->activity = owner;
        ogplay::test::AttachViewTestContext(vm, owner);
        root = On(owner, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        auto info = On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        Field(info, "flags").bits |= 0x400000;
        relative = New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(relative)});
        Capture();
    }

    Slot& Field(VmObjectRef obj, const char* name, const char* type = "I") {
        auto field = linker.FindFieldRecursive(model.ObjectClass(obj), name, type);
        REQUIRE(field);
        return model.InstanceSlots(obj)[linker.Field(*field).slot];
    }

    void Capture() {
        for (const auto& e : vm.Trace("", 10000))
            if (entered.empty() || e.sequence > entered.back().sequence) entered.push_back(e);
    }

    VmObjectRef Params(int w, int h) {
        return New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V", {VmValue::Int(w), VmValue::Int(h)});
    }

    VmObjectRef Leaf(int id) {
        auto v = New("Ltest/BaselineLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        On(v, "setId", "(I)V", {VmValue::Int(id)});
        return v;
    }

    void Add(VmObjectRef v, VmObjectRef lp) {
        On(relative, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
           {VmValue::Ref(v), VmValue::Ref(lp)});
        Capture();
    }

    void Rule(VmObjectRef lp, int verb, int anchor = -1) {
        On(lp, "addRule", "(II)V", {VmValue::Int(verb), VmValue::Int(anchor)});
    }

    void Remove(VmObjectRef lp, int verb) { On(lp, "removeRule", "(I)V", {VmValue::Int(verb)}); }

    void Request() { On(relative, "requestLayout", "()V"); }

    void Tick() {
        const auto before = context->ui_tree.PublishedProjection();
        REQUIRE(EnsureAndroidUiTraversal(vm, *context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140}));
        const auto after = context->ui_tree.PublishedProjection();
        REQUIRE(after);
        if (before) CHECK(after->ProjectionEpoch() == before->ProjectionEpoch() + 1);
        Capture();
    }

    void Bounds(VmObjectRef v, ui::Rect expected) {
        const auto id = FindViewUiNode(*context, v.Value());
        REQUIRE(id);
        CHECK(context->ui_tree.PublishedProjection()->Get(*id)->frame == expected);
        CHECK(On(v, "getLeft", "()I").AsInt() == expected.left);
        CHECK(On(v, "getTop", "()I").AsInt() == expected.top);
        CHECK(On(v, "getRight", "()I").AsInt() == expected.right);
        CHECK(On(v, "getBottom", "()I").AsInt() == expected.bottom);
    }

    int RuleValue(VmObjectRef lp, int verb, int direction) {
        auto array = On(lp, "getRules", "(I)[I", {VmValue::Int(direction)}).ref;
        return static_cast<int>(static_cast<std::uint32_t>(model.GetPrimitiveElement(array, verb)));
    }

    void Finish(InterpreterBackend backend) {
        Capture();
        CHECK(ledger.Unimplemented().empty());
        REQUIRE_FALSE(entered.empty());
        CHECK(entered.front().sequence == 1);
        for (std::size_t i = 1; i < entered.size(); ++i)
            CHECK(entered[i].sequence == entered[i - 1].sequence + 1);
        std::ofstream trace(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-09/relative-" +
                            (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        trace << RenderDexVmTraceJson(entered);
    }
};
}  // namespace

TEST_CASE("DVM-285 ROM RelativeLayout dependencies constraints baseline RTL and projection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        RelativeWindow f(backend);
        CHECK(f.Field(f.relative, "mGravity").bits == 0x800033);
        CHECK(f.Field(f.relative, "mAllowBrokenMeasureSpecs", "Z").bits == 0);
        CHECK(f.Field(f.relative, "mMeasureVerticalWithPaddingMargin", "Z").bits == 1);
        CHECK(f.Field(f.relative, "mGraph", "Landroid/widget/RelativeLayout$DependencyGraph;").bits != 0);
        auto a = f.Leaf(101), b = f.Leaf(102), c = f.Leaf(103);
        auto ap = f.Params(40, 20), bp = f.Params(30, 15), cp = f.Params(10, 10);
        f.Field(ap, "leftMargin").bits = 10;
        f.Field(ap, "topMargin").bits = 8;
        f.Field(ap, "rightMargin").bits = 3;
        f.Field(ap, "bottomMargin").bits = 4;
        f.Field(bp, "leftMargin").bits = 2;
        f.Field(bp, "topMargin").bits = 5;
        f.Rule(bp, 1, 101);  // RIGHT_OF
        f.Rule(bp, 3, 101);  // BELOW
        f.Rule(cp, 5, 102);  // ALIGN_LEFT
        f.Rule(cp, 8, 102);  // ALIGN_BOTTOM
        // Deliberately reverse insertion order: both graph sorts must run.
        f.Add(c, cp);
        f.Add(b, bp);
        f.Add(a, ap);
        f.Tick();
        f.Bounds(f.root, {0, 0, 200, 140});
        f.Bounds(f.relative, {0, 0, 200, 140});
        f.Bounds(a, {10, 8, 50, 28});
        f.Bounds(b, {55, 37, 85, 52});
        f.Bounds(c, {55, 42, 65, 52});
        f.Remove(bp, 1);
        f.Remove(bp, 3);
        f.Request();
        f.Tick();
        f.Bounds(b, {2, 5, 32, 20});
        f.Bounds(c, {2, 10, 12, 20});  // dependency-chain negative control
        f.Rule(bp, 1, 101);
        f.Rule(bp, 3, 101);
        f.Request();
        f.Tick();
        f.Bounds(b, {55, 37, 85, 52});
        // Both edges fixed: exact available size overrides requested10x10 and CENTER.
        f.On(f.relative, "removeAllViews", "()V");
        auto d = f.Leaf(104), dp = f.Params(10, 10);
        f.Field(dp, "leftMargin").bits = 7;
        f.Field(dp, "rightMargin").bits = 11;
        f.Field(dp, "topMargin").bits = 9;
        f.Field(dp, "bottomMargin").bits = 13;
        for (int verb : {9, 10, 11, 12, 13})
            f.Rule(dp, verb);
        f.Add(d, dp);
        f.Tick();
        f.Bounds(d, {7, 9, 189, 127});
        CHECK(f.On(d, "getMeasuredWidth", "()I").AsInt() == 182);
        CHECK(f.On(d, "getMeasuredHeight", "()I").AsInt() == 118);
        f.Remove(dp, 11);
        f.Remove(dp, 12);
        f.Request();
        f.Tick();
        f.Bounds(d, {7, 9, 17, 19});  // one fixed edge still defeats CENTER
        f.Remove(dp, 9);
        f.Remove(dp, 10);
        f.Request();
        f.Tick();
        f.Bounds(d, {95, 65, 105, 75});  // center ignores asymmetric margins
        f.Remove(dp, 13);
        f.On(dp, "addRule", "(I)V", {VmValue::Int(14)});
        f.Rule(dp, 15);
        f.Request();
        f.Tick();
        f.Bounds(d, {95, 65, 105, 75});
        f.Remove(dp, 14);
        f.Remove(dp, 15);
        f.Request();
        f.Tick();
        f.Bounds(d, {7, 9, 17, 19});
        // Same published epoch supplies pixels and hit testing.
        f.On(d, "setClickable", "(Z)V", {VmValue::Int(1)});
        auto did = *FindViewUiNode(*f.context, d.Value());
        f.On(d, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
        f.Request();
        f.Tick();
        auto snap = f.context->ui_tree.PublishedProjection();
        CHECK(snap->HitTestProjection(7, 9) == did);
        CHECK(snap->HitTestProjection(6, 9) != did);
        auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snap, {}), {200, 140});
        CHECK(pixels.rgba8[(9 * 200 + 7) * 4 + 3] == 255);
        CHECK(pixels.rgba8[(9 * 200 + 6) * 4 + 3] == 0);
        // Baseline pass runs after the two size/position passes, overriding BELOW.
        f.On(f.relative, "removeAllViews", "()V");
        f.Field(a, "baseline").bits = 12;
        f.Field(b, "baseline").bits = 5;
        f.Rule(bp, 4, 101);
        f.Add(b, bp);
        f.Add(a, ap);
        f.Tick();
        f.Bounds(a, {10, 8, 50, 28});
        f.Bounds(b, {55, 15, 85, 30});  // 8+12-5=15, not BELOW's37
        CHECK(f.On(f.relative, "getBaseline", "()I").AsInt() == 12);
        f.Remove(bp, 4);
        f.Request();
        f.Tick();
        f.Bounds(b, {55, 37, 85, 52});
        // Relative rules win over conflicting absolute rules; initialRules survive flips.
        f.On(f.relative, "removeAllViews", "()V");
        auto e = f.Leaf(105), ep = f.Params(20, 10);
        f.Rule(ep, 9);
        f.Rule(ep, 21);  // END -> RIGHT in LTR, LEFT in RTL; clears absolute LEFT
        f.On(ep, "setMarginStart", "(I)V", {VmValue::Int(3)});
        f.On(ep, "setMarginEnd", "(I)V", {VmValue::Int(7)});
        f.Add(e, ep);
        f.Tick();
        f.Bounds(e, {173, 0, 193, 10});
        CHECK(f.RuleValue(ep, 9, 0) == 0);
        CHECK(f.RuleValue(ep, 11, 0) == -1);
        CHECK(f.Field(ep, "leftMargin").bits == 3);
        CHECK(f.Field(ep, "rightMargin").bits == 7);
        f.On(f.relative, "setLayoutDirection", "(I)V", {VmValue::Int(1)});
        f.Request();
        f.Tick();
        f.Bounds(e, {7, 0, 27, 10});
        CHECK(f.RuleValue(ep, 9, 1) == -1);
        CHECK(f.RuleValue(ep, 11, 1) == 0);
        CHECK(f.Field(ep, "leftMargin").bits == 7);
        CHECK(f.Field(ep, "rightMargin").bits == 3);
        f.Remove(ep, 21);
        f.Rule(ep, 20);
        f.Request();
        f.Tick();
        f.Bounds(e, {177, 0, 197, 10});  // START in RTL
        f.On(f.relative, "setLayoutDirection", "(I)V", {VmValue::Int(0)});
        f.Request();
        f.Tick();
        f.Bounds(e, {3, 0, 23, 10});
        // Sibling END_OF/ALIGN_START map on both axes of horizontal direction.
        f.Remove(ep, 9);
        f.Remove(ep, 20);
        f.Rule(ep, 17, 101);
        f.Add(a, ap);
        f.Request();
        f.Tick();
        f.Bounds(e, {56, 0, 76, 10});  // a.right50 + a.rightMargin3 + start3
        f.On(f.relative, "setLayoutDirection", "(I)V", {VmValue::Int(1)});
        f.Request();
        f.Tick();
        // a defaults to right: [157,197]; END_OF -> LEFT_OF, right=157-10-3.
        f.Bounds(a, {157, 8, 197, 28});
        f.Bounds(e, {124, 0, 144, 10});
        f.Remove(ep, 17);
        f.Rule(ep, 18, 101);
        f.Request();
        f.Tick();
        f.Bounds(e, {174, 0, 194, 10});  // ALIGN_START -> ALIGN_RIGHT
        f.On(f.relative, "setLayoutDirection", "(I)V", {VmValue::Int(0)});
        f.Request();
        f.Tick();
        f.Bounds(e, {13, 0, 33, 10});  // ALIGN_START -> ALIGN_LEFT
        // Missing anchor: absent RIGHT/BOTTOM constraints default to left/top;
        // alignWithParent turns those exact same missing constraints into parent edges.
        f.On(f.relative, "removeAllViews", "()V");
        auto mp = f.Params(20, 10), m = f.Leaf(106);
        f.Rule(mp, 7, 999);
        f.Rule(mp, 8, 999);
        f.Add(m, mp);
        f.Tick();
        f.Bounds(m, {0, 0, 20, 10});
        f.Field(mp, "alignWithParent", "Z").bits = 1;
        f.Request();
        f.Tick();
        f.Bounds(m, {180, 130, 200, 140});
        // GONE anchor follows its same-relation chain; INVISIBLE still anchors normally.
        f.On(f.relative, "removeAllViews", "()V");
        f.Remove(bp, 1);
        f.Remove(bp, 3);
        f.Rule(bp, 1, 101);
        f.Rule(bp, 3, 101);
        f.Remove(cp, 5);
        f.Remove(cp, 8);
        f.Rule(cp, 1, 102);
        f.Rule(cp, 3, 102);
        f.Add(c, cp);
        f.Add(b, bp);
        f.Add(a, ap);
        f.On(b, "setVisibility", "(I)V", {VmValue::Int(8)});
        f.Tick();
        f.Bounds(c, {53, 32, 63, 42});  // follows through b to a, using a's margins
        f.On(b, "setVisibility", "(I)V", {VmValue::Int(4)});
        f.Tick();
        f.Bounds(c, {85, 52, 95, 62});
        f.On(b, "setVisibility", "(I)V", {VmValue::Int(8)});
        f.Remove(bp, 1);
        f.Remove(bp, 3);
        f.Request();
        f.Tick();
        f.Bounds(c, {0, 0, 10, 10});  // GONE chain exhausted
        // WRAP_CONTENT takes the resolveSize epilogue, unlike the EXACT window case.
        auto wrap = f.New("Landroid/widget/FrameLayout$LayoutParams;", "(II)V", {VmValue::Int(-2), VmValue::Int(-2)});
        f.On(f.relative, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(wrap)});
        f.Tick();
        f.Bounds(f.relative, {0, 0, 53, 32});  // max child right50+margin3, bottom28+margin4
        f.Bounds(a, {10, 8, 50, 28});
        f.Finish(backend);
    }
}

TEST_CASE("DVM-285 RelativeLayout ROM cycles parameter copies and explicit failure seams") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        RelativeWindow f(backend);
        auto a = f.Leaf(101), b = f.Leaf(102);
        auto ap = f.Params(40, 20), bp = f.Params(30, 15);
        f.Rule(ap, 1, 102);
        f.Rule(bp, 1, 101);
        f.Add(a, ap);
        f.Add(b, bp);
        const auto before = f.context->ui_tree.PublishedProjection();
        auto cycle = f.OnOutcome(f.relative, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x4000008c)});
        REQUIRE(cycle.exception.IsValid());
        CHECK(f.linker.Class(cycle.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(cycle.exception_message.find("Circular dependencies cannot exist") != std::string::npos);
        CHECK(f.context->ui_tree.PublishedProjection() == before);
        f.Remove(bp, 1);
        f.Request();
        f.Tick();
        f.Bounds(b, {0, 0, 30, 15});
        f.Bounds(a, {30, 0, 70, 20});
        // Vertical cycle has the same ROM rejection; removing one edge is its negative.
        f.Remove(ap, 1);
        f.Rule(ap, 3, 102);
        f.Rule(bp, 3, 101);
        f.Request();
        cycle = f.OnOutcome(f.relative, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x4000008c)});
        REQUIRE(cycle.exception.IsValid());
        CHECK(f.linker.Class(cycle.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        f.Remove(bp, 3);
        f.Request();
        f.Tick();
        f.Bounds(a, {0, 15, 40, 35});
        auto lp = f.On(f.relative, "generateDefaultLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(f.Field(lp, "width").bits == 0xfffffffeU);
        CHECK(f.Field(lp, "height").bits == 0xfffffffeU);
        f.Field(ap, "leftMargin").bits = 6;
        f.Rule(ap, 18, 102);
        auto copy = f.New("Landroid/widget/RelativeLayout$LayoutParams;",
                          "(Landroid/widget/RelativeLayout$LayoutParams;)V", {VmValue::Ref(ap)});
        CHECK(copy != ap);
        CHECK(f.Field(copy, "leftMargin").bits == 6);
        CHECK(f.RuleValue(copy, 5, 0) == 102);
        f.Rule(copy, 18, 101);
        CHECK(f.RuleValue(ap, 5, 0) == 102);
        CHECK(f.RuleValue(copy, 5, 0) == 101);
        // Actual legacy-compatibility branch in resolveRules: absolute wins and
        // START stays LEFT even when asked to resolve RTL. No fake Context flags.
        auto compat = f.Params(1, 1);
        f.Field(compat, "mIsRtlCompatibilityMode", "Z").bits = 1;
        f.Rule(compat, 5, 101);
        f.Rule(compat, 18, 102);
        CHECK(f.RuleValue(compat, 5, 1) == 101);
        CHECK(f.RuleValue(compat, 7, 1) == 0);
        f.Remove(compat, 5);
        f.Rule(compat, 18, 102);
        CHECK(f.RuleValue(compat, 5, 1) == 102);
        CHECK(f.ledger.Unimplemented().empty());
        // DVM-325: zero-scrollbar attribute construction succeeds; unsupported
        // XML parameters and system accessibility still fail explicitly.
        const auto ctor = f.linker.FindDirectMethod(f.model.ObjectClass(f.relative), "<init>",
                                                    "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
        REQUIRE(ctor);
        REQUIRE_FALSE(
            f.vm.Call(*ctor, std::array{VmValue::Ref(f.relative), VmValue::Ref(f.owner), VmValue::Ref(VmObjectRef{})})
                .exception.IsValid());
        CHECK((f.Field(f.relative, "mViewFlags").bits & 0x300U) == 0);
        CHECK(f.Field(f.relative, "mScrollCache", "Landroid/view/View$ScrollabilityCache;").bits == 0);
        CHECK(f.ledger.Unimplemented().empty());
        REQUIRE(f.OnOutcome(f.relative, "generateLayoutParams",
                            "(Landroid/util/AttributeSet;)Landroid/widget/RelativeLayout$LayoutParams;",
                            {VmValue::Ref(VmObjectRef{})})
                    .exception.IsValid());
        REQUIRE(f.OnOutcome(f.relative, "dispatchPopulateAccessibilityEvent",
                            "(Landroid/view/accessibility/AccessibilityEvent;)Z", {VmValue::Ref(VmObjectRef{})})
                    .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
