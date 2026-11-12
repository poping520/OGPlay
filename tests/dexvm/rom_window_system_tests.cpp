#include <doctest/doctest.h>

#include "android_value_fixture.h"
#include "runtime/integration/dexvm_android/rom_window.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::android_intrinsics;
using namespace ogplay::runtime::dexvm;
using namespace ogplay::test::android_value;

namespace {
std::shared_ptr<const ogplay::video::VideoFrame> Solid(int width, int height, std::array<std::uint8_t, 4> color) {
    auto frame = std::make_shared<ogplay::video::VideoFrame>();
    frame->width = static_cast<unsigned>(width);
    frame->height = static_cast<unsigned>(height);
    for (int i = 0; i < width * height; ++i)
        frame->rgba8.insert(frame->rgba8.end(), color.begin(), color.end());
    return frame;
}

AndroidBoundaryInput Touch(int action, float x, float y) {
    AndroidBoundaryInput input;
    input.type = AndroidBoundaryInputType::pointer_button;
    input.action = action;
    input.x = x;
    input.y = y;
    input.pointers = {AndroidInputPointer{}};
    input.pointers[0].axes[0] = x;
    input.pointers[0].axes[1] = y;
    return input;
}
}  // namespace

TEST_CASE("P3 WMS application subwindow and toast layers follow API19 policy") {
    WindowSystem w(800, 480);
    const auto app = w.Add({}, true);
    WindowAttributes a;
    a.parent = app;
    a.type = 1000;
    const auto panel = w.Add(a, true);
    a.type = 1002;
    const auto subpanel = w.Add(a, true);
    a.type = 1001;
    const auto media = w.Add(a, true);
    a.type = 1004;
    const auto overlay = w.Add(a, true);
    WindowAttributes toast;
    toast.type = 2005;
    toast.flags = 0x18;
    const auto top = w.Add(toast, true);
    for (const auto id : {app, panel, subpanel, media, overlay, top})
        w.Post(id, Solid(1, 1, {0, 0, 0, 255}), false);
    const auto layers = w.Layers();
    REQUIRE(layers.size() == 6);
    CHECK(layers[0].window == media);
    CHECK(layers[1].window == overlay);
    CHECK(layers[2].window == app);
    CHECK(layers[3].window == panel);
    CHECK(layers[4].window == subpanel);
    CHECK(layers[5].window == top);
    CHECK(w.Focus() == subpanel);
    w.Remove(app);
    CHECK(w.Layers().size() == 1);
    CHECK(w.Focus() == WindowId{});
}

TEST_CASE("P3 WMS dialog gravity popup offsets and zero system insets") {
    WindowSystem w(800, 480);
    WindowAttributes a;
    a.width = 400;
    a.height = 200;
    a.gravity = 0x11;
    const auto dialog = w.Add(a, true);
    CHECK(w.Frame(dialog) == WindowRect{200, 140, 600, 340});
    WindowAttributes popup;
    popup.parent = dialog;
    popup.type = 1000;
    popup.width = 100;
    popup.height = 50;
    popup.x = 20;
    popup.y = 30;
    const auto panel = w.Add(popup, true);
    CHECK(w.Frame(panel) == WindowRect{220, 170, 320, 220});
    popup.flags |= 0x100;
    w.Relayout(panel, popup, 100, 50, true);
    CHECK(w.Frame(panel) == WindowRect{20, 30, 120, 80});
    CHECK(w.DisplayFrame() == WindowRect{0, 0, 800, 480});
    CHECK_THROWS_AS(w.Add(WindowAttributes{.type = 2000}, true), std::invalid_argument);
}

TEST_CASE("WMS adds a late activity decor below an existing dialog of the same token") {
    WindowSystem w(800, 480);
    const auto older = w.Add(WindowAttributes{.type = 1, .application_token = 1}, true);
    const auto dialog = w.Add(WindowAttributes{.application_token = 2}, true);
    const auto decor = w.Add(WindowAttributes{.type = 1, .application_token = 2}, true);
    for (const auto id : {older, dialog, decor})
        w.Post(id, Solid(1, 1, {0, 0, 0, 255}), false);
    const auto layers = w.Layers();
    REQUIRE(layers.size() == 3);
    CHECK(layers[0].window == older);
    CHECK(layers[1].window == decor);
    CHECK(layers[2].window == dialog);
    CHECK(w.Focus() == dialog);
    w.Remove(dialog);
    CHECK(w.Focus() == decor);
}

TEST_CASE("P3 compositor ignores alpha for opaque formats and honors translucent alpha") {
    WindowSystem w(2, 1);
    const auto base = w.Add({}, true);
    w.Post(base, Solid(2, 1, {0, 0, 255, 255}), false, true);
    WindowAttributes a;
    a.format = 2;
    const auto top = w.Add(a, true);
    CHECK(w.Layers().size() == 1);  // never-posted windows have no layer
    w.Post(top, Solid(2, 1, {255, 0, 0, 0}), false);
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 255, 0, 0, 255});
    a.format = 1;
    w.Relayout(top, a, 2, 1, true);
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{0, 0, 255, 255, 0, 0, 255, 255});
    w.Post(top, Solid(2, 1, {255, 0, 0, 128}), false);
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{128, 0, 127, 255, 128, 0, 127, 255});
    a.alpha = 0.5f;
    w.Relayout(top, a, 2, 1, true);
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{64, 0, 191, 255, 64, 0, 191, 255});
}

TEST_CASE("P3 compositor transparent exclusion crop and video leases") {
    WindowSystem w(2, 1);
    const auto base = w.Add({}, true);
    w.Post(base, Solid(2, 1, {0, 0, 255, 255}), false, true);
    const auto video = w.Add({}, true);
    w.Post(video, Solid(2, 1, {255, 0, 0, 255}), false, true);
    w.SetTransparentRegion(video, {{0, 0, 1, 1}});
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{0, 0, 255, 255, 255, 0, 0, 255});
    w.SetTransparentRegion(video, {});
    w.SetCrop(video, {0, 0, 1, 1});
    CHECK(w.Compose().pixels->rgba8 == std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 255});
    const auto leased = w.Layers().back().pixels;
    w.Post(video, Solid(2, 1, {0, 255, 0, 255}), false, true);
    CHECK(leased->rgba8[0] == 255);
    CHECK(w.Layers().back().pixels != leased);
}

TEST_CASE("P3 compositor full-screen GL beneath transparent app uses shared fast path") {
    WindowSystem w(1280, 720);
    const auto app = w.Add({}, true);
    WindowAttributes a;
    a.type = 1001;
    a.parent = app;
    const auto gl = w.Add(a, true);
    const auto image = Solid(1280, 720, {11, 22, 33, 255});
    w.Post(gl, image, false, true);
    w.Post(app, Solid(1280, 720, {0, 0, 0, 0}), false);
    w.SetTransparentRegion(app, {{0, 0, 1280, 720}});
    const auto composition = w.Compose();
    CHECK(composition.borrowed);
    CHECK_FALSE(composition.blank);
    CHECK(composition.pixels == image);
    w.Remove(app);
    CHECK(w.Compose().blank);
}

TEST_CASE("P3 WMS input outside target capture and focus keys") {
    WindowSystem w(800, 480);
    const auto app = w.Add({}, true);
    WindowAttributes a;
    a.width = 100;
    a.height = 100;
    a.x = 100;
    a.y = 100;
    a.flags = 0x20 | 0x40000;
    const auto popup = w.Add(a, true);
    auto delivered = w.Dispatch(Touch(0, 10, 10));
    REQUIRE(delivered.size() == 2);
    CHECK(delivered[0].window == popup);
    CHECK(delivered[0].input.action == 4);
    CHECK(delivered[1].window == app);
    delivered = w.Dispatch(Touch(2, 150, 150));
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].window == app);
    w.Dispatch(Touch(1, 150, 150));
    delivered = w.Dispatch(Touch(0, 110, 120));
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].window == popup);
    CHECK(delivered[0].input.x == 10);
    CHECK(delivered[0].input.y == 20);
    CHECK(delivered[0].input.x_offset == -100);
    AndroidBoundaryInput key;
    key.type = AndroidBoundaryInputType::key;
    key.code = 4;
    key.pressed = true;
    delivered = w.Dispatch(key);
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].window == popup);
    w.Relayout(popup, a, 100, 100, false);
    delivered = w.Dispatch(Touch(2, 120, 130));
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].input.action == 3);
    CHECK(w.Dispatch(Touch(1, 120, 130)).empty());
    CHECK(w.Focus() == app);
}

TEST_CASE("P3 WMS input flags and explicit touch region") {
    WindowSystem w(100, 100);
    const auto base = w.Add({}, true);
    WindowAttributes a;
    a.flags = 0x18;
    const auto pass = w.Add(a, true);
    CHECK(w.Dispatch(Touch(0, 5, 5)).back().window == base);
    w.Dispatch(Touch(1, 5, 5));
    a.flags = 0x28;
    w.Relayout(pass, a, 100, 100, true);
    w.SetInsets(pass, 3, {}, {}, {{20, 20, 40, 40}});
    CHECK(w.Dispatch(Touch(0, 5, 5)).back().window == base);
    w.Dispatch(Touch(1, 5, 5));
    CHECK(w.Dispatch(Touch(0, 25, 25)).back().window == pass);
    CHECK(w.CancelTouch().back().input.action == 3);
    CHECK(w.Dispatch(Touch(1, 25, 25)).empty());
}
