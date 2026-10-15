#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-120 Typeface Java cache and styles drive measured rendered text") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto owner = f.linker.ResolveDescriptor("Landroid/graphics/Typeface;");
        CHECK(f.linker.Class(owner).is_boot_dex);
        std::array<VmObjectRef, 4> defaults;
        for (int style = 0; style < 4; ++style) {
            defaults[style] = f.Static("Landroid/graphics/Typeface;", "defaultFromStyle",
                "(I)Landroid/graphics/Typeface;", {VmValue::Int(style)}).ref;
            CHECK(f.On(defaults[style], "getStyle", "()I").AsInt() == style);
            CHECK(f.On(defaults[style], "isBold", "()Z").AsInt() == (style & 1));
            CHECK(f.On(defaults[style], "isItalic", "()Z").AsInt() == ((style >> 1) & 1));
        }
        const auto normal = defaults[0];
        const auto create = [&](int style) {
            return f.Static("Landroid/graphics/Typeface;", "create",
                "(Landroid/graphics/Typeface;I)Landroid/graphics/Typeface;",
                {VmValue::Ref(normal), VmValue::Int(style)}).ref;
        };
        CHECK(create(0) == normal);
        const auto cached = create(3);
        CHECK(create(3) == cached);
        static_cast<void>(f.vm.CollectGarbage("dvm120-typeface-static-cache"));
        CHECK(create(3) == cached);
        f.Static("Landroid/graphics/Typeface;", "recreateDefaults", "()V");
        CHECK(create(3) != cached);
        CHECK(f.On(normal, "getStyle", "()I").AsInt() == 0);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto root = f.vm.ProtectReferences(std::array{text});
        f.On(text, "setText", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("I"))});
        const auto node = *FindViewUiNode(*f.context, text.Value());
        f.context->ui_tree.Attach(f.context->ui_tree.Root(), node);
        std::array<std::vector<std::uint8_t>, 4> pixels;
        for (int style = 0; style < 4; ++style) {
            f.On(text, "setTypeface", "(Landroid/graphics/Typeface;I)V",
                 {VmValue::Ref(normal), VmValue::Int(style)});
            const auto face = f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref;
            CHECK(f.On(face, "getStyle", "()I").AsInt() == style);
            ui::LayoutUiTree(f.context->ui_tree, {32, 16});
            const int extra = ((style & 1) ? 1 : 0) + ((style & 2) ? 2 : 0);
            CHECK(f.context->ui_tree.Get(node)->measured.width == 5 + extra);
            pixels[style] = ui::RasterizeUiOverlay(ui::BuildUiRenderList(f.context->ui_tree, {}), {32, 16}).rgba8;
            if (style != 0) CHECK(pixels[style] != pixels[0]);
        }
        f.On(text, "setTypeface", "(Landroid/graphics/Typeface;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK_FALSE(f.On(text, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
        CHECK(f.context->ui_tree.Get(node)->text_style == 0);
        const auto factory = *f.linker.FindDirectMethod(owner, "createFromFile", "(Ljava/lang/String;)Landroid/graphics/Typeface;");
        const auto result = f.vm.Call(factory, std::array{VmValue::Ref(f.vm.NewStringUtf8("/font.ttf"))});
        REQUIRE(result.exception.IsValid());
        CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(f.ledger.Unimplemented().back().id == "dexvm.typeface");
    }
}

TEST_CASE("DVM-121 text appearance resolves theme bags and preserves Java color values") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        f.context->package_name = "org.example.fixture";
        f.context->application_theme = 0x01030007U;
        f.context->activity_themes["org.example.fixture.Light"] = 0x0103000cU;
        const auto activity = f.New("Landroid/app/Activity;");
        const auto activity_root = f.vm.ProtectReferences(std::array{activity});
        AttachAndroidActivityIdentity(f.vm, f.context, activity, "org.example.fixture.Light");
        CHECK(f.On(activity, "getThemeResId", "()I").AsInt() == 0x0103000c);
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto root = f.vm.ProtectReferences(std::array{text});
        f.On(text, "setTextColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
        f.On(text, "setTextSize", "(F)V", {VmValue::Float(16)});
        const auto apply = [&](std::uint32_t id) {
            return f.OnOutcome(text, "setTextAppearance", "(Landroid/content/Context;I)V",
                {VmValue::Ref(activity), VmValue::Int(static_cast<std::int32_t>(id))});
        };
        const auto buffer_type = f.vm.Linker().ResolveDescriptor("Landroid/widget/TextView$BufferType;");
        const auto values = f.vm.Linker().FindDirectMethod(buffer_type, "values", "()[Landroid/widget/TextView$BufferType;");
        REQUIRE(values.has_value());
        const auto buffers = f.vm.Call(*values, {});
        REQUIRE_FALSE(buffers.exception.IsValid());
        const auto buffer_root = f.vm.ProtectReferences(std::array{buffers.value.ref});
        CHECK(f.vm.Model().ArrayLength(buffers.value.ref) == 3);
        const auto normal = f.vm.Model().GetObjectElement(buffers.value.ref, 0);
        f.On(text, "setText", "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("CG subtitle")), VmValue::Ref(normal)});
        CHECK(f.vm.StringUtf8(f.On(text, "getText", "()Ljava/lang/CharSequence;").ref) == "CG subtitle");
        // A public attr id is metadata, not a reference to TextAppearance.Small.
        REQUIRE_FALSE(apply(0x01010042).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 16);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == 0xff123456U);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getHighlightColor", "()I").AsInt()) == 0x9983cc39U);
        const auto link = f.On(text, "getLinkTextColors", "()Landroid/content/res/ColorStateList;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(link)).is_boot_dex);
        CHECK(static_cast<std::uint32_t>(f.On(link, "getDefaultColor", "()I").AsInt()) == 0xff0000eeU);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentHintTextColor", "()I").AsInt()) == 0xff808080U);
        const auto wrapper = f.New("Landroid/content/ContextWrapper;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        f.On(text, "setTextAppearance", "(Landroid/content/Context;I)V", {VmValue::Ref(wrapper), VmValue::Int(0)});
        const auto wrapped_link = f.On(text, "getLinkTextColors", "()Landroid/content/res/ColorStateList;").ref;
        CHECK(static_cast<std::uint32_t>(f.On(wrapped_link, "getDefaultColor", "()I").AsInt()) == 0xff0000eeU);
        ogplay::loader::ArscEntry parent;
        parent.resource_id = 0x7f030001; parent.type_name = "style"; parent.is_complex = true;
        parent.bag = {{0x01010098, 0x1c, 0xff102030U, {}}, {0x01010097, 0x10, 1, {}}};
        auto child = parent;
        child.resource_id = 0x7f030002; child.parent = parent.resource_id;
        child.bag = {{0x01010095, 5, 0x00000e02, {}}, // 14sp
                     {0x01010098, 1, 0x7f040001, {}},
                     {0x0101009b, 2, 0x0101009b, {}}};
        ogplay::loader::ArscEntry color;
        color.resource_id = 0x7f040001; color.type_name = "color";
        color.value_type = 0x1c; color.value_data = 0xffabcdefU;
        f.context->arsc.entries = {parent, child, color};
        f.context->ui_scaled_density = 2;
        REQUIRE_FALSE(apply(child.resource_id).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 28);
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == color.value_data);
        CHECK(f.context->ui_tree.Get(*FindViewUiNode(*f.context, text.Value()))->text_style == 1);
        const auto colors = f.On(text, "getTextColors", "()Landroid/content/res/ColorStateList;").ref;
        f.On(text, "getCurrentTextColor", "()I"); // retire the returned reference root
        CHECK(f.vm.MarkReachable().IsMarked(colors));
        static_cast<void>(f.vm.CollectGarbage("dvm121-text-colors"));
        CHECK(f.On(text, "getTextColors", "()Landroid/content/res/ColorStateList;").ref == colors);
        f.context->arsc.entries[0].parent = child.resource_id;
        CHECK(apply(child.resource_id).exception.IsValid());
        CHECK(f.On(text, "getTextSize", "()F").AsFloat() == 28);
        f.context->arsc.entries[0].parent = 0;
        f.context->arsc.entries[1].bag.push_back({0x01010161, 0x1c, 0xff000000, {}});
        CHECK(apply(child.resource_id).exception.IsValid());
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == color.value_data);
        CHECK(apply(0x7f030099).exception.IsValid());
        CHECK(f.OnOutcome(text, "setTextAppearance", "(Landroid/content/Context;I)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)}).exception.IsValid());
    }
}

TEST_CASE("DVM-121 ColorStateList uses Java StateSet matching and rejects unsupported renderer state") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto int_array = [&](std::initializer_list<std::int32_t> items) {
            const auto result = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                static_cast<JniSize>(items.size()));
            JniSize i{};
            for (const auto item : items) f.model.SetPrimitiveElement(result, i++, static_cast<std::uint32_t>(item));
            return result;
        };
        const auto states = f.model.NewObjectArray(f.linker.ResolveDescriptor("[[I"), f.linker.ResolveDescriptor("[I"), 2);
        const auto state_root = f.vm.ProtectReferences(std::array{states});
        f.model.SetObjectElement(states, 0, int_array({-16842910}));
        f.model.SetObjectElement(states, 1, int_array({}));
        const auto colors = int_array({static_cast<std::int32_t>(0xff808080U), static_cast<std::int32_t>(0xffffffffU)});
        const auto color_root = f.vm.ProtectReferences(std::array{colors});
        const auto list = f.New("Landroid/content/res/ColorStateList;", "([[I[I)V", {VmValue::Ref(states), VmValue::Ref(colors)});
        const auto root = f.vm.ProtectReferences(std::array{list});
        CHECK(f.On(list, "isStateful", "()Z").AsInt() == 1);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getDefaultColor", "()I").AsInt()) == 0xffffffffU);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getColorForState", "([II)I",
            {VmValue::Ref(int_array({})), VmValue::Int(0)}).AsInt()) == 0xff808080U);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getColorForState", "([II)I",
            {VmValue::Ref(int_array({16842910})), VmValue::Int(0)}).AsInt()) == 0xffffffffU);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto rejected = f.OnOutcome(text, "setTextColor", "(Landroid/content/res/ColorStateList;)V", {VmValue::Ref(list)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
    }
}
