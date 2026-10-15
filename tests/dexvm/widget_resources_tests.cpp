#include "boot_dex.h"
// Widget state, resolved UiTree geometry and listener dispatch integration.

#include <doctest/doctest.h>

#include <array>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

#include "widget_fixture.h"

using namespace ogplay::test::widget;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("UI resources resolve string color dimension and image state") {
    ClickVm vm;
    vm.context->ui_density = 2.0F;
    vm.context->ui_scaled_density = 3.0F;
    vm.context->arsc.entries = {
        {.resource_id = 100, .type_name = "string", .entry_name = "go",
         .string_value = "GO", .value_type = 0x03, .value_data = 0},
        {.resource_id = 107, .type_name = "string", .entry_name = "unicode",
         .string_value = "\xE6\xA4\x8D\xE7\x89\xA9",
         .value_type = 0x03, .value_data = 0},
        {.resource_id = 101, .type_name = "color", .entry_name = "red",
         .value_type = 0x1c, .value_data = 0xffff0000U},
        {.resource_id = 102, .type_name = "color", .entry_name = "alias",
         .value_type = 0x01, .value_data = 101},
        {.resource_id = 103, .type_name = "dimen", .entry_name = "pad",
         .value_type = 0x05, .value_data = 0x00000801U},
        {.resource_id = 104, .type_name = "dimen", .entry_name = "label",
         .value_type = 0x05, .value_data = 0x00000802U},
        {.resource_id = 105, .type_name = "color", .entry_name = "cycle_a",
         .value_type = 0x01, .value_data = 106},
        {.resource_id = 106, .type_name = "color", .entry_name = "cycle_b",
         .value_type = 0x01, .value_data = 105},
    };
    CHECK(ResolveUiString(*vm.context, 100) == u"GO");
    CHECK(ResolveResourceString(*vm.context, 107) ==
          "\xE6\xA4\x8D\xE7\x89\xA9");
    CHECK_THROWS_WITH(static_cast<void>(ResolveUiString(*vm.context, 107)),
                      "fixed-font XML text must use the supported ASCII subset");
    CHECK(ResolveUiColor(*vm.context, 102) == 0xff0000ffU);
    CHECK(ResolveUiDimension(*vm.context, 103, false) == 16);
    CHECK(ResolveUiDimension(*vm.context, 104, true) == 24);
    CHECK_THROWS_WITH(static_cast<void>(ResolveUiColor(*vm.context, 105)),
                      "UI resource reference cycle");

    std::vector<ogplay::loader::BinaryXmlElement> elements(1);
    elements[0].name = "TextView";
    elements[0].attributes = {
        AndroidAttribute("text", 0x01, 100),
        AndroidAttribute("textColor", 0x01, 102),
        AndroidAttribute("padding", 0x01, 103),
        AndroidAttribute("textSize", 0x01, 104)};
    const auto text = InflateUiElements(vm.interpreter, *vm.context, elements);
    const auto text_node = FindViewUiNode(*vm.context, text.Value());
    REQUIRE(text_node.has_value());
    CHECK(vm.context->ui_tree.Get(*text_node)->text == u"GO");
    CHECK(vm.context->ui_tree.Get(*text_node)->text_color == 0xff0000ffU);
    CHECK(vm.context->ui_tree.Get(*text_node)->padding ==
          ui::Insets{16, 16, 16, 16});
    CHECK(vm.context->ui_tree.Get(*text_node)->text_size_px == 24.0F);

    std::vector<ogplay::loader::BinaryXmlElement> styled(5);
    styled[0].name = "LinearLayout";
    styled[1].name = "TextView";
    styled[1].parent = 0;
    styled[1].attributes = {StyleAttribute(0x01, 0x01030046U)};
    styled[2].name = "TextView";
    styled[2].parent = 0;
    styled[2].attributes = {StyleAttribute(0x01, 0x01030046U),
                            AndroidAttribute("textSize", 0x05, 0x00001202U)};
    styled[3].name = "EditText";
    styled[3].parent = 0;
    styled[3].attributes = {
        AndroidAttribute("textSize", 0x05, 0x00001e01U),
        AndroidAttribute("enabled", 0x12, 0),
        AndroidAttribute("clickable", 0x12, 1)};
    styled[4].name = "ProgressBar";
    styled[4].parent = 0;
    styled[4].attributes = {StyleAttribute(0x02, 0x01010078U)};
    const auto styled_root =
        InflateUiElements(vm.interpreter, *vm.context, styled);
    const auto root_node = FindViewUiNode(*vm.context, styled_root.Value());
    REQUIRE(root_node.has_value());
    REQUIRE(vm.context->ui_tree.Get(*root_node)->children.size() == 4);
    const auto small = vm.context->ui_tree.Get(
        vm.context->ui_tree.Get(*root_node)->children[0]);
    const auto overridden = vm.context->ui_tree.Get(
        vm.context->ui_tree.Get(*root_node)->children[1]);
    const auto edit = vm.context->ui_tree.Get(
        vm.context->ui_tree.Get(*root_node)->children[2]);
    CHECK(small->text_size_px == 42.0F);       // Small is 14sp
    CHECK(overridden->text_size_px == 54.0F);  // explicit 18sp wins
    CHECK(edit->text_size_px == 60.0F);    // 30dp at density 2
    CHECK(edit->minimum.height == 96);     // Holo EditText 48dp floor
    CHECK_FALSE(edit->enabled);
    CHECK(edit->clickable);

    styled[4].attributes = {StyleAttribute(0x02, 0x01010077U)};
    CHECK_THROWS_WITH(
        static_cast<void>(
            InflateUiElements(vm.interpreter, *vm.context, styled)),
        "UI style resource is outside the registered API 19 projection");

    const auto image = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/ImageView;");
    vm.CallOn(image, "setImageResource", "(I)V", {VmValue::Int(102)});
    const auto scale = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/ImageView$ScaleType;");
    vm.context->ui_image_scale_types[scale.Value()] =
        ui::ImageScaleType::CenterCrop;
    vm.CallOn(image, "setScaleType",
              "(Landroid/widget/ImageView$ScaleType;)V",
              {VmValue::Ref(scale)});
    const auto image_node = FindViewUiNode(*vm.context, image.Value());
    REQUIRE(image_node.has_value());
    CHECK(vm.context->ui_tree.Get(*image_node)->intrinsic == ui::Size{1, 1});
    CHECK(vm.context->ui_tree.Get(*image_node)->image_scale_type ==
          ui::ImageScaleType::CenterCrop);
}

TEST_CASE("TextView Java state controls deterministic measure and draw state") {
    ClickVm vm;
    const auto text = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/TextView;");
    vm.CallOn(text, "setText", "(Ljava/lang/CharSequence;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("Hi"))});
    vm.CallOn(text, "setTextColor", "(I)V",
              {VmValue::Int(static_cast<std::int32_t>(0xffff0000U))});
    vm.CallOn(text, "setTextSize", "(F)V", {VmValue::Float(8.0F)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(text)});
    const auto returned =
        vm.CallOn(text, "getText", "()Ljava/lang/CharSequence;").ref;
    CHECK(vm.model.StringValue(returned) == u"Hi");
    CHECK(vm.CallOn(text, "getWidth", "()I").AsInt() == 11);
    CHECK(vm.CallOn(text, "getHeight", "()I").AsInt() == 7);
    const auto node = FindViewUiNode(*vm.context, text.Value());
    REQUIRE(node.has_value());
    CHECK(vm.context->ui_tree.Get(*node)->text_color == 0xff0000ffU);

    vm.CallOn(text, "setText", "(Ljava/lang/CharSequence;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("H"))});
    CHECK(vm.CallOn(text, "getWidth", "()I").AsInt() == 5);

    const auto receiver_class = vm.model.ObjectClass(text);
    const auto method = vm.linker.FindVtableIndex(
        receiver_class, "setText", "(Ljava/lang/CharSequence;)V");
    REQUIRE(method.has_value());
    const auto multiline = vm.interpreter.Call(
        vm.linker.Class(receiver_class).vtable[*method],
        std::vector<VmValue>{
            VmValue::Ref(text),
            VmValue::Ref(vm.interpreter.NewStringUtf8("two\nlines"))});
    CHECK_FALSE(multiline.exception.IsValid());
    CHECK(vm.context->ui_tree.Get(*node)->text == u"two\nlines");
}

TEST_CASE("EditText Editable preserves identity text and watcher registration") {
    ClickVm vm;
    const auto edit = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/EditText;");
    vm.CallOn(edit, "setText", "(Ljava/lang/CharSequence;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("13"))});
    const auto first = vm.CallOn(edit, "getText",
                                 "()Landroid/text/Editable;").ref;
    const auto second = vm.CallOn(edit, "getText",
                                  "()Landroid/text/Editable;").ref;
    CHECK(first == second);
    const auto value = vm.CallOn(first, "toString", "()Ljava/lang/String;").ref;
    CHECK(vm.model.StringValue(value) == u"13");

    const auto watcher = vm.interpreter.NewIntrinsicInstance(
        "LRecordingTextWatcher;");
    vm.CallOn(edit, "addTextChangedListener",
              "(Landroid/text/TextWatcher;)V", {VmValue::Ref(watcher)});
    REQUIRE(vm.context->text_watchers.contains(edit.Value()));
    CHECK(vm.context->text_watchers.at(edit.Value()).front() == watcher);
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(edit)});
    static_cast<void>(vm.interpreter.CollectGarbage("text-watcher-owner"));

    const auto node = FindViewUiNode(*vm.context, edit.Value());
    REQUIRE(node.has_value());
    vm.context->ui_tree.Get(*node)->numeric_input = true;
    vm.context->ui_tree.Get(*node)->max_length = 3;
    vm.CallOn(first, "replace", "(IILjava/lang/CharSequence;)Landroid/text/Editable;",
              {VmValue::Int(1), VmValue::Int(2),
               VmValue::Ref(vm.interpreter.NewStringUtf8("x45"))});
    CHECK(vm.context->ui_tree.Get(*node)->text == u"145");
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});
    CHECK(vm.last_text_range == std::array<std::int32_t, 3>{1, 1, 2});

    vm.text_events.clear();
    vm.CallOn(first, "clear", "()V");
    CHECK(vm.context->ui_tree.Get(*node)->text.empty());
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});

    vm.text_events.clear();
    vm.CallOn(edit, "setText", "(Ljava/lang/CharSequence;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("9z876"))});
    CHECK(vm.context->ui_tree.Get(*node)->text == u"987");
    CHECK(vm.text_events == std::vector<std::string>{"before", "on", "after"});
}

TEST_CASE("Button three-argument constructor applies the pinned default style") {
    ClickVm vm;
    vm.CallOn(vm.activity, "attachBaseContext", "(Landroid/content/Context;)V",
        {VmValue::Ref(vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;"))});
    vm.CallOn(vm.activity, "setTheme", "(I)V", {VmValue::Int(0x01030007)});
    vm.context->ui_density = 2.0F;
    vm.context->ui_scaled_density = 2.0F;
    const auto button = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/Button;");
    constexpr std::int32_t kButtonStyleSmall = 0x01010049;
    vm.CallDirect(button, "Landroid/widget/Button;", "<init>",
                  "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(vm.activity), VmValue::Ref(VmObjectRef{}),
                   VmValue::Int(kButtonStyleSmall)});
    const auto node = FindViewUiNode(*vm.context, button.Value());
    REQUIRE(node.has_value());
    const auto* state = vm.context->ui_tree.Get(*node);
    CHECK(state->kind == ui::UiClass::Button);
    CHECK(state->text_size_px == 28.0F);  // 14sp at scaled density 2
    CHECK(state->text_color == 0x000000ffU);  // primary_text_light
    CHECK(state->gravity == 0x11U);
    CHECK(state->clickable);

    // defStyleAttr 0 keeps the plain one-argument defaults.
    const auto plain = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/Button;");
    vm.CallDirect(plain, "Landroid/widget/Button;", "<init>",
                  "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(vm.activity), VmValue::Ref(VmObjectRef{}),
                   VmValue::Int(0)});
    const auto plain_node = FindViewUiNode(*vm.context, plain.Value());
    REQUIRE(plain_node.has_value());
    const auto* plain_state = vm.context->ui_tree.Get(*plain_node);
    CHECK(plain_state->text_size_px == 8.0F);
    CHECK(plain_state->gravity == 0U);

    // Unknown framework attrs and non-null AttributeSet fail explicitly.
    const auto attributes =
        vm.interpreter.NewStringUtf8("not an AttributeSet");
    const auto reject = [&](const VmObjectRef attrs,
                            const std::int32_t style) {
        const auto target = vm.interpreter.NewIntrinsicInstance(
            "Landroid/widget/Button;");
        const auto button_class = vm.linker.FindClass("Landroid/widget/Button;");
        const auto init = vm.linker.FindDirectMethod(
            *button_class, "<init>",
            "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        const auto outcome = vm.interpreter.Call(
            *init, std::vector<VmValue>{VmValue::Ref(target),
                                        VmValue::Ref(vm.activity),
                                        VmValue::Ref(attrs),
                                        VmValue::Int(style)});
        CHECK(outcome.exception.IsValid());
    };
    reject(VmObjectRef{}, 0x01010044);  // unregistered framework attr
    reject(attributes, 0x01010049);
}

TEST_CASE("DVM229 resource Drawables retain image backing alpha and queued GC ownership") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto base_root = vm.interpreter.ProtectReferences(std::array{base});
        vm.CallDirect(base, "Landroid/content/Context;", "<init>", "()V");
        vm.CallOn(vm.activity, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        std::ifstream input(std::filesystem::path(OGPLAY_SOURCE_DIR) / "tests/fixtures/ui/drawable_resource.zip", std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        vm.context->apk_bytes.resize(data.size());
        std::transform(data.begin(), data.end(), vm.context->apk_bytes.begin(), [](char value) { return static_cast<std::byte>(value); });
        vm.context->archive = ogplay::loader::ParseApkArchive(vm.context->apk_bytes);
        vm.context->arsc.entries = {
            {.resource_id=101, .type_name="drawable", .entry_name="image", .string_value="res/drawable/test.png", .value_type=3},
            {.resource_id=102, .type_name="drawable", .entry_name="alias", .value_type=1, .value_data=101},
            {.resource_id=103, .type_name="color", .entry_name="color", .value_type=0x1c, .value_data=0xff112233},
            {.resource_id=104, .type_name="drawable", .entry_name="xml", .string_value="res/drawable/test.xml", .value_type=3}};
        const auto resources = vm.CallOn(vm.activity, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto resource_roots = vm.interpreter.ProtectReferences(std::array{resources});
        const auto get = [&](int id) { return vm.CallOn(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(id)}).ref; };
        const auto image = vm.interpreter.NewIntrinsicInstance("Landroid/widget/ImageView;");
        const auto image_root = vm.interpreter.ProtectReferences(std::array{image});
        vm.CallDirect(image, "Landroid/widget/ImageView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(vm.activity)});
        VmObjectRef drawable, bitmap;
        {
            drawable = get(102);
            const auto root = vm.interpreter.ProtectReferences(std::array{drawable});
            CHECK(vm.linker.Class(vm.model.ObjectClass(drawable)).descriptor == "Landroid/graphics/drawable/BitmapDrawable;");
            CHECK(vm.CallOn(drawable, "getIntrinsicWidth", "()I").AsInt() == 2);
            CHECK(vm.CallOn(drawable, "getIntrinsicHeight", "()I").AsInt() == 1);
            bitmap = vm.CallOn(drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref;
            REQUIRE(bitmap.IsValid());
            const auto second = get(101);
            const auto second_root = vm.interpreter.ProtectReferences(std::array{second});
            CHECK(second != drawable);
            vm.CallOn(drawable, "setAlpha", "(I)V", {VmValue::Int(128)});
            CHECK(vm.context->ui_drawables.at(second.Value()).alpha == 255);
            vm.CallOn(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(drawable)});
        }
        static_cast<void>(vm.interpreter.CollectGarbage("resource-image-binding"));
        CHECK(vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == drawable);
        CHECK(vm.CallOn(drawable, "getBitmap", "()Landroid/graphics/Bitmap;").ref == bitmap);
        const auto node = *FindViewUiNode(*vm.context, image.Value());
        auto* projected = vm.context->ui_tree.Get(node);
        projected->layout.width = {ui::SizeMode::Fixed, 2}; projected->layout.height = {ui::SizeMode::Fixed, 1};
        vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), node);
        const auto render = [&] {
            RefreshAndroidImageDrawables(*vm.context);
            ui::LayoutUiTree(vm.context->ui_tree, {100,100});
            return ui::RasterizeUiOverlay(ui::BuildUiRenderList(vm.context->ui_tree, vm.context->ui_bitmaps), {100,100});
        };
        auto frame = render();
        CHECK(frame.rgba8[0] == 128); CHECK(frame.rgba8[1] == 0); CHECK(frame.rgba8[3] == 128);
        CHECK(frame.rgba8[4] == 0); CHECK(frame.rgba8[5] == 128); CHECK(frame.rgba8[7] == 128);
        vm.context->bitmaps.at(bitmap.Value()).pixels->Set(0, 0xff0000ff);
        frame = render(); CHECK(frame.rgba8[0] == 0); CHECK(frame.rgba8[2] == 128);
        std::vector<std::byte> native_pixels;
        const ogplay::memory::GuestAddress native_base(0x1000);
        const NioDirectMemoryAccess access{
            .allocate = [&](std::uint32_t size) { native_pixels.resize(size); return native_base; },
            .release = [&](ogplay::memory::GuestAddress, std::uint32_t) { native_pixels.clear(); },
            .read = [&](ogplay::memory::GuestAddress address, std::span<std::byte> output) {
                std::copy_n(native_pixels.begin() + (address.Value() - native_base.Value()), output.size(), output.begin());
            },
            .write = [&](ogplay::memory::GuestAddress address, std::span<const std::byte> input) {
                std::copy(input.begin(), input.end(), native_pixels.begin() + (address.Value() - native_base.Value()));
            }};
        const auto pixels = vm.context->bitmaps.at(bitmap.Value()).pixels;
        static_cast<void>(pixels->Lock(access));
        native_pixels[0] = std::byte{0}; native_pixels[1] = std::byte{255}; native_pixels[2] = std::byte{0};
        frame = render(); CHECK(frame.rgba8[1] == 128); CHECK(frame.rgba8[2] == 0);
        pixels->Unlock();
        frame = render(); CHECK(frame.rgba8[1] == 128); CHECK(frame.rgba8[2] == 0);
        const auto color = get(103);
        const auto color_root = vm.interpreter.ProtectReferences(std::array{color});
        CHECK(vm.linker.Class(vm.model.ObjectClass(color)).descriptor == "Landroid/graphics/drawable/ColorDrawable;");
        CHECK(vm.CallOn(color, "getIntrinsicWidth", "()I").AsInt() == -1);
        vm.CallOn(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(color)});
        frame = render(); CHECK(frame.rgba8[0] == 0x11); CHECK(frame.rgba8[1] == 0x22); CHECK(frame.rgba8[2] == 0x33);
        vm.CallOn(color, "setColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff445566U))});
        frame = render(); CHECK(frame.rgba8[0] == 0x44); CHECK(frame.rgba8[1] == 0x55); CHECK(frame.rgba8[2] == 0x66);
        projected->padding.left = 1;
        frame = render(); CHECK(frame.rgba8[3] == 0); CHECK(frame.rgba8[4] == 0x44);
        projected->padding.left = 0;
        const auto previous = vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        const auto resource_class = vm.model.ObjectClass(resources);
        const auto method = *vm.linker.FindVtableIndex(resource_class, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;");
        const auto invalid = vm.interpreter.Call(vm.linker.Class(resource_class).vtable[method], std::vector{VmValue::Ref(resources), VmValue::Int(0)});
        REQUIRE(invalid.exception.IsValid());
        CHECK(vm.linker.Class(invalid.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
        const auto system = vm.interpreter.Call(*vm.linker.FindDirectMethod(resource_class, "getSystem", "()Landroid/content/res/Resources;"), std::vector<VmValue>{}).value.ref;
        const auto system_root = vm.interpreter.ProtectReferences(std::array{system});
        const auto system_image = vm.interpreter.Call(vm.linker.Class(resource_class).vtable[method], std::vector{VmValue::Ref(system), VmValue::Int(101)});
        REQUIRE(system_image.exception.IsValid());
        CHECK(vm.linker.Class(system_image.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
        const auto unsupported = vm.interpreter.Call(vm.linker.Class(resource_class).vtable[method], std::vector{VmValue::Ref(resources), VmValue::Int(104)});
        REQUIRE(unsupported.exception.IsValid());
        CHECK(vm.linker.Class(unsupported.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == previous);
        vm.CallOn(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref.IsValid());
        frame = render(); CHECK(frame.rgba8[3] == 0);
        const auto empty = vm.interpreter.NewIntrinsicInstance("Landroid/graphics/drawable/BitmapDrawable;");
        const auto empty_root = vm.interpreter.ProtectReferences(std::array{empty});
        vm.CallDirect(empty, "Landroid/graphics/drawable/BitmapDrawable;", "<init>", "(Landroid/graphics/Bitmap;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(vm.CallOn(empty, "getBitmap", "()Landroid/graphics/Bitmap;").ref.IsValid());
        CHECK(vm.CallOn(empty, "getIntrinsicWidth", "()I").AsInt() == -1);
        vm.CallOn(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(empty)});
        frame = render(); CHECK(frame.rgba8[3] == 0);
        const auto legacy = get(101);
        const auto legacy_root = vm.interpreter.ProtectReferences(std::array{legacy});
        vm.CallOn(image, "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(legacy)});
        vm.CallOn(image, "setImageResource", "(I)V", {VmValue::Int(101)});
        CHECK_FALSE(vm.context->ui_view_images.contains(image.Value()));
        frame = render(); CHECK(frame.rgba8[0] == 255); CHECK(frame.rgba8[1] == 0); CHECK(frame.rgba8[3] == 255);
    }
}

TEST_CASE("ImageView setImageBitmap delegates virtually and retains shared pixels") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto owner = vm.interpreter.NewIntrinsicInstance("LImageContextProbe;");
        const auto owner_root = vm.interpreter.ProtectReferences(std::array{owner});
        vm.CallDirect(owner, "Landroid/content/Context;", "<init>", "()V");
        const auto image = vm.interpreter.NewIntrinsicInstance("LImageDrawableProbe;");
        const auto image_root = vm.interpreter.ProtectReferences(std::array{image});
        vm.CallDirect(image, "Landroid/widget/ImageView;", "<init>",
                      "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto set = [&](VmObjectRef bitmap) {
            vm.CallOn(image, "setImageBitmap", "(Landroid/graphics/Bitmap;)V", {VmValue::Ref(bitmap)});
            return vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        };
        const auto node = *FindViewUiNode(*vm.context, image.Value());
        vm.context->ui_tree.Attach(vm.context->ui_tree.Root(), node);
        vm.context->ui_tree.Get(node)->layout.width = {ui::SizeMode::Fixed, 2};
        vm.context->ui_tree.Get(node)->layout.height = {ui::SizeMode::Fixed, 1};
        const auto render = [&] {
            RefreshAndroidImageDrawables(*vm.context);
            ui::LayoutUiTree(vm.context->ui_tree, {100, 100});
            return ui::RasterizeUiOverlay(ui::BuildUiRenderList(vm.context->ui_tree, vm.context->ui_bitmaps), {100, 100});
        };
        VmObjectRef bitmap, replacement;
        {
            bitmap = vm.interpreter.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
            const auto pixels = std::make_shared<BitmapPixels>(2, 1, 5,
                std::vector<std::uint32_t>{0xffff0000, 0xff0000ff});
            vm.context->bitmaps[bitmap.Value()] = {2, 1, pixels};
            const auto first = set(bitmap);
            const auto first_root = vm.interpreter.ProtectReferences(std::array{first});
            REQUIRE(first.IsValid());
            CHECK(vm.linker.Class(vm.model.ObjectClass(first)).descriptor == "Landroid/graphics/drawable/BitmapDrawable;");
            CHECK(vm.CallOn(first, "getBitmap", "()Landroid/graphics/Bitmap;").ref == bitmap);
            CHECK(vm.context->ui_drawables.at(first.Value()).callback_node == node);
            static_cast<void>(vm.interpreter.CollectGarbage("image-bitmap-owner"));
            CHECK(vm.context->bitmaps.contains(bitmap.Value()));
            auto frame = render();
            CHECK(frame.rgba8[0] == 255); CHECK(frame.rgba8[3] == 255);
            CHECK(frame.rgba8[6] == 255); CHECK(frame.rgba8[7] == 255);
            vm.CallOn(first, "setAlpha", "(I)V", {VmValue::Int(64)});
            const auto again = set(bitmap);
            const auto again_root = vm.interpreter.ProtectReferences(std::array{again});
            CHECK(again != first);
            CHECK_FALSE(vm.context->ui_drawables.at(first.Value()).callback_node.has_value());
            CHECK(vm.context->ui_drawables.at(again.Value()).alpha == 255);
            CHECK(vm.context->bitmaps.at(bitmap.Value()).pixels == pixels);
            pixels->Set(0, 0xff00ff00);
            frame = render(); CHECK(frame.rgba8[0] == 0); CHECK(frame.rgba8[1] == 255);
            replacement = vm.interpreter.NewIntrinsicInstance("Landroid/graphics/Bitmap;");
            vm.context->bitmaps[replacement.Value()] = {1, 1,
                std::make_shared<BitmapPixels>(1, 1, 5, std::vector<std::uint32_t>{0xffffffff})};
            const auto changed = set(replacement);
            const auto changed_root = vm.interpreter.ProtectReferences(std::array{changed});
            CHECK_FALSE(vm.context->ui_drawables.at(again.Value()).callback_node.has_value());
            CHECK(vm.context->ui_tree.Get(node)->intrinsic == ui::Size{1, 1});
            CHECK_FALSE(vm.context->bitmaps.at(bitmap.Value()).recycled);
            const auto empty = set(VmObjectRef{});
            REQUIRE(empty.IsValid());
            CHECK_FALSE(vm.CallOn(empty, "getBitmap", "()Landroid/graphics/Bitmap;").ref.IsValid());
            CHECK_FALSE(vm.context->ui_drawables.at(changed.Value()).callback_node.has_value());
            CHECK(vm.context->ui_tree.Get(node)->intrinsic == ui::Size{});
            frame = render(); CHECK(frame.rgba8[3] == 0);
            CHECK(vm.image_resource_queries == 4);
        }
        static_cast<void>(vm.interpreter.CollectGarbage("image-bitmap-replaced"));
        CHECK_FALSE(vm.context->bitmaps.contains(bitmap.Value()));
        CHECK_FALSE(vm.context->bitmaps.contains(replacement.Value()));
        const auto previous = vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        vm.image_delegate_exception = vm.interpreter.NewIntrinsicInstance("Ljava/lang/IllegalStateException;");
        const auto exception_root = vm.interpreter.ProtectReferences(std::array{vm.image_delegate_exception});
        const auto method = *vm.linker.FindVtableIndex(vm.model.ObjectClass(image), "setImageBitmap", "(Landroid/graphics/Bitmap;)V");
        const auto invoke = [&] {
            return vm.interpreter.Call(vm.linker.Class(vm.model.ObjectClass(image)).vtable[method],
                std::array{VmValue::Ref(image), VmValue::Ref(VmObjectRef{})});
        };
        // Resource delegate failures propagate before replacing the current image.
        auto outcome = invoke();
        CHECK(outcome.exception == vm.image_delegate_exception);
        CHECK(vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == previous);
        const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto base_root = vm.interpreter.ProtectReferences(std::array{base});
        vm.CallDirect(base, "Landroid/content/Context;", "<init>", "()V");
        AssignViewContext(vm.interpreter, *vm.context, image, base);
        // setImageDrawable overrides preserve their exception object after allocation/GC.
        outcome = invoke();
        CHECK(outcome.exception == vm.image_delegate_exception);
        CHECK(vm.CallOn(image, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref == previous);
    }
}

TEST_CASE("Button inherits View background identity and Drawable alpha invalidates") {
    ClickVm vm;
    const auto button = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/Button;");
    vm.CallDirect(button, "Landroid/widget/Button;", "<init>",
                  "(Landroid/content/Context;)V",
                  {VmValue::Ref(vm.activity)});
    const auto first = vm.CallOn(
        button, "getBackground",
        "()Landroid/graphics/drawable/Drawable;").ref;
    REQUIRE(first.IsValid());
    CHECK(vm.CallOn(button, "getBackground",
                    "()Landroid/graphics/drawable/Drawable;").ref == first);
    const auto node = FindViewUiNode(*vm.context, button.Value());
    REQUIRE(node.has_value());
    vm.context->ui_tree.ClearDrawDirty();
    vm.CallOn(first, "setAlpha", "(I)V", {VmValue::Int(96)});
    CHECK(vm.context->ui_tree.Get(*node)->background_alpha ==
          doctest::Approx(96.0F / 255.0F));
    CHECK(vm.context->ui_tree.Get(*node)->draw_dirty);

    vm.context->arsc.entries = {
        {.resource_id = 101, .type_name = "color", .entry_name = "red",
         .value_type = 0x1c, .value_data = 0xffff0000U},
    };
    vm.CallOn(button, "setBackgroundResource", "(I)V", {VmValue::Int(101)});
    const auto replacement = vm.CallOn(
        button, "getBackground",
        "()Landroid/graphics/drawable/Drawable;").ref;
    REQUIRE(replacement.IsValid());
    CHECK(replacement != first);
    CHECK(vm.context->ui_tree.Get(*node)->background_resource_id == 101U);
    CHECK(vm.context->ui_tree.Get(*node)->background_alpha == 1.0F);
    vm.context->ui_tree.ClearDrawDirty();
    vm.CallOn(first, "setAlpha", "(I)V", {VmValue::Int(12)});
    CHECK(vm.context->ui_tree.Get(*node)->background_alpha == 1.0F);
    CHECK_FALSE(vm.context->ui_tree.Get(*node)->draw_dirty);
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(button)});
    static_cast<void>(vm.interpreter.CollectGarbage("replaced-background"));
    CHECK_FALSE(vm.context->ui_drawables.contains(first.Value()));
    CHECK(vm.context->ui_drawables.contains(replacement.Value()));

    const auto plain = vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
    vm.CallDirect(plain, "Landroid/view/View;", "<init>",
                  "(Landroid/content/Context;)V", {VmValue::Ref(vm.activity)});
    CHECK_FALSE(vm.CallOn(plain, "getBackground",
                          "()Landroid/graphics/drawable/Drawable;").ref.IsValid());
}

TEST_CASE("API 19 resource qualifiers use ordered matching independent of table order") {
    ClickVm vm;
    vm.context->surface_width = 800;
    vm.context->surface_height = 480;
    vm.context->ui_density = 1.0F;
    const auto candidate = [](const std::uint32_t id,
                              const std::uint32_t value) {
        return ogplay::loader::ArscEntry{.resource_id = id, .type_name = "color",
            .entry_name = "candidate", .value_type = 0x1c,
            .value_data = value};
    };
    auto def = candidate(201, 1);
    auto portrait = candidate(201, 2); portrait.orientation = 1;
    auto land = candidate(201, 3); land.orientation = 2;
    auto normal = candidate(202, 4); normal.screen_layout = 2;
    auto large = candidate(202, 5); large.screen_layout = 3;
    auto xlarge = candidate(202, 6); xlarge.screen_layout = 4;
    auto sdk18 = candidate(203, 7); sdk18.sdk_version = 18;
    auto sdk19 = candidate(203, 8); sdk19.sdk_version = 19;
    auto d240 = candidate(204, 9); d240.density = 240;
    auto d320 = candidate(204, 10); d320.density = 320;
    auto width = candidate(205, 11); width.screen_width_dp = 600;
    auto height = candidate(205, 12); height.screen_height_dp = 400;
    auto sw = candidate(205, 13); sw.smallest_width_dp = 320;
    auto base_bitmap = candidate(206, 0xffff0000U);
    auto dense_bitmap = candidate(206, 0xff0000ffU); dense_bitmap.density = 320;
    vm.context->arsc.entries = {def, portrait, land, normal, large, xlarge,
                                sdk18, sdk19, d240, d320, width, height, sw,
                                base_bitmap, dense_bitmap};
    const auto verify = [&] {
        CHECK(ResolveUiResourceEntry(*vm.context, 201).value_data == 3);
        CHECK(ResolveUiResourceEntry(*vm.context, 202).value_data == 5);
        CHECK(ResolveUiResourceEntry(*vm.context, 203).value_data == 8);
        CHECK(ResolveUiResourceEntry(*vm.context, 204).value_data == 9);
        CHECK(ResolveUiResourceEntry(*vm.context, 205).value_data == 13);
    };
    verify();
    std::reverse(vm.context->arsc.entries.begin(),
                 vm.context->arsc.entries.end());
    verify();
    CHECK(ResolveUiDrawable(*vm.context, 206)->rgba8[0] == 0xff);
    vm.context->ui_density = 2.0F;
    CHECK(ResolveUiDrawable(*vm.context, 206)->rgba8[2] == 0xff);
}

TEST_CASE("DVM-123 Button constructor checks the supplied Context theme") {
    ClickVm vm;
    vm.CallOn(vm.activity, "attachBaseContext", "(Landroid/content/Context;)V",
        {VmValue::Ref(vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;"))});
    const auto type = *vm.linker.FindClass("Landroid/widget/Button;");
    const auto init = *vm.linker.FindDirectMethod(type, "<init>",
        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
    const auto construct = [&](VmObjectRef owner) {
        const auto view = vm.interpreter.NewIntrinsicInstance("Landroid/widget/Button;");
        return vm.interpreter.Call(init, std::vector<VmValue>{VmValue::Ref(view),
            VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x01010049)});
    };
    auto result = construct(VmObjectRef{});
    REQUIRE(result.exception.IsValid());
    CHECK(vm.linker.Class(result.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    vm.CallOn(vm.activity, "setTheme", "(I)V", {VmValue::Int(0x0103006b)}); // Holo is not legacy
    result = construct(vm.activity);
    REQUIRE(result.exception.IsValid());
    CHECK(vm.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");

    // APK theme inheritance and an alias to the registered legacy style work.
    vm.context->arsc.entries = {{.resource_id = 0x7f030001U, .type_name = "style",
        .entry_name = "AppTheme", .is_complex = true, .parent = 0x01030007U,
        .bag = {{0x01010049U, 2, 0x01010048U, {}}}}};
    vm.CallOn(vm.activity, "setTheme", "(I)V", {VmValue::Int(0x7f030001)});
    CHECK_FALSE(construct(vm.activity).exception.IsValid());
    // An app-supplied widget style must not be ignored and replaced by 14sp.
    vm.context->arsc.entries[0].bag[0] = {0x01010049U, 1, 0x7f030002U, {}};
    result = construct(vm.activity);
    REQUIRE(result.exception.IsValid());
    CHECK(vm.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("compound drawables resolve resources and drive measure") {
    ClickVm vm;
    vm.context->arsc.entries = {
        {.resource_id = 101, .type_name = "color", .entry_name = "red",
         .value_type = 0x1c, .value_data = 0xffff0000U},
    };
    const auto button = vm.interpreter.NewIntrinsicInstance(
        "Landroid/widget/Button;");
    vm.CallOn(button, "setText", "(Ljava/lang/CharSequence;)V",
              {VmValue::Ref(vm.interpreter.NewStringUtf8("Hi"))});
    vm.CallOn(button, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
              {VmValue::Int(101), VmValue::Int(0), VmValue::Int(0),
               VmValue::Int(0)});
    const auto node = FindViewUiNode(*vm.context, button.Value());
    REQUIRE(node.has_value());
    const auto* state = vm.context->ui_tree.Get(*node);
    CHECK(state->compound_drawables[0] ==
          ui::CompoundDrawable{101, 1, 1});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V",
              {VmValue::Ref(button)});
    // "Hi" measures 11 px at the 8 px fixed font; with the 1x1 left drawable
    // and the button's default 6/4 padding the wrap-content width is 24.
    CHECK(vm.CallOn(button, "getWidth", "()I").AsInt() == 24);

    // Clearing with 0 removes the drawable from measure again.
    vm.CallOn(button, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
              {VmValue::Int(0), VmValue::Int(0), VmValue::Int(0),
               VmValue::Int(0)});
    CHECK(vm.context->ui_tree.Get(*node)->compound_drawables[0] ==
          ui::CompoundDrawable{});
    CHECK(vm.CallOn(button, "getWidth", "()I").AsInt() == 23);

    // A missing resource surfaces as the real Java exception.
    const auto button_class = vm.model.ObjectClass(button);
    const auto method = vm.linker.FindVtableIndex(
        button_class, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V");
    REQUIRE(method.has_value());
    const auto missing = vm.interpreter.Call(
        vm.linker.Class(button_class).vtable[*method],
        std::vector<VmValue>{VmValue::Ref(button), VmValue::Int(999),
                             VmValue::Int(0), VmValue::Int(0),
                             VmValue::Int(0)});
    REQUIRE(missing.exception.IsValid());
    CHECK(vm.linker.Class(missing.exception_class).descriptor ==
          "Landroid/content/res/Resources$NotFoundException;");
}

TEST_CASE("DVM-123 compound drawable failure preserves state and virtual API shape") {
    ClickVm vm;
    vm.context->arsc.entries = {
        {.resource_id = 101, .type_name = "color", .entry_name = "red",
         .value_type = 0x1c, .value_data = 0xffff0000U}};
    const auto view = vm.interpreter.NewIntrinsicInstance("Landroid/widget/Button;");
    vm.CallOn(view, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
        {VmValue::Int(101), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0)});
    const auto node = FindViewUiNode(*vm.context, view.Value());
    REQUIRE(node.has_value());
    const auto before = vm.context->ui_tree.Get(*node)->compound_drawables;
    vm.context->ui_tree.ClearLayoutDirty();
    vm.context->ui_tree.ClearDrawDirty();
    const auto type = vm.model.ObjectClass(view);
    const auto slot = vm.linker.FindVtableIndex(type, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V");
    REQUIRE(slot.has_value());
    const auto method = vm.linker.Class(type).vtable[*slot];
    CHECK((vm.linker.Method(method).access_flags & kAccFinal) == 0);
    const auto result = vm.interpreter.Call(method,
        std::vector<VmValue>{VmValue::Ref(view), VmValue::Int(0), VmValue::Int(999),
                             VmValue::Int(0), VmValue::Int(0)});
    REQUIRE(result.exception.IsValid());
    CHECK(vm.linker.Class(result.exception_class).descriptor ==
          "Landroid/content/res/Resources$NotFoundException;");
    CHECK(vm.context->ui_tree.Get(*node)->compound_drawables == before);
    CHECK_FALSE(vm.context->ui_tree.Get(*node)->layout_dirty);
    CHECK_FALSE(vm.context->ui_tree.Get(*node)->draw_dirty);
}

TEST_CASE("DVM-123 hidden dynamic strip retains hierarchy drawable and listener") {
    ClickVm vm;
    vm.CallOn(vm.activity, "attachBaseContext", "(Landroid/content/Context;)V",
        {VmValue::Ref(vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;"))});
    vm.CallOn(vm.activity, "setTheme", "(I)V", {VmValue::Int(0x01030007)});
    vm.context->arsc.entries = {{.resource_id = 101, .type_name = "color",
        .entry_name = "icon", .value_type = 0x1c, .value_data = 0xffff0000U}};
    const auto frame = vm.interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout;");
    const auto group = vm.interpreter.NewIntrinsicInstance("Landroid/widget/RelativeLayout;");
    const auto button = vm.interpreter.NewIntrinsicInstance("Landroid/widget/Button;");
    const auto params = vm.interpreter.NewIntrinsicInstance("Landroid/widget/RelativeLayout$LayoutParams;");
    vm.CallDirect(button, "Landroid/widget/Button;", "<init>",
        "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
        {VmValue::Ref(vm.activity), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x01010049)});
    vm.CallOn(button, "setCompoundDrawablesWithIntrinsicBounds", "(IIII)V",
        {VmValue::Int(101), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0)});
    vm.CallOn(button, "setText", "(Ljava/lang/CharSequence;)V",
        {VmValue::Ref(vm.interpreter.NewStringUtf8("More"))});
    vm.CallOn(button, "setId", "(I)V", {VmValue::Int(2)});
    const auto listener = vm.NewListener();
    vm.CallOn(button, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V",
        {VmValue::Ref(listener)}); // Register while detached, then attach.
    vm.CallDirect(params, "Landroid/widget/RelativeLayout$LayoutParams;", "<init>",
        "(II)V", {VmValue::Int(-2), VmValue::Int(-2)});
    vm.CallOn(params, "addRule", "(I)V", {VmValue::Int(7)});
    vm.CallOn(group, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
        {VmValue::Ref(button), VmValue::Ref(params)});
    vm.CallOn(frame, "addView", "(Landroid/view/View;)V", {VmValue::Ref(group)});
    vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(frame)});
    vm.CallOn(frame, "setVisibility", "(I)V", {VmValue::Int(8)});
    const auto node = FindViewUiNode(*vm.context, button.Value());
    REQUIRE(node.has_value());
    CHECK(vm.context->ui_tree.IsAttached(*node));
    CHECK(vm.context->ui_tree.Get(*node)->parent == FindViewUiNode(*vm.context, group.Value()));
    CHECK(vm.context->ui_tree.FindByAndroidId(2) == node);
    CHECK(vm.context->ui_click_listeners.at(*node) == listener);
    CHECK(vm.context->ui_tree.Get(*node)->compound_drawables[0].resource_id == 101);
    CHECK(vm.CallOn(frame, "getVisibility", "()I").AsInt() == 8);
    CHECK_FALSE(vm.Click(1, 1).has_value());
    CHECK(vm.CallStaticInt("getClicks") == 0);
    ui::UiOverlayRenderer renderer;
    const auto& hidden = renderer.Render(vm.context->ui_tree, vm.context->ui_bitmaps, {100, 100});
    CHECK(hidden.rgba8 == std::vector<std::uint8_t>(100 * 100 * 4, 0));
}
