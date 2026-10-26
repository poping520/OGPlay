#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include "boot_dex.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"
#include <doctest/doctest.h>
#include <array>
#include <functional>
#include <algorithm>
#include <thread>
#include "ogplay/runtime/ui/ui_renderer.h"
#include <chrono>
#include "ogplay/session/ui_compositor.h"

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

namespace {
void OwnerHook(ogplay::test::container_traversal::ContainerVm& f, VmObjectRef receiver, const char* name,
               const char* signature, IntrinsicHandler hook) {
    const auto klass = f.model.ObjectClass(receiver);
    const auto index = f.linker.FindVtableIndex(klass, name, signature);
    REQUIRE(index);
    auto& method = f.linker.MutableMethod(f.linker.Class(klass).vtable[*index]);
    method.code.reset();
    method.fast_code.reset();
    method.kind = MethodKind::intrinsic;
    method.implementation = std::move(hook);
}
}  // namespace

TEST_CASE("DVM-309 owner input lease survives mutation and cancels detached gone disabled reparented targets once") {
    using ogplay::test::container_traversal::ContainerVm;
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (int mutation = 0; mutation < 5; ++mutation) {
            ContainerVm f(backend);
            f.context->ui_test_accessibility_disabled = true;
            f.context->surface_width = 180;
            f.context->surface_height = 120;
            const auto owner = f.New("Landroid/app/Activity;");
            f.context->activity = owner;
            const auto group =
                f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto listener = f.New("Ltest/StateListener;");
            f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
            f.On(leaf, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V", {VmValue::Ref(listener)});
            f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly));
            std::vector<int> actions;
            const auto initial = AcquireAndroidPresentation(*f.context);
            const auto measured = f.On(leaf, "count", "()I").AsInt();
            OwnerHook(
                f, listener, "onTouch", "(Landroid/view/View;Landroid/view/MotionEvent;)Z", [&](IntrinsicContext& c) {
                    const auto action = f.On(c.arguments[1].ref, "getAction", "()I").AsInt();
                    actions.push_back(action);
                    CHECK(f.context->input_presentation == initial);
                    if (action == 0) {
                        if (mutation == 0) f.On(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
                        if (mutation == 1) f.On(leaf, "setVisibility", "(I)V", {VmValue::Int(8)});
                        if (mutation == 2) f.On(leaf, "setEnabled", "(Z)V", {VmValue::Int(0)});
                        if (mutation == 3) {
                            f.On(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
                            f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
                        }
                        if (mutation == 4)
                            f.On(leaf, "layout", "(IIII)V",
                                 {VmValue::Int(50), VmValue::Int(50), VmValue::Int(100), VmValue::Int(100)});
                        CHECK_FALSE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                             AndroidUiTraversalPhase::ResolveOnly));
                        CHECK(f.context->ui_tree.PublishedProjection() == initial->guest_window_epoch_refs.front());
                        CHECK(ViewContainsPoint(f.vm, *f.context, leaf.Value(), 4, 4));
                        CHECK(f.On(leaf, "count", "()I").AsInt() == measured);
                    }
                    return VmValue::Int(1);
                });
            {
                AndroidInputEvent event(f.vm, *f.context);
                CHECK(f.context->input_presentation == initial);
                CHECK_THROWS_AS(AndroidInputEvent(f.vm, *f.context), VmJavaThrow);
                const auto down = DispatchDeepTouchEvent(f.vm, *f.context, 0, 4, 4, 0);
                REQUIRE_FALSE(down.error);
                CHECK(down.handled);
                CHECK(f.context->input_presentation == initial);
            }
            CHECK_FALSE(f.context->input_presentation);
            if (mutation < 4) {
                CHECK(actions == std::vector<int>{0, 3});
                {
                    AndroidInputEvent event(f.vm, *f.context);
                    const auto up = DispatchDeepTouchEvent(f.vm, *f.context, 1, 4, 4, 0);
                    REQUIRE_FALSE(up.error);
                    CHECK(up.handled);
                }
                CHECK(actions == std::vector<int>{0, 3});
                CHECK(f.context->ui_touch_capture == 0);
            } else {
                CHECK(actions == std::vector<int>{0});
                // Frozen hit coordinates change only at the next event boundary.
                static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforeInput,
                                                           AndroidUiTraversalPhase::ResolveOnly));
                AndroidInputEvent event(f.vm, *f.context);
                CHECK_FALSE(ViewContainsPoint(f.vm, *f.context, leaf.Value(), 4, 4));
                CHECK(ViewContainsPoint(f.vm, *f.context, leaf.Value(), 60, 60));
            }
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

TEST_CASE("DVM-309 session pointer and key Activity fallback borrow the event presentation") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        static_cast<void>(lifecycle.StepFrame());
        auto expected = AcquireAndroidPresentation(*f.context);
        unsigned touches{}, keys{};
        OwnerHook(f, f.context->activity, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", [&](IntrinsicContext&) {
            ++touches;
            REQUIRE(f.context->input_presentation == expected);
            static_cast<void>(FindTouchReceiversAt(f.vm, *f.context, 4, 4));
            CHECK(f.context->input_presentation == expected);
            f.On(f.context->content_view, "requestLayout", "()V");
            CHECK_FALSE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                 AndroidUiTraversalPhase::ResolveOnly));
            return VmValue::Int(1);
        });
        OwnerHook(f, f.context->activity, "onKeyDown", "(ILandroid/view/KeyEvent;)Z", [&](IntrinsicContext&) {
            ++keys;
            CHECK(f.context->input_presentation == expected);
            return VmValue::Int(1);
        });
        AndroidBoundaryInput input;
        input.type = AndroidBoundaryInputType::pointer_button;
        input.pressed = true;
        input.x = 4;
        input.y = 4;
        lifecycle.QueueInput(input);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(touches == 1);
        CHECK_FALSE(f.context->input_presentation);
        expected = AcquireAndroidPresentation(*f.context);
        input.type = AndroidBoundaryInputType::key;
        input.code = 29;
        input.action = 0;
        lifecycle.QueueInput(input);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(keys == 1);
        CHECK_FALSE(f.context->input_presentation);
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}

TEST_CASE("DVM-310 lifecycle window partitions retain Java identity scroll focus and failed-switch snapshot") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        static_cast<void>(lifecycle.StepFrame());
        const auto a = f.context->activity;
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        const auto leaf = f.context->content_view;
        f.On(leaf, "setFocusableInTouchMode", "(Z)V", {VmValue::Int(1)});
        REQUIRE(f.On(leaf, "requestFocus", "()Z").AsInt() == 1);
        f.context->arsc.package_id = 0x7f;
        ogplay::loader::ArscEntry theme;
        theme.resource_id = 0x7f020001;
        theme.type_name = "style";
        theme.entry_name = "surface";
        theme.is_complex = true;
        theme.parent = 0x01030005;
        for (auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U})
            theme.bag.push_back({id, 1, 0, {}});
        f.context->arsc.entries = {theme};
        f.On(a, "setTheme", "(I)V", {VmValue::Int(static_cast<int>(theme.resource_id))});
        const auto surface_view =
            f.New("Landroid/view/SurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(a)});
        const auto holder = f.On(surface_view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = f.New("Ltest/ManagedSurfaceCallback;");
        f.On(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        f.On(root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(surface_view)});
        f.On(root, "scrollTo", "(II)V", {VmValue::Int(3), VmValue::Int(5)});
        static_cast<void>(lifecycle.StepFrame());
        const auto old_surface = f.On(holder, "getSurface", "()Landroid/view/Surface;").ref;
        REQUIRE(old_surface.IsValid());
        REQUIRE(f.context->active_surface_holders.contains(holder.Value()));
        const auto sequence = [&] {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(callback), "sequence", "I");
            REQUIRE(field);
            return static_cast<int>(f.model.InstanceSlots(callback)[f.linker.Field(*field).slot].bits);
        };
        CHECK(sequence() == 12);
        const auto saved = AcquireAndroidPresentation(*f.context);
        REQUIRE(saved);
        const auto a_node = *FindViewUiNode(*f.context, leaf.Value());
        const auto root_node = *FindViewUiNode(*f.context, root.Value());
        const auto a_size = saved->guest_window_epoch_refs.front()->Get(a_node)->measured;
        const auto intent = f.New("Landroid/content/Intent;");
        DexVmAndroidContext::ActivityCommand launch;
        launch.owner = a;
        launch.intent = intent;
        launch.descriptor = "Ltest/OwnerActivity;";
        launch.component = "test.OwnerActivity";
        f.context->activity_commands.push_back(launch);
        static_cast<void>(lifecycle.StepFrame());
        const auto b = f.context->activity;
        REQUIRE(b != a);
        REQUIRE(f.context->activity_stack.size() == 2);
        CHECK(f.context->activity_stack.front().java_root == root);
        REQUIRE(f.context->activity_stack.front().projection);
        CHECK(f.context->activity_stack.front().projection->Get(root_node)->scroll_x == 3);
        CHECK(f.context->activity_stack.front().projection->Get(root_node)->scroll_y == 5);
        CHECK_FALSE(f.context->activity_stack.front().visible);
        CHECK_FALSE(f.context->active_surface_holders.contains(holder.Value()));
        CHECK(sequence() == 123);
        const auto current = AcquireAndroidPresentation(*f.context);
        REQUIRE(current);
        CHECK(current->activity == b);
        CHECK_FALSE(current->guest_window_epoch_refs.front()->IsAttached(a_node));
        CHECK(f.On(leaf, "hasWindowFocus", "()Z").AsInt() == 0);
        CHECK(f.On(leaf, "isFocused", "()Z").AsInt() == 1);
        f.On(b, "finish", "()V");
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity == a);
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        CHECK(f.context->content_view == leaf);
        CHECK(sequence() == 12312);
        CHECK(f.context->active_surface_holders.contains(holder.Value()));
        const auto restored_surface = f.On(holder, "getSurface", "()Landroid/view/Surface;").ref;
        CHECK(restored_surface != old_surface);
        CHECK(f.On(old_surface, "isValid", "()Z").AsInt() == 0);
        CHECK(f.On(restored_surface, "isValid", "()Z").AsInt() == 1);
        CHECK(f.On(leaf, "isFocused", "()Z").AsInt() == 1);
        CHECK(f.On(leaf, "hasWindowFocus", "()Z").AsInt() == 1);
        const auto restored = AcquireAndroidPresentation(*f.context);
        REQUIRE(restored);
        CHECK(restored->guest_window_epoch_refs.front()->Get(a_node)->measured == a_size);
        CHECK(restored->guest_window_epoch_refs.front()->Get(root_node)->scroll_x == 3);
        CHECK(restored->guest_window_epoch_refs.front()->Get(root_node)->scroll_y == 5);
        CHECK(saved->guest_window_epoch_refs.front()->Get(root_node)->scroll_x == 3);
        // A new window has no complete projection. Publication must retain A.
        const auto incomplete = f.New("Landroid/app/Activity;");
        f.context->activity_stack.push_back({incomplete});
        f.context->activity = incomplete;
        CHECK_FALSE(PublishAndroidPresentation(*f.context));
        CHECK(AcquireAndroidPresentation(*f.context) == restored);
        CHECK_THROWS_AS(AndroidInputEvent(f.vm, *f.context), VmJavaThrow);
        CHECK_FALSE(f.context->input_presentation);
        f.context->activity_stack.pop_back();
        f.context->activity = a;
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}

TEST_CASE("DVM-310 presentation freezes producer layers bitmap backing and non-owner exact pixels") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 8;
        f.context->surface_height = 6;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(view, "setClickable", "(Z)V", {VmValue::Int(1)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        const auto id = *FindViewUiNode(*f.context, view.Value());
        auto bitmap = std::make_shared<ui::UiBitmap>();
        bitmap->width = bitmap->height = 1;
        bitmap->rgba8 = {10, 20, 30, 255};
        f.context->ui_bitmaps[77] = bitmap;
        auto& producer = f.context->video_views[view.Value()];
        producer.latest_frame = ogplay::video::VideoFrame{0, 1, 1, {255, 0, 0, 255}};
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto lease = AcquireAndroidPresentation(*f.context);
        REQUIRE(lease);
        CHECK(lease->video_frames.at(id).rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255});
        CHECK(lease->guest_window_epoch_refs.front()->Get(id)->surface_has_frame);
        CHECK(lease->bitmaps.at(77) == bitmap);
        auto replacement = std::make_shared<ui::UiBitmap>(*bitmap);
        replacement->rgba8 = {40, 50, 60, 255};
        f.context->ui_bitmaps[77] = replacement;
        producer.latest_frame->rgba8 = {0, 255, 0, 255};
        f.context->ui_tree.Get(id)->surface_layer = ui::SurfaceLayer::on_top;
        f.context->ui_tree.MarkDrawDirty(id);
        const auto epoch = f.context->ui_measure_epoch;
        const auto trace = f.vm.Trace("", 10000).size();
        ogplay::runtime::AndroidBoundaryFrame frame{
            .width = 8, .height = 6, .rgba8 = std::vector<std::uint8_t>(8 * 6 * 4)};
        std::thread consumer(
            [&] { frame = ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(frame)); });
        consumer.join();
        for (std::size_t i = 0; i < frame.rgba8.size(); i += 4) {
            CHECK(frame.rgba8[i] == (((i / 4) % 8 >= 1 && (i / 4) % 8 < 7) ? 255 : 0));
            CHECK(frame.rgba8[i + 1] == 0);
            CHECK(frame.rgba8[i + 3] == 255);
            CHECK(lease->guest_window_epoch_refs.front()->HitTestProjection(static_cast<int>((i / 4) % 8),
                                                                            static_cast<int>((i / 4) / 8)) == id);
        }
        CHECK(f.vm.Trace("", 10000).size() == trace);
        CHECK(f.context->ui_measure_epoch == epoch);
        CHECK(AcquireAndroidPresentation(*f.context) == lease);
        CHECK(f.context->ui_tree.Get(id)->draw_dirty);
        CHECK_THROWS(static_cast<void>(ogplay::session::ComposeAndroidPresentationFrame(
            *f.context, {.width = 16, .height = 12, .rgba8 = std::vector<std::uint8_t>(16 * 12 * 4)})));
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        const auto next = AcquireAndroidPresentation(*f.context);
        REQUIRE(next != lease);
        CHECK(next->bitmaps.at(77) == replacement);
        CHECK(lease->bitmaps.at(77)->rgba8 == std::vector<std::uint8_t>{10, 20, 30, 255});
        CHECK(next->video_frames.at(id).rgba8 == std::vector<std::uint8_t>{0, 255, 0, 255});
        CHECK(next->guest_window_epoch_refs.front()->Get(id)->surface_layer == ui::SurfaceLayer::on_top);
        producer.latest_frame.reset();
        CHECK_FALSE(PublishAndroidPresentation(*f.context));
        CHECK(AcquireAndroidPresentation(*f.context) == next);
        RefreshAndroidSurfaceFrames(*f.context);
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        CHECK(AcquireAndroidPresentation(*f.context)->video_frames.empty());
        CHECK_FALSE(
            AcquireAndroidPresentation(*f.context)->guest_window_epoch_refs.front()->Get(id)->surface_has_frame);
        CHECK(lease->video_frames.at(id).rgba8.front() == 255);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-314 ROM window observers merge floating listeners and dispatch once per window") {
    using ogplay::test::container_traversal::ContainerVm;
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 100;
        f.context->surface_height = 80;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto listener = f.New("Ltest/GlobalLayoutCounter;");
        const auto other_listener = f.New("Ltest/GlobalLayoutCounter;");
        const auto observer = [&](VmObjectRef object) {
            return f.On(object, "getViewTreeObserver", "()Landroid/view/ViewTreeObserver;").ref;
        };
        const auto floating = observer(view), group_floating = observer(group);
        const auto roots =
            f.vm.ProtectReferences(std::array{owner, group, view, listener, other_listener, floating, group_floating});
        CHECK(observer(view) == floating);
        CHECK(f.On(floating, "isAlive", "()Z").AsInt() == 1);
        f.On(floating, "addOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
             {VmValue::Ref(listener)});
        f.On(group_floating, "addOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
             {VmValue::Ref(other_listener)});
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        CHECK(observer(view) == floating);  // detached hierarchy retains individual floating observers
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        CHECK(observer(group) == group_floating);  // setContentView is not window attachment.
        CHECK(observer(view) == floating);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveAndNotifySizes));
        const auto window = observer(group);
        CHECK(observer(view) == window);
        CHECK(window != floating);
        CHECK(f.On(floating, "isAlive", "()Z").AsInt() == 0);
        CHECK(f.On(group_floating, "isAlive", "()Z").AsInt() == 0);
        const auto dead =
            f.OnOutcome(floating, "addOnGlobalLayoutListener",
                        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V", {VmValue::Ref(listener)});
        REQUIRE(dead.exception.IsValid());
        CHECK(f.linker.Class(dead.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        const auto count = [&](VmObjectRef value) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(value), "calls", "I");
            REQUIRE(field);
            return f.model.InstanceSlots(value)[f.linker.Field(*field).slot].bits;
        };
        DispatchAndroidGlobalLayout(f.vm, *f.context);
        CHECK(count(listener) == 1);
        CHECK(count(other_listener) == 1);
        f.On(window, "removeOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
             {VmValue::Ref(listener)});
        DispatchAndroidGlobalLayout(f.vm, *f.context);
        CHECK(count(listener) == 1);
        CHECK(count(other_listener) == 2);
        f.On(view, "setSystemUiVisibility", "(I)V", {VmValue::Int(0x1706)});
        CHECK(f.On(view, "getSystemUiVisibility", "()I").AsInt() == 0x1706);
        const auto system_field = f.linker.FindFieldRecursive(f.model.ObjectClass(view), "mSystemUiVisibility", "I");
        REQUIRE(system_field);
        CHECK(f.model.InstanceSlots(view)[f.linker.Field(*system_field).slot].bits == 0x1706);
        f.On(group, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        const auto detached = observer(view);
        CHECK(detached != window);
        CHECK(f.On(detached, "isAlive", "()Z").AsInt() == 1);
        f.On(view, "setSystemUiVisibility", "(I)V", {VmValue::Int(7)});
        CHECK(f.On(view, "getSystemUiVisibility", "()I").AsInt() == 7);
        CHECK(f.ledger.Unimplemented().empty());
        const auto trace = f.vm.Trace("", 10000);
        CHECK(std::ranges::count_if(trace, [](const auto& e) {
                  return e.class_descriptor == "Landroid/view/ViewTreeObserver;" &&
                         e.method_name == "dispatchOnGlobalLayout";
              }) == 2);
    }
}

TEST_CASE("DVM-314 reviewed WebView construction retains explicit AbsoluteLayout measure boundary") {
    using ogplay::test::container_traversal::ContainerVm;
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Landroid/webkit/WebView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        CHECK(f.On(view, "getSettings", "()Landroid/webkit/WebSettings;").ref.IsValid());
        CHECK(f.ledger.Unimplemented().empty());
        const auto outcome =
            f.OnOutcome(view, "measure", "(II)V", {VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(outcome.exception_message.find("onMeasure") != std::string::npos);
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-315 NativeActivity style startup observer queries content and decor geometry") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 100;
        f.context->surface_height = 80;
        const auto owner = f.New("Ltest/NativeStartupActivity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.On(owner, "onCreate", "(Landroid/os/Bundle;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveAndNotifySizes));
        CHECK_NOTHROW(DispatchAndroidGlobalLayout(f.vm, *f.context));
        const auto value = [&](const char* name) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(owner), name, "I");
            REQUIRE(field);
            return f.model.InstanceSlots(owner)[f.linker.Field(*field).slot].bits;
        };
        // ROM keeps both explicit registrations even on the same observer.
        CHECK(value("calls") == 2);
        CHECK(value("width") == 100);
        CHECK(value("height") == 80);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-315 original NativeActivity startup executes ROM global layout on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend, false, true);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Landroid/app/NativeActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        CHECK_NOTHROW(static_cast<void>(lifecycle.Start()));
        CHECK(f.context->active_surface_holders.contains(f.context->window_surface_holder.Value()));
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}

TEST_CASE("DVM-315 startup guest callback exception becomes a lifecycle fault") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto type = f.linker.ResolveDescriptor("Ltest/NativeStartupActivity;");
        const auto index = f.linker.FindVtableIndex(type, "onGlobalLayout", "()V");
        REQUIRE(index);
        auto& method = f.linker.MutableMethod(f.linker.Class(type).vtable[*index]);
        method.code.reset();
        method.fast_code.reset();
        method.kind = MethodKind::intrinsic;
        method.implementation = [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "fixture startup callback"};
        };
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/NativeStartupActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        CHECK_THROWS_WITH_AS(
            static_cast<void>(lifecycle.Start()),
            "Activity startup failed: uncaught Java exception\n  guest boundary: Activity startup\n  exception: Ljava/lang/IllegalStateException;\n  message: fixture startup callback",
            ogplay::session::DexActivityLifecycleError);
        CHECK(lifecycle.State().state == ogplay::session::LifecycleRunState::failed);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-316 replaced startup content retains its floating observer until real attachment") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 100;
        f.context->surface_height = 80;
        const auto owner = f.New("Ltest/NativeStartupActivity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.On(owner, "onCreate", "(Landroid/os/Bundle;)V", {VmValue::Ref(VmObjectRef{})});
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(owner), "content", "Landroid/view/View;");
        REQUIRE(field);
        const auto removed = VmObjectRef{f.model.InstanceSlots(owner)[f.linker.Field(*field).slot].bits};
        const auto floating = f.On(removed, "getViewTreeObserver", "()Landroid/view/ViewTreeObserver;").ref;
        const auto replacement = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, removed, floating, replacement});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(replacement)});
        CHECK_NOTHROW(DispatchAndroidGlobalLayout(f.vm, *f.context));  // No traversal, no window observer yet.
        CHECK(f.ledger.Unimplemented().empty());
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveAndNotifySizes));
        // The decor registration remains live; isolate the removed content registration.
        const auto decor =
            f.On(f.On(owner, "getWindow", "()Landroid/view/Window;").ref, "getDecorView", "()Landroid/view/View;").ref;
        const auto window = f.On(decor, "getViewTreeObserver", "()Landroid/view/ViewTreeObserver;").ref;
        f.On(window, "removeOnGlobalLayoutListener", "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V",
             {VmValue::Ref(owner)});
        CHECK(floating != window);
        CHECK(f.On(floating, "isAlive", "()Z").AsInt() == 1);
        CHECK_NOTHROW(DispatchAndroidGlobalLayout(f.vm, *f.context));
        CHECK(f.ledger.Unimplemented().empty());
        const auto array = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 2);
        const auto failure = f.OnOutcome(removed, "getLocationInWindow", "([I)V", {VmValue::Ref(array)});
        REQUIRE(failure.exception.IsValid());
        CHECK(failure.exception_message == "Coordinates require managed attachment");
    }
}

TEST_CASE("DVM-316 lifecycle host guest boundary appends causes without changing the failure header") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto type = f.linker.ResolveDescriptor("Ltest/NativeStartupActivity;");
        const auto index = f.linker.FindVtableIndex(type, "onGlobalLayout", "()V");
        REQUIRE(index);
        auto& method = f.linker.MutableMethod(f.linker.Class(type).vtable[*index]);
        method.code.reset();
        method.fast_code.reset();
        method.kind = MethodKind::intrinsic;
        method.implementation = [](IntrinsicContext& call) -> VmValue {
            const auto inner = call.vm.MakeThrowable("Ljava/lang/IllegalArgumentException;", "constructor detail");
            const auto roots = call.vm.ProtectReferences(std::array{inner});
            const auto wrapper = call.vm.MakeThrowable("Ljava/lang/RuntimeException;", "wrapper");
            call.vm.InitThrowableCause(wrapper, inner);
            throw VmJavaThrow{"Ljava/lang/RuntimeException;", "wrapper", wrapper};
        };
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/NativeStartupActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        try {
            static_cast<void>(lifecycle.Start());
            FAIL("startup must fail");
        } catch (const ogplay::session::DexActivityLifecycleError& error) {
            const std::string text = error.what();
            CHECK(text.starts_with(
                "Activity startup failed: uncaught Java exception\n  guest boundary: Activity startup\n  exception: Ljava/lang/RuntimeException;\n  message: wrapper"));
            CHECK(text.find("\nCaused by: Ljava/lang/IllegalArgumentException;: constructor detail") !=
                  std::string::npos);
        }
        CHECK(lifecycle.State().state == ogplay::session::LifecycleRunState::failed);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-317 NativeActivity style empty content drains detached producer initialization before Surface") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        const auto owner = f.context->activity;
        const auto window = f.On(owner, "getWindow", "()Landroid/view/Window;").ref;
        // NativeActivity can relinquish the window Surface before attaching a
        // View producer. Its queueEvent initialization must still run on GLThread.
        f.On(window, "takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V", {VmValue::Ref(VmObjectRef{})});
        const auto content =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(content)});
        const auto view =
            f.New("Landroid/opengl/GLSurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto event = f.New("Ltest/OwnerMutation;");
        unsigned calls{};
        std::uint64_t event_token{};
        const auto method = f.linker.FindVtableIndex(f.model.ObjectClass(event), "run", "()V");
        REQUIRE(method);
        auto& run = f.linker.MutableMethod(f.linker.Class(f.model.ObjectClass(event)).vtable[*method]);
        run.code.reset();
        run.fast_code.reset();
        run.kind = MethodKind::intrinsic;
        run.implementation = [&](IntrinsicContext& call) {
            ++calls;
            event_token = call.vm.CurrentContextToken();
            return VmValue::Void();
        };
        // No render callback is legal before attachment, so the renderer identity
        // is never invoked here. The queue uses a real ROM Runnable dispatch.
        f.On(view, "setRenderer", "(Landroid/opengl/GLSurfaceView$Renderer;)V", {VmValue::Ref(event)});
        f.On(view, "queueEvent", "(Ljava/lang/Runnable;)V", {VmValue::Ref(event)});
        static_cast<void>(lifecycle.StepFrame());
        CHECK(calls == 1);
        CHECK(event_token != 0);
        CHECK(event_token != 1);
        const auto state = f.context->gl_surface_runtimes.at(view.Value());
        CHECK_FALSE(state->surface_available);
        CHECK_FALSE(state->renderer_surface.IsValid());
        CHECK_FALSE(f.context->active_surface_holders.contains(f.context->surface_holders.at(view.Value()).Value()));
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}

TEST_CASE("DVM-317 NativeActivity window producer composes continuous frames with a clean presentation lease") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend, false, true);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Landroid/app/NativeActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        REQUIRE(f.context->active_surface_holders.contains(f.context->window_surface_holder.Value()));
        std::uint64_t previous{};
        std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot> stable;
        for (unsigned i = 0; i != 4; ++i) {
            static_cast<void>(lifecycle.StepFrame());
            const auto lease = AcquireAndroidPresentation(*f.context);
            REQUIRE(lease);
            if (stable)
                CHECK(lease == stable);
            else
                stable = lease;
            const auto width = static_cast<std::uint32_t>(lease->viewport.width);
            const auto height = static_cast<std::uint32_t>(lease->viewport.height);
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4U, 255);
            pixels[0] = static_cast<std::uint8_t>(i + 1);
            // CPU producer seam after EGL readback/resolve; real EGL is an external gate.
            f.context->session->PublishSoftwareFrame(pixels);
            auto frame = f.context->session->TakeLatestFrame();
            REQUIRE(frame);
            CHECK(frame->sequence > previous);
            previous = frame->sequence;
            const auto trace = f.vm.Trace("", 10000).size();
            const auto composed = lifecycle.ComposePresentedFrame(std::move(*frame));
            CHECK(composed.sequence == previous);
            CHECK(composed.rgba8 == pixels);
            CHECK(AcquireAndroidPresentation(*f.context) == lease);
            CHECK(f.vm.Trace("", 10000).size() == trace);
            CHECK_THROWS(static_cast<void>(lifecycle.ComposePresentedFrame(
                {.width = width + 1,
                 .height = height,
                 .rgba8 = std::vector<std::uint8_t>(static_cast<std::size_t>(width + 1) * height * 4U)})));
        }
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
    }
}

TEST_CASE("DVM-318 DVM-321 late GLSurfaceView after NativeActivity takeSurface null starts renderer") {
    for (const int mount : {0, 1, 2})
        for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
            ogplay::test::container_traversal::ContainerVm f(backend, false, true);
            f.context->ui_test_accessibility_disabled = true;
            // CPU EGL seam: lifecycle executes the actual EGL Java calls on GLThread;
            // no host display/GPU is opened. Real context creation remains external.
            unsigned egl_requests{}, swaps{};
            const auto patch = [&](const char* name, const char* descriptor, IntrinsicHandler handler) {
                const auto type = f.linker.ResolveDescriptor("Ljavax/microedition/khronos/egl/EGL10$Impl;");
                const auto index = f.linker.FindVtableIndex(type, name, descriptor);
                REQUIRE(index);
                auto& method = f.linker.MutableMethod(f.linker.Class(type).vtable[*index]);
                method.code.reset();
                method.fast_code.reset();
                method.kind = MethodKind::intrinsic;
                method.implementation = std::move(handler);
            };
            const auto yes = [](IntrinsicContext&) { return VmValue::Int(1); };
            patch("eglGetDisplay", "(Ljava/lang/Object;)Ljavax/microedition/khronos/egl/EGLDisplay;",
                  [&](IntrinsicContext&) {
                      return VmValue::Ref(f.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLDisplay;"));
                  });
            patch("eglInitialize", "(Ljavax/microedition/khronos/egl/EGLDisplay;[I)Z", yes);
            patch("eglChooseConfig",
                  "(Ljavax/microedition/khronos/egl/EGLDisplay;[I[Ljavax/microedition/khronos/egl/EGLConfig;I[I)Z",
                  [&](IntrinsicContext& call) {
                      f.model.SetObjectElement(call.arguments[2].ref, 0,
                                               f.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLConfig;"));
                      f.model.SetPrimitiveElement(call.arguments[4].ref, 0, 1);
                      return VmValue::Int(1);
                  });
            patch(
                "eglCreateContext",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljavax/microedition/khronos/egl/EGLContext;[I)Ljavax/microedition/khronos/egl/EGLContext;",
                [&](IntrinsicContext&) {
                    ++egl_requests;
                    return VmValue::Ref(f.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLContext;"));
                });
            patch(
                "eglCreateWindowSurface",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLConfig;Ljava/lang/Object;[I)Ljavax/microedition/khronos/egl/EGLSurface;",
                [&](IntrinsicContext& call) {
                    CHECK(f.context->active_surface_holders.contains(call.arguments[2].ref.Value()));
                    CHECK(f.context->surface_callback_sizes.contains(call.arguments[2].ref.Value()));
                    return VmValue::Ref(f.vm.NewIntrinsicInstance("Ljavax/microedition/khronos/egl/EGLSurface;"));
                });
            patch(
                "eglMakeCurrent",
                "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLSurface;Ljavax/microedition/khronos/egl/EGLContext;)Z",
                yes);
            patch("eglSwapBuffers",
                  "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z",
                  [&](IntrinsicContext&) {
                      ++swaps;
                      f.context->session->PublishSoftwareFrame(
                          std::vector<std::uint8_t>(f.context->surface_width * f.context->surface_height * 4U, 255));
                      return VmValue::Int(1);
                  });
            patch("eglDestroySurface",
                  "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLSurface;)Z", yes);
            patch("eglDestroyContext",
                  "(Ljavax/microedition/khronos/egl/EGLDisplay;Ljavax/microedition/khronos/egl/EGLContext;)Z", yes);
            patch("eglTerminate", "(Ljavax/microedition/khronos/egl/EGLDisplay;)Z", yes);
            ogplay::session::DexActivityLifecycleBindings bindings;
            bindings.bridge = &f.app->DexVm();
            bindings.context = f.context;
            bindings.launcher_descriptor = "Ltest/LateGlActivity;";
            f.context->activity_components.front().name = "test.LateGlActivity";
            bindings.application_descriptor = "Landroid/app/Application;";
            const bool defer_mount = mount != 0;
            if (mount == 2) {
                bindings.launcher_descriptor = "Landroid/app/Activity;";
                const auto type = f.linker.ResolveDescriptor("Landroid/app/Activity;");
                const auto index = *f.linker.FindVtableIndex(type, "onCreate", "(Landroid/os/Bundle;)V");
                auto& method = f.linker.MutableMethod(f.linker.Class(type).vtable[index]);
                method.kind = MethodKind::intrinsic;
                method.code.reset();
                method.fast_code.reset();
                method.implementation = [&, type](IntrinsicContext& call) {
                    if (f.model.ObjectClass(call.receiver) != type) return VmValue::Void();
                    // A launcher retires before it ever materializes a Window root.
                    // Use the same command queue consumed by startActivity/finish.
                    DexVmAndroidContext::ActivityCommand launch;
                    launch.owner = call.receiver;
                    launch.intent = f.New("Landroid/content/Intent;");
                    launch.descriptor = "Ltest/LateGlActivity;";
                    launch.component = "test.LateGlActivity";
                    f.context->activity_commands.push_back(launch);
                    DexVmAndroidContext::ActivityCommand finish;
                    finish.kind = DexVmAndroidContext::ActivityCommand::Kind::finish;
                    finish.owner = call.receiver;
                    f.context->activity_commands.push_back(finish);
                    return VmValue::Void();
                };
            }
            f.Static("Ltest/LateGlActivity;", "deferMount", "(I)V", {VmValue::Int(defer_mount ? 1 : 0)});
            ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
            static_cast<void>(lifecycle.Start());
            REQUIRE(f.context->gl_surface_runtimes.size() == 1);
            const auto detached = f.context->gl_surface_runtimes.begin()->second;
            if (defer_mount) {
                static_cast<void>(lifecycle.StepFrame());
                CHECK(f.context->activity_stack.back().resumed);
                CHECK(f.context->window_has_focus.load());
                CHECK_FALSE(detached->surface_available);
                CHECK(egl_requests == 0);
                f.On(f.context->content_view, "addView", "(Landroid/view/View;)V", {VmValue::Ref(detached->view)});
            }
            for (unsigned i = 0; i != 3; ++i)
                static_cast<void>(lifecycle.StepFrame());
            REQUIRE(f.context->gl_surface_runtimes.size() == 1);
            const auto state = f.context->gl_surface_runtimes.begin()->second;
            const auto node = FindViewUiNode(*f.context, state->view.Value());
            REQUIRE(node);
            REQUIRE(f.context->ui_tree.PublishedProjection()->Get(*node));
            CAPTURE(mount);
            CAPTURE(static_cast<int>(backend));
            CHECK(f.context->ui_tree.Get(f.context->ui_tree.Root())->window_visible);
            CHECK(f.context->ui_tree.Get(*f.context->activity_stack.back().window_root)->window_visible);
            CHECK(f.context->ui_tree.Get(*node)->window_visible);
            CHECK(f.context->ui_tree.IsVisible(*node));
            CHECK(f.context->gl_surface_renderer_view == state->view);
            CHECK(state->surface_available);
            CHECK(egl_requests == 1);
            CHECK(swaps >= 1);
            const auto sequence =
                f.linker.FindFieldRecursive(f.linker.ResolveDescriptor("Ltest/LateGlActivity;"), "sequence", "I");
            REQUIRE(sequence);
            CHECK(f.linker.Class(f.linker.ResolveDescriptor("Ltest/LateGlActivity;"))
                      .static_storage[f.linker.Field(*sequence).slot] == 123);
            for (const auto name : {"init", "created", "changed", "drawn"}) {
                const auto field =
                    f.linker.FindFieldRecursive(f.linker.ResolveDescriptor("Ltest/LateGlActivity;"), name, "I");
                REQUIRE(field);
                CHECK(f.linker.Field(*field).is_static);
                const auto count = f.linker.Class(f.linker.ResolveDescriptor("Ltest/LateGlActivity;"))
                                       .static_storage[f.linker.Field(*field).slot];
                if (std::string_view(name) == "drawn")
                    CHECK(count >= 1);
                else
                    CHECK(count == 1);
            }
            if (swaps == 0) {
                static_cast<void>(lifecycle.Stop());
                continue;
            }
            CHECK(f.On(state->view, "getWindowVisibility", "()I").AsInt() == 0);
            auto frame = f.context->session->TakeLatestFrame();
            CHECK(frame.has_value());
            if (!frame) {
                static_cast<void>(lifecycle.Stop());
                continue;
            }
            const auto previous = frame->sequence;
            const auto presented = lifecycle.ComposePresentedFrame(std::move(*frame));
            CHECK(presented.sequence == previous);
            static_cast<void>(lifecycle.StepFrame());
            frame = f.context->session->TakeLatestFrame();
            REQUIRE(frame);
            CHECK(frame->sequence > previous);
            CHECK(lifecycle.ComposePresentedFrame(std::move(*frame)).sequence > presented.sequence);
            const auto records = f.logger.Snapshot(std::nullopt, "ui.surface.");
            for (const auto category : {"ui.surface.producer", "ui.surface.projection", "ui.surface.callback",
                                        "ui.surface.holder", "ui.surface.glthread", "ui.surface.egl"})
                CHECK(std::ranges::any_of(records, [&](const auto& record) {
                    return record.category == category && !record.fields.empty();
                }));
            static_cast<void>(lifecycle.Suspend());
            CHECK_FALSE(f.context->activity_stack.back().visible);
            CHECK(f.On(state->view, "getWindowVisibility", "()I").AsInt() == 4);
            CHECK_FALSE(f.context->gl_surface_renderer_view.IsValid());
            CHECK_FALSE(f.context->ui_tree.PublishedProjection()->IsVisible(*node));
            CHECK_FALSE(
                f.context->active_surface_holders.contains(f.context->surface_holders.at(state->view.Value()).Value()));
            const auto suspended_swaps = swaps;
            static_cast<void>(lifecycle.StepFrame());
            CHECK(swaps == suspended_swaps);
            static_cast<void>(lifecycle.Resume());
            static_cast<void>(lifecycle.StepFrame());
            CHECK(f.context->gl_surface_renderer_view == state->view);
            CHECK(f.context->ui_tree.PublishedProjection()->IsVisible(*node));
            CHECK(swaps > suspended_swaps);
            CHECK(f.ledger.Unimplemented().empty());
            static_cast<void>(lifecycle.Stop());
            CHECK_FALSE(f.context->gl_surface_renderer_view.IsValid());
            CHECK_FALSE(f.context->renderer.IsValid());
            CHECK(f.context->active_surface_holders.empty());
            CHECK(f.ledger.Unimplemented().empty());
        }
}


TEST_CASE("DVM-319 owner projection selects late producer before its first Surface callback") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        const auto owner = f.context->activity;
        const auto window = f.On(owner, "getWindow", "()Landroid/view/Window;").ref;
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        f.On(window, "takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        const auto content =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(content)});
        const auto view =
            f.New("Landroid/opengl/GLSurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto renderer = f.New("Ltest/OwnerMutation;");
        f.On(view, "setRenderer", "(Landroid/opengl/GLSurfaceView$Renderer;)V", {VmValue::Ref(renderer)});
        unsigned created{};
        const auto callback = f.New("Ltest/ManagedSurfaceCallback;");
        const auto holder = f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        f.On(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        // Stop at the real holder boundary before any EGL/GPU request.
        OwnerHook(f, callback, "surfaceCreated", "(Landroid/view/SurfaceHolder;)V", [&](IntrinsicContext&) -> VmValue {
            ++created;
            CHECK(f.context->gl_surface_renderer_view == view);
            CHECK(f.context->renderer == renderer);
            CHECK(f.context->ui_measure_epoch > 0);
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "fixture stop before EGL"};
        });
        static_cast<void>(lifecycle.StepFrame());
        CHECK_FALSE(f.context->gl_surface_renderer_view.IsValid());
        f.On(content, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        CHECK_THROWS_AS(static_cast<void>(lifecycle.StepFrame()), ogplay::session::DexActivityLifecycleError);
        CHECK(created == 1);
        CHECK_FALSE(f.context->gl_surface_runtimes.at(view.Value())->surface_available);
        CHECK(f.ledger.Unimplemented().empty());
        static_cast<void>(lifecycle.Stop());
        CHECK(f.logger.Snapshot(std::nullopt, "ui.diagnostics.summary").size() == 1);
        CHECK(f.logger.Snapshot(std::nullopt, "ui.diagnostics.producer").size() == 1);
    }
}

TEST_CASE("DVM-319 traversal reasons are per window first occurrence with retained counters") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->surface_width = 80;
        f.context->surface_height = 50;
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        REQUIRE(tick());
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        const auto epoch = f.context->ui_measure_epoch;
        CHECK_FALSE(tick());
        CHECK_FALSE(tick());
        CHECK(f.context->ui_measure_epoch == epoch);
        auto& counts = f.context->ui_traversal_diagnostics[root];
        CHECK(counts.at("clean snapshot") == 2);
        CHECK(counts.at("projection committed") == 1);
        {
            AndroidInputEvent input(f.vm, *f.context);
            CHECK_FALSE(tick());
            CHECK_FALSE(tick());
            auto target = *f.context->ui_tree.ProjectionTarget();
            CommitAndroidMeasuredRootProjection(f.vm, *f.context, root, target, epoch + 1);
        }
        CHECK(counts.at("input lease barrier") == 2);
        CHECK(counts.at("commit input lease") == 1);
        f.context->surface_event_dispatching = true;
        CHECK_FALSE(tick());
        f.context->surface_event_dispatching = false;
        auto& schedule = f.context->ui_window_schedules[root];
        schedule.deferred_until_tick = f.context->ui_owner_tick + 2;
        CHECK_FALSE(tick());
        CHECK(counts.at("deferred next tick") == 1);
        CHECK_FALSE(tick());
        CHECK(f.context->ui_measure_epoch == epoch + 1);  // pending draw publishes after lease release
        auto stale = *f.context->ui_tree.ProjectionTarget();
        ++stale.content_revision;
        CHECK_THROWS_AS(CommitAndroidMeasuredRootProjection(f.vm, *f.context, root, stale, epoch + 2), VmJavaThrow);
        CHECK(counts.at("commit stale identity") == 1);
        const auto records = f.logger.Snapshot(std::nullopt, "ui.traversal");
        for (const auto why : {"clean snapshot", "projection committed", "input lease barrier", "commit input lease",
                               "surface callback barrier", "deferred next tick", "commit stale identity"}) {
            CHECK(std::ranges::count_if(records, [&](const auto& record) {
                      return record.message == std::string(why) + " root=" + std::to_string(root.Value()) &&
                             !record.fields.empty();
                  }) == 1);
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-320 producer facts change after projection and summarize once before normal or failed teardown") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded})
        for (const bool failed : {false, true}) {
            ogplay::test::container_traversal::ContainerVm f(backend);
            f.context->ui_test_accessibility_disabled = true;
            ogplay::session::DexActivityLifecycleBindings bindings;
            bindings.bridge = &f.app->DexVm();
            bindings.context = f.context;
            bindings.launcher_descriptor = "Ltest/OwnerActivity;";
            bindings.application_descriptor = "Landroid/app/Application;";
            bindings.before_guest_shutdown = [&] {
                CHECK(f.logger.Snapshot(std::nullopt, "ui.diagnostics.summary").size() == 1);
            };
            ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
            static_cast<void>(lifecycle.Start());
            const auto owner = f.context->activity;
            const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
            const auto container =
                f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto view =
                f.New("Landroid/opengl/GLSurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            f.On(view, "setRenderer", "(Landroid/opengl/GLSurfaceView$Renderer;)V", {VmValue::Ref(owner)});
            // Registered while detached: actual Java parent is null, despite a UiNode binding.
            const auto detached = f.context->ui_producer_diagnostics.at(view.Value());
            CHECK(detached.find("bound=1 in_projection=0") != std::string::npos);
            CHECK(detached.find("parent_null=1") != std::string::npos);
            CHECK(detached.find("first_break=" + std::to_string(view.Value())) != std::string::npos);
            f.On(container, "addView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
            f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(container)});
            f.On(container, "setVisibility", "(I)V", {VmValue::Int(4)});
            static_cast<void>(lifecycle.StepFrame());
            const auto epoch = f.context->ui_measure_epoch;
            RecordAndroidProducerDiagnostic(f.vm, *f.context, view, "outside visible projection");
            const auto hidden = f.context->ui_producer_diagnostics.at(view.Value());
            CHECK(hidden.find("in_projection=1") != std::string::npos);
            CHECK(hidden.find("class=Landroid/opengl/GLSurfaceView;") != std::string::npos);
            CHECK(hidden.find("visibility=4") != std::string::npos);
            CHECK(hidden.find("first_break=" + std::to_string(container.Value())) != std::string::npos);
            CHECK(hidden.find("termination=root") != std::string::npos);
            CHECK(hidden.find("window_activity=" + std::to_string(owner.Value())) != std::string::npos);
            const auto count = f.logger.Snapshot(std::nullopt, "ui.surface.producer_state").size();
            RecordAndroidProducerDiagnostic(f.vm, *f.context, view, "outside visible projection");
            CHECK(f.logger.Snapshot(std::nullopt, "ui.surface.producer_state").size() == count);
            // Changed Java visibility is a new fact even with the same reason and epoch.
            f.On(container, "setVisibility", "(I)V", {VmValue::Int(8)});
            RecordAndroidProducerDiagnostic(f.vm, *f.context, view, "outside visible projection");
            CHECK(f.logger.Snapshot(std::nullopt, "ui.surface.producer_state").size() == count + 1);
            CHECK(f.context->ui_measure_epoch == epoch);
            CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
            RecordAndroidTraversalDiagnostic(f.vm, *f.context, root, "fixture repeated reason");
            RecordAndroidTraversalDiagnostic(f.vm, *f.context, root, "fixture repeated reason");
            CHECK(f.context->ui_diagnostic_totals.at("ui.traversal: fixture repeated reason").count == 2);
            CHECK(f.context->ui_diagnostic_totals.at("ui.surface.producer: renderer registered").count == 1);
            static_cast<void>(lifecycle.Stop(failed ? ogplay::session::DexProcessStopReason::runtime_failure
                                                    : ogplay::session::DexProcessStopReason::host_shutdown));
            static_cast<void>(lifecycle.Stop());
            SummarizeAndroidUiDiagnostics(f.vm, *f.context);
            CHECK(f.logger.Snapshot(std::nullopt, "ui.diagnostics.summary").size() == 1);
            const auto summaries = f.logger.Snapshot(std::nullopt, "ui.diagnostics.producer");
            REQUIRE(summaries.size() == 1);
            bool final_gone{}, final_epoch{};
            for (const auto& field : summaries.front().fields) {
                if (field.key == "state")
                    final_gone = std::get<std::string>(field.value).find("visibility=8") != std::string::npos;
                if (field.key == "epoch") final_epoch = std::get<std::uint64_t>(field.value) == epoch;
            }
            CHECK(final_gone);
            CHECK(final_epoch);
            const auto reasons = f.logger.Snapshot(std::nullopt, "ui.diagnostics.reason");
            const auto repeated = std::ranges::find_if(
                reasons, [](const auto& record) { return record.message == "ui.traversal: fixture repeated reason"; });
            REQUIRE(repeated != reasons.end());
            CHECK(std::get<std::uint64_t>(repeated->fields.front().value) == 2);
            CHECK(f.ledger.Unimplemented().empty());
        }
}

TEST_CASE("DVM-329 ROM bringToFront publishes child paint order") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->surface_width = 80;
        f.context->surface_height = 50;
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        const auto first = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto second = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.context->ui_tree.Get(*FindViewUiNode(*f.context, first.Value()))->background_color = 0xff0000ffU;
        f.context->ui_tree.Get(*FindViewUiNode(*f.context, second.Value()))->background_color = 0x0000ffffU;
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        REQUIRE(tick());
        auto before = f.context->ui_tree.PublishedProjection();
        CHECK(ui::RasterizeUiOverlay(ui::BuildUiRenderList(*before, {}), {80, 50}).rgba8[2] == 255);
        const auto node = *FindViewUiNode(*f.context, group.Value());
        const auto a = *FindViewUiNode(*f.context, first.Value());
        const auto b = *FindViewUiNode(*f.context, second.Value());
        CHECK(before->Get(node)->children == std::vector<ui::UiNodeId>{a, b});
        f.On(first, "bringToFront", "()V");
        CHECK(f.On(group, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(1)}).ref == first);
        CHECK(f.context->ui_tree.PublishedProjection() == before);
        REQUIRE(tick());
        auto after = f.context->ui_tree.PublishedProjection();
        CHECK(after->Get(node)->children == std::vector<ui::UiNodeId>{b, a});
        const auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*after, {}), {80, 50});
        CHECK(pixels.rgba8[0] == 255);
        CHECK(pixels.rgba8[2] == 0);
        CHECK(before->Get(node)->children == std::vector<ui::UiNodeId>{a, b});
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-329 producer binding outside the retained projection waits for publication") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Ltest/OwnerActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        const auto old = f.context->ui_tree.PublishedProjection();
        REQUIRE(old);
        const auto view = f.New("Landroid/opengl/GLSurfaceView;", "(Landroid/content/Context;)V",
                                {VmValue::Ref(f.context->activity)});
        const auto renderer = f.New("Ltest/OwnerMutation;");
        f.On(view, "setRenderer", "(Landroid/opengl/GLSurfaceView$Renderer;)V", {VmValue::Ref(renderer)});
        const auto binding = *FindViewUiNode(*f.context, view.Value());
        CHECK(old->Get(binding) == nullptr);
        // Suspend republishes only window facts from the old complete epoch,
        // then reevaluates all registered producers, including this new binding.
        CHECK_NOTHROW(static_cast<void>(lifecycle.Suspend()));
        CHECK_FALSE(f.context->gl_surface_renderer_view.IsValid());
        CHECK(f.context->ui_tree.PublishedProjection()->Get(binding) == nullptr);
        CHECK(f.context->ui_tree.Get(binding) != nullptr);
        CHECK(f.context->ui_tree.PublishedProjection()->ProjectionEpoch() > old->ProjectionEpoch());
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        f.On(root, "requestLayout", "()V");
        CHECK_NOTHROW(static_cast<void>(lifecycle.Resume()));
        CHECK(f.context->ui_tree.PublishedProjection()->Get(binding) != nullptr);
        CHECK(old->Get(binding) == nullptr);
        CHECK_FALSE(f.context->gl_surface_renderer_view.IsValid());
        CHECK_NOTHROW(static_cast<void>(lifecycle.Stop()));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-329 clean owner ticks never enter traversal and Java requests resume it") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        REQUIRE(tick());
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        const auto before = f.context->ui_tree.PublishedProjection();
        const auto entered = [&] { return f.context->ui_traversal_diagnostics[root]["traversal entered"]; };
        const auto count = entered();
        const auto trace_before = f.vm.Trace("", 10000);
        REQUIRE_FALSE(trace_before.empty());
        for (int i = 0; i < 100; ++i) {
            CHECK_FALSE(tick());
            for (int j = 0; j < 5; ++j)
                CHECK_FALSE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                     AndroidUiTraversalPhase::ResolveOnly));
        }
        CHECK(entered() == count);
        CHECK(f.vm.Trace("", 10000).back().sequence == trace_before.back().sequence);
        CHECK(f.context->ui_tree.PublishedProjection() == before);
        f.On(root, "invalidate", "()V");
        static_cast<void>(tick());
        CHECK(entered() == count + 1);
        CHECK(f.context->ui_tree.PublishedProjection() != before);
        const auto invalidated = f.context->ui_tree.PublishedProjection();
        f.On(root, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(entered() == count + 2);
        CHECK(f.context->ui_tree.PublishedProjection() != invalidated);
        f.context->surface_width = 960;
        REQUIRE(tick());
        CHECK(entered() == count + 3);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-329 headless clean frame scheduling benchmark") {
    const auto run = [](auto& f, const char* mode, const char* workload, const ui::Size size) {
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = static_cast<std::uint32_t>(size.width);
        f.context->surface_height = static_cast<std::uint32_t>(size.height);
        const auto content =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(content)});
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        f.context->ui_tree.ClearDrawDirty();
        std::vector<double> times;
        const auto counts = [&] {
            std::uint64_t count{};
            for (const auto& [root, values] : f.context->ui_traversal_diagnostics) {
                static_cast<void>(root);
                if (const auto it = values.find("traversal entered"); it != values.end()) count += it->second;
            }
            return count;
        };
        const auto before = counts();
        for (int sample = 0; sample < 21; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            for (int tick = 0; tick < 100; ++tick) {
                static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                           AndroidUiTraversalPhase::ResolveOnly));
                for (int barrier = 0; barrier < 5; ++barrier)
                    static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context,
                                                               AndroidUiTraversalReason::BeforePresent,
                                                               AndroidUiTraversalPhase::ResolveOnly));
            }
            times.push_back(
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 100.0);
        }
        std::sort(times.begin(), times.end());
        MESSAGE("DVM329 benchmark mode=" << std::string(mode) << " workload=" << std::string(workload)
                                         << " median_us=" << times[times.size() / 2]
                                         << " entries_per_tick=" << static_cast<double>(counts() - before) / 2100.0);
        CHECK(f.ledger.Unimplemented().empty());
    };
    for (const auto size : {ui::Size{800, 480}, ui::Size{1920, 1080}}) {
        const auto* workload = size.width == 800 ? "tml-window" : "tales-window";

        ogplay::test::container_traversal::ContainerVm java(InterpreterBackend::switch_dispatch);
        run(java, "C", workload, size);
    }
}

TEST_CASE("DVM-330 headless step and present benchmark") {
    const auto run = [](auto& f, const char* mode, ui::Size size) {
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = static_cast<std::uint32_t>(size.width);
        f.context->surface_height = static_cast<std::uint32_t>(size.height);
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        const auto view = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        REQUIRE(PublishAndroidPresentation(*f.context));
        ogplay::session::AndroidPresentationCache cache;
        AndroidBoundaryFrame frame;
        frame.width = static_cast<std::uint32_t>(size.width);
        frame.height = static_cast<std::uint32_t>(size.height);
        frame.rgba8.assign(static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4U, 0);
        std::vector<double> steps, presents;
        for (int batch = 0; batch < 15; ++batch) {
            std::uint64_t step_ns{}, present_ns{};
            for (int tick = 0; tick < 50; ++tick) {
                const auto start = ogplay::hal::Clock::SteadyTimestampNs();
                static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                           AndroidUiTraversalPhase::ResolveOnly));
                for (int barrier = 0; barrier < 5; ++barrier)
                    static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context,
                                                               AndroidUiTraversalReason::BeforePresent,
                                                               AndroidUiTraversalPhase::ResolveOnly));
                const auto middle = ogplay::hal::Clock::SteadyTimestampNs();
                frame = ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(frame), &cache);
                const auto end = ogplay::hal::Clock::SteadyTimestampNs();
                step_ns += middle - start;
                present_ns += end - middle;
            }
            steps.push_back(static_cast<double>(step_ns) / 50000.0);
            presents.push_back(static_cast<double>(present_ns) / 50000.0);
        }
        std::sort(steps.begin(), steps.end());
        std::sort(presents.begin(), presents.end());
        MESSAGE("DVM330 benchmark mode=" << std::string(mode) << " width=" << size.width << " step_us=" << steps[7]
                                         << " present_us=" << presents[7]);
        CHECK(f.ledger.Unimplemented().empty());
    };
    for (const auto size : {ui::Size{800, 480}, ui::Size{1920, 1080}}) {
        ogplay::test::container_traversal::ContainerVm java(InterpreterBackend::switch_dispatch);
        run(java, "C", size);
    }
}

TEST_CASE("DVM-330 ROM removeViewInLayout publishes remaining child order") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->surface_width = 80;
        f.context->surface_height = 50;
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        const auto first = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto second = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.context->ui_tree.Get(*FindViewUiNode(*f.context, first.Value()))->background_color = 0xff0000ffU;
        f.context->ui_tree.Get(*FindViewUiNode(*f.context, second.Value()))->background_color = 0x0000ffffU;
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(first)});
        f.On(group, "addView", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        REQUIRE(tick());
        auto before = f.context->ui_tree.PublishedProjection();
        CHECK(ui::RasterizeUiOverlay(ui::BuildUiRenderList(*before, {}), {80, 50}).rgba8[2] == 255);
        const auto node = *FindViewUiNode(*f.context, group.Value());
        const auto a = *FindViewUiNode(*f.context, first.Value());
        const auto b = *FindViewUiNode(*f.context, second.Value());
        CHECK(before->Get(node)->children == std::vector<ui::UiNodeId>{a, b});
        f.On(group, "removeViewInLayout", "(Landroid/view/View;)V", {VmValue::Ref(second)});
        CHECK(f.On(second, "getParent", "()Landroid/view/ViewParent;").ref == VmObjectRef{});
        CHECK(f.On(group, "getChildCount", "()I").AsInt() == 1);
        CHECK(f.On(group, "getChildAt", "(I)Landroid/view/View;", {VmValue::Int(0)}).ref == first);
        CHECK(f.context->ui_tree.PublishedProjection() == before);
        // In-layout removal deliberately does not request layout. The caller's
        // genuine invalidation publishes the changed Java child array.
        f.On(group, "invalidate", "()V");
        static_cast<void>(tick());
        auto after = f.context->ui_tree.PublishedProjection();
        CHECK(after->Get(node)->children == std::vector<ui::UiNodeId>{a});
        CHECK_FALSE(after->IsAttached(b));
        CHECK(before->IsAttached(b));
        const auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*after, {}), {80, 50});
        CHECK(pixels.rgba8[0] == 255);
        CHECK(pixels.rgba8[2] == 0);
        CHECK(before->Get(node)->children == std::vector<ui::UiNodeId>{a, b});
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-330 presentation cache follows immutable planes and producer frames") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        f.context->surface_width = 80;
        f.context->surface_height = 50;
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        const auto tick = [&] {
            static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                       AndroidUiTraversalPhase::ResolveOnly));
            REQUIRE(PublishAndroidPresentation(*f.context));
        };
        tick();
        ogplay::session::AndroidPresentationCache cache;
        const auto present = [&](std::uint8_t base) {
            AndroidBoundaryFrame frame;
            frame.width = f.context->surface_width;
            frame.height = f.context->surface_height;
            frame.rgba8.assign(static_cast<std::size_t>(frame.width) * frame.height * 4U, base);
            return ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(frame), &cache);
        };
        CHECK(present(17).rgba8[0] == 17);
        REQUIRE(cache.normal.size() == 1);
        const auto empty = cache.normal.front();
        for (int i = 0; i < 10; ++i)
            CHECK(present(29).rgba8[0] == 29);
        CHECK(cache.normal.front() == empty);
        const auto old = AcquireAndroidPresentation(*f.context);
        auto producer = std::make_shared<DexVmAndroidContext::PresentationSnapshot>(*old);
        ++producer->presentation_epoch;
        std::atomic_store(&f.context->presentation_snapshot,
                          std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot>{producer});
        CHECK(present(31).rgba8[0] == 31);
        CHECK(cache.snapshot == producer);
        CHECK(cache.normal.front() == empty);
        const auto node = *FindViewUiNode(*f.context, view.Value());
        f.context->ui_tree.Get(node)->background_color = 0xff0000ffU;
        f.On(view, "invalidate", "()V");
        tick();
        CHECK(present(0).rgba8[0] == 255);
        CHECK(cache.normal.front() != empty);
        const auto red = cache.normal.front();
        // A newly published modal plane leaves the guest raster reusable.
        auto next =
            std::make_shared<DexVmAndroidContext::PresentationSnapshot>(*AcquireAndroidPresentation(*f.context));
        DexVmAndroidContext::PresentationSnapshot::ModalSnapshot plane;
        plane.tree = next->guest_window_epoch_refs.front();
        next->modal_stack.push_back(plane);
        ++next->presentation_epoch;
        std::atomic_store(&f.context->presentation_snapshot,
                          std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot>{next});
        CHECK(present(0).rgba8[0] == 255);
        CHECK(cache.normal.front() == red);
        REQUIRE(cache.modal.size() == 1);
        const auto complete = cache.snapshot;
        next = std::make_shared<DexVmAndroidContext::PresentationSnapshot>(*next);
        next->viewport.width = 81;
        std::atomic_store(&f.context->presentation_snapshot,
                          std::shared_ptr<const DexVmAndroidContext::PresentationSnapshot>{next});
        CHECK_THROWS(present(0));
        CHECK(cache.snapshot == complete);
        CHECK(cache.normal.front() == red);
        // Restoring the real owner publication removes the modal plane.
        tick();
        CHECK(present(0).rgba8[0] == 255);
        CHECK(cache.modal.empty());
        CHECK(cache.normal.front() == red);
        const auto bitmap = std::make_shared<ui::UiBitmap>();
        bitmap->width = bitmap->height = 1;
        bitmap->rgba8 = {0, 255, 0, 255};
        f.context->ui_bitmaps[123] = bitmap;
        REQUIRE(PublishAndroidPresentation(*f.context));
        CHECK(present(0).rgba8[0] == 255);
        CHECK(cache.normal.front() != red);
        const auto with_bitmap = cache.normal.front();
        f.context->surface_width = 90;
        tick();
        CHECK(present(0).rgba8.size() == 90U * 50U * 4U);
        CHECK(cache.normal.front() != with_bitmap);
        CHECK(cache.snapshot->viewport.width == 90);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-331 window focus reaches the Java window tree across Activity switches without graphics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::native_library::OrchestratedApp f("fixture.WindowFocusActivity", true, false, {}, {}, true,
                                                        backend);
        ogplay::session::DexActivityLifecycleBindings bindings;
        bindings.bridge = &f.app->DexVm();
        bindings.context = f.context;
        bindings.launcher_descriptor = "Lfixture/WindowFocusActivity;";
        bindings.application_descriptor = "Landroid/app/Application;";
        ogplay::session::DexActivityLifecycle lifecycle(std::move(bindings), 1, 60);
        static_cast<void>(lifecycle.Start());
        const auto parent = f.context->activity;
        const auto events = [&] { return f.CallStaticInt("Lfixture/WindowFocusView;", "getEvents"); };
        CHECK(events() == 0);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(events() == 1);
        static_cast<void>(lifecycle.Suspend());
        CHECK(events() == 2);
        static_cast<void>(lifecycle.Resume());
        CHECK(events() == 3);
        const auto switched = f.app->DexVm().Linker().FindVtableIndex(f.app->DexVm().Vm().Model().ObjectClass(parent),
                                                                      "switchActivity", "()V");
        REQUIRE(switched);
        const auto target =
            f.app->DexVm().Linker().Class(f.app->DexVm().Vm().Model().ObjectClass(parent)).vtable[*switched];
        const auto outcome = f.app->DexVm().Vm().Call(target, std::array{VmValue::Ref(parent)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        static_cast<void>(lifecycle.StepFrame());
        CHECK(f.context->activity != parent);
        CHECK(events() == 5);
        static_cast<void>(lifecycle.Stop());
        CHECK(events() == 6);
        CHECK(f.ledger.Unimplemented().empty());
    }
}
