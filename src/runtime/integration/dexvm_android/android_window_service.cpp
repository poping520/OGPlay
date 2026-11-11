#include "rom_window.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "rom_input.h"
#include "rom_activity.h"

#include <algorithm>

#include <bit>

namespace ogplay::runtime::android_intrinsics {
namespace {
using V = dx::VmValue;

template <class F>
dx::IntrinsicHandler WindowCall(F fn) {
    return [fn](dx::IntrinsicContext& c) -> V {
        try {
            return fn(c);
        } catch (const std::invalid_argument& e) {
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", e.what()};
        } catch (const std::length_error& e) {
            throw dx::VmJavaThrow{"Landroid/view/Surface$OutOfResourcesException;", e.what()};
        }
    };
}

RomWindowState::Client& Client(RomWindowState& state, dx::VmObjectRef binder) {
    const auto found = state.clients.find(binder.Value());
    if (found == state.clients.end()) throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unknown IWindow"};
    return found->second;
}

void FocusChanged(dx::Interpreter& vm, const Context& context, RomWindowState& state, WindowId previous) {
    const auto current = state.system.Focus();
    if (current == previous) return;
    // Snapshot references: callback implementations may reenter this service.
    std::vector<std::pair<dx::VmObjectRef, bool>> callbacks;
    for (const auto& [_, client] : state.clients)
        if (client.window == previous || client.window == current)
            callbacks.emplace_back(client.binder, client.window == current);
    std::stable_sort(callbacks.begin(), callbacks.end(),
                     [](const auto& a, const auto& b) { return a.second < b.second; });
    const auto focused =
        std::ranges::find_if(state.clients, [&](const auto& item) { return item.second.window == current; });
    std::vector<dx::VmObjectRef> refs;
    for (const auto& [binder, _] : callbacks)
        refs.push_back(binder);
    const auto roots = vm.ProtectReferences(refs);
    SetRomInputWindowFocus(vm, context, focused == state.clients.end() ? dx::VmObjectRef{} : focused->second.binder);
    for (const auto& [binder, focused] : callbacks) {
        const auto client = state.clients.find(binder.Value());
        if (client == state.clients.end() || !state.system.Exists(client->second.window)) continue;
        static_cast<void>(CallAndroidMethod(vm, binder, "windowFocusChanged", "(ZZ)V",
                                            {V::Int(focused), V::Int(state.system.InTouchMode())}));
    }
}
}  // namespace

RomWindowState& RomWindows(const Context& context) {
    if (!context->rom_windows) {
        context->rom_windows = std::make_shared<RomWindowState>(context->surface_width, context->surface_height);
        if (context->rom_activities)
            for (const auto& r : context->rom_activities->stack)
                context->rom_windows->system.SetApplicationVisible(r.token.Value(), r.visible);
    }
    return *context->rom_windows;
}

void ResizeRomDisplay(dx::Interpreter& vm, const Context& context) {
    auto& s = RomWindows(context);
    QueueRomWindowInput(vm, context, s.system.CancelTouch());
    s.system.ResizeDisplay(static_cast<int>(context->surface_width), static_cast<int>(context->surface_height));
    const auto empty = NewPlatformObject(vm, "Landroid/graphics/Rect;", "()V", {});
    const auto frame = NewPlatformObject(vm, "Landroid/graphics/Rect;", "()V", {});
    const auto roots = vm.ProtectReferences(std::array{empty, frame});
    for (auto& [_, client] : s.clients) {
        const auto rect = s.system.Frame(client.window);
        WriteWindowRect(vm, frame, rect);
        static_cast<void>(CallAndroidMethod(
            vm, client.binder, "resized",
            "(Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;ZLandroid/content/res/Configuration;)V",
            {V::Ref(frame), V::Ref(empty), V::Ref(empty), V::Ref(empty), V::Int(1), V::Ref(dx::VmObjectRef{})}));
    }
}

void SetRomApplicationVisibility(dx::Interpreter& vm, const Context& context, dx::VmObjectRef token, bool visible) {
    if (!context->rom_windows) return;
    auto& s = *context->rom_windows;
    const auto focus = s.system.Focus();
    if (!visible) QueueRomWindowInput(vm, context, s.system.CancelApplicationTouch(token.Value()));
    s.system.SetApplicationVisible(token.Value(), visible);
    FocusChanged(vm, context, s, focus);
}

dx::Slot& WindowField(dx::Interpreter& vm, dx::VmObjectRef object, const char* name, const char* type) {
    if (!object.IsValid()) throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", name};
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, type);
    if (!field) throw std::logic_error("ROM window field missing: " + std::string(name));
    return vm.Model().InstanceSlots(object)[vm.Linker().Field(*field).slot];
}

void WriteWindowRect(dx::Interpreter& vm, dx::VmObjectRef object, WindowRect rect) {
    if (!object.IsValid()) return;
    for (const auto& [name, value] : std::array{std::pair{"left", rect.left}, std::pair{"top", rect.top},
                                                std::pair{"right", rect.right}, std::pair{"bottom", rect.bottom}})
        WindowField(vm, object, name, "I") = {static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

WindowAttributes ReadWindowAttributes(dx::Interpreter& vm, const Context& context, dx::VmObjectRef attrs) {
    const auto i = [&](const char* name) {
        return std::bit_cast<std::int32_t>(WindowField(vm, attrs, name, "I").bits);
    };
    const auto f = [&](const char* name) { return std::bit_cast<float>(WindowField(vm, attrs, name, "F").bits); };
    WindowAttributes result;
    result.type = i("type");
    result.flags = i("flags");
    result.format = i("format");
    result.width = i("width");
    result.height = i("height");
    result.x = i("x");
    result.y = i("y");
    result.gravity = i("gravity");
    result.alpha = f("alpha");
    result.horizontal_margin = f("horizontalMargin");
    result.vertical_margin = f("verticalMargin");
    result.application_token = WindowField(vm, attrs, "token", "Landroid/os/IBinder;").bits;
    if (result.type >= 1000 && result.type <= 1004) {
        const auto token = WindowField(vm, attrs, "token", "Landroid/os/IBinder;").bits;
        const auto found = RomWindows(context).clients.find(token);
        if (found == RomWindows(context).clients.end())
            throw dx::VmJavaThrow{"Landroid/view/WindowManager$BadTokenException;", "unknown subwindow token"};
        result.parent = found->second.window;
        result.application_token = found->second.attributes.application_token;
    }
    return result;
}

void AddWindowManagerMethods(dx::IntrinsicClassBuilder& builder, const Context& context) {
    builder.VirtualMethod(
        "openSession",
        "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;)Landroid/view/IWindowSession;",
        [context](dx::IntrinsicContext& c) {
            const auto input_method =
                GetLocalServiceInterface(c.vm, "input_method", "Lcom/android/internal/view/IInputMethodManager$Stub;",
                                         "Lcom/android/internal/view/IInputMethodManager;");
            static_cast<void>(CallAndroidMethod(
                c.vm, input_method, "addClient",
                "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;II)V",
                {c.arguments[0], c.arguments[1], V::Int(static_cast<int>(context->application_uid)),
                 V::Int(kGuestProcessId)}));
            auto& state = RomWindows(context);
            if (!state.session.IsValid())
                state.session = NewPlatformObject(c.vm, "Lorg/ogplay/framework/LocalWindowSession;", "()V", {});
            return V::Ref(state.session);
        });
    for (const auto* method : {"getInitialDisplaySize", "getBaseDisplaySize"})
        builder.VirtualMethod(method, "(ILandroid/graphics/Point;)V", [context](dx::IntrinsicContext& c) {
            if (c.arguments[0].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "only the default display exists"};
            auto point = dx::IntrinsicCall(c).NonNullRef(1, "size");
            WindowField(c.vm, point, "x", "I") = {context->surface_width, dx::SlotTag::cat1};
            WindowField(c.vm, point, "y", "I") = {context->surface_height, dx::SlotTag::cat1};
            return V::Void();
        });
    for (const auto* method : {"getInitialDisplayDensity", "getBaseDisplayDensity"})
        builder.VirtualMethod(method, "(I)I", [context](dx::IntrinsicContext& c) {
            if (c.arguments[0].AsInt() != 0)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "only the default display exists"};
            return V::Int(DeviceDensityDpi(context));
        });
    builder.VirtualMethod("hasNavigationBar", "()Z", [](dx::IntrinsicContext&) { return V::Int(0); });
    builder.VirtualMethod("getRotation", "()I", [](dx::IntrinsicContext&) { return V::Int(0); });
    builder.VirtualMethod("removeRotationWatcher", "(Landroid/view/IRotationWatcher;)V",
                          [](dx::IntrinsicContext&) { return V::Void(); });
    builder.VirtualMethod("setInTouchMode", "(Z)V", [context](dx::IntrinsicContext& c) {
        RomWindows(context).system.SetInTouchMode(c.arguments[0].AsInt() != 0);
        return V::Void();
    });
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "addAppToken",
                                   "(ILandroid/view/IApplicationToken;IIIZZII)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "addWindowToken",
                                   "(Landroid/os/IBinder;I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "clearForcedDisplayDensity", "(I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "clearForcedDisplaySize", "(I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "closeSystemDialogs",
                                   "(Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "disableKeyguard",
                                   "(Landroid/os/IBinder;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "dismissKeyguard", "()V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "executeAppTransition", "()V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "exitKeyguardSecurely",
                                   "(Landroid/view/IOnKeyguardExitResult;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "freezeRotation", "(I)V");
    // Window transitions are disabled. App ValueAnimator duration keeps its default scale.
    builder.VirtualMethod("getAnimationScale", "(I)F",
                          [](dx::IntrinsicContext& c) { return V::Float(c.arguments[0].AsInt() == 2 ? 1.0F : 0.0F); });
    builder.VirtualMethod("getAnimationScales", "()[F", [](dx::IntrinsicContext& c) {
        const auto scales =
            c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[F"), JniPrimitiveKind::float_value, 3);
        c.vm.Model().SetPrimitiveElement(scales, 2, std::bit_cast<std::uint32_t>(1.0F));
        return V::Ref(scales);
    });
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getAppOrientation",
                                   "(Landroid/view/IApplicationToken;)I");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getCompatibleMagnificationSpecForWindow",
                                   "(Landroid/os/IBinder;)Landroid/view/MagnificationSpec;");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getFocusedWindowToken",
                                   "()Landroid/os/IBinder;");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getPendingAppTransition", "()I");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getPreferredOptionsPanelGravity", "()I");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "getWindowFrame",
                                   "(Landroid/os/IBinder;Landroid/graphics/Rect;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "inputMethodClientHasFocus",
                                   "(Lcom/android/internal/view/IInputMethodClient;)Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "isRotationFrozen", "()Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "isSafeModeEnabled", "()Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "isViewServerRunning", "()Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "lockNow", "(Landroid/os/Bundle;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "overridePendingAppTransition",
                                   "(Ljava/lang/String;IILandroid/os/IRemoteCallback;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "overridePendingAppTransitionScaleUp",
                                   "(IIII)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "overridePendingAppTransitionThumb",
                                   "(Landroid/graphics/Bitmap;IILandroid/os/IRemoteCallback;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "pauseKeyDispatching",
                                   "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "prepareAppTransition", "(IZ)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "reenableKeyguard",
                                   "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "removeAppToken", "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "removeWindowToken",
                                   "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "resumeKeyDispatching",
                                   "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "screenshotApplications",
                                   "(Landroid/os/IBinder;IIIZ)Landroid/graphics/Bitmap;");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAnimationScale", "(IF)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAnimationScales", "([F)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAppGroupId", "(Landroid/os/IBinder;I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAppOrientation",
                                   "(Landroid/view/IApplicationToken;I)V");
    RejectUnsupportedServiceMethod(
        builder, "android.view.IWindowManager", "setAppStartingWindow",
        "(Landroid/os/IBinder;Ljava/lang/String;ILandroid/content/res/CompatibilityInfo;Ljava/lang/CharSequence;IIIILandroid/os/IBinder;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAppVisibility",
                                   "(Landroid/os/IBinder;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setAppWillBeHidden",
                                   "(Landroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setEventDispatching", "(Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setFocusedApp", "(Landroid/os/IBinder;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setForcedDisplayDensity", "(II)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setForcedDisplaySize", "(III)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setInputFilter",
                                   "(Landroid/view/IInputFilter;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setMagnificationCallbacks",
                                   "(Landroid/view/IMagnificationCallbacks;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setMagnificationSpec",
                                   "(Landroid/view/MagnificationSpec;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setNewConfiguration",
                                   "(Landroid/content/res/Configuration;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setOverscan", "(IIIII)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setStrictModeVisualIndicatorPreference",
                                   "(Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "setTouchExplorationEnabled", "(Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "showStrictModeViolation", "(Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "startAppFreezingScreen",
                                   "(Landroid/os/IBinder;I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "startFreezingScreen", "(II)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "startViewServer", "(I)Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "statusBarVisibilityChanged", "(I)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "stopAppFreezingScreen",
                                   "(Landroid/os/IBinder;Z)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "stopFreezingScreen", "()V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "stopViewServer", "()Z");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "thawRotation", "()V");
    RejectUnsupportedServiceMethod(
        builder, "android.view.IWindowManager", "updateOrientationFromAppTokens",
        "(Landroid/content/res/Configuration;Landroid/os/IBinder;)Landroid/content/res/Configuration;");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "updateRotation", "(ZZ)V");
    RejectUnsupportedServiceMethod(builder, "android.view.IWindowManager", "waitForWindowDrawn",
                                   "(Landroid/os/IBinder;Landroid/os/IRemoteCallback;)Z");
}

Decl Declare_local_AccessibilityService(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalAccessibilityService;",
                                                    "Landroid/view/accessibility/IAccessibilityManager$Stub;");
    builder.VirtualMethod("addClient", "(Landroid/view/accessibility/IAccessibilityManagerClient;I)I",
                          [](dx::IntrinsicContext&) { return V::Int(0); });
    const auto empty_list = [](dx::IntrinsicContext& c) {
        return V::Ref(NewPlatformObject(c.vm, "Ljava/util/ArrayList;", "()V", {}));
    };
    builder.VirtualMethod("getInstalledAccessibilityServiceList", "(I)Ljava/util/List;", empty_list);
    builder.VirtualMethod("getEnabledAccessibilityServiceList", "(II)Ljava/util/List;", empty_list);
    return std::move(builder).Build();
}

Decl Declare_local_TextServices() {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalTextServices;",
                                                    "Lcom/android/internal/textservice/ITextServicesManager$Stub;");
    builder.VirtualMethod("isSpellCheckerEnabled", "()Z", [](dx::IntrinsicContext&) { return V::Int(0); });
    const auto absent = [](dx::IntrinsicContext&) { return V::Ref(dx::VmObjectRef{}); };
    builder.VirtualMethod("getCurrentSpellChecker", "(Ljava/lang/String;)Landroid/view/textservice/SpellCheckerInfo;",
                          absent);
    builder.VirtualMethod("getCurrentSpellCheckerSubtype",
                          "(Ljava/lang/String;Z)Landroid/view/textservice/SpellCheckerSubtype;", absent);
    builder.VirtualMethod("getEnabledSpellCheckers", "()[Landroid/view/textservice/SpellCheckerInfo;",
                          [](dx::IntrinsicContext& call) {
                              auto& linker = call.vm.Linker();
                              return V::Ref(call.vm.Model().NewObjectArray(
                                  linker.ResolveDescriptor("[Landroid/view/textservice/SpellCheckerInfo;"),
                                  linker.ResolveDescriptor("Landroid/view/textservice/SpellCheckerInfo;"), 0));
                          });
    return std::move(builder).Build();
}

Decl Declare_local_WindowSession(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalWindowSession;",
                                              "Landroid/view/IWindowSession$Stub;");
    b.Constructor("()V", [](dx::IntrinsicContext& c) {
        const auto ctor = c.vm.Linker().FindDirectMethod(
            c.vm.Linker().ResolveDescriptor("Landroid/view/IWindowSession$Stub;"), "<init>", "()V");
        const auto result = c.vm.Call(*ctor, std::array{V::Ref(c.receiver)});
        if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
        return V::Void();
    });
    for (bool display : {false, true})
        for (bool channel : {false, true}) {
            const auto name = display ? (channel ? "addToDisplay" : "addToDisplayWithoutInputChannel")
                                      : (channel ? "add" : "addWithoutInputChannel");
            const auto sig = std::string("(Landroid/view/IWindow;ILandroid/view/WindowManager$LayoutParams;I") +
                             (display ? "I" : "") + "Landroid/graphics/Rect;" +
                             (channel ? "Landroid/view/InputChannel;" : "") + ")I";
            b.VirtualMethod(name, sig, WindowCall([context, display, channel](dx::IntrinsicContext& c) {
                                auto& state = RomWindows(context);
                                auto binder = dx::IntrinsicCall(c).NonNullRef(0, "window");
                                if (state.clients.contains(binder.Value())) return V::Int(-5);
                                if (display && c.arguments[4].AsInt() != 0) return V::Int(-9);
                                const auto attrs = c.arguments[2].ref;
                                const auto before = state.system.Focus();
                                const auto attributes = ReadWindowAttributes(c.vm, context, attrs);
                                // ViewRootImpl clears AttachInfo focus on its first traversal. The first
                                // relayout delivers focus after that traversal has attached the view.
                                const auto id = state.system.Add(attributes, false, channel);
                                state.clients.emplace(
                                    binder.Value(), RomWindowState::Client{id, binder, attrs, 0, 0, false, attributes});
                                if (channel)
                                    state.clients.at(binder.Value()).channel =
                                        CreateWindowInputChannel(c.vm, context, c.arguments[display ? 6 : 5].ref);
                                WriteWindowRect(c.vm, c.arguments[display ? 5 : 4].ref, {});
                                FocusChanged(c.vm, context, state, before);
                                return V::Int(2 | (state.system.InTouchMode() ? 1 : 0));
                            }));
        }
    b.VirtualMethod("remove", "(Landroid/view/IWindow;)V", WindowCall([context](dx::IntrinsicContext& c) {
                        auto& s = RomWindows(context);
                        const auto client = Client(s, c.arguments[0].ref);
                        const auto focus = s.system.Focus();
                        QueueRomWindowInput(c.vm, context, s.system.CancelWindowTouch(client.window));
                        if (context->session)
                            for (const auto& [_, child] : s.clients)
                                if (child.window == client.window || child.attributes.parent == client.window)
                                    context->session->Process().RetireSurfaceWindow(
                                        memory::GuestAddress{child.window.value});
                        s.system.Remove(client.window);
                        for (auto& [_, surface] : s.surfaces)
                            if (surface.window.value && !s.system.Exists(surface.window)) surface.valid = false;
                        for (auto it = s.clients.begin(); it != s.clients.end();) {
                            if (!s.system.Exists(it->second.window)) {
                                CloseWindowInputChannel(context, it->second.channel);
                                ReleaseRomSurfaceReference(context, it->second.surface);
                                it = s.clients.erase(it);
                            } else
                                ++it;
                        }
                        FocusChanged(c.vm, context, s, focus);
                        return V::Void();
                    }));
    b.VirtualMethod(
        "relayout",
        "(Landroid/view/IWindow;ILandroid/view/WindowManager$LayoutParams;IIII"
        "Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Rect;Landroid/content/res/Configuration;Landroid/view/Surface;)I",
        WindowCall([context](dx::IntrinsicContext& c) {
            auto& s = RomWindows(context);
            auto& client = Client(s, c.arguments[0].ref);
            const auto focus = s.system.Focus();
            const auto attrs = c.arguments[2].ref.IsValid() ? c.arguments[2].ref : client.attrs;
            const auto attributes =
                c.arguments[2].ref.IsValid() ? ReadWindowAttributes(c.vm, context, attrs) : client.attributes;
            const bool visible = c.arguments[5].AsInt() == 0;
            if (!visible || (attributes.flags & 0x10))
                QueueRomWindowInput(c.vm, context, s.system.CancelWindowTouch(client.window));
            const memory::GuestAddress identity{client.window.value};
            const bool retired = !visible && client.surface != 0;
            if (retired) {
                if (context->session) context->session->Process().RetireSurfaceWindow(identity);
                s.surfaces.at(client.surface).valid = false;
                ReleaseRomSurfaceReference(context, std::exchange(client.surface, 0));
            }
            const auto frame =
                s.system.Relayout(client.window, attributes, c.arguments[3].AsInt(), c.arguments[4].AsInt(), visible);
            client.attrs = attrs;
            client.attributes = attributes;
            WriteWindowRect(c.vm, c.arguments[7].ref, frame);
            for (std::size_t i = 8; i <= 10; ++i)
                WriteWindowRect(c.vm, c.arguments[i].ref, {});
            const bool first = visible && client.surface == 0;
            if (first) {
                client.surface = s.next_handle++;
                s.surfaces.emplace(client.surface, RomWindowState::Surface{client.window});
            }
            if (client.surface) {
                auto& surface = s.surfaces.at(client.surface);
                surface.format = attributes.format;
                surface.buffer_width = c.arguments[3].AsInt() > 0 ? c.arguments[3].AsInt() : frame.Width();
                surface.buffer_height = c.arguments[4].AsInt() > 0 ? c.arguments[4].AsInt() : frame.Height();
                if (context->session && surface.buffer_width > 0 && surface.buffer_height > 0)
                    context->session->Process().UpdateSurfaceWindow({identity,
                                                                     static_cast<unsigned>(surface.buffer_width),
                                                                     static_cast<unsigned>(surface.buffer_height)});
            }
            // In-process out parameters need no Parcel. The ROM Surface owns a native ref.
            auto& native = WindowField(c.vm, c.arguments[12].ref, "mNativeObject", "I");
            if (native.bits != static_cast<std::uint32_t>(client.surface)) {
                if (native.bits) ReleaseRomSurfaceReference(context, static_cast<int>(native.bits));
                if (client.surface) ++s.surfaces.at(client.surface).refs;
                native = {static_cast<std::uint32_t>(client.surface), dx::SlotTag::cat1};
                auto& generation = WindowField(c.vm, c.arguments[12].ref, "mGenerationId", "I");
                ++generation.bits;
            }
            FocusChanged(c.vm, context, s, focus);
            return V::Int((first ? 2 | 4 : retired ? 4 : 0) | (s.system.InTouchMode() ? 1 : 0));
        }));
    b.VirtualMethod("finishDrawing", "(Landroid/view/IWindow;)V", [context](dx::IntrinsicContext& c) {
        Client(RomWindows(context), c.arguments[0].ref).drawn = true;
        return V::Void();
    });
    b.VirtualMethod("getDisplayFrame", "(Landroid/view/IWindow;Landroid/graphics/Rect;)V",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = RomWindows(context);
                        Client(s, c.arguments[0].ref);
                        WriteWindowRect(c.vm, c.arguments[1].ref, s.system.DisplayFrame());
                        return V::Void();
                    });
    b.VirtualMethod("setTransparentRegion", "(Landroid/view/IWindow;Landroid/graphics/Region;)V",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = RomWindows(context);
                        s.system.SetTransparentRegion(Client(s, c.arguments[0].ref).window,
                                                      ReadWindowRegion(c.vm, context, c.arguments[1].ref));
                        return V::Void();
                    });
    b.VirtualMethod("setInsets",
                    "(Landroid/view/IWindow;ILandroid/graphics/Rect;Landroid/graphics/Rect;Landroid/graphics/Region;)V",
                    WindowCall([context](dx::IntrinsicContext& c) {
                        auto& s = RomWindows(context);
                        const auto rect = [&](dx::VmObjectRef ref) {
                            return WindowRect{static_cast<int>(WindowField(c.vm, ref, "left", "I").bits),
                                              static_cast<int>(WindowField(c.vm, ref, "top", "I").bits),
                                              static_cast<int>(WindowField(c.vm, ref, "right", "I").bits),
                                              static_cast<int>(WindowField(c.vm, ref, "bottom", "I").bits)};
                        };
                        s.system.SetInsets(Client(s, c.arguments[0].ref).window, c.arguments[1].AsInt(),
                                           rect(c.arguments[2].ref), rect(c.arguments[3].ref),
                                           ReadWindowRegion(c.vm, context, c.arguments[4].ref));
                        return V::Void();
                    }));
    b.VirtualMethod("setInTouchMode", "(Z)V", [context](dx::IntrinsicContext& c) {
        RomWindows(context).system.SetInTouchMode(c.arguments[0].AsInt() != 0);
        return V::Void();
    });
    b.VirtualMethod("getInTouchMode", "()Z",
                    [context](dx::IntrinsicContext&) { return V::Int(RomWindows(context).system.InTouchMode()); });
    b.VirtualMethod("performHapticFeedback", "(Landroid/view/IWindow;IZ)Z", [context](dx::IntrinsicContext& c) {
        Client(RomWindows(context), c.arguments[0].ref);
        return V::Int(0);
    });
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "dragRecipientEntered",
                                   "(Landroid/view/IWindow;)V");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "dragRecipientExited",
                                   "(Landroid/view/IWindow;)V");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "getWindowId",
                                   "(Landroid/os/IBinder;)Landroid/view/IWindowId;");
    b.VirtualMethod("onRectangleOnScreenRequested", "(Landroid/os/IBinder;Landroid/graphics/Rect;Z)V",
                    [](dx::IntrinsicContext&) { return V::Void(); });
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "outOfMemory", "(Landroid/view/IWindow;)Z");
    b.VirtualMethod("performDeferredDestroy", "(Landroid/view/IWindow;)V", [context](dx::IntrinsicContext& c) {
        Client(RomWindows(context), c.arguments[0].ref);
        return V::Void();
    });
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "performDrag",
                                   "(Landroid/view/IWindow;Landroid/os/IBinder;FFFFLandroid/content/ClipData;)Z");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "prepareDrag",
                                   "(Landroid/view/IWindow;IIILandroid/view/Surface;)Landroid/os/IBinder;");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "reportDropResult", "(Landroid/view/IWindow;Z)V");
    RejectUnsupportedServiceMethod(
        b, "android.view.IWindowSession", "sendWallpaperCommand",
        "(Landroid/os/IBinder;Ljava/lang/String;IIILandroid/os/Bundle;Z)Landroid/os/Bundle;");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "setUniverseTransform",
                                   "(Landroid/os/IBinder;FFFFFFF)V");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "setWallpaperPosition",
                                   "(Landroid/os/IBinder;FFFF)V");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "wallpaperCommandComplete",
                                   "(Landroid/os/IBinder;Landroid/os/Bundle;)V");
    RejectUnsupportedServiceMethod(b, "android.view.IWindowSession", "wallpaperOffsetsComplete",
                                   "(Landroid/os/IBinder;)V");
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
