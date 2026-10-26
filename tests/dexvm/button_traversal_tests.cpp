#include "container_traversal_fixture.h"
#include "../fixtures/bitmap_decode_data.h"
#include "file_vfs_fixture.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include <algorithm>

using ogplay::test::container_traversal::ContainerVm;

namespace {
void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

void Set32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8U] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void AppendAscii(std::vector<std::byte>& bytes, const std::string_view text) {
    for (const auto value : text) {
        bytes.push_back(static_cast<std::byte>(value));
    }
}

std::vector<std::byte> StringPool(const std::vector<std::string>& strings, const bool utf8) {
    std::vector<std::byte> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& string : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        if (utf8) {
            data.push_back(static_cast<std::byte>(string.size()));
            data.push_back(static_cast<std::byte>(string.size()));
            AppendAscii(data, string);
            data.push_back(std::byte{0});
        } else {
            Append16(data, static_cast<std::uint16_t>(string.size()));
            for (const auto value : string) {
                Append16(data, static_cast<std::uint8_t>(value));
            }
            Append16(data, 0);
        }
    }
    while (data.size() % 4U != 0)
        data.push_back(std::byte{0});

    std::vector<std::byte> chunk;
    Append16(chunk, 0x0001);
    Append16(chunk, 28);
    Append32(chunk, 0);
    Append32(chunk, static_cast<std::uint32_t>(strings.size()));
    Append32(chunk, 0);
    Append32(chunk, utf8 ? 0x100U : 0U);
    Append32(chunk, static_cast<std::uint32_t>(28U + offsets.size() * 4U));
    Append32(chunk, 0);
    for (const auto offset : offsets)
        Append32(chunk, offset);
    chunk.insert(chunk.end(), data.begin(), data.end());
    Set32(chunk, 4, static_cast<std::uint32_t>(chunk.size()));
    return chunk;
}

ogplay::loader::ArscEntry Style(std::uint32_t id, std::uint32_t parent, std::vector<ogplay::loader::ArscBagValue> bag) {
    ogplay::loader::ArscEntry e;
    e.resource_id = id;
    e.type_name = "style";
    e.entry_name = "fixture";
    e.is_complex = true;
    e.parent = parent;
    e.bag = std::move(bag);
    return e;
}

struct XmlValue {
    std::uint32_t id;
    std::uint8_t type;
    std::uint32_t data;
};

std::vector<std::byte> AttributeXml(const std::vector<std::vector<XmlValue>>& tags) {
    std::vector<std::uint32_t> ids{0};
    for (const auto& attrs : tags)
        for (const auto& attr : attrs)
            if (std::ranges::find(ids, attr.id) == ids.end()) ids.push_back(attr.id);
    std::vector<std::string> names{"style"};
    for (std::size_t i = 1; i < ids.size(); ++i)
        names.push_back("a" + std::to_string(i));
    std::vector<std::byte> bytes;
    Append16(bytes, 3);
    Append16(bytes, 8);
    Append32(bytes, 0);
    const auto pool = StringPool(names, true);
    bytes.insert(bytes.end(), pool.begin(), pool.end());
    Append16(bytes, 0x0180);
    Append16(bytes, 8);
    Append32(bytes, static_cast<std::uint32_t>(8 + ids.size() * 4));
    for (const auto id : ids)
        Append32(bytes, id);
    for (const auto& attrs : tags) {
        Append16(bytes, 0x0102);
        Append16(bytes, 16);
        Append32(bytes, static_cast<std::uint32_t>(36 + attrs.size() * 20));
        Append32(bytes, 1);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0);
        Append16(bytes, 20);
        Append16(bytes, 20);
        Append16(bytes, static_cast<std::uint16_t>(attrs.size()));
        Append16(bytes, 0);
        Append16(bytes, 0);
        const auto style = std::ranges::find_if(attrs, [](const auto& a) { return a.id == 0; });
        Append16(bytes, style == attrs.end() ? 0 : static_cast<std::uint16_t>(style - attrs.begin() + 1));
        for (const auto& attr : attrs) {
            Append32(bytes, 0xffffffffU);
            Append32(bytes, static_cast<std::uint32_t>(std::ranges::find(ids, attr.id) - ids.begin()));
            Append32(bytes, 0xffffffffU);
            Append16(bytes, 8);
            bytes.push_back(std::byte{0});
            bytes.push_back(static_cast<std::byte>(attr.type));
            Append32(bytes, attr.data);
        }
    }
    for (std::size_t i = 0; i < tags.size(); ++i) {
        Append16(bytes, 0x0103);
        Append16(bytes, 16);
        Append32(bytes, 24);
        Append32(bytes, 1);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0xffffffffU);
        Append32(bytes, 0);
    }
    Set32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

VmObjectRef OpenAttributeXml(ContainerVm& f, VmObjectRef owner, const std::vector<std::byte>& xml) {
    f.context->apk_bytes = ogplay::test::file_vfs::MakeStoredZip("res/layout/attributes.xml", xml);
    f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
    const auto assets = f.On(owner, "getAssets", "()Landroid/content/res/AssetManager;").ref;
    const auto parser =
        f.On(assets, "openXmlResourceParser", "(Ljava/lang/String;)Landroid/content/res/XmlResourceParser;",
             {VmValue::Ref(f.vm.NewStringUtf8("res/layout/attributes.xml"))})
            .ref;
    CHECK(f.linker.Class(f.model.ObjectClass(parser)).descriptor == "Landroid/content/res/XmlBlock$Parser;");
    CHECK(f.On(parser, "next", "()I").AsInt() == 0);
    CHECK(f.On(parser, "next", "()I").AsInt() == 2);
    return parser;
}

constexpr std::uint32_t theme = 0x7f020001, button_style = 0x7f020002, xml_style = 0x7f020003;

VmObjectRef Owner(ContainerVm& f, int defaults = 0) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->arsc.package_id = 0x7f;
    f.context->arsc.global_string_pool = StringPool({"res/drawable/fixture.png"}, true);
    auto t = Style(theme, 0x01030005, {{0x01010048, 1, button_style, {}}, {0x01010095, 5, 0x1000, {}}});
    auto b = Style(button_style, 0,
                   {{0x01010095, 5, 0x1400, {}},
                    {0x01010098, 28, 0xff123456, {}},
                    {0x01010096, 16, 2, {}},
                    {0x01010097, 16, 2, {}},
                    {0x010100da, 18, 1, {}},
                    {0x010100e5, 18, 1, {}}});
    for (const auto id :
         {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U, 0x01010099U, 0x0101009aU, 0x0101009bU, 0x010102c5U,
          0x010102c6U, 0x010102c7U, 0x01010314U, 0x01010315U, 0x0101035eU, 0x0101035fU, 0x01010374U})
        t.bag.push_back({id, 1, 0, {}});
    if (defaults == 1) t.bag.clear();  // Unmodified Theme, including default scrollbar requests.
    if (defaults == 2) std::erase_if(t.bag, [](const auto& v) { return v.name == 0x01010048; });
    if (defaults >= 3) {
        b.parent = 0x01030014;  // Actual Widget.Button; mask background/gravity, retain stateful textColor.
        if (defaults == 3) std::erase_if(b.bag, [](const auto& v) { return v.name == 0x01010098; });
        b.bag.push_back({0x010100d4, 1, 0, {}});
        b.bag.push_back({0x01010034, 1, 0, {}});
        b.bag.push_back({0x010100af, 16, 0x11, {}});
    }
    f.context->arsc.entries = {t, b, Style(xml_style, 0, {{0x01010095, 5, 0x1600, {}}})};
    f.context->application_theme = theme;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(theme)});
    return owner;
}

const ui::UiNode& State(ContainerVm& f, VmObjectRef view) {
    return *f.context->ui_tree.Get(*FindViewUiNode(*f.context, view.Value()));
}

VmCallOutcome Construct(ContainerVm& f, VmObjectRef owner, const char* sig = "(Landroid/content/Context;)V",
                        std::vector<VmValue> extra = {}) {
    const auto type = f.linker.ResolveDescriptor("Landroid/widget/Button;");
    f.Checked(f.vm.EnsureClassInitialized(type));
    const auto view = f.vm.NewIntrinsicInstance("Landroid/widget/Button;");
    const auto root = f.vm.ProtectReferences(std::array{view});
    extra.insert(extra.begin(), {VmValue::Ref(view), VmValue::Ref(owner)});
    return f.vm.Call(*f.linker.FindDirectMethod(type, "<init>", sig), extra);
}
}  // namespace

TEST_CASE("DVM-297 Button ROM constructors real buttonStyle precedence on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto cls = f.linker.ResolveDescriptor("Landroid/widget/Button;");
        for (const auto sig :
             {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
              "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
            const auto& method = f.linker.Method(*f.linker.FindDirectMethod(cls, "<init>", sig));
            CHECK(method.code.has_value());
            CHECK_FALSE(static_cast<bool>(method.implementation));
        }
        const auto button = f.New("Landroid/widget/Button;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto button_root = f.vm.ProtectReferences(std::array{button});
        CHECK(State(f, button).text_size_px == 20);  // defStyleAttr > theme's 16
        CHECK(State(f, button).text_color == 0x123456ff);
        CHECK(State(f, button).text_style == 2);
        CHECK(State(f, button).padding == ui::Insets{});
        CHECK_FALSE(State(f, button).background_color.has_value());
        CHECK_FALSE(f.context->ui_view_backgrounds.contains(button.Value()));
        CHECK(f.On(button, "isFocusable", "()Z").AsInt() == 1);
        CHECK(f.On(button, "isClickable", "()Z").AsInt() == 1);
        for (bool explicit_xml : {false, true}) {
            std::vector<XmlValue> attrs{{0, 1, xml_style}};
            if (explicit_xml) attrs.push_back({0x01010095, 5, 0x1800});
            const auto parser = OpenAttributeXml(f, owner, AttributeXml({attrs}));
            const auto parser_root = f.vm.ProtectReferences(std::array{parser});
            const auto styled =
                f.New("Landroid/widget/Button;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                      {VmValue::Ref(owner), VmValue::Ref(parser)});
            CHECK(State(f, styled).text_size_px == (explicit_xml ? 24 : 22));  // XML > style > buttonStyle
            CHECK(State(f, styled).text_color == 0x123456ff);
        }
        // Caller-supplied defStyleAttr remains real, not hardcoded to buttonStyle.
        f.context->arsc.entries.front().bag.push_back({0x7f010001, 1, xml_style, {}});
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(theme)});
        const auto custom = f.New("Landroid/widget/Button;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x7f010001)});
        CHECK(State(f, custom).text_size_px == 22);
        const auto trace = f.vm.Trace("", 10000);
        for (const auto name : {"Landroid/widget/Button;", "Landroid/view/View;", "Landroid/content/res/TypedArray;"})
            CHECK(std::any_of(trace.begin(), trace.end(), [&](const auto& e) { return e.class_descriptor == name; }));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-297 Button inherited text measure click focus compound and frozen projection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f, 4);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto button = f.New("Landroid/widget/Button;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto root = f.vm.ProtectReferences(std::array{button});
        f.On(button, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.model.NewString(u"HI HI"))});
        f.On(button, "setTextSize", "(IF)V", {VmValue::Int(0), VmValue::Float(8)});
        constexpr std::uint32_t image = 0x7f030050;
        f.context->arsc.entries.push_back({.resource_id = image,
                                           .type_name = "drawable",
                                           .entry_name = "compound",
                                           .string_value = "res/drawable/fixture.png",
                                           .value_type = 3,
                                           .density = 160});
        f.context->apk_bytes = ogplay::test::BitmapDecodeApk();
        f.context->archive = ogplay::loader::ParseApkArchive(f.context->apk_bytes);
        const auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto drawable =
            f.On(res, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(image)}).ref;
        f.On(
            button, "setCompoundDrawablesWithIntrinsicBounds",
            "(Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;Landroid/graphics/drawable/Drawable;)V",
            {VmValue::Ref(drawable), VmValue::Ref(VmObjectRef{}), VmValue::Ref(drawable), VmValue::Ref(VmObjectRef{})});
        CHECK(f.On(button, "getCompoundPaddingLeft", "()I").AsInt() == 2);
        CHECK(f.On(button, "getCompoundPaddingRight", "()I").AsInt() == 2);
        const auto listener = f.New("Ltest/StateListener;");
        f.On(button, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V", {VmValue::Ref(listener)});
        f.On(button, "setOnFocusChangeListener", "(Landroid/view/View$OnFocusChangeListener;)V",
             {VmValue::Ref(listener)});
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto parent =
            f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(parent), VmValue::Int(-1), VmValue::Int(-1)});
        f.On(parent, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(button), VmValue::Int(-2), VmValue::Int(-2)});
        f.context->content_view = group;
        ++f.context->ui_content_revision;
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{20, 40}));
        CHECK(f.On(button, "requestFocus", "()Z").AsInt() == 1);
        CHECK(f.On(button, "isFocused", "()Z").AsInt() == 1);
        CHECK(f.On(group, "findFocus", "()Landroid/view/View;").ref == button);
        const auto focuses = *f.linker.FindFieldRecursive(f.model.ObjectClass(listener), "focuses", "I");
        CHECK(f.model.InstanceSlots(listener)[f.linker.Field(focuses).slot].bits == 1);
        f.On(group, "setDescendantFocusability", "(I)V", {VmValue::Int(0x60000)});
        f.On(button, "clearFocus", "()V");
        CHECK_FALSE(f.On(button, "isFocused", "()Z").AsInt());
        CHECK(f.model.InstanceSlots(listener)[f.linker.Field(focuses).slot].bits == 2);
        CHECK_FALSE(InvokeViewOnClick(f.vm, *f.context, button.Value()).has_value());
        const auto clicks = *f.linker.FindFieldRecursive(f.model.ObjectClass(listener), "clicks", "I");
        CHECK(f.model.InstanceSlots(listener)[f.linker.Field(clicks).slot].bits == 1);
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        const auto node = *FindViewUiNode(*f.context, button.Value());
        REQUIRE(snapshot->Get(node)->text_line_plan);
        CHECK(snapshot->Get(node)->measured.width == f.On(button, "getMeasuredWidth", "()I").AsInt());
        CHECK(snapshot->Get(node)->text_line_plan->text == u"HI\nHI");
        CHECK(snapshot->Get(node)->compound_drawables[0].width == 2);
        static_cast<void>(f.vm.CollectGarbage("button-inherited-input"));
        CHECK(f.context->view_adapter_references.at(button.Value()).text_compounds[0] == drawable);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-297 Button default theme fails actual unsupported requests") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (int defaults : {1, 2, 3}) {
            ContainerVm f(backend);
            const auto owner = Owner(f, defaults);
            const auto root = f.vm.ProtectReferences(std::array{owner});
            const auto failed = Construct(f, owner);
            CHECK(failed.exception.IsValid());
            INFO(defaults, failed.exception_message);
            CHECK(failed.exception_message.find(defaults <= 2 ? "unreviewed ROM drawable load"
                                                              : "primary_text_light") != std::string::npos);
            CHECK_FALSE(f.ledger.Unimplemented().empty());
        }
    }
}
