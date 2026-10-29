#include "widget_fixture.h"

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

namespace ogplay::test::widget {

[[nodiscard]] std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path = std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

[[nodiscard]] ogplay::video::VideoPlayerFactory FakeFactory() {
    return [](const std::filesystem::path&) {
        ogplay::video::VideoMetadata metadata;
        metadata.width = 8U;
        metadata.height = 4U;
        metadata.duration_ms = 1000;
        return std::unique_ptr<ogplay::video::VideoPlayer>(
            std::make_unique<ogplay::video::FakeVideoPlayer>(metadata, 10U));
    };
}

[[nodiscard]] ogplay::loader::BinaryXmlAttribute AndroidAttribute(std::string name, const std::uint8_t type,
                                                                  const std::uint32_t data) {
    return ogplay::loader::BinaryXmlAttribute{
        .namespace_uri = "http://schemas.android.com/apk/res/android",
        .name = std::move(name),
        .value_type = type,
        .data = data,
    };
}

[[nodiscard]] ogplay::loader::BinaryXmlAttribute StyleAttribute(const std::uint8_t type, const std::uint32_t data) {
    return ogplay::loader::BinaryXmlAttribute{.name = "style", .value_type = type, .data = data};
}

ClickVm::ClickVm(const InterpreterBackend backend, const bool attach_activity)
    : context(std::make_shared<DexVmAndroidContext>()), runtime([this, backend] {
          ogplay::test::BindBootDexArchive(*context);
          std::vector<IntrinsicClassDecl> catalog;
          auto android = AndroidIntrinsicCatalog(context);
          for (auto& owner : android)
              for (auto& method : owner.methods) {
                  if (owner.descriptor == "Landroid/view/View;" && method.name == "onFinishInflate") {
                      method.implementation = [this](IntrinsicContext& call) {
                          const auto node = *FindViewUiNode(*context, call.receiver.Value());
                          inflater_finish_ids.push_back(context->ui_tree.Get(node)->android_id);
                          CHECK_FALSE(context->ui_tree.Get(node)->parent.has_value());
                          static_cast<void>(call.vm.CollectGarbage("inflater-finish"));
                          if (inflater_finish_hook) inflater_finish_hook(call);
                          return VmValue::Void();
                      };
                  }
              }
          catalog = std::move(android);
          std::vector<IntrinsicClassDecl> touch_probes;
          auto activity_probe = IntrinsicClassBuilder::Class("LTouchActivityProbe;", "Landroid/app/Activity;");
          activity_probe.OverrideMethod("dispatchTouchEvent", "(Landroid/view/MotionEvent;)Z",
                                        [this](IntrinsicContext&) {
                                            ++touch_dispatch_events;
                                            return VmValue::Int(1);
                                        });
          touch_probes.push_back(std::move(activity_probe).Build());
          auto fallback_probe = IntrinsicClassBuilder::Class("LTouchFallbackProbe;", "Landroid/app/Activity;");
          fallback_probe.OverrideMethod("onUserInteraction", "()V", [this](IntrinsicContext&) {
              ++user_interactions;
              return VmValue::Void();
          });
          fallback_probe.OverrideMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z", [this](IntrinsicContext&) {
              ++activity_touch_events;
              return VmValue::Int(1);
          });
          touch_probes.push_back(std::move(fallback_probe).Build());
          auto group_probe = IntrinsicClassBuilder::Class("LTouchGroupProbe;", "Landroid/widget/FrameLayout;");
          group_probe.OverrideMethod("onInterceptTouchEvent", "(Landroid/view/MotionEvent;)Z",
                                     [this](IntrinsicContext&) {
                                         ++intercept_events;
                                         return VmValue::Int(intercept_touches);
                                     });
          touch_probes.push_back(std::move(group_probe).Build());
          catalog.insert(catalog.end(), touch_probes.begin(), touch_probes.end());
          auto content_view = IntrinsicClassBuilder::Class("LTestContentView;", "Landroid/view/View;");
          content_view.OverrideMethod("onTouchEvent", "(Landroid/view/MotionEvent;)Z", [this](IntrinsicContext&) {
              ++content_view_events;
              return VmValue::Int(content_view_handled ? 1 : 0);
          });
          std::vector<IntrinsicClassDecl> test_catalog;
          test_catalog.push_back(std::move(content_view).Build());
          auto key_view = IntrinsicClassBuilder::Class("LTestKeyView;", "Landroid/view/View;");
          key_view.OverrideMethod("onKeyDown", "(ILandroid/view/KeyEvent;)Z", [this](IntrinsicContext& call) {
              ++key_down_events;
              last_key_code = call.arguments[0].AsInt();
              return VmValue::Int(1);
          });
          key_view.OverrideMethod("onKeyUp", "(ILandroid/view/KeyEvent;)Z", [this](IntrinsicContext& call) {
              ++key_up_events;
              last_key_code = call.arguments[0].AsInt();
              return VmValue::Int(1);
          });
          key_view.OverrideMethod("onKeyMultiple", "(IILandroid/view/KeyEvent;)Z", [this](IntrinsicContext& call) {
              ++key_multiple_events;
              last_key_code = call.arguments[0].AsInt();
              return VmValue::Int(call.arguments[1].AsInt() == 0);
          });
          test_catalog.push_back(std::move(key_view).Build());
          auto system_ui_listener =
              IntrinsicClassBuilder::Class("LSystemUiVisibilityListener;", "Ljava/lang/Object;",
                                           {"Landroid/view/View$OnSystemUiVisibilityChangeListener;"});
          system_ui_listener.VirtualMethod("onSystemUiVisibilityChange", "(I)V", [this](IntrinsicContext&) {
              ++system_ui_visibility_events;
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(system_ui_listener).Build());
          auto text_watcher = IntrinsicClassBuilder::Class("LRecordingTextWatcher;", "Ljava/lang/Object;",
                                                           {"Landroid/text/TextWatcher;"});
          text_watcher.VirtualMethod(
              "beforeTextChanged", "(Ljava/lang/CharSequence;III)V", [this](IntrinsicContext& call) {
                  text_events.emplace_back("before");
                  last_text_range = {call.arguments[1].AsInt(), call.arguments[2].AsInt(), call.arguments[3].AsInt()};
                  return VmValue::Void();
              });
          text_watcher.VirtualMethod("onTextChanged", "(Ljava/lang/CharSequence;III)V", [this](IntrinsicContext& call) {
              text_events.emplace_back("on");
              last_text_range = {call.arguments[1].AsInt(), call.arguments[2].AsInt(), call.arguments[3].AsInt()};
              return VmValue::Void();
          });
          text_watcher.VirtualMethod("afterTextChanged", "(Landroid/text/Editable;)V", [this](IntrinsicContext&) {
              text_events.emplace_back("after");
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(text_watcher).Build());
          auto removal_probe = IntrinsicClassBuilder::Class("LRemovalLayoutProbe;", "Landroid/widget/LinearLayout;");
          removal_probe.OverrideMethod("requestLayout", "()V", [this](IntrinsicContext&) {
              ++removal_layout_requests;
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(removal_probe).Build());
          auto bring_probe = IntrinsicClassBuilder::Class("LBringFrontProbe;", "Landroid/widget/FrameLayout;");
          bring_probe.OverrideMethod("bringChildToFront", "(Landroid/view/View;)V", [this](IntrinsicContext&) {
              ++bring_front_requests;
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(bring_probe).Build());
          auto remove_all_probe = IntrinsicClassBuilder::Class("LRemoveAllProbe;", "Landroid/widget/FrameLayout;");
          remove_all_probe.OverrideMethod("removeAllViewsInLayout", "()V", [this](IntrinsicContext&) {
              ++remove_all_in_layout_requests;
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(remove_all_probe).Build());
          auto image_context = IntrinsicClassBuilder::Class("LImageContextProbe;", "Landroid/content/Context;");
          image_context.OverrideMethod(
              "getResources", "()Landroid/content/res/Resources;", [this](IntrinsicContext& call) {
                  ++image_resource_queries;
                  static_cast<void>(call.vm.CollectGarbage("image-resource-delegate"));
                  if (image_delegate_exception.IsValid())
                      throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "resource delegate",
                                        image_delegate_exception};
                  return VmValue::Ref(call.vm.NewIntrinsicInstance("Landroid/content/res/Resources;"));
              });
          test_catalog.push_back(std::move(image_context).Build());
          auto image_probe = IntrinsicClassBuilder::Class("LImageDrawableProbe;", "Landroid/widget/ImageView;");
          const auto observed =
              image_probe.BoundInstanceField("observed", "Landroid/graphics/drawable/Drawable;", kAccPublic);
          image_probe.OverrideMethod(
              "setImageDrawable", "(Landroid/graphics/drawable/Drawable;)V", [this, observed](IntrinsicContext& call) {
                  IntrinsicCall(call).SetRef(observed, call.arguments[0].ref);
                  static_cast<void>(call.vm.CollectGarbage("image-delegate"));
                  if (image_delegate_exception.IsValid())
                      throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "image delegate",
                                        image_delegate_exception};
                  const auto parent = call.vm.Linker().ResolveDescriptor("Landroid/widget/ImageView;");
                  const auto index = *call.vm.Linker().FindVtableIndex(parent, "setImageDrawable",
                                                                       "(Landroid/graphics/drawable/Drawable;)V");
                  const auto result = call.vm.Call(call.vm.Linker().Class(parent).vtable[index],
                                                   std::array{VmValue::Ref(call.receiver), call.arguments[0]});
                  REQUIRE_FALSE(result.exception.IsValid());
                  return result.value;
              });
          test_catalog.push_back(std::move(image_probe).Build());
          auto content_activity = IntrinsicClassBuilder::Class("LContentActivityProbe;", "Landroid/app/Activity;");
          content_activity.OverrideMethod("onContentChanged", "()V", [this](IntrinsicContext& call) {
              ++content_change_events;
              static_cast<void>(call.vm.CollectGarbage("window-content-callback"));
              if (content_change_hook) content_change_hook(call);
              return VmValue::Void();
          });
          test_catalog.push_back(std::move(content_activity).Build());
          auto content_surface = IntrinsicClassBuilder::Class("LContentSurfaceCallback;", "Ljava/lang/Object;",
                                                              {"Landroid/view/SurfaceHolder$Callback;"});
          content_surface.VirtualMethod("surfaceCreated", "(Landroid/view/SurfaceHolder;)V", [this](IntrinsicContext&) {
              ++content_surface_creates;
              return VmValue::Void();
          });
          content_surface.VirtualMethod("surfaceChanged", "(Landroid/view/SurfaceHolder;III)V",
                                        [](IntrinsicContext&) { return VmValue::Void(); });
          content_surface.VirtualMethod("surfaceDestroyed", "(Landroid/view/SurfaceHolder;)V",
                                        [this](IntrinsicContext&) {
                                            ++content_surface_destroys;
                                            if (content_surface_destroy_hook) content_surface_destroy_hook();
                                            return VmValue::Void();
                                        });
          test_catalog.push_back(std::move(content_surface).Build());
          auto managed = IntrinsicClassBuilder::Class("LManagedDialogActivity;", "Landroid/app/Activity;");
          managed.OverrideMethod(
              "onCreate", "(Landroid/os/Bundle;)V",
              [this](IntrinsicContext& call) {
                  if (dialog_on_create_hook) dialog_on_create_hook(call);
                  return VmValue::Void();
              },
              kAccProtected);
          const auto supplied = managed.BoundInstanceField("supplied", "Landroid/app/Dialog;", kAccPrivate);
          const auto prepared = managed.BoundInstanceField("lastArgs", "Landroid/os/Bundle;", kAccPublic);
          managed.VirtualMethod("supply", "(Landroid/app/Dialog;)V", [supplied](IntrinsicContext& call) {
              IntrinsicCall(call).SetRef(supplied, call.arguments[0].ref);
              return VmValue::Void();
          });
          managed.OverrideMethod(
              "onCreateDialog", "(I)Landroid/app/Dialog;",
              [this, supplied](IntrinsicContext& call) {
                  ++dialog_creates;
                  return VmValue::Ref(IntrinsicCall(call).GetRef(supplied));
              },
              kAccProtected);
          managed.OverrideMethod(
              "onPrepareDialog", "(ILandroid/app/Dialog;Landroid/os/Bundle;)V",
              [this, prepared](IntrinsicContext& call) {
                  ++dialog_prepares;
                  IntrinsicCall(call).SetRef(prepared, call.arguments[2].ref);
                  if (dialog_prepare_hook) dialog_prepare_hook(call);
                  const auto type = call.vm.Model().ObjectClass(call.arguments[1].ref);
                  const auto index =
                      *call.vm.Linker().FindVtableIndex(type, "setOwnerActivity", "(Landroid/app/Activity;)V");
                  const auto result = call.vm.Call(call.vm.Linker().Class(type).vtable[index],
                                                   std::array{call.arguments[1], VmValue::Ref(call.receiver)});
                  REQUIRE_FALSE(result.exception.IsValid());
                  return VmValue::Void();
              },
              kAccProtected);
          test_catalog.push_back(std::move(managed).Build());
          auto observer = IntrinsicClassBuilder::Class("LDialogObserver;", "Ljava/lang/Object;",
                                                       {"Landroid/content/DialogInterface$OnClickListener;",
                                                        "Landroid/content/DialogInterface$OnCancelListener;",
                                                        "Landroid/content/DialogInterface$OnDismissListener;",
                                                        "Landroid/content/DialogInterface$OnShowListener;",
                                                        "Landroid/content/DialogInterface$OnKeyListener;"});
          observer.VirtualMethod("onClick", "(Landroid/content/DialogInterface;I)V", [this](IntrinsicContext& call) {
              dialog_events.push_back("click:" + std::to_string(call.arguments[1].AsInt()));
              if (dialog_click_hook) dialog_click_hook(call);
              return VmValue::Void();
          });
          for (const auto name : {"onCancel", "onDismiss", "onShow"}) {
              observer.VirtualMethod(name, "(Landroid/content/DialogInterface;)V", [this, name](IntrinsicContext&) {
                  dialog_events.push_back(name);
                  return VmValue::Void();
              });
          }
          observer.VirtualMethod("onKey", "(Landroid/content/DialogInterface;ILandroid/view/KeyEvent;)Z",
                                 [this](IntrinsicContext&) { return VmValue::Int(dialog_key_consumed ? 1 : 0); });
          test_catalog.push_back(std::move(observer).Build());
          catalog.insert(catalog.end(), test_catalog.begin(), test_catalog.end());
          return std::make_unique<ogplay::test::container_traversal::AndroidTestRuntime>(
              context, ledger, logger, std::move(catalog), ReadFixture("widgetclick.dex"),
              InterpreterConfig{.backend = backend}, false, &vfs);
      }()),
      model(runtime->bridge->Model()), linker(runtime->bridge->Linker()), interpreter(runtime->bridge->Vm()) {
    interpreter.Monitors().SetTimeSource([state = context] { return state->uptime_millis.load(); });
    context->surface_width = 100U;
    context->surface_height = 100U;
    context->vfs = &vfs;
    context->encoded_music = &music;
    threads = &runtime->bridge->Threads();
    context->threads = threads;
    const auto looper = linker.ResolveDescriptor("Landroid/os/Looper;");
    const auto prepare = linker.FindDirectMethod(looper, "prepareMainLooper", "()V");
    REQUIRE(prepare.has_value());
    const auto prepared = interpreter.Call(*prepare, {});
    REQUIRE_MESSAGE(!prepared.exception.IsValid(), prepared.exception_message);
    context->video_source_player_factory =
        [factory = FakeFactory()](std::shared_ptr<const ogplay::video::VideoDataSource>) { return factory({}); };
    interpreter.SetLogger(&logger);
    context->ui_test_accessibility_disabled = true;
    vfs.MountHostDirectory("/sdcard", std::filesystem::path{OGPLAY_SOURCE_DIR} / "tests/fixtures/video");

    video_view = interpreter.NewIntrinsicInstance("Landroid/widget/VideoView;");
    activity = interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
    if (attach_activity) ogplay::test::AttachViewTestContext(interpreter, activity);
    const auto bar = interpreter.NewIntrinsicInstance("Landroid/widget/LinearLayout;");
    skip_button = interpreter.NewIntrinsicInstance("Landroid/widget/ImageButton;");
    other_button = interpreter.NewIntrinsicInstance("Landroid/widget/ImageButton;");

    if (!attach_activity) return;
    context->activity = activity;
    const auto construct = [&](VmObjectRef view, const char* type, bool styled) {
        if (styled)
            CallDirect(view, type, "<init>", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V",
                       {VmValue::Ref(activity), VmValue::Ref(VmObjectRef{}), VmValue::Int(0)});
        else
            CallDirect(view, type, "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
    };
    construct(video_view, "Landroid/widget/VideoView;", false);
    construct(bar, "Landroid/widget/LinearLayout;", false);
    construct(other_button, "Landroid/widget/ImageButton;", true);
    construct(skip_button, "Landroid/widget/ImageButton;", true);
    CallOn(bar, "setGravity", "(I)V", {VmValue::Int(1)});
    CallOn(bar, "setPadding", "(IIII)V", {VmValue::Int(0), VmValue::Int(4), VmValue::Int(0), VmValue::Int(0)});
    for (auto button : {other_button, skip_button})
        CallOn(bar, "addView", "(Landroid/view/View;II)V", {VmValue::Ref(button), VmValue::Int(20), VmValue::Int(10)});
    CallOn(activity, "setContentView", "(Landroid/view/View;)V", {VmValue::Ref(video_view)});
    const auto params = interpreter.NewIntrinsicInstance("Landroid/widget/FrameLayout$LayoutParams;");
    CallDirect(params, "Landroid/widget/FrameLayout$LayoutParams;", "<init>", "(III)V",
               {VmValue::Int(-1), VmValue::Int(-2), VmValue::Int(0x50)});
    CallOn(activity, "addContentView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V",
           {VmValue::Ref(bar), VmValue::Ref(params)});
    static_cast<void>(EnsureAndroidUiTraversal(interpreter, *context, AndroidUiTraversalReason::OwnerTick,
                                               AndroidUiTraversalPhase::ResolveOnly));
}

VmValue ClickVm::CallOn(const VmObjectRef receiver, const std::string& name, const std::string& descriptor,
                        std::vector<VmValue> arguments) {
    const auto activity_type = linker.ResolveDescriptor("Landroid/app/Activity;");
    if (linker.IsAssignable(activity_type, model.ObjectClass(receiver)) && context->activity == receiver &&
        (context->activity_stack.empty() || context->activity_stack.back().object != receiver)) {
        context->content_view = VmObjectRef{};
        context->activity_stack.clear();
        DexVmAndroidContext::ActivityRecord record;
        record.object = receiver;
        record.visible = true;
        context->activity_stack.push_back(record);
    }
    const auto construct_view = [&](VmObjectRef object) {
        if (!object.IsValid()) return;
        const auto view_type = linker.ResolveDescriptor("Landroid/view/View;");
        auto type = model.ObjectClass(object);
        if (!linker.IsAssignable(view_type, type)) return;
        const auto field = linker.FindFieldRecursive(type, "mContext", "Landroid/content/Context;");
        if (field && model.InstanceSlots(object)[linker.Field(*field).slot].bits != 0) return;
        while (!linker.FindDirectMethod(type, "<init>", "(Landroid/content/Context;)V"))
            type = *linker.Class(type).super;
        CallDirect(object, linker.Class(type).descriptor, "<init>", "(Landroid/content/Context;)V",
                   {VmValue::Ref(activity)});
    };
    construct_view(receiver);
    if ((name == "addView" || name == "setContentView" || name == "addContentView") && !arguments.empty() &&
        !descriptor.starts_with("(I"))
        construct_view(arguments[0].ref);
    if (name == "getWindow" || name == "getDecorView" || name == "findViewById" || name == "setContentView" ||
        name == "addContentView")
        ogplay::test::AttachViewTestContext(interpreter, receiver);
    // Historical fixture assertions query geometry only after an explicit owner barrier.
    if (context->activity.IsValid() &&
        (name == "getWidth" || name == "getHeight" || name == "getLeft" || name == "getRight" || name == "getTop" ||
         name == "getBottom" || name == "getMeasuredWidth" || name == "getMeasuredHeight"))
        static_cast<void>(EnsureAndroidUiTraversal(interpreter, *context, AndroidUiTraversalReason::OwnerTick,
                                                   AndroidUiTraversalPhase::ResolveOnly));
    const auto receiver_class = model.ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(receiver_class, name, descriptor);
    REQUIRE_MESSAGE(index.has_value(), name);
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    const auto outcome = interpreter.Call(linker.Class(receiver_class).vtable[*index], arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmObjectRef ClickVm::NewTextDialog(VmObjectRef owner, VmObjectRef listener, bool cancelable) {
    context->surface_width = 800;
    context->surface_height = 480;
    context->activity = owner;
    const auto builder = interpreter.NewIntrinsicInstance("Landroid/app/AlertDialog$Builder;");
    const auto roots = interpreter.ProtectReferences(std::array{builder, owner, listener});
    CallDirect(builder, "Landroid/app/AlertDialog$Builder;", "<init>", "(Landroid/content/Context;)V",
               {VmValue::Ref(owner)});
    CallOn(builder, "setTitle", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
           {VmValue::Ref(interpreter.NewStringUtf8("Saved title"))});
    CallOn(builder, "setMessage", "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;",
           {VmValue::Ref(interpreter.NewStringUtf8("A real message"))});
    CallOn(
        builder, "setNeutralButton",
        "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;",
        {VmValue::Ref(interpreter.NewStringUtf8("OK")), VmValue::Ref(listener)});
    CallOn(builder, "setCancelable", "(Z)Landroid/app/AlertDialog$Builder;", {VmValue::Int(cancelable ? 1 : 0)});
    CallOn(builder, "setOnCancelListener",
           "(Landroid/content/DialogInterface$OnCancelListener;)Landroid/app/AlertDialog$Builder;",
           {VmValue::Ref(listener)});
    CallOn(builder, "setOnDismissListener",
           "(Landroid/content/DialogInterface$OnDismissListener;)Landroid/app/AlertDialog$Builder;",
           {VmValue::Ref(listener)});
    const auto dialog = CallOn(builder, "create", "()Landroid/app/AlertDialog;").ref;
    const auto dialog_root = interpreter.ProtectReferences(std::array{dialog});
    CallOn(dialog, "setOnShowListener", "(Landroid/content/DialogInterface$OnShowListener;)V",
           {VmValue::Ref(listener)});
    CallOn(dialog, "setOnKeyListener", "(Landroid/content/DialogInterface$OnKeyListener;)V", {VmValue::Ref(listener)});
    return dialog;
}

VmValue ClickVm::CallDirect(const VmObjectRef receiver, const std::string& class_descriptor, const std::string& name,
                            const std::string& descriptor, std::vector<VmValue> arguments) {
    if (name == "<init>" && descriptor.starts_with("(Landroid/content/Context;") && !arguments.empty()) {
        ogplay::test::AttachViewTestContext(interpreter, arguments.front().ref);
    }
    const auto java_class = linker.FindClass(class_descriptor);
    REQUIRE_MESSAGE(java_class.has_value(), class_descriptor);
    if (name == "<init>") {
        const auto initialized = interpreter.EnsureClassInitialized(*java_class);
        REQUIRE_MESSAGE(!initialized.exception.IsValid(), initialized.exception_message);
    }
    const auto method = linker.FindDirectMethod(*java_class, name, descriptor);
    REQUIRE_MESSAGE(method.has_value(), name);
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    const auto outcome = interpreter.Call(*method, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
}

VmObjectRef ClickVm::NewListener() {
    const auto java_class = linker.FindClass("LSkipListener;");
    REQUIRE(java_class.has_value());
    const auto clinit = interpreter.EnsureClassInitialized(*java_class);
    REQUIRE_MESSAGE(!clinit.exception.IsValid(), clinit.exception_message);
    const auto init = linker.FindDirectMethod(*java_class, "<init>", "()V");
    REQUIRE(init.has_value());
    const auto listener = model.NewInstance(*java_class, linker.Class(*java_class).instance_slots);
    const auto outcome = interpreter.Call(*init, std::vector<VmValue>{VmValue::Ref(listener)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return listener;
}

std::int32_t ClickVm::CallStaticInt(const std::string& name) {
    const auto java_class = linker.FindClass("LSkipListener;");
    REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, name, "()I");
    REQUIRE(method.has_value());
    const auto outcome = interpreter.Call(*method, {});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value.AsInt();
}

void ClickVm::SetTouchResult(const bool handled) {
    const auto java_class = linker.FindClass("LSkipListener;");
    REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, "setTouchResult", "(I)V");
    REQUIRE(method.has_value());
    const auto outcome = interpreter.Call(*method, std::vector<VmValue>{VmValue::Int(handled ? 1 : 0)});
    REQUIRE_FALSE(outcome.exception.IsValid());
}

void ClickVm::BindTarget(const VmObjectRef view) {
    const auto java_class = linker.FindClass("LSkipListener;");
    REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, "bind", "(Landroid/widget/VideoView;)V");
    REQUIRE(method.has_value());
    const auto outcome = interpreter.Call(*method, std::vector<VmValue>{VmValue::Ref(view)});
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
}

VmObjectRef ClickVm::NewTouchView() {
    const auto java_class = linker.FindClass("LTouchView;");
    REQUIRE(java_class.has_value());
    const auto initialized = interpreter.EnsureClassInitialized(*java_class);
    REQUIRE_FALSE(initialized.exception.IsValid());
    const auto view = model.NewInstance(*java_class, linker.Class(*java_class).instance_slots);
    CallDirect(view, "LTouchView;", "<init>", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
    return view;
}

void ClickVm::ResetTouchView(const bool handled) {
    const auto java_class = linker.FindClass("LTouchView;");
    REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, "reset", "(I)V");
    REQUIRE(method.has_value());
    const auto outcome = interpreter.Call(*method, std::vector<VmValue>{VmValue::Int(handled ? 1 : 0)});
    REQUIRE_FALSE(outcome.exception.IsValid());
}

std::int32_t ClickVm::TouchViewValue(const char* name) {
    const auto java_class = linker.FindClass("LTouchView;");
    REQUIRE(java_class.has_value());
    const auto method = linker.FindDirectMethod(*java_class, name, "()I");
    REQUIRE(method.has_value());
    const auto outcome = interpreter.Call(*method, {});
    REQUIRE_FALSE(outcome.exception.IsValid());
    return outcome.value.AsInt();
}

std::optional<std::string> ClickVm::Click(const float x, const float y) {
    const auto hit = FindClickableViewAt(interpreter, *context, x, y);
    if (!hit.has_value()) return std::nullopt;
    const auto error = InvokeViewOnClick(interpreter, *context, *hit);
    REQUIRE_MESSAGE(!error.has_value(), error.value_or(""));
    return "clicked";
}

}  // namespace ogplay::test::widget
