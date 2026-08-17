#include <doctest/doctest.h>

#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/session/ui_compositor.h"

namespace ui = ogplay::runtime::ui;

TEST_CASE("session UI compositor preserves base under transparent overlay") {
    const std::vector<std::uint8_t> base{1, 2, 3, 255, 4, 5, 6, 255};
    const ui::UiOverlayFrame overlay{2, 1, std::vector<std::uint8_t>(8, 0)};
    CHECK(ogplay::session::ComposeUiOverlay(base, overlay) == base);
}

TEST_CASE("session UI compositor changes only overlay region") {
    const std::vector<std::uint8_t> base{10, 20, 30, 255, 10, 20, 30, 255};
    const ui::UiOverlayFrame overlay{
        2, 1, {255, 0, 0, 255, 0, 0, 0, 0}};
    CHECK(ogplay::session::ComposeUiOverlay(base, overlay) ==
          std::vector<std::uint8_t>{255, 0, 0, 255, 10, 20, 30, 255});
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ComposeUiOverlay(
            std::span{base}.first(4), overlay)),
        std::invalid_argument);
}
