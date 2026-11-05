#include "android_value_fixture.h"
#include "container_traversal_fixture.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-365 ProgressBar ROM state measurement attachment and bounded resources") {
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
        const auto rejected = f.vm.Call(*ctor, std::array{VmValue::Ref(horizontal), VmValue::Ref(owner),
                                                          VmValue::Ref(VmObjectRef{}), VmValue::Int(0x0101020f)});
        REQUIRE(rejected.exception.IsValid());
        CHECK_FALSE(rejected.exception_message.empty());
        const auto small =
            f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x01010079)});
        REQUIRE(f.On(small, "getIndeterminateDrawable", "()Landroid/graphics/drawable/Drawable;").ref.IsValid());
        const auto large =
            f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x0101007a)});
        const auto large_root = f.vm.ProtectReferences(std::array{large});
        for (const auto name : {"mMinWidth", "mMaxWidth", "mMinHeight", "mMaxHeight"})
            CHECK(slot(large, name, "I").bits == 76);
        const auto large_drawable =
            f.On(large, "getIndeterminateDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(large_drawable.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(large_drawable)).descriptor ==
              "Landroid/graphics/drawable/AnimatedRotateDrawable;");
        CHECK(f.On(large_drawable, "getIntrinsicWidth", "()I").AsInt() == 72);
        CHECK(f.On(large_drawable, "getIntrinsicHeight", "()I").AsInt() == 72);
        CHECK(f.On(large_drawable, "isRunning", "()Z").AsInt() == 0);
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
        f.On(drawable, "start", "()V");  // No animator: ignored with a warning, still not running.
        CHECK(f.On(drawable, "isRunning", "()Z").AsInt() == 0);
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

TEST_CASE("DVM-365 arbitrary nested drawable XML remains rejected") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        const auto xml = f.linker.FindDirectMethod(
            f.linker.ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "createFromXmlInner",
            "(Landroid/content/res/Resources;Lorg/xmlpull/v1/XmlPullParser;Landroid/util/AttributeSet;)Landroid/graphics/drawable/Drawable;");
        REQUIRE(xml);
        const auto nested = f.vm.Call(
            *xml, std::array{VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(nested.exception.IsValid());
        CHECK(nested.exception_message == "nested drawable XML is unsupported");
    }
}

TEST_CASE("DVM-367 horizontal ROM ProgressBar retains layer IDs levels and static animation frames") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        ogplay::test::AttachViewTestContext(f.vm, owner);
        f.context->activity = owner;
        auto& record = f.context->activity_stack.emplace_back();
        record.object = owner;
        record.visible = record.resumed = true;
        f.On(owner, "setTheme", "(I)V", {VmValue::Int(0x01030128)});
        const auto bar =
            f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                  {VmValue::Ref(owner), VmValue::Ref(VmObjectRef{}), VmValue::Int(0x01010078)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, bar});
        CHECK(f.On(bar, "isIndeterminate", "()Z").AsInt() == 0);
        const auto layers = f.On(bar, "getProgressDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(f.linker.Class(f.model.ObjectClass(layers)).descriptor == "Landroid/graphics/drawable/LayerDrawable;");
        CHECK(f.On(layers, "getNumberOfLayers", "()I").AsInt() == 3);
        const std::array ids{0x01020000, 0x0102000f, 0x0102000d};
        for (int i = 0; i < 3; ++i)
            CHECK(f.On(layers, "getId", "(I)I", {VmValue::Int(i)}).AsInt() == ids[static_cast<std::size_t>(i)]);
        f.On(bar, "setMax", "(I)V", {VmValue::Int(200)});
        f.On(bar, "setProgress", "(I)V", {VmValue::Int(50)});
        f.On(bar, "setSecondaryProgress", "(I)V", {VmValue::Int(120)});
        for (int i : {1, 2}) {
            const auto scale =
                f.On(layers, "getDrawable", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(i)}).ref;
            CHECK(f.linker.Class(f.model.ObjectClass(scale)).descriptor == "Landroid/graphics/drawable/ScaleDrawable;");
            CHECK(f.On(scale, "getLevel", "()I").AsInt() == (i == 1 ? 6000 : 2500));
            const auto patch = f.On(scale, "getDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
            CHECK(f.linker.Class(f.model.ObjectClass(patch)).descriptor ==
                  "Landroid/graphics/drawable/NinePatchDrawable;");
            CHECK(f.On(patch, "getLevel", "()I").AsInt() == (i == 1 ? 6000 : 2500));
        }
        f.On(bar, "measure", "(II)V",
             {VmValue::Int(static_cast<int>(0x80000100U)), VmValue::Int(static_cast<int>(0x80000100U))});
        CHECK(f.On(bar, "getMeasuredHeight", "()I").AsInt() == 16);
        const auto animation = f.On(bar, "getIndeterminateDrawable", "()Landroid/graphics/drawable/Drawable;").ref;
        REQUIRE(f.linker.Class(f.model.ObjectClass(animation)).descriptor ==
                "Landroid/graphics/drawable/AnimationDrawable;");
        CHECK(f.On(animation, "getNumberOfFrames", "()I").AsInt() == 8);
        CHECK(f.On(animation, "getIntrinsicWidth", "()I").AsInt() == 0);
        CHECK(f.On(animation, "getIntrinsicHeight", "()I").AsInt() == 0);
        CHECK(f.On(animation, "getMinimumWidth", "()I").AsInt() == 0);
        CHECK(f.On(animation, "getMinimumHeight", "()I").AsInt() == 0);
        const auto padding = f.New("Landroid/graphics/Rect;");
        CHECK(f.On(animation, "getPadding", "(Landroid/graphics/Rect;)Z", {VmValue::Ref(padding)}).AsInt() == 0);
        CHECK(f.On(animation, "isOneShot", "()Z").AsInt() == 0);
        CHECK(f.On(animation, "isRunning", "()Z").AsInt() == 0);
        for (int i = 0; i < 8; ++i) {
            CHECK(f.On(animation, "getDuration", "(I)I", {VmValue::Int(i)}).AsInt() == 50);
            const auto clip =
                f.On(animation, "getFrame", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(i)}).ref;
            CHECK(f.linker.Class(f.model.ObjectClass(clip)).descriptor == "Landroid/graphics/drawable/ClipDrawable;");
            CHECK(f.On(clip, "getLevel", "()I").AsInt() == 10000);
        }
        const auto read_ref = [&](VmObjectRef object, const char* name, const char* descriptor) {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, descriptor);
            REQUIRE(field);
            return VmObjectRef{f.model.InstanceSlots(object)[f.linker.Field(*field).slot].bits};
        };
        const auto clip = f.On(animation, "getFrame", "(I)Landroid/graphics/drawable/Drawable;", {VmValue::Int(0)}).ref;
        const auto clip_state = read_ref(clip, "mClipState", "Landroid/graphics/drawable/ClipDrawable$ClipState;");
        const auto shape = read_ref(clip_state, "mDrawable", "Landroid/graphics/drawable/Drawable;");
        const auto paint = f.On(shape, "getPaint", "()Landroid/graphics/Paint;").ref;
        const auto shader = f.On(paint, "getShader", "()Landroid/graphics/Shader;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(shader)).descriptor == "Landroid/graphics/BitmapShader;");
        const auto bitmap = read_ref(shader, "mBitmap", "Landroid/graphics/Bitmap;");
        REQUIRE(f.context->bitmaps.contains(bitmap.Value()));
        CHECK(f.context->bitmaps.at(bitmap.Value()).width > 0);
        CHECK(f.On(paint, "getAlpha", "()I").AsInt() == 255);
        f.On(paint, "setAlpha", "(I)V", {VmValue::Int(128)});
        const auto copy = f.New("Landroid/graphics/Paint;", "(Landroid/graphics/Paint;)V", {VmValue::Ref(paint)});
        const auto copy_root = f.vm.ProtectReferences(std::array{copy});
        CHECK(f.On(copy, "getAlpha", "()I").AsInt() == 128);
        CHECK(f.On(copy, "getShader", "()Landroid/graphics/Shader;").ref == shader);
        f.On(copy, "setAlpha", "(I)V", {VmValue::Int(7)});
        CHECK(f.On(paint, "getAlpha", "()I").AsInt() == 128);
        f.On(paint, "setAlpha", "(I)V", {VmValue::Int(255)});
        f.On(animation, "start", "()V");
        CHECK(f.On(animation, "isRunning", "()Z").AsInt() == 0);
        f.On(bar, "setIndeterminate", "(Z)V", {VmValue::Int(1)});
        CHECK(f.On(bar, "isIndeterminate", "()Z").AsInt() == 1);
        f.On(bar, "setIndeterminate", "(Z)V", {VmValue::Int(0)});
        CHECK(f.On(bar, "getProgress", "()I").AsInt() == 50);
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(bar)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly));
        const auto round_shape = f.On(shape, "getShape", "()Landroid/graphics/drawable/shapes/Shape;").ref;
        const auto path = read_ref(round_shape, "mPath", "Landroid/graphics/Path;");
        const auto& commands = f.context->paths.at(path.Value()).commands;
        REQUIRE(commands.size() == 1);
        CHECK(commands[0].verb == DexVmAndroidContext::PathState::Verb::round_rect);
        CHECK(commands[0].radii == std::array<float, 8>{5, 5, 5, 5, 5, 5, 5, 5});
        static_cast<void>(f.vm.CollectGarbage("horizontal-drawable-tree"));
        CHECK(f.model.IsValidRef(bitmap));
        CHECK(f.model.IsValidRef(shader));
        bool rejected_content = false;
        for (const auto& hit : f.ledger.Unimplemented())
            if (hit.id == "dexvm.progressbar_draw") rejected_content = true;
        CHECK(rejected_content);
        for (const auto& hit : f.ledger.Unimplemented())
            CHECK(hit.id != "dexvm.progressbar_tiling");
    }
}
