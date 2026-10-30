#include "android_value_fixture.h"
#include "container_traversal_fixture.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-365 ProgressBar ROM state measurement attachment and bounded resources") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend, true);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        auto& record = f.context->activity_stack.emplace_back();
        record.object = owner;
        record.visible = record.resumed = true;
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x01030007)});
        const auto view = f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        const auto slot = [&](VmObjectRef object, const char* name, const char* type) -> Slot& {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(field);
            f.vm.RequireImplementedField(*field);
            return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
        };
        for (const auto signature : {"(Landroid/content/Context;Landroid/util/AttributeSet;)V",
                                     "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                                     "(Landroid/content/Context;Landroid/util/AttributeSet;II)V"}) {
            std::vector args{VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})};
            const std::string_view sig = signature;
            if (sig.find(";I") != std::string_view::npos) args.push_back(VmValue::Int(0x01010077));
            if (sig.ends_with(";II)V")) args.push_back(VmValue::Int(0));
            const auto extra = f.New("Landroid/widget/ProgressBar;", signature, args);
            CHECK(f.On(extra, "isIndeterminate", "()Z").AsInt() == 1);
        }
        CHECK(slot(view, "mOnlyIndeterminate", "Z").bits == 1);
        CHECK(slot(view, "mDuration", "I").bits == 3500);
        CHECK(slot(view, "mBehavior", "I").bits == 1);
        for (const auto name : {"mMinWidth", "mMaxWidth", "mMinHeight", "mMaxHeight"})
            CHECK(slot(view, name, "I").bits == 48);
        const auto interpolator = f.On(view, "getInterpolator", "()Landroid/view/animation/Interpolator;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(interpolator)).descriptor ==
              "Landroid/view/animation/LinearInterpolator;");
        CHECK(f.On(view, "isIndeterminate", "()Z").AsInt() == 1);
        f.On(view, "setProgress", "(I)V", {VmValue::Int(5)});
        CHECK(f.On(view, "getProgress", "()I").AsInt() == 0);
        f.On(view, "refreshDrawableState", "()V");
        f.On(view, "setIndeterminate", "(Z)V", {VmValue::Int(0)});
        CHECK(f.On(view, "isIndeterminate", "()Z").AsInt() == 1);
        f.On(view, "setIndeterminate", "(Z)V", {VmValue::Int(1)});
        const auto drawable = f.On(view, "getIndeterminateDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(drawable.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(drawable)).descriptor ==
              "Landroid/graphics/drawable/AnimatedRotateDrawable;");
        CHECK(f.On(drawable, "getIntrinsicWidth", "()I").AsInt() == 48);
        CHECK(f.On(drawable, "getIntrinsicHeight", "()I").AsInt() == 48);
        CHECK(f.On(drawable, "isRunning", "()Z").AsInt() == 0);
        f.On(drawable, "setLevel", "(I)Z", {VmValue::Int(6000)});
        CHECK(f.On(drawable, "getLevel", "()I").AsInt() == 6000);
        const auto state = VmObjectRef{
            slot(drawable, "mState", "Landroid/graphics/drawable/AnimatedRotateDrawable$AnimatedRotateState;").bits};
        CHECK(slot(state, "mFramesCount", "I").bits == 12);
        CHECK(slot(state, "mFrameDuration", "I").bits == 100);
        CHECK(slot(state, "mPivotXRel", "Z").bits == 1);
        CHECK(slot(state, "mPivotYRel", "Z").bits == 1);
        const auto bitmap_drawable = f.On(drawable, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        const auto paint = f.On(bitmap_drawable, "getPaint", "()Landroid/graphics/Paint;").ref;
        CHECK((f.On(paint, "getFlags", "()I").AsInt() & 3) == 3);
        const auto resources = f.On(owner, "getResources", "()Landroid/content/res/Resources;").ref;
        const auto missing = f.OnOutcome(resources, "getAnimation", "(I)Landroid/content/res/XmlResourceParser;",
                                         {VmValue::Int(0x010a000a)});
        REQUIRE(missing.exception.IsValid());
        CHECK(f.linker.Class(missing.exception_class).descriptor ==
              "Landroid/content/res/Resources$NotFoundException;");
        const auto horizontal = f.vm.NewIntrinsicInstance("Landroid/widget/ProgressBar;");
        const auto horizontal_root = f.vm.ProtectReferences(std::array{horizontal});
        const auto ctor =
            f.linker.FindDirectMethod(f.linker.ResolveDescriptor("Landroid/widget/ProgressBar;"), "<init>",
                                      "(Landroid/content/Context;Landroid/util/AttributeSet;I)V");
        for (int style : {0x01010078, 0x01010079, 0x0101007a}) {
            const auto rejected = f.vm.Call(*ctor, std::array{VmValue::Ref(horizontal), VmValue::Ref(owner),
                                                              VmValue::Ref(VmObjectRef{}), VmValue::Int(style)});
            REQUIRE(rejected.exception.IsValid());
            CHECK(rejected.exception_message.find("drawable") != std::string::npos);
        }
        f.On(view, "setPadding", "(IIII)V", {VmValue::Int(2), VmValue::Int(3), VmValue::Int(4), VmValue::Int(5)});
        f.On(view, "measure", "(II)V",
             {VmValue::Int(static_cast<int>(0x80000100U)), VmValue::Int(static_cast<int>(0x80000100U))});
        CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == 54);
        CHECK(f.On(view, "getMeasuredHeight", "()I").AsInt() == 56);
        f.On(view, "measure", "(II)V", {VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
        CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == 100);
        CHECK(f.On(view, "getMeasuredHeight", "()I").AsInt() == 80);
        const auto container =
            f.New("Landroid/widget/AbsoluteLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto params = f.New("Landroid/widget/AbsoluteLayout$LayoutParams;", "(IIII)V",
                                  {VmValue::Int(-2), VmValue::Int(-2), VmValue::Int(12), VmValue::Int(20)});
        const auto container_root = f.vm.ProtectReferences(std::array{container, params});
        f.On(container, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(view), VmValue::Ref(params)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(container)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto node = FindViewUiNode(*f.context, view.Value());
        REQUIRE(node);
        const auto published = f.context->ui_tree.PublishedProjection();
        REQUIRE(published);
        REQUIRE(published->Get(*node));
        CHECK(published->Get(*node)->frame == ui::Rect{12, 20, 66, 76});
        const auto exact = f.New("Landroid/widget/AbsoluteLayout$LayoutParams;", "(IIII)V",
                                 {VmValue::Int(100), VmValue::Int(80), VmValue::Int(12), VmValue::Int(20)});
        f.On(view, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", {VmValue::Ref(exact)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        CHECK(f.context->ui_tree.PublishedProjection()->Get(*node)->frame == ui::Rect{12, 20, 112, 100});
        for (int i = 0; i < 2; ++i) {
            f.On(view, "invalidate", "()V");
            static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                       AndroidUiTraversalPhase::ResolveOnly));
        }
        unsigned draw_hits = 0;
        for (const auto& hit : f.ledger.Unimplemented())
            if (hit.id == "dexvm.progressbar_draw") draw_hits = static_cast<unsigned>(hit.count);
        CHECK(draw_hits == 1);
        f.On(view, "onAttachedToWindow", "()V");
        for (int visible : {4, 0, 8, 0})
            f.On(view, "setVisibility", "(I)V", {VmValue::Int(visible)});
        CHECK(f.On(drawable, "isRunning", "()Z").AsInt() == 0);
        f.On(view, "onDetachedFromWindow", "()V");
        CHECK(f.On(drawable, "isRunning", "()Z").AsInt() == 0);
        const auto animation = f.OnOutcome(drawable, "start", "()V");
        REQUIRE(animation.exception.IsValid());
        // Exercise the ROM determinate branch without admitting an unsealed horizontal style.
        slot(view, "mOnlyIndeterminate", "Z").bits = 0;
        f.On(view, "setIndeterminate", "(Z)V", {VmValue::Int(0)});
        f.On(view, "setMax", "(I)V", {VmValue::Int(10)});
        f.On(view, "setProgress", "(I)V", {VmValue::Int(14)});
        CHECK(f.On(view, "getMax", "()I").AsInt() == 10);
        CHECK(f.On(view, "getProgress", "()I").AsInt() == 10);
        f.On(view, "setProgress", "(I)V", {VmValue::Int(-1)});
        CHECK(f.On(view, "getProgress", "()I").AsInt() == 0);
        f.On(view, "incrementProgressBy", "(I)V", {VmValue::Int(3)});
        CHECK(f.On(view, "getProgress", "()I").AsInt() == 3);
        f.On(view, "setSecondaryProgress", "(I)V", {VmValue::Int(100)});
        CHECK(f.On(view, "getSecondaryProgress", "()I").AsInt() == 10);
        f.On(view, "onAttachedToWindow", "()V");
        const auto worker = f.New("Ltest/ProgressWorker;", "(Landroid/widget/ProgressBar;)V", {VmValue::Ref(view)});
        const auto thread = f.New("Ljava/lang/Thread;", "(Ljava/lang/Runnable;)V", {VmValue::Ref(worker)});
        const auto worker_roots = f.vm.ProtectReferences(std::array{worker, thread});
        f.On(thread, "start", "()V");
        f.On(thread, "join", "()V");
        CHECK(f.On(view, "getProgress", "()I").AsInt() == 7);
        CHECK(slot(view, "mRefreshIsPosted", "Z").bits == 1);
        CHECK(f.On(view, "removeCallbacks", "(Ljava/lang/Runnable;)Z", {VmValue::Ref(VmObjectRef{})}).AsInt() == 1);
        const auto refresh = VmObjectRef{slot(view, "mRefreshData", "Ljava/util/ArrayList;").bits};
        CHECK(f.On(refresh, "size", "()I").AsInt() == 1);
        REQUIRE_FALSE(PumpJavaThreads(f.vm, *f.context).has_value());
        CHECK(slot(view, "mRefreshIsPosted", "Z").bits == 0);
        CHECK(f.On(refresh, "size", "()I").AsInt() == 0);
        f.On(view, "onDetachedFromWindow", "()V");
        f.On(view, "setMax", "(I)V", {VmValue::Int(-2)});
        CHECK(f.On(view, "getMax", "()I").AsInt() == 0);
    }
}

TEST_CASE("DVM-365 unsupported drawable construction and tiling seams fail explicitly") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend, true);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/ProgressBar;");
        const auto tile = f.linker.FindDirectMethod(
            type, "tileify", "(Landroid/graphics/drawable/Drawable;Z)Landroid/graphics/drawable/Drawable;");
        REQUIRE(tile);
        const auto tiled =
            f.vm.Call(*tile, std::array{VmValue::Ref(view), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        REQUIRE(tiled.exception.IsValid());
        CHECK(tiled.exception_message == "ProgressBar tiled drawable is unsupported");
        const auto xml = f.linker.FindDirectMethod(
            f.linker.ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "createFromXmlInner",
            "(Landroid/content/res/Resources;Lorg/xmlpull/v1/XmlPullParser;Landroid/util/AttributeSet;)Landroid/graphics/drawable/Drawable;");
        REQUIRE(xml);
        const auto nested = f.vm.Call(
            *xml, std::array{VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(nested.exception.IsValid());
        CHECK(nested.exception_message == "nested drawable XML is unsupported");
        const auto animation = f.vm.NewIntrinsicInstance("Landroid/graphics/drawable/AnimationDrawable;");
        const auto animation_root = f.vm.ProtectReferences(std::array{animation});
        const auto ctor = f.linker.FindDirectMethod(
            f.linker.ResolveDescriptor("Landroid/graphics/drawable/AnimationDrawable;"), "<init>", "()V");
        REQUIRE(ctor);
        const auto rejected = f.vm.Call(*ctor, std::array{VmValue::Ref(animation)});
        REQUIRE(rejected.exception.IsValid());
        CHECK(rejected.exception_message == "frame animation drawable is unsupported");
        const auto one = f.OnOutcome(animation, "setOneShot", "(Z)V", {VmValue::Int(1)});
        REQUIRE(one.exception.IsValid());
        CHECK(one.exception_message == "frame animation drawable is unsupported");
    }
}
