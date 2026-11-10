#include "dexvm/resource_fixture.h"
#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include "../../src/runtime/integration/dexvm_android/shared.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include <doctest/doctest.h>
#include <array>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstring>
#include "ogplay/runtime/ui/ui_leaf_measure.h"
#include "ogplay/runtime/ui/ui_fallback_font.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {
// Stable recursive serialization of every guest layout fact, including dirty
// state. Retaining the published pointer alone would miss build-tree mutations.
std::string Geometry(const ui::UiTree& tree) {
    std::ostringstream out;
    out << tree.Generation() << ':' << tree.ProjectionEpoch() << ':' << tree.Size();
    const auto walk = [&](auto&& self, ui::UiNodeId id) -> void {
        const auto* n = tree.Get(id);
        REQUIRE(n);
        out << '/' << id.Value() << ':' << n->android_id << ':' << static_cast<int>(n->visibility) << ':'
            << n->measured.width << ':' << n->measured.height << ':' << n->layout_dirty << ':' << n->draw_dirty;
        for (auto rect : {n->frame, n->screen_frame, n->screen_clip})
            out << ':' << rect.left << ':' << rect.top << ':' << rect.right << ':' << rect.bottom;
        out << ':' << n->scroll_x << ':' << n->scroll_y << ':' << n->children.size();
        for (auto child : n->children)
            self(self, child);
    };
    walk(walk, tree.Root());
    return out.str();
}

template <typename Vm>
VmObjectRef Dialog(Vm& f, VmObjectRef owner, const char* message = "A real message",
                   VmObjectRef listener = VmObjectRef{}) {
    const auto builder =
        f.New("Landroid/app/AlertDialog$Builder;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
    const auto keep = f.vm.ProtectReferences(std::array{builder});
    f.On(builder, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
         {VmValue::Ref(f.vm.NewStringUtf8("Title"))});
    f.On(builder, "setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
         {VmValue::Ref(f.vm.NewStringUtf8(message))});
    for (const auto name : {"setPositiveButton", "setNegativeButton", "setNeutralButton"})
        f.On(
            builder, name,
            "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;",
            {VmValue::Ref(f.vm.NewStringUtf8("OK")), VmValue::Ref(listener)});
    return f.On(builder, "create", "()Landroid/app/AlertDialog;").ref;
}
}  // namespace

TEST_CASE("DVM-304 isolated host Dialog layout leaves guest geometry epoch and focus unchanged") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", AndroidUiTraversalOwner::JavaTraversal);
        f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        f.context->ui_test_accessibility_disabled = true;
        f.context->target_sdk_version = 19;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto root = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto keep = f.vm.ProtectReferences(std::array{owner, root, leaf});
        // Standalone Java root: this fixture tests modal isolation, not Window attachment.
        f.context->content_view = root;
        f.On(root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        f.On(leaf, "setFocusableInTouchMode", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(leaf, "requestFocus", "()Z").AsInt() == 1);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto before = Geometry(f.context->ui_tree);
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        const auto epoch = f.context->ui_measure_epoch;
        const auto measures = f.On(leaf, "count", "()I").AsInt();
        const auto layouts = f.On(root, "layouts", "()I").AsInt();
        const auto java_geometry = [&] {
            std::vector<std::int32_t> values;
            for (auto view : {root, leaf}) {
                IntrinsicContext call{f.vm, view, {}};
                IntrinsicCall fields(call);
                const auto& tokens = *f.context->view_java_measure_fields;
                for (auto token : tokens.measured)
                    values.push_back(fields.GetInt(token));
                for (auto token : tokens.frame)
                    values.push_back(fields.GetInt(token));
                for (auto token : tokens.flags)
                    values.push_back(fields.GetInt(token));
                for (auto token : tokens.old_specs)
                    values.push_back(fields.GetInt(token));
            }
            return values;
        };
        const auto java_before = java_geometry();
        const auto check_guest = [&] {
            CHECK(Geometry(f.context->ui_tree) == before);
            CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
            CHECK(f.context->ui_measure_epoch == epoch);
            CHECK(java_geometry() == java_before);
            CHECK(f.On(leaf, "count", "()I").AsInt() == measures);
            CHECK(f.On(root, "layouts", "()I").AsInt() == layouts);
            CHECK(f.On(root, "findFocus", "()Landroid/view/View;").ref == leaf);
            CHECK_FALSE(f.context->ui_tree.Focused());
        };
        const auto dialog = Dialog(f, owner);
        const auto dialog_keep = f.vm.ProtectReferences(std::array{dialog});
        f.On(dialog, "show", "()V");
        REQUIRE(f.context->dialog_presentations.size() == 1);
        const auto first = f.context->dialog_presentations.front();
        REQUIRE(first.host_tree);
        CHECK(first.host_tree->GeometryOwner() == ui::UiGeometryOwner::HostLayout);
        CHECK(first.partition != 0);
        CHECK(first.generation != 0);
        CHECK(first.revision <= f.context->dialog_host_revision);
        CHECK_FALSE(first.prior_focus);
        REQUIRE(first.prior_guest_focus);
        CHECK(first.prior_guest_focus->view == leaf);
        CHECK(first.prior_guest_focus->window_root == root);
        CHECK(first.prior_guest_focus->window_generation == f.context->ui_tree.Generation());
        REQUIRE(first.host_tree->Get(first.root));
        CHECK(first.host_tree->Get(first.root)->parent == first.host_tree->Root());
        for (const auto button : first.buttons) {
            REQUIRE(button);
            CHECK(first.host_tree->Get(*button)->text == u"OK");
            CHECK(first.host_tree->IsAttached(*button));
        }
        CHECK(first.host_tree->Focused().has_value());
        ui::UiOverlayRenderer renderer;
        const auto& frame = renderer.Render(*first.host_tree, f.context->ui_bitmaps, {800, 480});
        CHECK(frame.rgba8[3] >= 128);
        check_guest();
        CHECK(android_intrinsics::FocusAndroidDialog(*f.context, dialog, 1) < 0);
        check_guest();
        // A second tree deliberately reuses raw UiNodeId values. Its partition
        // disambiguates identity; focusing it must leave the first tree alone.
        const auto second_dialog = Dialog(f, owner);
        const auto second_keep = f.vm.ProtectReferences(std::array{second_dialog});
        f.On(second_dialog, "show", "()V");
        REQUIRE(f.context->dialog_presentations.size() == 2);
        const auto second = f.context->dialog_presentations.back();
        CHECK(second.root == first.root);
        CHECK(second.partition != first.partition);
        CHECK(second.host_tree != first.host_tree);
        const auto first_focus = first.host_tree->Focused();
        CHECK(android_intrinsics::FocusAndroidDialog(*f.context, second_dialog, 1) < 0);
        CHECK(first.host_tree->Focused() == first_focus);
        f.On(second_dialog, "dismiss", "()V");
        check_guest();
        // A successful update swaps only its own presentation.
        f.On(dialog, "setTitle", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Updated"))});
        const auto updated = f.context->dialog_presentations.front();
        CHECK(updated.host_tree != first.host_tree);
        CHECK(updated.partition != first.partition);
        CHECK(updated.generation > first.generation);
        CHECK(updated.prior_guest_focus->view == leaf);
        check_guest();
        // Long content now succeeds at the original viewport. Invalid metrics
        // still preserve the old tree, revision, focus and frozen geometry.
        f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
             {VmValue::Ref(f.vm.NewStringUtf8(std::string(700, 'M')))});
        CHECK(f.ledger.Unimplemented().empty());
        const auto retained = f.context->dialog_presentations.front();
        const auto old_focus = retained.host_tree->Focused();
        const auto old_geometry = Geometry(*retained.host_tree);
        const auto revision = f.context->dialog_host_revision;
        CHECK(f.ledger.Unimplemented().empty());
        f.context->surface_height = 47;
        const auto fail = f.OnOutcome(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
                                      {VmValue::Ref(f.vm.NewStringUtf8(std::string(200, 'M')))});
        f.context->surface_height = 480;
        REQUIRE(fail.exception.IsValid());
        CHECK(f.linker.Class(fail.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.context->dialog_presentations.front().host_tree == retained.host_tree);
        CHECK(f.context->dialog_host_revision == revision);
        CHECK(retained.host_tree->Focused() == old_focus);
        CHECK(Geometry(*retained.host_tree) == old_geometry);
        check_guest();
        CHECK_THROWS_AS(ui::LayoutUiTree(f.context->ui_tree, {800, 480}), std::exception);
        check_guest();
        // Host modal input consumes the published host partition without guest traversal.
        CHECK(DispatchAndroidDialogInput(
            f.vm, *f.context, {.type = AndroidBoundaryInputType::pointer_button, .x = 1, .y = 1, .action = 0}));
        check_guest();
        f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Small"))});
        check_guest();
        f.On(dialog, "hide", "()V");
        CHECK_FALSE(HasAndroidModalDialog(*f.context));
        check_guest();
        f.On(dialog, "show", "()V");
        CHECK(HasAndroidModalDialog(*f.context));
        check_guest();
        f.On(dialog, "dismiss", "()V");
        CHECK(f.context->dialog_presentations.empty());
        check_guest();
        // Retirement and release must never destroy a guest node whose raw ID
        // happens to collide with a host tree root.
        f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Small"))});
        f.On(dialog, "show", "()V");
        CHECK(f.context->dialog_presentations.front().generation > first.generation);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "runtime.ui.dialog.presentation");
        CHECK(hits.front().count == 1);  // Invalid viewport only.
        f.context->process_stopping = true;
        SynchronizeAndroidDialogs(*f.context);
        CHECK_FALSE(HasAndroidModalDialog(*f.context));
        ReleaseAndroidDialogs(*f.context);
        check_guest();
    }
}

TEST_CASE("DVM-305 presentation snapshot composes modal pixels input and atomic metrics without guest traversal") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::vector<int> clicks;
        std::vector<std::string> events;
        std::function<void(IntrinsicContext&)> click_hook;
        auto observer = IntrinsicClassBuilder::Class("LModalSnapshotObserver;", "Ljava/lang/Object;",
                                                     {"Landroid/content/DialogInterface$OnClickListener;",
                                                      "Landroid/content/DialogInterface$OnCancelListener;",
                                                      "Landroid/content/DialogInterface$OnDismissListener;"});
        observer.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        observer.VirtualMethod("onClick", "(Landroid/content/DialogInterface;I)V", [&](IntrinsicContext& c) {
            clicks.push_back(c.arguments[1].AsInt());
            if (click_hook) click_hook(c);
            return VmValue::Void();
        });
        for (const auto* name : {"onCancel", "onDismiss"})
            observer.VirtualMethod(name, "(Landroid/content/DialogInterface;)V", [&, name](IntrinsicContext&) {
                events.emplace_back(name);
                return VmValue::Void();
            });
        AndroidValueVm f(backend, {std::move(observer).Build()}, false, "view_measure.dex",
                         AndroidUiTraversalOwner::JavaTraversal);
        f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto root = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto listener = f.New("LModalSnapshotObserver;");
        const auto keep = f.vm.ProtectReferences(std::array{owner, root, leaf, listener});
        // Standalone Java root: this fixture tests modal isolation, not Window attachment.
        f.context->content_view = root;
        f.On(root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        f.On(leaf, "setFocusableInTouchMode", "(Z)V", {VmValue::Int(1)});
        REQUIRE(f.On(leaf, "requestFocus", "()Z").AsInt() == 1);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        auto guest = f.context->ui_tree.PublishedProjection();
        auto presentation = AcquireAndroidPresentation(*f.context);
        REQUIRE(presentation);
        CHECK(presentation->guest_window_epoch_refs == std::vector{guest});
        CHECK(presentation->modal_stack.empty());
        const auto before = Geometry(f.context->ui_tree);
        const auto epoch = f.context->ui_measure_epoch;
        const auto measures = f.On(leaf, "count", "()I").AsInt();
        const auto layouts = f.On(root, "layouts", "()I").AsInt();
        // No Surface is created or retired by presentation; all managed holder facts stay unchanged.
        const auto holders = f.context->active_surface_holders;
        const auto check_guest = [&] {
            CHECK(Geometry(f.context->ui_tree) == before);
            CHECK(f.context->ui_tree.PublishedProjection() == guest);
            CHECK(f.context->ui_measure_epoch == epoch);
            CHECK(f.On(leaf, "count", "()I").AsInt() == measures);
            CHECK(f.On(root, "layouts", "()I").AsInt() == layouts);
            CHECK(f.On(root, "findFocus", "()Landroid/view/View;").ref == leaf);
            CHECK(f.context->active_surface_holders == holders);
        };
        const auto pointer = [&](int action, float x, float y) {
            return DispatchAndroidDialogInput(f.vm, *f.context,
                                              {.type = AndroidBoundaryInputType::pointer_button,
                                               .x = x,
                                               .y = y,
                                               .pressed = action == 0,
                                               .action = action});
        };
        const auto key = [&](int action, int code, int flags = 0) {
            return DispatchAndroidDialogInput(f.vm, *f.context,
                                              {.type = AndroidBoundaryInputType::key,
                                               .code = code,
                                               .pressed = action == 0,
                                               .action = action,
                                               .flags = flags});
        };
        const auto pump = [&] { CHECK_FALSE(PumpJavaThreads(f.vm, *f.context).has_value()); };
        const auto dialog = Dialog(f, owner, "Message", listener);
        const auto dialog_keep = f.vm.ProtectReferences(std::array{dialog});
        f.On(dialog, "setOnCancelListener", "(Landroid/content/DialogInterface$OnCancelListener;)V",
             {VmValue::Ref(listener)});
        f.On(dialog, "setOnDismissListener", "(Landroid/content/DialogInterface$OnDismissListener;)V",
             {VmValue::Ref(listener)});
        for (std::size_t i = 0; i < 3; ++i) {
            f.On(dialog, "show", "()V");
            auto current = AcquireAndroidPresentation(*f.context);
            REQUIRE(current->modal_stack.size() == 1);
            CHECK(current->presentation_epoch > presentation->presentation_epoch);
            CHECK(current->host_modal_revision > presentation->host_modal_revision);
            CHECK(current->guest_window_epoch_refs == presentation->guest_window_epoch_refs);
            const auto& modal = current->modal_stack.back();
            REQUIRE(modal.buttons[i]);
            const auto rect = modal.tree->Get(*modal.buttons[i])->screen_frame;
            AndroidBoundaryFrame base;
            base.width = 800;
            base.height = 480;
            base.rgba8.resize(800 * 480 * 4, 0);
            const auto pixels = ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(base));
            const auto offset =
                (static_cast<std::size_t>(rect.top + 1) * 800 + static_cast<std::size_t>(rect.left + 1)) * 4;
            CHECK(pixels.rgba8[offset] == 0xdd);
            CHECK(pixels.rgba8[offset + 3] == 0xff);
            CHECK(AcquireAndroidPresentation(*f.context) == current);  // Compose only reads.
            CHECK(pointer(0, static_cast<float>(rect.left + 1), static_cast<float>(rect.top + 1)));
            REQUIRE(f.context->dialog_capture);
            CHECK(f.context->dialog_capture->partition == modal.partition);
            CHECK(pointer(1, static_cast<float>(rect.left + 1), static_cast<float>(rect.top + 1)));
            CHECK(clicks.size() == i);  // Java listener is queued, never inline.
            check_guest();
            pump();
            CHECK(clicks.back() == -static_cast<int>(i) - 1);
            CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
            CHECK(current->modal_stack.size() == 1);  // Retained snapshot remains immutable.
            check_guest();
            presentation = AcquireAndroidPresentation(*f.context);
        }
        // Capture cancelled by hide/update/dismiss; terminal events cannot click through.
        for (const auto* operation : {"hide", "update", "dismiss", "cancel"}) {
            f.On(dialog, "show", "()V");
            const auto old = AcquireAndroidPresentation(*f.context);
            const auto old_geometry = Geometry(*old->modal_stack.back().tree);
            const auto rect = old->modal_stack.back().tree->Get(*old->modal_stack.back().buttons[0])->screen_frame;
            CHECK(pointer(0, static_cast<float>(rect.left + 1), static_cast<float>(rect.top + 1)));
            if (std::string_view(operation) == "update")
                f.On(dialog, "setTitle", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Changed"))});
            else
                f.On(dialog, operation, "()V");
            CHECK_FALSE(f.context->dialog_capture);
            CHECK_FALSE(f.context->dialog_pointer_active);
            CHECK(pointer(1, static_cast<float>(rect.left + 1), static_cast<float>(rect.top + 1)));
            pump();
            CHECK(clicks.size() == 3);
            CHECK(Geometry(*old->modal_stack.back().tree) == old_geometry);
            f.On(dialog, "dismiss", "()V");
            check_guest();
        }
        f.On(dialog, "show", "()V");
        const auto pointer_snapshot = AcquireAndroidPresentation(*f.context);
        const auto button_rect = pointer_snapshot->modal_stack.back()
                                     .tree->Get(*pointer_snapshot->modal_stack.back().buttons[0])
                                     ->screen_frame;
        const auto bx = static_cast<float>(button_rect.left + 1), by = static_cast<float>(button_rect.top + 1);
        CHECK(pointer(0, bx, by));
        CHECK(pointer(2, 0, 0));
        CHECK(pointer(1, bx, by));
        pump();
        CHECK(clicks.size() == 3);  // Leaving a button cancels click eligibility.
        CHECK(pointer(0, bx, by));
        CHECK(pointer(3, bx, by));
        CHECK(pointer(1, bx, by));
        pump();
        CHECK(clicks.size() == 3);
        f.On(dialog, "dismiss", "()V");
        check_guest();
        // Topmost modal consumes outside touches and keys; raw node IDs collide safely.
        f.On(dialog, "show", "()V");
        const auto other = Dialog(f, owner, "Second", listener);
        const auto other_keep = f.vm.ProtectReferences(std::array{other});
        f.On(other, "show", "()V");
        auto both = AcquireAndroidPresentation(*f.context);
        REQUIRE(both->modal_stack.size() == 2);
        CHECK(both->modal_stack[0].buttons == both->modal_stack[1].buttons);
        CHECK(both->modal_stack[0].partition != both->modal_stack[1].partition);
        CHECK(pointer(0, 0, 0));
        CHECK(pointer(1, 0, 0));
        pump();
        CHECK(clicks.size() == 3);
        CHECK(key(0, 4));
        CHECK(key(1, 4, 0x20));  // Cancelled Back does not dismiss.
        CHECK(f.On(other, "isShowing", "()Z").AsInt() == 1);
        CHECK(key(0, 4));
        CHECK(key(1, 4));
        pump();
        CHECK(f.On(other, "isShowing", "()Z").AsInt() == 0);
        CHECK(f.On(dialog, "isShowing", "()Z").AsInt() == 1);
        CHECK(key(0, 61));
        CHECK(key(1, 61));  // Tab selects another host button.
        CHECK(key(0, 66));
        CHECK(key(1, 66));
        pump();
        CHECK(clicks.size() == 4);
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
        check_guest();
        // Owner hide/exit clears captures before publishing the visible stack.
        f.On(dialog, "show", "()V");
        f.context->activity_stack.push_back({});
        f.context->activity_stack.back().object = owner;
        f.context->activity_stack.back().visible = true;
        CHECK(pointer(0, 1, 1));
        f.context->activity_stack.back().visible = false;
        SynchronizeAndroidDialogs(*f.context);
        CHECK_FALSE(f.context->dialog_capture);
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
        CHECK(pointer(1, 1, 1));
        f.context->activity_stack.back().visible = true;
        SynchronizeAndroidDialogs(*f.context);
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.size() == 1);
        DismissAndroidActivityDialogs(f.vm, *f.context, owner);
        pump();
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
        check_guest();
        CHECK(f.ledger.Unimplemented().empty());
        // Callback tree mutations request their own owner traversal, not host input traversal.
        click_hook = [&](IntrinsicContext&) { f.On(leaf, "requestLayout", "()V"); };
        f.On(dialog, "show", "()V");
        CHECK(key(0, 66));
        CHECK(key(1, 66));
        CHECK(f.context->ui_measure_epoch == epoch);
        pump();
        CHECK(f.context->ui_measure_epoch == epoch);
        CHECK_FALSE(AndroidUiMeasureRequests(f.vm, *f.context).empty());
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        CHECK(f.context->ui_measure_epoch > epoch);
        click_hook = {};
        // Metrics publication waits for the guest partition; all host roots resize together.
        f.On(dialog, "show", "()V");
        f.On(other, "show", "()V");
        const auto old = AcquireAndroidPresentation(*f.context);
        const auto old_modal_geometry = Geometry(*old->modal_stack.back().tree);
        f.context->surface_width = 640;
        CHECK_FALSE(PublishAndroidPresentation(*f.context));
        CHECK(AcquireAndroidPresentation(*f.context) == old);
        // Owner visibility can publish a collection change at the last complete metrics.
        // It must not wait for resize or retain a hidden owner's modal pixels.
        f.context->activity_stack.back().visible = false;
        SynchronizeAndroidDialogs(*f.context);
        const auto hidden_during_resize = AcquireAndroidPresentation(*f.context);
        CHECK(hidden_during_resize->modal_stack.empty());
        CHECK(hidden_during_resize->viewport == ui::Size{800, 480});
        CHECK(hidden_during_resize->guest_window_epoch_refs == old->guest_window_epoch_refs);
        f.context->activity_stack.back().visible = true;
        SynchronizeAndroidDialogs(*f.context);
        // Restoring a previously hidden modal has no retained geometry in the hidden snapshot;
        // wait for the new guest metrics instead of publishing mismatched partitions.
        CHECK(AcquireAndroidPresentation(*f.context) == hidden_during_resize);
        AndroidBoundaryFrame mismatch;
        mismatch.width = 640;
        mismatch.height = 480;
        mismatch.rgba8.resize(640 * 480 * 4);
        CHECK_THROWS_AS(
            static_cast<void>(ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(mismatch))),
            std::runtime_error);
        CHECK(AcquireAndroidPresentation(*f.context) == hidden_during_resize);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto resized = AcquireAndroidPresentation(*f.context);
        CHECK(resized != old);
        CHECK(resized->viewport == ui::Size{640, 480});
        CHECK(resized->metrics_revision ==
              resized->guest_window_epoch_refs.front()->ProjectionTarget()->metrics_revision);
        for (const auto& modal : resized->modal_stack)
            CHECK(modal.tree->Get(modal.tree->Root())->measured == ui::Size{640, 480});
        CHECK(Geometry(*old->modal_stack.back().tree) == old_modal_geometry);
        // Too-small metrics fail after guest traversal; the complete old presentation survives.
        const auto old_host = f.context->dialog_presentations.front().host_tree;
        f.context->surface_height = 47;
        CHECK_THROWS_AS(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                 AndroidUiTraversalPhase::ResolveOnly),
                        VmJavaThrow);
        CHECK(AcquireAndroidPresentation(*f.context) == resized);
        CHECK(f.context->dialog_presentations.front().host_tree == old_host);
        CHECK(resized->modal_stack.size() == 2);
        // Input under mismatched metrics never dispatches stale geometry into Java.
        CHECK(pointer(0, 1, 1));
        CHECK(pointer(1, 1, 1));
        f.context->surface_height = 480;
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto recovered = AcquireAndroidPresentation(*f.context);
        CHECK(recovered->viewport == ui::Size{640, 480});
        // An over-budget title is truncated and warned about; the Dialog keeps showing.
        f.On(dialog, "setTitle", "(Ljava/lang/CharSequence;)V",
             {VmValue::Ref(f.vm.NewStringUtf8(std::string(513, 'X')))});
        CHECK(AcquireAndroidPresentation(*f.context) != recovered);
        std::vector<VmObjectRef> extra;
        for (int i = 0; i < 6; ++i) {
            extra.push_back(Dialog(f, owner));
            f.On(extra.back(), "show", "()V");
        }
        const auto extra_roots = f.vm.ProtectReferences(extra);
        const auto eight = AcquireAndroidPresentation(*f.context);
        CHECK(eight->modal_stack.size() == 8);
        const auto ninth = Dialog(f, owner);
        const auto ninth_keep = f.vm.ProtectReferences(std::array{ninth});
        CHECK(f.OnOutcome(ninth, "show", "()V").exception.IsValid());
        CHECK(AcquireAndroidPresentation(*f.context) == eight);
        CHECK(key(0, 4));
        f.context->process_stopping = true;
        SynchronizeAndroidDialogs(*f.context);
        CHECK_FALSE(f.context->dialog_capture);
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
        CHECK(key(1, 4));
        ReleaseAndroidDialogs(*f.context);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].id == "runtime.ui.dialog.presentation");
        CHECK(hits[0].count == 2);  // invalid resize metrics, ninth modal.
    }
}

TEST_CASE("DVM-305 modal presentation preserves a live managed Surface generation without GPU") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        f.context->ui_test_accessibility_disabled = true;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        ogplay::test::ResourceFixture(*f.context).package_id = 0x7f;
        ogplay::test::ResourceFixtureEntry theme;
        theme.resource_id = 0x7f020001;
        theme.type_name = "style";
        theme.entry_name = "surface";
        theme.is_complex = true;
        theme.parent = 0x01030005;
        for (auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U})
            theme.bag.push_back({id, 1, 0, {}});
        ogplay::test::ResourceFixture(*f.context).entries = {theme};
        f.context->application_theme = theme.resource_id;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<int>(theme.resource_id))});
        const auto view = f.New("Landroid/view/SurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto holder = f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = f.New("Ltest/ManagedSurfaceCallback;");
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, holder, callback});
        f.On(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        // Prepare the live-holder fixture directly; the created traversal phase belongs to VD-11-6.
        f.context->managed_host_surface_open = true;
        f.context->active_surface_holders.insert(holder.Value());
        android_intrinsics::PublishSurfaceHolderFrame(f.vm, *f.context, holder);
        f.On(callback, "surfaceCreated", "(Landroid/view/SurfaceHolder;)V", {VmValue::Ref(holder)});
        REQUIRE_FALSE(DispatchSurfaceHolderCallbacks(f.vm, *f.context, SurfaceHolderPhase::changed));
        const auto surface = f.On(holder, "getSurface", "()Landroid/view/Surface;").ref;
        const auto frame = f.On(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref;
        const auto surface_generation = f.On(surface, "getGenerationId", "()I").AsInt();
        const auto sequence_field = f.linker.FindFieldRecursive(f.model.ObjectClass(callback), "sequence", "I");
        REQUIRE(sequence_field);
        const auto sequence = f.model.InstanceSlots(callback)[f.linker.Field(*sequence_field).slot].bits;
        CHECK(sequence == 12);
        const auto geometry = Geometry(f.context->ui_tree);
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        const auto holders = f.context->active_surface_holders;
        const auto surfaces = f.context->holder_surfaces;
        const auto sizes = f.context->surface_callback_sizes;
        const auto dialog = Dialog(f, owner);
        const auto keep = f.vm.ProtectReferences(std::array{dialog, surface, frame});
        const auto unchanged = [&] {
            CHECK(Geometry(f.context->ui_tree) == geometry);
            CHECK(f.context->ui_tree.PublishedProjection() == snapshot);
            CHECK(f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref == holder);
            CHECK(f.On(holder, "getSurface", "()Landroid/view/Surface;").ref == surface);
            CHECK(f.On(surface, "getGenerationId", "()I").AsInt() == surface_generation);
            CHECK(f.On(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref == frame);
            CHECK(f.context->active_surface_holders == holders);
            CHECK(f.context->holder_surfaces == surfaces);
            CHECK(f.context->surface_callback_sizes == sizes);
            CHECK(f.model.InstanceSlots(callback)[f.linker.Field(*sequence_field).slot].bits == sequence);
        };
        f.On(dialog, "show", "()V");
        unchanged();
        f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Update"))});
        unchanged();
        AndroidBoundaryFrame base;
        base.width = 800;
        base.height = 480;
        base.rgba8.resize(800 * 480 * 4);
        CHECK(ogplay::session::ComposeAndroidPresentationFrame(*f.context, std::move(base)).rgba8[3] >= 128);
        unchanged();
        for (int action : {0, 1}) {
            CHECK(DispatchAndroidDialogInput(
                f.vm, *f.context,
                {.type = AndroidBoundaryInputType::key, .code = 4, .pressed = action == 0, .action = action}));
            unchanged();
        }
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
        unchanged();
        CHECK(AcquireAndroidPresentation(*f.context)->modal_stack.empty());
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-370 long AlertDialog messages scroll inside bounded chrome on both interpreters") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const ui::Size viewport : {ui::Size{1280, 720}, ui::Size{800, 480}}) {
            CAPTURE(static_cast<int>(backend));
            CAPTURE(viewport.width);
            CAPTURE(viewport.height);
            int clicks = 0;
            bool consume_key = false;
            auto observer = IntrinsicClassBuilder::Class("LScrollDialogObserver;", "Ljava/lang/Object;",
                                                         {"Landroid/content/DialogInterface$OnClickListener;",
                                                          "Landroid/content/DialogInterface$OnKeyListener;"});
            observer.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
            observer.VirtualMethod("onClick", "(Landroid/content/DialogInterface;I)V", [&](IntrinsicContext&) {
                ++clicks;
                return VmValue::Void();
            });
            observer.VirtualMethod("onKey", "(Landroid/content/DialogInterface;ILandroid/view/KeyEvent;)Z",
                                   [&](IntrinsicContext&) { return VmValue::Int(consume_key); });
            AndroidValueVm f(backend, {std::move(observer).Build()}, false, "view_measure.dex",
                             AndroidUiTraversalOwner::JavaTraversal);
            f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
            f.context->ui_test_accessibility_disabled = true;
            f.context->surface_width = static_cast<std::uint32_t>(viewport.width);
            f.context->surface_height = static_cast<std::uint32_t>(viewport.height);
            f.context->ui_density = 2.0F;
            const auto owner = f.New("Landroid/app/Activity;");
            const auto root = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto listener = f.New("LScrollDialogObserver;");
            const auto keep = f.vm.ProtectReferences(std::array{owner, root, listener});
            f.context->content_view = root;
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly));
            std::string message;
            while (message.size() < 700)
                message += "Read this neutral sample before continuing. ";
            message.resize(700);
            const auto dialog = Dialog(f, owner, message.c_str(), listener);
            const auto dialog_keep = f.vm.ProtectReferences(std::array{dialog});
            f.On(dialog, "show", "()V");
            REQUIRE(f.ledger.Unimplemented().empty());
            const auto current = [&] {
                REQUIRE(PublishAndroidPresentation(*f.context));
                return AcquireAndroidPresentation(*f.context);
            };
            const auto original = current();
            const auto& modal = original->modal_stack.back();
            REQUIRE(modal.scroll);
            const auto scroll_id = *modal.scroll;
            const auto message_id = modal.tree->Get(scroll_id)->children.front();
            const auto panel_id = f.context->dialog_presentations.front().panel;
            const auto panel = modal.tree->Get(panel_id)->screen_frame;
            const auto scroll_frame = modal.tree->Get(scroll_id)->screen_frame;
            CHECK(panel.top >= 8);
            CHECK(panel.bottom <= viewport.height - 8);
            REQUIRE(modal.tree->Get(message_id)->measured.height > (scroll_frame.bottom - scroll_frame.top));
            for (const auto id : modal.buttons) {
                REQUIRE(id);
                const auto bounds = modal.tree->Get(*id)->screen_frame;
                CHECK(bounds.top >= scroll_frame.bottom);
                CHECK(bounds.bottom <= panel.bottom);
                CHECK(bounds.left >= panel.left);
                CHECK(bounds.right <= panel.right);
            }
            const auto pixel_check = [&](const ui::UiTree& tree) {
                ui::UiTree blank(tree), actual(tree);
                blank.Get(message_id)->text_color = 0xffffffffU;
                blank.MarkDrawDirty(message_id);
                ui::UiOverlayRenderer renderer, blank_renderer;
                const auto pixels = renderer.Render(actual, {}, {viewport.width, viewport.height}).rgba8;
                const auto clean = blank_renderer.Render(blank, {}, {viewport.width, viewport.height}).rgba8;
                const auto clip = tree.Get(scroll_id)->screen_frame;
                int differences = 0;
                for (int y = 0; y < viewport.height; ++y)
                    for (int x = 0; x < viewport.width; ++x) {
                        const auto offset = (y * viewport.width + x) * 4;
                        if (std::equal(pixels.begin() + offset, pixels.begin() + offset + 4, clean.begin() + offset))
                            continue;
                        ++differences;
                        CHECK(x >= clip.left);
                        CHECK(x < clip.right);
                        CHECK(y >= clip.top);
                        CHECK(y < clip.bottom);
                    }
                CHECK(differences > 0);
            };
            pixel_check(*modal.tree);
            const auto pointer = [&](int action, float x, float y) {
                CHECK(DispatchAndroidDialogInput(
                    f.vm, *f.context,
                    {.type = AndroidBoundaryInputType::pointer_button, .x = x, .y = y, .action = action}));
                return current();
            };
            const float x = scroll_frame.left + 10, y = scroll_frame.top + 30;
            pointer(0, x, y);
            CHECK(pointer(2, x, y - 15)->modal_stack.back().tree->Get(scroll_id)->scroll_y == 0);
            const auto moved = pointer(2, x, y - 50);
            CHECK(moved->modal_stack.back().tree != modal.tree);
            CHECK(moved->modal_stack.back().tree->Get(scroll_id)->scroll_y == 50);
            CHECK(moved->modal_stack.back().tree->Get(message_id)->screen_frame.top ==
                  modal.tree->Get(message_id)->screen_frame.top - 50);
            CHECK(modal.tree->Get(scroll_id)->scroll_y == 0);
            pixel_check(*moved->modal_stack.back().tree);
            const auto bottom = pointer(2, x, -10000);
            const auto* scrolled = bottom->modal_stack.back().tree->Get(scroll_id);
            const auto limit =
                bottom->modal_stack.back().tree->Get(message_id)->measured.height - scrolled->measured.height;
            CHECK(scrolled->scroll_y == limit);
            CHECK(pointer(2, x, 10000)->modal_stack.back().tree->Get(scroll_id)->scroll_y == 0);
            pointer(1, x, 10000);
            CHECK(clicks == 0);
            const auto button = modal.tree->Get(*modal.buttons[0])->screen_frame;
            pointer(0, button.left + 5, button.top + 5);
            CHECK(pointer(2, x, y - 80)->modal_stack.back().tree->Get(scroll_id)->scroll_y == 0);
            pointer(1, x, y - 80);
            CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
            CHECK(clicks == 0);
            const auto key = [&](int code) {
                const auto event =
                    MakeAndroidKeyEvent(f.vm, {.type = AndroidBoundaryInputType::key, .code = code, .action = 0});
                const auto event_keep = f.vm.ProtectReferences(std::array{event});
                const auto handled =
                    f.On(dialog, "dispatchKeyEvent", "(Landroid/view/KeyEvent;)Z", {VmValue::Ref(event)}).AsInt();
                current();
                return handled;
            };
            CHECK(key(20) == 1);
            const auto step = (scroll_frame.bottom - scroll_frame.top) / 2;
            CHECK(current()->modal_stack.back().tree->Get(scroll_id)->scroll_y == std::min(step, limit));
            CHECK(key(19) == 1);
            CHECK(current()->modal_stack.back().tree->Get(scroll_id)->scroll_y == 0);
            f.On(dialog, "setOnKeyListener", "(Landroid/content/DialogInterface$OnKeyListener;)V",
                 {VmValue::Ref(listener)});
            consume_key = true;
            CHECK(key(20) == 1);
            CHECK(current()->modal_stack.back().tree->Get(scroll_id)->scroll_y == 0);
            consume_key = false;
            // Resize preserves the bottom offset, clamping to the new range.
            pointer(0, x, y);
            pointer(2, x, -10000);
            pointer(1, x, -10000);
            const auto before_resize = current();
            const auto before_geometry = Geometry(*before_resize->modal_stack.back().tree);
            f.context->surface_width = 1280;
            f.context->surface_height = 720;
            const auto traversed = EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                            AndroidUiTraversalPhase::ResolveOnly);
            if (viewport.width != 1280) REQUIRE(traversed);
            const auto resized = current();
            const auto* new_scroll = resized->modal_stack.back().tree->Get(scroll_id);
            const auto new_limit =
                resized->modal_stack.back().tree->Get(message_id)->measured.height - new_scroll->measured.height;
            CHECK(new_scroll->scroll_y == std::min(limit, new_limit));
            CHECK(Geometry(*before_resize->modal_stack.back().tree) == before_geometry);
            // Visible buttons still enqueue their listener and dismiss on the Looper.
            const auto clickable = resized->modal_stack.back().tree->Get(*modal.buttons[0])->screen_frame;
            pointer(0, clickable.left + 5, clickable.top + 5);
            pointer(1, clickable.left + 5, clickable.top + 5);
            CHECK(clicks == 0);
            CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
            CHECK(clicks == 1);
            f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("Short"))});
            f.On(dialog, "show", "()V");
            CHECK(key(20) == 0);
            CHECK(key(19) == 0);
            f.context->surface_width = static_cast<std::uint32_t>(viewport.width);
            f.context->surface_height = static_cast<std::uint32_t>(viewport.height);
            static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                       AndroidUiTraversalPhase::ResolveOnly));
            // Long labels use equal shares, two lines, and preserve the existing order.
            f.On(dialog, "setTitle", "(Ljava/lang/CharSequence;)V",
                 {VmValue::Ref(f.vm.NewStringUtf8(std::string(512, 'T')))});
            for (int which : {-1, -2, -3})
                f.On(dialog, "setButton",
                     "(ILjava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)V",
                     {VmValue::Int(which), VmValue::Ref(f.vm.NewStringUtf8(std::string(128, 'B'))),
                      VmValue::Ref(listener)});
            const auto long_labels = current()->modal_stack.back();
            const auto* title = long_labels.tree->Get(long_labels.tree->Get(panel_id)->children.front());
            CHECK(title->max_lines == 2);
            CHECK(title->measured.height <= 2 * 8 * 5 + 16);
            const auto long_panel = long_labels.tree->Get(panel_id)->screen_frame;
            int last_left = long_panel.right;
            for (const auto id : long_labels.buttons) {
                const auto* node = long_labels.tree->Get(*id);
                CHECK(node->max_lines == 2);
                CHECK(node->screen_frame.left >= long_panel.left);
                CHECK(node->screen_frame.right <= long_panel.right);
                CHECK(node->screen_frame.bottom <= long_panel.bottom);
                CHECK(node->screen_frame.left < last_left);
                last_left = node->screen_frame.left;
            }
            // The session's pinned API19 fallback chain also handles long mixed text.
            const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
            const auto load_font = [&](const char* name) {
                std::ifstream stream(repo / "data/android/19/fonts" / name, std::ios::binary);
                REQUIRE(stream.good());
                const std::vector<char> bytes{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
                std::vector<std::byte> result(bytes.size());
                std::memcpy(result.data(), bytes.data(), bytes.size());
                return result;
            };
            const auto roboto = load_font("Roboto-Regular.ttf"), droid = load_font("DroidSansFallback.ttf");
            f.context->ui_tree.SetFallbackFont(std::make_shared<ui::UiFallbackFont>(roboto, droid));
            f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
                 {VmValue::Ref(f.vm.NewStringUtf8(message + " ©"))});
            const auto mixed = current()->modal_stack.back().tree;
            CHECK(mixed->FallbackFont() == f.context->ui_tree.FallbackFont());
            CHECK(mixed->Get(message_id)->text.back() == u'\u00a9');
            CHECK(mixed->Get(message_id)->measured.height > mixed->Get(scroll_id)->measured.height);
            pixel_check(*mixed);
            f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
                 {VmValue::Ref(f.vm.NewStringUtf8(std::string(5000, 'M')))});
            CHECK(current()->modal_stack.back().tree->Get(message_id)->text.size() == 5000);
            // Even chrome alone exceeding the valid viewport degrades without UOE.
            f.context->surface_height = 48;
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly));
            const auto tiny = current();
            const auto tiny_bounds = tiny->modal_stack.back().tree->Get(panel_id)->screen_frame;
            CHECK(tiny_bounds.top >= 8);
            CHECK(tiny_bounds.bottom <= 40);
            CHECK(f.ledger.Unimplemented().empty());
            f.On(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
                 {VmValue::Ref(f.vm.NewStringUtf8(std::string(ui::kMaxFixedTextUnits + 1, 'M')))});
            CHECK(current() != tiny);
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}
