#include "android_value_fixture.h"
#include "../../src/runtime/integration/dexvm_android/shared.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include <doctest/doctest.h>
#include <array>
#include <sstream>

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

VmObjectRef Dialog(AndroidValueVm& f, VmObjectRef owner, const char* message = "A real message") {
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
            {VmValue::Ref(f.vm.NewStringUtf8("OK")), VmValue::Ref(VmObjectRef{})});
    return f.On(builder, "create", "()Landroid/app/AlertDialog;").ref;
}
}  // namespace

TEST_CASE("DVM-304 isolated host Dialog layout leaves guest geometry epoch and focus unchanged") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend, {}, false, "view_measure.dex", false, AndroidUiTraversalOwner::JavaTraversal,
                         OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar");
        f.Static("Landroid/os/Looper;", "prepareMainLooper", "()V");
        f.context->ui_test_accessibility_disabled = true;
        f.context->target_sdk_version = 19;
        f.context->surface_width = 800;
        f.context->surface_height = 480;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto root = f.New("Ltest/HierarchyGroup;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto keep = f.vm.ProtectReferences(std::array{owner, root, leaf});
        f.context->activity = owner;
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
        CHECK(first.revision == f.context->dialog_host_revision);
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
        // Fail after measurement, not merely at input validation. The old tree,
        // revision, focus and retained old tree geometry must survive intact.
        const auto old_focus = updated.host_tree->Focused();
        const auto old_geometry = Geometry(*updated.host_tree);
        const auto revision = f.context->dialog_host_revision;
        CHECK(f.ledger.Unimplemented().empty());
        f.context->surface_height = 100;
        const auto fail = f.OnOutcome(dialog, "setMessage", "(Ljava/lang/CharSequence;)V",
                                      {VmValue::Ref(f.vm.NewStringUtf8(std::string(200, 'M')))});
        f.context->surface_height = 480;
        REQUIRE(fail.exception.IsValid());
        CHECK(f.linker.Class(fail.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.context->dialog_presentations.front().host_tree == updated.host_tree);
        CHECK(f.context->dialog_host_revision == revision);
        CHECK(updated.host_tree->Focused() == old_focus);
        CHECK(Geometry(*updated.host_tree) == old_geometry);
        check_guest();
        CHECK_THROWS_AS(ui::LayoutUiTree(f.context->ui_tree, {800, 480}), std::exception);
        check_guest();
        // Input composition is explicitly deferred to VD-10c-2, not credited
        // by routing these IDs into the guest tree or triggering traversal.
        CHECK_THROWS_AS(
            static_cast<void>(DispatchAndroidDialogInput(
                f.vm, *f.context, {.type = AndroidBoundaryInputType::pointer_button, .x = 1, .y = 1, .action = 0})),
            VmJavaThrow);
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
        CHECK(hits.front().count == 2);  // Oversized content and deferred input only.
        f.context->process_stopping = true;
        SynchronizeAndroidDialogs(*f.context);
        CHECK_FALSE(HasAndroidModalDialog(*f.context));
        ReleaseAndroidDialogs(*f.context);
        check_guest();
    }
}
