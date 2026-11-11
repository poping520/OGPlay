#include "dexvm/resource_fixture.h"
#include "container_traversal_fixture.h"
#include <regex>
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
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
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
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-277 View constructor initializes ROM SDK compatibility flags once") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        for (const auto sdk : {17U, 18U, 19U}) {
            AndroidValueVm fixture(backend);
            fixture.context->target_sdk_version = sdk;
            const auto owner = fixture.New("Landroid/app/Activity;");
            static_cast<void>(
                fixture.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)}));
            const auto type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
            const auto flag = [&](const char* name) {
                const auto field = fixture.linker.FindFieldRecursive(type, name, "Z");
                REQUIRE(field.has_value());
                CHECK_FALSE(fixture.linker.Field(*field).is_unimplemented);
                return fixture.linker.Class(type).static_storage[fixture.linker.Field(*field).slot];
            };
            CHECK(flag("sCompatibilityDone") == 1);
            CHECK(flag("sUseBrokenMakeMeasureSpec") == (sdk <= 17U));
            CHECK(flag("sIgnoreMeasureCache") == (sdk < 19U));
            fixture.context->target_sdk_version = 1;
            static_cast<void>(
                fixture.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)}));
            CHECK(flag("sUseBrokenMakeMeasureSpec") == (sdk <= 17U));
            CHECK(flag("sIgnoreMeasureCache") == (sdk < 19U));
        }
    }
}

TEST_CASE("DVM-138 View.getParent follows the ROM ViewGroup hierarchy") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/content/Context;");
        const auto first_parent =
            f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto second_parent =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});

        const auto view_parent = f.linker.ResolveDescriptor("Landroid/view/ViewParent;");
        CHECK(f.linker.Class(view_parent).is_interface);
        CHECK(f.linker.IsAssignable(view_parent, f.model.ObjectClass(first_parent)));
        const auto method =
            f.linker.FindVtableIndex(f.model.ObjectClass(child), "getParent", "()Landroid/view/ViewParent;");
        REQUIRE(method.has_value());
        CHECK((f.linker.Method(f.linker.Class(f.model.ObjectClass(child)).vtable[*method]).access_flags &
               (kAccPublic | kAccFinal)) == (kAccPublic | kAccFinal));

        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(first_parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == first_parent);
        f.On(first_parent, "removeView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        f.On(second_parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(child)});
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == second_parent);
        f.On(second_parent, "removeViews", "(II)V", {VmValue::Int(0), VmValue::Int(1)});
        CHECK_FALSE(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
    }
}

TEST_CASE("DVM-167 ViewGroup width-height add uses virtual default params") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto relative =
            f.New("Landroid/widget/RelativeLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        const auto child = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});

        f.On(relative, "addView", "(Landroid/view/View;II)V",
             {VmValue::Ref(child), VmValue::Int(-1), VmValue::Int(-1)});
        const auto params = f.On(child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        REQUIRE(params.IsValid());
        CHECK(f.model.ObjectClass(params) ==
              f.linker.ResolveDescriptor("Landroid/widget/RelativeLayout$LayoutParams;"));
        const auto field = [&](const char* name) -> std::int32_t {
            const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(params), name, "I");
            REQUIRE(id.has_value());
            return static_cast<std::int32_t>(f.model.InstanceSlots(params)[f.linker.Field(*id).slot].bits);
        };
        CHECK(field("width") == -1);
        CHECK(field("height") == -1);
        CHECK(f.On(child, "getParent", "()Landroid/view/ViewParent;").ref == relative);
    }
}

TEST_CASE("DVM-174 AnimationListener type compatibility links and dispatches") {
    constexpr auto kListener = "Landroid/view/animation/Animation$AnimationListener;";
    constexpr auto kAnimation = "Landroid/view/animation/Animation;";
    constexpr auto kSignature = "(Landroid/view/animation/Animation;)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        std::int32_t starts{};
        std::int32_t ends{};
        std::int32_t repeats{};
        auto implementor = IntrinsicClassBuilder::Class("Ltest/AnimationListener;", "Ljava/lang/Object;", {kListener});
        implementor.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        implementor.VirtualMethod("onAnimationStart", kSignature, [&starts](IntrinsicContext&) {
            ++starts;
            return VmValue::Void();
        });
        implementor.VirtualMethod("onAnimationEnd", kSignature, [&ends](IntrinsicContext&) {
            ++ends;
            return VmValue::Void();
        });
        implementor.VirtualMethod("onAnimationRepeat", kSignature, [&repeats](IntrinsicContext&) {
            ++repeats;
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(implementor).Build()});

        const auto listener = fixture.linker.ResolveDescriptor(kListener);
        const auto& listener_class = fixture.linker.Class(listener);
        CHECK(listener_class.is_boot_dex);
        CHECK(listener_class.is_interface);
        CHECK(listener_class.access_flags == (kAccPublic | kAccInterface | kAccAbstract));
        const auto animation_type = fixture.linker.FindClass(kAnimation);
        REQUIRE(animation_type.has_value());
        CHECK(fixture.linker.Class(*animation_type).is_boot_dex);
        CHECK((fixture.linker.Class(*animation_type).access_flags & kAccAbstract) != 0);
        CHECK(fixture.linker.IsAssignable(fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), *animation_type));

        std::vector<std::string> declared;
        for (const auto method : listener_class.own_virtual_methods) {
            const auto& linked = fixture.linker.Method(method);
            declared.push_back(linked.name);
            CHECK(linked.descriptor == kSignature);
            CHECK(linked.kind == MethodKind::abstract);
            CHECK(linked.declared_invoke_kind == DeclaredInvokeKind::interface_call);
            CHECK((linked.access_flags & (kAccPublic | kAccAbstract)) == (kAccPublic | kAccAbstract));
        }
        CHECK(declared == std::vector<std::string>{"onAnimationEnd", "onAnimationRepeat", "onAnimationStart"});

        const auto object = fixture.New("Ltest/AnimationListener;");
        const auto object_class = fixture.model.ObjectClass(object);
        CHECK(fixture.linker.IsAssignable(listener, object_class));
        CHECK_FALSE(fixture.linker.IsAssignable(object_class, listener));

        const auto animation = VmValue::Ref(VmObjectRef{});
        fixture.On(object, "onAnimationStart", kSignature, {animation});
        fixture.On(object, "onAnimationEnd", kSignature, {animation});
        fixture.On(object, "onAnimationRepeat", kSignature, {animation});
        CHECK(starts == 1);
        CHECK(ends == 1);
        CHECK(repeats == 1);
    }
}

TEST_CASE("DVM-184 View.getContext preserves constructor and inflation identity") {
    constexpr auto kGetContext = "getContext";
    constexpr auto kContextSig = "()Landroid/content/Context;";
    constexpr auto kViewInit = "(Landroid/content/Context;)V";
    constexpr auto kTwoArg = "(Landroid/content/Context;Landroid/util/AttributeSet;)V";
    constexpr auto kStyleInit = "(Landroid/content/Context;Landroid/util/AttributeSet;I)V";
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto subclass = IntrinsicClassBuilder::Class("Ltest/BurstlyView;", "Landroid/widget/LinearLayout;");
        subclass.Constructor(kViewInit, [](IntrinsicContext& call) {
            const auto type = call.vm.Linker().ResolveDescriptor("Landroid/widget/LinearLayout;");
            const auto constructor = call.vm.Linker().FindDirectMethod(type, "<init>", kViewInit);
            const std::array arguments{VmValue::Ref(call.receiver), call.arguments[0]};
            const auto outcome = call.vm.Call(*constructor, arguments);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{call.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                                  outcome.exception};
            }
            return VmValue::Void();
        });
        AndroidValueVm fixture(backend, {std::move(subclass).Build()});
        const auto view_type = fixture.linker.ResolveDescriptor("Landroid/view/View;");
        const auto subclass_type = fixture.linker.ResolveDescriptor("Ltest/BurstlyView;");
        const auto view_slot = fixture.linker.FindVtableIndex(view_type, kGetContext, kContextSig);
        const auto subclass_slot = fixture.linker.FindVtableIndex(subclass_type, kGetContext, kContextSig);
        REQUIRE(view_slot.has_value());
        REQUIRE(subclass_slot.has_value());
        CHECK(*view_slot == *subclass_slot);
        const auto& inherited = fixture.linker.Method(fixture.linker.Class(subclass_type).vtable[*subclass_slot]);
        CHECK(inherited.owner == view_type);
        CHECK_FALSE(inherited.overridable);
        CHECK((inherited.access_flags & kAccFinal) != 0);

        bool found_context_field = false;
        for (const auto field : fixture.linker.Class(view_type).own_instance_fields) {
            const auto& linked = fixture.linker.Field(field);
            if (linked.name != "mContext") continue;
            found_context_field = true;
            CHECK(linked.descriptor == "Landroid/content/Context;");
            CHECK((linked.access_flags & kAccProtected) != 0);
        }
        CHECK(found_context_field);

        const auto activity = fixture.New("Landroid/app/Activity;");
        const auto wrapper = fixture.New("Landroid/view/ContextThemeWrapper;");
        const auto other = fixture.New("Landroid/content/Context;");
        REQUIRE(activity != wrapper);
        REQUIRE(activity != other);

        const auto view = fixture.New("Landroid/view/View;", kViewInit, {VmValue::Ref(activity)});
        const auto layout =
            fixture.New("Landroid/widget/LinearLayout;", kTwoArg, {VmValue::Ref(wrapper), VmValue::Ref(VmObjectRef{})});
        const auto button = fixture.New("Landroid/widget/Button;", kStyleInit,
                                        {VmValue::Ref(other), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        const auto burstly = fixture.New("Ltest/BurstlyView;", kViewInit, {VmValue::Ref(activity)});
        CHECK(fixture.On(view, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(layout, kGetContext, kContextSig).ref == wrapper);
        CHECK(fixture.On(button, kGetContext, kContextSig).ref == other);
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
        CHECK(fixture.On(view, kGetContext, kContextSig).ref != fixture.On(layout, kGetContext, kContextSig).ref);

        const auto roots = fixture.vm.ProtectReferences(std::array{burstly});
        static_cast<void>(fixture.vm.CollectGarbage("dvm184-view-context"));
        CHECK(fixture.On(burstly, kGetContext, kContextSig).ref == activity);
    }
}

TEST_CASE("DVM-371 View scrollbar style round-trips and animation state stays inert") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto web = f.New("Landroid/webkit/WebView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto parent =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{view, web, parent});
        for (const auto target : {view, web})
            for (const int style : {0x01000000, 0x02000000, 0x03000000, 0}) {
                f.On(target, "setScrollBarStyle", "(I)V", {VmValue::Int(style)});
                CHECK(f.On(target, "getScrollBarStyle", "()I").AsInt() == style);
            }
        f.On(parent, "addView", "(Landroid/view/View;)V", {VmValue::Ref(web)});
        const auto animation_type = "Landroid/view/animation/Animation;";
        const auto animation =
            f.New("Landroid/view/animation/AlphaAnimation;", "(FF)V", {VmValue::Float(0.0F), VmValue::Float(1.0F)});
        const auto interpolator = f.New("Landroid/view/animation/LinearInterpolator;");
        const auto animation_roots = f.vm.ProtectReferences(std::array{animation, interpolator});
        f.On(animation, "setDuration", "(J)V", {VmValue::Long(500)});
        f.On(animation, "setInterpolator", "(Landroid/view/animation/Interpolator;)V", {VmValue::Ref(interpolator)});
        const auto get_animation = [&] {
            return f.On(web, "getAnimation", (std::string("()") + animation_type).c_str()).ref;
        };
        f.On(web, "clearAnimation", "()V");
        CHECK_FALSE(get_animation().IsValid());
        f.On(web, "startAnimation", (std::string("(") + animation_type + ")V").c_str(), {VmValue::Ref(animation)});
        CHECK(get_animation() == animation);
        f.On(web, "setVisibility", "(I)V", {VmValue::Int(0)});
        f.On(web, "invalidate", "()V");
        f.On(web, "clearAnimation", "()V");
        CHECK_FALSE(get_animation().IsValid());
        f.On(web, "setVisibility", "(I)V", {VmValue::Int(8)});
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-372 display-only setters warn and keep running while getters stay honest") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto paint = f.New("Landroid/graphics/Paint;");
        const auto roots = f.vm.ProtectReferences(std::array{view, text, paint});
        const auto warned = [&](std::string_view item) {
            const auto warnings = f.ledger.Warnings();
            return std::ranges::any_of(warnings, [&](const auto& hit) {
                return hit.id == std::string(ogplay::core::kUiDegradedWarningPrefix) + std::string(item);
            });
        };
        // ROM View keeps transform state.
        f.On(view, "setRotation", "(F)V", {VmValue::Float(45.0F)});
        CHECK(f.On(view, "getRotation", "()F").AsFloat() == 45.0F);
        // ROM Paint stores color and shadow state.
        f.On(paint, "setColor", "(I)V", {VmValue::Int(0x11223344)});
        CHECK(f.On(paint, "getColor", "()I").AsInt() == 0x11223344);
        f.On(paint, "setShadowLayer", "(FFFI)V",
             {VmValue::Float(2), VmValue::Float(1), VmValue::Float(1), VmValue::Int(0x11223344)});
        CHECK_FALSE(warned("Paint.shadowLayer"));
        // State pairs execute ROM.
        const auto tag = f.vm.NewStringUtf8("tag");
        const auto tag_root = f.vm.ProtectReferences(std::array{tag});
        f.On(view, "setTag", "(Ljava/lang/Object;)V", {VmValue::Ref(tag)});
        CHECK(f.On(view, "getTag", "()Ljava/lang/Object;").ref == tag);
        f.On(view, "setTag", "(ILjava/lang/Object;)V", {VmValue::Int(0x7f010001), VmValue::Ref(tag)});
        CHECK(f.On(view, "getTag", "(I)Ljava/lang/Object;", {VmValue::Int(0x7f010001)}).ref == tag);
        for (const bool enabled : {false, true}) {
            f.On(view, "setVerticalScrollBarEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isVerticalScrollBarEnabled", "()Z").AsInt() == enabled);
            f.On(text, "setEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(text, "isEnabled", "()Z").AsInt() == enabled);
        }
        CHECK_FALSE(warned("TextView.bufferType"));
        const auto shown = [&] {
            const auto value = f.On(text, "getText", "()Ljava/lang/CharSequence;").ref;
            return f.vm.StringUtf8(f.On(value, "toString", "()Ljava/lang/String;").ref);
        };
        const auto buffer = [&](const char* name) {
            return f
                .Static("Landroid/widget/TextView$BufferType;", "valueOf",
                        "(Ljava/lang/String;)Landroid/widget/TextView$BufferType;",
                        {VmValue::Ref(f.vm.NewStringUtf8(name))})
                .ref;
        };
        const auto set_typed = "(Ljava/lang/CharSequence;Landroid/widget/TextView$BufferType;)V";
        const auto normal = buffer("NORMAL");
        f.On(text, "setText", set_typed, {VmValue::Ref(f.vm.NewStringUtf8("plain")), VmValue::Ref(normal)});
        CHECK(shown() == "plain");
        CHECK_FALSE(warned("TextView.bufferType"));
        const auto spannable = buffer("SPANNABLE");
        f.On(text, "setText", set_typed, {VmValue::Ref(f.vm.NewStringUtf8("spans")), VmValue::Ref(spannable)});
        CHECK(shown() == "spans");
        CHECK_FALSE(warned("TextView.bufferType"));
        f.On(text, "setTextKeepState", "(Ljava/lang/CharSequence;)V", {VmValue::Ref(f.vm.NewStringUtf8("kept"))});
        CHECK(shown() == "kept");
        // The standalone Matrix also retains its native transform.
        const auto matrix = f.New("Landroid/graphics/Matrix;");
        f.On(matrix, "setScale", "(FF)V", {VmValue::Float(2.0F), VmValue::Float(2.0F)});
        CHECK(f.On(matrix, "isIdentity", "()Z").AsInt() == 0);
    }
}

TEST_CASE("DVM-363 View flags focus ids and ROM toString resolve actual resource identities") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ogplay::test::container_traversal::ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto owner_root = f.vm.ProtectReferences(std::array{owner});
        const auto view = f.New("Landroid/view/View;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto root = f.vm.ProtectReferences(std::array{view});
        for (const auto direction : {"Left", "Up", "Down", "Right"}) {
            const auto setter = std::string("setNextFocus") + direction + "Id";
            const auto getter = std::string("getNextFocus") + direction + "Id";
            for (int id : {123, -1, 456}) {
                f.On(view, setter.c_str(), "(I)V", {VmValue::Int(id)});
                CHECK(f.On(view, getter.c_str(), "()I").AsInt() == id);
            }
        }
        for (bool enabled : {false, true, false}) {
            f.On(view, "setSoundEffectsEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isSoundEffectsEnabled", "()Z").AsInt() == enabled);
            f.On(view, "setHorizontalScrollBarEnabled", "(Z)V", {VmValue::Int(enabled)});
            CHECK(f.On(view, "isHorizontalScrollBarEnabled", "()Z").AsInt() == enabled);
        }
        f.On(view, "layout", "(IIII)V", {VmValue::Int(1), VmValue::Int(2), VmValue::Int(30), VmValue::Int(40)});
        const auto identity =
            f.Static("Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", {VmValue::Ref(view)});
        const auto hex = f.model.StringValue(
            f.Static("Ljava/lang/Integer;", "toHexString", "(I)Ljava/lang/String;", {identity}).ref);
        const auto inspect = [&] { return f.vm.StringUtf8(f.On(view, "toString", "()Ljava/lang/String;").ref); };
        const auto plain = inspect();
        CHECK(plain.starts_with("android.view.View{" + f.vm.StringUtf8(f.model.NewString(hex)) + " "));
        CHECK(std::regex_match(
            plain,
            std::regex(
                R"(android\.view\.View\{[0-9a-f]+ [VIG.][F.][E.][D.][H.][V.][C.][L.] [R.][F.][S.][pP.][H.][A.][I.][D.] 1,2-30,40\})")));
        ogplay::test::ResourceFixture(*f.context).package_id = 0x7f;
        ogplay::test::ResourceFixture(*f.context).package_name = "fixture";
        ogplay::test::ResourceFixtureEntry entry;
        entry.resource_id = 0x7f0a0042;
        entry.type_name = "id";
        entry.entry_name = "player";
        ogplay::test::ResourceFixture(*f.context).entries.push_back(entry);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        for (const auto [id, suffix] :
             {std::pair{0x7f0a0042, " #7f0a0042 app:id/player}"},
              std::pair{0x01020002, " #1020002 android:id/content}"}, std::pair{0x7f0a0999, " #7f0a0999}"},
              std::pair{0x0102ffff, " #102ffff}"}, std::pair{0, " #0}"}}) {
            f.On(view, "setId", "(I)V", {VmValue::Int(id)});
            CHECK(inspect().ends_with(suffix));
        }
        CHECK(f.vm.StringUtf8(
                  f.On(resources, "getResourcePackageName", "(I)Ljava/lang/String;", {VmValue::Int(0x7f0a0042)}).ref) ==
              "fixture");
        CHECK(f.vm.StringUtf8(
                  f.On(resources, "getResourcePackageName", "(I)Ljava/lang/String;", {VmValue::Int(0x01020002)}).ref) ==
              "android");
        for (const auto name : {"getResourcePackageName", "getResourceTypeName", "getResourceEntryName"}) {
            const auto missing = f.OnOutcome(resources, name, "(I)Ljava/lang/String;", {VmValue::Int(0x0102ffff)});
            REQUIRE(missing.exception.IsValid());
            CHECK(f.linker.Class(missing.exception_class).descriptor ==
                  "Landroid/content/res/Resources$NotFoundException;");
        }
        CHECK(f.ledger.Unimplemented().empty());
    }
}
