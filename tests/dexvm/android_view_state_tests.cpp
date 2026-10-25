#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-281 raw host allocation bypasses failure constructors and class initialization") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", true, AndroidUiTraversalOwner::JavaTraversal,
                         OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        for (const auto descriptor : {"Landroid/view/ViewRootImpl;", "Landroid/view/ViewOverlay$OverlayViewGroup;"}) {
            const auto type = f.linker.ResolveDescriptor(descriptor);
            const auto signature = std::string_view(descriptor) == "Landroid/view/ViewRootImpl;"
                                       ? "(Landroid/content/Context;Landroid/view/Display;)V"
                                       : "(Landroid/content/Context;Landroid/view/View;)V";
            const auto constructor = f.linker.FindDirectMethod(type, "<init>", signature);
            REQUIRE(constructor.has_value());
            REQUIRE(f.linker.Method(*constructor).kind != MethodKind::interpreted);
            REQUIRE_FALSE(f.linker.Method(*constructor).implementation);
            // Existing by-name host allocation performs neither clinit nor
            // construction; failure constructors alone cannot prove absence.
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            const auto roots = f.vm.ProtectReferences(std::array{object});
            REQUIRE(object.IsValid());
            CHECK(f.model.ObjectClass(object) == type);
            const auto rejected = f.vm.Call(*constructor, std::array{VmValue::Ref(object), VmValue::Ref(VmObjectRef{}),
                                                                     VmValue::Ref(VmObjectRef{})});
            CHECK(rejected.exception.IsValid());
            CHECK(f.model.ObjectClass(object) == type);
        }
    }
}

TEST_CASE("DVM-279 ROM measure specs state cache and guest overrides use Java authority") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto sdk : {17U, 18U, 19U}) {
            AndroidValueVm f(backend, {}, false, "view_measure.dex", false, AndroidUiTraversalOwner::JavaTraversal,
                             OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
            f.context->target_sdk_version = sdk;
            const auto owner = f.New("Landroid/app/Activity;");
            const auto make = [&](const char* type) {
                return f.New(type, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            };
            const auto measure = [&](const VmObjectRef view, const std::int32_t mode, const std::int32_t width = 80,
                                     const std::int32_t height = 50) {
                return f.OnOutcome(view, "measure", "(II)V", {VmValue::Int(mode | width), VmValue::Int(mode | height)});
            };
            const auto get = [&](const VmObjectRef view, const char* name) { return f.On(view, name, "()I").AsInt(); };
            const auto base = make("Landroid/view/View;");
            f.On(base, "setMinimumWidth", "(I)V", {VmValue::Int(7)});
            f.On(base, "setMinimumHeight", "(I)V", {VmValue::Int(9)});
            f.On(base, "setPadding", "(IIII)V", {VmValue::Int(3), VmValue::Int(4), VmValue::Int(5), VmValue::Int(6)});
            const auto insets = f.On(base, "getOpticalInsets", "()Landroid/graphics/Insets;").ref;
            REQUIRE(insets.IsValid());
            CHECK(f.On(base, "getOpticalInsets", "()Landroid/graphics/Insets;").ref == insets);
            for (const auto mode : {0, 0x40000000, static_cast<int>(0x80000000U)}) {
                const auto result = measure(base, mode);
                REQUIRE_FALSE_MESSAGE(result.exception.IsValid(), result.exception_message);
                CHECK(get(base, "getMeasuredWidth") == (mode == 0 ? 7 : 80));
                CHECK(get(base, "getMeasuredHeight") == (mode == 0 ? 9 : 50));
                CHECK(get(base, "getMeasuredState") == 0);
                CHECK(get(base, "getPaddingLeft") == 3);
                CHECK(get(base, "getPaddingRight") == 5);
            }
            const auto leaf = make("Ltest/MeasuredLeaf;");
            const auto node = *FindViewUiNode(*f.context, leaf.Value());
            // A contradictory host value proves that neither ROM measurement
            // nor its getters consult the Legacy layout result.
            f.context->ui_tree.Get(node)->measured = {333, 444};
            for (const auto mode : {0, 0x40000000, static_cast<int>(0x80000000U)}) {
                const auto result = measure(leaf, mode);
                REQUIRE_FALSE_MESSAGE(result.exception.IsValid(), result.exception_message);
                const auto small = mode == static_cast<int>(0x80000000U);
                CHECK(get(leaf, "getMeasuredWidth") == (mode == 0 ? 120 : 80));
                CHECK(get(leaf, "getMeasuredHeight") == (mode == 0 ? 90 : 50));
                CHECK(get(leaf, "getMeasuredWidthAndState") == ((mode == 0 ? 120 : 80) | (small ? 0x01000000 : 0)));
                CHECK(get(leaf, "getMeasuredHeightAndState") == ((mode == 0 ? 90 : 50) | (small ? 0x01000000 : 0)));
                CHECK(get(leaf, "getMeasuredState") == (small ? 0x01000100 : 0));
            }
            CHECK(f.context->ui_tree.Get(node)->measured == ui::Size{333, 444});
            const auto cached = make("Ltest/MeasuredLeaf;");
            REQUIRE_FALSE(measure(cached, static_cast<int>(0x80000000U)).exception.IsValid());
            REQUIRE_FALSE(measure(cached, static_cast<int>(0x80000000U), 100, 70).exception.IsValid());
            REQUIRE_FALSE(measure(cached, static_cast<int>(0x80000000U)).exception.IsValid());
            CHECK(get(cached, "count") == (sdk < 19 ? 3 : 2));
            CHECK(get(cached, "getMeasuredWidth") == 80);
            CHECK(get(cached, "getMeasuredWidthAndState") == 0x01000050);
            CHECK(get(cached, "getMeasuredState") == 0x01000100);
            REQUIRE_FALSE(measure(cached, static_cast<int>(0x80000000U)).exception.IsValid());
            CHECK(get(cached, "count") == (sdk < 19 ? 3 : 2));
            const auto before_layout_measure = get(cached, "count");
            f.On(cached, "layout", "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(80), VmValue::Int(50)});
            CHECK(get(cached, "count") == before_layout_measure + (sdk >= 19 ? 1 : 0));
            const auto broken = f.Static("Landroid/view/View$MeasureSpec;", "makeMeasureSpec", "(II)I",
                                         {VmValue::Int(0x40000005), VmValue::Int(0x40000000)})
                                    .AsInt();
            CHECK(broken == (sdk <= 17 ? static_cast<int>(0x80000005U) : 0x40000005));
            CHECK(f.Static("Landroid/view/View$MeasureSpec;", "getSize", "(I)I", {VmValue::Int(broken)}).AsInt() == 5);
            CHECK(f.Static("Landroid/view/View$MeasureSpec;", "getMode", "(I)I", {VmValue::Int(broken)}).AsInt() ==
                  (sdk <= 17 ? static_cast<int>(0x80000000U) : 0x40000000));

            f.context->content_view = leaf;
            const ui::UiProjectionTarget target{f.context->ui_tree.Root(), 1, 0, 1, {320, 200}};
            f.context->ui_tree.SetProjectionTarget(target);
            CommitAndroidMeasuredRootProjection(f.vm, *f.context, leaf, target, 1);
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            REQUIRE(snapshot);
            CHECK(snapshot->Get(node)->measured == ui::Size{80, 50});
            CHECK(snapshot->Get(node)->frame == ui::Rect{});
            CHECK_THROWS_AS(ui::LayoutUiTree(f.context->ui_tree, {320, 200}), std::runtime_error);
            CHECK_THROWS_AS(CommitAndroidMeasuredRootProjection(f.vm, *f.context, leaf, target, 1), std::runtime_error);
            CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
            CHECK(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                           AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 60}));
            CHECK(f.context->ui_tree.PublishedProjection()->Get(node)->measured == ui::Size{100, 60});
            f.On(leaf, "requestLayout", "()V");
            CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(leaf));
            CHECK(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                           AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 60}));
            CHECK_FALSE(AndroidUiMeasureRequests(f.vm, *f.context).contains(leaf));

            const auto view_class = f.linker.ResolveDescriptor("Landroid/view/View;");
            CHECK(f.linker
                      .Method(
                          f.linker.Class(view_class).vtable[*f.linker.FindVtableIndex(view_class, "measure", "(II)V")])
                      .kind == MethodKind::interpreted);
            const auto leaf_class = f.linker.ResolveDescriptor("Ltest/MeasuredLeaf;");
            CHECK(
                f.linker
                    .Method(
                        f.linker.Class(leaf_class).vtable[*f.linker.FindVtableIndex(leaf_class, "onMeasure", "(II)V")])
                    .kind == MethodKind::interpreted);
            const auto trace = f.vm.Trace("", 8192);
            CHECK(std::any_of(trace.begin(), trace.end(), [](const auto& event) {
                return event.class_descriptor == "Ltest/MeasuredLeaf;" && event.method_name == "onMeasure";
            }));
            const auto trace_path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/view-measure-" +
                                    (backend == InterpreterBackend::threaded ? "threaded-" : "switch-") +
                                    std::to_string(sdk) + ".trace.json";
            std::ofstream(trace_path) << RenderDexVmTraceJson(trace);
            const auto bad = make("Ltest/MeasuredLeaf;");
            f.On(bad, "omitNext", "()V");
            const auto failed = measure(bad, 0x40000000);
            REQUIRE(failed.exception.IsValid());
            CHECK(f.linker.Class(failed.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
            CHECK(failed.exception_message.find("setMeasuredDimension") != std::string::npos);
            const auto before_failure = f.context->ui_tree.PublishedProjection();
            f.context->content_view = bad;
            CHECK_THROWS_AS(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                     AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 60}),
                            VmJavaThrow);
            CHECK(f.context->ui_tree.PublishedProjection() == before_failure);
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

TEST_CASE("DVM-280 ROM layout callbacks and two-pass owner scheduling publish Java frames") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::function<void(IntrinsicContext&, int)> hook;
        std::vector<int> events;
        std::function<void()> traverse;
        std::vector<std::array<int, 4>> sizes;
        auto probe = IntrinsicClassBuilder::Class("Ltest/LayoutProbe;");
        probe.StaticMethod("size", "(Landroid/view/View;IIII)V", [&](IntrinsicContext& call) {
            events.push_back(0);
            sizes.push_back({call.arguments[1].AsInt(), call.arguments[2].AsInt(), call.arguments[3].AsInt(),
                             call.arguments[4].AsInt()});
            if (hook) hook(call, 0);
            return VmValue::Void();
        });
        probe.StaticMethod("hit", "(Landroid/view/View;I)V", [&](IntrinsicContext& call) {
            const auto stage = call.arguments[1].AsInt();
            events.push_back(stage);
            if (hook) hook(call, stage);
            return VmValue::Void();
        });
        probe.StaticMethod("traverse", "()V", [&](IntrinsicContext&) {
            traverse();
            return VmValue::Void();
        });
        AndroidValueVm f(backend, {std::move(probe).Build()}, false, "view_layout.dex", true,
                         AndroidUiTraversalOwner::JavaTraversal, OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        // Materialize the negative-control type before allocating any instances.
        static_cast<void>(f.linker.ResolveDescriptor("Landroid/view/GLES20DisplayList;"));
        const auto owner = f.New("Landroid/content/Context;");
        const auto make = [&] {
            return f.New("Ltest/LayoutLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        };
        const auto view = make();
        const auto node = *FindViewUiNode(*f.context, view.Value());
        f.context->content_view = view;
        const auto get = [&](const char* name) { return f.On(view, name, "()I").AsInt(); };
        const auto tick = [&](ui::Size size = {80, 50}) {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly, size);
        };
        traverse = [&] { static_cast<void>(tick()); };
        const auto field = [&](const VmObjectRef object, const char* name, const char* type = "I") -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(id);
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot];
        };
        hook = [&](IntrinsicContext&, int) {
            CHECK(get("getWidth") == 80);
            CHECK(get("getHeight") == 50);
            CHECK(get("getLeft") == 0);
            CHECK(get("getTop") == 0);
            CHECK(get("getRight") == 80);
            CHECK(get("getBottom") == 50);
            CHECK_FALSE(tick());  // Reentry does not traverse or publish.
            CHECK_FALSE(f.context->ui_tree.PublishedProjection());
        };
        f.context->ui_tree.Get(node)->frame = {3, 4, 333, 444};
        f.context->ui_tree.Get(node)->background_color = 0xff0000ff;
        f.context->ui_test_accessibility_disabled = true;
        f.On(view, "setClickable", "(Z)V", {VmValue::Int(1)});
        REQUIRE(tick());
        CHECK(events == std::vector<int>{0, 1, 2});
        CHECK(sizes == std::vector<std::array<int, 4>>{{80, 50, 0, 0}});
        auto snapshot = f.context->ui_tree.PublishedProjection();
        REQUIRE(snapshot);
        CHECK(snapshot->Get(node)->frame == ui::Rect{0, 0, 80, 50});
        CHECK(field(view, "mRight").bits == 80);
        CHECK(field(view, "mBottom").bits == 50);
        CHECK(snapshot->ProjectionEpoch() == f.context->ui_measure_epoch);
        CHECK(snapshot->HitTestProjection(79, 49) == node);
        const auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snapshot, {}), {80, 50});
        CHECK(pixels.rgba8[(49 * 80 + 79) * 4 + 3] == 255);
        const auto view_class = f.linker.ResolveDescriptor("Landroid/view/View;");
        const auto size_change = f.linker.FindDirectMethod(view_class, "sizeChange", "(IIII)V");
        REQUIRE(size_change);
        CHECK(f.linker.Method(*size_change).kind == MethodKind::interpreted);
        for (const auto& [name, desc] :
             std::array<std::pair<const char*, const char*>, 10>{{{"layout", "(IIII)V"},
                                                                  {"setFrame", "(IIII)Z"},
                                                                  {"onSizeChanged", "(IIII)V"},
                                                                  {"onLayout", "(ZIIII)V"},
                                                                  {"getLeft", "()I"},
                                                                  {"getTop", "()I"},
                                                                  {"getRight", "()I"},
                                                                  {"getBottom", "()I"},
                                                                  {"getWidth", "()I"},
                                                                  {"getHeight", "()I"}}}) {
            const auto index = f.linker.FindVtableIndex(view_class, name, desc);
            REQUIRE(index);
            CHECK(f.linker.Method(f.linker.Class(view_class).vtable[*index]).kind == MethodKind::interpreted);
        }
        CHECK(f.ledger.Unimplemented().empty());
        hook = {};
        events.clear();
        sizes.clear();
        CHECK_FALSE(tick());
        CHECK(events.empty());
        CHECK(sizes.empty());
        REQUIRE(tick({90, 60}));
        CHECK(sizes == std::vector<std::array<int, 4>>{{90, 60, 80, 50}});
        // An onLayout request is already fulfilled by this root's ROM layout:
        // FORCE_LAYOUT is cleared by the ROM epilogue, so there is no second pass.
        events.clear();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 1) f.On(view, "requestLayout", "()V");
        };
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(std::count(events.begin(), events.end(), 2) == 1);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        // A request after super.layout remains valid. First pass retries in this
        // frame; the second pass defers without setting FORCE_LAYOUT.
        events.clear();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 2) f.On(view, "requestLayout", "()V");
        };
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(std::count(events.begin(), events.end(), 2) == 2);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(view));
        CHECK((field(view, "mPrivateFlags").bits & 0x1000) == 0);
        const auto epoch = f.context->ui_measure_epoch;
        events.clear();
        hook = {};
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(std::count(events.begin(), events.end(), 1) == 1);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        CHECK(f.context->ui_measure_epoch == epoch + 1);
        // GONE requesters are filtered in both passes, as in ViewRootImpl.
        field(view, "mViewFlags").bits |= 8;
        events.clear();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 2) f.On(view, "requestLayout", "()V");
        };
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(std::count(events.begin(), events.end(), 2) == 1);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        CHECK((field(view, "mPrivateFlags").bits & 0x1000) == 0);
        field(view, "mViewFlags").bits &= ~8U;
        events.clear();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 2) {
                if (f.context->ui_java_layout_pass == 2) field(view, "mViewFlags").bits |= 8;
                f.On(view, "requestLayout", "()V");
            }
        };
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(std::count(events.begin(), events.end(), 2) == 2);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        field(view, "mViewFlags").bits &= ~8U;
        CHECK(f.ledger.Unimplemented().empty());
        // Repeated cross-tick requests hit a bounded watchdog, retaining both
        // the requester queue and the last complete snapshot.
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 2) f.On(view, "requestLayout", "()V");
        };
        f.On(view, "requestLayout", "()V");
        for (int i = 0; i < 8; ++i)
            REQUIRE(tick());
        snapshot = f.context->ui_tree.PublishedProjection();
        try {
            static_cast<void>(tick());
            FAIL("watchdog must fail");
        } catch (const VmJavaThrow& error) {
            CHECK(error.message.find("requester=" + std::to_string(view.Value())) != std::string::npos);
            CHECK(error.message.find("epoch=") != std::string::npos);
        }
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        CHECK(AndroidUiMeasureRequests(f.vm, *f.context).contains(view));
        CHECK_FALSE(f.context->ui_layout_dispatching);
        hook = {};
        f.On(view, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.context->ui_window_schedules.at(view).deferred_ticks == 0);
        // Guest exception identity and already-mutated Java bounds survive;
        // no partial snapshot is exposed.
        const auto exception = f.New("Ljava/lang/IllegalStateException;");
        snapshot = f.context->ui_tree.PublishedProjection();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 1) throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "layout probe", exception};
        };
        try {
            static_cast<void>(tick({110, 70}));
            FAIL("callback must throw");
        } catch (const VmJavaThrow& error) {
            CHECK(error.existing == exception);
        }
        CHECK(get("getWidth") == 110);
        CHECK(get("getHeight") == 70);
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        CHECK_FALSE(f.context->ui_layout_dispatching);
        // A second guest token fails even on the same host thread.
        const auto execution = f.vm.CreateExecutionContext();
        const auto method =
            f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Ltest/LayoutProbe;"), "traverse", "()V");
        REQUIRE(method);
        const auto other = f.vm.Call(execution, *method, {});
        REQUIRE(other.exception.IsValid());
        CHECK(f.linker.Class(other.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        f.vm.DiscardExecutionContext(execution);
        // Existing hardware state is rejected, rather than ignored as null.
        static_cast<void>(f.linker.ResolveDescriptor("Landroid/view/GLES20DisplayList;"));
        const auto hardware = f.vm.NewIntrinsicInstance("Landroid/view/GLES20DisplayList;");
        auto& display_list = field(view, "mDisplayList", "Landroid/view/DisplayList;");
        display_list = {hardware.Value(), SlotTag::ref};
        const auto hardware_result = f.OnOutcome(view, "invalidate", "()V");
        REQUIRE(hardware_result.exception.IsValid());
        CHECK(f.linker.Class(hardware_result.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        display_list = {0, SlotTag::ref};
        // Replacement and window generation invalidation cancel publication.
        hook = {};
        const auto replacement = make();
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 1) {
                f.context->content_view = replacement;
                ++f.context->ui_content_revision;
            }
        };
        CHECK_FALSE(tick({120, 80}));
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        f.context->content_view = view;
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 1) ++f.context->surface_width;
        };
        CHECK_FALSE(tick({125, 85}));
        CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
        hook = [&](IntrinsicContext&, int stage) {
            if (stage == 1) f.context->ui_tree.Reset();
        };
        CHECK_FALSE(tick({130, 90}));
        CHECK_FALSE(f.context->ui_tree.PublishedProjection());
        hook = {};
        // The same VM token or a held VM lock does not make another host thread owner.
        bool rejected{};
        std::thread worker([&] {
            VmExecutionLockScope lock(f.vm.ExecutionLock());
            try {
                static_cast<void>(tick());
            } catch (const VmJavaThrow& error) {
                rejected = error.descriptor == "Ljava/lang/IllegalStateException;";
            }
        });
        worker.join();
        CHECK(rejected);
        // Only deliberate failure controls hit the ledger.
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 2);
        CHECK(hits[0].id == "dexvm.view_layout_system_state");
        CHECK(hits[1].id == "dexvm.view_layout_watchdog");
        std::ofstream(std::string(".local/analysis/vd-05/layout-") +
                      (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json")
            << RenderDexVmTraceJson(f.vm.Trace("", 8192));
    }
}

TEST_CASE("DVM-279 owner payload pairing is fixed before VM creation") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        CHECK_THROWS_AS(AndroidValueVm(backend, {}, false, nullptr, false, AndroidUiTraversalOwner::JavaTraversal),
                        std::invalid_argument);
        CHECK_THROWS_AS(AndroidValueVm(backend, {}, false, nullptr, false, AndroidUiTraversalOwner::Legacy,
                                       OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar"),
                        std::invalid_argument);
    }
}

TEST_CASE("DVM-277 ROM View shape preserves adapters and rejects host-owned fields") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend, {}, false, "view_field_access.dex");
        const auto owner = fixture.New("Landroid/app/Activity;");
        const auto view = fixture.New("Ltest/ViewFieldAccess;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        CHECK(fixture.On(view, "context", "()Landroid/content/Context;").ref == owner);
        const auto type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto& cls = fixture.linker.Class(type);
        CHECK(cls.is_boot_dex);
        CHECK(cls.own_instance_fields.size() == 91);
        for (const auto name : {"mOgplayWindowOwner", "mOnFocusChangeListener", "mOgplaySystemUiVisibilityListener"}) {
            CHECK(std::none_of(cls.own_instance_fields.begin(), cls.own_instance_fields.end(),
                               [&](const auto id) { return fixture.linker.Field(id).name == name; }));
        }
        const auto states = fixture.linker.FindFieldRecursive(type, "VIEW_STATE_SETS", "[[I");
        REQUIRE(states.has_value());
        const auto array = VmObjectRef(fixture.linker.Class(type).static_storage[fixture.linker.Field(*states).slot]);
        REQUIRE(array.IsValid());
        CHECK(fixture.model.ArrayLength(array) == 1024);
        for (const auto name : {"left", "writeLeft"}) {
            const auto outcome = fixture.OnOutcome(view, name, "()I");
            REQUIRE(outcome.exception.IsValid());
            CHECK(fixture.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
        }
        CHECK(fixture.On(view, "getId", "()I").AsInt() == -1);
        RegisterAndroidOwnerAttachedStateTable(fixture.vm, fixture.context);
        const auto focus = fixture.New("Ljava/lang/Object;");
        const auto system_ui = fixture.New("Ljava/lang/Object;");
        const auto window_owner = fixture.New("Landroid/app/Activity;");
        fixture.context->view_adapter_references[view.Value()] = {window_owner, focus, system_ui};
        {
            const auto roots = fixture.vm.ProtectReferences(std::array{view});
            static_cast<void>(fixture.vm.CollectGarbage("dvm277-view-adapter-trace"));
            CHECK(fixture.model.ObjectClass(focus) == fixture.linker.ResolveDescriptor("Ljava/lang/Object;"));
            CHECK(fixture.model.ObjectClass(system_ui) == fixture.model.ObjectClass(focus));
            CHECK(fixture.model.ObjectClass(window_owner) == fixture.model.ObjectClass(owner));
        }
        static_cast<void>(fixture.vm.CollectGarbage("dvm277-view-adapter-sweep"));
        CHECK_FALSE(fixture.context->view_adapter_references.contains(view.Value()));
    }
}

TEST_CASE("DVM-277 View constructor initializes ROM SDK compatibility flags once") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto sdk : {17U, 18U, 19U}) {
            AndroidValueVm fixture(backend);
            fixture.context->target_sdk_version = sdk;
            const auto owner = fixture.New("Landroid/app/Activity;");
            static_cast<void>(
                fixture.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)}));
            const auto type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
            const auto flag = [&](const char* name) {
                const auto field = fixture.linker.FindFieldRecursive(type, name, "Z");
                REQUIRE(field.has_value());
                CHECK_FALSE(fixture.linker.Field(*field).is_unimplemented);
                return fixture.linker.Class(type).static_storage[fixture.linker.Field(*field).slot];
            };
            CHECK(flag("sCompatibilityDone") == 1);
            CHECK(flag("sUseBrokenMakeMeasureSpec") == (sdk <= 17U));
            CHECK(flag("sIgnoreMeasureCache") == (sdk < 19U));
            fixture.context->target_sdk_version = 1;
            static_cast<void>(
                fixture.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)}));
            CHECK(flag("sUseBrokenMakeMeasureSpec") == (sdk <= 17U));
            CHECK(flag("sIgnoreMeasureCache") == (sdk < 19U));
        }
    }
}

TEST_CASE("DVM-277B Java View layout fields feed ROM getters and host projections") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        VmObjectRef supplied_resources;
        int resource_calls = 0;
        auto owner_class = IntrinsicClassBuilder::Class("Ltest/ViewResourcesOwner;", "Landroid/content/Context;");
        owner_class.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        owner_class.OverrideMethod("getResources", "()Landroid/content/res/Resources;", [&](IntrinsicContext&) {
            ++resource_calls;
            return VmValue::Ref(supplied_resources);
        });
        AndroidValueVm f(backend, {std::move(owner_class).Build()}, false, "view_java_layout.dex");
        const auto base = f.New("Landroid/content/Context;");
        supplied_resources = f.On(base, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto expected_resources = supplied_resources;
        const auto owner = f.New("Ltest/ViewResourcesOwner;");
        // The production adapter must propagate a genuinely unattached
        // ContextWrapper failure; only valid UI fixtures attach a test base.
        const auto empty_wrapper = f.New("Landroid/content/ContextWrapper;");
        const auto uninitialized_view = f.vm.NewIntrinsicInstance("Landroid/view/View;");
        const auto view_type = f.linker.ResolveDescriptor("Landroid/view/View;");
        const auto constructor = f.linker.FindDirectMethod(view_type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor.has_value());
        const auto failed =
            f.vm.Call(*constructor, std::array{VmValue::Ref(uninitialized_view), VmValue::Ref(empty_wrapper)});
        REQUIRE(failed.exception.IsValid());
        CHECK(f.linker.Class(failed.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto retried =
            f.vm.Call(*constructor, std::array{VmValue::Ref(uninitialized_view), VmValue::Ref(empty_wrapper)});
        REQUIRE(retried.exception.IsValid());
        CHECK(f.linker.Class(retried.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto compatibility = f.linker.FindFieldRecursive(view_type, "sCompatibilityDone", "Z");
        REQUIRE(compatibility.has_value());
        CHECK(f.linker.Class(view_type).static_storage[f.linker.Field(*compatibility).slot] == 0);
        const auto view = f.New("Ltest/ViewJavaLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{view, owner});
        CHECK(resource_calls == 1);
        supplied_resources =
            f.Static("Landroid/content/res/Resources;", "getSystem", "()Landroid/content/res/Resources;").ref;
        CHECK(f.On(view, "getResources", "()Landroid/content/res/Resources;").ref == expected_resources);
        CHECK(resource_calls == 1);
        CHECK(f.On(view, "getMinimumWidth", "()I").AsInt() == 0);
        CHECK(f.On(view, "getMinimumHeight", "()I").AsInt() == 0);
        const auto type = f.linker.ResolveDescriptor("Landroid/view/View;");
        for (const auto name : {"getPaddingTop", "getPaddingBottom", "getMinimumWidth", "getMinimumHeight"}) {
            const auto slot = f.linker.FindVtableIndex(type, name, "()I");
            REQUIRE(slot.has_value());
            CHECK(f.linker.Method(f.linker.Class(type).vtable[*slot]).kind == MethodKind::interpreted);
        }
        const auto resources_slot = f.linker.FindVtableIndex(type, "getResources", "()Landroid/content/res/Resources;");
        REQUIRE(resources_slot.has_value());
        CHECK(f.linker.Method(f.linker.Class(type).vtable[*resources_slot]).kind == MethodKind::interpreted);
        const auto int_field = [&](const char* name, const char* descriptor = "I") {
            const auto field = f.linker.FindFieldRecursive(type, name, descriptor);
            REQUIRE(field.has_value());
            CHECK_FALSE(f.linker.Field(*field).is_unimplemented);
            return static_cast<std::int32_t>(f.model.InstanceSlots(view)[f.linker.Field(*field).slot].bits);
        };
        CHECK(int_field("mUserPaddingStart") == std::numeric_limits<std::int32_t>::min());
        CHECK(int_field("mUserPaddingEnd") == std::numeric_limits<std::int32_t>::min());
        CHECK(int_field("mLeftPaddingDefined", "Z") == 0);
        f.On(view, "setPadding", "(IIII)V", {VmValue::Int(3), VmValue::Int(4), VmValue::Int(5), VmValue::Int(6)});
        const std::array names{"Left", "Top", "Right", "Bottom"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            CHECK(f.On(view, (std::string("padding") + names[i]).c_str(), "()I").AsInt() == static_cast<int>(i + 3));
            CHECK(f.On(view, (std::string("getPadding") + names[i]).c_str(), "()I").AsInt() == static_cast<int>(i + 3));
        }
        CHECK(int_field("mUserPaddingLeft") == 3);
        CHECK(int_field("mUserPaddingRightInitial") == 5);
        CHECK(int_field("mUserPaddingBottom") == 6);
        CHECK(int_field("mLeftPaddingDefined", "Z") == 1);
        f.On(view, "setMinimumWidth", "(I)V", {VmValue::Int(31)});
        f.On(view, "setMinimumHeight", "(I)V", {VmValue::Int(19)});
        CHECK(f.On(view, "getMinimumWidth", "()I").AsInt() == 31);
        CHECK(f.On(view, "getMinimumHeight", "()I").AsInt() == 19);
        CHECK(f.Static("Ltest/ViewJavaLayout;", "requestCount", "()I").AsInt() == 3);
        const auto node = FindViewUiNode(*f.context, view.Value());
        REQUIRE(node.has_value());
        const auto params =
            f.New("Landroid/widget/FrameLayout$LayoutParams;", "(II)V", {VmValue::Int(-2), VmValue::Int(-2)});
        f.On(view, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(params)});
        f.context->ui_tree.Attach(f.context->ui_tree.Root(), *node);
        const auto resolve = [&] {
            static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                       AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 80}));
        };
        resolve();
        CHECK(f.context->ui_tree.Get(*node)->measured == (ui::Size{31, 19}));
        CHECK_FALSE(f.context->ui_tree.Get(f.context->ui_tree.Root())->layout_dirty);
        f.On(view, "storePadding", "(IIII)V", {VmValue::Int(40), VmValue::Int(11), VmValue::Int(9), VmValue::Int(12)});
        CHECK(f.On(view, "getPaddingLeft", "()I").AsInt() == 40);
        // A direct guest field store must invalidate an otherwise clean input projection.
        resolve();
        CHECK(f.context->ui_tree.Get(*node)->padding == (ui::Insets{40, 11, 9, 12}));
        CHECK(f.context->ui_tree.Get(*node)->measured == (ui::Size{49, 23}));
        static_cast<void>(f.vm.CollectGarbage("dvm277b-java-layout"));
        CHECK(f.On(view, "getResources", "()Landroid/content/res/Resources;").ref == expected_resources);
    }
}

TEST_CASE("DVM-277B XML padding and minimum are Java-owned layout inputs") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto owner = f.New("Landroid/content/Context;");
        const auto attribute = [](const char* name, const std::uint8_t type, const std::uint32_t value) {
            ogplay::loader::BinaryXmlAttribute result;
            result.namespace_uri = "http://schemas.android.com/apk/res/android";
            result.name = name;
            result.value_type = type;
            result.data = value;
            return result;
        };
        std::vector<ogplay::loader::BinaryXmlElement> xml(1);
        xml[0].name = "FrameLayout";
        xml[0].attributes = {
            attribute("layout_width", 0x10, 0xfffffffeU), attribute("layout_height", 0x10, 0xfffffffeU),
            attribute("paddingLeft", 5, 3U << 8U),        attribute("paddingTop", 5, 4U << 8U),
            attribute("paddingRight", 5, 5U << 8U),       attribute("paddingBottom", 5, 6U << 8U),
            attribute("minWidth", 5, 31U << 8U),          attribute("minHeight", 5, 19U << 8U)};
        const auto view = InflateUiElements(f.vm, *f.context, xml, owner);
        const auto node = FindViewUiNode(*f.context, view.Value());
        REQUIRE(node.has_value());
        CHECK(f.On(view, "getPaddingLeft", "()I").AsInt() == 3);
        CHECK(f.On(view, "getPaddingTop", "()I").AsInt() == 4);
        CHECK(f.On(view, "getMinimumWidth", "()I").AsInt() == 31);
        CHECK(f.On(view, "getMinimumHeight", "()I").AsInt() == 19);
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 80}));
        CHECK(f.context->ui_tree.Get(*node)->padding == (ui::Insets{3, 4, 5, 6}));
        CHECK(f.context->ui_tree.Get(*node)->measured == (ui::Size{31, 19}));
    }
}

TEST_CASE("DVM-138 View.getParent follows the live UiTree hierarchy") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto first_parent = f.New(
            "Landroid/widget/RelativeLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second_parent = f.New(
            "Landroid/widget/FrameLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});

        const auto view_parent =
            f.linker.ResolveDescriptor("Landroid/view/ViewParent;");
        CHECK(f.linker.Class(view_parent).is_interface);
        CHECK(f.linker.IsAssignable(
            view_parent, f.model.ObjectClass(first_parent)));
        const auto method = f.linker.FindVtableIndex(
            f.model.ObjectClass(child), "getParent",
            "()Landroid/view/ViewParent;");
        REQUIRE(method.has_value());
        CHECK((f.linker.Method(
                   f.linker.Class(f.model.ObjectClass(child))
                       .vtable[*method])
                   .access_flags &
               (kAccPublic | kAccFinal)) ==
              (kAccPublic | kAccFinal));

        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(first_parent, "addView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == first_parent);
        f.On(first_parent, "removeView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(second_parent, "addView", "(Landroid/view/View;)V",
             {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == second_parent);
        f.On(second_parent, "removeViews", "(II)V",
             {VmValue::Int(0), VmValue::Int(1)});
        CHECK_FALSE(f.On(child, "getParent",
                         "()Landroid/view/ViewParent;").ref.IsValid());

        f.On(activity, "setContentView", "(Landroid/view/View;)V",
             {VmValue::Ref(first_parent)});
        CHECK(f.On(first_parent, "getParent", "()Landroid/view/ViewParent;").ref ==
              f.On(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref);
    }
}

TEST_CASE("DVM-119 Java layout params drive FrameLayout geometry and copy semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->surface_width = 100;
        f.context->surface_height = 100;
        const auto activity = f.New("Landroid/app/Activity;");
        const auto frame = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto frame_params = f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V",
                                         {VmValue::Int(-1), VmValue::Int(-1)});
        f.On(frame, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(frame_params)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto params = f.New("Landroid/widget/FrameLayout$LayoutParams;", "(III)V",
                                   {VmValue::Int(20), VmValue::Int(10), VmValue::Int(85)});
        f.On(params, "setMargins", "(IIII)V",
             {VmValue::Int(1), VmValue::Int(2), VmValue::Int(3), VmValue::Int(4)});
        CHECK(f.linker.Class(f.model.ObjectClass(params)).is_boot_dex);
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/view/ViewGroup$MarginLayoutParams;"),
                                    f.model.ObjectClass(params)));
        const auto copy = f.New("Landroid/widget/FrameLayout$LayoutParams;",
                                "(Landroid/widget/FrameLayout$LayoutParams;)V", {VmValue::Ref(params)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        const auto field = [&](VmObjectRef object, const char* name) -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, "I");
            REQUIRE(id.has_value());
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot];
        };
        CHECK(field(copy, "gravity").bits == 85);
        CHECK(field(copy, "bottomMargin").bits == 4);
        f.On(frame, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(child), VmValue::Ref(params)});
        f.On(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(frame)});
        CHECK(f.On(child, "getLeft", "()I").AsInt() == 77);
        CHECK(f.On(child, "getTop", "()I").AsInt() == 86);
        CHECK(f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == params);
        field(params, "width").bits = 30;
        f.On(child, "requestLayout", "()V");
        CHECK(f.On(child, "getLeft", "()I").AsInt() == 67);
        CHECK(f.On(child, "getWidth", "()I").AsInt() == 30);
        CHECK(field(copy, "width").bits == 20);
        const auto bad = f.OnOutcome(child, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
                                     {VmValue::Ref(VmObjectRef{})});
        REQUIRE(bad.exception.IsValid());
        CHECK(f.linker.Class(bad.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        const auto relative = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                    {VmValue::Int(10), VmValue::Int(10)});
        f.On(relative, "addRule", "(I)V", {VmValue::Int(4)});
        const auto baseline = f.OnOutcome(child, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V",
                                          {VmValue::Ref(relative)});
        REQUIRE(baseline.exception.IsValid());
        CHECK(f.linker.Class(baseline.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref == params);
        const auto invalid = f.OnOutcome(relative, "addRule", "(I)V", {VmValue::Int(22)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(f.linker.Class(invalid.exception_class).descriptor == "Ljava/lang/ArrayIndexOutOfBoundsException;");
        const auto group = f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V",
                                  {VmValue::Ref(activity)});
        const auto group_params = f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V",
                                         {VmValue::Int(100), VmValue::Int(100)});
        f.On(group, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(group_params)});
        const auto first = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        f.On(first, "setId", "(I)V", {VmValue::Int(1001)});
        const auto first_params = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                         {VmValue::Int(20), VmValue::Int(10)});
        const auto second_params = f.New("Landroid/widget/RelativeLayout$LayoutParams;", "(II)V",
                                          {VmValue::Int(10), VmValue::Int(10)});
        f.On(second_params, "addRule", "(II)V", {VmValue::Int(1), VmValue::Int(1001)});
        f.On(group, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(first), VmValue::Ref(first_params)});
        f.On(group, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(second), VmValue::Ref(second_params)});
        f.On(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
        CHECK(f.On(group, "getGravity", "()I").AsInt() == 0x00800033);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(85)});
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 70);
        CHECK(f.On(second, "getLeft", "()I").AsInt() == 90);
        CHECK(f.On(first, "getTop", "()I").AsInt() == 90);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(17)});
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 35);
        CHECK(f.On(second, "getLeft", "()I").AsInt() == 55);
        CHECK(f.On(first, "getTop", "()I").AsInt() == 45);
        f.On(group, "setGravity", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(group, "getGravity", "()I").AsInt() == 0x00800033);
        CHECK(f.On(first, "getLeft", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-167 ViewGroup width-height add uses virtual default params") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto relative = f.New(
            "Landroid/widget/RelativeLayout;",
            "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});

        f.On(relative, "addView", "(Landroid/view/View;II)V",
             {VmValue::Ref(child), VmValue::Int(-1), VmValue::Int(-1)});
        const auto params = f.On(
            child, "getLayoutParams",
            "()Landroid/view/ViewGroup$LayoutParams;").ref;
        REQUIRE(params.IsValid());
        CHECK(f.model.ObjectClass(params) ==
              f.linker.ResolveDescriptor(
                  "Landroid/widget/RelativeLayout$LayoutParams;"));
        const auto field = [&](const char* name) -> std::int32_t {
            const auto id = f.linker.FindFieldRecursive(
                f.model.ObjectClass(params), name, "I");
            REQUIRE(id.has_value());
            return static_cast<std::int32_t>(
                f.model.InstanceSlots(params)[f.linker.Field(*id).slot].bits);
        };
        CHECK(field("width") == -1);
        CHECK(field("height") == -1);
        CHECK(f.On(child, "getParent",
                   "()Landroid/view/ViewParent;").ref == relative);
    }
}

TEST_CASE("DVM-174 AnimationListener type compatibility links and dispatches") {
    constexpr auto kListener =
        "Landroid/view/animation/Animation$AnimationListener;";
    constexpr auto kAnimation = "Landroid/view/animation/Animation;";
    constexpr auto kSignature =
        "(Landroid/view/animation/Animation;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t starts{};
        std::int32_t ends{};
        std::int32_t repeats{};
        auto implementor = IntrinsicClassBuilder::Class(
            "Ltest/AnimationListener;", "Ljava/lang/Object;",
            {kListener});
        implementor.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        implementor.VirtualMethod(
            "onAnimationStart", kSignature,
            [&starts](IntrinsicContext&) {
                ++starts;
                return VmValue::Void();
            });
        implementor.VirtualMethod(
            "onAnimationEnd", kSignature,
            [&ends](IntrinsicContext&) {
                ++ends;
                return VmValue::Void();
            });
        implementor.VirtualMethod(
            "onAnimationRepeat", kSignature,
            [&repeats](IntrinsicContext&) {
                ++repeats;
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(implementor).Build()});

        const auto listener = fixture.linker.ResolveDescriptor(kListener);
        const auto& listener_class = fixture.linker.Class(listener);
        CHECK(listener_class.is_boot_dex);
        CHECK(listener_class.is_interface);
        CHECK(listener_class.access_flags ==
              (kAccPublic | kAccInterface | kAccAbstract));
        const auto animation_type = fixture.linker.FindClass(kAnimation);
        REQUIRE(animation_type.has_value());
        CHECK(fixture.linker.Class(*animation_type).is_boot_dex);
        CHECK((fixture.linker.Class(*animation_type).access_flags & kAccAbstract) != 0);
        CHECK(fixture.linker.IsAssignable(
            fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), *animation_type));

        std::vector<std::string> declared;
        for (const auto method : listener_class.own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            declared.push_back(linked.name);
            CHECK(linked.descriptor == kSignature);
            CHECK(linked.kind == MethodKind::abstract);
            CHECK(linked.declared_invoke_kind ==
                  DeclaredInvokeKind::interface_call);
            CHECK((linked.access_flags & (kAccPublic | kAccAbstract)) ==
                  (kAccPublic | kAccAbstract));
        }
        CHECK(declared == std::vector<std::string>{
            "onAnimationEnd", "onAnimationRepeat", "onAnimationStart"});

        const auto object = fixture.New("Ltest/AnimationListener;");
        const auto object_class = fixture.model.ObjectClass(object);
        CHECK(fixture.linker.IsAssignable(listener, object_class));
        CHECK_FALSE(fixture.linker.IsAssignable(object_class, listener));

        const auto animation = VmValue::Ref(VmObjectRef{});
        fixture.On(object, "onAnimationStart", kSignature, {animation});
        fixture.On(object, "onAnimationEnd", kSignature, {animation});
        fixture.On(object, "onAnimationRepeat", kSignature, {animation});
        CHECK(starts == 1);
        CHECK(ends == 1);
        CHECK(repeats == 1);
    }
}

TEST_CASE("DVM-175 View.setOnClickListener is overridable and super registers the listener") {
    constexpr auto kSignature = "(Landroid/view/View$OnClickListener;)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t overrides{};
        VmObjectRef last_wrapper{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/WrappingView;", "Landroid/view/View;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    view, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.OverrideMethod(
            "setOnClickListener", kSignature,
            [&overrides, &last_wrapper](IntrinsicContext& call) {
                ++overrides;
                const auto incoming = call.arguments[0].ref;
                last_wrapper = incoming.IsValid()
                    ? call.vm.NewIntrinsicInstance("Ltest/ClickWrapper;")
                    : VmObjectRef{};
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                std::optional<VmMethodId> inherited;
                for (const auto method :
                     call.vm.Linker().Class(view).own_virtual_methods) {
                    const auto& linked = call.vm.Linker().Method(method);
                    if (linked.name == "setOnClickListener" &&
                        linked.descriptor == kSignature) {
                        inherited = method;
                        break;
                    }
                }
                const std::array arguments{
                    VmValue::Ref(call.receiver),
                    VmValue::Ref(last_wrapper)};
                const auto outcome = call.vm.Call(*inherited, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto wrapper = IntrinsicClassBuilder::Class("Ltest/ClickWrapper;");
        wrapper.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/ClickListener;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnClickListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        AndroidValueVm fixture(
            backend,
            {std::move(subclass).Build(), std::move(wrapper).Build(),
             std::move(listener).Build()});

        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        std::optional<VmMethodId> view_method;
        for (const auto method :
             fixture.linker.Class(view_type).own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            if (linked.name == "setOnClickListener" &&
                linked.descriptor == kSignature) {
                view_method = method;
                break;
            }
        }
        REQUIRE(view_method.has_value());
        CHECK(fixture.linker.Method(*view_method).overridable);
        CHECK((fixture.linker.Method(*view_method).access_flags & kAccFinal) ==
              0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto object = fixture.New(
            "Ltest/WrappingView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, "setOnClickListener", kSignature);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, "setOnClickListener", kSignature);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(object_class).vtable[*subclass_slot])
                  .owner == object_class);

        const auto callback = fixture.New("Ltest/ClickListener;");
        fixture.On(object, "setOnClickListener", kSignature,
                   {VmValue::Ref(callback)});
        CHECK(overrides == 1);
        const auto node = FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(node.has_value());
        REQUIRE(fixture.context->ui_click_listeners.contains(*node));
        CHECK(fixture.context->ui_click_listeners.at(*node) == last_wrapper);
        CHECK(last_wrapper != callback);

        fixture.On(object, "setOnClickListener", kSignature,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(overrides == 2);
        CHECK_FALSE(last_wrapper.IsValid());
        CHECK_FALSE(fixture.context->ui_click_listeners.contains(*node));
    }
}

TEST_CASE("DVM-177 LinearLayout programmatic AttributeSet constructor") {
    constexpr auto kTwoArg =
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    const auto super_construct = [](IntrinsicContext& call, const char* owner,
                                    const char* signature) {
        const auto type = call.vm.Linker().ResolveDescriptor(owner);
        const auto constructor =
            call.vm.Linker().FindDirectMethod(type, "<init>", signature);
        std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
        arguments.insert(arguments.end(), call.arguments.begin(),
                         call.arguments.end());
        const auto outcome = call.vm.Call(*constructor, arguments);
        if (outcome.exception.IsValid()) {
            throw VmJavaThrow{
                call.vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message, outcome.exception};
        }
        return VmValue::Void();
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ProgrammaticLinear;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto type = call.vm.Model().ObjectClass(call.receiver);
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>",
                    "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0],
                    VmValue::Ref(VmObjectRef{})};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.Constructor(
            kTwoArg, [&super_construct](IntrinsicContext& call) {
                return super_construct(
                    call, "Landroid/widget/LinearLayout;", kTwoArg);
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto context = fixture.New("Landroid/content/Context;");
        const auto activity = fixture.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(fixture.vm, activity);
        const auto two_arg = fixture.linker.FindDirectMethod(
            fixture.linker.ResolveDescriptor("Landroid/widget/LinearLayout;"),
            "<init>", kTwoArg);
        REQUIRE(two_arg.has_value());

        const auto before = fixture.context->object_to_ui_node.size();
        const auto layout = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        const auto layout_node = FindViewUiNode(*fixture.context, layout.Value());
        REQUIRE(layout_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*layout_node)->kind ==
              ui::UiClass::LinearLayout);
        CHECK(fixture.context->ui_tree.Get(*layout_node)->orientation ==
              ui::Orientation::Horizontal);
        CHECK(fixture.On(layout, "getOrientation", "()I").AsInt() == 0);
        CHECK(fixture.context->object_to_ui_node.size() == before + 1);
        CHECK(fixture.context->ui_node_to_object.at(*layout_node) == layout);

        const auto second = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(context), VmValue::Ref(VmObjectRef{})});
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(second_node.has_value());
        CHECK(*second_node != *layout_node);
        CHECK(second != layout);
        CHECK(fixture.context->object_to_ui_node.size() == before + 2);
        CHECK(fixture.On(layout, "getOrientation", "()I").AsInt() == 0);
        CHECK(fixture.context->object_to_ui_node.size() == before + 2);

        const auto subclass_object = fixture.New(
            "Ltest/ProgrammaticLinear;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        CHECK(fixture.model.ObjectClass(subclass_object) ==
              fixture.linker.ResolveDescriptor("Ltest/ProgrammaticLinear;"));
        const auto subclass_node =
            FindViewUiNode(*fixture.context, subclass_object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->kind ==
              ui::UiClass::LinearLayout);
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->orientation ==
              ui::Orientation::Horizontal);
        CHECK(*subclass_node != *layout_node);

        fixture.On(subclass_object, "setOrientation", "(I)V",
                   {VmValue::Int(1)});
        CHECK(fixture.On(subclass_object, "getOrientation", "()I").AsInt() == 1);
        const auto child = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        fixture.On(subclass_object, "addView", "(Landroid/view/View;)V",
                   {VmValue::Ref(child)});
        fixture.On(activity, "setContentView", "(Landroid/view/View;)V",
                   {VmValue::Ref(subclass_object)});
        CHECK(fixture.On(child, "getParent", "()Landroid/view/ViewParent;")
                  .ref == subclass_object);

        const auto null_context = fixture.vm.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        const auto null_outcome = fixture.vm.Call(
            *two_arg,
            std::array{VmValue::Ref(null_context),
                       VmValue::Ref(VmObjectRef{}),
                       VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_outcome.exception.IsValid());
        CHECK(fixture.linker.Class(null_outcome.exception_class).descriptor ==
              "Ljava/lang/NullPointerException;");

        const auto attrs = fixture.vm.NewStringUtf8("not an AttributeSet");
        const auto attr_target = fixture.vm.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        const auto attr_outcome = fixture.vm.Call(
            *two_arg,
            std::array{VmValue::Ref(attr_target), VmValue::Ref(context),
                       VmValue::Ref(attrs)});
        REQUIRE(attr_outcome.exception.IsValid());
        CHECK(fixture.linker.Class(attr_outcome.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
        CHECK(attr_outcome.exception_message ==
              "constructing a View from an AttributeSet is unsupported");
        bool recorded = false;
        for (const auto& hit : fixture.ledger.Unimplemented()) {
            if (hit.id == "dexvm.view_xml_attributes") {
                recorded = true;
                CHECK(hit.count >= 1);
            }
        }
        CHECK(recorded);
    }
}

TEST_CASE("DVM-178 ViewGroup clipChildren and clipToPadding are overridable state") {
    constexpr auto kSetClipChildren = "setClipChildren";
    constexpr auto kSetClipToPadding = "setClipToPadding";
    constexpr auto kGetClipChildren = "getClipChildren";
    constexpr auto kBoolVoid = "(Z)V";
    constexpr auto kBool = "()Z";
    const auto find_virtual = [](const DexClassLinker& linker,
                                 const DexClassId type, const char* name,
                                 const char* signature) {
        std::optional<VmMethodId> found;
        for (const auto method : linker.Class(type).own_virtual_methods) {
            const auto& linked = linker.Method(method);
            if (linked.name == name && linked.descriptor == signature) {
                found = method;
                break;
            }
        }
        return found;
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t overrides{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ClipLinear;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/widget/LinearLayout;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        subclass.OverrideMethod(
            kSetClipChildren, kBoolVoid,
            [&overrides](IntrinsicContext& call) {
                ++overrides;
                const auto group = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/ViewGroup;");
                std::optional<VmMethodId> inherited;
                for (const auto method :
                     call.vm.Linker().Class(group).own_virtual_methods) {
                    const auto& linked = call.vm.Linker().Method(method);
                    if (linked.name == kSetClipChildren &&
                        linked.descriptor == kBoolVoid) {
                        inherited = method;
                        break;
                    }
                }
                const std::array arguments{VmValue::Ref(call.receiver),
                                           call.arguments[0]};
                const auto outcome = call.vm.Call(*inherited, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto group_type =
            fixture.linker.ResolveDescriptor("Landroid/view/ViewGroup;");
        const auto children_method =
            find_virtual(fixture.linker, group_type, kSetClipChildren, kBoolVoid);
        const auto padding_method =
            find_virtual(fixture.linker, group_type, kSetClipToPadding, kBoolVoid);
        const auto getter =
            find_virtual(fixture.linker, group_type, kGetClipChildren, kBool);
        REQUIRE(children_method.has_value());
        REQUIRE(padding_method.has_value());
        REQUIRE(getter.has_value());
        CHECK(fixture.linker.Method(*children_method).overridable);
        CHECK(fixture.linker.Method(*padding_method).overridable);
        CHECK(fixture.linker.Method(*getter).overridable);
        CHECK((fixture.linker.Method(*children_method).access_flags & kAccFinal) ==
              0);
        CHECK((fixture.linker.Method(*padding_method).access_flags & kAccFinal) ==
              0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto second = fixture.New(
            "Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto first_node = FindViewUiNode(*fixture.context, first.Value());
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(first_node.has_value());
        REQUIRE(second_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*first_node)->clip_children);
        CHECK(fixture.context->ui_tree.Get(*first_node)->clip_to_padding);
        CHECK(fixture.On(first, kGetClipChildren, kBool).AsInt() == 1);
        CHECK(fixture.On(second, kGetClipChildren, kBool).AsInt() == 1);

        fixture.context->ui_tree.ClearDrawDirty();
        fixture.context->ui_tree.ClearLayoutDirty();
        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(1)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(1)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->draw_dirty);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->layout_dirty);

        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK(fixture.On(first, kGetClipChildren, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clip_children);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clip_to_padding);
        CHECK(fixture.context->ui_tree.Get(*first_node)->draw_dirty);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->layout_dirty);
        CHECK(fixture.context->ui_tree.Get(*second_node)->clip_children);
        CHECK(fixture.context->ui_tree.Get(*second_node)->clip_to_padding);
        CHECK(fixture.On(second, kGetClipChildren, kBool).AsInt() == 1);

        fixture.context->ui_tree.ClearDrawDirty();
        fixture.On(first, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        fixture.On(first, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->draw_dirty);

        const auto object = fixture.New(
            "Ltest/ClipLinear;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto group_slot = fixture.linker.FindVtableIndex(
            group_type, kSetClipChildren, kBoolVoid);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, kSetClipChildren, kBoolVoid);
        REQUIRE(group_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*group_slot == *subclass_slot);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(object_class).vtable[*subclass_slot])
                  .owner == object_class);
        fixture.On(object, kSetClipChildren, kBoolVoid, {VmValue::Int(0)});
        CHECK(overrides == 1);
        const auto subclass_node =
            FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK_FALSE(fixture.context->ui_tree.Get(*subclass_node)->clip_children);
        CHECK(fixture.On(object, kGetClipChildren, kBool).AsInt() == 0);
        fixture.On(object, kSetClipToPadding, kBoolVoid, {VmValue::Int(0)});
        CHECK_FALSE(fixture.context->ui_tree.Get(*subclass_node)->clip_to_padding);
    }
}

TEST_CASE("DVM-179 View clickable state is overridable and shared with UiNode") {
    constexpr auto kSetClickable = "setClickable";
    constexpr auto kIsClickable = "isClickable";
    constexpr auto kSetListener = "setOnClickListener";
    constexpr auto kBoolVoid = "(Z)V";
    constexpr auto kBool = "()Z";
    constexpr auto kListener = "(Landroid/view/View$OnClickListener;)V";
    const auto find_virtual = [](const DexClassLinker& linker,
                                 const DexClassId type, const char* name,
                                 const char* signature) {
        std::optional<VmMethodId> found;
        for (const auto method : linker.Class(type).own_virtual_methods) {
            const auto& linked = linker.Method(method);
            if (linked.name == name && linked.descriptor == signature) {
                found = method;
                break;
            }
        }
        return found;
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t setters{};
        std::int32_t getters{};
        std::int32_t clicks{};
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/ClickableView;", "Landroid/view/View;");
        subclass.Constructor(
            "(Landroid/content/Context;)V",
            [](IntrinsicContext& call) {
                const auto view = call.vm.Linker().ResolveDescriptor(
                    "Landroid/view/View;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    view, "<init>", "(Landroid/content/Context;)V");
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        const auto call_super = [](IntrinsicContext& call, const char* name,
                                   const char* signature) {
            const auto view = call.vm.Linker().ResolveDescriptor(
                "Landroid/view/View;");
            std::optional<VmMethodId> inherited;
            for (const auto method :
                 call.vm.Linker().Class(view).own_virtual_methods) {
                const auto& linked = call.vm.Linker().Method(method);
                if (linked.name == name && linked.descriptor == signature) {
                    inherited = method;
                    break;
                }
            }
            std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
            arguments.insert(arguments.end(), call.arguments.begin(),
                             call.arguments.end());
            const auto outcome = call.vm.Call(*inherited, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    call.vm.Linker().Class(outcome.exception_class).descriptor,
                    outcome.exception_message, outcome.exception};
            }
            return outcome.value;
        };
        subclass.OverrideMethod(
            kSetClickable, kBoolVoid,
            [&setters, &call_super](IntrinsicContext& call) {
                ++setters;
                return call_super(call, kSetClickable, kBoolVoid);
            });
        subclass.OverrideMethod(
            kIsClickable, kBool,
            [&getters, &call_super](IntrinsicContext& call) {
                ++getters;
                return call_super(call, kIsClickable, kBool);
            });
        auto listener = IntrinsicClassBuilder::Class(
            "Ltest/CountClick;", "Ljava/lang/Object;",
            {"Landroid/view/View$OnClickListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) {
            return VmValue::Void();
        });
        listener.VirtualMethod(
            "onClick", "(Landroid/view/View;)V",
            [&clicks](IntrinsicContext&) {
                ++clicks;
                return VmValue::Void();
            });
        AndroidValueVm fixture(
            backend, {std::move(subclass).Build(), std::move(listener).Build()});
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        REQUIRE(find_virtual(fixture.linker, view_type, kSetClickable, kBoolVoid)
                    .has_value());
        REQUIRE(find_virtual(fixture.linker, view_type, kIsClickable, kBool)
                    .has_value());
        CHECK(fixture.linker.Method(
                          *find_virtual(fixture.linker, view_type, kSetClickable,
                                        kBoolVoid))
                  .overridable);
        CHECK((fixture.linker.Method(
                           *find_virtual(fixture.linker, view_type,
                                         kSetClickable, kBoolVoid))
                   .access_flags &
               kAccFinal) == 0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto first = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto second = fixture.New(
            "Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto first_node = FindViewUiNode(*fixture.context, first.Value());
        const auto second_node = FindViewUiNode(*fixture.context, second.Value());
        REQUIRE(first_node.has_value());
        REQUIRE(second_node.has_value());
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*first_node)->clickable);
        CHECK(fixture.On(second, kIsClickable, kBool).AsInt() == 0);

        fixture.On(first, kSetClickable, kBoolVoid, {VmValue::Int(1)});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 1);
        CHECK(fixture.context->ui_tree.Get(*first_node)->clickable);
        CHECK(fixture.On(second, kIsClickable, kBool).AsInt() == 0);
        CHECK_FALSE(fixture.context->ui_tree.Get(*second_node)->clickable);

        const auto callback = fixture.New("Ltest/CountClick;");
        fixture.On(first, kSetListener, kListener, {VmValue::Ref(callback)});
        CHECK(fixture.context->ui_tree.Get(*first_node)->clickable);
        REQUIRE(fixture.context->ui_click_listeners.contains(*first_node));
        fixture.On(first, kSetClickable, kBoolVoid, {VmValue::Int(0)});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 0);
        REQUIRE(fixture.context->ui_click_listeners.contains(*first_node));
        CHECK(fixture.context->ui_click_listeners.at(*first_node) == callback);
        CHECK_FALSE(InvokeViewOnClick(fixture.vm, *fixture.context, first.Value())
                        .has_value());
        CHECK(clicks == 1);

        fixture.On(first, kSetListener, kListener,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(fixture.On(first, kIsClickable, kBool).AsInt() == 1);
        CHECK_FALSE(fixture.context->ui_click_listeners.contains(*first_node));

        const auto object = fixture.New(
            "Ltest/ClickableView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, kSetClickable, kBoolVoid);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            object_class, kSetClickable, kBoolVoid);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        fixture.On(object, kSetListener, kListener,
                   {VmValue::Ref(VmObjectRef{})});
        CHECK(getters >= 1);
        CHECK(setters >= 1);
        CHECK(fixture.On(object, kIsClickable, kBool).AsInt() == 1);
        const auto subclass_node =
            FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(subclass_node.has_value());
        CHECK(fixture.context->ui_tree.Get(*subclass_node)->clickable);
    }
}

TEST_CASE("DVM-184 View.getContext preserves constructor and inflation identity") {
    constexpr auto kGetContext = "getContext";
    constexpr auto kContextSig = "()Landroid/content/Context;";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kTwoArg =
        "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    constexpr auto kStyleInit =
        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V";
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class(
            "Ltest/BurstlyView;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(
            kViewInit, [](IntrinsicContext& call) {
                const auto type = call.vm.Linker().ResolveDescriptor(
                    "Landroid/widget/LinearLayout;");
                const auto constructor = call.vm.Linker().FindDirectMethod(
                    type, "<init>", kViewInit);
                const std::array arguments{
                    VmValue::Ref(call.receiver), call.arguments[0]};
                const auto outcome = call.vm.Call(*constructor, arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto subclass_type =
            fixture.linker.ResolveDescriptor("Ltest/BurstlyView;");
        const auto view_slot = fixture.linker.FindVtableIndex(
            view_type, kGetContext, kContextSig);
        const auto subclass_slot = fixture.linker.FindVtableIndex(
            subclass_type, kGetContext, kContextSig);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(
            fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == view_type);
        CHECK_FALSE(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) != 0);

        bool found_context_field = false;
        for (const auto field :
             fixture.linker.Class(view_type).own_instance_fields) {
            const auto& linked = fixture.linker.Field(field);
            if (linked.name != "mContext") continue;
            found_context_field = true;
            CHECK(linked.descriptor == "Landroid/content/Context;");
            CHECK((linked.access_flags & kAccProtected) != 0);
        }
        CHECK(found_context_field);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto wrapper =
            fixture.New("Landroid/view/ContextThemeWrapper;");
        const auto other = fixture.New("Landroid/content/Context;");
        REQUIRE(activity != wrapper);
        REQUIRE(activity != other);

        const auto view = fixture.New(
            "Landroid/view/View;", kViewInit, {VmValue::Ref(activity)});
        const auto layout = fixture.New(
            "Landroid/widget/LinearLayout;", kTwoArg,
            {VmValue::Ref(wrapper), VmValue::Ref(VmObjectRef{})});
        const auto button = fixture.New(
            "Landroid/widget/Button;", kStyleInit,
            {VmValue::Ref(other), VmValue::Ref(VmObjectRef{}),
             VmValue::Int(0)});
        const auto burstly = fixture.New(
            "Ltest/BurstlyView;", kViewInit, {VmValue::Ref(activity)});
        CHECK(fixture.On(view, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(layout, kGetContext, kContextSig).ref == wrapper);
        CHECK(fixture.On(button, kGetContext, kContextSig).ref == other);
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(view, kGetContext, kContextSig).ref !=
              fixture.On(layout, kGetContext, kContextSig).ref);

        std::vector<ogplay::loader::BinaryXmlElement> elements(1);
        elements[0].name = "LinearLayout";
        const auto inflated = InflateUiElements(
            fixture.vm, *fixture.context, elements, activity);
        CHECK(fixture.On(inflated, kGetContext, kContextSig).ref == activity);

        const auto roots = fixture.vm.ProtectReferences(std::array{burstly});
        static_cast<void>(fixture.vm.CollectGarbage("dvm184-view-context"));
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
    }
}

TEST_CASE("View scroll bar style is inherited and preserves unrelated flags") {
    constexpr auto kSetStyle = "setScrollBarStyle";
    constexpr auto kSetStyleSig = "(I)V";
    constexpr auto kGetStyle = "getScrollBarStyle";
    constexpr auto kGetStyleSig = "()I";
    constexpr std::uint32_t kStyleMask = 0x03000000U;
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto view_type =
            fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto webview_type =
            fixture.linker.ResolveDescriptor("Landroid/webkit/WebView;");
        const auto setter = fixture.linker.FindVtableIndex(
            webview_type, kSetStyle, kSetStyleSig);
        const auto getter = fixture.linker.FindVtableIndex(
            webview_type, kGetStyle, kGetStyleSig);
        REQUIRE(setter.has_value());
        REQUIRE(getter.has_value());
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(webview_type).vtable[*setter]).owner ==
              view_type);
        CHECK(fixture.linker.Method(
                  fixture.linker.Class(webview_type).vtable[*setter])
                  .overridable);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto first = fixture.New(
            "Landroid/webkit/WebView;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        const auto second = fixture.New(
            "Landroid/view/View;", "(Landroid/content/Context;)V",
            {VmValue::Ref(activity)});
        CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() == 0);
        CHECK(fixture.On(second, kGetStyle, kGetStyleSig).AsInt() == 0);

        const auto flags_id = fixture.linker.FindFieldRecursive(
            view_type, "mViewFlags", "I");
        REQUIRE(flags_id.has_value());
        auto& flags = fixture.model.InstanceSlots(first)
                          [fixture.linker.Field(*flags_id).slot];
        flags.bits = 0x40000040U;
        flags.tag = SlotTag::cat1;
        for (const auto style :
             {0x00000000, 0x01000000, 0x02000000, 0x03000000}) {
            fixture.On(first, kSetStyle, kSetStyleSig,
                       {VmValue::Int(style)});
            CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() ==
                  style);
            CHECK((flags.bits & ~kStyleMask) == 0x40000040U);
            CHECK(fixture.On(second, kGetStyle, kGetStyleSig).AsInt() == 0);
        }
        fixture.On(first, kSetStyle, kSetStyleSig,
                   {VmValue::Int(0x7fffffff)});
        CHECK(fixture.On(first, kGetStyle, kGetStyleSig).AsInt() ==
              0x03000000);
        CHECK((flags.bits & ~kStyleMask) == 0x40000040U);
    }
}

TEST_CASE("DVM-306 sealed BootDex assembly fixes owner and platform facts before VM creation") {
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
        const auto bytes = std::as_bytes(std::span(raw));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const auto legacy = AssembleAndroidContext(
        read(std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework/bootdex.jar"), {true});
    CHECK(legacy->ui_traversal_owner == AndroidUiTraversalOwner::Legacy);
    CHECK(legacy->ui_tree.GeometryOwner() == ui::UiGeometryOwner::HostLayout);
    auto bytes = read(std::filesystem::path(OGPLAY_DEXVM_FIXTURE_DIR) / "view-measure.jar");
    const auto context = AssembleAndroidContext(bytes, {true});
    CHECK(context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal);
    CHECK(context->ui_tree.GeometryOwner() == ui::UiGeometryOwner::ExternalProjection);
    CHECK(context->platform_services.accessibility_service_absent);
    CHECK(context->UiAccessibilityUnavailable());
    CHECK_FALSE(context->ui_test_accessibility_disabled);
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        const auto assembled = AssembleAndroidContext(bytes, {true});
        DexClassLinker linker;
        linker.RegisterIntrinsics(BoundCoreIntrinsicCatalog(assembled));
        linker.RegisterIntrinsics(AndroidIntrinsicCatalog(assembled));
        ogplay::test::BindBootDexPlatformNatives(linker);
        const auto dex = ogplay::loader::ReadApkEntry(bytes, assembled->boot_classpath_archive, "classes.dex");
        std::vector<std::uint8_t> code(dex.size());
        std::memcpy(code.data(), dex.data(), dex.size());
        linker.RegisterBootDex(std::move(code), DexClassLinker::BootDexMemberSources(bytes));
        linker.Link();
        HostCopyJavaPayloadProvider strings_payload, arrays_payload;
        JniStringStore strings{strings_payload};
        JniPrimitiveArrayStore arrays{arrays_payload};
        JavaObjectModel model{strings, arrays};
        ogplay::core::CapabilityLedger ledger;
        Interpreter vm(linker, model, nullptr, ledger, {.backend = backend});
        const auto get = linker.FindDirectMethod(
            linker.ResolveDescriptor("Landroid/view/accessibility/AccessibilityManager;"), "getInstance",
            "(Landroid/content/Context;)Landroid/view/accessibility/AccessibilityManager;");
        REQUIRE(get);
        const auto owner = vm.NewIntrinsicInstance("Landroid/app/Activity;");
        const std::array arguments{VmValue::Ref(owner)};
        const auto manager = vm.Call(*get, arguments);
        REQUIRE_FALSE(manager.exception.IsValid());
        REQUIRE(manager.value.ref.IsValid());
        const auto manager_class = linker.ResolveDescriptor("Landroid/view/accessibility/AccessibilityManager;");
        const auto enabled = linker.FindVtableIndex(manager_class, "isEnabled", "()Z");
        REQUIRE(enabled);
        const std::array receiver{manager.value};
        const auto result = vm.Call(linker.Class(manager_class).vtable[*enabled], receiver);
        REQUIRE_FALSE(result.exception.IsValid());
        CHECK(result.value.AsInt() == 0);
        CHECK_FALSE(assembled->ui_test_accessibility_disabled);
        CHECK(ledger.Unimplemented().empty());
    }
    const auto undeclared = AssembleAndroidContext(bytes, {});
    CHECK_FALSE(undeclared->UiAccessibilityUnavailable());
    undeclared->ui_test_accessibility_disabled = true;
    CHECK(undeclared->UiAccessibilityUnavailable());
    static_assert(!std::is_assignable_v<decltype((context->ui_traversal_owner)), AndroidUiTraversalOwner>);
    static_assert(!std::is_assignable_v<decltype((context->platform_services)), AndroidPlatformServices>);
    const auto replace = [&](std::string_view from, std::string_view to) {
        REQUIRE(from.size() == to.size());
        std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::size_t offset = 0, count = 0;
        while ((offset = raw.find(from, offset)) != std::string::npos) {
            raw.replace(offset, from.size(), to);
            offset += to.size();
            ++count;
        }
        REQUIRE(count > 0);
        const auto result = std::as_bytes(std::span(raw));
        return std::vector<std::byte>(result.begin(), result.end());
    };
    CHECK_THROWS_WITH(static_cast<void>(AssembleAndroidContext(replace("java-traversal", "unknown-mode!!"), {true})),
                      "BootDex pairing: unknown mode/schema/version");
    CHECK_THROWS_WITH(static_cast<void>(AssembleAndroidContext(replace("\"version\":1", "\"version\":2"), {true})),
                      "BootDex pairing: unknown mode/schema/version");
    CHECK_THROWS_WITH(
        static_cast<void>(AssembleAndroidContext(replace("theme-projection.json", "theme-projectioX.json"), {true})),
        "BootDex pairing: missing META-INF/ogplay/theme-projection.json");
    CHECK_THROWS_WITH(static_cast<void>(AssembleAndroidContext(replace("vm-pairing.json", "vm-pairinX.json"), {true})),
                      "BootDex pairing: experimental payload lacks sealed mode; no Legacy fallback");
    // Stored payload mutation leaves the ZIP readable but must fail its sealed identity.
    const auto archive = ogplay::loader::ParseApkArchive(bytes);
    const auto offset = ogplay::loader::StoredApkEntryDataOffset(bytes, archive, "classes.dex");
    bytes.at(offset + 20) ^= std::byte{1};
    CHECK_THROWS_WITH(static_cast<void>(AssembleAndroidContext(bytes, {true})),
                      "BootDex pairing: identity mismatch: dex_sha256");
    CHECK(context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal);
}
