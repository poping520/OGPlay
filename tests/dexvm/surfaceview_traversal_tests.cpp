#include "container_traversal_fixture.h"
#include <bit>

using ogplay::test::container_traversal::ContainerVm;

namespace {
Slot& Field(ContainerVm& f, VmObjectRef object, const char* name, const char* type = "I") {
    const auto id = f.linker.FindFieldRecursive(f.model.ObjectClass(object), name, type);
    REQUIRE(id);
    return f.model.InstanceSlots(object)[f.linker.Field(*id).slot];
}

int Get(ContainerVm& f, VmObjectRef object, const char* name) {
    return f.On(object, name, "()I").AsInt();
}

VmObjectRef Owner(ContainerVm& f) {
    f.context->ui_test_accessibility_disabled = true;
    f.context->target_sdk_version = 19;
    f.context->surface_width = 320;
    f.context->surface_height = 240;
    f.context->arsc.package_id = 0x7f;
    ogplay::loader::ArscEntry theme;
    theme.resource_id = 0x7f020001;
    theme.type_name = "style";
    theme.entry_name = "surface";
    theme.is_complex = true;
    theme.parent = 0x01030005;
    for (auto id : {0x01010064U, 0x01010065U, 0x01010034U, 0x010100d4U})
        theme.bag.push_back({id, 1, 0, {}});
    f.context->arsc.entries = {theme};
    f.context->application_theme = theme.resource_id;
    const auto owner = f.New("Landroid/app/Activity;");
    f.context->activity = owner;
    ogplay::test::AttachViewTestContext(f.vm, owner);
    f.On(owner, "setTheme", "(I)V", {VmValue::Int(static_cast<int>(theme.resource_id))});
    return owner;
}

VmObjectRef Surface(ContainerVm& f, VmObjectRef owner) {
    return f.New("Landroid/view/SurfaceView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
}

VmObjectRef Holder(ContainerVm& f, VmObjectRef view) {
    return f.On(view, "getHolder", "()Landroid/view/SurfaceHolder;").ref;
}

VmObjectRef Frame(ContainerVm& f, VmObjectRef holder) {
    return f.On(holder, "getSurfaceFrame", "()Landroid/graphics/Rect;").ref;
}

void Size(ContainerVm& f, VmObjectRef holder, int w, int h) {
    f.On(holder, "setFixedSize", "(II)V", {VmValue::Int(w), VmValue::Int(h)});
}

void Shape(ContainerVm& f, VmObjectRef frame, int w, int h) {
    CHECK(Get(f, frame, "width") == w);
    CHECK(Get(f, frame, "height") == h);
    CHECK(Field(f, frame, "left").bits == 0);
    CHECK(Field(f, frame, "top").bits == 0);
}
}  // namespace

TEST_CASE("DVM-302 SurfaceView ROM measure matrix and holder sentinel on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f), view = Surface(f, owner), holder = Holder(f, view), frame = Frame(f, holder);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, holder, frame});
        CHECK(Field(f, view, "mRequestedWidth").bits == UINT32_MAX);
        CHECK(Field(f, view, "mRequestedHeight").bits == UINT32_MAX);
        CHECK(f.On(view, "willNotDraw", "()Z").AsInt() == 1);
        for (auto wm : {0U, 0x80000000U, 0x40000000U})
            for (auto hm : {0U, 0x80000000U, 0x40000000U})
                for (int fixed : {0, 1, 2, 3}) {
                    Size(f, holder, (fixed & 1) ? 320 : 0, (fixed & 2) ? 240 : 0);
                    CHECK(f.On(view, "isFixedSize", "()Z").AsInt() == (fixed != 0));
                    f.On(view, "measure", "(II)V",
                         {VmValue::Int(std::bit_cast<int>(wm | 200U)), VmValue::Int(std::bit_cast<int>(hm | 150U))});
                    const int w = wm == 0 ? ((fixed & 1) ? 320 : 0) : 200;
                    const int h = hm == 0 ? ((fixed & 2) ? 240 : 0) : 150;
                    CHECK(Get(f, view, "getMeasuredWidth") == w);
                    CHECK(Get(f, view, "getMeasuredHeight") == h);
                    const auto state = ((fixed & 1) && wm == 0x80000000U ? 0x01000000 : 0) |
                                       ((fixed & 2) && hm == 0x80000000U ? 0x00000100 : 0);
                    CHECK(Get(f, view, "getMeasuredState") == state);
                    f.On(view, "layout", "(IIII)V",
                         {VmValue::Int(4), VmValue::Int(7), VmValue::Int(4 + w), VmValue::Int(7 + h)});
                    CHECK(Get(f, view, "getWidth") == w);
                    CHECK(Get(f, view, "getHeight") == h);
                    CHECK(Holder(f, view) == holder);
                    CHECK(Frame(f, holder) == frame);
                    Shape(f, frame, 0, 0);  // detached requests/layout never manufacture a buffer
                    CHECK(f.context->surface_width == 320);
                    CHECK(f.context->surface_height == 240);
                }
        f.On(holder, "setSizeFromLayout", "()V");
        CHECK(Field(f, view, "mRequestedWidth").bits == UINT32_MAX);
        CHECK(Field(f, view, "mRequestedHeight").bits == UINT32_MAX);
        CHECK(f.On(view, "isFixedSize", "()Z").AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());
        std::ofstream out(std::string(".local/analysis/vd-10b4/measure-") +
                          (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        out << RenderDexVmTraceJson(f.vm.Trace("", 10000));
    }
}

TEST_CASE("DVM-302 SurfaceView managed fixed rejection is atomic and requestLayout is real") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f), view = Surface(f, owner), holder = Holder(f, view), frame = Frame(f, holder);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, holder, frame});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
        const auto id = *FindViewUiNode(*f.context, view.Value());
        const auto snapshot = f.context->ui_tree.PublishedProjection();
        Size(f, holder, 320, 240);
        CHECK(f.On(view, "isLayoutRequested", "()Z").AsInt() == 1);
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
        const auto published = f.context->ui_tree.PublishedProjection();
        CHECK(published->Get(id)->measured.width == Get(f, view, "getMeasuredWidth"));
        CHECK(published->Get(id)->measured.height == Get(f, view, "getMeasuredHeight"));
        CHECK(published->Get(id)->frame.right - published->Get(id)->frame.left == Get(f, view, "getWidth"));
        CHECK(snapshot->Get(id)->measured == ui::Size{200, 150});
        CHECK(f.On(view, "isLayoutRequested", "()Z").AsInt() == 0);
        CHECK(f.ledger.Unimplemented().empty());
        const auto before = f.model.InstanceSlots(view);
        const std::vector<Slot> saved(before.begin(), before.end());
        const auto epoch = published->ProjectionEpoch();
        for (const auto pair : {std::pair{320, 241}, std::pair{321, 240}, std::pair{0, 241}}) {
            const auto rejected =
                f.OnOutcome(holder, "setFixedSize", "(II)V", {VmValue::Int(pair.first), VmValue::Int(pair.second)});
            REQUIRE(rejected.exception.IsValid());
            CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
            const auto after = f.model.InstanceSlots(view);
            REQUIRE(after.size() == saved.size());
            for (std::size_t i = 0; i < saved.size(); ++i)
                CHECK(after[i].bits == saved[i].bits);
            CHECK(f.context->ui_tree.PublishedProjection()->ProjectionEpoch() == epoch);
            CHECK(Frame(f, holder) == frame);
            Shape(f, frame, 0, 0);
            CHECK(f.context->surface_width == 320);
            CHECK(f.context->surface_height == 240);
        }
        CHECK_FALSE(f.ledger.Unimplemented().empty());
        f.On(holder, "setSizeFromLayout", "()V");
        CHECK(f.On(view, "isLayoutRequested", "()Z").AsInt() == 1);
        CHECK(Field(f, view, "mRequestedWidth").bits == UINT32_MAX);
        CHECK(Field(f, view, "mRequestedHeight").bits == UINT32_MAX);
    }
}

TEST_CASE("DVM-302 SurfaceView managed holder generation and stable Rect without GPU") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f), view = Surface(f, owner), holder = Holder(f, view), frame = Frame(f, holder);
        const auto listener = f.New("Ltest/ManagedSurfaceCallback;");
        const auto roots = f.vm.ProtectReferences(std::array{owner, view, holder, frame, listener});
        f.On(holder, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(listener)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                         AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 150}));
        Shape(f, frame, 0, 0);
        // Managed holder fixture: callbacks only, no native window, renderer or APK lifecycle.
        // Explicit OwnerTick above supplies real geometry. Isolate the existing
        // holder dispatcher through its reentrant scheduling guard; this fixture
        // gives no credit to the still-disabled BeforeSurfaceCallbacks traversal.
        const auto dispatch = [&](SurfaceHolderPhase phase) {
            struct Restore {
                bool& flag;
                bool old;
                ~Restore() { flag = old; }
            } restore{f.context->ui_layout_dispatching, f.context->ui_layout_dispatching};
            f.context->ui_layout_dispatching = true;
            return DispatchSurfaceHolderCallbacks(f.vm, *f.context, phase);
        };
        for (int repeat = 0; repeat < 2; ++repeat) {
            REQUIRE_FALSE(dispatch(SurfaceHolderPhase::created));
            REQUIRE_FALSE(dispatch(SurfaceHolderPhase::changed));
        }
        CHECK(Field(f, listener, "sequence").bits == 12);
        CHECK(Field(f, listener, "frame", "Landroid/graphics/Rect;").bits == frame.Value());
        Shape(f, frame, 320, 240);
        Size(f, holder, 320, 240);
        f.On(view, "measure", "(II)V", {VmValue::Int(0x40000064), VmValue::Int(0x40000050)});
        f.On(view, "layout", "(IIII)V", {VmValue::Int(0), VmValue::Int(0), VmValue::Int(100), VmValue::Int(80)});
        Shape(f, frame, 320, 240);
        CHECK(Get(f, view, "getWidth") == 100);
        f.On(holder, "setSizeFromLayout", "()V");
        f.context->surface_width = 640;
        f.context->surface_height = 480;
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::changed));
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::changed));
        CHECK(Field(f, listener, "sequence").bits == 122);
        CHECK(Frame(f, holder) == frame);
        Shape(f, frame, 640, 480);
        REQUIRE_FALSE(RetireSurfaceHolderGeneration(f.vm, *f.context));
        REQUIRE_FALSE(RetireSurfaceHolderGeneration(f.vm, *f.context));
        CHECK(Field(f, listener, "sequence").bits == 1223);
        CHECK(Frame(f, holder) == frame);
        Shape(f, frame, 640, 480);
        const auto replacement = Holder(f, view), next_frame = Frame(f, replacement);
        CHECK(replacement != holder);
        CHECK(next_frame != frame);
        const auto keep = f.vm.ProtectReferences(std::array{replacement, next_frame});
        f.On(replacement, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", {VmValue::Ref(listener)});
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::created));
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::changed));
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::created));
        REQUIRE_FALSE(dispatch(SurfaceHolderPhase::changed));
        CHECK(Field(f, listener, "sequence").bits == 122312);
        CHECK(Frame(f, replacement) == next_frame);
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-302 SurfaceView reviewed constructors and disabled WMS remain explicit") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = Owner(f);
        const auto root = f.vm.ProtectReferences(std::array{owner});
        for (const auto sig :
             {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V",
              "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
            std::vector<VmValue> args{VmValue::Ref(owner)};
            if (std::string_view(sig).find("AttributeSet") != std::string_view::npos)
                args.push_back(VmValue::Ref(VmObjectRef{}));
            if (std::string_view(sig).find(";I)") != std::string_view::npos) args.push_back(VmValue::Int(0));
            const auto view = f.New("Landroid/view/SurfaceView;", sig, args);
            const auto keep = f.vm.ProtectReferences(std::array{view});
            CHECK(Field(f, view, "mRequestedWidth").bits == UINT32_MAX);
            CHECK(Field(f, view, "mRequestedHeight").bits == UINT32_MAX);
            CHECK(Holder(f, view) == Holder(f, view));
        }
        const auto type = f.linker.ResolveDescriptor("Landroid/view/SurfaceView;");
        for (const auto name : {"onMeasure", "isFixedSize"}) {
            const auto index =
                f.linker.FindVtableIndex(type, name, std::string_view(name) == "onMeasure" ? "(II)V" : "()Z");
            REQUIRE(index);
            const auto method = f.linker.Class(type).vtable.at(*index);
            CHECK(f.linker.Method(method).code.has_value());
            CHECK_FALSE(static_cast<bool>(f.linker.Method(method).implementation));
        }
        CHECK(f.ledger.Unimplemented().empty());
        const auto view = Surface(f, owner);
        const auto update = f.linker.FindDirectMethod(type, "updateWindow", "(ZZ)V");
        REQUIRE(update);
        const auto rejected = f.vm.Call(*update, std::array{VmValue::Ref(view), VmValue::Int(0), VmValue::Int(0)});
        CHECK(rejected.exception.IsValid());
        CHECK_FALSE(f.ledger.Unimplemented().empty());
    }
}
