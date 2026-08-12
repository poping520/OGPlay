// Widget click dispatch semantics: visibility and click listeners are real
// state, bounds derive from the recorded layout facts (fullscreen roots and
// an edge-anchored button row), and clicks reach onClick only for visible
// listener-bearing views. The skip-button scenario stops a playing fake
// VideoView end to end.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/video/fake_video_player.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

[[nodiscard]] std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

[[nodiscard]] ogplay::video::VideoPlayerFactory FakeFactory() {
    return [](const std::filesystem::path&) {
        ogplay::video::VideoMetadata metadata;
        metadata.width = 8U;
        metadata.height = 4U;
        metadata.duration_ms = 1000;
        return std::unique_ptr<ogplay::video::VideoPlayer>(
            std::make_unique<ogplay::video::FakeVideoPlayer>(metadata, 10U));
    };
}

// Surface 100x100 with the videoview.xml shape: a fullscreen VideoView and
// a bottom bar (paddingTop 4, centered horizontally) holding two 20x10
// image buttons. Bar: y 86..100, buttons at y 90..100, x 30..50 and 50..70.
struct ClickVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    Interpreter interpreter;
    VmObjectRef video_view;
    VmObjectRef skip_button;
    VmObjectRef other_button;

    ClickVm()
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("widgetclick.dex"));
                  linker.Link();
                  return linker;
              }(),
              model,
              [this]() {
                  context->surface_width = 100U;
                  context->surface_height = 100U;
                  context->vfs = &vfs;
                  context->video_player_factory = FakeFactory();
                  IntrinsicRegistry registry;
                  RegisterAndroidBuiltins(registry, context);
                  return registry;
              }(),
              nullptr, ledger, {}) {
        interpreter.RegisterCoreBuiltins();
        interpreter.SetLogger(&logger);
        vfs.MountHostDirectory(
            "/sdcard",
            std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");

        video_view =
            interpreter.NewIntrinsicInstance("Landroid/widget/VideoView;");
        const auto bar = interpreter.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        skip_button = interpreter.NewIntrinsicInstance(
            "Landroid/widget/ImageButton;");
        other_button = interpreter.NewIntrinsicInstance(
            "Landroid/widget/ImageButton;");

        using Fact = DexVmAndroidContext::LayoutViewFact;
        constexpr auto kFill = ogplay::loader::BinaryXmlElement::
            kSizeFillParent;
        constexpr auto kWrap = ogplay::loader::BinaryXmlElement::
            kSizeWrapContent;
        Fact fullscreen;
        fullscreen.view = video_view;
        fullscreen.tag = "VideoView";
        fullscreen.layout_width = kFill;
        fullscreen.layout_height = kFill;
        context->layout_views.push_back(fullscreen);
        Fact bottom_bar;
        bottom_bar.view = bar;
        bottom_bar.tag = "LinearLayout";
        bottom_bar.layout_width = kFill;
        bottom_bar.layout_height = kWrap;
        bottom_bar.gravity = 0x01U;         // center_horizontal
        bottom_bar.layout_gravity = 0x50U;  // bottom
        bottom_bar.padding_top = 4;
        context->layout_views.push_back(bottom_bar);
        for (const auto& button : {other_button, skip_button}) {
            Fact fact;
            fact.view = button;
            fact.parent = 1;
            fact.tag = "ImageButton";
            fact.layout_width = kWrap;
            fact.layout_height = kWrap;
            fact.measured_width = 20;
            fact.measured_height = 10;
            context->layout_views.push_back(fact);
        }
    }

    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
                   const std::string& descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto receiver_class = model.ObjectClass(receiver);
        const auto index =
            linker.FindVtableIndex(receiver_class, name, descriptor);
        REQUIRE_MESSAGE(index.has_value(), name);
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = interpreter.Call(
            linker.Class(receiver_class).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value;
    }

    [[nodiscard]] VmObjectRef NewListener() {
        const auto java_class = linker.FindClass("LSkipListener;");
        REQUIRE(java_class.has_value());
        const auto clinit = interpreter.EnsureClassInitialized(*java_class);
        REQUIRE_MESSAGE(!clinit.exception.IsValid(),
                        clinit.exception_message);
        const auto init =
            linker.FindDirectMethod(*java_class, "<init>", "()V");
        REQUIRE(init.has_value());
        const auto listener = model.NewInstance(
            *java_class, linker.Class(*java_class).instance_slots);
        const auto outcome = interpreter.Call(
            *init, std::vector<VmValue>{VmValue::Ref(listener)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return listener;
    }

    [[nodiscard]] std::int32_t CallStaticInt(const std::string& name) {
        const auto java_class = linker.FindClass("LSkipListener;");
        REQUIRE(java_class.has_value());
        const auto method = linker.FindDirectMethod(*java_class, name, "()I");
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, {});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }

    void BindTarget(const VmObjectRef view) {
        const auto java_class = linker.FindClass("LSkipListener;");
        REQUIRE(java_class.has_value());
        const auto method = linker.FindDirectMethod(
            *java_class, "bind", "(Landroid/widget/VideoView;)V");
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(
            *method, std::vector<VmValue>{VmValue::Ref(view)});
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
    }

    [[nodiscard]] std::optional<std::string> Click(const float x,
                                                   const float y) {
        const auto hit = FindClickableViewAt(*context, x, y);
        if (!hit.has_value()) return std::nullopt;
        const auto error = InvokeViewOnClick(interpreter, *context, *hit);
        REQUIRE_MESSAGE(!error.has_value(), error.value_or(""));
        return "clicked";
    }
};

}  // namespace

TEST_CASE("visible button with a listener receives the click") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // Both buttons visible: row centered, skip is the second (x 50..70).
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    // The sibling without a listener consumes nothing.
    CHECK_FALSE(vm.Click(40.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
}

TEST_CASE("hidden buttons do not receive clicks and GONE reflows the row") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // INVISIBLE keeps the slot but never consumes the touch.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(4)});
    CHECK(vm.CallOn(vm.skip_button, "getVisibility", "()I").AsInt() == 4);
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());

    // GONE removes the sibling: skip alone re-centers to x 40..60.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(0)});
    vm.CallOn(vm.other_button, "setVisibility", "(I)V", {VmValue::Int(8)});
    CHECK(vm.Click(50.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    CHECK_FALSE(vm.Click(65.0F, 95.0F).has_value());
}

TEST_CASE("touches outside every clickable view fall through") {
    ClickVm vm;
    const auto listener = vm.NewListener();
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // The fullscreen VideoView has no listener, so the middle of the
    // screen belongs to Activity.onTouchEvent.
    CHECK_FALSE(vm.Click(50.0F, 50.0F).has_value());
    // Inside the bar but outside both buttons.
    CHECK_FALSE(vm.Click(10.0F, 95.0F).has_value());
}

TEST_CASE("setVisibility rejects values outside VISIBLE/INVISIBLE/GONE") {
    ClickVm vm;
    const auto receiver_class = vm.model.ObjectClass(vm.skip_button);
    const auto index = vm.linker.FindVtableIndex(receiver_class,
                                                 "setVisibility", "(I)V");
    REQUIRE(index.has_value());
    const auto outcome = vm.interpreter.Call(
        vm.linker.Class(receiver_class).vtable[*index],
        std::vector<VmValue>{VmValue::Ref(vm.skip_button), VmValue::Int(3)});
    CHECK(outcome.exception.IsValid());
}

TEST_CASE("skip click stops the playing fake video end to end") {
    ClickVm vm;
    vm.CallOn(vm.video_view, "setVideoPath", "(Ljava/lang/String;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8(
                  "/sdcard/short-mp4v-aac.mp4"))});
    vm.CallOn(vm.video_view, "start", "()V");
    CHECK_FALSE(vm.context->video_views.empty());

    const auto listener = vm.NewListener();
    vm.BindTarget(vm.video_view);
    vm.CallOn(vm.skip_button, "setOnClickListener",
              "(Landroid/view/View$OnClickListener;)V",
              {VmValue::Ref(listener)});
    // The trailer flow: the button starts hidden, a touch elsewhere would
    // reveal it (activity logic), then the click stops playback.
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(8)});
    CHECK_FALSE(vm.Click(60.0F, 95.0F).has_value());
    vm.CallOn(vm.skip_button, "setVisibility", "(I)V", {VmValue::Int(0)});
    CHECK(vm.Click(60.0F, 95.0F).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 1);
    CHECK(vm.context->video_views.empty());  // stopPlayback ran
}
