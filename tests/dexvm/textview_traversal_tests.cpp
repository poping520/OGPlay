#include "container_traversal_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include <algorithm>

using ogplay::test::container_traversal::ContainerVm;

namespace {
VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->arsc.package_id = 0x7f;
    ogplay::loader::ArscEntry theme;
    theme.resource_id = 0x7f020001;
    theme.type_name = "style";
    theme.is_complex = true;
    theme.parent = 0x01030005;
    theme.bag = {{0x01010084, 1, 0x7f020002, {}}};
    auto style = theme;
    style.resource_id = 0x7f020002;
    style.parent = 0;
    style.bag.clear();
    for (auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x01010099U, 0x0101009aU, 0x0101009bU, 0x010102c5U,
                    0x010102c6U, 0x010102c7U, 0x01010314U, 0x01010315U, 0x0101035eU, 0x0101035fU, 0x01010374U}) {
        theme.bag.push_back({id, 1, 0, {}});
        style.bag.push_back({id, 1, 0, {}});
    }
    f.context->arsc.entries = {theme, style};
    f.context->application_theme = theme.resource_id;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<int>(theme.resource_id))});
    return owner;
}

VmObjectRef Text(ContainerVm& f, VmObjectRef owner, const char* type = "Landroid/widget/TextView;") {
    const auto text = f.New(type, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
    f.On(text, "setTextSize", "(IF)V", {VmValue::Int(0), VmValue::Float(8)});
    f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HI HI"))});
    return text;
}

void Measure(ContainerVm& f, VmObjectRef text, int w, int h) {
    f.On(text, "requestLayout", "()V");
    f.On(text, "measure", "(II)V", {VmValue::Int(w), VmValue::Int(h)});
}
}  // namespace

TEST_CASE("DVM-295 TextView specs state baseline cache and virtual super on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{text});
        f.On(text, "setPadding", "(IIII)V", {VmValue::Int(1), VmValue::Int(2), VmValue::Int(1), VmValue::Int(2)});
        const auto node = *FindViewUiNode(*f.context, text.Value());
        const auto host_measured = f.context->ui_tree.Get(node)->measured;
        const auto host_frame = f.context->ui_tree.Get(node)->frame;
        for (auto mode : {0, 0x40000000, static_cast<int>(0x80000000U)}) {
            Measure(f, text, mode | 20, mode | 10);
            CHECK(f.On(text, "getMeasuredWidth", "()I").AsInt() == (mode ? 20 : 31));
            CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == (mode ? 10 : 11));
            CHECK(f.On(text, "getMeasuredState", "()I").AsInt() == 0);
            CHECK(f.On(text, "getBaseline", "()I").AsInt() == 9);
            const auto plan = f.context->view_adapter_references.at(text.Value()).text_measure->line_plan;
            CHECK(plan->text == (mode ? u"HI\nHI" : u"HI HI"));
            f.On(text, "measure", "(II)V", {VmValue::Int(mode | 20), VmValue::Int(mode | 10)});
            CHECK(f.context->view_adapter_references.at(text.Value()).text_measure->line_plan->text == plan->text);
        }
        CHECK(f.context->ui_tree.Get(node)->measured == host_measured);
        CHECK(f.context->ui_tree.Get(node)->frame == host_frame);
        f.On(text, "setMinimumHeight", "(I)V", {VmValue::Int(40)});
        f.On(text, "setGravity", "(I)V", {VmValue::Int(0x53)});
        Measure(f, text, 0, 0);
        CHECK(f.On(text, "getBaseline", "()I").AsInt() == 38);
        f.On(text, "setMinimumHeight", "(I)V", {VmValue::Int(0)});
        f.On(text, "setGravity", "(I)V", {VmValue::Int(0x33)});
        for (auto type : {"Ltest/TextMeasureOverride;", "Ltest/TextMeasureSuper;"}) {
            const auto custom = Text(f, owner, type);
            const auto custom_root = f.vm.ProtectReferences(std::array{custom});
            Measure(f, custom, 0, 0);
            const auto super = std::string_view(type) == "Ltest/TextMeasureSuper;";
            CHECK(f.On(custom, "getMeasuredWidth", "()I").AsInt() == (super ? 29 : 37));
            CHECK(f.On(custom, "getMeasuredHeight", "()I").AsInt() == (super ? 7 : 19));
            CHECK(f.On(custom, "calls", "()I").AsInt() >= 1);
            CHECK(f.context->view_adapter_references.at(custom.Value()).text_measure.has_value() == super);
        }
        f.On(text, "setSingleLine", "(Z)V", {VmValue::Int(1)});
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HI\nHI"))});
        Measure(f, text, 20 | 0x40000000, 0);
        CHECK(f.context->view_adapter_references.at(text.Value()).text_measure->line_plan->text == u"HI HI");
        CHECK(f.model.StringValue(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref) == u"HI\nHI");
        CHECK(f.ledger.Unimplemented().empty());
        CHECK(f.OnOutcome(text, "setMaxLines", "(I)V", {VmValue::Int(0)}).exception.IsValid());
        CHECK(f.OnOutcome(text, "setGravity", "(I)V", {VmValue::Int(0x800003)}).exception.IsValid());
    }
}

TEST_CASE("DVM-295 TextView containers owner tick immutable raster and failed publish") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (auto container : {"FrameLayout", "LinearLayout", "RelativeLayout"}) {
            ContainerVm f(backend);
            const auto owner = Owner(f);
            const auto owner_root = f.vm.ProtectReferences(std::array{owner});
            const auto descriptor = "Landroid/widget/" + std::string(container) + ";";
            const auto group = f.New(descriptor.c_str(), "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto group_root = f.vm.ProtectReferences(std::array{group});
            const auto text = Text(f, owner);
            const auto text_root = f.vm.ProtectReferences(std::array{text});
            f.On(text, "setClickable", "(Z)V", {VmValue::Int(1)});
            f.On(group, "addView", "(Landroid/view/View;II)V",
                 {VmValue::Ref(text), VmValue::Int(-2), VmValue::Int(-2)});
            f.context->content_view = group;
            ++f.context->ui_content_revision;
            const auto tick = [&](int width = 20) {
                return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                AndroidUiTraversalPhase::ResolveOnly, ui::Size{width, 40});
            };
            CHECK(tick());
            CHECK(f.On(text, "getMeasuredWidth", "()I").AsInt() == 20);
            CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 15);
            const auto id = *FindViewUiNode(*f.context, text.Value());
            CHECK(tick(40));
            CHECK(tick(20));
            const auto old = f.context->ui_tree.PublishedProjection();
            REQUIRE(old);
            REQUIRE(old->Get(id)->text_line_plan);
            CHECK(old->Get(id)->text_line_plan->text == u"HI\nHI");
            const auto commands = ui::BuildUiRenderList(*old, {});
            bool found{};
            for (const auto& cmd : commands)
                if (const auto* draw = std::get_if<ui::DrawText>(&cmd)) {
                    CHECK(draw->line_plan == old->Get(id)->text_line_plan);
                    found = true;
                }
            CHECK(found);
            const auto frame = old->Get(id)->screen_frame;
            CHECK(old->HitTestProjection(frame.left + 1, frame.top + 1) == id);
            f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"M"))});
            f.On(text, "setTextColor", "(I)V", {VmValue::Int(static_cast<int>(0xff123456U))});
            CHECK(f.context->ui_tree.PublishedProjection() == old);
            CHECK(old->Get(id)->text == u"HI HI");
            CHECK(tick());
            const auto next = f.context->ui_tree.PublishedProjection();
            CHECK(next->ProjectionEpoch() > old->ProjectionEpoch());
            CHECK(next->Get(id)->text_line_plan->text == u"M");
            CHECK(next->Get(id)->text_color == 0x123456ff);
            CHECK(next->Get(id)->measured == ui::Size{5, 7});
            const auto custom = Text(f, owner, "Ltest/TextMeasureOverride;");
            f.On(group, "addView", "(Landroid/view/View;II)V",
                 {VmValue::Ref(custom), VmValue::Int(-2), VmValue::Int(-2)});
            CHECK(tick());
            const auto custom_id = *FindViewUiNode(*f.context, custom.Value());
            const auto overridden = f.context->ui_tree.PublishedProjection();
            CHECK(overridden->Get(custom_id)->measured == ui::Size{37, 19});
            CHECK(overridden->Get(custom_id)->text_line_plan->text == u"HI HI");
            CHECK_FALSE(f.context->view_adapter_references.at(custom.Value()).text_measure.has_value());
            // A later guest callback failure must preserve the complete epoch.
            const auto bad = Text(f, owner, "Ltest/TextMeasureFails;");
            f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(bad), VmValue::Int(-2), VmValue::Int(-2)});
            bool rejected{};
            try {
                static_cast<void>(tick());
            } catch (const VmJavaThrow&) {
                rejected = true;
            }
            CHECK(rejected);
            CHECK(f.context->ui_tree.PublishedProjection() == overridden);
            CHECK(f.On(bad, "calls", "()I").AsInt() > 0);
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

TEST_CASE("DVM-295 TextView host input retired by GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto id = *FindViewUiNode(*f.context, text.Value());
        Measure(f, text, 0, 0);
        static_cast<void>(f.vm.CollectGarbage("text-leaf-owner"));
        CHECK_FALSE(f.model.IsValidRef(text));
        CHECK_FALSE(f.context->view_adapter_references.contains(text.Value()));
        CHECK_FALSE(f.context->ui_tree.Get(id));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-361 TextView horizontal scrolling preserves newlines on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{text});
        auto& refs = f.context->view_adapter_references.at(text.Value());
        const auto get = [&] { return f.On(text, "getHorizontallyScrolling", "()Z").AsInt(); };
        const auto set = [&](bool value) {
            const auto revision = refs.text_revision;
            const auto changed = get() != value;
            const auto plan = refs.text_measure;
            f.On(text, "setHorizontallyScrolling", "(Z)V", {VmValue::Int(value)});
            CHECK(get() == value);
            CHECK(refs.text_revision == revision + changed);
            if (!changed && plan) CHECK(refs.text_measure->line_plan == plan->line_plan);
            if (changed) CHECK_FALSE(refs.text_measure.has_value());
        };
        const auto type = f.model.ObjectClass(text);
        CHECK(f.linker.Class(type).is_boot_dex);
        const auto getter = f.linker.FindVtableIndex(type, "getHorizontallyScrolling", "()Z");
        REQUIRE(getter);
        CHECK(f.linker.Method(f.linker.Class(type).vtable[*getter]).kind == MethodKind::interpreted);
        const auto horizontal_field = f.linker.FindFieldRecursive(type, "mHorizontallyScrolling", "Z");
        REQUIRE(horizontal_field);
        CHECK(f.model.InstanceSlots(text)[f.linker.Field(*horizontal_field).slot].bits == 0);
        CHECK(get() == 0);
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 15);
        set(false);
        set(true);
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(f.On(text, "getMeasuredWidth", "()I").AsInt() == 20);
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 7);
        CHECK(refs.text_measure->line_plan->text == u"HI HI");
        set(true);
        Measure(f, text, static_cast<int>(0x80000000U) | 20, 0);
        CHECK(f.On(text, "getMeasuredWidth", "()I").AsInt() == 20);
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 7);
        set(false);
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(refs.text_measure->line_plan->text == u"HI\nHI");
        set(true);
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HI HI\nHI HI"))});
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 15);
        CHECK(refs.text_measure->line_plan->text == u"HI HI\nHI HI");
        f.On(text, "setMaxLines", "(I)V", {VmValue::Int(1)});
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(refs.text_measure->line_plan->text == u"HI HI");
        f.On(text, "setSingleLine", "(Z)V", {VmValue::Int(1)});
        CHECK(get() == 1);
        Measure(f, text, 0x40000000 | 20, 0);
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == 7);
        CHECK(refs.text_measure->line_plan->text == u"HI HI HI HI");
        f.On(text, "setSingleLine", "(Z)V", {VmValue::Int(0)});
        CHECK(get() == 0);

        // Exercise both cached measurement and guest override raster-only preparation.
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto group_root = f.vm.ProtectReferences(std::array{group});
        const auto custom = Text(f, owner, "Ltest/TextMeasureOverride;");
        f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(text), VmValue::Int(20), VmValue::Int(-2)});
        f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(custom), VmValue::Int(20), VmValue::Int(-2)});
        f.context->content_view = group;
        ++f.context->ui_content_revision;
        for (bool horizontal : {false, true}) {
            for (auto view : {text, custom}) {
                f.On(view, "setText", "(Ljava/lang/CharSequence;)V",
                     {VmValue::Ref(f.model.NewString(u"HI HI\nHI HI"))});
                if (view == custom)
                    f.On(view, "setText", "(Ljava/lang/CharSequence;)V",
                         {VmValue::Ref(f.model.NewString(u"HI HI HI\nHI HI HI"))});
                f.On(view, "setHorizontallyScrolling", "(Z)V", {VmValue::Int(horizontal)});
            }
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{20, 80}));
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            const auto expected = horizontal ? u"HI HI\nHI HI" : u"HI\nHI\nHI\nHI";
            for (auto view : {text, custom}) {
                const auto id = *FindViewUiNode(*f.context, view.Value());
                const auto raster_expected =
                    view == custom ? (horizontal ? u"HI HI HI\nHI HI HI" : u"HI HI\nHI\nHI HI\nHI") : expected;
                CHECK(snapshot->Get(id)->text_line_plan->text == raster_expected);
            }
            CHECK(refs.text_measure->line_plan->text == expected);
            for (const auto& command : ui::BuildUiRenderList(*snapshot, {}))
                if (const auto* draw = std::get_if<ui::DrawText>(&command))
                    CHECK((draw->line_plan->text == expected ||
                           draw->line_plan->text == (horizontal ? u"HI HI HI\nHI HI HI" : u"HI HI\nHI\nHI HI\nHI")));
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}
