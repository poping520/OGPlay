#include "container_traversal_fixture.h"
#include "android_value_fixture.h"
#include "ogplay/runtime/integration/theme_resources.h"
#include "ogplay/loader/apk.h"
#include <array>

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

namespace {
VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.context->activity = owner;
    return owner;
}

std::array<int, 4> Padding(ContainerVm& f, VmObjectRef object) {
    const auto rect = f.New("Landroid/graphics/Rect;");
    REQUIRE(f.On(object, "getPadding", "(Landroid/graphics/Rect;)Z", {VmValue::Ref(rect)}).AsInt() == 1);
    std::array<int, 4> result{};
    int i = 0;
    for (const auto name : {"left", "top", "right", "bottom"}) {
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(rect), name, "I");
        result[static_cast<std::size_t>(i++)] =
            static_cast<int>(f.model.InstanceSlots(rect)[f.linker.Field(*field).slot].bits);
    }
    return result;
}
}  // namespace

TEST_CASE("DVM-326 same ROM named style flags resolve through real TypedArray on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto theme = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        f.model.SetPrimitiveElement(attrs, 0, 0x01010097);  // textStyle
        const auto array = f.On(theme, "obtainStyledAttributes", "(I[I)Landroid/content/res/TypedArray;",
                                {VmValue::Int(0x0103003e), VmValue::Ref(attrs)})
                               .ref;  // TextAppearance
        CHECK(f.On(array, "getInt", "(II)I", {VmValue::Int(0), VmValue::Int(-1)}).AsInt() == 0);
        const auto value = f.On(array, "peekValue", "(I)Landroid/util/TypedValue;", {VmValue::Int(0)}).ref;
        REQUIRE(value.IsValid());
        const auto type = f.linker.FindFieldRecursive(f.model.ObjectClass(value), "type", "I");
        CHECK(f.model.InstanceSlots(value)[f.linker.Field(*type).slot].bits == 17);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-326 ROM edit text selector scales true ninepatch padding and selects state without drawing") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto skin =
            f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)}).ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, resources, skin});
        CHECK(f.linker.Class(f.model.ObjectClass(skin)).descriptor == "Landroid/graphics/drawable/StateListDrawable;");
        CHECK(Padding(f, skin) == std::array<int, 4>{11, 7, 11, 12});
        CHECK(f.On(skin, "isStateful", "()Z").AsInt() == 1);
        const auto current = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(current.IsValid());
        CHECK(f.context->ui_drawables.at(current.Value()).resource_id == 0x01080640);
        const auto states = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 3);
        f.model.SetPrimitiveElement(states, 0, 0x0101009d);  // window focused
        f.model.SetPrimitiveElement(states, 1, 0x0101009c);  // focused
        f.model.SetPrimitiveElement(states, 2, 0x0101009e);  // enabled
        CHECK(f.On(skin, "setState", "([I)Z", {VmValue::Ref(states)}).AsInt() == 1);
        const auto selected = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(f.context->ui_drawables.at(selected.Value()).resource_id == 0x01080668);
        CHECK(Padding(f, skin) == std::array<int, 4>{11, 7, 11, 12});
        REQUIRE(f.context->ui_drawables.at(selected.Value()).leaf_pixels);
        CHECK(f.ledger.Unimplemented().empty());
        const auto draw = f.OnOutcome(skin, "draw", "(Landroid/graphics/Canvas;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(draw.exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        const auto unknown = f.OnOutcome(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;",
                                         {VmValue::Int(0x0108ffff)});
        CHECK(unknown.exception.IsValid());
    }
}

TEST_CASE("DVM-326 window tokens follow attachment stable window identity and permanent retirement") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        CHECK_FALSE(f.On(leaf, "getWindowToken", "()Landroid/os/IBinder;").ref.IsValid());
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        CHECK_FALSE(f.On(leaf, "getApplicationWindowToken", "()Landroid/os/IBinder;").ref.IsValid());
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto token = f.On(leaf, "getWindowToken", "()Landroid/os/IBinder;").ref;
        REQUIRE(token.IsValid());
        const auto roots = f.vm.ProtectReferences(std::array{owner, leaf, token});
        CHECK(f.On(token, "isBinderAlive", "()Z").AsInt() == 1);
        static_cast<void>(f.vm.CollectGarbage("window-token-rooted"));
        CHECK(f.On(leaf, "getWindowToken", "()Landroid/os/IBinder;").ref == token);
        CHECK(f.On(leaf, "getApplicationWindowToken", "()Landroid/os/IBinder;").ref == token);
        const auto replacement = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(replacement)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        CHECK(f.On(replacement, "getWindowToken", "()Landroid/os/IBinder;").ref == token);
        CHECK_FALSE(f.On(leaf, "getWindowToken", "()Landroid/os/IBinder;").ref.IsValid());
        const auto root = AndroidUiTraversalRoot(f.vm, *f.context);
        RetireViewUiSubtree(*f.context, *FindViewUiNode(*f.context, root.Value()));
        CHECK(f.On(token, "isBinderAlive", "()Z").AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());
        const auto imm = f.On(owner, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                              {VmValue::Ref(f.vm.NewStringUtf8("input_method"))})
                             .ref;
        const auto ime = f.OnOutcome(imm, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z",
                                     {VmValue::Ref(token), VmValue::Int(0)});
        CHECK_FALSE(ime.exception.IsValid());
        CHECK(ime.value.AsInt() == 0);
        CHECK(f.OnOutcome(token, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
                          {VmValue::Int(1), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)})
                  .exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        const auto parcel = f.Static("Landroid/os/Parcel;", "obtain", "()Landroid/os/Parcel;").ref;
        const auto marshalled =
            f.OnOutcome(parcel, "writeStrongBinder", "(Landroid/os/IBinder;)V", {VmValue::Ref(token)});
        CHECK(marshalled.exception.IsValid());
        CHECK(marshalled.exception_message.find("cannot be marshalled") != std::string::npos);
    }
}

TEST_CASE("DVM-326 projected strings retain ROM cookie and reject an invalid pool index") {
    const std::string projection =
        R"({"schema":"ogplay.bounded-theme","schema_version":1,"api_level":19,"configuration":"values","resource_types":{},"projected_references":[],"styleables":[],"registered":[{"id":16973829,"name":"Theme","attributes":[{"id":16843692,"name":"fontFamily","state":"known","type":3,"data":0,"cookie":2}]}],"unregistered":[],"style_closure":[],"strings":["normal"],"string_pool":"01001c003000000001000000000000000000000020000000000000000000000006006e006f0072006d0061006c000000"})";
    ThemeResources resources;
    resources.Load(projection);
    ogplay::loader::ArscTable table;
    const auto theme = resources.New();
    resources.Apply(theme, 16973829, true, table);
    const auto value = resources.Attribute(theme, 16843692, true, table);
    REQUIRE(value);
    CHECK(value->type == 3);
    CHECK(value->cookie == 2);
    CHECK(value->data == 0);
    XmlCursorRegistry cursors;
    const auto pool = cursors.CreateStringPool(resources.Strings());
    CHECK(cursors.StringPool(pool)->String(0) == "normal");
    auto invalid = projection;
    invalid.replace(invalid.find("\"data\":0"), 8, "\"data\":1");
    ThemeResources rejected;
    CHECK_THROWS_WITH(rejected.Load(invalid), "invalid projected string reference");
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        REQUIRE_FALSE(f.context->theme_projection_loaded);
        f.context->theme_resources.Load(projection);
        f.context->theme_projection_loaded = true;
        f.context->arsc.global_string_pool = resources.Strings();
        for (std::size_t i = 0; i < 6; ++i)
            f.context->arsc.global_string_pool[34 + i * 2] = static_cast<std::byte>(std::string_view("WRONG!")[i]);
        f.context->application_theme = 16973829;
        const auto owner = Owner(f);
        const auto theme_object = f.On(owner, "getTheme", "()Landroid/content/res/Resources$Theme;").ref;
        const auto output = f.New("Landroid/util/TypedValue;");
        CHECK(f.On(theme_object, "resolveAttribute", "(ILandroid/util/TypedValue;Z)Z",
                   {VmValue::Int(16843692), VmValue::Ref(output), VmValue::Int(1)})
                  .AsInt() == 1);
        const auto field =
            f.linker.FindFieldRecursive(f.model.ObjectClass(output), "string", "Ljava/lang/CharSequence;");
        const VmObjectRef string{f.model.InstanceSlots(output)[f.linker.Field(*field).slot].bits};
        CHECK(f.vm.StringUtf8(string) == "normal");
        const auto attrs = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
        f.model.SetPrimitiveElement(attrs, 0, 16843692);
        const auto values =
            f.On(theme_object, "obtainStyledAttributes", "([I)Landroid/content/res/TypedArray;", {VmValue::Ref(attrs)})
                .ref;
        CHECK(f.vm.StringUtf8(f.On(values, "getString", "(I)Ljava/lang/String;", {VmValue::Int(0)}).ref) == "normal");
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-326 ROM selector padding follows exact hdpi and xhdpi variants") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto density : {1.5F, 2.0F}) {
            ContainerVm f(backend);
            f.context->ui_density = density;
            const auto owner = Owner(f);
            const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto skin =
                f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)})
                    .ref;
            CHECK(Padding(f, skin) ==
                  (density == 1.5F ? std::array<int, 4>{16, 10, 16, 17} : std::array<int, 4>{21, 13, 21, 23}));
            const auto current = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
            const auto insets = f.On(current, "getOpticalInsets", "()Landroid/graphics/Insets;").ref;
            REQUIRE(insets.IsValid());
            for (const auto name : {"left", "top", "right", "bottom"}) {
                const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(insets), name, "I");
                CHECK(f.model.InstanceSlots(insets)[f.linker.Field(*field).slot].bits == 0);
            }
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

TEST_CASE("DVM-326 selector traces its ninepatch children and releases decoded backing on GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto owner_roots = f.vm.ProtectReferences(std::array{owner, resources});
        VmObjectRef skin;
        std::vector<VmObjectRef> children;
        std::weak_ptr<const ui::UiBitmap> pixels;
        {
            skin = f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)})
                       .ref;
            const auto root = f.vm.ProtectReferences(std::array{skin});
            children = f.context->ui_drawables.at(skin.Value()).skin_children;
            REQUIRE(children.size() == 6);
            pixels = f.context->ui_drawables.at(children.front().Value()).leaf_pixels;
            static_cast<void>(f.vm.CollectGarbage("rom-skin-rooted"));
            CHECK_FALSE(pixels.expired());
            for (const auto child : children)
                CHECK(f.context->ui_drawables.contains(child.Value()));
            CHECK(Padding(f, skin) == std::array<int, 4>{11, 7, 11, 12});
        }
        static_cast<void>(f.vm.CollectGarbage("rom-skin-unrooted"));
        CHECK(pixels.expired());
        CHECK_FALSE(f.context->ui_drawables.contains(skin.Value()));
        for (const auto child : children)
            CHECK_FALSE(f.context->ui_drawables.contains(child.Value()));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-327 ROM color selectors execute XML inflation and state/default selection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto res = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        for (const auto id : {0x01060003, 0x01060005}) {
            const auto color =
                f.On(res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(id)}).ref;
            const auto roots = f.vm.ProtectReferences(std::array{owner, res, color});
            REQUIRE(color.IsValid());
            CHECK(f.On(color, "isStateful", "()Z").AsInt() == 1);
            CHECK(static_cast<std::uint32_t>(f.On(res, "getColor", "(I)I", {VmValue::Int(id)}).AsInt()) ==
                  (id == 0x01060003 ? 0xff000000U : 0xffbebebeU));
            const auto states =
                f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 1);
            f.model.SetPrimitiveElement(states, 0, 0x0101009e);
            const auto selected =
                f.On(color, "getColorForState", "([II)I", {VmValue::Ref(states), VmValue::Int(0)}).AsInt();
            CHECK(static_cast<std::uint32_t>(selected) == (id == 0x01060003 ? 0xff000000U : 0xffbebebeU));
            CHECK(static_cast<std::uint32_t>(f.On(color, "getDefaultColor", "()I").AsInt()) ==
                  (id == 0x01060003 ? 0xff000000U : 0xffbebebeU));
            f.model.SetPrimitiveElement(states, 0, 0x0101009d);  // window focused, disabled
            CHECK(static_cast<std::uint32_t>(
                      f.On(color, "getColorForState", "([II)I", {VmValue::Ref(states), VmValue::Int(0)}).AsInt()) ==
                  (id == 0x01060003 ? 0x80000000U : 0x80bebebeU));
        }
        CHECK(f.ledger.Unimplemented().empty());
        CHECK(
            f.OnOutcome(res, "getColorStateList", "(I)Landroid/content/res/ColorStateList;", {VmValue::Int(0x0106ffff)})
                .exception.IsValid());
    }
}

TEST_CASE("DVM-327 JavaTraversal IME uses real bounded focus before rejecting connections") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto leaf = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto imm = f.On(owner, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                              {VmValue::Ref(f.vm.NewStringUtf8("input_method"))})
                             .ref;
        const auto check = [&](bool active) {
            const auto show =
                f.OnOutcome(imm, "showSoftInput", "(Landroid/view/View;I)Z", {VmValue::Ref(leaf), VmValue::Int(0)});
            const auto hide = f.OnOutcome(imm, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z",
                                          {VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
            for (const auto* result : {&show, &hide}) {
                CHECK(result->exception.IsValid() == active);
                if (active)
                    CHECK(result->exception_message == "InputConnection creation for the focused View is unsupported");
                else
                    CHECK(result->value.AsInt() == 0);
            }
        };
        check(false);
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(leaf)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        f.context->window_focus_activity.store(owner.Value());
        f.context->window_has_focus.store(true);
        check(false);
        f.On(leaf, "setFocusable", "(Z)V", {VmValue::Int(1)});
        REQUIRE(f.On(leaf, "requestFocus", "()Z").AsInt() == 1);
        CHECK(f.ledger.Unimplemented().empty());
        check(true);
        f.context->window_has_focus.store(false);
        check(false);
    }
}

TEST_CASE("API19 View drawable state skips stateless backgrounds and rejects unsupported stateful ones") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto view = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto color = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V",
                                 {VmValue::Int(static_cast<std::int32_t>(0xff000000U))});
        const auto config = f.Static("Landroid/graphics/Bitmap$Config;", "valueOf",
                                     "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;",
                                     {VmValue::Ref(f.vm.NewStringUtf8("ARGB_8888"))})
                                .ref;
        const auto bitmap = f.Static("Landroid/graphics/Bitmap;", "createBitmap",
                                     "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
                                     {VmValue::Int(1), VmValue::Int(1), VmValue::Ref(config)})
                                .ref;
        const auto image =
            f.New("Landroid/graphics/drawable/BitmapDrawable;", "(Landroid/graphics/Bitmap;)V", {VmValue::Ref(bitmap)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, color, image});
        const auto state_calls = [&] {
            int count = 0;
            for (const auto& event : f.vm.Trace("", 10000))
                if (event.method_name == "setState") ++count;
            return count;
        };
        // Populate the Java background field directly: BitmapDrawable.getOpacity
        // is outside this state protocol regression's supported surface.
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(view), "mBackground",
                                                       "Landroid/graphics/drawable/Drawable;");
        REQUIRE(field);
        const auto background = [&](VmObjectRef drawable) {
            f.model.InstanceSlots(view)[f.linker.Field(*field).slot] = {drawable.Value(), SlotTag::ref};
        };
        for (const auto drawable : {color, image}) {
            CHECK(f.On(drawable, "isStateful", "()Z").AsInt() == 0);
            background(drawable);
            const auto before = state_calls();
            CHECK_FALSE(f.OnOutcome(view, "refreshDrawableState", "()V").exception.IsValid());
            CHECK(state_calls() == before);
        }
        CHECK(f.ledger.Unimplemented().empty());

        background(color);
        // Substitute the virtual implementation to exercise true and throwing overrides
        // without adding a BootDex class or changing the sealed fixture recipe.
        const auto slot = f.linker.FindVtableIndex(f.model.ObjectClass(color), "isStateful", "()Z");
        REQUIRE(slot);
        auto& method = f.linker.MutableMethod(f.linker.Class(f.model.ObjectClass(color)).vtable[*slot]);
        method.kind = MethodKind::intrinsic;
        int queries = 0;
        method.implementation = [&queries](IntrinsicContext&) {
            ++queries;
            return VmValue::Int(1);
        };
        const auto before = state_calls();
        const auto rejected = f.OnOutcome(view, "refreshDrawableState", "()V");
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        CHECK(rejected.exception_message == "Drawable state unsupported");
        CHECK(queries == 1);
        CHECK(state_calls() == before);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.view_drawable_state");
        CHECK(hits.front().count == 1);

        const auto exception_root = f.vm.ProtectReferences(std::array{rejected.exception});
        method.implementation = [exception = rejected.exception](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "sentinel", exception};
        };
        const auto propagated = f.OnOutcome(view, "refreshDrawableState", "()V");
        CHECK(propagated.exception == rejected.exception);
        CHECK(f.ledger.Unimplemented().front().count == 1);
        CHECK(state_calls() == before);
    }
}

TEST_CASE("API19 View drawable state retains ROM skin state selection") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto view = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto skin =
            f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)}).ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, skin});
        f.On(view, "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(skin)});
        f.On(view, "refreshDrawableState", "()V");
        const auto enabled = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        f.On(view, "setEnabled", "(Z)V", {VmValue::Int(0)});
        f.On(view, "refreshDrawableState", "()V");
        const auto disabled = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(disabled != enabled);
        CHECK(f.context->ui_drawables.at(disabled.Value()).resource_id == 0x01080640);
        f.On(view, "setEnabled", "(Z)V", {VmValue::Int(1)});
        f.On(view, "refreshDrawableState", "()V");
        CHECK(f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref == enabled);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-352 ROM background color mutation invalidates and projects Java color on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto view = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        const auto tick = [&] {
            return EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                            AndroidUiTraversalPhase::ResolveOnly);
        };
        const auto background = [&] {
            return f.On(view, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
        };
        const auto set_color = [&](std::uint32_t color) {
            f.On(view, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(color))});
        };
        REQUIRE_FALSE(background().IsValid());
        set_color(0xff000000U);
        const auto color = background();
        REQUIRE(color.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(color)).descriptor == "Landroid/graphics/drawable/ColorDrawable;");
        CHECK(static_cast<std::uint32_t>(f.On(color, "getColor", "()I").AsInt()) == 0xff000000U);
        REQUIRE(tick());
        const auto node = *FindViewUiNode(*f.context, view.Value());
        const auto projected = [&]() -> const ui::UiNode& { return *f.context->ui_tree.Get(node); };
        const auto pixel = [&] {
            const auto image =
                ui::RasterizeUiOverlay(ui::BuildUiRenderList(*f.context->ui_tree.PublishedProjection(), {}),
                                       {static_cast<std::int32_t>(f.context->surface_width),
                                        static_cast<std::int32_t>(f.context->surface_height)});
            return std::array<std::uint8_t, 4>{image.rgba8[0], image.rgba8[1], image.rgba8[2], image.rgba8[3]};
        };
        CHECK(projected().background_color == 0x000000ffU);
        CHECK(pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255});
        CHECK(projected().background_resource_id == 0);
        CHECK(projected().background_alpha == 1.0F);
        CHECK(f.ledger.Unimplemented().empty());
        REQUIRE_FALSE(tick());
        const auto invalidations = [&] {
            int count = 0;
            for (const auto& event : f.vm.Trace("", 10000))
                if (event.method_name == "invalidateDrawable") ++count;
            return count;
        };
        const auto before = invalidations();
        set_color(0x80ff0000U);
        CHECK(background() == color);
        CHECK(invalidations() > before);
        CHECK(static_cast<std::uint32_t>(f.On(color, "getColor", "()I").AsInt()) == 0x80ff0000U);
        static_cast<void>(tick());
        CHECK(projected().background_color == 0xff000080U);
        // Raster output uses premultiplied source-over channels on a transparent plane.
        CHECK(pixel() == std::array<std::uint8_t, 4>{128, 0, 0, 128});
        CHECK(projected().background_alpha == 1.0F);
        set_color(0);
        static_cast<void>(tick());
        CHECK(pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0});
        CHECK_FALSE(projected().background_color);
        CHECK(projected().background_resource_id == 0);
        set_color(0xff123456U);
        static_cast<void>(tick());
        f.On(view, "setBackground", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(VmObjectRef{})});
        static_cast<void>(tick());
        CHECK_FALSE(projected().background_color);
        CHECK(projected().background_resource_id == 0);
        for (const auto visibility : {4, 8}) {
            f.On(view, "setVisibility", "(I)V", {VmValue::Int(visibility)});
            const auto calls = invalidations();
            set_color(0xff000000U);
            CHECK(invalidations() > calls);
            CHECK(f.On(background(), "isVisible", "()Z").AsInt() == 0);
            f.On(view, "setBackground", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(VmObjectRef{})});
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-352 unreviewed background projection clears old color and records the drawing gap") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto view = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto image = f.New("Ltest/UnreviewedBackground;");
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, image});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        f.On(view, "setBackgroundColor", "(I)V", {VmValue::Int(-1)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        f.On(view, "setBackground", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(image)});
        f.On(view, "invalidate", "()V");
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                   AndroidUiTraversalPhase::ResolveOnly));
        const auto& projected = *f.context->ui_tree.Get(*FindViewUiNode(*f.context, view.Value()));
        CHECK_FALSE(projected.background_color);
        CHECK(projected.background_resource_id == 0);
        CHECK(projected.background_alpha == 1.0F);
        const auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.view_background_draw");
        CHECK(hits.front().count == 1);
    }
}

TEST_CASE("DVM-352 Drawable subclasses never silently inherit unsupported state methods") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend, true);
        for (const auto* owner : {"BitmapDrawable", "PaintDrawable", "ShapeDrawable", "DrawableContainer",
                                  "NinePatchDrawable", "GradientDrawable", "LayerDrawable", "StateListDrawable"}) {
            const auto descriptor = std::string("Landroid/graphics/drawable/") + owner + ";";
            const auto type = f.linker.ResolveDescriptor(descriptor);
            // No constructor is needed to establish virtual dispatch/failure admission.
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            const auto roots = f.vm.ProtectReferences(std::array{object});
            for (const auto& [name, signature] : std::array{
                     std::pair{"mutate", "()Landroid/graphics/drawable/Drawable;"},
                     std::pair{"getConstantState", "()Landroid/graphics/drawable/Drawable$ConstantState;"},
                     std::pair{"getAlpha", "()I"}, std::pair{"setAlpha", "(I)V"}, std::pair{"getOpacity", "()I"}}) {
                const auto slot = f.linker.FindVtableIndex(type, name, signature);
                REQUIRE(slot);
                const auto& target = f.linker.Method(f.linker.Class(type).vtable[*slot]);
                CHECK(f.linker.Class(target.owner).descriptor != "Landroid/graphics/drawable/Drawable;");
                // Skin opacity is implemented against decoded backing; all state mutators above are rejected.
                if (std::string_view(name) == "getOpacity" &&
                    (std::string_view(owner) == "DrawableContainer" || std::string_view(owner) == "NinePatchDrawable" ||
                     std::string_view(owner) == "StateListDrawable"))
                    continue;
                const auto result = f.OnOutcome(object, name, signature,
                                                std::string_view(name) == "setAlpha" ? std::vector{VmValue::Int(128)}
                                                                                     : std::vector<VmValue>{});
                REQUIRE(result.exception.IsValid());
                const bool skeleton_only = std::string_view(owner) == "ShapeDrawable" ||
                                           std::string_view(owner) == "GradientDrawable" ||
                                           std::string_view(owner) == "LayerDrawable";
                CHECK(
                    f.linker.Class(result.exception_class).descriptor ==
                    (skeleton_only ? "Ljava/lang/UnsatisfiedLinkError;" : "Ljava/lang/UnsupportedOperationException;"));
            }
        }
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
