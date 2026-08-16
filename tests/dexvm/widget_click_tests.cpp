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
#include <utility>
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

[[nodiscard]] ogplay::loader::BinaryXmlAttribute AndroidAttribute(
    std::string name, const std::uint8_t type, const std::uint32_t data) {
    return ogplay::loader::BinaryXmlAttribute{
        .namespace_uri = "http://schemas.android.com/apk/res/android",
        .name = std::move(name),
        .value_type = type,
        .data = data,
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
    VmObjectRef activity;
    VmObjectRef video_view;
    VmObjectRef skip_button;
    VmObjectRef other_button;

    ClickVm()
        : model(strings, arrays),
          context(std::make_shared<DexVmAndroidContext>()),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
                  linker.RegisterDex(ReadFixture("widgetclick.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}) {
        context->surface_width = 100U;
        context->surface_height = 100U;
        context->vfs = &vfs;
        context->video_player_factory = FakeFactory();
        interpreter.SetLogger(&logger);
        vfs.MountHostDirectory(
            "/sdcard",
            std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");

        video_view =
            interpreter.NewIntrinsicInstance("Landroid/widget/VideoView;");
        activity = interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto bar = interpreter.NewIntrinsicInstance(
            "Landroid/widget/LinearLayout;");
        skip_button = interpreter.NewIntrinsicInstance(
            "Landroid/widget/ImageButton;");
        other_button = interpreter.NewIntrinsicInstance(
            "Landroid/widget/ImageButton;");

        const auto video_node = context->ui_tree.CreateNode(
            ui::UiClass::VideoView);
        const auto bar_node = context->ui_tree.CreateNode(
            ui::UiClass::LinearLayout);
        const auto other_node = context->ui_tree.CreateNode(
            ui::UiClass::ImageButton);
        const auto skip_node = context->ui_tree.CreateNode(
            ui::UiClass::ImageButton);
        BindViewToUiNode(*context, video_view, video_node);
        BindViewToUiNode(*context, bar, bar_node);
        BindViewToUiNode(*context, other_button, other_node);
        BindViewToUiNode(*context, skip_button, skip_node);
        context->ui_tree.Attach(context->ui_tree.Root(), video_node);
        context->ui_tree.Attach(context->ui_tree.Root(), bar_node);
        context->ui_tree.Attach(bar_node, other_node);
        context->ui_tree.Attach(bar_node, skip_node);
        auto* video_state = context->ui_tree.Get(video_node);
        video_state->layout.width.mode = ui::SizeMode::MatchParent;
        video_state->layout.height.mode = ui::SizeMode::MatchParent;
        auto* bar_state = context->ui_tree.Get(bar_node);
        bar_state->layout.width.mode = ui::SizeMode::MatchParent;
        bar_state->layout.height.mode = ui::SizeMode::WrapContent;
        bar_state->layout.layout_gravity = 0x50U;
        bar_state->gravity = 0x01U;
        bar_state->padding.top = 4;
        context->ui_tree.Get(other_node)->intrinsic = {20, 10};
        context->ui_tree.Get(skip_node)->intrinsic = {20, 10};

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

TEST_CASE("View id and Activity lookup share the bound guest identity") {
    ClickVm vm;
    CHECK(vm.CallOn(vm.skip_button, "getId", "()I").AsInt() == -1);
    CHECK_FALSE(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(-1)}).ref.IsValid());
    vm.CallOn(vm.skip_button, "setId", "(I)V", {VmValue::Int(0x1234)});
    CHECK(vm.CallOn(vm.skip_button, "getId", "()I").AsInt() == 0x1234);
    const auto found = vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x1234)});
    CHECK(found.ref == vm.skip_button);

    vm.CallOn(vm.skip_button, "setId", "(I)V", {VmValue::Int(0x5678)});
    CHECK_FALSE(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x1234)}).ref.IsValid());
    CHECK(vm.CallOn(
        vm.activity, "findViewById", "(I)Landroid/view/View;",
        {VmValue::Int(0x5678)}).ref == vm.skip_button);

    const auto node = FindViewUiNode(*vm.context, vm.skip_button.Value());
    REQUIRE(node.has_value());
    const auto extra = vm.context->ui_tree.CreateNode(ui::UiClass::View);
    CHECK_THROWS_WITH(BindViewToUiNode(*vm.context, vm.skip_button, extra),
                      "guest View already has a different UI node");
    CHECK_THROWS_WITH(BindViewToUiNode(
                          *vm.context, vm.other_button, *node),
                      "guest View already has a different UI node");
}

TEST_CASE("registry inflater attaches merge children with exact identities") {
    ClickVm vm;
    const auto stale = FindViewUiNode(*vm.context, vm.skip_button.Value());
    REQUIRE(stale.has_value());
    vm.context->ui_click_listeners[*stale] = vm.NewListener();

    std::vector<ogplay::loader::BinaryXmlElement> elements(5);
    elements[0].name = "merge";
    elements[1].name = "VideoView";
    elements[1].parent = 0;
    elements[1].attributes = {
        AndroidAttribute("id", 0x01, 101),
        AndroidAttribute("layout_width", 0x10, 0xffffffffU),
        AndroidAttribute("layout_height", 0x10, 0xffffffffU)};
    elements[2].name = "TextView";
    elements[2].parent = 0;
    elements[2].attributes = {AndroidAttribute("id", 0x01, 102)};
    elements[3].name = "LinearLayout";
    elements[3].parent = 0;
    elements[3].attributes = {
        AndroidAttribute("id", 0x01, 103),
        AndroidAttribute("orientation", 0x10, 0),
        AndroidAttribute("gravity", 0x11, 1),
        AndroidAttribute("layout_gravity", 0x11, 0x50)};
    elements[4].name = "ImageButton";
    elements[4].parent = 3;
    elements[4].attributes = {
        AndroidAttribute("id", 0x01, 104),
        AndroidAttribute("visibility", 0x10, 1)};

    const auto content = InflateUiElements(
        vm.interpreter, *vm.context, elements);
    CHECK(vm.context->ui_tree.Get(*stale) == nullptr);
    CHECK(vm.context->ui_click_listeners.empty());
    const auto* root = vm.context->ui_tree.Get(vm.context->ui_tree.Root());
    REQUIRE(root != nullptr);
    REQUIRE(root->children.size() == 3);
    CHECK(vm.context->ui_tree.Get(root->children[0])->kind ==
          ui::UiClass::VideoView);
    CHECK(vm.context->ui_tree.Get(root->children[1])->kind ==
          ui::UiClass::TextView);
    const auto row = root->children[2];
    CHECK(vm.context->ui_tree.Get(row)->kind == ui::UiClass::LinearLayout);
    REQUIRE(vm.context->ui_tree.Get(row)->children.size() == 1);
    const auto button = vm.context->ui_tree.Get(row)->children[0];
    CHECK(vm.context->ui_tree.Get(button)->visibility ==
          ui::Visibility::Invisible);
    CHECK(vm.context->ui_tree.FindByAndroidId(104) == button);
    const auto button_view = ViewObjectForUiNode(*vm.context, button);
    CHECK(vm.linker.Class(vm.model.ObjectClass(button_view)).descriptor ==
          "Landroid/widget/ImageButton;");
    CHECK(content == ViewObjectForUiNode(*vm.context, root->children[0]));
}

TEST_CASE("registry inflater rejects unsupported structural layouts") {
    ClickVm vm;
    std::vector<ogplay::loader::BinaryXmlElement> unknown(2);
    unknown[0].name = "UnknownLayout";
    unknown[1].name = "View";
    unknown[1].parent = 0;
    CHECK_THROWS_WITH(static_cast<void>(
                          InflateUiElements(vm.interpreter, *vm.context, unknown)),
                      "unsupported structural UI tag: UnknownLayout");
    CHECK(vm.context->ui_tree.Size() == 1);
    CHECK(vm.context->object_to_ui_node.empty());

    std::vector<ogplay::loader::BinaryXmlElement> nested_merge(2);
    nested_merge[0].name = "FrameLayout";
    nested_merge[1].name = "merge";
    nested_merge[1].parent = 0;
    CHECK_THROWS_WITH(static_cast<void>(InflateUiElements(
                          vm.interpreter, *vm.context, nested_merge)),
                      "merge must be the layout root");
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
