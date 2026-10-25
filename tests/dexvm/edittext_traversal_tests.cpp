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
    auto t = Style(theme, 0x01030005, {{0x0101006e, 1, button_style, {}}, {0x01010095, 5, 0x1000, {}}});
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
    if (defaults == 2) std::erase_if(t.bag, [](const auto& v) { return v.name == 0x0101006e; });
    if (defaults >= 3) {
        b.parent = 0x01030023;  // Actual Widget.EditText; mask background/gravity, retain stateful textColor.
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
    const auto type = f.linker.ResolveDescriptor("Landroid/widget/EditText;");
    f.Checked(f.vm.EnsureClassInitialized(type));
    const auto view = f.vm.NewIntrinsicInstance("Landroid/widget/EditText;");
    const auto root = f.vm.ProtectReferences(std::array{view});
    extra.insert(extra.begin(), {VmValue::Ref(view), VmValue::Ref(owner)});
    return f.vm.Call(*f.linker.FindDirectMethod(type, "<init>", sig), extra);
}
}  // namespace

TEST_CASE("DVM-298 EditText ROM shape style defaults and bounded editable on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        const auto edit = f.New("Landroid/widget/EditText;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto edit_root = f.vm.ProtectReferences(std::array{edit});
        CHECK(State(f, edit).text_size_px == 20);
        for (bool explicit_xml : {false, true}) {
            std::vector<XmlValue> attrs{{0, 1, xml_style}};
            if (explicit_xml) attrs.push_back({0x01010095, 5, 0x1800});
            const auto parser = OpenAttributeXml(f, owner, AttributeXml({attrs}));
            const auto parser_root = f.vm.ProtectReferences(std::array{parser});
            const auto styled =
                f.New("Landroid/widget/EditText;", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                      {VmValue::Ref(owner), VmValue::Ref(parser)});
            CHECK(State(f, styled).text_size_px == (explicit_xml ? 24 : 22));
        }
        f.context->arsc.entries.front().bag.push_back({0x7f010001, 1, xml_style, {}});
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(theme)});
        const auto custom =
            f.New("Landroid/widget/EditText;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x7f010001)});
        CHECK(State(f, custom).text_size_px == 22);

        CHECK_FALSE(State(f, edit).background_color.has_value());
        CHECK(f.On(edit, "getDefaultEditable", "()Z").AsInt() == 1);
        const auto movement = f.On(edit, "getDefaultMovementMethod", "()Landroid/text/method/MovementMethod;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(movement)).descriptor ==
              "Landroid/text/method/ArrowKeyMovementMethod;");
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/EditText;");
        for (const auto sig :
             {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
              "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
            const auto& method = f.linker.Method(*f.linker.FindDirectMethod(type, "<init>", sig));
            CHECK(method.code.has_value());
            CHECK_FALSE(static_cast<bool>(method.implementation));
        }
        const auto editable = f.On(edit, "getText", "()Landroid/text/Editable;").ref;
        CHECK(editable.IsValid());
        CHECK(f.On(edit, "getText", "()Ljava/lang/CharSequence;").ref == editable);
        f.On(edit, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("abc"))});
        CHECK(f.On(editable, "length", "()I").AsInt() == 3);
        const auto normal = f.Static("Landroid/widget/TextView$BufferType;", "valueOf",
                                     "(Ljava/lang/String;)Landroid/widget/TextView$BufferType;",
                                     {VmValue::Ref(f.vm.NewStringUtf8("NORMAL"))})
                                .ref;
        f.On(edit, "setText", "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
             {VmValue::Ref(f.vm.NewStringUtf8("abcd")), VmValue::Ref(normal)});
        CHECK(State(f, edit).text == u"abcd");  // ROM override forces EDITABLE despite NORMAL.
        f.On(edit, "setSelection", "(II)V", {VmValue::Int(3), VmValue::Int(1)});
        CHECK(f.On(edit, "getSelectionStart", "()I").AsInt() == 3);
        CHECK(f.On(edit, "getSelectionEnd", "()I").AsInt() == 1);
        f.On(edit, "selectAll", "()V");
        CHECK(f.On(edit, "getSelectionEnd", "()I").AsInt() == 4);
        f.On(edit, "extendSelection", "(I)V", {VmValue::Int(2)});
        CHECK(f.On(edit, "getSelectionEnd", "()I").AsInt() == 2);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-298 EditText actual input Java focus chain watchers reentry measure and GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto group = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto parent =
            f.New("Landroid/widget/LinearLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto edit = f.New("Landroid/widget/EditText;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto edit2 = f.New("Landroid/widget/EditText;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(group, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(parent), VmValue::Int(-1), VmValue::Int(-1)});
        f.On(parent, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(edit), VmValue::Int(-2), VmValue::Int(-2)});
        f.On(parent, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(edit2), VmValue::Int(-2), VmValue::Int(-2)});
        f.context->content_view = group;
        ++f.context->ui_content_revision;
        const auto resolve = [&] {
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{160, 80}));
        };
        resolve();
        const auto listener = f.New("Ltest/StateListener;");
        f.On(edit, "setOnFocusChangeListener", "(Landroid/view/View$OnFocusChangeListener;)V",
             {VmValue::Ref(listener)});
        CHECK(f.On(edit, "requestFocus", "()Z").AsInt() == 1);
        CHECK(f.On(group, "findFocus", "()Landroid/view/View;").ref == edit);
        const auto input = [&](int code, int unicode = 0) {
            AndroidBoundaryInput event{};
            event.type = AndroidBoundaryInputType::key;
            event.pressed = true;
            event.code = code;
            event.unicode_char = unicode;
            CHECK(DispatchAndroidBoundedEditInput(f.vm, *f.context, event));
        };
        f.On(edit, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("abcd"))});
        f.On(edit, "setSelection", "(II)V", {VmValue::Int(1), VmValue::Int(3)});
        const auto a = f.New("Ltest/EditWatcher;"), b = f.New("Ltest/EditWatcher;");
        f.On(edit, "addTextChangedListener", "(Landroid/text/TextWatcher;)V", {VmValue::Ref(a)});
        f.On(edit, "addTextChangedListener", "(Landroid/text/TextWatcher;)V", {VmValue::Ref(b)});
        input(0, 'X');
        CHECK(State(f, edit).text == u"aXd");
        CHECK(f.On(edit, "getSelectionStart", "()I").AsInt() == 2);
        const auto log = [&] {
            return f.vm.StringUtf8(f.Static("Ltest/EditWatcher;", "events", "()Ljava/lang/String;").ref);
        };
        CHECK(log() == "112233");  // all before, all on, all after
        f.Static("Ltest/EditWatcher;", "reset", "()V");
        f.On(a, "reenter", "()V");
        input(0, 'Y');
        CHECK(State(f, edit).text == u"ZXYd");
        CHECK(log() == "112231122333");
        f.On(edit, "removeTextChangedListener", "(Landroid/text/TextWatcher;)V", {VmValue::Ref(a)});
        f.On(edit, "removeTextChangedListener", "(Landroid/text/TextWatcher;)V", {VmValue::Ref(b)});
        input(21);
        input(67);
        CHECK(State(f, edit).text == u"ZYd");
        resolve();
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        const auto node = *FindViewUiNode(*f.context, edit.Value());
        REQUIRE(snapshot->Get(node)->text_line_plan);
        CHECK(snapshot->Get(node)->text_line_plan->text == u"ZYd");
        const auto width = f.On(edit, "getMeasuredWidth", "()I").AsInt();
        input(0, 'M');
        input(0, 'M');
        resolve();
        CHECK(f.On(edit, "getMeasuredWidth", "()I").AsInt() > width);
        CHECK(snapshot->Get(node)->text_line_plan->text == u"ZYd");
        CHECK(f.On(edit2, "requestFocus", "()Z").AsInt() == 1);
        input(0, 'Q');
        CHECK(State(f, edit2).text == u"Q");
        f.On(edit2, "setSelection", "(I)V", {VmValue::Int(0)});
        input(112);
        CHECK(State(f, edit2).text.empty());
        input(0, 'Q');
        CHECK(f.On(group, "findFocus", "()Landroid/view/View;").ref == edit2);
        const auto field = *f.linker.FindFieldRecursive(f.model.ObjectClass(listener), "focuses", "I");
        CHECK(f.model.InstanceSlots(listener)[f.linker.Field(field).slot].bits == 2);
        CHECK_FALSE(f.context->ui_tree.Focused().has_value());
        CHECK_FALSE(f.context->focused_edit_text.IsValid());
        const auto editable = f.On(edit, "getText", "()Landroid/text/Editable;").ref;
        static_cast<void>(f.vm.CollectGarbage("bounded-edit-retain"));
        CHECK(f.On(editable, "length", "()I").AsInt() == 5);
        CHECK(f.ledger.Unimplemented().empty());
        f.context->content_view = VmObjectRef{};
        static_cast<void>(f.vm.CollectGarbage("bounded-edit-release"));
        CHECK_FALSE(f.context->view_adapter_references.contains(edit.Value()));
        CHECK_FALSE(f.context->editable_owner.contains(editable.Value()));
    }
}

TEST_CASE("DVM-298 EditText real default style and unsupported Editor IME fail explicitly") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (int defaults : {1, 2}) {
            ContainerVm f(backend);
            const auto owner = Owner(f, defaults);
            const auto failed = Construct(f, owner);
            CHECK(failed.exception.IsValid());
            INFO(failed.exception_message);
            CHECK(failed.exception_message.find(defaults == 1 ? "scrollbar" : "edit_text") != std::string::npos);
            CHECK_FALSE(f.ledger.Unimplemented().empty());
        }
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto edit = f.New("Landroid/widget/EditText;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto root = f.vm.ProtectReferences(std::array{owner, edit});
        const auto editor = f.OnOutcome(edit, "setInputType", "(I)V", {VmValue::Int(1)});
        CHECK(editor.exception.IsValid());
        CHECK(editor.exception_message.find("setInputType") != std::string::npos);
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        const auto ime =
            f.OnOutcome(edit, "onCreateInputConnection",
                        "(Landroid/view/inputmethod/EditorInfo;)Landroid/view/inputmethod/InputConnection;",
                        {VmValue::Ref(VmObjectRef{})});
        CHECK(ime.exception.IsValid());
        CHECK(ime.exception_message.find("onCreateInputConnection") != std::string::npos);
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
