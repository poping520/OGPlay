#include <doctest/doctest.h>

#include "ogplay/runtime/ui/ui_tree.h"
#include "ogplay/runtime/ui/ui_renderer.h"

namespace ui = ogplay::runtime::ui;

TEST_CASE("DVM-235 window visibility preserves attachment and guest View visibility") {
    ui::UiTree tree;
    const auto window = tree.CreateNode(ui::UiClass::FrameLayout);
    const auto view = tree.CreateNode(ui::UiClass::Button);
    tree.Get(view)->focusable = true;
    tree.Attach(tree.Root(), window);
    tree.Attach(window, view);
    REQUIRE(tree.RequestFocus(view, false));
    tree.SetWindowVisible(window, false);
    CHECK(tree.IsAttached(view));
    CHECK_FALSE(tree.IsVisible(view));
    CHECK(tree.Get(view)->visibility == ui::Visibility::Visible);
    CHECK_FALSE(tree.Focused().has_value());
    CHECK_FALSE(tree.RequestFocus(view, false));
    tree.SetWindowVisible(window, true);
    CHECK(tree.IsVisible(view));
    CHECK(tree.RequestFocus(view, false));
    tree.SetVisibility(view, ui::Visibility::Invisible);
    tree.SetWindowVisible(window, false);
    tree.SetWindowVisible(window, true);
    CHECK_FALSE(tree.IsVisible(view));
    CHECK(tree.Get(view)->visibility == ui::Visibility::Invisible);
}

TEST_CASE("DVM-235 RelativeLayout edge pairs stretch and take precedence over centering") {
    ui::UiTree tree;
    const auto parent = tree.CreateNode(ui::UiClass::RelativeLayout);
    tree.Get(parent)->layout.width = {ui::SizeMode::MatchParent, 0};
    tree.Get(parent)->layout.height = {ui::SizeMode::MatchParent, 0};
    tree.Attach(tree.Root(), parent);
    const auto child = tree.CreateNode(ui::UiClass::View);
    tree.Get(child)->layout.width = {ui::SizeMode::Fixed, 10};
    tree.Get(child)->layout.height = {ui::SizeMode::Fixed, 12};
    tree.Get(child)->layout.relative.align_parent_left = true;
    tree.Get(child)->layout.relative.align_parent_right = true;
    tree.Get(child)->layout.relative.align_parent_top = true;
    tree.Get(child)->layout.relative.align_parent_bottom = true;
    tree.Get(child)->layout.relative.center_in_parent = true;
    tree.Get(child)->layout.margin = {3, 4, 5, 6};
    tree.Attach(parent, child);
    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(child)->frame == ui::Rect{3, 4, 95, 74});
    CHECK(tree.Get(child)->measured == ui::Size{92, 70});
    tree.Get(child)->layout.relative.align_parent_left = false;
    tree.Get(child)->layout.relative.align_parent_top = false;
    tree.MarkLayoutDirty(parent);
    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(child)->frame == ui::Rect{85, 62, 95, 74});
}

TEST_CASE("UI tree preserves hierarchy order and attached id lookup") {
    ui::UiTree tree;
    const auto parent = tree.CreateNode(ui::UiClass::FrameLayout);
    const auto first = tree.CreateNode(ui::UiClass::View);
    const auto second = tree.CreateNode(ui::UiClass::ImageButton);
    tree.SetAndroidId(first, 100);
    tree.SetAndroidId(second, 200);
    tree.Attach(tree.Root(), parent);
    tree.Attach(parent, second);
    tree.Attach(parent, first, 0);

    REQUIRE(tree.Get(parent) != nullptr);
    CHECK(tree.Get(parent)->children ==
          std::vector<ui::UiNodeId>{first, second});
    CHECK(tree.FindByAndroidId(100) == first);
    CHECK(tree.FindByAndroidId(200) == second);

    tree.Detach(first);
    CHECK_FALSE(tree.FindByAndroidId(100).has_value());
    CHECK(tree.Get(first) != nullptr);
    tree.Attach(parent, first);
    CHECK(tree.FindByAndroidId(100) == first);
}

TEST_CASE("UI tree brings a child to front without detaching it") {
    ui::UiTree tree;
    const auto parent = tree.CreateNode(ui::UiClass::FrameLayout);
    const auto first = tree.CreateNode(ui::UiClass::Button);
    const auto second = tree.CreateNode(ui::UiClass::TextView);
    const auto third = tree.CreateNode(ui::UiClass::View);
    const auto outsider = tree.CreateNode(ui::UiClass::View);
    tree.Get(first)->focusable = true;
    tree.SetAndroidId(first, 5);
    tree.SetAndroidId(third, 5); // duplicate id: lookup follows child order
    tree.Attach(tree.Root(), parent);
    for (const auto child : {first, second, third}) tree.Attach(parent, child);
    REQUIRE(tree.RequestFocus(first, false));
    CHECK(tree.FindByAndroidId(5) == first);
    tree.ClearLayoutDirty();
    tree.ClearDrawDirty();

    REQUIRE(tree.BringChildToFront(parent, first));
    CHECK(tree.Get(parent)->children ==
          std::vector<ui::UiNodeId>{second, third, first});
    CHECK(tree.Get(first)->parent == parent);
    CHECK(tree.IsAttached(first));
    CHECK(tree.Focused() == first);
    CHECK(tree.FindByAndroidId(5) == third);
    CHECK(tree.Get(tree.Root())->draw_dirty);
    CHECK_FALSE(tree.Get(tree.Root())->layout_dirty); // caller requests layout

    tree.ClearDrawDirty();
    CHECK_FALSE(tree.BringChildToFront(parent, outsider));
    CHECK_FALSE(tree.BringChildToFront(tree.Root(), first));
    CHECK(tree.Get(parent)->children ==
          std::vector<ui::UiNodeId>{second, third, first});
    CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
}

TEST_CASE("UI tree updates ids visibility and dirty propagation") {
    ui::UiTree tree;
    const auto child = tree.CreateNode(ui::UiClass::TextView);
    tree.Attach(tree.Root(), child);
    tree.ClearLayoutDirty();
    tree.ClearDrawDirty();

    tree.SetAndroidId(child, 7);
    CHECK(tree.FindByAndroidId(7) == child);
    tree.SetAndroidId(child, 8);
    CHECK_FALSE(tree.FindByAndroidId(7).has_value());
    CHECK(tree.FindByAndroidId(8) == child);

    tree.SetVisibility(child, ui::Visibility::Invisible);
    CHECK(tree.Get(child)->visibility == ui::Visibility::Invisible);
    CHECK_FALSE(tree.Get(child)->layout_dirty);
    CHECK(tree.Get(child)->draw_dirty);
    CHECK(tree.Get(tree.Root())->draw_dirty);

    tree.ClearLayoutDirty();
    tree.ClearDrawDirty();
    tree.SetVisibility(child, ui::Visibility::Gone);
    CHECK(tree.Get(child)->layout_dirty);
    CHECK(tree.Get(tree.Root())->layout_dirty);
    tree.SetVisibility(child, ui::Visibility::Visible);
    CHECK(tree.Get(child)->visibility == ui::Visibility::Visible);
}

TEST_CASE("empty text widget measurement retains one font line") {
    ui::UiTree tree;
    const auto edit = tree.CreateNode(ui::UiClass::EditText);
    tree.Get(edit)->layout.width = {ui::SizeMode::WrapContent, 0};
    tree.Get(edit)->layout.height = {ui::SizeMode::WrapContent, 0};
    tree.Get(edit)->text_size_px = 30.0F;
    tree.Attach(tree.Root(), edit);

    ui::LayoutUiTree(tree, {100, 100});
    CHECK(tree.Get(edit)->measured.height == 28);
}

TEST_CASE("UI tree destroy and generation reset invalidate old nodes") {
    ui::UiTree tree;
    const auto old_root = tree.Root();
    const auto parent = tree.CreateNode(ui::UiClass::LinearLayout);
    const auto child = tree.CreateNode(ui::UiClass::ImageView);
    tree.SetAndroidId(child, 42);
    tree.Attach(old_root, parent);
    tree.Attach(parent, child);

    tree.DestroySubtree(parent);
    CHECK(tree.Get(parent) == nullptr);
    CHECK(tree.Get(child) == nullptr);
    CHECK_FALSE(tree.FindByAndroidId(42).has_value());

    const auto generation = tree.Generation();
    tree.Reset();
    CHECK(tree.Generation() == generation + 1);
    CHECK(tree.Root() != old_root);
    CHECK(tree.Get(old_root) == nullptr);
    CHECK(tree.Size() == 1);
}

TEST_CASE("UI tree rejects invalid hierarchy mutations") {
    ui::UiTree tree;
    const auto parent = tree.CreateNode(ui::UiClass::FrameLayout);
    const auto child = tree.CreateNode(ui::UiClass::View);
    tree.Attach(tree.Root(), parent);
    tree.Attach(parent, child);
    CHECK_THROWS_WITH(tree.Attach(child, parent),
                      "UI tree child is already attached");
    CHECK_THROWS_WITH(tree.Attach(parent, tree.Root()),
                      "UI tree attachment would create a cycle");
    CHECK_THROWS_WITH(tree.Detach(tree.Root()),
                      "UI content root cannot be detached");
}

TEST_CASE("FrameLayout resolves fullscreen bottom center and overlap geometry") {
    ui::UiTree tree;
    const auto full = tree.CreateNode(ui::UiClass::VideoView);
    const auto bottom = tree.CreateNode(ui::UiClass::View);
    const auto centered = tree.CreateNode(ui::UiClass::View);
    tree.Get(full)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(full)->layout.height.mode = ui::SizeMode::MatchParent;
    tree.Get(bottom)->layout.width = {ui::SizeMode::Fixed, 40};
    tree.Get(bottom)->layout.height = {ui::SizeMode::Fixed, 20};
    tree.Get(bottom)->layout.layout_gravity = 0x51;
    tree.Get(centered)->layout.width = {ui::SizeMode::Fixed, 20};
    tree.Get(centered)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Get(centered)->layout.layout_gravity = 0x11;
    tree.Attach(tree.Root(), full);
    tree.Attach(tree.Root(), bottom);
    tree.Attach(tree.Root(), centered);

    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(full)->screen_frame == ui::Rect{0, 0, 100, 80});
    CHECK(tree.Get(bottom)->screen_frame == ui::Rect{30, 60, 70, 80});
    CHECK(tree.Get(centered)->screen_frame == ui::Rect{40, 35, 60, 45});
    CHECK(tree.Get(tree.Root())->children.back() == centered);
}

TEST_CASE("FrameLayout applies parent padding and child margins") {
    ui::UiTree tree;
    auto* root = tree.Get(tree.Root());
    root->padding = {5, 6, 7, 8};
    const auto child = tree.CreateNode(ui::UiClass::View);
    tree.Get(child)->layout.width = {ui::SizeMode::Fixed, 20};
    tree.Get(child)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Get(child)->layout.margin = {2, 3, 4, 5};
    tree.Get(child)->layout.layout_gravity = 0x55;
    tree.Attach(tree.Root(), child);

    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(child)->measured == ui::Size{20, 10});
    CHECK(tree.Get(child)->screen_frame == ui::Rect{69, 57, 89, 67});
    CHECK_FALSE(tree.Get(tree.Root())->layout_dirty);
}

TEST_CASE("FrameLayout wrap content uses intrinsic size within parent bounds") {
    ui::UiTree tree;
    const auto child = tree.CreateNode(ui::UiClass::ImageView);
    tree.Get(child)->intrinsic = {24, 16};
    tree.Get(child)->padding = {1, 2, 3, 4};
    tree.Attach(tree.Root(), child);
    ui::LayoutUiTree(tree, {20, 40});
    CHECK(tree.Get(child)->measured == ui::Size{20, 22});
}

TEST_CASE("horizontal LinearLayout removes GONE and preserves INVISIBLE space") {
    ui::UiTree tree;
    const auto row = tree.CreateNode(ui::UiClass::LinearLayout);
    tree.Get(row)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(row)->layout.height.mode = ui::SizeMode::WrapContent;
    tree.Get(row)->layout.layout_gravity = 0x50;
    tree.Get(row)->gravity = 0x01;
    const auto gone = tree.CreateNode(ui::UiClass::ImageButton);
    const auto invisible = tree.CreateNode(ui::UiClass::ImageButton);
    const auto visible = tree.CreateNode(ui::UiClass::ImageButton);
    for (const auto child : {gone, invisible, visible}) {
        tree.Get(child)->intrinsic = {20, 10};
        tree.Attach(row, child);
    }
    tree.SetVisibility(gone, ui::Visibility::Gone);
    tree.SetVisibility(invisible, ui::Visibility::Invisible);
    tree.Attach(tree.Root(), row);

    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(row)->screen_frame == ui::Rect{0, 70, 100, 80});
    CHECK(tree.Get(gone)->measured == ui::Size{});
    CHECK(tree.Get(invisible)->screen_frame == ui::Rect{30, 70, 50, 80});
    CHECK(tree.Get(visible)->screen_frame == ui::Rect{50, 70, 70, 80});

    tree.SetVisibility(invisible, ui::Visibility::Gone);
    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(visible)->screen_frame == ui::Rect{40, 70, 60, 80});
}

TEST_CASE("ScrollView measures document height while clipping to viewport") {
    ui::UiTree tree;
    const auto scroll = tree.CreateNode(ui::UiClass::ScrollView);
    tree.Get(scroll)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(scroll)->layout.height.mode = ui::SizeMode::MatchParent;
    const auto document = tree.CreateNode(ui::UiClass::LinearLayout);
    tree.Get(document)->orientation = ui::Orientation::Vertical;
    tree.Get(document)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(document)->layout.height.mode = ui::SizeMode::WrapContent;
    for (int index = 0; index < 3; ++index) {
        const auto child = tree.CreateNode(ui::UiClass::View);
        tree.Get(child)->layout.width.mode = ui::SizeMode::MatchParent;
        tree.Get(child)->layout.height = {ui::SizeMode::Fixed, 30};
        tree.Attach(document, child);
    }
    tree.Attach(scroll, document);
    tree.Attach(tree.Root(), scroll);
    ui::LayoutUiTree(tree, {100, 50});
    CHECK(tree.Get(scroll)->measured == ui::Size{100, 50});
    CHECK(tree.Get(document)->measured == ui::Size{100, 90});
    tree.Get(scroll)->scroll_y = 40;
    tree.MarkLayoutDirty(scroll);
    ui::LayoutUiTree(tree, {100, 50});
    CHECK(tree.Get(document)->screen_frame == ui::Rect{0, -40, 100, 50});
}

TEST_CASE("vertical LinearLayout distributes weight with padding and margins") {
    ui::UiTree tree;
    const auto column = tree.CreateNode(ui::UiClass::LinearLayout);
    auto* column_state = tree.Get(column);
    column_state->orientation = ui::Orientation::Vertical;
    column_state->layout.width.mode = ui::SizeMode::MatchParent;
    column_state->layout.height.mode = ui::SizeMode::MatchParent;
    column_state->padding = {5, 5, 5, 5};
    const auto first = tree.CreateNode(ui::UiClass::View);
    const auto second = tree.CreateNode(ui::UiClass::View);
    const auto fixed = tree.CreateNode(ui::UiClass::View);
    for (const auto child : {first, second, fixed}) {
        auto* state = tree.Get(child);
        state->layout.width.mode = ui::SizeMode::MatchParent;
        state->layout.margin = {2, 1, 3, 1};
        tree.Attach(column, child);
    }
    tree.Get(first)->layout.height = {ui::SizeMode::Fixed, 0};
    tree.Get(first)->layout.weight = 1.0F;
    tree.Get(second)->layout.height = {ui::SizeMode::Fixed, 0};
    tree.Get(second)->layout.weight = 2.0F;
    tree.Get(fixed)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Attach(tree.Root(), column);

    ui::LayoutUiTree(tree, {100, 100});
    CHECK(tree.Get(first)->screen_frame == ui::Rect{7, 6, 92, 30});
    CHECK(tree.Get(second)->screen_frame == ui::Rect{7, 32, 92, 82});
    CHECK(tree.Get(fixed)->screen_frame == ui::Rect{7, 84, 92, 94});
}

TEST_CASE("horizontal LinearLayout distributes weighted zero-width children") {
    ui::UiTree tree;
    const auto row = tree.CreateNode(ui::UiClass::LinearLayout);
    auto* row_state = tree.Get(row);
    row_state->layout.width.mode = ui::SizeMode::MatchParent;
    row_state->layout.height = {ui::SizeMode::Fixed, 20};
    row_state->padding = {5, 0, 5, 0};
    const auto first = tree.CreateNode(ui::UiClass::View);
    const auto second = tree.CreateNode(ui::UiClass::View);
    const auto fixed = tree.CreateNode(ui::UiClass::View);
    for (const auto child : {first, second, fixed}) {
        auto* state = tree.Get(child);
        state->layout.height.mode = ui::SizeMode::MatchParent;
        state->layout.margin.left = 1;
        state->layout.margin.right = 1;
        tree.Attach(row, child);
    }
    tree.Get(first)->layout.width = {ui::SizeMode::Fixed, 0};
    tree.Get(first)->layout.weight = 1.0F;
    tree.Get(second)->layout.width = {ui::SizeMode::Fixed, 0};
    tree.Get(second)->layout.weight = 1.0F;
    tree.Get(fixed)->layout.width = {ui::SizeMode::Fixed, 10};
    tree.Attach(tree.Root(), row);

    ui::LayoutUiTree(tree, {100, 20});
    CHECK(tree.Get(first)->screen_frame == ui::Rect{6, 0, 43, 20});
    CHECK(tree.Get(second)->screen_frame == ui::Rect{45, 0, 82, 20});
    CHECK(tree.Get(fixed)->screen_frame == ui::Rect{84, 0, 94, 20});

    tree.Get(first)->layout.weight = -1.0F;
    tree.MarkLayoutDirty(first);
    CHECK_THROWS_WITH(ui::LayoutUiTree(tree, {100, 20}),
                      "LinearLayout child weight must be finite and non-negative");
}

TEST_CASE("RelativeLayout resolves parent center and sibling edge rules") {
    ui::UiTree tree;
    const auto relative = tree.CreateNode(ui::UiClass::RelativeLayout);
    tree.Get(relative)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(relative)->layout.height.mode = ui::SizeMode::MatchParent;
    tree.Get(relative)->padding = {5, 5, 5, 5};
    const auto left = tree.CreateNode(ui::UiClass::View);
    const auto above = tree.CreateNode(ui::UiClass::View);
    const auto anchor = tree.CreateNode(ui::UiClass::View);
    const auto centered = tree.CreateNode(ui::UiClass::View);
    tree.SetAndroidId(anchor, 100);
    for (const auto child : {left, above, anchor, centered}) {
        tree.Attach(relative, child);
    }
    tree.Get(anchor)->layout.width = {ui::SizeMode::Fixed, 20};
    tree.Get(anchor)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Get(anchor)->layout.relative.align_parent_right = true;
    tree.Get(anchor)->layout.relative.align_parent_bottom = true;
    tree.Get(left)->layout.width = {ui::SizeMode::Fixed, 10};
    tree.Get(left)->layout.height = {ui::SizeMode::Fixed, 6};
    tree.Get(left)->layout.margin.right = 2;
    tree.Get(left)->layout.relative.left_of = 100;
    tree.Get(left)->layout.relative.align_bottom = 100;
    tree.Get(above)->layout.width = {ui::SizeMode::Fixed, 8};
    tree.Get(above)->layout.height = {ui::SizeMode::Fixed, 4};
    tree.Get(above)->layout.margin.bottom = 1;
    tree.Get(above)->layout.relative.align_left = 100;
    tree.Get(above)->layout.relative.above = 100;
    tree.Get(centered)->layout.width = {ui::SizeMode::Fixed, 10};
    tree.Get(centered)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Get(centered)->layout.relative.center_in_parent = true;
    tree.Attach(tree.Root(), relative);

    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(anchor)->screen_frame == ui::Rect{75, 65, 95, 75});
    CHECK(tree.Get(left)->screen_frame == ui::Rect{63, 69, 73, 75});
    CHECK(tree.Get(above)->screen_frame == ui::Rect{75, 60, 83, 64});
    CHECK(tree.Get(centered)->screen_frame == ui::Rect{45, 35, 55, 45});
}

TEST_CASE("RelativeLayout resolves dependencies independent of document order") {
    ui::UiTree tree;
    const auto relative = tree.CreateNode(ui::UiClass::RelativeLayout);
    tree.Get(relative)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(relative)->layout.height.mode = ui::SizeMode::MatchParent;
    const auto dependent = tree.CreateNode(ui::UiClass::View);
    const auto anchor = tree.CreateNode(ui::UiClass::View);
    tree.SetAndroidId(anchor, 7);
    tree.Get(anchor)->layout.width = {ui::SizeMode::Fixed, 20};
    tree.Get(anchor)->layout.height = {ui::SizeMode::Fixed, 10};
    tree.Get(anchor)->layout.relative.center_in_parent = true;
    tree.Get(dependent)->layout.width = {ui::SizeMode::Fixed, 5};
    tree.Get(dependent)->layout.height = {ui::SizeMode::Fixed, 5};
    tree.Get(dependent)->layout.relative.right_of = 7;
    tree.Get(dependent)->layout.relative.below = 7;
    tree.Attach(relative, dependent);
    tree.Attach(relative, anchor);
    tree.Attach(tree.Root(), relative);

    ui::LayoutUiTree(tree, {100, 80});
    CHECK(tree.Get(anchor)->screen_frame == ui::Rect{40, 35, 60, 45});
    CHECK(tree.Get(dependent)->screen_frame == ui::Rect{60, 45, 65, 50});
}

TEST_CASE("RelativeLayout fails deterministically on missing refs and cycles") {
    ui::UiTree missing;
    const auto missing_parent =
        missing.CreateNode(ui::UiClass::RelativeLayout);
    const auto missing_child = missing.CreateNode(ui::UiClass::View);
    missing.Get(missing_child)->layout.relative.below = 404;
    missing.Attach(missing_parent, missing_child);
    missing.Attach(missing.Root(), missing_parent);
    CHECK_THROWS_WITH(ui::LayoutUiTree(missing, {20, 20}),
                      "RelativeLayout rule references a missing sibling id");

    ui::UiTree cycle;
    const auto cycle_parent = cycle.CreateNode(ui::UiClass::RelativeLayout);
    const auto first = cycle.CreateNode(ui::UiClass::View);
    const auto second = cycle.CreateNode(ui::UiClass::View);
    cycle.SetAndroidId(first, 1);
    cycle.SetAndroidId(second, 2);
    cycle.Get(first)->layout.relative.below = 2;
    cycle.Get(second)->layout.relative.above = 1;
    cycle.Attach(cycle_parent, first);
    cycle.Attach(cycle_parent, second);
    cycle.Attach(cycle.Root(), cycle_parent);
    CHECK_THROWS_WITH(ui::LayoutUiTree(cycle, {20, 20}),
                      "RelativeLayout vertical dependency cycle");
}


TEST_CASE("DVM-247 detached subtree attachment enforces the total depth budget") {
    using namespace ogplay::runtime::ui;
    UiTree tree;
    const auto leaf = tree.CreateNode(UiClass::View);
    auto root = leaf;
    for (std::size_t depth = 1; depth < UiTree::kMaxDepth; ++depth) {
        const auto parent = tree.CreateNode(UiClass::FrameLayout);
        tree.Attach(parent, root); root = parent;
    }
    CHECK_THROWS_WITH(tree.Attach(tree.Root(), root), "UI tree depth limit exceeded");
    CHECK_FALSE(tree.Get(root)->parent.has_value());
    CHECK(tree.Get(tree.Root())->children.empty());
    const auto child = tree.Get(root)->children.front();
    tree.Detach(child);
    tree.Attach(tree.Root(), child);
    CHECK(tree.IsAttached(leaf));
}

namespace {

struct ProjectionFixture {
    ui::UiTree tree{ui::UiGeometryOwner::ExternalProjection};
    ui::UiNodeId parent = tree.CreateNode(ui::UiClass::FrameLayout);
    ui::UiNodeId leaf = tree.CreateNode(ui::UiClass::View);
    ui::UiProjectionTarget target{tree.Root(), 7, 11, 13, {100, 100}};

    ProjectionFixture() {
        tree.SetProjectionTarget(target);
        tree.SetAndroidId(leaf, 42);
        tree.Get(leaf)->clickable = true;
        tree.Get(leaf)->background_color = 0xff0000ffU;
    }

    ui::UiGeometryProjection Batch(std::uint64_t epoch = 1) const {
        return {target,
                epoch,
                {{tree.Root(), std::nullopt, {10, 20, 90, 80}, {80, 60}},
                 {parent, tree.Root(), {5, 6, 35, 36}, {30, 30}},
                 {leaf, parent, {-2, 4, 40, 20}, {42, 16}}}};
    }

    void Reject(const ui::UiGeometryProjection& batch) {
        const auto before = tree.PublishedProjection();
        const auto epoch = tree.ProjectionEpoch();
        std::vector<ui::UiNode> nodes;
        for (const auto id : {tree.Root(), parent, leaf})
            nodes.push_back(*tree.Get(id));
        const auto indexed = tree.FindByAndroidId(42);
        CHECK_THROWS(tree.CommitProjection(batch));
        CHECK(tree.PublishedProjection() == before);
        CHECK(tree.ProjectionEpoch() == epoch);
        CHECK(tree.FindByAndroidId(42) == indexed);
        for (const auto& old : nodes) {
            const auto& now = *tree.Get(old.id);
            CHECK(now.parent == old.parent);
            CHECK(now.children == old.children);
            CHECK(now.frame == old.frame);
            CHECK(now.screen_frame == old.screen_frame);
            CHECK(now.screen_clip == old.screen_clip);
            CHECK(now.measured == old.measured);
            CHECK(now.visibility == old.visibility);
            CHECK(now.padding == old.padding);
            CHECK(now.scroll_y == old.scroll_y);
            CHECK(now.clip_children == old.clip_children);
            CHECK(now.clip_to_padding == old.clip_to_padding);
            CHECK(now.layout_dirty == old.layout_dirty);
            CHECK(now.draw_dirty == old.draw_dirty);
        }
    }
};

}  // namespace

TEST_CASE("VD-02 complete projection commits hierarchy and geometry in one epoch") {
    ProjectionFixture f;
    CHECK_FALSE(f.tree.PublishedProjection());
    auto batch = f.Batch();
    batch.nodes[2].parent = f.tree.Root();
    // A duplicate near the end must not partially apply the earlier reparent.
    auto invalid = batch;
    invalid.nodes.push_back(invalid.nodes.back());
    f.Reject(invalid);
    CHECK_FALSE(f.tree.FindByAndroidId(42));
    f.tree.CommitProjection(batch);
    const auto first = f.tree.PublishedProjection();
    REQUIRE(first);
    CHECK(first->ProjectionEpoch() == 1);
    CHECK(first->Get(f.leaf)->parent == f.tree.Root());
    CHECK(first->Get(f.tree.Root())->children == std::vector<ui::UiNodeId>{f.parent, f.leaf});
    CHECK(first->FindByAndroidId(42) == f.leaf);
    CHECK_FALSE(first->Get(f.tree.Root())->layout_dirty);
    CHECK(first->Get(f.tree.Root())->draw_dirty);
    // Full replacement detaches omitted nodes and removes their id index entry.
    auto next = f.Batch(2);
    next.nodes.resize(2);
    f.tree.CommitProjection(next);
    CHECK_FALSE(f.tree.FindByAndroidId(42));
    CHECK_FALSE(f.tree.IsAttached(f.leaf));
    CHECK_FALSE(f.tree.Get(f.leaf)->parent);
    CHECK(first->FindByAndroidId(42) == f.leaf);
    CHECK(first->ProjectionEpoch() == 1);
    CHECK(f.tree.PublishedProjection()->ProjectionEpoch() == 2);
}

TEST_CASE("VD-06a projected identity retirement preserves the published epoch") {
    ProjectionFixture f;
    auto batch = f.Batch();
    batch.nodes[2].android_id = 77;
    f.tree.CommitProjection(batch);
    const auto before = f.tree.PublishedProjection();
    CHECK(before->FindByAndroidId(77) == f.leaf);
    CHECK_FALSE(before->FindByAndroidId(42));
    CHECK_THROWS(f.tree.RetireProjectedIdentity(f.tree.Root()));
    f.tree.RetireProjectedIdentity(f.leaf);
    CHECK(f.tree.Get(f.leaf) == nullptr);
    CHECK(f.tree.PublishedProjection() == before);
    CHECK(before->Get(f.leaf)->parent == f.parent);
    CHECK(before->FindByAndroidId(77) == f.leaf);
    auto next = f.Batch(2);
    CHECK_THROWS(f.tree.CommitProjection(next));
    CHECK(f.tree.PublishedProjection() == before);
    next.nodes.resize(2);
    f.tree.CommitProjection(next);
    CHECK_FALSE(f.tree.PublishedProjection()->FindByAndroidId(77));
    ui::UiTree legacy;
    CHECK_THROWS(legacy.RetireProjectedIdentity(legacy.CreateNode(ui::UiClass::View)));
}

TEST_CASE("VD-02 stale window content metrics and epoch batches change nothing") {
    ProjectionFixture f;
    f.tree.CommitProjection(f.Batch());
    for (const int field : {0, 1, 2, 3, 4, 5}) {
        auto batch = f.Batch(2);
        switch (field) {
        case 0: --batch.target.window_generation; break;
        case 1: --batch.target.content_revision; break;
        case 2: --batch.target.metrics_revision; break;
        case 3: ++batch.target.content_revision; break;
        case 4: ++batch.target.metrics.width; break;
        case 5: batch.target.root = f.parent; break;
        }
        f.Reject(batch);
    }
    f.Reject(f.Batch(0));
    f.Reject(f.Batch(1));
    // A target invalidation is independent of a traversal: old complete snapshot survives.
    const auto old = f.tree.PublishedProjection();
    auto next = f.target;
    ++next.content_revision;
    ++next.metrics_revision;
    next.metrics.width = 120;
    f.tree.SetProjectionTarget(next);
    f.Reject(f.Batch(2));
    auto valid = f.Batch(2);
    valid.target = next;
    f.tree.CommitProjection(valid);
    CHECK(f.tree.PublishedProjection() != old);
    CHECK(old->ProjectionTarget()->content_revision == f.target.content_revision);
    CHECK(f.tree.PublishedProjection()->ProjectionTarget()->metrics.width == 120);
    CHECK_THROWS(f.tree.SetProjectionTarget(f.target));
    ++next.window_generation;
    next.content_revision = 0;
    next.metrics_revision = 0;
    f.tree.SetProjectionTarget(next);
    f.Reject(valid);
    valid.target = next;
    valid.epoch = 3;
    f.tree.CommitProjection(valid);
    CHECK(f.tree.ProjectionEpoch() == 3);
    f.tree.Reset();
    CHECK_FALSE(f.tree.PublishedProjection());
    CHECK_THROWS(f.tree.CommitProjection(valid));
    CHECK(f.tree.ProjectionEpoch() == 0);
}

TEST_CASE("VD-02 invalid complete batches preserve all published and live state") {
    ProjectionFixture f;
    f.tree.CommitProjection(f.Batch());
    for (int fault = 0; fault < 11; ++fault) {
        auto batch = f.Batch(2);
        batch.nodes[0].frame.left = 3;  // Detect partial application before a later error.
        switch (fault) {
        case 0: batch.nodes[2].id = ui::UiNodeId{123}; break;
        case 1: batch.nodes[2].parent = ui::UiNodeId{456}; break;
        case 2: batch.nodes[1].parent = f.leaf; break;  // Disconnected cycle.
        case 3: batch.nodes[2].parent.reset(); break;
        case 4: batch.nodes[2].frame.right = -3; break;
        case 5: batch.nodes[2].measured.height = -1; break;
        case 6: batch.nodes[2].padding.left = -1; break;
        case 7: batch.nodes[2].visibility = static_cast<ui::Visibility>(255); break;
        case 8: batch.nodes.erase(batch.nodes.begin()); break;
        case 9: batch.nodes[2].frame = {2147483640, 0, 2147483647, 10}; break;
        case 10: batch.nodes[0].parent = f.leaf; break;
        }
        f.Reject(batch);
    }
    auto valid = f.Batch(2);
    valid.nodes[2].visibility = ui::Visibility::Invisible;
    f.tree.CommitProjection(valid);
    CHECK(f.tree.Get(f.leaf)->visibility == ui::Visibility::Invisible);
    CHECK_FALSE(f.tree.PublishedProjection()->HitTestProjection(20, 32));
}

TEST_CASE("VD-02 local frames scroll and padding resolve one screen geometry and clip") {
    ProjectionFixture f;
    auto batch = f.Batch();
    batch.nodes[0].padding = {2, 3, 4, 5};
    batch.nodes[0].scroll_y = 4;
    batch.nodes[1].padding = {3, 2, 4, 1};
    batch.nodes[1].scroll_y = 7;
    f.tree.CommitProjection(batch);
    auto snapshot = f.tree.PublishedProjection();
    CHECK(snapshot->Get(f.parent)->screen_frame == ui::Rect{15, 22, 45, 52});
    CHECK(snapshot->Get(f.leaf)->screen_frame == ui::Rect{13, 19, 55, 35});
    CHECK(snapshot->Get(f.leaf)->screen_clip == ui::Rect{18, 24, 41, 35});
    CHECK(snapshot->Get(f.leaf)->measured == ui::Size{42, 16});
    CHECK(snapshot->HitTestProjection(18, 24) == f.leaf);
    CHECK_FALSE(snapshot->HitTestProjection(17, 24));
    CHECK_FALSE(snapshot->HitTestProjection(41, 24));
    CHECK_FALSE(snapshot->HitTestProjection(18, 35));
    // Negative controls: removing padding clip and scroll changes the result.
    batch.epoch = 2;
    batch.nodes[1].clip_to_padding = false;
    batch.nodes[1].scroll_y = 0;
    f.tree.CommitProjection(batch);
    snapshot = f.tree.PublishedProjection();
    CHECK(snapshot->Get(f.leaf)->screen_frame == ui::Rect{13, 26, 55, 42});
    CHECK(snapshot->Get(f.leaf)->screen_clip == ui::Rect{15, 26, 45, 42});
    CHECK(snapshot->HitTestProjection(17, 30) == f.leaf);
}

TEST_CASE("VD-02 external owner rejects C++ layout and direct hierarchy mutation") {
    ProjectionFixture f;
    f.tree.CommitProjection(f.Batch());
    const auto old = *f.tree.Get(f.leaf);
    f.tree.MarkLayoutDirty(f.leaf);
    CHECK_THROWS_WITH(ui::LayoutUiTree(f.tree, {200, 300}), "LayoutUiTree cannot rearrange an external geometry owner");
    CHECK(f.tree.Get(f.leaf)->frame == old.frame);
    CHECK(f.tree.Get(f.leaf)->measured == old.measured);
    CHECK(f.tree.Get(f.leaf)->screen_frame == old.screen_frame);
    CHECK(f.tree.Get(f.tree.Root())->layout_dirty);
    CHECK_THROWS(f.tree.Attach(f.tree.Root(), f.leaf));
    CHECK_THROWS(f.tree.Detach(f.leaf));
    CHECK_THROWS(static_cast<void>(f.tree.BringChildToFront(f.parent, f.leaf)));
    CHECK_THROWS(f.tree.SetVisibility(f.leaf, ui::Visibility::Gone));
    ui::UiTree host;
    CHECK_NOTHROW(ui::LayoutUiTree(host, {200, 300}));
    CHECK(host.Get(host.Root())->measured == ui::Size{200, 300});
    auto batch = f.Batch();
    batch.target.root = host.Root();
    CHECK_THROWS(host.SetProjectionTarget(batch.target));
    CHECK_THROWS(host.CommitProjection(batch));
}

TEST_CASE("VD-02 render and hit test consume the same retained complete epoch") {
    ProjectionFixture f;
    CHECK_THROWS(static_cast<void>(ui::BuildUiRenderList(f.tree, {})));
    CHECK_THROWS(static_cast<void>(f.tree.HitTestProjection(20, 32)));
    f.tree.CommitProjection(f.Batch());
    const auto old = f.tree.PublishedProjection();
    REQUIRE(old);
    CHECK_THROWS(static_cast<void>(ui::BuildUiRenderList(f.tree, {})));
    const auto pixel = [](const ui::UiTree& snapshot, int x, int y) {
        const auto frame = ui::RasterizeUiOverlay(ui::BuildUiRenderList(snapshot, {}), {100, 100});
        return frame.rgba8[(static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) * 4 + 3];
    };
    CHECK(old->ProjectionEpoch() == 1);
    CHECK(old->HitTestProjection(20, 32) == f.leaf);
    CHECK(pixel(*old, 20, 32) == 255);
    CHECK_FALSE(old->HitTestProjection(14, 32));
    CHECK(pixel(*old, 14, 32) == 0);
    auto next = f.Batch(2);
    next.nodes[2].frame = {10, 12, 20, 22};
    f.tree.CommitProjection(next);
    const auto current = f.tree.PublishedProjection();
    CHECK(current->ProjectionEpoch() == 2);
    CHECK_FALSE(current->HitTestProjection(20, 32));
    CHECK(pixel(*current, 20, 32) == 0);
    CHECK(current->HitTestProjection(26, 39) == f.leaf);
    CHECK(pixel(*current, 26, 39) == 255);
    // Presenter retaining epoch 1 still sees all old nodes, clip and pixels.
    CHECK(old->HitTestProjection(20, 32) == f.leaf);
    CHECK(pixel(*old, 20, 32) == 255);
    auto invalid = next;
    invalid.epoch = 3;
    invalid.nodes[2].measured.width = -1;
    f.Reject(invalid);
    CHECK(f.tree.PublishedProjection()->ProjectionEpoch() == 2);
    CHECK(pixel(*current, 26, 39) == 255);
}

TEST_CASE("VD-02 hierarchy bounds reject excess depth and fanout before publishing") {
    for (const bool deep : {true, false}) {
        ProjectionFixture f;
        f.tree.CommitProjection(f.Batch());
        auto batch = f.Batch(2);
        batch.nodes.resize(1);
        auto parent = f.tree.Root();
        const auto limit = deep ? ui::UiTree::kMaxDepth : ui::UiTree::kMaxChildren;
        for (std::size_t i = 0; i < limit; ++i) {
            const auto id = f.tree.CreateNode(ui::UiClass::View);
            batch.nodes.push_back({id, parent, {0, 0, 1, 1}, {1, 1}});
            if (deep) parent = id;
        }
        if (!deep) {
            // Exactly kMaxChildren is valid; the next child is the negative control.
            f.tree.CommitProjection(batch);
            batch.epoch = 3;
            const auto extra = f.tree.CreateNode(ui::UiClass::View);
            batch.nodes.push_back({extra, parent, {0, 0, 1, 1}, {1, 1}});
        }
        f.Reject(batch);
        batch.nodes.pop_back();
        if (deep) {
            f.tree.CommitProjection(batch);
            CHECK(f.tree.ProjectionEpoch() == 2);
        }
    }
}
