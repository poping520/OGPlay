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
