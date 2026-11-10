#include "dexvm/resource_fixture.h"
#include "container_traversal_fixture.h"
#include <regex>
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
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("View edit mode defaults to false and preserves guest overrides") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", AndroidUiTraversalOwner::JavaTraversal);
        const auto context = f.New("Landroid/app/Activity;");
        const auto context_root = f.vm.ProtectReferences(std::array{context});
        const auto base = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(context)});
        CHECK(f.On(base, "isInEditMode", "()Z").AsInt() == 0);
        const auto leaf = f.New("Ltest/BaselineLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(context)});
        CHECK(f.On(leaf, "isInEditMode", "()Z").AsInt() == 1);
    }
}

TEST_CASE("DVM-279 ROM measure specs state cache and guest overrides use Java authority") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto sdk : {17U, 18U, 19U}) {
            AndroidValueVm f(backend, {}, false, "view_measure.dex", AndroidUiTraversalOwner::JavaTraversal);
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
        AndroidValueVm f(backend, {std::move(probe).Build()}, false, "view_layout.dex",
                         AndroidUiTraversalOwner::JavaTraversal);
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
        f.On(view, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
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

TEST_CASE("DVM-277 ROM View shape preserves adapters and Java-owned fields") {
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
        CHECK(fixture.On(view, "left", "()I").AsInt() == 0);
        CHECK(fixture.On(view, "writeLeft", "()I").AsInt() == 7);
        CHECK(fixture.On(view, "left", "()I").AsInt() == 7);
        CHECK(fixture.On(view, "getId", "()I").AsInt() == -1);
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

TEST_CASE("DVM-138 View.getParent follows the live UiTree hierarchy") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto first_parent =
            f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second_parent =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});

        const auto view_parent = f.linker.ResolveDescriptor("Landroid/view/ViewParent;");
        CHECK(f.linker.Class(view_parent).is_interface);
        CHECK(f.linker.IsAssignable(view_parent, f.model.ObjectClass(first_parent)));
        const auto method =
            f.linker.FindVtableIndex(f.model.ObjectClass(child), "getParent", "()Landroid/view/ViewParent;");
        REQUIRE(method.has_value());
        CHECK((f.linker.Method(f.linker.Class(f.model.ObjectClass(child)).vtable[*method]).access_flags &
               (kAccPublic | kAccFinal)) == (kAccPublic | kAccFinal));

        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(first_parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == first_parent);
        f.On(first_parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(second_parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == second_parent);
        f.On(second_parent, "removeViews", "(II)V", {VmValue::Int(0), VmValue::Int(1)});
        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());

        f.On(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(first_parent)});
        CHECK(f.On(first_parent, "getParent", "()Landroid/view/ViewParent;").ref ==
              f.On(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref);
    }
}

TEST_CASE("DVM-167 ViewGroup width-height add uses virtual default params") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto relative =
            f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});

        f.On(relative, "addView", "(Landroid/view/View;II)V",
             {VmValue::Ref(child), VmValue::Int(-1), VmValue::Int(-1)});
        const auto params = f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        REQUIRE(params.IsValid());
        CHECK(f.model.ObjectClass(params) ==
              f.linker.ResolveDescriptor("Landroid/widget/RelativeLayout$LayoutParams;"));
        const auto field = [&](const char* name) -> std::int32_t {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(params), name, "I");
            REQUIRE(id.has_value());
            return static_cast<std::int32_t>(f.model.InstanceSlots(params)[f.linker.Field(*id).slot].bits);
        };
        CHECK(field("width") == -1);
        CHECK(field("height") == -1);
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == relative);
    }
}

TEST_CASE("DVM-174 AnimationListener type compatibility links and dispatches") {
    constexpr auto kListener = "Landroid/view/animation/Animation$AnimationListener;";
    constexpr auto kAnimation = "Landroid/view/animation/Animation;";
    constexpr auto kSignature = "(Landroid/view/animation/Animation;)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t starts{};
        std::int32_t ends{};
        std::int32_t repeats{};
        auto implementor = IntrinsicClassBuilder::Class("Ltest/AnimationListener;", "Ljava/lang/Object;", {kListener});
        implementor.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        implementor.VirtualMethod("onAnimationStart", kSignature, [&starts](IntrinsicContext&) {
            ++starts;
            return VmValue::Void();
        });
        implementor.VirtualMethod("onAnimationEnd", kSignature, [&ends](IntrinsicContext&) {
            ++ends;
            return VmValue::Void();
        });
        implementor.VirtualMethod("onAnimationRepeat", kSignature, [&repeats](IntrinsicContext&) {
            ++repeats;
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(implementor).Build()});

        const auto listener = fixture.linker.ResolveDescriptor(kListener);
        const auto& listener_class = fixture.linker.Class(listener);
        CHECK(listener_class.is_boot_dex);
        CHECK(listener_class.is_interface);
        CHECK(listener_class.access_flags == (kAccPublic | kAccInterface | kAccAbstract));
        const auto animation_type = fixture.linker.FindClass(kAnimation);
        REQUIRE(animation_type.has_value());
        CHECK(fixture.linker.Class(*animation_type).is_boot_dex);
        CHECK((fixture.linker.Class(*animation_type).access_flags & kAccAbstract) != 0);
        CHECK(fixture.linker.IsAssignable(fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), *animation_type));

        std::vector<std::string> declared;
        for (const auto method : listener_class.own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            declared.push_back(linked.name);
            CHECK(linked.descriptor == kSignature);
            CHECK(linked.kind == MethodKind::abstract);
            CHECK(linked.declared_invoke_kind == DeclaredInvokeKind::interface_call);
            CHECK((linked.access_flags & (kAccPublic | kAccAbstract)) == (kAccPublic | kAccAbstract));
        }
        CHECK(declared == std::vector<std::string>{"onAnimationEnd", "onAnimationRepeat", "onAnimationStart"});

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

TEST_CASE("DVM-184 View.getContext preserves constructor and inflation identity") {
    constexpr auto kGetContext = "getContext";
    constexpr auto kContextSig = "()Landroid/content/Context;";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kTwoArg = "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    constexpr auto kStyleInit = "(Landroid/content/Context;Landroid/util/AttributeSet;I)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class("Ltest/BurstlyView;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(kViewInit, [](IntrinsicContext& call) {
            const auto type = call.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;");
            const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", kViewInit);
            const std::array arguments{VmValue::Ref(call.receiver), call.arguments[0]};
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            }
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto view_type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto subclass_type = fixture.linker.ResolveDescriptor("Ltest/BurstlyView;");
        const auto view_slot = fixture.linker.FindVtableIndex(view_type, kGetContext, kContextSig);
        const auto subclass_slot = fixture.linker.FindVtableIndex(subclass_type, kGetContext, kContextSig);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == view_type);
        CHECK_FALSE(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) != 0);

        bool found_context_field = false;
        for (const auto field : fixture.linker.Class(view_type).own_instance_fields) {
            const auto& linked = fixture.linker.Field(field);
            if (linked.name != "mContext") continue;
            found_context_field = true;
            CHECK(linked.descriptor == "Landroid/content/Context;");
            CHECK((linked.access_flags & kAccProtected) != 0);
        }
        CHECK(found_context_field);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto wrapper = fixture.New("Landroid/view/ContextThemeWrapper;");
        const auto other = fixture.New("Landroid/content/Context;");
        REQUIRE(activity != wrapper);
        REQUIRE(activity != other);

        const auto view = fixture.New("Landroid/view/View;", kViewInit, {VmValue::Ref(activity)});
        const auto layout =
            fixture.New("Landroid/widget/LinearLayout;", kTwoArg, {VmValue::Ref(wrapper), VmValue::Ref(VmObjectRef{})});
        const auto button = fixture.New("Landroid/widget/Button;", kStyleInit,
                                        {VmValue::Ref(other), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        const auto burstly = fixture.New("Ltest/BurstlyView;", kViewInit, {VmValue::Ref(activity)});
        CHECK(fixture.On(view, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(layout, kGetContext, kContextSig).ref == wrapper);
        CHECK(fixture.On(button, kGetContext, kContextSig).ref == other);
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(view, kGetContext, kContextSig).ref != fixture.On(layout, kGetContext, kContextSig).ref);

        const auto roots = fixture.vm.ProtectReferences(std::array{burstly});
        static_cast<void>(fixture.vm.CollectGarbage("dvm184-view-context"));
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
    }
}

TEST_CASE("DVM-306 boot classpath assembly fixes owner and platform facts before VM creation") {
    const auto framework = std::filesystem::path(OGPLAY_SOURCE_DIR) / "data/android/19/framework";
    const auto context = AssembleAndroidContext(ReadBootClasspath(framework), {true});
    CHECK(context->ui_traversal_owner == AndroidUiTraversalOwner::JavaTraversal);
    CHECK(context->ui_tree.GeometryOwner() == ui::UiGeometryOwner::ExternalProjection);
    CHECK(context->platform_services.accessibility_service_absent);
    CHECK(context->UiAccessibilityUnavailable());
    CHECK_FALSE(context->ui_test_accessibility_disabled);
    REQUIRE(context->boot_classpath.size() == kBootClasspathJars.size());
    CHECK(context->boot_classpath.front().guest_path == "/system/framework/ogplay.jar");
    CHECK(FindBootClasspathEntry(*context, "classes.dex") == &context->boot_classpath.front());
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        const auto assembled = AssembleAndroidContext(ReadBootClasspath(framework), {true});
        DexClassLinker linker;
        linker.RegisterIntrinsics(BoundCoreIntrinsicCatalog(assembled));
        linker.RegisterIntrinsics(AndroidIntrinsicCatalog(assembled));
        ogplay::test::BindBootDexPlatformNatives(linker);
        for (auto& dex : BootClasspathDexFiles(assembled->boot_classpath))
            linker.RegisterBootDex(std::move(dex));
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
    const auto undeclared = AssembleAndroidContext(ReadBootClasspath(framework), {});
    CHECK_FALSE(undeclared->UiAccessibilityUnavailable());
    undeclared->ui_test_accessibility_disabled = true;
    CHECK(undeclared->UiAccessibilityUnavailable());
    static_assert(!std::is_assignable_v<decltype((context->ui_traversal_owner)), AndroidUiTraversalOwner>);
    static_assert(!std::is_assignable_v<decltype((context->platform_services)), AndroidPlatformServices>);
    CHECK_THROWS_WITH(static_cast<void>(AssembleAndroidContext({}, {true})), "boot classpath is empty");
    auto broken = ReadBootClasspath(framework);
    broken.back().bytes.resize(16);
    CHECK_THROWS(static_cast<void>(AssembleAndroidContext(std::move(broken), {true})));
}

TEST_CASE("DVM-175 View.setOnClickListener is overridable and super registers the listener") {
    constexpr auto kSignature = "(Landroid/view/View$OnClickListener;)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t overrides{};
        VmObjectRef last_wrapper{};
        auto subclass = IntrinsicClassBuilder::Class("Ltest/WrappingView;", "Landroid/view/View;");
        subclass.Constructor("(Landroid/content/Context;)V", [](IntrinsicContext& call) {
            const auto view = call.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            const auto constructor = call.vm.Linker().FindDirectMethod(view, "<init>", "(Landroid/content/Context;)V");
            const std::array arguments{VmValue::Ref(call.receiver), call.arguments[0]};
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            }
            return VmValue::Void();
        });
        subclass.OverrideMethod("setOnClickListener", kSignature, [&overrides, &last_wrapper](IntrinsicContext& call) {
            ++overrides;
            const auto incoming = call.arguments[0].ref;
            last_wrapper = incoming.IsValid() ? call.vm.NewIntrinsicInstance("Ltest/ClickWrapper;") : VmObjectRef{};
            const auto view = call.vm.Linker().ResolveDescriptor("Landroid/view/View;");
            std::optional<VmMethodId> inherited;
            for (const auto method : call.vm.Linker().Class(view).own_virtual_methods) {
                const auto& linked = call.vm.Linker().Method(method);
                if (linked.name == "setOnClickListener" && linked.descriptor == kSignature) {
                    inherited = method;
                    break;
                }
            }
            const std::array arguments{VmValue::Ref(call.receiver), VmValue::Ref(last_wrapper)};
            const auto outcome = call.vm.Call(*inherited, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            }
            return VmValue::Void();
        });
        auto wrapper = IntrinsicClassBuilder::Class("Ltest/ClickWrapper;");
        wrapper.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        auto listener = IntrinsicClassBuilder::Class("Ltest/ClickListener;", "Ljava/lang/Object;",
                                                     {"Landroid/view/View$OnClickListener;"});
        listener.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        AndroidValueVm fixture(backend,
                               {std::move(subclass).Build(), std::move(wrapper).Build(), std::move(listener).Build()});

        const auto view_type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
        std::optional<VmMethodId> view_method;
        for (const auto method : fixture.linker.Class(view_type).own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            if (linked.name == "setOnClickListener" && linked.descriptor == kSignature) {
                view_method = method;
                break;
            }
        }
        REQUIRE(view_method.has_value());
        CHECK(fixture.linker.Method(*view_method).overridable);
        CHECK((fixture.linker.Method(*view_method).access_flags & kAccFinal) == 0);

        const auto context = fixture.New("Landroid/content/Context;");
        const auto object = fixture.New("Ltest/WrappingView;", "(Landroid/content/Context;)V", {VmValue::Ref(context)});
        const auto object_class = fixture.model.ObjectClass(object);
        const auto view_slot = fixture.linker.FindVtableIndex(view_type, "setOnClickListener", kSignature);
        const auto subclass_slot = fixture.linker.FindVtableIndex(object_class, "setOnClickListener", kSignature);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        CHECK(fixture.linker.Method(fixture.linker.Class(object_class).vtable[*subclass_slot]).owner == object_class);

        const auto callback = fixture.New("Ltest/ClickListener;");
        fixture.On(object, "setOnClickListener", kSignature, {VmValue::Ref(callback)});
        CHECK(overrides == 1);
        const auto node = FindViewUiNode(*fixture.context, object.Value());
        REQUIRE(node.has_value());
        CHECK(ReadAndroidViewListener(fixture.vm, *fixture.context, object, AndroidViewListener::Click) ==
              last_wrapper);
        CHECK(last_wrapper != callback);

        fixture.On(object, "setOnClickListener", kSignature, {VmValue::Ref(VmObjectRef{})});
        CHECK(overrides == 2);
        CHECK_FALSE(last_wrapper.IsValid());
        CHECK_FALSE(
            ReadAndroidViewListener(fixture.vm, *fixture.context, object, AndroidViewListener::Click).IsValid());
    }
}

TEST_CASE("DVM-371 View scrollbar style round-trips and animation state stays inert") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto web = f.New("Landroid/webkit/WebView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto parent =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{view, web, parent});
        for (const auto target : {view, web})
            for (const int style : {0x01000000, 0x02000000, 0x03000000, 0}) {
                f.On(target, "setScrollBarStyle", "(I)V", {VmValue::Int(style)});
                CHECK(f.On(target, "getScrollBarStyle", "()I").AsInt() == style);
            }
        f.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(web)});
        const auto animation_type = "Landroid/view/animation/Animation;";
        const auto animation =
            f.New("Landroid/view/animation/AlphaAnimation;", "(FF)V", {VmValue::Float(0.0F), VmValue::Float(1.0F)});
        const auto interpolator = f.New("Landroid/view/animation/LinearInterpolator;");
        const auto animation_roots = f.vm.ProtectReferences(std::array{animation, interpolator});
        f.On(animation, "setDuration", "(J)V", {VmValue::Long(500)});
        f.On(animation, "setInterpolator", "(Landroid/view/animation/Interpolator;)V", {VmValue::Ref(interpolator)});
        const auto get_animation = [&] {
            return f.On(web, "getAnimation", (std::string("()") + animation_type).c_str()).ref;
        };
        f.On(web, "clearAnimation", "()V");
        CHECK_FALSE(get_animation().IsValid());
        f.On(web, "startAnimation", (std::string("(") + animation_type + ")V").c_str(), {VmValue::Ref(animation)});
        CHECK(get_animation() == animation);
        f.On(web, "setVisibility", "(I)V", {VmValue::Int(0)});
        f.On(web, "invalidate", "()V");
        f.On(web, "clearAnimation", "()V");
        CHECK_FALSE(get_animation().IsValid());
        f.On(web, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-372 display-only setters warn and keep running while getters stay honest") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto paint = f.New("Landroid/graphics/Paint;");
        const auto roots = f.vm.ProtectReferences(std::array{view, text, paint});
        const auto warned = [&](std::string_view item) {
            const auto warnings = f.ledger.Warnings();
            return std::ranges::any_of(warnings, [&](const auto& hit) {
                return hit.id == std::string(ogplay::core::kUiDegradedWarningPrefix) + std::string(item);
            });
        };
        // ROM View keeps transform state; the projection ignores it with a warning.
        f.On(view, "setRotation", "(F)V", {VmValue::Float(45.0F)});
        CHECK(f.On(view, "getRotation", "()F").AsFloat() == 45.0F);
        // ROM Paint stores supported color state. Shadow geometry is retained with a display warning.
        f.On(paint, "setColor", "(I)V", {VmValue::Int(0x11223344)});
        CHECK(f.On(paint, "getColor", "()I").AsInt() == 0x11223344);
        f.On(paint, "setShadowLayer", "(FFFI)V",
             {VmValue::Float(2), VmValue::Float(1), VmValue::Float(1), VmValue::Int(0x11223344)});
        CHECK_FALSE(warned("Paint.shadowLayer"));
        // State pairs execute ROM.
        const auto tag = f.vm.NewStringUtf8("tag");
        const auto tag_root = f.vm.ProtectReferences(std::array{tag});
        f.On(view, "setTag", "(Ljava/lang/Object;)V", {VmValue::Ref(tag)});
        CHECK(f.On(view, "getTag", "()Ljava/lang/Object;").ref == tag);
        f.On(view, "setTag", "(ILjava/lang/Object;)V", {VmValue::Int(0x7f010001), VmValue::Ref(tag)});
        CHECK(f.On(view, "getTag", "(I)Ljava/lang/Object;", {VmValue::Int(0x7f010001)}).ref == tag);
        for (const bool enabled : {false, true}) {
            f.On(view, "setVerticalScrollBarEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isVerticalScrollBarEnabled", "()Z").AsInt() == enabled);
            f.On(text, "setEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(text, "isEnabled", "()Z").AsInt() == enabled);
        }
        CHECK_FALSE(warned("TextView.bufferType"));
        // A plain TextView accepts every BufferType; only the unsupported ones warn.
        const auto shown = [&] { return f.vm.StringUtf8(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref); };
        const auto buffer = [&](const char* name) {
            return f
                .Static("Landroid/widget/TextView$BufferType;", "valueOf",
                        "(Ljava/lang/String;)Landroid/widget/TextView$BufferType;",
                        {VmValue::Ref(f.vm.NewStringUtf8(name))})
                .ref;
        };
        const auto set_typed = "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V";
        const auto normal = buffer("NORMAL");
        f.On(text, "setText", set_typed, {VmValue::Ref(f.vm.NewStringUtf8("plain")), VmValue::Ref(normal)});
        CHECK(shown() == "plain");
        CHECK_FALSE(warned("TextView.bufferType"));
        const auto spannable = buffer("SPANNABLE");
        f.On(text, "setText", set_typed, {VmValue::Ref(f.vm.NewStringUtf8("spans")), VmValue::Ref(spannable)});
        CHECK(shown() == "spans");
        CHECK(warned("TextView.bufferType"));
        f.On(text, "setTextKeepState", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("kept"))});
        CHECK(shown() == "kept");
        // The standalone Matrix retains its native transform even when the View projection degrades transforms.
        const auto matrix = f.New("Landroid/graphics/Matrix;");
        f.On(matrix, "setScale", "(FF)V", {VmValue::Float(2.0F), VmValue::Float(2.0F)});
        CHECK(f.On(matrix, "isIdentity", "()Z").AsInt() == 0);
    }
}

TEST_CASE("DVM-363 View flags focus ids and ROM toString resolve actual resource identities") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto root = f.vm.ProtectReferences(std::array{view});
        for (const auto direction : {"Left", "Up", "Down", "Right"}) {
            const auto setter = std::string("setNextFocus") + direction + "Id";
            const auto getter = std::string("getNextFocus") + direction + "Id";
            for (int id : {123, -1, 456}) {
                f.On(view, setter.c_str(), "(I)V", {VmValue::Int(id)});
                CHECK(f.On(view, getter.c_str(), "()I").AsInt() == id);
            }
        }
        for (bool enabled : {false, true, false}) {
            f.On(view, "setSoundEffectsEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isSoundEffectsEnabled", "()Z").AsInt() == enabled);
            f.On(view, "setHorizontalScrollBarEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isHorizontalScrollBarEnabled", "()Z").AsInt() == enabled);
        }
        f.On(view, "layout", "(IIII)V", {VmValue::Int(1), VmValue::Int(2), VmValue::Int(30), VmValue::Int(40)});
        const auto identity =
            f.Static("Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", {VmValue::Ref(view)});
        const auto hex = f.model.StringValue(
            f.Static("Ljava/lang/Integer;", "toHexString", "(I)Ljava/lang/String;", {identity}).ref);
        const auto inspect = [&] { return f.vm.StringUtf8(f.On(view, "toString", "()Ljava/lang/String;").ref); };
        const auto plain = inspect();
        CHECK(plain.starts_with("android.view.View{" + f.vm.StringUtf8(f.model.NewString(hex)) + " "));
        CHECK(std::regex_match(
            plain,
            std::regex(
                R"(android\.view\.View\{[0-9a-f]+ [VIG.][F.][E.][D.][H.][V.][C.][L.] [R.][F.][S.][pP.][H.][A.][I.][D.] 1,2-30,40\})")));
        ogplay::test::ResourceFixture(*f.context).package_id = 0x7f;
        ogplay::test::ResourceFixture(*f.context).package_name = "fixture";
        ogplay::test::ResourceFixtureEntry entry;
        entry.resource_id = 0x7f0a0042;
        entry.type_name = "id";
        entry.entry_name = "player";
        ogplay::test::ResourceFixture(*f.context).entries.push_back(entry);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        for (const auto [id, suffix] :
             {std::pair{0x7f0a0042, " #7f0a0042 app:id/player}"},
              std::pair{0x01020002, " #1020002 android:id/content}"}, std::pair{0x7f0a0999, " #7f0a0999}"},
              std::pair{0x0102ffff, " #102ffff}"}, std::pair{0, " #0}"}}) {
            f.On(view, "setId", "(I)V", {VmValue::Int(id)});
            CHECK(inspect().ends_with(suffix));
        }
        CHECK(f.vm.StringUtf8(
                  f.On(resources, "getResourcePackageName", "(I)Ljava/lang/String;", {VmValue::Int(0x7f0a0042)}).ref) ==
              "fixture");
        CHECK(f.vm.StringUtf8(
                  f.On(resources, "getResourcePackageName", "(I)Ljava/lang/String;", {VmValue::Int(0x01020002)}).ref) ==
              "android");
        for (const auto name : {"getResourcePackageName", "getResourceTypeName", "getResourceEntryName"}) {
            const auto missing = f.OnOutcome(resources, name, "(I)Ljava/lang/String;", {VmValue::Int(0x0102ffff)});
            REQUIRE(missing.exception.IsValid());
            CHECK(f.linker.Class(missing.exception_class).descriptor ==
                  "Landroid/content/res/Resources$NotFoundException;");
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}
