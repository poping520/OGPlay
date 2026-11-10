#include "dexvm/resource_fixture.h"
#include "container_traversal_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ogplay/runtime/ui/ui_leaf_measure.h"
#include <filesystem>
#include <algorithm>
#include <limits>

using ogplay::test::container_traversal::ContainerVm;

namespace {
VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    ogplay::test::ResourceFixture(*f.context).package_id = 0x7f;
    ogplay::test::ResourceFixtureEntry theme;
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
    ogplay::test::ResourceFixture(*f.context).entries = {theme, style};
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
        const auto lines = f.On(text, "getMaxLines", "()I").AsInt();
        CHECK_FALSE(f.OnOutcome(text, "setMaxLines", "(I)V", {VmValue::Int(0)}).exception.IsValid());
        CHECK(f.On(text, "getMaxLines", "()I").AsInt() == lines);
        // Gravity.START resolves to LEFT under the fixed LTR direction.
        CHECK_FALSE(f.OnOutcome(text, "setGravity", "(I)V", {VmValue::Int(0x800003)}).exception.IsValid());
        CHECK(f.On(text, "getGravity", "()I").AsInt() == 0x33);
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

TEST_CASE("DVM-362 TextView Paint hint input and real Java filters on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto font_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "data/android/19/fonts";
        const auto font_bytes = [&](const char* name) {
            std::ifstream stream(font_root / name, std::ios::binary);
            REQUIRE(stream.good());
            const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
            std::vector<std::byte> bytes(raw.size());
            std::memcpy(bytes.data(), raw.data(), raw.size());
            return bytes;
        };
        f.context->ui_tree.SetFallbackFont(std::make_shared<ui::UiFallbackFont>(font_bytes("Roboto-Regular.ttf"),
                                                                                font_bytes("DroidSansFallback.ttf")));
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{text});
        const auto paint = f.On(text, "getPaint", "()Landroid/text/TextPaint;").ref;
        CHECK(f.On(text, "getPaint", "()Landroid/text/TextPaint;").ref == paint);
        for (float size : {8.F, 16.F, 24.F}) {
            f.On(text, "setTextSize", "(IF)V", {VmValue::Int(0), VmValue::Float(size)});
            CHECK(f.On(paint, "getTextSize", "()F").AsFloat() == size);
            CHECK(f.On(text, "getTextSize", "()F").AsFloat() == size);
        }
        const auto face =
            f.Static("Landroid/graphics/Typeface;", "create", "(Ljava/lang/String;I)Landroid/graphics/Typeface;",
                     {VmValue::Ref(f.vm.NewStringUtf8("monospace")), VmValue::Int(1)})
                .ref;
        const auto face_root = f.vm.ProtectReferences(std::array{face});
        f.On(text, "setTypeface", "(Landroid/graphics/Typeface;)V", {VmValue::Ref(face)});
        CHECK(f.On(paint, "getTypeface", "()Landroid/graphics/Typeface;").ref == face);
        CHECK(f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref == face);
        // The TextView getter reads the same native Paint that the projection consumes.
        f.On(paint, "setTextSize", "(F)V", {VmValue::Float(32)});
        CHECK(f.On(paint, "getTextSize", "()F").AsFloat() == 32);
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 32);
        f.On(paint, "setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;",
             {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(f.On(paint, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
        CHECK_FALSE(f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
        f.On(text, "setTypeface", "(Landroid/graphics/Typeface;)V", {VmValue::Ref(face)});
        f.On(text, "setTextSize", "(IF)V", {VmValue::Int(0), VmValue::Float(8)});
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u""))});
        const auto hint = f.model.NewString(u"WATERMARK");
        f.On(text, "setHint", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(hint)});
        CHECK(f.On(text, "getHint", "()Ljava/lang/CharSequence;").ref == hint);
        Measure(f, text, 0, 0);
        const auto hint_width = f.On(text, "getMeasuredWidth", "()I").AsInt();
        CHECK(f.context->view_adapter_references.at(text.Value()).text_measure->line_plan->text == u"WATERMARK");
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HI"))});
        Measure(f, text, 0, 0);
        CHECK(f.On(text, "getMeasuredWidth", "()I").AsInt() < hint_width);
        CHECK(f.context->view_adapter_references.at(text.Value()).text_measure->line_plan->text == u"HI");
        f.On(text, "setInputType", "(I)V", {VmValue::Int(129)});
        CHECK(f.On(text, "getInputType", "()I").AsInt() == 129);
        CHECK(f.On(text, "getHorizontallyScrolling", "()Z").AsInt() != 0);
        Measure(f, text, 0, 0);
        CHECK(f.context->view_adapter_references.at(text.Value()).text_measure->line_plan->text == u"\u2022\u2022");
        CHECK(f.model.StringValue(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref) == u"HI");
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto group_root = f.vm.ProtectReferences(std::array{group});
        const auto custom_view = Text(f, owner, "Ltest/TextMeasureOverride;");
        f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(text), VmValue::Int(200), VmValue::Int(-2)});
        f.On(group, "addView", "(Landroid/view/View;II)V",
             {VmValue::Ref(custom_view), VmValue::Int(200), VmValue::Int(-2)});
        f.context->content_view = group;
        ++f.context->ui_content_revision;
        for (const auto view : {text, custom_view}) {
            f.On(view, "setHint", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HINT"))});
            f.On(view, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u""))});
        }
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 100}));
        std::size_t hint_draws = 0;
        for (const auto& command : ui::BuildUiRenderList(*f.context->ui_tree.PublishedProjection(), {}))
            if (const auto* draw = std::get_if<ui::DrawText>(&command)) {
                CHECK(draw->line_plan->text == u"HINT");
                ++hint_draws;
            }
        CHECK(hint_draws == 2);
        for (const auto view : {text, custom_view}) {
            const auto node = f.context->ui_tree.PublishedProjection()->Get(*FindViewUiNode(*f.context, view.Value()));
            CHECK(node->text_line_plan->text == u"HINT");
            const auto argb = static_cast<std::uint32_t>(f.On(view, "getCurrentHintTextColor", "()I").AsInt());
            CHECK(node->text_color == ((argb << 8U) | (argb >> 24U)));
            f.On(view, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"ABC"))});
            f.On(view, "setInputType", "(I)V", {VmValue::Int(129)});
        }
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 100}));
        for (const auto view : {text, custom_view})
            CHECK(f.context->ui_tree.PublishedProjection()
                      ->Get(*FindViewUiNode(*f.context, view.Value()))
                      ->text_line_plan->text == u"\u2022\u2022\u2022");
        for (int type : {2, 524321, 528385, 0x20001}) {
            f.On(text, "setInputType", "(I)V", {VmValue::Int(type)});
            CHECK(f.On(text, "getInputType", "()I").AsInt() == type);
            CHECK(f.On(text, "getHorizontallyScrolling", "()Z").AsInt() == (type != 0x20001));
        }
        const auto filters = f.model.NewObjectArray(f.linker.ResolveDescriptor("[Landroid/text/InputFilter;"),
                                                    f.linker.ResolveDescriptor("Landroid/text/InputFilter;"), 2);
        const auto filter_root = f.vm.ProtectReferences(std::array{filters});
        const auto length = f.New("Landroid/text/InputFilter$LengthFilter;", "(I)V", {VmValue::Int(3)});
        f.model.SetObjectElement(filters, 0, length);
        const auto custom = f.New("Ltest/OrderedInputFilter;");
        f.model.SetObjectElement(filters, 1, custom);
        f.On(text, "setFilters", "([Landroid/text/InputFilter;)V", {VmValue::Ref(filters)});
        CHECK(f.On(text, "getFilters", "()[Landroid/text/InputFilter;").ref == filters);
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"123456"))});
        CHECK(f.model.StringValue(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref) == u"OK");
        CHECK(f.On(custom, "calls", "()I").AsInt() == 1);
        const auto edit = Text(f, owner, "Landroid/widget/EditText;");
        const auto edit_root = f.vm.ProtectReferences(std::array{edit});
        f.On(edit, "setFilters", "([Landroid/text/InputFilter;)V", {VmValue::Ref(filters)});
        f.On(edit, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"abcdef"))});
        const auto editable = f.On(edit, "getText", "()Landroid/text/Editable;").ref;
        CHECK(f.model.StringValue(f.On(editable, "toString", "()Ljava/lang/String;").ref) == u"OK");
        CHECK(f.On(custom, "calls", "()I").AsInt() == 2);
        const auto bad =
            f.OnOutcome(text, "setFilters", "([Landroid/text/InputFilter;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(bad.exception.IsValid());
        CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
    }
}

TEST_CASE("DVM-366 TextView and Editable keep their fixed-font length bound on both backends") {
    constexpr auto bound = ui::kMaxFixedTextUnits;
    // One glyph per wrapped line at 128 px bold-italic keeps every coordinate inside int32.
    const std::u16string longest(bound, u'W');
    const auto wrapped = ui::WrapFixedText(longest, 128, 3, 1, std::numeric_limits<std::int32_t>::max());
    CHECK(wrapped.size() == 2 * bound - 1);
    CHECK(ui::MeasureFixedText(wrapped, 128, 3) ==
          ui::FixedTextMetrics{128, static_cast<std::int32_t>((bound * 8 - 1) * 16), 16});
    CHECK(ui::MeasureFixedText(longest, 128, 3).width == static_cast<std::int32_t>((bound * 9 - 1) * 16));
    CHECK_THROWS(static_cast<void>(ui::MeasureFixedText(longest + u"W", 8)));
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto root = f.vm.ProtectReferences(std::array{text});
        const auto text_length = [&] {
            return static_cast<int>(
                f.model.StringValue(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref).size());
        };
        const auto set = [&](VmObjectRef view, const std::u16string& value) {
            return f.OnOutcome(view, "setText", "(Ljava/lang/CharSequence;)V",
                               {VmValue::Ref(f.model.NewString(value))});
        };
        // GTASA's Social Club label: 1193 units, no wrapping, explicit lines.
        std::u16string label;
        while (label.size() < 1193)
            label += u"SOCIAL CLUB TERMS\n";
        label.resize(1193);
        f.On(text, "setHorizontallyScrolling", "(Z)V", {VmValue::Int(1)});
        REQUIRE_FALSE(set(text, label).exception.IsValid());
        CHECK(text_length() == 1193);
        Measure(f, text, 0, 0);
        const auto lines = static_cast<std::int32_t>(std::count(label.begin(), label.end(), u'\n')) + 1;
        CHECK(f.On(text, "getMeasuredHeight", "()I").AsInt() == lines * 8 - 1);
        REQUIRE_FALSE(set(text, std::u16string(bound, u'H')).exception.IsValid());
        CHECK(text_length() == static_cast<int>(bound));
        const auto over = set(text, std::u16string(bound + 1, u'H'));
        REQUIRE(over.exception.IsValid());
        CHECK(f.linker.Class(over.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(over.exception_message == "text outside bounded service");
        CHECK(text_length() == static_cast<int>(bound));

        const auto paint = f.New("Landroid/graphics/Paint;");
        const auto paint_root = f.vm.ProtectReferences(std::array{paint});
        const auto measure = [&](std::size_t size) {
            return f.OnOutcome(paint, "measureText", "(Ljava/lang/String;)F",
                               {VmValue::Ref(f.model.NewString(std::u16string(size, u'H')))});
        };
        const auto at_limit = measure(bound);
        const auto past_limit = measure(bound + 1);
        REQUIRE_FALSE(at_limit.exception.IsValid());
        REQUIRE_FALSE(past_limit.exception.IsValid());
        CHECK(past_limit.value.AsFloat() > at_limit.value.AsFloat());

        const auto edit = Text(f, owner, "Landroid/widget/EditText;");
        const auto edit_root = f.vm.ProtectReferences(std::array{edit});
        REQUIRE_FALSE(set(edit, std::u16string(bound - 1, u'H')).exception.IsValid());
        const auto editable = f.On(edit, "getText", "()Landroid/text/Editable;").ref;
        const auto replace = [&](const char16_t* value) {
            return f.OnOutcome(editable, "replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
                               {VmValue::Int(0), VmValue::Int(0), VmValue::Ref(f.model.NewString(value))});
        };
        REQUIRE_FALSE(replace(u"H").exception.IsValid());
        CHECK(f.On(editable, "length", "()I").AsInt() == static_cast<int>(bound));
        const auto edit_over = replace(u"H");
        REQUIRE(edit_over.exception.IsValid());
        CHECK(edit_over.exception_message == "text exceeds bounded edit service");
        CHECK(f.On(editable, "length", "()I").AsInt() == static_cast<int>(bound));
    }
}

TEST_CASE("DVM-366 plain TextView touch returns View.onTouchEvent and editors fail explicitly on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto text = Text(f, owner);
        const auto edit = Text(f, owner, "Landroid/widget/EditText;");
        const auto root = f.vm.ProtectReferences(std::array{text, edit});
        const auto event = [&](int action) { return MakeMotionEvent(f.vm, action, 2, 2, 0); };
        const auto touch = [&](VmObjectRef view, int action) {
            return f.OnOutcome(view, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(event(action))});
        };
        // GTASA andButton overlays a non-clickable label on its ImageButton: the label must not consume.
        for (const int action : {0, 1}) {
            const auto result = touch(text, action);
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK(result.value.AsInt() == 0);
        }
        // Whatever View.onTouchEvent decides is returned unchanged.
        const auto view_type = f.linker.ResolveDescriptor("Landroid/view/View;");
        const auto slot = f.linker.FindVtableIndex(view_type, "onTouchEvent", "(Landroid/view/MotionEvent;)Z");
        REQUIRE(slot);
        f.On(text, "setClickable", "(Z)V", {VmValue::Int(1)});
        for (const auto& [node_clickable, expected] : {std::pair{false, 0}, std::pair{true, 1}}) {
            f.context->ui_tree.Get(*FindViewUiNode(*f.context, text.Value()))->clickable = node_clickable;
            const auto direct = f.vm.Call(f.linker.Class(view_type).vtable[*slot],
                                          std::array{VmValue::Ref(text), VmValue::Ref(event(0))});
            const auto result = touch(text, 0);
            REQUIRE_FALSE(result.exception.IsValid());
            CHECK(result.value.AsInt() == direct.value.AsInt());
            CHECK(result.value.AsInt() == expected);
        }
        CHECK(f.ledger.Unimplemented().empty());
        const auto rejected = touch(edit, 0);
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(rejected.exception_message == "editable TextView touch requires Editor/MovementMethod/IME");
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-368 initially hidden ancestors skip descendant text plans") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (bool gone : {false, true}) {
            ContainerVm f(backend);
            const auto owner = Owner(f);
            f.context->activity = owner;
            auto& record = f.context->activity_stack.emplace_back();
            record.object = owner;
            record.visible = record.resumed = true;
            const auto root = f.vm.ProtectReferences(std::array{owner});
            const auto outer =
                f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto parent =
                f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto text = Text(f, owner);
            const auto refs = f.vm.ProtectReferences(std::array{outer, parent, text});
            // Like XML inflation, set the parent's visibility before adding children.
            // Changing an attached TextView's visibility callback remains outside this test.
            if (gone) f.On(parent, "setVisibility", "(I)V", {VmValue::Int(8)});
            f.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(text)});
            f.On(outer, "addView", "(Landroid/view/View;)V", {VmValue::Ref(parent)});
            f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(outer)});
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 100}));
            const auto id = *FindViewUiNode(*f.context, text.Value());
            CHECK(f.context->view_adapter_references.at(text.Value()).text_measure_cache.empty() == gone);
            CHECK((f.On(text, "getMeasuredWidth", "()I").AsInt() == 0) == gone);
            CHECK(static_cast<bool>(f.context->ui_tree.PublishedProjection()->Get(id)->text_line_plan) == !gone);
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}
