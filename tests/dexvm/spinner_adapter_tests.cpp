#include "android_value_fixture.h"
#include "container_traversal_fixture.h"
#include <thread>
#include <set>

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-364 Spinner ROM adapter layout and asynchronous selection chain") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        auto& record = f.context->activity_stack.emplace_back();
        record.object = owner;
        record.visible = record.resumed = true;
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x01030007)});
        const auto spinner = f.New("Ltest/SpinnerLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, spinner});
        const auto ref_field = [&](VmObjectRef object, const char* name, const char* type) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(field);
            return VmObjectRef{
                static_cast<std::uint32_t>(f.model.InstanceSlots(object)[f.linker.Field(*field).slot].bits)};
        };
        const auto popup = ref_field(spinner, "mPopup", "Landroid/widget/Spinner$SpinnerPopup;");
        CHECK(f.linker.Class(f.model.ObjectClass(popup)).descriptor == "Landroid/widget/Spinner$DialogPopup;");
        CHECK(f.On(spinner, "getGravity", "()I").AsInt() == 17);
        const auto background = f.On(spinner, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(background.IsValid());
        const auto count = f.On(background, "getStateCount", "()I").AsInt();
        CHECK(count == 7);
        for (int i = 0; i < count; ++i) {
            const auto child =
                f.On(background, "getStateDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(i)}).ref;
            CHECK(f.linker.Class(f.model.ObjectClass(child)).descriptor ==
                  "Landroid/graphics/drawable/NinePatchDrawable;");
            CHECK(f.On(child, "getIntrinsicWidth", "()I").AsInt() > 0);
        }
        const auto list = f.New("Ljava/util/ArrayList;");
        const auto list_root = f.vm.ProtectReferences(std::array{list});
        for (const auto text : {"First", "Much longer second label", "Third"}) {
            const auto item =
                f.New("Ltest/SpinnerItem;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8(text))});
            f.On(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(item)});
        }
        const auto adapter = f.New("Landroid/widget/ArrayAdapter;", "(Landroid/content/Context;ILjava/util/List;)V",
                                   {VmValue::Ref(owner), VmValue::Int(0x01090008), VmValue::Ref(list)});
        const auto adapter_root = f.vm.ProtectReferences(std::array{adapter});
        f.On(adapter, "setDropDownViewResource", "(I)V", {VmValue::Int(0x01090009)});
        f.On(spinner, "setAdapter", "(Landroid/widget/SpinnerAdapter;)V", {VmValue::Ref(adapter)});
        CHECK(f.On(spinner, "getCount", "()I").AsInt() == 3);
        CHECK(f.On(spinner, "getSelectedItemPosition", "()I").AsInt() == 0);
        CHECK(f.model.StringValue(
                  f.On(f.On(spinner, "getSelectedItem", "()Ljava/lang/Object;").ref, "toString", "()Ljava/lang/String;")
                      .ref) == u"First");
        const auto callback = f.New("Ltest/SpinnerSelection;");
        const auto number = [&](const char* name) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(callback), name, "I");
            REQUIRE(field);
            return static_cast<int>(f.model.InstanceSlots(callback)[f.linker.Field(*field).slot].bits);
        };
        const auto callback_root = f.vm.ProtectReferences(std::array{callback});
        f.On(spinner, "setOnItemSelectedListener", "(Landroid/widget/AdapterView$OnItemSelectedListener;)V",
             {VmValue::Ref(callback)});
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        for (int id : {0x01090008, 0x01090009}) {
            const auto parser =
                f.On(resources, "getLayout", "(I)Landroid/content/res/XmlResourceParser;", {VmValue::Int(id)}).ref;
            REQUIRE(parser.IsValid());
            CHECK(f.On(parser, "next", "()I").AsInt() == 0);
            CHECK(f.On(parser, "next", "()I").AsInt() == 2);
            const auto tag = f.On(parser, "getName", "()Ljava/lang/String;").ref;
            CHECK(f.On(parser, "getAttributeCount", "()I").AsInt() == 7);
            CHECK(f.model.StringValue(tag) == (id == 0x01090008 ? u"TextView" : u"CheckedTextView"));
            f.On(parser, "close", "()V");
        }
        const auto additional =
            f.On(resources, "getLayout", "(I)Landroid/content/res/XmlResourceParser;", {VmValue::Int(0x01090003)}).ref;
        REQUIRE(additional.IsValid());
        f.On(additional, "close", "()V");
        const auto measured =
            f.OnOutcome(spinner, "measure", "(II)V", {VmValue::Int(0x400000c8), VmValue::Int(0x40000030)});
        REQUIRE_MESSAGE(!measured.exception.IsValid(), measured.exception_message,
                        f.vm.DescribeThrowableCauses(measured.exception));
        CHECK(f.On(spinner, "getMeasuredWidth", "()I").AsInt() == 200);
        CHECK(f.On(spinner, "getMeasuredHeight", "()I").AsInt() == 48);
        f.On(spinner, "measure", "(II)V",
             {VmValue::Int(static_cast<int>(0x800000c8U)), VmValue::Int(static_cast<int>(0x80000030U))});
        CHECK(f.On(spinner, "getMeasuredWidth", "()I").AsInt() > 0);
        CHECK(f.On(spinner, "getMeasuredWidth", "()I").AsInt() <= 200);
        CHECK(f.On(spinner, "getMeasuredWidth", "()I").AsInt() >
              100);  // measureContentWidth consumes the longer second label.
        const auto container =
            f.New("Landroid/widget/AbsoluteLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto params = f.New("Landroid/widget/AbsoluteLayout$LayoutParams;", "(IIII)V",
                                  {VmValue::Int(200), VmValue::Int(48), VmValue::Int(12), VmValue::Int(20)});
        const auto container_root = f.vm.ProtectReferences(std::array{container, params});
        f.On(container, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(spinner), VmValue::Ref(params)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(container)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        f.On(spinner, "onAttachedToWindow", "()V");
        CHECK(f.On(spinner, "attachments", "()I").AsInt() == 1);
        CHECK(number("calls") == 0);
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(number("calls") == 1);
        CHECK(number("position") == 0);
        CHECK(ref_field(callback, "parent", "Landroid/widget/AdapterView;") == spinner);
        CHECK(ref_field(callback, "selected", "Landroid/view/View;") ==
              f.On(spinner, "getSelectedView", "()Landroid/view/View;").ref);
        const auto id_field = f.linker.FindFieldRecursive(f.model.ObjectClass(callback), "id", "J");
        REQUIRE(id_field);
        CHECK(f.model.InstanceSlots(callback)[f.linker.Field(*id_field).slot].bits == 0);
        CHECK(number("nothing") == 0);
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(number("calls") == 1);
        CHECK(f.On(spinner, "getChildCount", "()I").AsInt() == 1);
        const auto selected = f.On(spinner, "getSelectedView", "()Landroid/view/View;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(selected)).descriptor == "Landroid/widget/TextView;");
        CHECK(f.model.StringValue(f.On(selected, "getText", "()Ljava/lang/CharSequence;").ref) == u"First");
        CHECK(f.On(selected, "getId", "()I").AsInt() == 0x01020014);
        const auto node = FindViewUiNode(*f.context, selected.Value());
        REQUIRE(node);
        CHECK(f.context->ui_tree.Get(*node)->text == u"First");
        CHECK(f.On(selected, "getWidth", "()I").AsInt() > 0);
        const auto published = f.context->ui_tree.PublishedProjection();
        REQUIRE(published);
        REQUIRE(published->Get(*node));
        const auto child_frame = published->Get(*node)->frame;
        CHECK(child_frame == ui::Rect{f.On(selected, "getLeft", "()I").AsInt(), f.On(selected, "getTop", "()I").AsInt(),
                                      f.On(selected, "getRight", "()I").AsInt(),
                                      f.On(selected, "getBottom", "()I").AsInt()});
        const auto spinner_node = FindViewUiNode(*f.context, spinner.Value());
        REQUIRE(spinner_node);
        CHECK(published->Get(*spinner_node)->frame == ui::Rect{12, 20, 212, 68});
        CHECK(published->Get(*node)->screen_frame.left == 12 + child_frame.left);
        CHECK(published->Get(*node)->screen_frame.top == 20 + child_frame.top);
        CHECK(f.On(selected, "getEllipsize", "()Landroid/text/TextUtils$TruncateAt;").ref.IsValid());
        f.On(spinner, "setSelection", "(I)V", {VmValue::Int(2)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        CHECK(number("calls") == 1);
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(number("calls") == 2);
        CHECK(number("position") == 2);
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(number("calls") == 2);
        const auto marquee_view = f.On(spinner, "getSelectedView", "()Landroid/view/View;").ref;
        CHECK_FALSE(f.OnOutcome(marquee_view, "setSelected", "(Z)V", {VmValue::Int(1)}).exception.IsValid());
        CHECK(f.On(marquee_view, "isSelected", "()Z").AsInt() == 1);
        f.On(adapter, "clear", "()V");
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(f.On(spinner, "getCount", "()I").AsInt() == 0);
        CHECK(f.On(spinner, "getSelectedItemPosition", "()I").AsInt() == -1);
        CHECK(f.On(spinner, "getChildCount", "()I").AsInt() == 0);
        CHECK(number("nothing") == 1);
        const auto rejected_add = f.OnOutcome(spinner, "addView", "(Landroid/view/View;)V", {VmValue::Ref(container)});
        REQUIRE(rejected_add.exception.IsValid());
        CHECK(rejected_add.exception_message.find("not supported in AdapterView") != std::string::npos);
        for (const auto name : {"removeView", "removeViewAt", "removeAllViews"}) {
            const auto removed = std::string_view(name) == "removeView"
                                     ? f.OnOutcome(spinner, name, "(Landroid/view/View;)V", {VmValue::Ref(container)})
                                 : std::string_view(name) == "removeViewAt"
                                     ? f.OnOutcome(spinner, name, "(I)V", {VmValue::Int(0)})
                                     : f.OnOutcome(spinner, name, "()V");
            REQUIRE(removed.exception.IsValid());
            CHECK(f.linker.Class(removed.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        }
        const auto click = f.OnOutcome(spinner, "performClick", "()Z");
        REQUIRE(click.exception.IsValid());
        CHECK(click.exception_message == "Spinner popup interaction is unsupported");
    }
}

TEST_CASE("DVM-364 Spinner rejects popup constructors at explicit interaction boundaries") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        for (const auto [owner, signature] :
             {std::pair{"Landroid/widget/Spinner$DropdownPopup;",
                        "(Landroid/widget/Spinner;Landroid/content/Context;Landroid/util/AttributeSet;I)V"},
              std::pair{"Landroid/widget/Spinner$1;",
                        "(Landroid/widget/Spinner;Landroid/view/View;Landroid/widget/Spinner$DropdownPopup;)V"}}) {
            const auto type = f.linker.ResolveDescriptor(owner);
            const auto object = f.vm.NewIntrinsicInstance(owner);
            const auto root = f.vm.ProtectReferences(std::array{object});
            const auto constructor = f.linker.FindDirectMethod(type, "<init>", signature);
            REQUIRE(constructor);
            std::vector<VmValue> args{VmValue::Ref(object), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}),
                                      VmValue::Ref(VmObjectRef{})};
            if (std::string_view(owner).find("DropdownPopup") != std::string_view::npos)
                args.push_back(VmValue::Int(0));
            const auto out = f.vm.Call(*constructor, args);
            REQUIRE(out.exception.IsValid());
            CHECK(out.exception_message == "Spinner popup interaction is unsupported");
        }
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
