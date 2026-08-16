#include <doctest/doctest.h>

#include "ogplay/runtime/ui/ui_renderer.h"

namespace ui = ogplay::runtime::ui;

namespace {

[[nodiscard]] std::vector<std::uint8_t> Pixel(const ui::UiOverlayFrame& frame,
                                              const std::uint32_t x,
                                              const std::uint32_t y) {
    const auto offset = (static_cast<std::size_t>(y) * frame.width + x) * 4U;
    return {frame.rgba8.begin() + static_cast<std::ptrdiff_t>(offset),
            frame.rgba8.begin() + static_cast<std::ptrdiff_t>(offset + 4U)};
}

}  // namespace

TEST_CASE("UI overlay starts transparent and honors draw cache") {
    ui::UiTree tree;
    ui::LayoutUiTree(tree, {2, 2});
    ui::UiOverlayRenderer renderer;
    const ui::UiBitmapCache bitmaps;
    const auto& first = renderer.Render(tree, bitmaps, {2, 2});
    CHECK(first.rgba8 == std::vector<std::uint8_t>(16, 0));
    CHECK(renderer.BuildCount() == 1);
    static_cast<void>(renderer.Render(tree, bitmaps, {2, 2}));
    CHECK(renderer.BuildCount() == 1);
    tree.MarkDrawDirty(tree.Root());
    static_cast<void>(renderer.Render(tree, bitmaps, {2, 2}));
    CHECK(renderer.BuildCount() == 2);
}

TEST_CASE("UI overlay draws bitmap clips and document-order alpha") {
    ui::UiTree tree;
    const auto bitmap_node = tree.CreateNode(ui::UiClass::ImageButton);
    tree.Get(bitmap_node)->layout.width = {ui::SizeMode::Fixed, 2};
    tree.Get(bitmap_node)->layout.height = {ui::SizeMode::Fixed, 2};
    tree.Get(bitmap_node)->image_resource_id = 7;
    const auto top = tree.CreateNode(ui::UiClass::View);
    tree.Get(top)->layout.width = {ui::SizeMode::Fixed, 1};
    tree.Get(top)->layout.height = {ui::SizeMode::Fixed, 1};
    tree.Get(top)->background_color = 0x0000ffffU;
    tree.Get(top)->alpha = 0.5F;
    tree.Attach(tree.Root(), bitmap_node);
    tree.Attach(tree.Root(), top);
    ui::LayoutUiTree(tree, {2, 2});

    auto bitmap = std::make_shared<ui::UiBitmap>();
    bitmap->width = 2;
    bitmap->height = 2;
    bitmap->rgba8 = {255, 0, 0, 255, 0, 255, 0, 255,
                     255, 255, 255, 255, 0, 0, 0, 255};
    const ui::UiBitmapCache bitmaps{{7, bitmap}};
    const auto frame =
        ui::RasterizeUiOverlay(ui::BuildUiRenderList(tree, bitmaps), {2, 2});
    CHECK(Pixel(frame, 0, 0) ==
          std::vector<std::uint8_t>{127, 0, 128, 255});
    CHECK(Pixel(frame, 1, 0) ==
          std::vector<std::uint8_t>{0, 255, 0, 255});
    CHECK(Pixel(frame, 1, 1) ==
          std::vector<std::uint8_t>{0, 0, 0, 255});
}

TEST_CASE("UI overlay omits INVISIBLE and GONE nodes") {
    ui::UiTree tree;
    const auto invisible = tree.CreateNode(ui::UiClass::View);
    const auto gone = tree.CreateNode(ui::UiClass::View);
    for (const auto node : {invisible, gone}) {
        tree.Get(node)->layout.width = {ui::SizeMode::Fixed, 1};
        tree.Get(node)->layout.height = {ui::SizeMode::Fixed, 1};
        tree.Get(node)->background_color = 0xffffffffU;
        tree.Attach(tree.Root(), node);
    }
    tree.SetVisibility(invisible, ui::Visibility::Invisible);
    tree.SetVisibility(gone, ui::Visibility::Gone);
    ui::LayoutUiTree(tree, {1, 1});
    const auto frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(tree, {}), {1, 1});
    CHECK(frame.rgba8 == std::vector<std::uint8_t>(4, 0));
}
