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

TEST_CASE("registry inflater preserves RelativeLayout sibling rules") {
    ClickVm vm;
    std::vector<ogplay::loader::BinaryXmlElement> elements(3);
    elements[0].name = "RelativeLayout";
    elements[0].attributes = {
        AndroidAttribute("layout_width", 0x10, 0xffffffffU),
        AndroidAttribute("layout_height", 0x10, 0xffffffffU)};
    elements[1].name = "View";
    elements[1].parent = 0;
    elements[1].attributes = {
        AndroidAttribute("id", 0x01, 700),
        AndroidAttribute("layout_width", 0x10, 20),
        AndroidAttribute("layout_height", 0x10, 10),
        AndroidAttribute("layout_centerInParent", 0x12, 1)};
    elements[2].name = "View";
    elements[2].parent = 0;
    elements[2].attributes = {
        AndroidAttribute("layout_width", 0x10, 5),
        AndroidAttribute("layout_height", 0x10, 5),
        AndroidAttribute("layout_toRightOf", 0x01, 700),
        AndroidAttribute("layout_below", 0x01, 700)};

    static_cast<void>(InflateUiElements(vm.interpreter, *vm.context, elements));
    ui::LayoutUiTree(vm.context->ui_tree, {100, 80});
    const auto* root = vm.context->ui_tree.Get(vm.context->ui_tree.Root());
    REQUIRE(root->children.size() == 1);
    const auto* relative = vm.context->ui_tree.Get(root->children[0]);
    REQUIRE(relative->children.size() == 2);
    CHECK(vm.context->ui_tree.Get(relative->children[0])->screen_frame ==
          ui::Rect{40, 35, 60, 45});
    CHECK(vm.context->ui_tree.Get(relative->children[1])->screen_frame ==
          ui::Rect{60, 45, 65, 50});
}

TEST_CASE("include expansion matches inline geometry and applies overrides") {
    std::vector<ogplay::loader::BinaryXmlElement> included(2);
    included[0].name = "LinearLayout";
    included[0].attributes = {
        AndroidAttribute("layout_width", 0x10, 10),
        AndroidAttribute("layout_height", 0x10, 10)};
    included[1].name = "View";
    included[1].parent = 0;
    included[1].attributes = {
        AndroidAttribute("layout_width", 0x10, 5),
        AndroidAttribute("layout_height", 0x10, 5)};

    std::vector<ogplay::loader::BinaryXmlElement> outer(2);
    outer[0].name = "FrameLayout";
    outer[0].attributes = {
        AndroidAttribute("layout_width", 0x10, 0xffffffffU),
        AndroidAttribute("layout_height", 0x10, 0xffffffffU)};
    outer[1].name = "include";
    outer[1].parent = 0;
    outer[1].attributes = {
        AndroidAttribute("layout", 0x01, 900),
        AndroidAttribute("id", 0x01, 901),
        AndroidAttribute("layout_width", 0x10, 40),
        AndroidAttribute("layout_height", 0x10, 20),
        AndroidAttribute("layout_gravity", 0x11, 0x11)};
    const auto expanded = ExpandUiIncludes(
        outer, [&included](const std::uint32_t id) {
            if (id != 900U) throw std::runtime_error("unexpected layout id");
            return included;
        }, 899U);
    REQUIRE(expanded.size() == 3);
    CHECK(expanded[1].name == "LinearLayout");
    CHECK(expanded[1].parent == 0);
    CHECK(expanded[2].parent == 1);

    ClickVm include_vm;
    static_cast<void>(InflateUiElements(include_vm.interpreter,
                                        *include_vm.context, expanded));
    ui::LayoutUiTree(include_vm.context->ui_tree, {100, 100});
    const auto included_root =
        include_vm.context->ui_tree.FindByAndroidId(901);
    REQUIRE(included_root.has_value());
    const auto included_frame =
        include_vm.context->ui_tree.Get(*included_root)->screen_frame;
    const auto included_child_frame = include_vm.context->ui_tree.Get(
        include_vm.context->ui_tree.Get(*included_root)->children[0])->screen_frame;

    std::vector<ogplay::loader::BinaryXmlElement> inline_layout(3);
    inline_layout[0] = outer[0];
    inline_layout[1].name = "LinearLayout";
    inline_layout[1].parent = 0;
    inline_layout[1].attributes = {
        AndroidAttribute("id", 0x01, 901),
        AndroidAttribute("layout_width", 0x10, 40),
        AndroidAttribute("layout_height", 0x10, 20),
        AndroidAttribute("layout_gravity", 0x11, 0x11)};
    inline_layout[2] = included[1];
    inline_layout[2].parent = 1;
    ClickVm inline_vm;
    static_cast<void>(InflateUiElements(inline_vm.interpreter,
                                        *inline_vm.context, inline_layout));
    ui::LayoutUiTree(inline_vm.context->ui_tree, {100, 100});
    const auto inline_root = inline_vm.context->ui_tree.FindByAndroidId(901);
    REQUIRE(inline_root.has_value());
    CHECK(included_frame == ui::Rect{30, 40, 70, 60});
    CHECK(included_frame ==
          inline_vm.context->ui_tree.Get(*inline_root)->screen_frame);
    CHECK(included_child_frame == inline_vm.context->ui_tree.Get(
        inline_vm.context->ui_tree.Get(*inline_root)->children[0])->screen_frame);
}

TEST_CASE("DVM-245 scoped XML inflation keeps other windows and the stable container") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto first = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto second = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{first, second});
        const auto query = [&](VmObjectRef activity, int id) {
            return vm.CallOn(activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(id)}).ref;
        };
        const auto parent = query(first, 0x01020002);
        const auto other = query(second, 0x01020002);
        CHECK(parent != other);
        const auto node = *FindViewUiNode(*vm.context, parent.Value());
        const auto other_node = *FindViewUiNode(*vm.context, other.Value());
        std::vector<ogplay::loader::BinaryXmlElement> elements(3);
        elements[0].name = "merge";
        for (std::size_t i = 1; i < 3; ++i) {
            elements[i].name = "View"; elements[i].parent = 0;
            elements[i].attributes = {AndroidAttribute("id", 1, static_cast<std::uint32_t>(100 + i)),
                AndroidAttribute("layout_width", 0x10, 30), AndroidAttribute("layout_height", 0x10, 10)};
        }
        const auto other_child = InflateUiElements(vm.interpreter, *vm.context, elements, second, other_node);
        const auto child = InflateUiElements(vm.interpreter, *vm.context, elements, first, node);
        CHECK(vm.context->ui_tree.Get(node)->children.size() == 2);
        CHECK(vm.context->ui_tree.Get(other_node)->children.size() == 2);
        CHECK(vm.CallOn(child, "getParent", "()Landroid/view/ViewParent;").ref == parent);
        CHECK(query(first, 101) == child);
        CHECK(query(second, 101) == other_child);
        const auto before = vm.context->object_to_ui_node.size();
        // Failure after a bound but not-yet-attached node must not leak its binding.
        elements[1].attributes[1] = AndroidAttribute("layout_width", 5, 0x0000010f);
        CHECK_THROWS(static_cast<void>(InflateUiElements(vm.interpreter, *vm.context, elements, first, node)));
        CHECK(vm.context->ui_tree.Get(node)->children.empty());
        CHECK(vm.context->object_to_ui_node.size() == before - 2);
        CHECK(query(first, 0x01020002) == parent);
        CHECK(query(second, 101) == other_child);
        // The Window XML entry preserves the same isolation on resource failure.
        const auto type = vm.model.ObjectClass(first);
        const auto method = *vm.linker.FindVtableIndex(type, "setContentView", "(I)V");
        const auto result = vm.interpreter.Call(vm.linker.Class(type).vtable[method],
            std::array{VmValue::Ref(first), VmValue::Int(999)});
        REQUIRE(result.exception.IsValid());
        CHECK(query(first, 0x01020002) == parent);
        CHECK(query(second, 101) == other_child);
    }
}

TEST_CASE("DVM-247 inflater service identity cloning and guest Context ownership") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto activity = vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
        const auto roots = vm.interpreter.ProtectReferences(std::array{base, activity});
        vm.CallDirect(activity, "Landroid/app/Activity;", "<init>", "()V");
        vm.CallOn(activity, "attachBaseContext", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto service = vm.interpreter.NewStringUtf8("layout_inflater");
        const auto service_root = vm.interpreter.ProtectReferences(std::array{service});
        const auto get = [&](VmObjectRef object) {
            return vm.CallOn(object, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service)}).ref;
        };
        const auto base_inflater = get(base);
        const auto inflater = vm.CallOn(activity, "getLayoutInflater", "()Landroid/view/LayoutInflater;").ref;
        CHECK(base_inflater != inflater);
        CHECK(get(activity) == inflater);
        const auto window = vm.CallOn(activity, "getWindow", "()Landroid/view/Window;").ref;
        CHECK(vm.CallOn(window, "getLayoutInflater", "()Landroid/view/LayoutInflater;").ref == inflater);
        const auto method = *vm.linker.FindDirectMethod(vm.linker.ResolveDescriptor("Landroid/view/LayoutInflater;"), "from", "(Landroid/content/Context;)Landroid/view/LayoutInflater;");
        const auto from = vm.interpreter.Call(method, std::array{VmValue::Ref(activity)});
        REQUIRE_FALSE(from.exception.IsValid());
        CHECK(from.value.ref == inflater);
        CHECK(vm.CallOn(inflater, "getContext", "()Landroid/content/Context;").ref == activity);
        CHECK(vm.CallOn(base_inflater, "getContext", "()Landroid/content/Context;").ref == base);
        const auto wrapper = vm.interpreter.NewIntrinsicInstance("Landroid/view/ContextThemeWrapper;");
        const auto wrapper_root = vm.interpreter.ProtectReferences(std::array{wrapper});
        vm.CallDirect(wrapper, "Landroid/view/ContextThemeWrapper;", "<init>", "(Landroid/content/Context;I)V", {VmValue::Ref(base), VmValue::Int(42)});
        const auto themed = get(wrapper);
        CHECK(themed != inflater);
        CHECK(themed != base_inflater);
        CHECK(vm.CallOn(themed, "getContext", "()Landroid/content/Context;").ref == wrapper);
        CHECK(vm.CallOn(wrapper, "getThemeResId", "()I").AsInt() == 42);
        const auto clone = vm.CallOn(inflater, "cloneInContext", "(Landroid/content/Context;)Landroid/view/LayoutInflater;", {VmValue::Ref(base)}).ref;
        const auto clone_root = vm.interpreter.ProtectReferences(std::array{clone});
        CHECK(clone != inflater);
        CHECK(clone != base_inflater);
        CHECK(vm.CallOn(clone, "getContext", "()Landroid/content/Context;").ref == base);
        static_cast<void>(vm.interpreter.CollectGarbage("inflater-context-fields"));
        CHECK(get(base) == base_inflater);
        CHECK(get(wrapper) == themed);
        CHECK(get(activity) == inflater);
    }
}

TEST_CASE("DVM-247 public inflate detached append typed parameters merge and local rollback") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ClickVm vm(backend);
        const auto base = vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
        const auto base_root = vm.interpreter.ProtectReferences(std::array{base});
        const auto service = vm.interpreter.NewStringUtf8("layout_inflater");
        const auto service_root = vm.interpreter.ProtectReferences(std::array{service});
        const auto inflater = vm.CallOn(base, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", {VmValue::Ref(service)}).ref;
        const auto inflater_root = vm.interpreter.ProtectReferences(std::array{inflater});
        const auto parent = vm.CallOn(vm.activity, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        const auto parent_node = *FindViewUiNode(*vm.context, parent.Value());
        vm.context->managed_host_surface_open = true;
        const auto surface = vm.interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
        const auto surface_root = vm.interpreter.ProtectReferences(std::array{surface});
        vm.CallDirect(surface, "Landroid/view/SurfaceView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(base)});
        const auto holder = vm.CallOn(surface, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
        const auto callback = vm.interpreter.NewIntrinsicInstance("LContentSurfaceCallback;");
        vm.CallOn(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(callback)});
        vm.CallOn(vm.activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(surface)});
        REQUIRE(vm.content_surface_creates == 1);
        const auto old_children = vm.context->ui_tree.Get(parent_node)->children;
        vm.inflater_elements.resize(3);
        vm.inflater_elements[0].name = "RelativeLayout";
        vm.inflater_elements[0].attributes = {AndroidAttribute("id", 1, 700), AndroidAttribute("layout_width", 0x10, 90), AndroidAttribute("layout_height", 0x10, 40)};
        vm.inflater_elements[1].name = "SurfaceView"; vm.inflater_elements[1].parent = 0;
        vm.inflater_elements[1].attributes = {AndroidAttribute("id", 1, 701), AndroidAttribute("layout_centerInParent", 0x12, 1)};
        vm.inflater_elements[2].name = "TextView"; vm.inflater_elements[2].parent = 0;
        vm.inflater_elements[2].attributes = {AndroidAttribute("id", 1, 702), AndroidAttribute("layout_alignBottom", 1, 701), AndroidAttribute("layout_centerHorizontal", 0x12, 1), AndroidAttribute("layout_marginBottom", 0x10, 10)};
        const auto inflate = [&](VmObjectRef root, bool attach) {
            return vm.CallOn(inflater, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;", {VmValue::Int(123), VmValue::Ref(root), VmValue::Int(attach ? 1 : 0)}).ref;
        };
        const auto detached = vm.CallOn(inflater, "inflate", "(ILandroid/view/ViewGroup;)Landroid/view/View;", {VmValue::Int(123), VmValue::Ref(VmObjectRef{})}).ref;
        const auto detached_root = vm.interpreter.ProtectReferences(std::array{detached});
        CHECK(vm.inflater_finish_ids == std::vector<int>{701, 702, 700});
        CHECK(vm.context->ui_tree.Get(parent_node)->children == old_children);
        CHECK_FALSE(vm.CallOn(detached, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        CHECK_FALSE(vm.CallOn(detached, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref.IsValid());
        CHECK(vm.CallOn(detached, "getContext", "()Landroid/content/Context;").ref == base);
        CHECK(vm.content_surface_creates == 1);
        const auto text = vm.CallOn(detached, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(702)}).ref;
        const auto params = vm.CallOn(text, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(vm.linker.Class(vm.model.ObjectClass(params)).descriptor == "Landroid/widget/RelativeLayout$LayoutParams;");
        const auto rules = vm.CallOn(params, "getRules", "()[I").ref;
        CHECK(vm.model.GetPrimitiveElement(rules, 8) == 701);
        CHECK(vm.model.GetPrimitiveElement(rules, 14) == 0xffffffffU);
        vm.CallOn(text, "requestLayout", "()V");
        CHECK(vm.context->ui_tree.Get(*FindViewUiNode(*vm.context, text.Value()))->layout.margin.bottom == 10);
        const auto typed = inflate(parent, false);
        const auto typed_root = vm.interpreter.ProtectReferences(std::array{typed});
        CHECK_FALSE(vm.CallOn(typed, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        const auto root_params = vm.CallOn(typed, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(vm.linker.Class(vm.model.ObjectClass(root_params)).descriptor == "Landroid/widget/FrameLayout$LayoutParams;");
        CHECK(vm.context->ui_tree.Get(parent_node)->children == old_children);
        CHECK(inflate(parent, true) == parent);
        CHECK(vm.context->ui_tree.Get(parent_node)->children.size() == old_children.size() + 1);
        CHECK(vm.content_surface_destroys == 0);
        // Surface construction stayed detached; only actual publication creates it.
        CHECK(vm.content_surface_creates == 1); // new layout's holder has no listener
        const auto before = vm.context->object_to_ui_node.size();
        const auto children_before = vm.context->ui_tree.Get(parent_node)->children;
        vm.inflater_elements[2].name = "UnsupportedWidget";
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base, parent, true)));
        CHECK(vm.context->object_to_ui_node.size() == before);
        CHECK(vm.context->ui_tree.Get(parent_node)->children == children_before);
        vm.inflater_elements[2].name = "TextView";
        const auto saved_elements = vm.inflater_elements;
        vm.inflater_elements.resize(ui::UiTree::kMaxDepth + 1);
        for (std::size_t i = 0; i < vm.inflater_elements.size(); ++i) {
            vm.inflater_elements[i].name = "FrameLayout";
            vm.inflater_elements[i].parent = i == 0 ? -1 : static_cast<std::int32_t>(i - 1);
        }
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base, parent, true)));
        CHECK(vm.context->object_to_ui_node.size() == before);
        CHECK(vm.context->ui_tree.Get(parent_node)->children == children_before);
        vm.inflater_elements = saved_elements;
        vm.inflater_elements[0].name = "View";
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base, parent, true)));
        CHECK(vm.context->object_to_ui_node.size() == before);
        CHECK(vm.context->ui_tree.Get(parent_node)->children == children_before);
        vm.inflater_elements[0].name = "RelativeLayout";
        vm.inflater_finish_hook = [](IntrinsicContext&) { throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "finish failed"}; };
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base, parent, true)));
        CHECK(vm.context->object_to_ui_node.size() == before);
        CHECK(vm.context->ui_tree.Get(parent_node)->children == children_before);
        vm.inflater_finish_hook = {};
        vm.inflater_elements[0].name = "merge";
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base)));
        CHECK_THROWS(static_cast<void>(BuildUiElements(vm.interpreter, *vm.context, vm.inflater_elements, base, parent, false)));
        CHECK(inflate(parent, true) == parent);
        CHECK(vm.context->ui_tree.Get(parent_node)->children.size() == children_before.size() + 2);
        vm.inflater_elements.resize(1);
        CHECK(inflate(parent, true) == parent); // empty merge
        vm.inflater_elements.clear();
        const auto type = vm.model.ObjectClass(inflater);
        const auto method = *vm.linker.FindVtableIndex(type, "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;");
        const auto error = vm.interpreter.Call(vm.linker.Class(type).vtable[method], std::array{VmValue::Ref(inflater), VmValue::Int(999), VmValue::Ref(parent), VmValue::Int(1)});
        REQUIRE(error.exception.IsValid());
        CHECK(vm.linker.Class(error.exception_class).descriptor == "Landroid/content/res/Resources$NotFoundException;");
        CHECK(vm.content_surface_destroys == 0);
    }
}
