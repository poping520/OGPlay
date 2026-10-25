#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include "boot_dex.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"
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
        for (const auto reason : {AndroidUiTraversalReason::HostDialog, AndroidUiTraversalReason::GeometryRead}) {
            CHECK_THROWS_AS(EnsureAndroidUiTraversal(f.vm, *f.context, reason, AndroidUiTraversalPhase::ResolveOnly),
                            VmJavaThrow);
        }
        CHECK_FALSE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforeSurfaceCallbacks,
                                             AndroidUiTraversalPhase::ResolveAndNotifySizes));
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
                const auto location =
                    f.vm.Model().NewPrimitiveArray(f.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
                f.On(view, "getLocationInWindow", "([I)V", {VmValue::Ref(location)});
                CHECK(f.vm.Model().GetPrimitiveElement(location, 0) == 0);
                CHECK(f.vm.Model().GetPrimitiveElement(location, 1) == 0);
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

TEST_CASE("DVM-308 managed window coordinates focus cancellation and draw-only owner publication") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 180;
        f.context->surface_height = 120;
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveAndNotifySizes));
        const auto snapshot = AcquireAndroidPresentation(*f.context);
        REQUIRE(snapshot);
        const auto count = f.On(leaf, "count", "()I").AsInt();
        const auto epoch = f.context->ui_measure_epoch;
        f.On(leaf, "invalidate", "()V");
        CHECK_FALSE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                             AndroidUiTraversalPhase::ResolveOnly));
        CHECK(f.context->ui_measure_epoch == epoch + 1);
        CHECK(f.On(leaf, "count", "()I").AsInt() == count);
        CHECK(AcquireAndroidPresentation(*f.context) != snapshot);
        // Temporary queries observe nested Java geometry, including parent scroll,
        // without traversing or publishing those deliberately uncommitted frames.
        const auto frame = [&](VmObjectRef object, int l, int t, int r, int b) {
            f.On(object, "layout", "(IIII)V", {VmValue::Int(l), VmValue::Int(t), VmValue::Int(r), VmValue::Int(b)});
        };
        frame(group, 10, 20, 150, 110);
        frame(leaf, 7, 9, 27, 29);
        f.On(group, "scrollTo", "(II)V", {VmValue::Int(3), VmValue::Int(5)});
        const auto published = AcquireAndroidPresentation(*f.context);
        const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
        for (const auto name : {"getLocationInWindow", "getLocationOnScreen"}) {
            f.On(leaf, name, "([I)V", {VmValue::Ref(array)});
            CHECK(f.model.GetPrimitiveElement(array, 0) == 14);
            CHECK(f.model.GetPrimitiveElement(array, 1) == 24);
            CHECK(AcquireAndroidPresentation(*f.context) == published);
        }
        CHECK(f.On(leaf, "getWindowVisibility", "()I").AsInt() == 0);
        f.context->window_has_focus.store(true);
        f.context->window_focus_activity.store(owner.Value());
        CHECK(f.On(leaf, "hasWindowFocus", "()Z").AsInt() == 1);
        f.context->window_has_focus.store(false);
        CHECK(f.On(leaf, "hasWindowFocus", "()Z").AsInt() == 0);
        const auto invalid_location = f.OnOutcome(leaf, "getLocationOnScreen", "([I)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(invalid_location.exception.IsValid());
        CHECK(f.linker.Class(invalid_location.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        IntrinsicContext call{f.vm, leaf, {}};
        IntrinsicCall fields(call);
        const auto flags = f.context->view_java_measure_fields->flags[0];
        fields.SetInt(flags, fields.GetInt(flags) | 0x02004000);
        f.context->ui_touch_capture = leaf.Value();
        f.On(leaf, "onWindowFocusChanged", "(Z)V", {VmValue::Int(0)});
        CHECK((fields.GetInt(flags) & 0x02004000) == 0);
        CHECK(f.context->ui_touch_capture == 0);
        CHECK(f.context->ui_tree.Get(f.context->ui_tree.Root())->draw_dirty);
        f.On(leaf, "onWindowVisibilityChanged", "(I)V", {VmValue::Int(0)});
        f.On(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        f.context->window_has_focus.store(true);
        CHECK(f.On(leaf, "hasWindowFocus", "()Z").AsInt() == 0);
        CHECK(f.On(leaf, "getWindowVisibility", "()I").AsInt() == 8);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-308 GLSurfaceView invokes exact experimental SurfaceView initialization") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view =
            f.New("Landroid/opengl/GLSurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        IntrinsicContext call{f.vm, view, {}};
        IntrinsicCall fields(call);
        CHECK(fields.GetInt(*f.context->surface_view_requested_width_field) == -1);
        CHECK(fields.GetInt(*f.context->surface_view_requested_height_field) == -1);
        CHECK(f.On(view, "willNotDraw", "()Z").AsInt() == 1);
        const auto holder = f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        REQUIRE(holder.IsValid());
        CHECK(f.context->surface_holders.at(view.Value()) == holder);
        CHECK(f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref == holder);
        CHECK(f.On(view, "isFixedSize", "()Z").AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-308 real lifecycle owner barriers cover input Looper focus and first snapshot") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        // Exercise the production lifecycle with no native window or GPU callback.
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        REQUIRE(AcquireAndroidPresentation(*f.context));
        const auto leaf_field = f.linker.FindFieldRecursive(f.linker.ResolveDescriptor("Ltest/OwnerActivity;"), "leaf",
                                                            "Ltest/MeasuredLeaf;");
        REQUIRE(leaf_field);
        const auto leaf = f.context->content_view;
        REQUIRE(leaf.IsValid());
        CHECK(f.On(leaf, "count", "()I").AsInt() == 1);
        static_cast<void>(lifecycle.StepFrame());  // deferred initial window focus requests layout
        CHECK(f.On(leaf, "count", "()I").AsInt() == 2);
        const auto input_root = AndroidUiTraversalRoot(f.vm, *f.context);
        CHECK(f.On(input_root, "getChildCount", "()I").AsInt() == 2);
        const auto first_frame = f.app->DexVm().Session().TakeLatestFrame();
        REQUIRE(first_frame);
        const auto complete = AcquireAndroidPresentation(*f.context);
        REQUIRE(complete);
        const auto size_calls = [&] {
            unsigned count{};
            for (const auto& event : f.vm.Trace("", 10000))
                if (event.method_name == "onSizeChanged") ++count;
            return count;
        };
        const auto delivered_sizes = size_calls();
        for (unsigned i = 0; i < 3; ++i)
            CHECK_FALSE(DispatchAndroidViewSizes(f.vm, *f.context));
        CHECK(size_calls() == delivered_sizes);
        CHECK(f.context->ui_notified_view_sizes.empty());
        const auto epoch = f.context->ui_measure_epoch;
        const auto trace = f.vm.Trace("", 10000).size();
        const auto viewport = complete->viewport;
        static_cast<void>(lifecycle.ComposePresentedFrame(
            {.width = static_cast<std::uint32_t>(viewport.width),
             .height = static_cast<std::uint32_t>(viewport.height),
             .rgba8 = std::vector<std::uint8_t>(static_cast<std::size_t>(viewport.width) *
                                                static_cast<std::size_t>(viewport.height) * 4)}));
        CHECK(f.vm.Trace("", 10000).size() == trace);
        CHECK(f.context->ui_measure_epoch == epoch);
        const auto handler = f.New("Landroid/os/Handler;");
        const auto runnable = f.New("Ltest/OwnerMutation;");
        CHECK(f.On(handler, "post", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(runnable)}).AsInt() == 1);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.On(leaf, "count", "()I").AsInt() == 3);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.On(leaf, "count", "()I").AsInt() == 3);
        const auto builder = f.New("Landroid/app/AlertDialog$Builder;", "(Landroid/content/Context;)V",
                                   {VmValue::Ref(f.context->activity)});
        f.On(builder, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
             {VmValue::Ref(f.vm.NewStringUtf8("Owner modal"))});
        const auto dialog = f.On(builder, "create", "()Landroid/app/AlertDialog;").ref;
        f.On(dialog, "show", "()V");
        static_cast<void>(lifecycle.StepFrame());
        const auto modal_frame = f.app->DexVm().Session().TakeLatestFrame();
        REQUIRE(modal_frame);
        CHECK(modal_frame->sequence > first_frame->sequence);
        const auto modal_count = f.On(leaf, "count", "()I").AsInt();
        f.On(dialog, "setTitle", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Changed"))});
        static_cast<void>(lifecycle.StepFrame());
        const auto changed_frame = f.app->DexVm().Session().TakeLatestFrame();
        REQUIRE(changed_frame);
        CHECK(changed_frame->sequence > modal_frame->sequence);
        CHECK(f.On(leaf, "count", "()I").AsInt() == modal_count);
        f.On(dialog, "dismiss", "()V");
        static_cast<void>(lifecycle.StepFrame());
        const auto before_input = f.On(leaf, "count", "()I").AsInt();
        const auto children_before_input = f.On(input_root, "getChildCount", "()I").AsInt();
        AndroidBoundaryInput input;
        input.type = AndroidBoundaryInputType::pointer_button;
        input.pressed = true;
        input.x = 4;
        input.y = 4;
        f.On(leaf, "requestLayout", "()V");
        lifecycle.QueueInput(input);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.Static("Ltest/OwnerActivity;", "inputSeen", "()I").AsInt() == before_input + 1);
        CHECK(f.On(leaf, "count", "()I").AsInt() == before_input + 2);
        CHECK(f.On(input_root, "getChildCount", "()I").AsInt() == children_before_input + 1);
        const auto owner = f.context->activity;
        const auto video = f.New("Landroid/widget/VideoView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto prepared = f.New("Ltest/OwnerPrepared;");
        f.context->vfs = &f.filesystem;
        f.filesystem.MountHostDirectory("/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
        f.context->video_source_player_factory = [](auto) {
            return std::make_unique<ogplay::video::FakeVideoPlayer>(ogplay::video::VideoMetadata{320, 240, 1000}, 10);
        };
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        f.On(root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(video)});
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        f.On(video, "setOnPreparedListener", "(Landroid/media/MediaPlayer$OnPreparedListener;)V",
             {VmValue::Ref(prepared)});
        f.On(video, "setVideoPath", "(Ljava/lang/String;)V",
             {VmValue::Ref(f.vm.NewStringUtf8("/sdcard/short-mp4v-aac.mp4"))});
        const auto before_metadata = f.On(leaf, "count", "()I").AsInt();
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.On(leaf, "count", "()I").AsInt() == before_metadata + 1);
        const auto before_producer = AcquireAndroidPresentation(*f.context);
        const auto producer_count = f.On(leaf, "count", "()I").AsInt();
        f.On(video, "start", "()V");
        static_cast<void>(lifecycle.StepFrame());
        const auto producer_snapshot = AcquireAndroidPresentation(*f.context);
        REQUIRE(producer_snapshot);
        CHECK(producer_snapshot != before_producer);
        const auto video_node = *FindViewUiNode(*f.context, video.Value());
        CHECK(producer_snapshot->guest_window_epoch_refs.front()->Get(video_node)->surface_has_frame);
        CHECK(f.On(leaf, "count", "()I").AsInt() == producer_count);
        f.On(video, "stopPlayback", "()V");
        const auto before_resume = f.On(leaf, "count", "()I").AsInt();
        static_cast<void>(lifecycle.Suspend());
        static_cast<void>(lifecycle.Resume());
        CHECK(f.On(leaf, "count", "()I").AsInt() == before_resume + 1);
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}
