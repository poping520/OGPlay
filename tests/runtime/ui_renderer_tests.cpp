#include <doctest/doctest.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstring>
#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ogplay/core/sha256.h"

#include "ogplay/runtime/ui/ui_renderer.h"

namespace ui = ogplay::runtime::ui;

TEST_CASE("DVM-248 Surface hole follows paint order clipping and final transparency") {
    ui::UiTree tree;
    tree.Get(tree.Root())->background_color = 0x000000ffU;
    const auto surface = tree.CreateNode(ui::UiClass::View);
    tree.Get(surface)->layout.width = {ui::SizeMode::Fixed, 8};
    tree.Get(surface)->layout.height = {ui::SizeMode::Fixed, 8};
    tree.Get(surface)->layout.margin.left = 2;
    tree.Get(surface)->surface_has_frame = true;
    tree.Attach(tree.Root(), surface);
    const auto label = tree.CreateNode(ui::UiClass::View);
    tree.Get(label)->layout.width = {ui::SizeMode::Fixed, 2};
    tree.Get(label)->layout.height = {ui::SizeMode::Fixed, 2};
    tree.Get(label)->layout.margin.left = 4;
    tree.Get(label)->background_color = 0xffffffffU;
    tree.Attach(tree.Root(), label);
    ui::LayoutUiTree(tree, {12, 8});
    ui::UiOverlayRenderer renderer;
    auto frame = renderer.Render(tree, {}, {12, 8});
    CHECK(frame.rgba8[3] == 255); // black outside Surface
    CHECK(frame.rgba8[2 * 4 + 3] == 0); // video hole
    CHECK(frame.rgba8[4 * 4] == 255); // later label survives
    CHECK(frame.rgba8[4 * 4 + 3] == 255);
    tree.Get(surface)->surface_layer = ui::SurfaceLayer::on_top;
    tree.MarkDrawDirty(surface);
    frame = renderer.Render(tree, {}, {12, 8});
    CHECK(frame.rgba8[2 * 4 + 3] == 255); // no window hole for onTop
    tree.Get(surface)->surface_layer = ui::SurfaceLayer::media_overlay;
    tree.MarkDrawDirty(surface);
    tree.Get(tree.Root())->padding.left = 3;
    frame = renderer.Render(tree, {}, {12, 8});
    CHECK(frame.rgba8[2 * 4 + 3] == 255); // ancestor clipping
    CHECK(frame.rgba8[3 * 4 + 3] == 0);
    tree.SetVisibility(surface, ui::Visibility::Invisible);
    frame = renderer.Render(tree, {}, {12, 8});
    CHECK(frame.rgba8[3 * 4 + 3] == 255);
    const auto cleared = ui::RasterizeUiOverlay({ui::DrawSolidRect{{0, 0, 8, 8}, 0xff0000ffU},
        ui::ClearSurfaceRect{{0, 0, 8, 8}}}, {8, 8});
    CHECK(cleared.fully_transparent);
    CHECK(std::all_of(cleared.rgba8.begin(), cleared.rgba8.end(), [](auto byte) { return byte == 0; }));
}

TEST_CASE("DVM-246 modal render plane separates Surface ordering and shares dirty evidence") {
    ui::UiTree tree;
    const auto normal = tree.CreateNode(ui::UiClass::FrameLayout);
    tree.Get(normal)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(normal)->layout.height.mode = ui::SizeMode::MatchParent;
    tree.Get(normal)->background_color = 0xff0000ffU;
    tree.Attach(tree.Root(), normal);
    const auto modal = tree.CreateNode(ui::UiClass::FrameLayout);
    tree.Get(modal)->modal_layer = true;
    tree.Get(modal)->layout.width.mode = ui::SizeMode::MatchParent;
    tree.Get(modal)->layout.height.mode = ui::SizeMode::MatchParent;
    tree.Get(modal)->background_color = 0x00000088U;
    tree.Attach(tree.Root(), modal);
    const auto panel = tree.CreateNode(ui::UiClass::View);
    tree.Get(panel)->layout.width = {ui::SizeMode::Fixed, 20};
    tree.Get(panel)->layout.height = {ui::SizeMode::Fixed, 20};
    tree.Get(panel)->layout.layout_gravity = 0x11;
    tree.Get(panel)->background_color = 0xffffffffU;
    tree.Attach(modal, panel);
    ui::LayoutUiTree(tree, {100, 100});
    ui::UiOverlayRenderer first, last;
    const bool dirty = tree.Get(tree.Root())->draw_dirty;
    const auto& before_surface = first.Render(tree, {}, {100, 100}, ui::UiRenderPlane::normal);
    CHECK(before_surface.rgba8[0] == 255); CHECK(before_surface.rgba8[3] == 255);
    const auto& after_surface = last.Render(tree, {}, {100, 100}, ui::UiRenderPlane::modal, dirty);
    CHECK(after_surface.rgba8[0] == 0); CHECK(after_surface.rgba8[3] == 136);
    const auto center = (50U * 100U + 50U) * 4U;
    CHECK(after_surface.rgba8[center] == 255); CHECK(after_surface.rgba8[center + 3] == 255);
    tree.Get(panel)->background_color = 0x00ff00ffU; tree.MarkDrawDirty(panel);
    const bool changed = tree.Get(tree.Root())->draw_dirty;
    static_cast<void>(first.Render(tree, {}, {100, 100}, ui::UiRenderPlane::normal));
    CHECK_FALSE(tree.Get(tree.Root())->draw_dirty);
    CHECK(last.Render(tree, {}, {100, 100}, ui::UiRenderPlane::modal, changed).rgba8[center + 1] == 255);
    CHECK(last.BuildCount() == 2);
    static_cast<void>(last.Render(tree, {}, {100, 100}, ui::UiRenderPlane::modal));
    CHECK(last.BuildCount() == 2);
}

TEST_CASE("DVM-235 trusted API19 fallback font renders distinct CJK glyphs") {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream input(root / "data/android/19/fonts/DroidSansFallback.ttf", std::ios::binary);
    REQUIRE(input.good());
    const std::vector<char> raw{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    const auto font = std::make_shared<ui::UiFallbackFont>(bytes);
    const auto king = font->Glyph(u'王', 15), village = font->Glyph(u'村', 15);
    REQUIRE_FALSE(king->alpha.empty());
    REQUIRE_FALSE(village->alpha.empty());
    CHECK(king->alpha != village->alpha);
    CHECK_FALSE(font->Glyph(u'\u2026', 15)->alpha.empty());
    CHECK_FALSE(font->Glyph(u'\u201c', 15)->alpha.empty());
    CHECK(ui::MeasureFixedText(u"国王村", 16, 0, font.get()).width == 46);
    const ui::UiRenderList commands{ui::DrawText{0, 0, u"国王村", 0xffffffffU, 16, 1, 0, font}};
    const auto frame = ui::RasterizeUiOverlay(commands, {64, 24});
    CHECK_FALSE(frame.fully_transparent);
    bytes[0] ^= std::byte{1};
    CHECK_THROWS_WITH(ui::UiFallbackFont{bytes}, "fallback font is not the pinned API19 trusted payload");
    CHECK_THROWS(static_cast<void>(font->Glyph(u'ا', 15)));
    CHECK_THROWS(static_cast<void>(font->Glyph(u'\u202e', 15)));
    CHECK_THROWS(static_cast<void>(font->Glyph(u'\u20d0', 15)));
    CHECK_THROWS(static_cast<void>(font->Glyph(u'\u0301', 15)));
    CHECK_THROWS(static_cast<void>(font->Glyph(u'\ufe0f', 15)));
    CHECK(ogplay::core::Sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

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
    // This golden checks raster pixels, independently of the word-wrap policy.
    tree.Get(text)->max_lines = 1;
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

namespace {

[[nodiscard]] std::shared_ptr<const ui::UiBitmap> SolidBitmap(
    const std::int32_t width, const std::int32_t height,
    const std::uint8_t red, const std::uint8_t green,
    const std::uint8_t blue) {
    auto bitmap = std::make_shared<ui::UiBitmap>();
    bitmap->width = width;
    bitmap->height = height;
    bitmap->rgba8.assign(static_cast<std::size_t>(width * height) * 4U, 0U);
    for (std::size_t index = 0; index < bitmap->rgba8.size(); index += 4U) {
        bitmap->rgba8[index] = red;
        bitmap->rgba8[index + 1U] = green;
        bitmap->rgba8[index + 2U] = blue;
        bitmap->rgba8[index + 3U] = 255U;
    }
    return bitmap;
}

}  // namespace

TEST_CASE("ViewGroup clipChildren and clipToPadding change descendant pixels") {
    const std::vector<std::uint8_t> kRed{255, 0, 0, 255};
    const std::vector<std::uint8_t> kClear{0, 0, 0, 0};
    const ui::UiBitmapCache bitmaps{{9, SolidBitmap(6, 2, 255, 0, 0)}};
    const auto make_padded_crop = [](const bool clip_children,
                                     const bool clip_to_padding) {
        ui::UiTree tree;
        const auto container = tree.CreateNode(ui::UiClass::FrameLayout);
        auto* group = tree.Get(container);
        CHECK(group->clip_children);
        CHECK(group->clip_to_padding);
        group->layout.width = {ui::SizeMode::Fixed, 6};
        group->layout.height = {ui::SizeMode::Fixed, 4};
        group->padding = {1, 1, 1, 1};
        group->clip_children = clip_children;
        group->clip_to_padding = clip_to_padding;
        const auto image = tree.CreateNode(ui::UiClass::ImageView);
        tree.Get(image)->layout.width = {ui::SizeMode::Fixed, 2};
        tree.Get(image)->layout.height = {ui::SizeMode::Fixed, 2};
        tree.Get(image)->image_resource_id = 9;
        tree.Get(image)->image_scale_type = ui::ImageScaleType::CenterCrop;
        tree.Attach(tree.Root(), container);
        tree.Attach(container, image);
        ui::LayoutUiTree(tree, {6, 4});
        CHECK(tree.Get(container)->measured == ui::Size{6, 4});
        CHECK(tree.Get(image)->screen_frame == ui::Rect{1, 1, 3, 3});
        return tree;
    };

    const auto clipped = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(make_padded_crop(true, true), bitmaps), {6, 4});
    CHECK(Pixel(clipped, 0, 1) == kClear);
    CHECK(Pixel(clipped, 1, 1) == kRed);
    CHECK(Pixel(clipped, 2, 1) == kRed);
    CHECK(Pixel(clipped, 3, 1) == kClear);

    const auto padding_only = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(make_padded_crop(false, true), bitmaps), {6, 4});
    CHECK(Pixel(padding_only, 0, 1) == kClear);
    CHECK(Pixel(padding_only, 1, 1) == kRed);
    CHECK(Pixel(padding_only, 3, 1) == kRed);
    CHECK(Pixel(padding_only, 5, 1) == kClear);

    const auto unclipped = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(make_padded_crop(false, false), bitmaps), {6, 4});
    CHECK(Pixel(unclipped, 0, 1) == kRed);
    CHECK(Pixel(unclipped, 3, 1) == kRed);
    CHECK(Pixel(unclipped, 5, 1) == kRed);

    ui::UiTree nested;
    const auto outer = nested.CreateNode(ui::UiClass::FrameLayout);
    nested.Get(outer)->layout.width = {ui::SizeMode::Fixed, 6};
    nested.Get(outer)->layout.height = {ui::SizeMode::Fixed, 4};
    nested.Get(outer)->clip_children = true;
    const auto inner = nested.CreateNode(ui::UiClass::FrameLayout);
    nested.Get(inner)->layout.width = {ui::SizeMode::Fixed, 4};
    nested.Get(inner)->layout.height = {ui::SizeMode::Fixed, 4};
    nested.Get(inner)->layout.margin.left = 1;
    nested.Get(inner)->clip_children = false;
    nested.Get(inner)->clip_to_padding = false;
    const auto nested_image = nested.CreateNode(ui::UiClass::ImageView);
    nested.Get(nested_image)->layout.width = {ui::SizeMode::Fixed, 2};
    nested.Get(nested_image)->layout.height = {ui::SizeMode::Fixed, 2};
    nested.Get(nested_image)->image_resource_id = 9;
    nested.Get(nested_image)->image_scale_type = ui::ImageScaleType::CenterCrop;
    nested.Attach(nested.Root(), outer);
    nested.Attach(outer, inner);
    nested.Attach(inner, nested_image);
    ui::LayoutUiTree(nested, {6, 4});
    CHECK(nested.Get(inner)->screen_frame == ui::Rect{1, 0, 5, 4});
    CHECK(nested.Get(nested_image)->screen_frame == ui::Rect{1, 0, 3, 2});
    const auto nested_frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(nested, bitmaps), {6, 4});
    CHECK(Pixel(nested_frame, 0, 0) == kClear);
    CHECK(Pixel(nested_frame, 1, 0) == kRed);
    CHECK(Pixel(nested_frame, 3, 0) == kRed);

    ui::UiTree isolated;
    const auto row = isolated.CreateNode(ui::UiClass::LinearLayout);
    isolated.Get(row)->layout.width = {ui::SizeMode::Fixed, 8};
    isolated.Get(row)->layout.height = {ui::SizeMode::Fixed, 4};
    isolated.Get(row)->orientation = ui::Orientation::Horizontal;
    const auto open = isolated.CreateNode(ui::UiClass::FrameLayout);
    isolated.Get(open)->layout.width = {ui::SizeMode::Fixed, 4};
    isolated.Get(open)->layout.height = {ui::SizeMode::Fixed, 4};
    isolated.Get(open)->clip_children = false;
    isolated.Get(open)->clip_to_padding = false;
    const auto closed = isolated.CreateNode(ui::UiClass::FrameLayout);
    isolated.Get(closed)->layout.width = {ui::SizeMode::Fixed, 4};
    isolated.Get(closed)->layout.height = {ui::SizeMode::Fixed, 4};
    const auto left = isolated.CreateNode(ui::UiClass::ImageView);
    isolated.Get(left)->layout.width = {ui::SizeMode::Fixed, 2};
    isolated.Get(left)->layout.height = {ui::SizeMode::Fixed, 2};
    isolated.Get(left)->image_resource_id = 9;
    isolated.Get(left)->image_scale_type = ui::ImageScaleType::CenterCrop;
    const auto right = isolated.CreateNode(ui::UiClass::ImageView);
    isolated.Get(right)->layout.width = {ui::SizeMode::Fixed, 2};
    isolated.Get(right)->layout.height = {ui::SizeMode::Fixed, 2};
    isolated.Get(right)->image_resource_id = 9;
    isolated.Get(right)->image_scale_type = ui::ImageScaleType::CenterCrop;
    isolated.Attach(isolated.Root(), row);
    isolated.Attach(row, open);
    isolated.Attach(row, closed);
    isolated.Attach(open, left);
    isolated.Attach(closed, right);
    ui::LayoutUiTree(isolated, {8, 4});
    CHECK(isolated.Get(left)->screen_frame == ui::Rect{0, 0, 2, 2});
    CHECK(isolated.Get(right)->screen_frame == ui::Rect{4, 0, 6, 2});
    const auto isolated_frame = ui::RasterizeUiOverlay(
        ui::BuildUiRenderList(isolated, bitmaps), {8, 4});
    CHECK(Pixel(isolated_frame, 2, 0) == kRed);
    CHECK(Pixel(isolated_frame, 6, 0) == kClear);

    auto live = make_padded_crop(true, true);
    ui::UiOverlayRenderer renderer;
    CHECK(Pixel(renderer.Render(live, bitmaps, {6, 4}), 0, 1) == kClear);
    CHECK(renderer.BuildCount() == 1);
    const auto container = live.Get(live.Root())->children.front();
    const auto measured = live.Get(container)->measured;
    live.Get(container)->clip_children = false;
    live.Get(container)->clip_to_padding = false;
    live.MarkDrawDirty(container);
    CHECK(live.Get(container)->measured == measured);
    CHECK_FALSE(live.Get(container)->layout_dirty);
    CHECK(Pixel(renderer.Render(live, bitmaps, {6, 4}), 0, 1) == kRed);
    CHECK(renderer.BuildCount() == 2);
}

TEST_CASE("DVM-291 pure leaf specs intrinsic padding minimum and explicit state") {
    const ui::ImageLeafInput image{{20, 10}, {2, 3, 4, 5}, {30, 12}};
    for (const auto mode : {ui::MeasureMode::Exactly, ui::MeasureMode::AtMost, ui::MeasureMode::Unspecified}) {
        const auto measured = ui::MeasureImageLeaf(image, {mode, 25}, {mode, 16});
        CHECK(measured.desired == ui::Size{30, 18});
        CHECK(measured.constrained == (mode == ui::MeasureMode::Unspecified ? ui::Size{30, 18} : ui::Size{25, 16}));
        CHECK(measured.width_state == (mode == ui::MeasureMode::AtMost ? 0x01000000U : 0U));
        CHECK(measured.height_state == measured.width_state);
        CHECK_FALSE(measured.baseline.has_value());
        CHECK_FALSE(measured.line_plan);
    }
    CHECK(ui::MeasureImageLeaf({{-1, -1}, {}, {4, 6}}, {}, {}).desired == ui::Size{4, 6});
    ui::TextLeafInput text;
    text.padding = {2, 3, 4, 5};
    auto empty = ui::MeasureTextLeaf(text, {}, {});
    CHECK(empty.desired == ui::Size{11, 15});
    REQUIRE(empty.line_plan);
    CHECK(empty.line_plan->text.empty());
    CHECK(empty.line_plan->metrics == ui::FixedTextMetrics{0, 0, 1});
    text.minimum = {30, 20};
    for (const auto mode : {ui::MeasureMode::Exactly, ui::MeasureMode::AtMost, ui::MeasureMode::Unspecified}) {
        const auto result = ui::MeasureTextLeaf(text, {mode, 12}, {mode, 10});
        CHECK(result.desired == ui::Size{30, 20});
        CHECK(result.constrained == (mode == ui::MeasureMode::Unspecified ? ui::Size{30, 20} : ui::Size{12, 10}));
        CHECK(result.width_state == (mode == ui::MeasureMode::AtMost ? 0x01000000U : 0U));
        CHECK_FALSE(result.baseline);
    }
    text.text = u"HI\nM";
    text.minimum = {};
    text.compound = {{{3, 20}, {25, 4}, {5, 21}, {30, 6}}};
    const auto compound = ui::MeasureTextLeaf(text, {}, {});
    CHECK(compound.desired == ui::Size{44, 39});
    CHECK(compound.line_plan->text == u"HI\nM");
    CHECK(compound.line_plan->metrics == ui::FixedTextMetrics{11, 15, 1});
    text.text.clear();
    CHECK(ui::MeasureTextLeaf(text, {}, {}).desired == ui::Size{44, 39});
    text.text = u"\u0301";
    CHECK_THROWS(static_cast<void>(ui::MeasureTextLeaf(text, {}, {})));
    text.text = u"M";
    text.size_px = 0;
    CHECK_THROWS(static_cast<void>(ui::MeasureTextLeaf(text, {}, {})));
}

TEST_CASE("DVM-291 compound line plan preserves Legacy formula and drives raster") {
    ui::TextLeafInput text;
    text.text = u"HI HI";
    text.max_lines = 3;
    text.padding = {1, 2, 1, 2};
    text.compound[0] = {6, 7};
    text.compound[2] = {6, 7};
    const ui::MeasureSpec width{ui::MeasureMode::AtMost, 32};
    const auto unified = ui::MeasureTextLeaf(text, width, {});
    CHECK(unified.line_plan->text == u"HI\nHI");
    CHECK(unified.desired == ui::Size{25, 19});
    CHECK(unified.line_plan->metrics.height == 15);
    text.wrap_width = ui::TextWrapWidth::LegacyPadding;
    const auto legacy = ui::MeasureTextLeaf(text, width, {});
    CHECK(legacy.line_plan->text == u"HI HI");
    CHECK(legacy.desired == ui::Size{43, 11});
    CHECK(legacy.constrained == ui::Size{32, 11});
    // The original caller still commits its original measured dimensions.
    ui::UiTree tree;
    const auto id = tree.CreateNode(ui::UiClass::TextView);
    auto& node = *tree.Get(id);
    node.text = text.text;
    node.text_size_px = text.size_px;
    node.max_lines = text.max_lines;
    node.padding = text.padding;
    node.compound_drawables[0].width = 6;
    node.compound_drawables[0].height = 7;
    node.compound_drawables[2].width = 6;
    node.compound_drawables[2].height = 7;
    tree.Attach(tree.Root(), id);
    ui::LayoutUiTree(tree, {32, 40});
    CHECK(node.measured == legacy.constrained);
    ui::DrawText planned;
    planned.rgba = 0xffffffffU;
    planned.text = u"INVALID\u0301";  // plan exclusively supplies raster inputs
    planned.size_px = 0;
    planned.line_plan = unified.line_plan;
    ui::DrawText explicit_lines;
    explicit_lines.rgba = planned.rgba;
    explicit_lines.text = u"HI\nHI";
    const auto rendered = ui::RasterizeUiOverlay({planned}, {32, 24});
    const auto expected = ui::RasterizeUiOverlay({explicit_lines}, {32, 24});
    CHECK(rendered.rgba8 == expected.rgba8);
    CHECK(rendered.rgba8[(8U * 32U) * 4U + 3U] == 255);
    CHECK(rendered.rgba8[(7U * 32U) * 4U + 3U] == 0);
    // A later value mutation cannot alter the frozen plan.
    text.text = u"M";
    CHECK(unified.line_plan->text == u"HI\nHI");
}

TEST_CASE("DVM-295 Java TextView compound plan uses final width and TextView state policy") {
    ui::TextLeafInput input;
    input.java_text_view = true;
    input.text = u"HI HI";
    input.size_px = 8;
    input.max_lines = 3;
    input.padding = {1, 2, 1, 2};
    input.compound = {ui::Size{6, 7}, ui::Size{}, ui::Size{6, 7}, ui::Size{}};
    const auto result = ui::MeasureTextLeaf(input, {ui::MeasureMode::AtMost, 32}, {ui::MeasureMode::AtMost, 10});
    CHECK(result.desired == ui::Size{43, 19});
    CHECK(result.constrained == ui::Size{32, 10});
    CHECK(result.width_state == 0);
    CHECK(result.height_state == 0);
    CHECK(result.baseline == 9);
    REQUIRE(result.line_plan);
    CHECK(result.line_plan->text == u"HI\nHI");
    ui::UiTree tree;
    tree.Get(tree.Root())->frame = tree.Get(tree.Root())->screen_frame = {0, 0, 64, 32};
    const auto id = tree.CreateNode(ui::UiClass::TextView);
    tree.Attach(tree.Root(), id);
    auto& n = *tree.Get(id);
    n.text = u"HI HI";
    n.text_size_px = 8;
    n.text_line_plan = result.line_plan;
    n.frame = n.screen_frame = {0, 0, 64, 32};  // Deliberately wider than measured: presenter must not rewrap.
    const auto commands = ui::BuildUiRenderList(tree, {});
    bool found{};
    for (const auto& command : commands)
        if (const auto* draw = std::get_if<ui::DrawText>(&command)) {
            CHECK(draw->text == u"HI\nHI");
            CHECK(draw->line_plan == result.line_plan);
            found = true;
        }
    CHECK(found);
    const auto frame = ui::RasterizeUiOverlay(commands, {64, 32});
    const auto golden = ui::RasterizeUiOverlay({ui::DrawText{0, 0, u"HI\nHI", 0xffffffffU, 8}}, {64, 32});
    CHECK(frame.rgba8 == golden.rgba8);
}
