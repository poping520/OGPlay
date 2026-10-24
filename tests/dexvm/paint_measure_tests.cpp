#include "container_traversal_fixture.h"
#include "ogplay/runtime/ui/ui_fallback_font.h"
#include "ogplay/runtime/ui/ui_leaf_measure.h"
#include <filesystem>
#include <algorithm>

using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-293 ROM TextView clinit measures H through instance Paint on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        std::ifstream input(root / "data/android/19/fonts/DroidSansFallback.ttf", std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
        std::vector<std::byte> bytes(raw.size());
        std::memcpy(bytes.data(), raw.data(), raw.size());
        f.context->ui_tree.SetFallbackFont(std::make_shared<ui::UiFallbackFont>(bytes));
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/TextView;");
        f.Checked(f.vm.EnsureClassInitialized(type));
        REQUIRE(f.context->paint_measure_states.size() == 1);
        const auto& warmup = f.context->paint_measure_states.begin()->second;
        CHECK(warmup.flags == 0x501);
        CHECK(warmup.text_size == 12.0F);
        CHECK(warmup.measurements == 1);
        CHECK(warmup.last_text == u"H");
        CHECK(warmup.last_width > 0);
        CHECK(warmup.last_width == ui::MeasureFixedText(u"H", 12, 0, f.context->ui_tree.FallbackFont().get()).width);
        f.Checked(f.vm.EnsureClassInitialized(type));
        CHECK(warmup.measurements == 1);
        const auto entered = f.vm.Trace("", 10000);
        for (const auto signature : {"Landroid/widget/TextView;-><clinit>()V", "Landroid/graphics/RectF;-><init>()V",
                                     "Landroid/text/SpannedString;-><init>(Ljava/lang/CharSequence;)V",
                                     "Landroid/text/BoringLayout$Metrics;-><init>()V",
                                     "Landroid/graphics/Paint;->measureText(Ljava/lang/String;)F"}) {
            CHECK(std::any_of(entered.begin(), entered.end(), [&](const auto& event) {
                return event.class_descriptor + "->" + event.method_name + event.method_descriptor == signature;
            }));
        }
        const auto a = f.New("Landroid/graphics/Paint;"),
                   b = f.New("Landroid/graphics/Paint;", "(I)V", {VmValue::Int(1)});
        const auto roots = f.vm.ProtectReferences(std::array{a, b});
        f.On(a, "setTextSize", "(F)V", {VmValue::Float(24)});
        f.On(a, "setAntiAlias", "(Z)V", {VmValue::Int(0)});
        CHECK(f.On(a, "getTextSize", "()F").AsFloat() == 24);
        CHECK(f.On(b, "getTextSize", "()F").AsFloat() == 12);
        CHECK(f.On(a, "isAntiAlias", "()Z").AsInt() == 0);
        CHECK(f.On(b, "isAntiAlias", "()Z").AsInt() == 1);
        CHECK(f.On(a, "getFlags", "()I").AsInt() == 0x500);
        f.On(b, "setFlags", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(b, "getFlags", "()I").AsInt() == 0);
        CHECK(f.On(a, "getFlags", "()I").AsInt() == 0x500);
        const auto text = f.model.NewString(u"xHIy");
        const auto measure = [&](VmObjectRef paint, const char* desc, std::vector<VmValue> args) {
            return f.On(paint, "measureText", desc, std::move(args)).AsFloat();
        };
        const auto expected = ui::MeasureFixedText(u"HI", 24, 0, f.context->ui_tree.FallbackFont().get()).width;
        CHECK(measure(a, "(Ljava/lang/String;II)F", {VmValue::Ref(text), VmValue::Int(1), VmValue::Int(3)}) ==
              expected);
        CHECK(measure(a, "(Ljava/lang/CharSequence;II)F", {VmValue::Ref(text), VmValue::Int(1), VmValue::Int(3)}) ==
              expected);
        const auto chars = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[C"), JniPrimitiveKind::character, 4);
        for (JniSize i = 0; i < 4; ++i)
            f.model.SetPrimitiveElement(chars, i, u"xHIy"[i]);
        CHECK(measure(a, "([CII)F", {VmValue::Ref(chars), VmValue::Int(1), VmValue::Int(2)}) == expected);
        const auto builder = f.New("Ljava/lang/StringBuilder;");
        f.On(builder, "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;", {VmValue::Ref(text)});
        CHECK(measure(a, "(Ljava/lang/CharSequence;II)F", {VmValue::Ref(builder), VmValue::Int(1), VmValue::Int(3)}) ==
              expected);
        CHECK(measure(b, "(Ljava/lang/String;)F", {VmValue::Ref(f.model.NewString(u"H"))}) ==
              ui::MeasureFixedText(u"H", 12).width);
        CHECK(measure(a, "(Ljava/lang/String;)F", {VmValue::Ref(f.model.NewString(u""))}) == 0);
        const auto face = f.Static("Landroid/graphics/Typeface;", "defaultFromStyle", "(I)Landroid/graphics/Typeface;",
                                   {VmValue::Int(3)})
                              .ref;
        CHECK(f.On(a, "setTypeface", "(Landroid/graphics/Typeface;)Landroid/graphics/Typeface;", {VmValue::Ref(face)})
                  .ref == face);
        CHECK(f.On(a, "getTypeface", "()Landroid/graphics/Typeface;").ref == face);
        CHECK_FALSE(f.On(b, "getTypeface", "()Landroid/graphics/Typeface;").ref.IsValid());
        const auto cjk = f.model.NewString(u"王");
        CHECK(measure(a, "(Ljava/lang/String;)F", {VmValue::Ref(cjk)}) ==
              ui::MeasureFixedText(u"王", 24, 3, f.context->ui_tree.FallbackFont().get()).width);
        const auto retired = f.New("Landroid/graphics/Paint;");
        static_cast<void>(f.vm.CollectGarbage("paint-measure-owners"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("paint-measure-finalized"));
        CHECK(f.context->paint_measure_states.contains(a.Value()));
        CHECK(f.context->paint_measure_states.contains(b.Value()));
        CHECK_FALSE(f.context->paint_measure_states.contains(retired.Value()));
        CHECK(f.On(a, "getTypeface", "()Landroid/graphics/Typeface;").ref == face);
        CHECK(f.ledger.Unimplemented().empty());
        // TextView construction and the full TextPaint/Layout stack remain closed.
        const auto ctor = f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(ctor);
        CHECK(f.vm.Call(*ctor, std::array{VmValue::Ref(f.vm.NewIntrinsicInstance("Landroid/widget/TextView;")),
                                          VmValue::Ref(VmObjectRef{})})
                  .exception.IsValid());
    }
}

TEST_CASE("DVM-293 unsupported Paint features and invalid ranges fail explicitly") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto paint = f.New("Landroid/graphics/Paint;");
        for (const auto entry :
             {std::pair{"setShader", "(Landroid/graphics/Shader;)Landroid/graphics/Shader;"},
              std::pair{"setPathEffect", "(Landroid/graphics/PathEffect;)Landroid/graphics/PathEffect;"},
              std::pair{"setMaskFilter", "(Landroid/graphics/MaskFilter;)Landroid/graphics/MaskFilter;"}}) {
            CHECK(f.OnOutcome(paint, entry.first, entry.second, {VmValue::Ref(VmObjectRef{})}).exception.IsValid());
            CHECK_FALSE(f.ledger.Unimplemented().empty());
        }
        for (const auto text : {u"\u0627", u"\U0001f600", u"\u0378", u"\n"})
            CHECK(f.OnOutcome(paint, "measureText", "(Ljava/lang/String;)F", {VmValue::Ref(f.model.NewString(text))})
                      .exception.IsValid());
        CHECK(f.OnOutcome(paint, "setTextSize", "(F)V", {VmValue::Float(0)}).exception.IsValid());
        CHECK(f.OnOutcome(paint, "setFlags", "(I)V", {VmValue::Int(8)}).exception.IsValid());
        CHECK(f.On(paint, "getTextSize", "()F").AsFloat() == 12);
        CHECK(f.On(paint, "getFlags", "()I").AsInt() == 0x500);
        CHECK(f.OnOutcome(paint, "measureText", "(Ljava/lang/String;II)F",
                          {VmValue::Ref(f.model.NewString(u"H")), VmValue::Int(-1), VmValue::Int(1)})
                  .exception.IsValid());
        CHECK(f.OnOutcome(paint, "measureText", "(Ljava/lang/String;)F",
                          {VmValue::Ref(f.model.NewString(std::u16string(1025, u'H')))})
                  .exception.IsValid());
        CHECK(f.context->paint_measure_states.at(paint.Value()).measurements == 0);
    }
}
