#include <doctest/doctest.h>

#include "ogplay/runtime/ui/ui_tree.h"

namespace ui = ogplay::runtime::ui;

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

TEST_CASE("UI tree updates ids visibility and dirty propagation") {
    ui::UiTree tree;
    const auto child = tree.CreateNode(ui::UiClass::TextView);
    tree.Attach(tree.Root(), child);
    tree.ClearDirty();

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

    tree.ClearDirty();
    tree.SetVisibility(child, ui::Visibility::Gone);
    CHECK(tree.Get(child)->layout_dirty);
    CHECK(tree.Get(tree.Root())->layout_dirty);
    tree.SetVisibility(child, ui::Visibility::Visible);
    CHECK(tree.Get(child)->visibility == ui::Visibility::Visible);
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
