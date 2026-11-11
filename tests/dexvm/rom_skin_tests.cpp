#include "container_traversal_fixture.h"
#include "android_value_fixture.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/raster/bitmap.h"
#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include <tuple>

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

namespace {
VmObjectRef Owner(ContainerVm& f, bool managed_window = false) {
    f.context->ui_test_accessibility_disabled = true;
    const auto owner = f.New("Landroid/app/Activity;");
    ogplay::test::AttachViewTestContext(f.vm, owner, f.context);
    f.context->activity = owner;
    if (managed_window) {
        auto& record = f.context->activity_stack.emplace_back();
        record.object = owner;
        record.visible = true;
        record.resumed = true;
    }
    return owner;
}

VmObjectRef SkinBitmap(ContainerVm& f, VmObjectRef drawable) {
    const auto field =
        f.linker.FindFieldRecursive(f.model.ObjectClass(drawable), "mNinePatch", "Landroid/graphics/NinePatch;");
    REQUIRE(field);
    const auto patch = VmObjectRef(f.model.InstanceSlots(drawable)[f.linker.Field(*field).slot].bits);
    REQUIRE(patch.IsValid());
    return f.On(patch, "getBitmap", "()Landroid/graphics/Bitmap;").ref;
}

std::vector<VmObjectRef> Children(ContainerVm& f, VmObjectRef selector) {
    const auto count = f.On(selector, "getStateCount", "()I").AsInt();
    std::vector<VmObjectRef> children;
    for (int i = 0; i < count; ++i)
        children.push_back(
            f.On(selector, "getStateDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(i)}).ref);
    return children;
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

TEST_CASE("DVM-355 ROM scrollbar thumbs load as ninepatches with ROM identity and scaled dimensions") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (int scale = 0; scale < 3; ++scale) {
            ContainerVm f(backend);
            f.context->ui_density = 1.0F + static_cast<float>(scale) * 0.5F;
            const auto owner = Owner(f, true);
            const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
            const auto roots = f.vm.ProtectReferences(std::array{owner, resources});
            // Pinned PNG dimensions/densities: classic 37x13 and 13x37 at 320;
            // Holo 30x30 at 480. Expected results at device densities 160/240/320.
            const std::array long_side{19, 28, 37};
            const std::array short_side{7, 10, 13};
            const std::array holo_side{10, 15, 20};
            for (const auto id : {0x0108052a, 0x0108052b, 0x01080528, 0x01080529}) {
                const auto skin =
                    f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(id)}).ref;
                const auto skin_root = f.vm.ProtectReferences(std::array{skin});
                CHECK(f.linker.Class(f.model.ObjectClass(skin)).descriptor ==
                      "Landroid/graphics/drawable/NinePatchDrawable;");
                CHECK(SkinBitmap(f, skin).IsValid());
                const auto at = static_cast<std::size_t>(scale);
                const auto width = id == 0x0108052a ? long_side[at] : id == 0x0108052b ? short_side[at] : holo_side[at];
                const auto height = id == 0x0108052a   ? short_side[at]
                                    : id == 0x0108052b ? long_side[at]
                                                       : holo_side[at];
                CHECK(f.On(skin, "getIntrinsicWidth", "()I").AsInt() == width);
                CHECK(f.On(skin, "getIntrinsicHeight", "()I").AsInt() == height);
                const auto bitmap = SkinBitmap(f, skin);
                REQUIRE(f.On(bitmap, "getNinePatchChunk", "()[B").ref.IsValid());
                CHECK(f.On(bitmap, "getWidth", "()I").AsInt() == width);
                CHECK(f.On(bitmap, "getHeight", "()I").AsInt() == height);
            }
            CHECK(f.ledger.Unimplemented().empty());
            CHECK(f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080527)})
                      .ref.IsValid());
        }
    }
}

TEST_CASE("DVM-355 ROM scrollbar padding preserves user inputs and dispatches layout only for changes") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->target_sdk_version = 21;
        const auto owner = Owner(f, true);
        const auto view = f.New("Landroid/widget/ScrollView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        const auto field = [&](const char* name) -> Slot& {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(view), name, "I");
            REQUIRE(id);
            return f.model.InstanceSlots(view)[f.linker.Field(*id).slot];
        };
        const auto view_class = f.linker.ResolveDescriptor("Landroid/view/View;");
        const auto request =
            f.linker.Class(view_class).vtable[*f.linker.FindVtableIndex(view_class, "requestLayout", "()V")];
        const auto original = f.linker.Method(request).implementation;
        REQUIRE(original);
        int requests = 0;
        f.linker.MutableMethod(request).implementation = [&](IntrinsicContext& c) {
            ++requests;
            return original(c);
        };
        const auto padding = [&] {
            return std::array{field("mPaddingLeft").bits, field("mPaddingTop").bits, field("mPaddingRight").bits,
                              field("mPaddingBottom").bits};
        };
        for (bool rtl : {false, true}) {
            field("mPrivateFlags2").bits = (field("mPrivateFlags2").bits & ~0x10U) | (rtl ? 0x10U : 0U);
            CHECK(f.On(view, "getLayoutDirection", "()I").AsInt() == (rtl ? 1 : 0));
            for (const auto style : {0U, 0x01000000U, 0x03000000U}) {
                field("mViewFlags").bits = (field("mViewFlags").bits & ~0x03000300U) | style | 0x300U;
                const auto width = static_cast<std::uint32_t>(f.On(view, "getVerticalScrollbarWidth", "()I").AsInt());
                const auto height =
                    static_cast<std::uint32_t>(f.On(view, "getHorizontalScrollbarHeight", "()I").AsInt());
                REQUIRE(width > 0);
                REQUIRE(height > 0);
                const auto inset = style != 0;
                const auto expected = std::array{2U + (inset && rtl ? width : 0U), 3U,
                                                 5U + (inset && !rtl ? width : 0U), 7U + (inset ? height : 0U)};
                const auto before = padding();
                const auto count = requests;
                f.On(view, "setPadding", "(IIII)V",
                     {VmValue::Int(2), VmValue::Int(3), VmValue::Int(5), VmValue::Int(7)});
                CHECK(padding() == expected);
                CHECK(requests == count + (before != expected ? 1 : 0));
                CHECK(field("mUserPaddingLeft").bits == 2);
                CHECK(field("mUserPaddingRight").bits == 5);
                CHECK(field("mUserPaddingBottom").bits == 7);
                const auto unchanged = requests;
                f.On(view, "setPadding", "(IIII)V",
                     {VmValue::Int(2), VmValue::Int(3), VmValue::Int(5), VmValue::Int(7)});
                CHECK(requests == unchanged);
            }
        }
        f.linker.MutableMethod(request).implementation = original;
        const auto method =
            f.linker.Class(f.model.ObjectClass(view))
                .vtable[*f.linker.FindVtableIndex(f.model.ObjectClass(view), "internalSetPadding", "(IIII)V")];
        CHECK(f.linker.Method(method).kind == MethodKind::interpreted);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-355 withdrawing ROM internalSetPadding makes default ScrollView construction fail") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->target_sdk_version = 21;
        const auto owner = Owner(f, true);
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x01030007)});
        const auto type = f.linker.ResolveDescriptor("Landroid/view/View;");
        const auto slot = f.linker.FindVtableIndex(type, "internalSetPadding", "(IIII)V");
        REQUIRE(slot);
        auto& disabled = f.linker.MutableMethod(f.linker.Class(type).vtable[*slot]);
        disabled.kind = MethodKind::intrinsic;
        disabled.implementation = {};
        f.Checked(f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor("Landroid/widget/ScrollView;")));
        const auto object = f.vm.NewIntrinsicInstance("Landroid/widget/ScrollView;");
        const auto roots = f.vm.ProtectReferences(std::array{owner, object});
        const auto constructor =
            f.linker.FindDirectMethod(f.model.ObjectClass(object), "<init>", "(Landroid/content/Context;)V");
        REQUIRE(constructor);
        const auto failed = f.vm.Call(*constructor, std::array{VmValue::Ref(object), VmValue::Ref(owner)});
        REQUIRE(failed.exception.IsValid());
        INFO(failed.exception_message);
        CHECK(failed.exception_message.find("internalSetPadding") != std::string::npos);
    }
}

TEST_CASE("DVM-355 default classic and Holo Scroll constructors retain real cache and thumb identities") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto theme : {0x01030007, 0x0103006b}) {
            ContainerVm f(backend);
            f.context->target_sdk_version = 21;
            const auto owner = Owner(f, true);
            f.On(owner, "setTheme", "(I)V", {VmValue::Int(theme)});
            const auto get = [&](VmObjectRef object, const char* name, const char* type) {
                const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
                REQUIRE(field);
                return f.model.InstanceSlots(object)[f.linker.Field(*field).slot].bits;
            };
            const auto config =
                f.Static("Landroid/view/ViewConfiguration;", "get",
                         "(Landroid/content/Context;)Landroid/view/ViewConfiguration;", {VmValue::Ref(owner)})
                    .ref;
            CHECK(f.Static("Landroid/view/ViewConfiguration;", "get",
                           "(Landroid/content/Context;)Landroid/view/ViewConfiguration;", {VmValue::Ref(owner)})
                      .ref == config);
            CHECK(f.On(config, "getScaledScrollBarSize", "()I").AsInt() == 10);
            CHECK(f.On(config, "getScaledTouchSlop", "()I").AsInt() == 8);
            CHECK(f.On(config, "getScaledPagingTouchSlop", "()I").AsInt() == 16);
            CHECK(f.On(config, "getScaledMinimumFlingVelocity", "()I").AsInt() == 50);
            CHECK(f.On(config, "getScaledMaximumFlingVelocity", "()I").AsInt() == 8000);
            CHECK(f.On(config, "hasPermanentMenuKey", "()Z").AsInt() == 0);  // ARSC 0x01110044=true.
            for (const auto descriptor : {"Landroid/widget/ScrollView;", "Landroid/widget/HorizontalScrollView;"}) {
                const auto view = f.New(descriptor, "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
                const auto roots = f.vm.ProtectReferences(std::array{owner, config, view});
                const auto cache = VmObjectRef{get(view, "mScrollCache", "Landroid/view/View$ScrollabilityCache;")};
                REQUIRE(cache.IsValid());
                const auto bar = VmObjectRef{get(cache, "scrollBar", "Landroid/widget/ScrollBarDrawable;")};
                REQUIRE(bar.IsValid());
                const bool horizontal = std::string_view(descriptor).find("Horizontal") != std::string_view::npos;
                CHECK(f.On(view, horizontal ? "isHorizontalScrollBarEnabled" : "isVerticalScrollBarEnabled", "()Z")
                          .AsInt() == 1);
                for (bool vertical : {false, true}) {
                    const auto thumb = VmObjectRef{get(bar, vertical ? "mVerticalThumb" : "mHorizontalThumb",
                                                       "Landroid/graphics/drawable/Drawable;")};
                    REQUIRE(thumb.IsValid());
                    CHECK(f.linker.Class(f.model.ObjectClass(thumb)).descriptor ==
                          "Landroid/graphics/drawable/NinePatchDrawable;");
                    CHECK(SkinBitmap(f, thumb).IsValid());
                    CHECK(get(bar, vertical ? "mVerticalTrack" : "mHorizontalTrack",
                              "Landroid/graphics/drawable/Drawable;") == 0);
                }
                for (const auto [name, getter] : {std::pair{"mTouchSlop", "getScaledTouchSlop"},
                                                  {"mMinimumVelocity", "getScaledMinimumFlingVelocity"},
                                                  {"mMaximumVelocity", "getScaledMaximumFlingVelocity"},
                                                  {"mOverscrollDistance", "getScaledOverscrollDistance"},
                                                  {"mOverflingDistance", "getScaledOverflingDistance"}})
                    CHECK(get(view, name, "I") == static_cast<std::uint32_t>(f.On(config, getter, "()I").AsInt()));
                // ROM prefers positive intrinsic thumb thickness; config size is only a fallback.
                CHECK(f.On(view, "getVerticalScrollbarWidth", "()I").AsInt() == (theme == 0x01030007 ? 7 : 10));
                CHECK(get(cache, "scrollBarSize", "I") == 10);
            }
            CHECK(f.ledger.Unimplemented().empty());
        }
    }
}

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
        CHECK(SkinBitmap(f, current).IsValid());
        const auto states = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 3);
        f.model.SetPrimitiveElement(states, 0, 0x0101009d);  // window focused
        f.model.SetPrimitiveElement(states, 1, 0x0101009c);  // focused
        f.model.SetPrimitiveElement(states, 2, 0x0101009e);  // enabled
        CHECK(f.On(skin, "setState", "([I)Z", {VmValue::Ref(states)}).AsInt() == 1);
        const auto selected = f.On(skin, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        CHECK(SkinBitmap(f, selected).IsValid());
        CHECK(Padding(f, skin) == std::array<int, 4>{11, 7, 11, 12});
        REQUIRE(f.context->bitmaps.at(SkinBitmap(f, selected).Value()).pixels);
        CHECK(f.ledger.Unimplemented().empty());
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

TEST_CASE("DVM-326 ROM selector cache retains its ninepatch backing after caller GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto owner_roots = f.vm.ProtectReferences(std::array{owner, resources});
        VmObjectRef skin;
        std::vector<VmObjectRef> children;
        std::weak_ptr<raster::Bitmap> pixels;
        std::vector<VmObjectRef> bitmaps;
        {
            skin = f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0x01080016)})
                       .ref;
            const auto root = f.vm.ProtectReferences(std::array{skin});
            children = Children(f, skin);
            for (const auto child : children)
                bitmaps.push_back(SkinBitmap(f, child));
            REQUIRE(children.size() == 6);
            pixels = f.context->bitmaps.at(bitmaps.front().Value()).pixels;
            static_cast<void>(f.vm.CollectGarbage("rom-skin-rooted"));
            CHECK_FALSE(pixels.expired());
            for (const auto bitmap : bitmaps)
                CHECK(f.context->bitmaps.contains(bitmap.Value()));
            CHECK(Padding(f, skin) == std::array<int, 4>{11, 7, 11, 12});
        }
        static_cast<void>(f.vm.CollectGarbage("rom-skin-unrooted"));
        CHECK_FALSE(pixels.expired());
        for (const auto bitmap : bitmaps)
            CHECK(f.context->bitmaps.contains(bitmap.Value()));
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

TEST_CASE("API19 View drawable state dispatches stateful backgrounds and propagates errors") {
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
        CHECK_FALSE(f.OnOutcome(view, "refreshDrawableState", "()V").exception.IsValid());
        CHECK(queries == 1);
        CHECK(state_calls() > before);
        CHECK(f.ledger.Unimplemented().empty());
        method.implementation = [](IntrinsicContext&) -> VmValue {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "sentinel"};
        };
        const auto propagated = f.OnOutcome(view, "refreshDrawableState", "()V");
        REQUIRE(propagated.exception.IsValid());
        CHECK(propagated.exception_message == "sentinel");
        CHECK(f.ledger.Unimplemented().empty());
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
        CHECK(SkinBitmap(f, disabled).IsValid());
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

TEST_CASE("DVM-355 ViewConfiguration input seam honors ROM override and mainkeys and fails when withdrawn") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto [mainkeys, expected] : {std::pair{"", false}, {"0", false}, {"1", true}}) {
            ContainerVm f(backend);
            static_cast<void>(f.vm.SetSystemProperty("qemu.hw.mainkeys", mainkeys));
            const auto owner = Owner(f, true);
            const auto config =
                f.Static("Landroid/view/ViewConfiguration;", "get",
                         "(Landroid/content/Context;)Landroid/view/ViewConfiguration;", {VmValue::Ref(owner)})
                    .ref;
            CHECK(f.On(config, "hasPermanentMenuKey", "()Z").AsInt() == (expected ? 1 : 0));
            const auto type = f.model.ObjectClass(config);
            for (const auto name : {"sHasPermanentMenuKey", "sHasPermanentMenuKeySet"}) {
                const auto field = f.linker.FindFieldRecursive(type, name, "Z");
                REQUIRE(field);
                CHECK_FALSE(f.linker.Field(*field).is_static);
                CHECK(f.linker.Field(*field).access_flags == kAccPrivate);
            }
            CHECK(f.ledger.Unimplemented().empty());
        }
        ContainerVm f(backend);
        const auto owner = Owner(f, true);
        const auto type = f.linker.ResolveDescriptor("Landroid/view/ViewConfiguration;");
        f.Checked(f.vm.EnsureClassInitialized(type));
        const auto method = f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V");
        REQUIRE(method);
        CHECK(f.linker.Method(*method).kind == MethodKind::interpreted);
        REQUIRE(f.linker.Method(*method).constructor_inputs);
        f.linker.MutableMethod(*method).constructor_inputs = {};
        const auto object = f.vm.NewIntrinsicInstance("Landroid/view/ViewConfiguration;");
        const auto roots = f.vm.ProtectReferences(std::array{owner, object});
        const auto failed = f.vm.Call(*method, std::array{VmValue::Ref(object), VmValue::Ref(owner)});
        REQUIRE(failed.exception.IsValid());
        CHECK_FALSE(failed.exception_message.empty());
        INFO(failed.exception_message);
        CHECK(f.linker.Class(failed.exception_class).descriptor == "Ljava/lang/AbstractMethodError;");
        CHECK(failed.exception_message.find("hasNavigationBar") != std::string::npos);
        const auto hits = f.ledger.Unimplemented();
        CHECK(std::ranges::any_of(hits, [](const auto& hit) {
            return hit.id == "dexvm.abstract.Landroid/view/IWindowManager;.hasNavigationBar()Z";
        }));
    }
}

TEST_CASE("DVM-355 Interpolator samples ScrollabilityCache alpha keys repeat mirror reset and cubic blends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto interp = f.New("Landroid/graphics/Interpolator;", "(II)V", {VmValue::Int(1), VmValue::Int(2)});
        const auto array =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 1);
        const auto roots = f.vm.ProtectReferences(std::array{interp, array});
        const auto key = [&](int index, int time, float value) {
            f.model.SetPrimitiveElement(array, 0, std::bit_cast<std::uint32_t>(value));
            f.On(interp, "setKeyFrame", "(II[F)V", {VmValue::Int(index), VmValue::Int(time), VmValue::Ref(array)});
        };
        const auto sample = [&](int time, float expected, int result) {
            const auto sampled = f.On(interp, "timeToValues", "(I[F)Landroid/graphics/Interpolator$Result;",
                                      {VmValue::Int(time), VmValue::Ref(array)})
                                     .ref;
            CHECK(f.On(sampled, "ordinal", "()I").AsInt() == result);
            CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(f.model.GetPrimitiveElement(array, 0))) ==
                  doctest::Approx(expected).epsilon(0.002));
        };
        key(0, 1000, 255);
        key(1, 1250, 0);
        sample(999, 255, 1);
        sample(1000, 255, 0);
        sample(1125, 127.5F, 0);
        sample(1250, 0, 0);
        sample(1251, 0, 2);
        f.On(interp, "setRepeatMirror", "(FZ)V", {VmValue::Float(3), VmValue::Int(1)});
        sample(1375, 127.5F, 0);
        sample(1500, 255, 0);
        sample(1750, 0, 2);
        f.On(interp, "setRepeatMirror", "(FZ)V", {VmValue::Float(1.5F), VmValue::Int(0)});
        sample(1375, 127.5F, 2);
        f.On(interp, "reset", "(II)V", {VmValue::Int(1), VmValue::Int(2)});
        key(0, 2000, 10);
        key(1, 2100, 20);
        sample(2050, 15, 0);
        const auto blend =
            f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 4);
        const auto blend_root = f.vm.ProtectReferences(std::array{blend});
        for (int i = 0; i < 4; ++i)
            f.model.SetPrimitiveElement(blend, i, std::bit_cast<std::uint32_t>(i == 2 || i == 3 ? 1.0F : 0.0F));
        f.model.SetPrimitiveElement(array, 0, std::bit_cast<std::uint32_t>(10.0F));
        f.On(interp, "setKeyFrame", "(II[F[F)V",
             {VmValue::Int(0), VmValue::Int(2000), VmValue::Ref(array), VmValue::Ref(blend)});
        sample(2050, 15, 0);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-355 Paint retains drawable effects through GC") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto tile_name = f.vm.NewStringUtf8("CLAMP");
        const auto tile = f.Static("Landroid/graphics/Shader$TileMode;", "valueOf",
                                   "(Ljava/lang/String;)Landroid/graphics/Shader$TileMode;", {VmValue::Ref(tile_name)})
                              .ref;
        const auto shader =
            f.New("Landroid/graphics/LinearGradient;", "(FFFFIILandroid/graphics/Shader$TileMode;)V",
                  {VmValue::Float(0), VmValue::Float(0), VmValue::Float(0), VmValue::Float(1),
                   VmValue::Int(static_cast<std::int32_t>(0xff000000U)), VmValue::Int(0), VmValue::Ref(tile)});
        const auto mode_name = f.vm.NewStringUtf8("DST_OUT");
        const auto mode = f.Static("Landroid/graphics/PorterDuff$Mode;", "valueOf",
                                   "(Ljava/lang/String;)Landroid/graphics/PorterDuff$Mode;", {VmValue::Ref(mode_name)})
                              .ref;
        const auto xfer = f.New("Landroid/graphics/PorterDuffXfermode;", "(Landroid/graphics/PorterDuff$Mode;)V",
                                {VmValue::Ref(mode)});
        const auto paint = f.New("Landroid/graphics/Paint;");
        const auto canvas = f.New("Landroid/graphics/Canvas;");
        const auto roots = f.vm.ProtectReferences(std::array{paint, canvas});
        CHECK(f.On(paint, "setShader", "(Landroid/graphics/Shader;)Landroid/graphics/Shader;", {VmValue::Ref(shader)})
                  .ref == shader);
        CHECK(
            f.On(paint, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;", {VmValue::Ref(xfer)})
                .ref == xfer);
        const auto matrix = f.New("Landroid/graphics/Matrix;");
        const auto matrix_root = f.vm.ProtectReferences(std::array{matrix});
        f.On(matrix, "setTranslate", "(FF)V", {VmValue::Float(3), VmValue::Float(4)});
        f.On(shader, "setLocalMatrix", "(Landroid/graphics/Matrix;)V", {VmValue::Ref(matrix)});
        const auto copy = f.New("Landroid/graphics/Paint;", "(Landroid/graphics/Paint;)V", {VmValue::Ref(paint)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        const auto shader_copy = f.On(copy, "getShader", "()Landroid/graphics/Shader;").ref;
        CHECK(shader_copy != shader);
        const auto copied_matrix = f.New("Landroid/graphics/Matrix;");
        CHECK(f.On(shader_copy, "getLocalMatrix", "(Landroid/graphics/Matrix;)Z", {VmValue::Ref(copied_matrix)})
                  .AsInt() != 0);
        CHECK(f.On(copied_matrix, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(matrix)}).AsInt() != 0);
        CHECK(f.On(copy, "getXfermode", "()Landroid/graphics/Xfermode;").ref == xfer);
        f.On(copy, "reset", "()V");
        static_cast<void>(f.vm.CollectGarbage("paint-retains-graphics-state"));
        CHECK(f.model.IsValidRef(shader));
        CHECK(f.model.IsValidRef(xfer));
        CHECK(f.On(paint, "getShader", "()Landroid/graphics/Shader;").ref == shader);
        CHECK(f.On(paint, "getXfermode", "()Landroid/graphics/Xfermode;").ref == xfer);
        CHECK(f.ledger.Unimplemented().empty());
        f.On(canvas, "drawPaint", "(Landroid/graphics/Paint;)V", {VmValue::Ref(paint)});
        const auto effects_supported = [&] {
            const auto warnings = f.ledger.Warnings();
            CHECK(std::ranges::none_of(warnings,
                                       [](const auto& hit) { return hit.id.starts_with("ui.degraded.Canvas.Paint"); }));
        };
        effects_supported();
        f.On(paint, "setShader", "(Landroid/graphics/Shader;)Landroid/graphics/Shader;", {VmValue::Ref(VmObjectRef{})});
        f.On(canvas, "drawPaint", "(Landroid/graphics/Paint;)V", {VmValue::Ref(paint)});
        effects_supported();
        f.On(paint, "setXfermode", "(Landroid/graphics/Xfermode;)Landroid/graphics/Xfermode;",
             {VmValue::Ref(VmObjectRef{})});
        static_cast<void>(f.vm.CollectGarbage("paint-releases-graphics-state"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("paint-finalized-graphics-state"));
        CHECK_FALSE(f.model.IsValidRef(shader));
        CHECK_FALSE(f.model.IsValidRef(xfer));
    }
}

TEST_CASE("DVM-355 visible scrollbar projection records the gap without changing pixels and runs ROM fade") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->target_sdk_version = 21;
        const auto owner = Owner(f, true);
        const auto view = f.New("Landroid/widget/ScrollView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        f.On(view, "setBackgroundColor", "(I)V", {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        // Managed windows do not fabricate AttachInfo; explicitly request the draw barrier.
        const auto tick = [&] {
            f.On(view, "invalidate", "()V");
            static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                       AndroidUiTraversalPhase::ResolveOnly));
        };
        const auto pixels = [&] {
            return ui::RasterizeUiOverlay(ui::BuildUiRenderList(*f.context->ui_tree.PublishedProjection(), {}),
                                          {static_cast<std::int32_t>(f.context->surface_width),
                                           static_cast<std::int32_t>(f.context->surface_height)})
                .rgba8;
        };
        tick();
        const auto before = pixels();
        CHECK(f.ledger.Unimplemented().empty());
        CHECK(f.On(view, "awakenScrollBars", "()Z").AsInt() == 1);
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
        tick();
        CHECK(pixels() == before);
        auto hits = f.ledger.Unimplemented();
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().id == "dexvm.view_scrollbar_draw");
        const auto cache_field = f.linker.FindFieldRecursive(f.model.ObjectClass(view), "mScrollCache",
                                                             "Landroid/view/View$ScrollabilityCache;");
        REQUIRE(cache_field);
        const auto cache = VmObjectRef{f.model.InstanceSlots(view)[f.linker.Field(*cache_field).slot].bits};
        const auto state_field = f.linker.FindFieldRecursive(f.model.ObjectClass(cache), "state", "I");
        REQUIRE(state_field);
        const auto state = [&] { return f.model.InstanceSlots(cache)[f.linker.Field(*state_field).slot].bits; };
        CHECK(state() == 1);
        AdvanceAndroidClock(*f.context, 751);
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
        CHECK(state() == 2);
        tick();
        CHECK(pixels() == before);
        AdvanceAndroidClock(*f.context, 251);
        CHECK_FALSE(PumpJavaThreads(f.vm, *f.context));
        tick();
        CHECK(state() == 0);
        CHECK(pixels() == before);
    }
}

TEST_CASE("DVM-363 buttons load sealed classic and Holo skins and selectors keep ordered state") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (bool holo : {false, true}) {
            ContainerVm f(backend);
            const auto owner = Owner(f, true);
            const auto owner_root = f.vm.ProtectReferences(std::array{owner});
            f.On(owner, "setTheme", "(I)V",
                 {VmValue::Int(holo ? 0x01030128 : 0x01030007)});  // Theme.NoTitleBar.Fullscreen
            const auto button = f.New(holo ? "Landroid/widget/Button;" : "Landroid/widget/ImageButton;",
                                      "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
            const auto button_root = f.vm.ProtectReferences(std::array{button});
            const auto background = f.On(button, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
            REQUIRE(background.IsValid());
            CHECK(f.linker.Class(f.model.ObjectClass(background)).descriptor ==
                  "Landroid/graphics/drawable/StateListDrawable;");
            CHECK(f.On(background, "getStateCount", "()I").AsInt() > 1);
            for (const auto child : Children(f, background)) {
                CHECK(f.linker.Class(f.model.ObjectClass(child)).descriptor ==
                      "Landroid/graphics/drawable/NinePatchDrawable;");
                REQUIRE(f.On(SkinBitmap(f, child), "getNinePatchChunk", "()[B").ref.IsValid());
            }
            const auto selector = f.New("Landroid/graphics/drawable/StateListDrawable;");
            const auto selector_root = f.vm.ProtectReferences(std::array{selector});
            const auto first = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V", {VmValue::Int(0)});
            const auto first_root = f.vm.ProtectReferences(std::array{first});
            const auto second = f.New("Landroid/graphics/drawable/ColorDrawable;", "(I)V",
                                      {VmValue::Int(static_cast<std::int32_t>(0xff123456U))});
            const auto second_root = f.vm.ProtectReferences(std::array{second});
            const auto states =
                f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 3);
            const auto empty =
                f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 0);
            const auto arrays_root = f.vm.ProtectReferences(std::array{states, empty});
            f.model.SetPrimitiveElement(states, 0, 0x010100a7);  // state_pressed
            f.model.SetPrimitiveElement(states, 1, 0);           // API19 terminator ignores later values
            f.model.SetPrimitiveElement(states, 2, 0x0101009e);
            f.On(selector, "addState", "([ILandroid/graphics/drawable/Drawable;)V",
                 {VmValue::Ref(states), VmValue::Ref(first)});
            f.On(selector, "addState", "([ILandroid/graphics/drawable/Drawable;)V",
                 {VmValue::Ref(empty), VmValue::Ref(second)});
            f.On(selector, "addState", "([ILandroid/graphics/drawable/Drawable;)V",
                 {VmValue::Ref(empty), VmValue::Ref(VmObjectRef{})});
            CHECK(f.On(selector, "getStateCount", "()I").AsInt() == 2);
            CHECK(f.On(selector, "getStateSet", "(I)[I", {VmValue::Int(0)}).ref == states);
            CHECK(
                f.On(selector, "getStateDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0)}).ref ==
                first);
            CHECK(f.On(selector, "getStateDrawableIndex", "([I)I", {VmValue::Ref(states)}).AsInt() == 0);
            CHECK(f.On(selector, "getStateDrawableIndex", "([I)I", {VmValue::Ref(empty)}).AsInt() == 1);
            f.On(selector, "setState", "([I)Z", {VmValue::Ref(states)});
            CHECK(f.On(selector, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref == first);
            f.On(selector, "setState", "([I)Z", {VmValue::Ref(empty)});
            CHECK(f.On(selector, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref == second);
            f.model.SetPrimitiveElement(states, 0, static_cast<std::uint32_t>(-0x010100a7));
            CHECK(f.On(selector, "getStateDrawableIndex", "([I)I", {VmValue::Ref(empty)}).AsInt() == 0);
            f.On(button, "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(selector)});
            CHECK(f.On(button, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref == selector);
            f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(button)});
            f.On(button, "invalidate", "()V");
            REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                             AndroidUiTraversalPhase::ResolveOnly, ui::Size{100, 100}));
            const auto projection = f.context->ui_tree.PublishedProjection();
            const auto node = projection->Get(*FindViewUiNode(*f.context, button.Value()));
            CHECK(node->background_resource_id == 0);
            CHECK_FALSE(node->background_color.has_value());
            const auto gaps = f.ledger.Unimplemented();
            CHECK(std::any_of(gaps.begin(), gaps.end(),
                              [](const auto& gap) { return gap.id == "dexvm.view_background_draw"; }));
        }
    }
}

TEST_CASE("DrawableContainer forwards only the current child's callbacks to its owner") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f, true);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x01030007)});  // Theme.NoTitleBar.Fullscreen
        const auto button =
            f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto button_root = f.vm.ProtectReferences(std::array{button});
        const auto background = f.On(button, "getBackground", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(background.IsValid());
        const auto current = f.On(background, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(current.IsValid());
        VmObjectRef other{};
        for (const auto child : Children(f, background))
            if (child != current) other = child;
        REQUIRE(other.IsValid());

        // ImageButton.setVisibility -> DrawableContainer.setVisible -> child invalidateSelf.
        for (const auto visibility : {8, 0})
            CHECK_FALSE(f.OnOutcome(button, "setVisibility", "(I)V", {VmValue::Int(visibility)}).exception.IsValid());
        for (const auto child : {current, other}) {
            CHECK_FALSE(f.OnOutcome(background, "invalidateDrawable", "(Landroid/graphics/drawable/Drawable;)V",
                                    {VmValue::Ref(child)})
                            .exception.IsValid());
            CHECK_FALSE(f.OnOutcome(background, "unscheduleDrawable",
                                    "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;)V",
                                    {VmValue::Ref(child), VmValue::Ref(VmObjectRef{})})
                            .exception.IsValid());
        }
        // A non-current child is dropped; the current one reaches the ROM View.scheduleDrawable.
        constexpr auto schedule = "(Landroid/graphics/drawable/Drawable;Ljava/lang/Runnable;J)V";
        CHECK_FALSE(f.OnOutcome(background, "scheduleDrawable", schedule,
                                {VmValue::Ref(other), VmValue::Ref(VmObjectRef{}), VmValue::Long(0)})
                        .exception.IsValid());
        const auto forwarded = f.OnOutcome(background, "scheduleDrawable", schedule,
                                           {VmValue::Ref(current), VmValue::Ref(VmObjectRef{}), VmValue::Long(0)});
        CHECK_FALSE(forwarded.exception.IsValid());
        const auto gaps = f.ledger.Unimplemented();
        CHECK(std::none_of(gaps.begin(), gaps.end(),
                           [](const auto& gap) { return gap.id.find("DrawableContainer;.") != std::string::npos; }));
    }
}

TEST_CASE("DVM-366 LightingColorFilter retires software peers across the button press path") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        constexpr auto owner_type = "Landroid/graphics/LightingColorFilter;";
        const auto handles = [&](VmObjectRef filter) {
            const auto type = f.model.ObjectClass(filter);
            std::array<std::int32_t, 2> result{};
            for (std::size_t i = 0; i < 2; ++i) {
                const auto field =
                    f.linker.FindFieldRecursive(type, i == 0 ? "native_instance" : "nativeColorFilter", "I");
                REQUIRE(field);
                result[i] = static_cast<std::int32_t>(f.model.InstanceSlots(filter)[f.linker.Field(*field).slot].bits);
            }
            return result;
        };
        const auto view_of = [&](std::int32_t native, std::int32_t mul, std::int32_t add) {
            const auto method =
                f.linker.FindDirectMethod(f.linker.ResolveDescriptor(owner_type), "nCreateLightingFilter", "(III)I");
            REQUIRE(method);
            return f.vm.Call(*method, std::array{VmValue::Int(native), VmValue::Int(mul), VmValue::Int(add)});
        };
        const auto type = f.linker.ResolveDescriptor(owner_type);
        CHECK(f.linker.Class(type).is_boot_dex);
        CHECK(f.linker.IsAssignable(f.linker.ResolveDescriptor("Landroid/graphics/ColorFilter;"), type));

        // GTASA andButton ACTION_DOWN: new LightingColorFilter(-6710887, 0), then invalidate().
        const auto owner = Owner(f);
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto button =
            f.New("Landroid/widget/ImageButton;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto button_root = f.vm.ProtectReferences(std::array{button});
        std::array<std::int32_t, 2> first{};
        const auto other = f.New(owner_type, "(II)V", {VmValue::Int(0x00ff00), VmValue::Int(0x10)});
        const auto other_root = f.vm.ProtectReferences(std::array{other});
        const auto second = handles(other);
        {
            const auto pressed = f.New(owner_type, "(II)V", {VmValue::Int(-6710887), VmValue::Int(0)});
            const auto pressed_root = f.vm.ProtectReferences(std::array{pressed});
            f.On(button, "invalidate", "()V");
            first = handles(pressed);
        }
        CHECK(first[0] != 0);
        CHECK(first[1] == 0);
        CHECK(second[1] == 0);
        CHECK(first[0] != second[0]);

        // Software-only API19 post-create entry points return a null HWUI peer.
        CHECK(view_of(first[0], -6710887, 0).value.AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());

        // ACTION_UP drops the filter; ROM finalize retires both handles.
        static_cast<void>(f.vm.CollectGarbage("lighting-filter-release"));
        f.vm.RunFinalization();
        static_cast<void>(f.vm.CollectGarbage("lighting-filter-finalized"));
        const auto set_filter = f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Landroid/graphics/Paint;"),
                                                          "native_setColorFilter", "(II)I");
        REQUIRE(set_filter);
        const auto paint = f.New("Landroid/graphics/Paint;");
        const auto native_field = f.linker.FindFieldRecursive(f.model.ObjectClass(paint), "mNativePaint", "I");
        REQUIRE(native_field);
        const auto paint_token = VmValue::Int(
            static_cast<std::int32_t>(f.model.InstanceSlots(paint)[f.linker.Field(*native_field).slot].bits));
        CHECK(f.vm.Call(*set_filter, std::array{paint_token, VmValue::Int(first[0])}).exception.IsValid());
        CHECK_FALSE(f.vm.Call(*set_filter, std::array{paint_token, VmValue::Int(second[0])}).exception.IsValid());
    }
}

TEST_CASE("ROM background projection reads live selector Bitmap and NinePatch backing") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto view = f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, resources});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        for (const int id : {0x01080016, 0x01080006, 0x0108052a, 0x0108052b, 0x01080528, 0x01080529}) {
            const auto drawable =
                f.On(resources, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(id)}).ref;
            const auto root = f.vm.ProtectReferences(std::array{drawable});
            f.On(view, "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V", {VmValue::Ref(drawable)});
            for (int enabled : {1, 0}) {
                f.On(view, "setEnabled", "(Z)V", {VmValue::Int(enabled)});
                f.On(view, "refreshDrawableState", "()V");
                f.On(view, "invalidate", "()V");
                static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                           AndroidUiTraversalPhase::ResolveOnly));
                const auto current = f.On(drawable, "getCurrent", "()Landroid/graphics/drawable/Drawable;").ref;
                const auto bitmap = SkinBitmap(f, current);
                const auto& node = *f.context->ui_tree.Get(*FindViewUiNode(*f.context, view.Value()));
                REQUIRE(node.background_bitmap);
                const auto& snapshot = *node.background_bitmap;
                const auto& native = f.context->bitmaps.at(bitmap.Value());
                CHECK(snapshot.width == native.width);
                CHECK(snapshot.height == native.height);
                REQUIRE(snapshot.nine_patch);
                REQUIRE_FALSE(snapshot.stretch_x.empty());
                REQUIRE_FALSE(snapshot.stretch_y.empty());
                const auto argb = native.pixels->Snapshot();
                REQUIRE(snapshot.rgba8.size() == argb.size() * 4);
                for (std::size_t i = 0; i < argb.size(); ++i) {
                    CHECK(snapshot.rgba8[i * 4] == static_cast<std::uint8_t>(argb[i] >> 16));
                    CHECK(snapshot.rgba8[i * 4 + 1] == static_cast<std::uint8_t>(argb[i] >> 8));
                    CHECK(snapshot.rgba8[i * 4 + 2] == static_cast<std::uint8_t>(argb[i]));
                    CHECK(snapshot.rgba8[i * 4 + 3] == static_cast<std::uint8_t>(argb[i] >> 24));
                }
            }
        }
    }
}
