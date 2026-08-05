#include <doctest/doctest.h>

#include <ostream>
#include <limits>
#include <stdexcept>
#include <vector>

#if OGPLAY_TEST_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include "ogplay/hal/window_input.h"

TEST_CASE("display layout preserves aspect ratio and centers black bars") {
    using ogplay::hal::FitDisplayRect;
    CHECK((FitDisplayRect(1280, 720, 1920, 1080) ==
           ogplay::hal::DisplayRect{0, 0, 1920, 1080}));
    CHECK((FitDisplayRect(640, 480, 1280, 720) ==
           ogplay::hal::DisplayRect{160, 0, 960, 720}));
    CHECK((FitDisplayRect(1080, 1920, 1280, 720) ==
           ogplay::hal::DisplayRect{437, 0, 405, 720}));
    CHECK((FitDisplayRect(2, 2, 320, 180) ==
           ogplay::hal::DisplayRect{70, 0, 180, 180}));
    CHECK_THROWS_AS(static_cast<void>(FitDisplayRect(0, 1, 1, 1)),
                    std::invalid_argument);
}

TEST_CASE("display points map through content geometry and identify black bars") {
    using ogplay::hal::MapDisplayPoint;
    const auto origin = MapDisplayPoint(160, 0, 640, 480, 1280, 720);
    CHECK(origin.inside);
    CHECK(origin.x == doctest::Approx(0));
    CHECK(origin.y == doctest::Approx(0));

    const auto center = MapDisplayPoint(640, 360, 640, 480, 1280, 720);
    CHECK(center.inside);
    CHECK(center.x == doctest::Approx(320));
    CHECK(center.y == doctest::Approx(240));

    const auto left_bar = MapDisplayPoint(100, 360, 640, 480, 1280, 720);
    CHECK_FALSE(left_bar.inside);
    CHECK(left_bar.x == doctest::Approx(0));
    const auto right_bar = MapDisplayPoint(1200, 360, 640, 480, 1280, 720);
    CHECK_FALSE(right_bar.inside);
    CHECK(right_bar.x == doctest::Approx(640));
    CHECK_THROWS_AS(
        static_cast<void>(MapDisplayPoint(
            std::numeric_limits<float>::infinity(), 0, 1, 1, 1, 1)),
        std::invalid_argument);
}

#if OGPLAY_TEST_HAS_SDL3
namespace {

std::unique_ptr<ogplay::hal::WindowInput> OpenDummyWindow() {
    auto host = ogplay::hal::CreateSdlWindowInput(ogplay::hal::VideoBackend::dummy);
    host->Open({.title = "OGPlay HAL contract", .width = 320, .height = 180,
                .hidden = true, .resizable = false});
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    return host;
}

}  // namespace

TEST_CASE("SDL dummy window has an explicit lifecycle") {
    auto host = ogplay::hal::CreateSdlWindowInput(ogplay::hal::VideoBackend::dummy);
    CHECK(host->BackendName() == "dummy");
    CHECK_FALSE(host->State().open);
    CHECK_THROWS_AS(host->Open({.width = 0, .height = 180}), std::invalid_argument);

    host->Open({.title = "OGPlay HAL contract", .width = 320, .height = 180,
                .hidden = true, .resizable = false});
    const auto state = host->State();
    CHECK(state.open);
    CHECK(state.id != 0);
    CHECK(state.width == 320);
    CHECK(state.height == 180);
    CHECK_THROWS_AS(host->Open({}), std::logic_error);
    host->Close();
    CHECK_FALSE(host->State().open);
    host->Close();
}

TEST_CASE("SDL window presents exact RGBA8 frames") {
    auto host = OpenDummyWindow();
    const std::vector<std::uint8_t> pixels{
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255,
    };

    CHECK_THROWS_AS(host->PresentRgba8({}, 0, 2), std::invalid_argument);
    CHECK_THROWS_AS(host->PresentRgba8(std::span{pixels}.first(15), 2, 2),
                    std::invalid_argument);
    host->PresentRgba8(pixels, 2, 2);
    CHECK(host->State().present_count == 1);

    auto* window = SDL_GetWindowFromID(host->State().id);
    REQUIRE(window != nullptr);
    auto* surface = SDL_GetWindowSurface(window);
    REQUIRE(surface != nullptr);
    Uint8 red{}, green{}, blue{}, alpha{};
    REQUIRE(SDL_ReadSurfacePixel(surface, 0, 0, &red, &green, &blue, &alpha));
    CHECK(red == 0); CHECK(green == 0); CHECK(blue == 0); CHECK(alpha == 255);
    REQUIRE(SDL_ReadSurfacePixel(surface, 70, 0, &red, &green, &blue, &alpha));
    CHECK(red == 255); CHECK(green == 0); CHECK(blue == 0); CHECK(alpha == 255);
    REQUIRE(SDL_ReadSurfacePixel(surface, 250, 0, &red, &green, &blue, &alpha));
    CHECK(red == 0); CHECK(green == 0); CHECK(blue == 0); CHECK(alpha == 255);
}

TEST_CASE("SDL events map to backend-independent keyboard and pointer events") {
    auto host = OpenDummyWindow();
    const auto window_id = host->State().id;

    SDL_Event key{};
    key.key.type = SDL_EVENT_KEY_DOWN;
    key.key.timestamp = 101;
    key.key.windowID = window_id;
    key.key.which = 7;
    key.key.scancode = SDL_SCANCODE_A;
    key.key.down = true;
    key.key.repeat = true;
    REQUIRE(SDL_PushEvent(&key));

    SDL_Event motion{};
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.timestamp = 102;
    motion.motion.windowID = window_id;
    motion.motion.which = 8;
    motion.motion.x = 12.5F;
    motion.motion.y = 24.5F;
    motion.motion.xrel = 1.5F;
    motion.motion.yrel = -2.0F;
    REQUIRE(SDL_PushEvent(&motion));

    const auto events = host->PollEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[0].type == ogplay::hal::InputEventType::key);
    CHECK(events[0].timestamp_ns == 101);
    CHECK(events[0].code == SDL_SCANCODE_A);
    CHECK(events[0].pressed);
    CHECK(events[0].repeat);
    CHECK(events[1].type == ogplay::hal::InputEventType::pointer_motion);
    CHECK(events[1].x == doctest::Approx(12.5F));
    CHECK(events[1].delta_y == doctest::Approx(-2.0F));
}

TEST_CASE("SDL events map gamepad state and quit requests") {
    auto host = OpenDummyWindow();

    SDL_Event axis{};
    axis.gaxis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    axis.gaxis.timestamp = 201;
    axis.gaxis.which = 42;
    axis.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    axis.gaxis.value = -12345;
    REQUIRE(SDL_PushEvent(&axis));

    SDL_Event button{};
    button.gbutton.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    button.gbutton.timestamp = 202;
    button.gbutton.which = 42;
    button.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    button.gbutton.down = true;
    REQUIRE(SDL_PushEvent(&button));

    SDL_Event quit{};
    quit.quit.type = SDL_EVENT_QUIT;
    quit.quit.timestamp = 203;
    REQUIRE(SDL_PushEvent(&quit));

    const auto events = host->PollEvents();
    REQUIRE(events.size() == 3);
    CHECK(events[0].type == ogplay::hal::InputEventType::gamepad_axis);
    CHECK(events[0].device_id == 42);
    CHECK(events[0].value == -12345);
    CHECK(events[1].type == ogplay::hal::InputEventType::gamepad_button);
    CHECK(events[1].pressed);
    CHECK(events[2].type == ogplay::hal::InputEventType::quit);
}
#else
TEST_CASE("SDL-disabled builds fail the window factory explicitly") {
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            ogplay::hal::CreateSdlWindowInput(ogplay::hal::VideoBackend::dummy)),
        "SDL3 support is disabled in this build", std::runtime_error);
}
#endif
