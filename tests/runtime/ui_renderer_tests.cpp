#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

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

TEST_CASE("layout preserves text draw dirty until overlay cache rebuild") {
    ui::UiTree tree;
    const auto text = tree.CreateNode(ui::UiClass::TextView);
    tree.Get(text)->text = u"A";
    tree.Attach(tree.Root(), text);
    ui::LayoutUiTree(tree, {30, 8});
    ui::UiOverlayRenderer renderer;
    const ui::UiBitmapCache bitmaps;
    const auto first = renderer.Render(tree, bitmaps, {30, 8});
    const auto first_pixels = first.rgba8;
    CHECK(renderer.BuildCount() == 1);

    tree.Get(text)->text = u"BBBB";
    tree.MarkLayoutDirty(text);
    ui::LayoutUiTree(tree, {30, 8});
    CHECK(tree.Get(tree.Root())->draw_dirty);
    const auto& second = renderer.Render(tree, bitmaps, {30, 8});
    CHECK(renderer.BuildCount() == 2);
    CHECK(second.rgba8 != first_pixels);
    CHECK(tree.Get(text)->measured.width == 23);
    CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
}

TEST_CASE("GONE layout mutation rebuilds cache and removes child pixels") {
    ui::UiTree tree;
    const auto child = tree.CreateNode(ui::UiClass::View);
    tree.Get(child)->layout.width = {ui::SizeMode::Fixed, 1};
    tree.Get(child)->layout.height = {ui::SizeMode::Fixed, 1};
    tree.Get(child)->background_color = 0xffffffffU;
    tree.Attach(tree.Root(), child);
    ui::LayoutUiTree(tree, {1, 1});
    ui::UiOverlayRenderer renderer;
    const ui::UiBitmapCache bitmaps;
    CHECK(renderer.Render(tree, bitmaps, {1, 1}).rgba8 ==
          std::vector<std::uint8_t>{255, 255, 255, 255});
    CHECK(renderer.BuildCount() == 1);

    tree.SetVisibility(child, ui::Visibility::Gone);
    ui::LayoutUiTree(tree, {1, 1});
    CHECK(tree.Get(tree.Root())->draw_dirty);
    CHECK(renderer.Render(tree, bitmaps, {1, 1}).rgba8 ==
          std::vector<std::uint8_t>(4, 0));
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

    CHECK(ui::MeasureFixedText(u"two\nlines", 8.0F) ==
          ui::FixedTextMetrics{29, 15, 1});
    CHECK(ui::WrapFixedText(u"one two", 8.0F, 0, 23, 4) ==
          u"one\ntwo");
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

TEST_CASE("ImageView scale types produce exact destinations and crop golden") {
    auto bitmap = std::make_shared<ui::UiBitmap>();
    bitmap->width = 2;
    bitmap->height = 1;
    bitmap->rgba8 = {255, 0, 0, 255, 0, 255, 0, 255};
    const ui::UiBitmapCache bitmaps{{9, bitmap}};
    const auto destination = [&bitmaps](const ui::ImageScaleType scale_type) {
        ui::UiTree tree;
        const auto image = tree.CreateNode(ui::UiClass::ImageView);
        tree.Get(image)->layout.width = {ui::SizeMode::Fixed, 4};
        tree.Get(image)->layout.height = {ui::SizeMode::Fixed, 4};
        tree.Get(image)->image_resource_id = 9;
        tree.Get(image)->image_scale_type = scale_type;
        tree.Attach(tree.Root(), image);
        ui::LayoutUiTree(tree, {4, 4});
        const auto commands = ui::BuildUiRenderList(tree, bitmaps);
        for (const auto& command : commands) {
            if (const auto* draw = std::get_if<ui::DrawBitmap>(&command)) {
                return draw->rect;
            }
        }
        throw std::runtime_error("missing bitmap command");
    };
    CHECK(destination(ui::ImageScaleType::Center) == ui::Rect{1, 2, 3, 3});
    CHECK(destination(ui::ImageScaleType::CenterInside) ==
          ui::Rect{1, 2, 3, 3});
    CHECK(destination(ui::ImageScaleType::FitCenter) ==
          ui::Rect{0, 1, 4, 3});
    CHECK(destination(ui::ImageScaleType::FitXy) == ui::Rect{0, 0, 4, 4});
    CHECK(destination(ui::ImageScaleType::CenterCrop) ==
          ui::Rect{-1, 0, 7, 4});

    ui::UiTree crop_tree;
    const auto crop = crop_tree.CreateNode(ui::UiClass::ImageView);
    crop_tree.Get(crop)->layout.width = {ui::SizeMode::Fixed, 4};
    crop_tree.Get(crop)->layout.height = {ui::SizeMode::Fixed, 4};
    crop_tree.Get(crop)->image_resource_id = 9;
    crop_tree.Get(crop)->image_scale_type = ui::ImageScaleType::CenterCrop;
    crop_tree.Attach(crop_tree.Root(), crop);
    ui::LayoutUiTree(crop_tree, {4, 4});
    const auto frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(crop_tree, bitmaps), {4, 4});
    for (std::uint32_t y = 0; y < 4; ++y) {
        CHECK(Pixel(frame, 0, y) ==
              std::vector<std::uint8_t>{255, 0, 0, 255});
        CHECK(Pixel(frame, 1, y) ==
              std::vector<std::uint8_t>{255, 0, 0, 255});
        CHECK(Pixel(frame, 2, y) ==
              std::vector<std::uint8_t>{255, 0, 0, 255});
        CHECK(Pixel(frame, 3, y) ==
              std::vector<std::uint8_t>{0, 255, 0, 255});
    }
}

TEST_CASE("compound drawables rasterize and inset the text band") {
    ui::UiTree tree;
    const auto button = tree.CreateNode(ui::UiClass::Button);
    tree.Get(button)->text = u"AB";
    tree.Get(button)->text_color = 0xffffffffU;
    tree.Get(button)->compound_drawables[0] = {7, 2, 2};
    tree.Attach(tree.Root(), button);
    ui::LayoutUiTree(tree, {30, 20});
    // "AB" measures 11 px; with the 2 px left drawable and the button's
    // default 6/4 padding the wrap-content width is 11 + 2 + 12.
    CHECK(tree.Get(button)->measured.width == 25);

    auto red = std::make_shared<const ui::UiBitmap>(
        ui::UiBitmap{2, 2, {255, 0, 0, 255, 255, 0, 0, 255,
                            255, 0, 0, 255, 255, 0, 0, 255}});
    const ui::UiBitmapCache bitmaps{{7, red}};
    ui::UiOverlayRenderer renderer;
    const auto& frame = renderer.Render(tree, bitmaps, {30, 20});
    // Content box: y 4..11. The 2 px drawable is vertically centered in the
    // 7 px content height: rows 6..7 (AOSP integer division truncates).
    CHECK(Pixel(frame, 6, 6) == std::vector<std::uint8_t>{255, 0, 0, 255});
    CHECK(Pixel(frame, 6, 8) != std::vector<std::uint8_t>{255, 0, 0, 255});
    CHECK(Pixel(frame, 5, 7) != std::vector<std::uint8_t>{255, 0, 0, 255});
    // The text band starts after the drawable; row 3 of "A" is full width.
    CHECK(Pixel(frame, 8, 7) == std::vector<std::uint8_t>{255, 255, 255, 255});
}

TEST_CASE("UI overlay preserves nine-patch fixed edges while stretching center") {
    ui::UiTree tree;
    const auto view = tree.CreateNode(ui::UiClass::View);
    tree.Get(view)->background_resource_id = 9;
    tree.Attach(tree.Root(), view);
    auto bitmap = std::make_shared<ui::UiBitmap>();
    bitmap->width = 3;
    bitmap->height = 1;
    bitmap->rgba8 = {255, 0, 0, 255, 0, 255, 0, 255,
                     0, 0, 255, 255};
    bitmap->nine_patch = true;
    bitmap->stretch_x = {1, 2};
    bitmap->stretch_y = {0, 1};
    ui::LayoutUiTree(tree, {7, 1});
    ui::UiOverlayRenderer renderer;
    const auto& frame = renderer.Render(tree, {{9, bitmap}}, {7, 1});
    CHECK(Pixel(frame, 0, 0) == std::vector<std::uint8_t>{255, 0, 0, 255});
    CHECK(Pixel(frame, 3, 0) == std::vector<std::uint8_t>{0, 255, 0, 255});
    CHECK(Pixel(frame, 6, 0) == std::vector<std::uint8_t>{0, 0, 255, 255});
}

TEST_CASE("DVM-123 compound drawables measure all axes and empty text") {
    ui::UiTree tree;
    const auto view = tree.CreateNode(ui::UiClass::TextView);
    auto* node = tree.Get(view);
    node->text = u"A";
    node->compound_drawables = {{{1, 3, 19}, {2, 23, 2},
                                 {3, 5, 11}, {4, 17, 4}}};
    tree.Attach(tree.Root(), view);
    ui::LayoutUiTree(tree, {100, 100});
    CHECK(node->measured.width == 31);  // max(text, top, bottom) + left + right
    CHECK(node->measured.height == 25); // max(text, left, right) + top + bottom
    node->text.clear();
    tree.MarkLayoutDirty(view);
    ui::LayoutUiTree(tree, {100, 100});
    CHECK(node->measured.width == 31);
    CHECK(node->measured.height == 25);

    ui::UiBitmapCache bitmaps;
    for (const auto& drawable : node->compound_drawables) {
        bitmaps.emplace(drawable.resource_id, std::make_shared<const ui::UiBitmap>(
            ui::UiBitmap{drawable.width, drawable.height,
                std::vector<std::uint8_t>(static_cast<std::size_t>(drawable.width * drawable.height) * 4, 255)}));
    }
    const std::array expected{ui::Rect{0, 2, 3, 21}, ui::Rect{3, 0, 26, 2},
                              ui::Rect{26, 6, 31, 17}, ui::Rect{6, 21, 23, 25}};
    std::size_t count{};
    for (const auto& command : ui::BuildUiRenderList(tree, bitmaps)) {
        if (const auto* bitmap = std::get_if<ui::DrawBitmap>(&command)) {
            REQUIRE(count < expected.size());
            CHECK(bitmap->rect == expected[count++]);
        }
    }
    CHECK(count == 4);
}
