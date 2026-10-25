#include "container_traversal_fixture.h"
#include "../../src/runtime/integration/dexvm_android/shared.h"
#include "ogplay/runtime/ui/ui_leaf_measure.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/video/fake_video_player.h"
#include <bit>
#include <thread>

using ogplay::test::container_traversal::ContainerVm;

namespace {
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

VmObjectRef Video(ContainerVm& f, VmObjectRef owner) {
    f.context->vfs = &f.filesystem;
    f.filesystem.MountHostDirectory("/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
    f.filesystem.MountHostDirectory("/next", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");
    f.context->video_source_player_factory = [opened = 0](auto) mutable {
        ogplay::video::VideoMetadata m;
        m.width = opened == 0 ? 320U : 640U;
        m.height = opened++ == 0 ? 240U : 360U;
        m.duration_ms = 1000;
        return std::make_unique<ogplay::video::FakeVideoPlayer>(m, 10);
    };
    return f.New("Landroid/widget/VideoView;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
}

void Open(ContainerVm& f, VmObjectRef view, const char* path = "/sdcard/short-mp4v-aac.mp4") {
    f.On(view, "setVideoPath", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8(path))});
}

int Generation(ContainerVm& f, VmObjectRef view) {
    IntrinsicContext call{f.vm, view, {}};
    return IntrinsicCall(call).GetInt(*f.context->video_view_generation_field);
}

void Deliver(ContainerVm& f, VmObjectRef view, int generation) {
    REQUIRE_FALSE(android_intrinsics::InvokeVideoEvent(f.vm, *f.context, view.Value(), generation, 1));
}

void Matrix(ContainerVm& f, VmObjectRef view, ui::Size intrinsic) {
    // Compare every scalar with the pre-existing pure service, retaining the
    // original ClampSize state=0 convention; EXACT is intentionally non-proportional.
    for (auto wm : {0U, 0x80000000U, 0x40000000U})
        for (auto hm : {0U, 0x80000000U, 0x40000000U}) {
            const auto spec = [](unsigned m, int size) {
                return ui::MeasureSpec{m == 0             ? ui::MeasureMode::Unspecified
                                       : m == 0x40000000U ? ui::MeasureMode::Exactly
                                                          : ui::MeasureMode::AtMost,
                                       size};
            };
            const auto expected =
                ui::MeasureImageLeaf({intrinsic, {3, 5, 7, 11}, {17, 23}}, spec(wm, 200), spec(hm, 100));
            f.On(view, "requestLayout", "()V");
            f.On(view, "measure", "(II)V",
                 {VmValue::Int(std::bit_cast<int>(wm | 200U)), VmValue::Int(std::bit_cast<int>(hm | 100U))});
            CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == expected.constrained.width);
            CHECK(f.On(view, "getMeasuredHeight", "()I").AsInt() == expected.constrained.height);
            CHECK(f.On(view, "getMeasuredState", "()I").AsInt() == 0);
        }
}

void Tick(ContainerVm& f) {
    REQUIRE(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::OwnerTick,
                                     AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 100}));
}
}  // namespace

TEST_CASE("DVM-303 VideoView adapter metadata matrix rejects stale generations on both backends") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        auto owner = Owner(f), view = Video(f, owner);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        f.On(view, "setPadding", "(IIII)V", {VmValue::Int(3), VmValue::Int(5), VmValue::Int(7), VmValue::Int(11)});
        f.On(view, "setMinimumWidth", "(I)V", {VmValue::Int(17)});
        f.On(view, "setMinimumHeight", "(I)V", {VmValue::Int(23)});
        Matrix(f, view, {});
        Open(f, view);
        const auto old = Generation(f, view);
        Matrix(f, view, {});  // decoder facts are not delivered yet
        // Host state alone is insufficient: Java generation must also match.
        {
            IntrinsicContext c{f.vm, view, {}};
            IntrinsicCall(c).SetInt(*f.context->video_view_generation_field, old + 1);
            Deliver(f, view, old);
            Matrix(f, view, {});
            IntrinsicCall(c).SetInt(*f.context->video_view_generation_field, old);
        }
        std::optional<std::string> off_owner;
        std::thread worker(
            [&] { off_owner = android_intrinsics::InvokeVideoEvent(f.vm, *f.context, view.Value(), old, 1); });
        worker.join();
        CHECK(off_owner.has_value());
        Matrix(f, view, {});
        Deliver(f, view, old);
        Matrix(f, view, {320, 240});
        Open(f, view, "/next/short-mp4v-aac.mp4");
        const auto current = Generation(f, view);
        CHECK(current != old);
        Deliver(f, view, old);
        Matrix(f, view, {});
        Deliver(f, view, current);
        Matrix(f, view, {640, 360});
        f.On(view, "stopPlayback", "()V");
        Deliver(f, view, current);
        Matrix(f, view, {});
        CHECK(f.context->video_views.empty());
        const auto type = f.model.ObjectClass(view);
        const auto slot = f.linker.FindVtableIndex(type, "onMeasure", "(II)V");
        REQUIRE(slot);
        const auto& method = f.linker.Method(f.linker.Class(type).vtable[*slot]);
        CHECK(method.kind == MethodKind::intrinsic);
        CHECK(method.adapter_review == "tools/bootdex/view-video-adapter.json#onMeasure");
        std::ofstream out(std::string(".local/analysis/vd-10b5/measure-") +
                          (backend == InterpreterBackend::threaded ? "threaded" : "switch") + ".trace.json");
        out << RenderDexVmTraceJson(f.vm.Trace("", 10000));
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-303 VideoView owner metadata invalidates Java and freezes published video geometry") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        auto owner = Owner(f), view = Video(f, owner);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        f.On(view, "setClickable", "(Z)V", {VmValue::Int(1)});
        f.On(owner, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(view)});
        Tick(f);
        const auto old = f.context->ui_tree.PublishedProjection();
        REQUIRE(old);
        const auto id = *FindViewUiNode(*f.context, view.Value());
        Open(f, view);
        const auto gen = Generation(f, view);
        REQUIRE_FALSE(PumpVideoViews(f.vm, *f.context, {}, false));
        CHECK(f.On(view, "isLayoutRequested", "()Z").AsInt() == 1);
        CHECK(f.context->ui_tree.PublishedProjection() == old);
        CHECK(old->Get(id)->intrinsic == ui::Size{});
        Tick(f);
        const auto snap = f.context->ui_tree.PublishedProjection();
        REQUIRE(snap);
        CHECK(snap != old);
        CHECK(snap->Get(id)->intrinsic == ui::Size{320, 240});
        CHECK(snap->Get(id)->measured == ui::Size{200, 100});
        CHECK(f.On(view, "getMeasuredWidth", "()I").AsInt() == 200);
        CHECK(f.On(view, "getMeasuredHeight", "()I").AsInt() == 100);
        // Unpublished building-tree geometry must not affect video pixels.
        auto& state = f.context->video_views.at(view.Value());
        ogplay::video::VideoFrame frame;
        frame.width = 2;
        frame.height = 1;
        frame.rgba8 = {255, 0, 0, 255, 255, 0, 0, 255};
        state.latest_frame = frame;
        // DVM-310: producer pixels/opening are frozen by the owner, never Compose.
        RefreshAndroidSurfaceFrames(*f.context);
        static_cast<void>(EnsureAndroidUiTraversal(f.vm, *f.context, AndroidUiTraversalReason::BeforePresent,
                                                   AndroidUiTraversalPhase::ResolveOnly, ui::Size{200, 100}));
        const auto frozen = f.context->ui_tree.PublishedProjection();
        f.context->ui_tree.Get(id)->screen_frame = {50, 50, 51, 51};
        std::vector<std::uint8_t> canvas(200 * 100 * 4, 0);
        ComposeVideoViews(*f.context, canvas, 200, 100, false);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 200; ++x) {
                const auto hit = snap->HitTestProjection(x, y);
                CHECK(hit == id);
                CHECK(canvas[static_cast<std::size_t>((y * 200 + x) * 4)] == 255);
                CHECK(canvas[static_cast<std::size_t>((y * 200 + x) * 4 + 3)] == 255);
            }
        f.On(view, "stopPlayback", "()V");
        CHECK(f.context->ui_tree.PublishedProjection() == frozen);
        CHECK(snap->Get(id)->intrinsic == ui::Size{320, 240});
        Deliver(f, view, gen);
        Tick(f);
        CHECK(f.context->ui_tree.PublishedProjection()->Get(id)->intrinsic == ui::Size{});
        CHECK(f.ledger.Unimplemented().empty());
    }
}

TEST_CASE("DVM-303 VideoView unsupported services and unhandled errors fail with ledger") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend, true);
        auto owner = Owner(f), view = Video(f, owner);
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        CHECK(f.ledger.Unimplemented().empty());
        auto type = f.model.ObjectClass(view);
        auto reject = [&](const char* name, const char* sig, std::vector<VmValue> args) {
            const auto m = f.linker.FindDirectMethod(type, name, sig);
            REQUIRE(m);
            args.insert(args.begin(), VmValue::Ref(view));
            auto result = f.vm.Call(*m, args);
            REQUIRE(result.exception.IsValid());
            CHECK(f.linker.Class(result.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        };
        for (const auto service : {"subtitle", "AudioManager", "MediaController"})
            reject("nativeUnsupported", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8(service))});
        reject("nativeUnhandledError", "()V", {});
        CHECK(f.ledger.Unimplemented().size() == 4);
        const auto controller = f.OnOutcome(view, "setMediaController", "(Landroid/widget/MediaController;)V",
                                            {VmValue::Ref(VmObjectRef{})});
        CHECK(controller.exception.IsValid());
        const auto subtitle =
            f.OnOutcome(view, "addSubtitleSource", "(Ljava/io/InputStream;Landroid/media/MediaFormat;)V",
                        {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        CHECK(subtitle.exception.IsValid());
        const auto audio = f.On(owner, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
                                {VmValue::Ref(f.vm.NewStringUtf8("audio"))})
                               .ref;
        REQUIRE(audio.IsValid());
        const auto focus =
            f.OnOutcome(audio, "requestAudioFocus", "(Landroid/media/AudioManager$OnAudioFocusChangeListener;II)I",
                        {VmValue::Ref(VmObjectRef{}), VmValue::Int(3), VmValue::Int(1)});
        CHECK(focus.exception.IsValid());
        CHECK(f.ledger.Unimplemented().size() == 7);
    }
}
