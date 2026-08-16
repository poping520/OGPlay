#include <doctest/doctest.h>

#include <array>

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

TEST_CASE("fixed UI font drives wrap-content measurement") {
    CHECK(ui::MeasureFixedText(u"Hi", 8.0F) ==
          ui::FixedTextMetrics{11, 7, 1});
    CHECK(ui::MeasureFixedText(u"Hi", 16.0F) ==
          ui::FixedTextMetrics{22, 14, 2});
    ui::UiTree tree;
    const auto text = tree.CreateNode(ui::UiClass::TextView);
    tree.Get(text)->text = u"HI";
    tree.Get(text)->padding = {1, 1, 1, 1};
    tree.Attach(tree.Root(), text);
    ui::LayoutUiTree(tree, {30, 20});
    CHECK(tree.Get(text)->measured == ui::Size{13, 9});

    tree.Get(text)->text = u"H";
    tree.MarkLayoutDirty(text);
    ui::LayoutUiTree(tree, {30, 20});
    CHECK(tree.Get(text)->measured == ui::Size{7, 9});

    CHECK_THROWS_WITH((void)ui::MeasureFixedText(u"two\nlines", 8.0F),
                      "multiline fixed-font text is unsupported");
    CHECK_THROWS_WITH((void)ui::MeasureFixedText(u"中", 8.0F),
                      "unsupported fixed-font glyph");
}

TEST_CASE("fixed UI font raster matches the exact HI golden") {
    ui::UiTree tree;
    const auto text = tree.CreateNode(ui::UiClass::TextView);
    tree.Get(text)->layout.width = {ui::SizeMode::Fixed, 11};
    tree.Get(text)->layout.height = {ui::SizeMode::Fixed, 7};
    tree.Get(text)->text = u"HI";
    tree.Get(text)->text_color = 0xff0000ffU;
    tree.Attach(tree.Root(), text);
    ui::LayoutUiTree(tree, {11, 7});
    const auto frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(tree, {}), {11, 7});

    constexpr std::array<std::string_view, 7> golden{
        "10001001110", "10001000100", "10001000100", "11111000100",
        "10001000100", "10001000100", "10001001110"};
    std::vector<std::uint8_t> expected(11U * 7U * 4U, 0U);
    for (std::size_t y = 0; y < golden.size(); ++y) {
        for (std::size_t x = 0; x < golden[y].size(); ++x) {
            if (golden[y][x] != '1') continue;
            const auto offset = (y * 11U + x) * 4U;
            expected[offset] = 255U;
            expected[offset + 3U] = 255U;
        }
    }
    CHECK(frame.rgba8 == expected);
}

TEST_CASE("Button owns deterministic background padding and text content") {
    ui::UiTree tree;
    const auto button = tree.CreateNode(ui::UiClass::Button);
    tree.Get(button)->text = u"GO";
    tree.Attach(tree.Root(), button);
    ui::LayoutUiTree(tree, {40, 20});
    CHECK(tree.Get(button)->measured == ui::Size{23, 15});
    const auto frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(tree, {}), {40, 20});
    CHECK(Pixel(frame, 0, 0) ==
          std::vector<std::uint8_t>{64, 64, 64, 255});
    CHECK(Pixel(frame, 6, 4) ==
          std::vector<std::uint8_t>{64, 64, 64, 255});
    CHECK(Pixel(frame, 7, 4) ==
          std::vector<std::uint8_t>{255, 255, 255, 255});
}
