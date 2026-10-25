#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include "boot_dex.h"
#include <doctest/doctest.h>
#include <array>
#include <functional>
#include <thread>

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-307 stable empty Activity root owner binding and clean barriers") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        CHECK(f.context->ui_java_owner_thread == std::this_thread::get_id());
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 180;
        f.context->surface_height = 120;
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        const auto barrier = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforeInput,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        CHECK_FALSE(AndroidUiTraversalRoot(f.vm, *f.context).IsValid());
        REQUIRE(tick());
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        REQUIRE(root.IsValid());
        CHECK_FALSE(f.context->content_view.IsValid());
        CHECK(f.model.ObjectClass(root) == f.linker.ResolveDescriptor("Landroid/widget/FrameLayout;"));
        CHECK(f.On(root, "getChildCount", "()I").AsInt() == 0);
        const auto empty = f.context->ui_tree.PublishedProjection();
        REQUIRE(empty);
        const auto node = *FindViewUiNode(*f.context, root.Value());
        CHECK(empty->Get(node)->frame == ui::Rect{0, 0, 180, 120});
        CHECK(empty->Get(node)->children.empty());
        const auto invocations = [&] {
            std::array<unsigned, 2> result{};
            for (const auto& event : f.vm.Trace("", 10000)) {
                if (event.method_name == "measure") ++result[0];
                if (event.method_name == "layout") ++result[1];
            }
            return result;
        };
        const auto calls = invocations();
        const auto epoch = f.context->ui_measure_epoch;
        CHECK_FALSE(barrier());
        CHECK_FALSE(tick());
        CHECK_FALSE(barrier());
        CHECK(f.context->ui_measure_epoch == epoch);
        CHECK(invocations() == calls);
        CHECK(f.context->ui_tree.PublishedProjection() == empty);
        const auto first = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto second = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        // Constructing detached Views cannot dirty this window's published geometry.
        CHECK_FALSE(barrier());
        CHECK(f.context->ui_tree.PublishedProjection() == empty);
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        REQUIRE(barrier());
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        CHECK(f.context->content_view == first);
        CHECK(f.On(first, "count", "()I").AsInt() == 1);
        CHECK_FALSE(barrier());
        CHECK_FALSE(tick());
        CHECK(f.On(first, "count", "()I").AsInt() == 1);
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        REQUIRE(barrier());
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        CHECK(f.On(second, "count", "()I").AsInt() == 1);
        const auto complete = f.context->ui_tree.PublishedProjection();
        REQUIRE(complete);
        CHECK(complete->Get(node)->children == std::vector<ui::UiNodeId>{*FindViewUiNode(*f.context, second.Value())});
        f.context->surface_width = 210;
        f.context->surface_height = 130;
        f.context->ui_density = 1.5f;
        REQUIRE(barrier());
        CHECK(f.On(root, "getWidth", "()I").AsInt() == 210);
        CHECK(f.On(second, "count", "()I").AsInt() == 2);
        const auto resized = f.context->ui_tree.PublishedProjection();
        CHECK_FALSE(tick());
        CHECK(f.context->ui_tree.PublishedProjection() == resized);
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto reason :
             {AndroidUiTraversalReason::BeforePresent, AndroidUiTraversalReason::HostDialog,
              AndroidUiTraversalReason::BeforeSurfaceCallbacks, AndroidUiTraversalReason::GeometryRead}) {
            CHECK_THROWS_AS(EnsureAndroidUiTraversal(f.vm, *f.context, reason, AndroidUiTraversalPhase::ResolveOnly),
                            VmJavaThrow);
        }
        CHECK_THROWS_AS(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                 AndroidUiTraversalPhase::ResolveAndNotifySizes),
                        VmJavaThrow);
        CHECK(f.context->ui_tree.PublishedProjection() == resized);
    }
}

TEST_CASE("DVM-307 deferred second pass stays in next owner tick and callback geometry is transient") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::function<void(IntrinsicContext&, int)> hook;
        std::vector<int> events;
        auto probe = IntrinsicClassBuilder::Class("Ltest/LayoutProbe;");
        probe.StaticMethod("size", "(Landroid/view/View;IIII)V", [&](IntrinsicContext& c) {
            events.push_back(0);
            if (hook) hook(c, 0);
            return VmValue::Void();
        });
        probe.StaticMethod("hit", "(Landroid/view/View;I)V", [&](IntrinsicContext& c) {
            const auto stage = c.arguments[1].AsInt();
            events.push_back(stage);
            if (hook) hook(c, stage);
            return VmValue::Void();
        });
        probe.StaticMethod("traverse", "()V", [](IntrinsicContext&) { return VmValue::Void(); });
        AndroidValueVm f(backend, {std::move(probe).Build()}, false, "view_layout.dex", false,
                         AndroidUiTraversalOwner::JavaTraversal, OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Ltest/LayoutLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 80;
        f.context->surface_height = 50;
        f.context->content_view = view;
        const auto ensure = [&](AndroidUiTraversalReason reason) {
            return EnsureAndroidUiTraversal(f.vm, *f.context, reason, AndroidUiTraversalPhase::ResolveOnly);
        };
        const auto node = *FindViewUiNode(*f.context, view.Value());
        std::shared_ptr<const ui::UiTree> temporary;
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 0) {
                const auto published = f.context->ui_tree.PublishedProjection();
                const auto epoch = f.context->ui_measure_epoch;
                temporary = ReadAndroidUiGeometry(f.vm, *f.context);
                CHECK(temporary->Get(node)->frame == ui::Rect{0, 0, 80, 50});
                CHECK(f.context->ui_tree.PublishedProjection() == published);
                CHECK(f.context->ui_measure_epoch == epoch);
                CHECK(f.context->surface_holders.empty());
            }
            if (stage == 2) f.On(view, "requestLayout", "()V");
        };
        REQUIRE(ensure(AndroidUiTraversalReason::OwnerTick));
        CHECK(events == std::vector<int>{0, 1, 2, 1, 2});
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(view));
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        const auto epoch = f.context->ui_measure_epoch;
        events.clear();
        for (int i = 0; i < 4; ++i)
            CHECK_FALSE(ensure(AndroidUiTraversalReason::BeforeInput));
        CHECK(events.empty());
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        CHECK(f.context->ui_measure_epoch == epoch);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(view));
        // Another root has its own queue/budget; switching back cannot steal A's deferred work.
        const auto other = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.context->content_view = other;
        ++f.context->ui_content_revision;
        REQUIRE(ensure(AndroidUiTraversalReason::BeforeInput));
        const auto other_snapshot = f.context->ui_tree.PublishedProjection();
        f.context->content_view = view;
        ++f.context->ui_content_revision;
        CHECK_FALSE(ensure(AndroidUiTraversalReason::BeforeInput));
        CHECK(f.context->ui_tree.PublishedProjection() == other_snapshot);
        hook = {};
        REQUIRE(ensure(AndroidUiTraversalReason::OwnerTick));
        CHECK(events == std::vector<int>{1, 2});
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        CHECK_FALSE(ensure(AndroidUiTraversalReason::BeforeInput));
        // Exceptions preserve the complete publication and already-executed Java state.
        const auto complete = f.context->ui_tree.PublishedProjection();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 0) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "DVM-307 callback"};
        };
        f.context->surface_width = 90;
        CHECK_THROWS_AS(ensure(AndroidUiTraversalReason::OwnerTick), VmJavaThrow);
        CHECK(f.context->ui_tree.PublishedProjection() == complete);
        CHECK(f.On(view, "getWidth", "()I").AsInt() == 90);
        CHECK(temporary->Get(node)->frame == ui::Rect{0, 0, 80, 50});
        // Swapping content from a callback cancels the old generation's commit.
        const auto replacement = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 0) {
                f.context->content_view = replacement;
                ++f.context->ui_content_revision;
            }
        };
        f.context->surface_width = 100;
        CHECK_FALSE(ensure(AndroidUiTraversalReason::OwnerTick));
        CHECK(f.context->ui_tree.PublishedProjection() == complete);
        hook = {};
        REQUIRE(ensure(AndroidUiTraversalReason::OwnerTick));
        CHECK(f.ledger.Unimplemented().empty());
    }
}
