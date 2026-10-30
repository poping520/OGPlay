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
        std::ifstream primary(root / "data/android/19/fonts/Roboto-Regular.ttf", std::ios::binary);
        REQUIRE(primary.good());
        const std::vector<char> primary_raw{std::istreambuf_iterator<char>{primary}, std::istreambuf_iterator<char>{}};
        std::vector<std::byte> roboto(primary_raw.size());
        std::memcpy(roboto.data(), primary_raw.data(), primary_raw.size());
        f.context->ui_tree.SetFallbackFont(std::make_shared<ui::UiFallbackFont>(roboto, bytes));
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
        const auto copyright = f.model.NewString(u"©");
        CHECK(measure(a, "(Ljava/lang/String;)F", {VmValue::Ref(copyright)}) ==
              ui::MeasureFixedText(u"©", 24, 3, f.context->ui_tree.FallbackFont().get()).width);
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

TEST_CASE("DVM-356 RectF ROM value semantics and explicit failure boundaries on both backends") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto type = f.linker.ResolveDescriptor("Landroid/graphics/RectF;");
        CHECK(f.linker.Class(type).is_boot_dex);
        CHECK_FALSE(f.linker.Class(type).is_intrinsic);
        const auto field = [&](VmObjectRef object, const char* name, const char* descriptor = "F") {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(id);
            return f.model.InstanceSlots(object)[f.linker.Field(*id).slot].bits;
        };
        const auto coordinates = [&](VmObjectRef object, std::array<float, 4> expected) {
            std::size_t i = 0;
            for (const auto name : {"left", "top", "right", "bottom"})
                CHECK(std::bit_cast<float>(field(object, name)) == expected[i++]);
        };
        const auto values = [](float l, float t, float r, float b) {
            return std::vector{VmValue::Float(l), VmValue::Float(t), VmValue::Float(r), VmValue::Float(b)};
        };
        const auto rect = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                {VmValue::Int(-3), VmValue::Int(2), VmValue::Int(8), VmValue::Int(12)});
        const auto a = f.New("Landroid/graphics/RectF;", "(FFFF)V", values(1, 2, 10, 20));
        const auto b = f.New("Landroid/graphics/RectF;", "(Landroid/graphics/RectF;)V", {VmValue::Ref(a)});
        const auto c = f.New("Landroid/graphics/RectF;", "(Landroid/graphics/Rect;)V", {VmValue::Ref(rect)});
        const auto roots = f.vm.ProtectReferences(std::array{rect, a, b, c});
        coordinates(a, {1, 2, 10, 20});
        coordinates(b, {1, 2, 10, 20});
        coordinates(c, {-3, 2, 8, 12});
        for (const auto descriptor : {"(Landroid/graphics/Rect;)V", "(Landroid/graphics/RectF;)V"})
            coordinates(f.New("Landroid/graphics/RectF;", descriptor, {VmValue::Ref(VmObjectRef{})}), {0, 0, 0, 0});
        f.On(a, "set", "(Landroid/graphics/Rect;)V", {VmValue::Ref(rect)});
        coordinates(a, {-3, 2, 8, 12});
        f.On(a, "set", "(Landroid/graphics/RectF;)V", {VmValue::Ref(b)});
        coordinates(a, {1, 2, 10, 20});
        f.On(a, "set", "(FFFF)V", values(-1, -2, 9, 18));
        coordinates(a, {-1, -2, 9, 18});
        CHECK(f.On(a, "width", "()F").AsFloat() == 10);
        CHECK(f.On(a, "height", "()F").AsFloat() == 20);
        CHECK(f.On(a, "centerX", "()F").AsFloat() == 4);
        CHECK(f.On(a, "centerY", "()F").AsFloat() == 8);
        CHECK(f.On(a, "isEmpty", "()Z").AsInt() == 0);
        CHECK(f.On(a, "contains", "(FF)Z", {VmValue::Float(-1), VmValue::Float(-2)}).AsInt() == 1);
        CHECK(f.On(a, "contains", "(FF)Z", {VmValue::Float(9), VmValue::Float(0)}).AsInt() == 0);
        CHECK(f.On(a, "contains", "(FF)Z", {VmValue::Float(0), VmValue::Float(18)}).AsInt() == 0);
        CHECK(f.On(a, "contains", "(FFFF)Z", values(-1, -2, 9, 18)).AsInt() == 1);
        CHECK(f.On(a, "contains", "(Landroid/graphics/RectF;)Z", {VmValue::Ref(b)}).AsInt() == 0);
        CHECK(f.On(a, "intersect", "(FFFF)Z", values(9, 0, 12, 10)).AsInt() == 0);
        coordinates(a, {-1, -2, 9, 18});  // Touching edges do not intersect or mutate.
        CHECK(f.On(a, "intersect", "(Landroid/graphics/RectF;)Z", {VmValue::Ref(b)}).AsInt() == 1);
        coordinates(a, {1, 2, 9, 18});
        f.On(a, "union", "(Landroid/graphics/RectF;)V", {VmValue::Ref(c)});
        coordinates(a, {-3, 2, 9, 18});
        f.On(a, "union", "(FFFF)V", values(10, 20, 10, 30));  // Empty input is ignored.
        coordinates(a, {-3, 2, 9, 18});
        f.On(a, "union", "(FF)V", {VmValue::Float(-5), VmValue::Float(22)});
        coordinates(a, {-5, 2, 9, 22});
        CHECK(f.On(a, "intersects", "(FFFF)Z", values(9, 2, 11, 22)).AsInt() == 0);
        CHECK(f.Static("Landroid/graphics/RectF;", "intersects", "(Landroid/graphics/RectF;Landroid/graphics/RectF;)Z",
                       {VmValue::Ref(a), VmValue::Ref(b)})
                  .AsInt() == 1);
        CHECK(f.On(a, "setIntersect", "(Landroid/graphics/RectF;Landroid/graphics/RectF;)Z",
                   {VmValue::Ref(b), VmValue::Ref(c)})
                  .AsInt() == 1);
        coordinates(a, {1, 2, 8, 12});
        f.On(a, "offset", "(FF)V", {VmValue::Float(3), VmValue::Float(-4)});
        coordinates(a, {4, -2, 11, 8});
        f.On(a, "offsetTo", "(FF)V", {VmValue::Float(0), VmValue::Float(1)});
        coordinates(a, {0, 1, 7, 11});
        f.On(a, "inset", "(FF)V", {VmValue::Float(1), VmValue::Float(2)});
        coordinates(a, {1, 3, 6, 9});
        f.On(a, "set", "(FFFF)V", values(6, 9, 1, 3));
        f.On(a, "sort", "()V");
        coordinates(a, {1, 3, 6, 9});
        f.On(a, "set", "(FFFF)V", values(-1.6F, -0.4F, 6.4F, 9.6F));
        f.On(a, "round", "(Landroid/graphics/Rect;)V", {VmValue::Ref(rect)});
        CHECK(static_cast<std::int32_t>(field(rect, "left", "I")) == -2);
        CHECK(field(rect, "top", "I") == 0);
        CHECK(field(rect, "right", "I") == 6);
        CHECK(field(rect, "bottom", "I") == 10);
        f.On(a, "set", "(Landroid/graphics/RectF;)V", {VmValue::Ref(b)});
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(b)}).AsInt() == 1);
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(c)}).AsInt() == 0);
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(rect)}).AsInt() == 0);
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(VmObjectRef{})}).AsInt() == 0);
        // API19 folds float bits with Java int overflow; +0 and -0 share a hash.
        std::uint32_t hash = 0;
        for (const auto v : {1.0F, 2.0F, 10.0F, 20.0F})
            hash = 31U * hash + std::bit_cast<std::uint32_t>(v);
        CHECK(f.On(a, "hashCode", "()I").AsInt() == std::bit_cast<std::int32_t>(hash));
        CHECK(f.On(a, "hashCode", "()I").AsInt() == f.On(b, "hashCode", "()I").AsInt());
        CHECK(f.vm.StringUtf8(f.On(a, "toString", "()Ljava/lang/String;").ref) == "RectF(1.0, 2.0, 10.0, 20.0)");
        CHECK(f.vm.StringUtf8(f.On(a, "toShortString", "()Ljava/lang/String;").ref) == "[1.0,2.0][10.0,20.0]");
        const auto sb = f.New("Ljava/lang/StringBuilder;");
        CHECK(f.vm.StringUtf8(
                  f.On(a, "toShortString", "(Ljava/lang/StringBuilder;)Ljava/lang/String;", {VmValue::Ref(sb)}).ref) ==
              "[1.0,2.0][10.0,20.0]");
        const auto writer = f.New("Ljava/io/StringWriter;");
        const auto writer_root = f.vm.ProtectReferences(std::array{writer});
        const auto printer = f.New("Ljava/io/PrintWriter;", "(Ljava/io/Writer;)V", {VmValue::Ref(writer)});
        f.On(a, "printShortString", "(Ljava/io/PrintWriter;)V", {VmValue::Ref(printer)});
        CHECK(f.vm.StringUtf8(f.On(writer, "toString", "()Ljava/lang/String;").ref) == "[1.0,2.0][10.0,20.0]");
        f.On(a, "setEmpty", "()V");
        f.On(b, "set", "(FFFF)V", values(-0.0F, 0, -0.0F, 0));
        coordinates(a, {0, 0, 0, 0});
        CHECK(f.On(a, "isEmpty", "()Z").AsInt() == 1);
        CHECK(f.On(a, "contains", "(FF)Z", {VmValue::Float(0), VmValue::Float(0)}).AsInt() == 0);
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(b)}).AsInt() == 1);
        CHECK(f.On(a, "hashCode", "()I").AsInt() == 0);
        CHECK(f.On(b, "hashCode", "()I").AsInt() == 0);
        const auto nan = std::bit_cast<float>(0x7fa12345U);
        f.On(a, "set", "(FFFF)V", values(0, 0, 0, nan));
        f.On(b, "set", "(FFFF)V", values(0, 0, 0, nan));
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(a)}).AsInt() == 1);
        CHECK(f.On(a, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(b)}).AsInt() == 0);
        CHECK(f.On(a, "hashCode", "()I").AsInt() == 0x7fc00000);
        CHECK(f.On(a, "describeContents", "()I").AsInt() == 0);
        const auto creator_field = f.linker.FindFieldRecursive(type, "CREATOR", "Landroid/os/Parcelable$Creator;");
        REQUIRE(creator_field);
        const VmObjectRef creator{f.linker.Class(type).static_storage[f.linker.Field(*creator_field).slot]};
        for (const auto desc : {"(I)[Landroid/graphics/RectF;", "(I)[Ljava/lang/Object;"}) {
            const auto array = f.On(creator, "newArray", desc, {VmValue::Int(3)}).ref;
            CHECK(f.model.ArrayLength(array) == 3);
            CHECK(f.model.ObjectArrayElementClass(array) == type);
            CHECK(f.OnOutcome(creator, "newArray", desc, {VmValue::Int(-1)}).exception.IsValid());
        }
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto& entry : {std::pair{"roundOut", "(Landroid/graphics/Rect;)V"},
                                  std::pair{"writeToParcel", "(Landroid/os/Parcel;I)V"}}) {
            const auto outcome =
                entry.first == std::string_view("roundOut")
                    ? f.OnOutcome(a, entry.first, entry.second, {VmValue::Ref(rect)})
                    : f.OnOutcome(a, entry.first, entry.second, {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
            REQUIRE(outcome.exception.IsValid());
            CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
            const auto hits = f.ledger.Unimplemented();
            CHECK(std::any_of(hits.begin(), hits.end(), [&](const auto& hit) {
                return hit.id ==
                           std::string("dexvm.intrinsic.Landroid/graphics/RectF;.") + entry.first + entry.second &&
                       hit.count == 1;
            }));
        }
        CHECK(f.ledger.Unimplemented().size() == 2);
        for (const auto* methods :
             {&f.linker.Class(type).own_direct_methods, &f.linker.Class(type).own_virtual_methods}) {
            for (const auto id : *methods) {
                const auto& method = f.linker.Method(id);
                if (method.name == "roundOut" || method.name == "writeToParcel" || method.name == "readFromParcel") {
                    CHECK(method.kind == MethodKind::intrinsic);
                    CHECK_FALSE(method.implementation);
                    CHECK_FALSE(method.code);
                } else {
                    CHECK(method.kind == MethodKind::interpreted);
                    CHECK(method.code.has_value());
                }
            }
        }
    }
}
