#include <doctest/doctest.h>

#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/session/ui_compositor.h"
#include "ogplay/session/dex_activity_lifecycle.h"

namespace ui = ogplay::runtime::ui;

TEST_CASE("session UI compositor preserves base under transparent overlay") {
    const std::vector<std::uint8_t> base{1, 2, 3, 255, 4, 5, 6, 255};
    const ui::UiOverlayFrame overlay{2, 1, std::vector<std::uint8_t>(8, 0)};
    CHECK(ogplay::session::ComposeUiOverlay(base, overlay) == base);
    const auto empty = ui::RasterizeUiOverlay({}, {2, 1});
    REQUIRE(empty.fully_transparent);
    auto owned = base;
    auto* storage = owned.data();
    ogplay::session::ComposeUiOverlayInPlace(owned, empty);
    CHECK(owned == base);
    CHECK(owned.data() == storage);
    CHECK_THROWS_AS(ogplay::session::ComposeUiOverlayInPlace(
        std::span{owned}.first(4), empty), std::invalid_argument);
}

TEST_CASE("session UI compositor changes only overlay region") {
    const std::vector<std::uint8_t> base{10, 20, 30, 255, 10, 20, 30, 255};
    const ui::UiOverlayFrame overlay{
        2, 1, {255, 0, 0, 255, 0, 0, 0, 0}};
    CHECK(ogplay::session::ComposeUiOverlay(base, overlay) ==
          std::vector<std::uint8_t>{255, 0, 0, 255, 10, 20, 30, 255});
    auto owned = base;
    ogplay::session::ComposeUiOverlayInPlace(owned, overlay);
    CHECK(owned == ogplay::session::ComposeUiOverlay(base, overlay));
    const auto painted = ui::RasterizeUiOverlay(
        {ui::DrawSolidRect{{0, 0, 1, 1}, 0xff0000ffU}}, {2, 1});
    CHECK_FALSE(painted.fully_transparent);
    const auto clipped = ui::RasterizeUiOverlay(
        {ui::PushClip{{1, 0, 2, 1}},
         ui::DrawSolidRect{{0, 0, 1, 1}, 0xff0000ffU}, ui::PopClip{}}, {2, 1});
    CHECK(clipped.fully_transparent);
    const auto invisible = ui::RasterizeUiOverlay(
        {ui::DrawSolidRect{{0, 0, 2, 1}, 0xff000000U}}, {2, 1});
    CHECK(invisible.fully_transparent);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ComposeUiOverlay(
            std::span{base}.first(4), overlay)),
        std::invalid_argument);
}

TEST_CASE("real-time video clock catches up within 100ms without counting guest advances twice") {
    using ogplay::session::VideoClockAdvanceMillis;
    CHECK(VideoClockAdvanceMillis(8, 0) == 16);
    CHECK(VideoClockAdvanceMillis(40, 0) == 40);
    CHECK(VideoClockAdvanceMillis(1000, 0) == 100);
    CHECK(VideoClockAdvanceMillis(40, 25) == 15);
    CHECK(VideoClockAdvanceMillis(40, 80) == 0);
}
