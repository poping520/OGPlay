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
