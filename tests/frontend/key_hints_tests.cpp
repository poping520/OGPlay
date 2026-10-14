#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ogplay/frontend/control_overlay.h"
#include "ogplay/frontend/host_shortcuts.h"

namespace {

using namespace ogplay;
using frontend::HostShortcut;

hal::InputEvent Key(const std::int32_t code, const bool pressed, const std::uint32_t modifiers = 0,
                    const bool repeat = false) {
    hal::InputEvent event{.type = hal::InputEventType::key, .code = code};
    event.pressed = pressed;
    event.repeat = repeat;
    event.key_modifiers = modifiers;
    return event;
}

constexpr std::int32_t kF1 = 58, kF2 = 59, kS = 22, kW = 26, kLeftCtrl = 224, kLeftMeta = 227;
constexpr auto kCtrl = static_cast<std::uint32_t>(hal::KeyModifier::left_control);
constexpr auto kCmd = static_cast<std::uint32_t>(hal::KeyModifier::left_meta);

}  // namespace

TEST_CASE("INP-4 F1 and F2 are host shortcuts and never reach the guest") {
    frontend::HostShortcutFilter filter;
    auto decision = filter.Filter(Key(kF1, true));
    CHECK(decision.consumed);
    CHECK(decision.shortcut == HostShortcut::toggle_hints);
    decision = filter.Filter(Key(kF1, true, 0, true));
    CHECK(decision.consumed);
    CHECK(decision.shortcut == HostShortcut::none);  // auto-repeat does not toggle again
    CHECK(filter.Filter(Key(kF1, false)).consumed);
    decision = filter.Filter(Key(kF2, true));
    CHECK(decision.shortcut == HostShortcut::open_key_editor);
    CHECK(filter.Filter(Key(kF2, false)).consumed);
    CHECK_FALSE(filter.Filter(Key(kW, true)).consumed);
    CHECK_FALSE(filter.Filter(Key(kW, false)).consumed);
    CHECK_FALSE(filter.Filter({.type = hal::InputEventType::pointer_button, .code = 0, .pressed = true}).consumed);
}

TEST_CASE("INP-4 Ctrl and Cmd chords stay on the host whatever the release order") {
    frontend::HostShortcutFilter filter;
    // Ctrl alone is an ordinary key; the chord key is the host's from press to release.
    CHECK_FALSE(filter.Filter(Key(kLeftCtrl, true, kCtrl)).consumed);
    CHECK(filter.Filter(Key(kS, true, kCtrl)).consumed);
    CHECK_FALSE(filter.Filter(Key(kLeftCtrl, false)).consumed);  // modifier released first
    CHECK(filter.Filter(Key(kS, true, 0, true)).consumed);       // repeats follow the press
    CHECK(filter.Filter(Key(kS, false)).consumed);
    CHECK_FALSE(filter.Filter(Key(kS, true)).consumed);           // a fresh press is the guest's
    // A guest key keeps its repeat and release when Ctrl joins later.
    CHECK_FALSE(filter.Filter(Key(kS, true, kCtrl, true)).consumed);
    CHECK_FALSE(filter.Filter(Key(kS, false, kCtrl)).consumed);
    // Cmd/Win itself and Cmd chords belong to the host.
    CHECK(filter.Filter(Key(kLeftMeta, true, kCmd)).consumed);
    CHECK(filter.Filter(Key(kW, true, kCmd)).consumed);
    CHECK(filter.Filter(Key(kLeftMeta, false)).consumed);
    CHECK(filter.Filter(Key(kW, false)).consumed);
    // A reset forgets held host keys.
    CHECK(filter.Filter(Key(kF1, true)).consumed);
    filter.Reset();
    CHECK_FALSE(filter.Filter(Key(kF1, false)).consumed);
}

TEST_CASE("INP-4 hint overlay draws zones and labels only on the given copy") {
    constexpr std::uint32_t width = 320, height = 200;
    const std::vector<std::uint8_t> guest(static_cast<std::size_t>(width) * height * 4U, 10);
    auto copy = guest;
    input::ControlHint button{.zone = {.name = "fire", .center_x = 250, .center_y = 150, .half_width = 30,
                                       .half_height = 30, .circle = true},
                              .sources = {input::ParseInputSource("key:j"), input::ParseInputSource("mouse:right")}};
    input::ControlHint stick{.zone = {.name = "move", .center_x = 60, .center_y = 140, .half_width = 50,
                                      .half_height = 50, .circle = true},
                             .stick = true,
                             .sources = {input::ParseInputSource("key:up"), input::ParseInputSource("key:left"),
                                         input::ParseInputSource("key:down"), input::ParseInputSource("key:right")}};
    const std::vector hints{button, stick};
    frontend::DrawControlHints(copy, width, height, hints, "map");
    const auto pixel = [&](const std::uint32_t x, const std::uint32_t y) {
        return copy[(static_cast<std::size_t>(y) * width + x) * 4U + 2U];
    };
    CHECK(pixel(250, 121) > 100);   // circle outline (blue channel)
    CHECK(pixel(250, 160) != 10);   // inside the zone is tinted
    CHECK(pixel(150, 100) == 10);   // outside every zone and label stays untouched
    CHECK(std::ranges::count_if(copy, [](const std::uint8_t value) { return value == 255; }) > 50);  // label text
    CHECK(std::ranges::all_of(guest, [](const std::uint8_t value) { return value == 10; }));
    // A short buffer is ignored rather than overrun.
    std::vector<std::uint8_t> small(16, 10);
    frontend::DrawControlHints(small, width, height, hints, "default");
    CHECK(std::ranges::all_of(small, [](const std::uint8_t value) { return value == 10; }));
}

TEST_CASE("INP-4 hint labels are short ASCII names") {
    CHECK(frontend::HintLabel(input::ParseInputSource("key:j")) == "J");
    CHECK(frontend::HintLabel(input::ParseInputSource("key:space")) == "SPC");
    CHECK(frontend::HintLabel(input::ParseInputSource("key:f5")) == "F5");
    CHECK(frontend::HintLabel(input::ParseInputSource("key:up")) == "\x01");
    CHECK(frontend::HintLabel(input::ParseInputSource("key:slash")) == "/");
    CHECK(frontend::HintLabel(input::ParseInputSource("mouse:right")) == "RMB");
    CHECK(frontend::HintLabel(input::ParseInputSource("mouse:x1")) == "X1");
}
