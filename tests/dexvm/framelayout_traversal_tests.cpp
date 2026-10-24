#include "../runtime/native_library_fixture.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include <cstring>
#include "boot_dex.h"
#include <doctest/doctest.h>
#include <array>
#include <fstream>
#include <string>
#include <vector>

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

namespace {
// Real pinned ICU/JNI is required by Rect.<clinit> in the original FrameLayout constructor.
// Create initializes the process only; no Activity lifecycle, APK, EGL or display is started.
struct FrameVm {
    VirtualFileSystem filesystem;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>(AndroidUiTraversalOwner::JavaTraversal)};
    std::unique_ptr<ogplay::session::AndroidAppProcess> app;
    Interpreter& vm;
    DexClassLinker& linker;
    JavaObjectModel& model;

    std::unique_ptr<ogplay::session::AndroidAppProcess> Create(InterpreterBackend backend) {
        using namespace ogplay::test::native_library;
        std::vector<std::vector<std::byte>> bytes;
        std::vector<BionicModuleSource> sources;
        for (const auto name : SelectBionicProfile(19).guest_libraries) {
            bytes.push_back(ReadPayloadBytes("lib/" + std::string(name)));
            sources.push_back({std::string(name), bytes.back()});
        }
        std::ifstream stream(OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar", std::ios::binary);
        const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
        context->boot_classpath_bytes.resize(raw.size());
        std::memcpy(context->boot_classpath_bytes.data(), raw.data(), raw.size());
        const auto dex =
            ogplay::loader::ReadApkEntry(context->boot_classpath_bytes,
                                         ogplay::loader::ParseApkArchive(context->boot_classpath_bytes), "classes.dex");
        ogplay::session::AndroidAppProcessRequest request;
        request.manifest = AppManifest("fixture.MainActivity");
        request.system_libraries = sources;
        request.dex_bytes = ReadDexFixture("view_measure.dex");
        request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
        request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
        request.boot_dex_bytes.resize(dex.size());
        std::memcpy(request.boot_dex_bytes.data(), dex.data(), dex.size());
        request.context = context;
        request.dexvm.interpreter.backend = backend;
        request.dexvm.interpreter.diagnostics.trace_capacity = 10000;
        request.dexvm.interpreter.diagnostics.event_mask = DexVmTraceBit(DexVmTraceKind::method_enter);
        request.maximum_ticks_per_call = UINT64_C(100000000);
        request.filesystem = &filesystem;
        request.ledger = &ledger;
        request.logger = &logger;
        return ogplay::session::AndroidAppProcess::Create(std::move(request));
    }

    explicit FrameVm(InterpreterBackend backend)
        : app(Create(backend)), vm(app->DexVm().Vm()), linker(vm.Linker()), model(vm.Model()) {}

    VmValue Checked(const VmCallOutcome& result) {
        REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
        return result.value;
    }

    VmCallOutcome OnOutcome(VmObjectRef object, const char* name, const char* descriptor,
                            std::vector<VmValue> args = {}) {
        const auto type = model.ObjectClass(object);
        const auto index = linker.FindVtableIndex(type, name, descriptor);
        REQUIRE(index);
        args.insert(args.begin(), VmValue::Ref(object));
        return vm.Call(linker.Class(type).vtable[*index], args);
    }

    VmValue On(VmObjectRef object, const char* name, const char* descriptor, std::vector<VmValue> args = {}) {
        return Checked(OnOutcome(object, name, descriptor, std::move(args)));
    }

    VmValue Static(const char* owner, const char* name, const char* descriptor, std::vector<VmValue> args = {}) {
        const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, descriptor);
        REQUIRE(method);
        return Checked(vm.Call(*method, args));
    }

    VmObjectRef New(const char* owner, const char* descriptor = "()V", std::vector<VmValue> args = {}) {
        Checked(vm.EnsureClassInitialized(linker.ResolveDescriptor(owner)));
        const auto result = vm.NewIntrinsicInstance(owner);
        const auto roots = vm.ProtectReferences(std::array{result});
        if (std::string_view(descriptor).starts_with("(Landroid/content/Context;") && !args.empty())
            ogplay::test::AttachViewTestContext(vm, args.front().ref);
        args.insert(args.begin(), VmValue::Ref(result));
        Static(owner, "<init>", descriptor, std::move(args));
        return result;
    }
};
}  // namespace

TEST_CASE("DVM-283 ROM FrameLayout Window bounds state RTL and match-parent remeasure") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        FrameVm f(backend);
        f.context->target_sdk_version = 19;
        f.context->ui_test_accessibility_disabled = true;
        const auto owner = f.New("Landroid/app/Activity;");
        f.context->activity = owner;
        ogplay::test::AttachViewTestContext(f.vm, owner);
        const auto root = f.On(owner, "findViewById", "(I)Landroid/view/View;", {VmValue::Int(0x01020002)}).ref;
        REQUIRE(root.IsValid());
        CHECK(f.linker.Class(f.model.ObjectClass(root)).descriptor == "Landroid/widget/FrameLayout;");
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        const auto info = f.On(owner, "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;").ref;
        const auto slot = [&](auto object, const char* name, const char* type = "I") -> Slot& {
            const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
            REQUIRE(field);
            return f.model.InstanceSlots(object)[f.linker.Field(*field).slot];
        };
        slot(info, "flags").bits |= 0x400000;
        CHECK(slot(root, "mForegroundGravity").bits == 0x77);
        CHECK(slot(root, "mForegroundInPadding", "Z").bits == 1);
        CHECK(slot(root, "mMeasureAllChildren", "Z").bits == 0);
        for (const auto name :
             {"mForegroundPaddingLeft", "mForegroundPaddingTop", "mForegroundPaddingRight", "mForegroundPaddingBottom"})
            CHECK(slot(root, name).bits == 0);
        CHECK(slot(root, "mForeground", "Landroid/graphics/drawable/Drawable;").bits == 0);
        for (const auto name : {"mSelfBounds", "mOverlayBounds"})
            CHECK(slot(root, name, "Landroid/graphics/Rect;").bits != 0);
        CHECK(slot(root, "mMatchParentChildren", "Ljava/util/ArrayList;").bits != 0);
        const auto make_leaf = [&] {
            return f.New("Ltest/MeasuredLeaf;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        };
        const auto params = [&](int w, int h, int gravity) {
            return f.New("Landroid/widget/FrameLayout$LayoutParams;", "(III)V",
                         {VmValue::Int(w), VmValue::Int(h), VmValue::Int(gravity)});
        };
        const auto first = make_leaf();
        const auto lp = params(40, 20, 0x800033);  // TOP|START
        f.On(lp, "setMargins", "(IIII)V", {VmValue::Int(3), VmValue::Int(5), VmValue::Int(7), VmValue::Int(9)});
        const auto lp_copy = f.New("Landroid/widget/FrameLayout$LayoutParams;",
                                   "(Landroid/widget/FrameLayout$LayoutParams;)V", {VmValue::Ref(lp)});
        CHECK(lp_copy != lp);
        CHECK(slot(lp_copy, "gravity").bits == 0x800033);
        CHECK(slot(lp_copy, "leftMargin").bits == 3);
        CHECK(slot(lp_copy, "rightMargin").bits == 7);
        f.On(owner, "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(first), VmValue::Ref(lp)});
        CHECK(f.context->content_view == first);
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) != first);
        f.On(root, "setPadding", "(IIII)V", {VmValue::Int(10), VmValue::Int(8), VmValue::Int(14), VmValue::Int(12)});
        f.On(first, "setClickable", "(Z)V", {VmValue::Int(1)});
        const auto first_id = *FindViewUiNode(*f.context, first.Value());
        f.context->ui_tree.Get(first_id)->background_color = 0x123456ff;
        std::vector<DexVmTraceEntry> entered;
        const auto capture = [&] {
            for (const auto& entry : f.vm.Trace("", 10000))
                if (entered.empty() || entry.sequence > entered.back().sequence) entered.push_back(entry);
        };
        const auto tick = [&] {
            const auto result = EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 140});
            capture();
            return result;
        };
        const auto bounds = [&](auto view, ui::Rect expected) {
            const auto id = *FindViewUiNode(*f.context, view.Value());
            const auto snapshot = f.context->ui_tree.PublishedProjection();
            REQUIRE(snapshot);
            CHECK(snapshot->Get(id)->frame == expected);
            CHECK(f.On(view, "getLeft", "()I").AsInt() == expected.left);
            CHECK(f.On(view, "getTop", "()I").AsInt() == expected.top);
            CHECK(f.On(view, "getRight", "()I").AsInt() == expected.right);
            CHECK(f.On(view, "getBottom", "()I").AsInt() == expected.bottom);
        };
        REQUIRE(tick());
        // AOSP layoutChildren: left=paddingLeft+leftMargin=13, top=8+5=13.
        bounds(root, {0, 0, 200, 140});
        bounds(first, {13, 13, 53, 33});
        CHECK(f.On(root, "getMeasuredState", "()I").AsInt() == 0);
        auto snapshot = f.context->ui_tree.PublishedProjection();
        CHECK(snapshot->HitTestProjection(13, 13) == first_id);
        CHECK(snapshot->HitTestProjection(12, 13) != first_id);
        auto pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snapshot, {}), {200, 140});
        CHECK(pixels.rgba8[(13 * 200 + 13) * 4 + 3] == 255);
        CHECK(pixels.rgba8[(13 * 200 + 12) * 4 + 3] == 0);
        // Relative margins override absolute ones; RTL swaps start=3/end=7.
        f.On(lp, "setMarginStart", "(I)V", {VmValue::Int(3)});
        f.On(lp, "setMarginEnd", "(I)V", {VmValue::Int(7)});
        f.On(root, "setLayoutDirection", "(I)V", {VmValue::Int(1)});
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(slot(lp, "leftMargin").bits == 7);
        CHECK(slot(lp, "rightMargin").bits == 3);
        // START becomes RIGHT: 200-14-40-3=143.
        bounds(first, {143, 13, 183, 33});
        slot(lp, "gravity").bits = 0x800035;  // TOP|END becomes LEFT.
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        bounds(first, {17, 13, 57, 33});
        f.On(root, "setLayoutDirection", "(I)V", {VmValue::Int(0)});
        slot(lp, "gravity").bits = 0x11;
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        // CENTER: 10+(176-40)/2+3-7=74; 8+(120-20)/2+5-9=54.
        bounds(first, {74, 54, 114, 74});
        slot(lp, "gravity").bits = 0x55;
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        bounds(first, {139, 99, 179, 119});
        const auto before_invisible = f.On(first, "count", "()I").AsInt();
        f.On(first, "setVisibility", "(I)V", {VmValue::Int(4)});
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.On(first, "count", "()I").AsInt() > before_invisible);
        bounds(first, {139, 99, 179, 119});
        snapshot = f.context->ui_tree.PublishedProjection();
        CHECK(snapshot->HitTestProjection(140, 100) != first_id);
        pixels = ui::RasterizeUiOverlay(ui::BuildUiRenderList(*snapshot, {}), {200, 140});
        CHECK(pixels.rgba8[(100 * 200 + 140) * 4 + 3] == 0);
        f.On(first, "setVisibility", "(I)V", {VmValue::Int(8)});
        const auto before_gone = f.On(first, "count", "()I").AsInt();
        f.On(first, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.On(first, "count", "()I").AsInt() == before_gone);
        f.On(root, "setMeasureAllChildren", "(Z)V", {VmValue::Int(1)});
        // The API19 setter only stores the boolean; request a traversal explicitly.
        f.On(first, "requestLayout", "()V");
        f.On(root, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.On(first, "count", "()I").AsInt() > before_gone);
        CHECK(f.context->ui_tree.PublishedProjection()->HitTestProjection(140, 100) != first_id);
        // AOSP default params are FrameLayout.LayoutParams(-1,-1), gravity=-1.
        const auto default_child = make_leaf();
        f.On(root, "addView", "(Landroid/view/View;)V", {VmValue::Ref(default_child)});
        const auto defaults = f.On(default_child, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref;
        CHECK(f.linker.Class(f.model.ObjectClass(defaults)).descriptor == "Landroid/widget/FrameLayout$LayoutParams;");
        CHECK(static_cast<int>(slot(defaults, "width").bits) == -1);
        CHECK(static_cast<int>(slot(defaults, "height").bits) == -1);
        CHECK(static_cast<int>(slot(defaults, "gravity").bits) == -1);
        REQUIRE(tick());
        bounds(default_child, {10, 8, 186, 128});
        // Copy conversion runs generateLayoutParams/checkLayoutParams in ROM.
        const auto generic =
            f.New("Landroid/view/ViewGroup$LayoutParams;", "(II)V", {VmValue::Int(9), VmValue::Int(6)});
        const auto copied = make_leaf();
        f.On(owner, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(copied), VmValue::Ref(generic)});
        CHECK(f.context->content_view == first);
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        REQUIRE(tick());
        bounds(copied, {10, 8, 19, 14});
        CHECK(f.On(copied, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;").ref != generic);
        // A non-exact nested frame runs under the same Window traversal. Two MATCH_PARENT
        // children trigger AOSP count>1; one child is the negative control (no second pass).
        const auto nested =
            f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto np = params(-2, -2, 0x33);
        f.On(owner, "setContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(nested), VmValue::Ref(np)});
        CHECK(AndroidUiTraversalRoot(f.vm, *f.context) == root);
        CHECK(f.context->content_view == nested);
        CHECK_FALSE(f.On(first, "getParent", "()Landroid/view/ViewParent;").ref.IsValid());
        const auto a = make_leaf();
        const auto b = make_leaf();
        f.On(nested, "setPadding", "(IIII)V", {VmValue::Int(2), VmValue::Int(3), VmValue::Int(4), VmValue::Int(5)});
        const auto ap = params(-1, -1, 0x33);
        const auto bp = params(-1, -1, 0x33);
        f.On(nested, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(a), VmValue::Ref(ap)});
        const auto before_a = f.On(a, "count", "()I").AsInt();
        REQUIRE(tick());
        CHECK(f.On(a, "count", "()I").AsInt() - before_a == 1);
        // desired 120x90 + padding 6x8 -> frame 126x98; available 176x120.
        bounds(nested, {10, 8, 136, 106});
        bounds(a, {2, 3, 122, 93});
        f.On(nested, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
             {VmValue::Ref(b), VmValue::Ref(bp)});
        const auto ac = f.On(a, "count", "()I").AsInt();
        const auto bc = f.On(b, "count", "()I").AsInt();
        REQUIRE(tick());
        // a cached AT_MOST first pass skips; second EXACTLY pass must execute.
        CHECK(f.On(a, "count", "()I").AsInt() > ac);
        CHECK(f.On(b, "count", "()I").AsInt() - bc == 2);
        bounds(a, {2, 3, 122, 93});
        bounds(b, {2, 3, 122, 93});
        CHECK(f.On(nested, "getMeasuredState", "()I").AsInt() == 0);
        // Constrain wrap child below desired: state bits propagate from leaf to frame.
        slot(np, "width").bits = 70;
        slot(np, "height").bits = 50;
        slot(ap, "width").bits = 0xfffffffeU;
        slot(ap, "height").bits = 0xfffffffeU;
        f.On(a, "requestLayout", "()V");
        REQUIRE(tick());
        CHECK(f.On(a, "getMeasuredState", "()I").AsInt() == 0x01000100);
        CHECK(f.On(nested, "getMeasuredState", "()I").AsInt() == 0x01000100);
        // Explicit Gravity.apply uses the retained API19 ROM implementation.
        const auto container = f.New("Landroid/graphics/Rect;", "(IIII)V",
                                     {VmValue::Int(10), VmValue::Int(8), VmValue::Int(186), VmValue::Int(128)});
        const auto output = f.New("Landroid/graphics/Rect;");
        f.Static("Landroid/view/Gravity;", "apply", "(IIILandroid/graphics/Rect;Landroid/graphics/Rect;I)V",
                 {VmValue::Int(0x800033), VmValue::Int(40), VmValue::Int(20), VmValue::Ref(container),
                  VmValue::Ref(output), VmValue::Int(1)});
        CHECK(slot(output, "left").bits == 146);
        CHECK(slot(output, "top").bits == 8);
        f.Static("Landroid/view/Gravity;", "apply", "(IIILandroid/graphics/Rect;Landroid/graphics/Rect;I)V",
                 {VmValue::Int(0x800033), VmValue::Int(40), VmValue::Int(20), VmValue::Ref(container),
                  VmValue::Ref(output), VmValue::Int(0)});
        CHECK(slot(output, "left").bits == 10);  // LTR negative control for RTL apply.
        // Isolate foreground-padding helpers with explicit Java fixture inputs; no Drawable service is claimed.
        slot(root, "mForegroundPaddingLeft").bits = 16;
        CHECK(f.On(root, "getPaddingLeftWithForeground", "()I").AsInt() == 16);  // max(10,16)
        slot(root, "mForegroundInPadding", "Z").bits = 0;
        CHECK(f.On(root, "getPaddingLeftWithForeground", "()I").AsInt() == 26);  // 10+16
        slot(root, "mForegroundInPadding", "Z").bits = 1;
        f.On(root, "setForegroundGravity", "(I)V", {VmValue::Int(0)});
        CHECK(f.On(root, "getForegroundGravity", "()I").AsInt() == 0x800033);
        CHECK(slot(root, "mForegroundPaddingLeft").bits == 0);  // null foreground resets padding
        capture();
        REQUIRE_FALSE(entered.empty());
        CHECK(entered.front().sequence == 1);
        for (std::size_t i = 1; i < entered.size(); ++i)
            CHECK(entered[i].sequence == entered[i - 1].sequence + 1);
        CHECK(f.ledger.Unimplemented().empty());
        for (const auto owner_sig : {"Landroid/widget/FrameLayout;", "Landroid/view/ViewGroup$MarginLayoutParams;",
                                     "Landroid/view/Gravity;"}) {
            bool executed = false;
            for (const auto& event : entered)
                if (event.class_descriptor == owner_sig) executed = true;
            CHECK(executed);
        }
        std::ofstream trace(std::string(OGPLAY_SOURCE_DIR) + "/.local/analysis/vd-07/frame-" +
                            (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        trace << RenderDexVmTraceJson(entered);
    }
}

TEST_CASE("DVM-283 experimental unsupported containers foreground and attribute constructors reject") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        FrameVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        for (const auto type :
             {"Landroid/widget/LinearLayout;", "Landroid/widget/RelativeLayout;", "Landroid/widget/ScrollView;",
              "Landroid/widget/TableLayout;", "Landroid/widget/TableRow;", "Landroid/widget/AbsoluteLayout;"}) {
            const auto object = f.vm.NewIntrinsicInstance(type);
            const auto ctor =
                f.linker.FindDirectMethod(f.linker.ResolveDescriptor(type), "<init>", "(Landroid/content/Context;)V");
            REQUIRE(ctor);
            const auto result = f.vm.Call(*ctor, std::array{VmValue::Ref(object), VmValue::Ref(owner)});
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
            const auto measured = f.OnOutcome(object, "onMeasure", "(II)V", {VmValue::Int(0), VmValue::Int(0)});
            REQUIRE(measured.exception.IsValid());
            CHECK(f.linker.Class(measured.exception_class).descriptor == "Ljava/lang/UnsatisfiedLinkError;");
            const auto laid_out =
                f.OnOutcome(object, "onLayout", "(ZIIII)V",
                            {VmValue::Int(0), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0), VmValue::Int(0)});
            REQUIRE(laid_out.exception.IsValid());
        }
        const auto frame = f.New("Landroid/widget/FrameLayout;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto failed = f.OnOutcome(frame, "setForeground", "(Landroid/graphics/drawable/Drawable;)V",
                                        {VmValue::Ref(VmObjectRef{})});
        REQUIRE(failed.exception.IsValid());
        const auto type = f.linker.ResolveDescriptor("Landroid/widget/FrameLayout;");
        const auto attrs =
            f.linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;)V");
        REQUIRE(attrs);
        const auto attributed =
            f.vm.Call(*attrs, std::array{VmValue::Ref(frame), VmValue::Ref(owner), VmValue::Ref(VmObjectRef{})});
        REQUIRE(attributed.exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
