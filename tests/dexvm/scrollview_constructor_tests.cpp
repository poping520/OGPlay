#include "container_traversal_fixture.h"
#include "../fixtures/bitmap_decode_data.h"
#include "file_vfs_fixture.h"
#include <bit>
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

constexpr std::uint32_t theme = 0x7f020001, vertical = 0x7f020002, horizontal = 0x7f020003;

VmObjectRef Owner(ContainerVm& f, bool supported = true) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->arsc.package_id = 0x7f;
    auto t = Style(theme, 0x01030005, {{0x01010080, 1, vertical, {}}, {0x01010353, 1, horizontal, {}}});
    for (const auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U})
        t.bag.push_back({id, 1, 0, {}});
    if (supported) t.bag.push_back({0x010102c1, 16, 2, {}});  // overScrollMode=never
    f.context->arsc.entries = {t, Style(vertical, 0, {{0x0101017a, 18, 1, {}}}),
                               Style(horizontal, 0, {{0x0101017a, 18, 0, {}}})};  // fillViewport
    f.context->application_theme = theme;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(theme)});
    return owner;
}

Slot& Field(ContainerVm& f, VmObjectRef object, const char* name, const char* type = "I") {
    const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
    REQUIRE(field);
    return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
}

void Failure(ContainerVm& f, VmObjectRef object, const char* name, const char* sig, std::vector<VmValue> args = {}) {
    const auto hits = [&] {
        std::uint64_t count = 0;
        for (const auto& hit : f.ledger.Unimplemented())
            count += hit.count;
        return count;
    };
    const auto before = hits();
    const auto result = f.OnOutcome(object, name, sig, std::move(args));
    CHECK(result.exception.IsValid());
    CHECK_FALSE(f.ledger.Unimplemented().empty());
    CHECK(hits() > before);
    const bool glow = std::string_view(name) == "setOverScrollMode";
    const bool bounded = std::string_view(name) == "onTouchEvent" &&
                         f.linker.Class(f.model.ObjectClass(object)).descriptor == "Landroid/widget/ScrollView;";
    CHECK(f.linker.Class(result.exception_class).descriptor ==
          ((glow || bounded) ? "Ljava/lang/UnsupportedOperationException;" : "Ljava/lang/UnsatisfiedLinkError;"));
    CHECK(result.exception_message.find(glow ? "EdgeEffect" : name) != std::string::npos);
}
}  // namespace

TEST_CASE("DVM-299 scroll ROM constructors real metrics and TypedArray on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto density : {1.0F, 2.0F}) {
            ContainerVm f(backend);
            f.context->ui_density = density;
            f.context->surface_width = density == 1 ? 800 : 2400;
            f.context->surface_height = density == 1 ? 480 : 1800;
            const auto owner = Owner(f);
            const auto root = f.vm.ProtectReferences(std::array{owner});
            for (const auto name : {"Landroid/widget/ScrollView;", "Landroid/widget/HorizontalScrollView;"}) {
                const auto view = f.New(name, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
                const auto keep = f.vm.ProtectReferences(std::array{view});
                CHECK(f.linker.Class(*f.linker.Class(f.model.ObjectClass(view)).super).descriptor ==
                      "Landroid/widget/FrameLayout;");
                CHECK(Field(f, view, "mTouchSlop").bits == static_cast<unsigned>(8 * density));
                CHECK(Field(f, view, "mMinimumVelocity").bits == static_cast<unsigned>(50 * density));
                CHECK(Field(f, view, "mMaximumVelocity").bits == static_cast<unsigned>(8000 * density));
                CHECK(Field(f, view, "mOverscrollDistance").bits == 0);
                CHECK(Field(f, view, "mOverflingDistance").bits == (density == 1 ? 6 : 18));
                CHECK(Field(f, view, "mSmoothScrollingEnabled", "Z").bits == 1);
                CHECK(Field(f, view, "mIsLayoutDirty", "Z").bits == 1);
                CHECK(Field(f, view, "mActivePointerId").bits == 0xffffffffU);
                CHECK(Field(f, view, "mTempRect", "Landroid/graphics/Rect;").bits != 0);
                CHECK(f.On(view, "isFocusable", "()Z").AsInt() == 1);
                CHECK(f.On(view, "getDescendantFocusability", "()I").AsInt() == 0x40000);
                CHECK(f.On(view, "willNotDraw", "()Z").AsInt() == 0);
                const auto scroller = VmObjectRef{Field(f, view, "mScroller", "Landroid/widget/OverScroller;").bits};
                REQUIRE(scroller.IsValid());
                for (const auto axis : {"mScrollerX", "mScrollerY"}) {
                    const auto spline =
                        VmObjectRef{Field(f, scroller, axis, "Landroid/widget/OverScroller$SplineOverScroller;").bits};
                    REQUIRE(spline.IsValid());
                    CHECK(Field(f, spline, "mFinished", "Z").bits == 1);
                    CHECK(std::bit_cast<float>(Field(f, spline, "mPhysicalCoeff", "F").bits) ==
                          doctest::Approx(9.80665F * 39.37F * density * 160 * .84F));
                }
                const bool expected = std::string_view(name) == "Landroid/widget/ScrollView;";
                CHECK(f.On(view, "isFillViewport", "()Z").AsInt() == expected);
                // Null attrs still parses the selected nonzero default style.
                const auto empty = f.New(name, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                         {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})});
                CHECK(f.On(empty, "isFillViewport", "()Z").AsInt() == expected);
                const auto attrs = OpenAttributeXml(f, owner, AttributeXml({{{0x0101017a, 18, expected ? 0U : 1U}}}));
                const auto xml = f.New(name, "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                       {VmValue::Ref(owner), VmValue::Ref(attrs)});
                CHECK(f.On(xml, "isFillViewport", "()Z").AsInt() == !expected);
                const auto explicit_style = f.New(name, "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                                                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}),
                                                   VmValue::Int(expected ? 0x01010353 : 0x01010080)});
                CHECK(f.On(explicit_style, "isFillViewport", "()Z").AsInt() == !expected);
                f.On(view, "setSmoothScrollingEnabled", "(Z)V", {VmValue::Int(0)});
                CHECK(f.On(view, "isSmoothScrollingEnabled", "()Z").AsInt() == 0);
                const auto type = f.model.ObjectClass(view);
                for (const auto sig :
                     {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                      "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
                    const auto ctor = f.linker.FindDirectMethod(type, "<init>", sig);
                    REQUIRE(ctor);
                    CHECK(f.linker.Method(*ctor).code.has_value());
                    CHECK_FALSE(static_cast<bool>(f.linker.Method(*ctor).implementation));
                }
                static_cast<void>(f.vm.CollectGarbage("scroll-constructor-strong-edges"));
                CHECK(f.model.ObjectClass(scroller) == f.linker.ResolveDescriptor("Landroid/widget/OverScroller;"));
                CHECK(f.ledger.Unimplemented().empty());
            }
            const auto trace = f.vm.Trace("", 10000);
            for (const auto name : {"Landroid/widget/ScrollView;", "Landroid/widget/HorizontalScrollView;",
                                    "Landroid/widget/OverScroller;", "Landroid/widget/OverScroller$SplineOverScroller;",
                                    "Landroid/content/res/TypedArray;"})
                CHECK(
                    std::any_of(trace.begin(), trace.end(), [&](const auto& e) { return e.class_descriptor == name; }));
            std::ofstream out(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-10b1/scroll-" +
                              (backend == InterpreterBackend::threaded ? "threaded-" : "switch-") +
                              (density == 1 ? "1" : "2") + ".trace.json");
            out << RenderDexVmTraceJson(trace);
        }
    }
}

TEST_CASE("DVM-299 scroll unsupported system and next-stage methods explicitly fail") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto name : {"Landroid/widget/ScrollView;", "Landroid/widget/HorizontalScrollView;"}) {
            ContainerVm f(backend);
            const auto owner = Owner(f);
            const auto root = f.vm.ProtectReferences(std::array{owner});
            const auto view = f.New(name, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto keep = f.vm.ProtectReferences(std::array{view});
            CHECK(f.ledger.Unimplemented().empty());
            Failure(f, view, "fling", "(I)V", {VmValue::Int(20)});
            Failure(f, view, "smoothScrollTo", "(II)V", {VmValue::Int(0), VmValue::Int(10)});
            f.On(view, "setOverScrollMode", "(I)V", {VmValue::Int(0)});
            CHECK(f.On(view, "getOverScrollMode", "()I").AsInt() == 0);
            Failure(f, view, "onSaveInstanceState", "()Landroid/os/Parcelable;");
            Failure(f, view, "initializeScrollbars", "(Landroid/content/res/TypedArray;)V",
                    {VmValue::Ref(VmObjectRef{})});
            Failure(f, view, "onRestoreInstanceState", "(Landroid/os/Parcelable;)V",
                    {VmValue::Ref(f.vm.NewIntrinsicInstance(
                        (std::string(name).substr(0, std::string(name).size() - 1) + "$SavedState;").c_str()))});
            Failure(f, view, "requestChildFocus", "(Landroid/view/View;Landroid/view/View;)V",
                    {VmValue::Ref(view), VmValue::Ref(view)});
            if (std::string_view(name) == "Landroid/widget/HorizontalScrollView;") {
                Failure(f, view, "onMeasure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
                Failure(f, view, "onLayout", "(ZIIII)V",
                        {VmValue::Int(0), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0)});
            }
            Failure(f, view, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", {VmValue::Ref(VmObjectRef{})});
        }
        ContainerVm f(backend);
        const auto owner = Owner(f, false);
        CHECK(f.ledger.Unimplemented().empty());
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (const auto name : {"Landroid/widget/ScrollView;", "Landroid/widget/HorizontalScrollView;"}) {
            const auto view = f.vm.NewIntrinsicInstance(name);
            const auto result = f.vm.Call(
                *f.linker.FindDirectMethod(f.linker.ResolveDescriptor(name), "<init>", "(Landroid/content/Context;)V"),
                std::array{VmValue::Ref(view), VmValue::Ref(owner)});
            CHECK_FALSE(result.exception.IsValid());
        }
    }
}
