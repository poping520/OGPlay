#include "rom_input.h"

#include <algorithm>
#include <limits>

namespace ogplay::runtime::android_intrinsics {
namespace {
using V = dx::VmValue;
using Ref = dx::VmObjectRef;
constexpr auto native = dx::kAccPrivate | dx::kAccNative;
constexpr auto static_native = native | dx::kAccStatic;
const auto null = V::Ref(Ref{});

int ChannelToken(dx::Interpreter& vm, Ref object) {
    return static_cast<int>(WindowField(vm, object, "mPtr", "I").bits);
}

void SetChannelToken(dx::Interpreter& vm, Ref object, int token) {
    WindowField(vm, object, "mPtr", "I") = {static_cast<unsigned>(token), dx::SlotTag::cat1};
}

RomInputState::Endpoint& Endpoint(RomInputState& s, int token) {
    const auto found = s.endpoints.find(token);
    if (found == s.endpoints.end())
        throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputChannel is disposed"};
    return found->second;
}

Ref Receiver(dx::Interpreter& vm, RomInputState::Receiver& r) {
    return CallAndroidMethod(vm, r.weak, "get", "()Ljava/lang/Object;").ref;
}

void InputCallback(dx::Interpreter& vm, const char* type, Ref receiver, const char* name, const char* signature,
                   std::vector<V> arguments) {
    const auto method = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor(type), name, signature);
    if (!method) throw std::logic_error("ROM input callback is unavailable: " + std::string(name));
    arguments.insert(arguments.begin(), V::Ref(receiver));
    const auto outcome = vm.Call(*method, arguments);
    if (outcome.exception.IsValid())
        throw dx::VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
}

void Wake(dx::Interpreter& vm, const Context& context, Ref queue, std::uint64_t thread) {
    const auto token = static_cast<unsigned>(ChannelToken(vm, queue));
    {
        std::scoped_lock lock(context->scheduler_mutex);
        if (auto q = context->message_queues.find(token); q != context->message_queues.end()) q->second->wake = true;
    }
    if (context->wake_native_looper) context->wake_native_looper(thread);
    context->scheduler_changed.notify_all();
}

void Deliver(dx::Interpreter& vm, const Context& context, RomInputState::Receiver& receiver,
             const RomInputState::Event& e) {
    const auto target = Receiver(vm, receiver);
    if (!target.IsValid()) return;
    const auto root = vm.ProtectReferences(std::array{target});
    const auto event = MakeInputEvent(vm, context, e.input);
    const auto event_root = vm.ProtectReferences(std::array{event});
    receiver.inflight.insert(e.sequence);
    ++receiver.channel->inflight;
    InputCallback(vm, "Landroid/view/InputEventReceiver;", target, "dispatchInputEvent",
                  "(ILandroid/view/InputEvent;)V", {V::Int(e.sequence), V::Ref(event)});
}

void ImeVisible(RomInputState& s, bool visible) {
    if (s.ime_visible == visible) return;
    s.ime_visible = visible;
    if (s.text_input) s.text_input(visible);
}

void ImeResult(dx::Interpreter& vm, Ref result, int code) {
    if (result.IsValid())
        static_cast<void>(CallAndroidMethod(vm, result, "send", "(ILandroid/os/Bundle;)V", {V::Int(code), null}));
}

Ref BindInput(dx::Interpreter& vm, RomInputState& s, Ref client, Ref context) {
    if (!s.ime_clients.contains(client.Value()))
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unregistered IME client"};
    if (s.active_client != client) ImeVisible(s, false);
    s.active_client = client;
    s.active_context = context;
    static_cast<void>(CallAndroidMethod(vm, client, "setActive", "(Z)V", {V::Int(1)}));
    if (!s.ime_method.IsValid())
        s.ime_method = NewPlatformObject(vm, "Lorg/ogplay/framework/LocalInputMethodSession;", "()V", {});
    if (!s.ime_channel.IsValid() || !ChannelToken(vm, s.ime_channel)) {
        s.ime_channel = NewPlatformObject(vm, "Landroid/view/InputChannel;", "()V", {});
        auto channel = std::make_shared<RomInputState::Channel>();
        channel->name = "local input method";
        channel->local_ime = true;
        const auto id = AllocateRomInputHandle(s);
        s.endpoints.emplace(id, RomInputState::Endpoint{std::move(channel), false});
        SetChannelToken(vm, s.ime_channel, id);
    }
    const auto method_id = vm.NewStringUtf8("local input method");
    const auto root = vm.ProtectReferences(std::array{method_id});
    return NewPlatformObject(
        vm, "Lcom/android/internal/view/InputBindResult;",
        "(Lcom/android/internal/view/IInputMethodSession;Landroid/view/InputChannel;Ljava/lang/String;I)V",
        {V::Ref(s.ime_method), V::Ref(s.ime_channel), V::Ref(method_id), V::Int(++s.ime_sequence)});
}
}  // namespace

RomInputState& RomInput(const Context& context) {
    if (!context->rom_input) {
        context->rom_input = std::make_shared<RomInputState>();
        context->rom_input->text_input = [weak = std::weak_ptr(context)](bool visible) {
            if (const auto owner = weak.lock()) owner->host_ime_visible.store(visible);
        };
    }
    return *context->rom_input;
}

int AllocateRomInputHandle(RomInputState& s) {
    if (s.next_handle == std::numeric_limits<int>::max())
        throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "input handle space exhausted"};
    return s.next_handle++;
}

int AllocateRomInputSequence(RomInputState& s) {
    if (s.next_sequence == std::numeric_limits<int>::max())
        throw dx::VmJavaThrow{"Ljava/lang/OutOfMemoryError;", "input sequence space exhausted"};
    return s.next_sequence++;
}

void VisitRomInputRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit) {
    if (!context.rom_input) return;
    const auto& s = *context.rom_input;
    const auto root = [&](Ref ref) {
        if (ref.IsValid()) visit(ref);
    };
    for (const auto& [_, r] : s.receivers) {
        root(r.weak);
        root(r.queue);
    }
    for (const auto& [_, sender] : s.ime_senders) {
        root(sender.weak);
        root(sender.queue);
    }
    for (const auto& [_, q] : s.native_queues) {
        root(q->weak);
        root(q->queue);
    }
    for (const auto& [_, c] : s.ime_clients) {
        root(c.client);
        root(c.context);
    }
    root(s.active_client);
    root(s.active_context);
    root(s.focused_window);
    root(s.window_focus);
    root(s.ime_method);
    root(s.ime_channel);
}

int CreateWindowInputChannel(dx::Interpreter& vm, const Context& context, Ref out) {
    auto& s = RomInput(context);
    const auto server = AllocateRomInputHandle(s), client = AllocateRomInputHandle(s);
    auto channel = std::make_shared<RomInputState::Channel>();
    channel->name = "window";
    s.endpoints.emplace(server, RomInputState::Endpoint{channel, true});
    s.endpoints.emplace(client, RomInputState::Endpoint{channel, false});
    SetChannelToken(vm, out, client);
    return server;
}

void CloseWindowInputChannel(const Context& context, int token) {
    if (context->rom_input) context->rom_input->endpoints.erase(token);
}

void DispatchRomWindowInput(dx::Interpreter& vm, const Context& context, const AndroidBoundaryInput& input) {
    QueueRomWindowInput(vm, context, RomWindows(context).system.Dispatch(input));
}

void QueueRomWindowInput(dx::Interpreter& vm, const Context& context, std::span<const WindowInputDelivery> deliveries) {
    if (deliveries.empty()) return;
    auto& w = RomWindows(context);
    auto& s = RomInput(context);
    for (const auto& delivery : deliveries) {
        const auto client =
            std::ranges::find_if(w.clients, [&](const auto& p) { return p.second.window == delivery.window; });
        if (client == w.clients.end() || client->second.channel == 0) continue;
        auto& endpoint = Endpoint(s, client->second.channel);
        if (endpoint.channel->pending.size() + endpoint.channel->inflight >= 4096)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "input dispatch queue budget exhausted"};
        endpoint.channel->pending.push_back({AllocateRomInputSequence(s), delivery.input});
        for (auto& [_, r] : s.receivers)
            if (r.channel == endpoint.channel) Wake(vm, context, r.queue, r.thread);
    }
}

void PumpRomInputEvents(dx::Interpreter& vm, const Context& context) {
    if (!context->rom_input) return;
    auto& s = *context->rom_input;
    std::vector<int> senders;
    for (const auto& [id, sender] : s.ime_senders)
        if (sender.thread == vm.CurrentContextToken() && !sender.pending.empty()) senders.push_back(id);
    for (const auto id : senders) {
        auto found = s.ime_senders.find(id);
        if (found == s.ime_senders.end()) continue;
        auto pending = std::exchange(found->second.pending, {});
        const auto owner = CallAndroidMethod(vm, found->second.weak, "get", "()Ljava/lang/Object;").ref;
        if (!owner.IsValid()) continue;
        const auto root = vm.ProtectReferences(std::array{owner});
        // SDL produces composed text separately. Unhandled physical events continue through ViewRootImpl.
        for (const auto sequence : pending) {
            if (!s.ime_senders.contains(id)) break;
            InputCallback(vm, "Landroid/view/InputEventSender;", owner, "dispatchInputEventFinished", "(IZ)V",
                          {V::Int(sequence), V::Int(0)});
        }
    }
    std::vector<std::shared_ptr<RomInputState::NativeQueue>> queues;
    for (const auto& [_, q] : s.native_queues)
        if (q->thread == vm.CurrentContextToken()) queues.push_back(q);
    for (const auto& q : queues) {
        std::deque<std::pair<int, bool>> finished;
        {
            std::scoped_lock lock(q->mutex);
            finished.swap(q->finished);
        }
        if (finished.empty()) continue;
        const auto owner = CallAndroidMethod(vm, q->weak, "get", "()Ljava/lang/Object;").ref;
        if (!owner.IsValid()) continue;
        const auto root = vm.ProtectReferences(std::array{owner});
        for (const auto& [id, handled] : finished)
            InputCallback(vm, "Landroid/view/InputQueue;", owner, "finishInputEvent", "(IZ)V",
                          {V::Int(id), V::Int(handled)});
    }
    std::vector<int> tokens;
    for (const auto& [id, r] : s.receivers)
        if (r.thread == vm.CurrentContextToken()) tokens.push_back(id);
    for (const int id : tokens) {
        // Java dispatch may dispose the receiver; refetch after each callback.
        for (;;) {
            auto found = s.receivers.find(id);
            if (found == s.receivers.end()) break;
            auto& r = found->second;
            auto& pending = r.channel->pending;
            if (pending.empty()) break;
            if ((pending.front().input.action & 255) == 2) {
                if (!r.channel->batch_notified) {
                    r.channel->batch_notified = true;
                    const auto target = Receiver(vm, r);
                    if (target.IsValid())
                        InputCallback(vm, "Landroid/view/InputEventReceiver;", target,
                                      "dispatchBatchedInputEventPending", "()V", {});
                }
                break;
            }
            const auto e = pending.front();
            pending.pop_front();
            Deliver(vm, context, r, e);
        }
    }
}

void CommitRomInputText(dx::Interpreter& vm, const Context& context, const std::u16string& text, bool composing) {
    CommitRomInputText(vm, context, vm.Model().NewString(text), composing);
}

void CommitRomInputText(dx::Interpreter& vm, const Context& context, dx::VmObjectRef string, bool composing) {
    auto& s = RomInput(context);
    if (!s.ime_visible || !s.active_context.IsValid()) return;
    const auto target = s.active_context;
    const auto roots = vm.ProtectReferences(std::array{target, string});
    static_cast<void>(CallAndroidMethod(vm, target, composing ? "setComposingText" : "commitText",
                                        "(Ljava/lang/CharSequence;I)V", {V::Ref(string), V::Int(1)}));
}

void SetRomInputWindowFocus(dx::Interpreter& vm, const Context& context, Ref window) {
    if (!context->rom_input) return;
    auto& s = *context->rom_input;
    s.window_focus = window;
    if (s.focused_window != window) {
        ImeVisible(s, false);
        if (s.active_client.IsValid())
            static_cast<void>(CallAndroidMethod(vm, s.active_client, "setActive", "(Z)V", {V::Int(0)}));
        s.active_context = Ref{};
    }
}

Decl Declare_android_view_InputChannel_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/InputChannel;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeOpenInputChannelPair", "(Ljava/lang/String;)[Landroid/view/InputChannel;",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto name = c.vm.StringUtf8(dx::IntrinsicCall(c).NonNullRef(0, "name"));
            const auto pair =
                c.vm.Model().NewObjectArray(c.vm.Linker().ResolveDescriptor("[Landroid/view/InputChannel;"),
                                            c.vm.Linker().ResolveDescriptor("Landroid/view/InputChannel;"), 2);
            const auto root = c.vm.ProtectReferences(std::array{pair});
            auto channel = std::make_shared<RomInputState::Channel>();
            channel->name = name;
            for (int i = 0; i < 2; ++i) {
                const auto object = NewPlatformObject(c.vm, "Landroid/view/InputChannel;", "()V", {});
                const auto id = AllocateRomInputHandle(s);
                s.endpoints.emplace(id, RomInputState::Endpoint{channel, i == 0});
                SetChannelToken(c.vm, object, id);
                c.vm.Model().SetObjectElement(pair, i, object);
            }
            return V::Ref(pair);
        },
        static_native);
    b.DirectMethod(
        "nativeDispose", "(Z)V",
        [context](dx::IntrinsicContext& c) {
            RomInput(context).endpoints.erase(ChannelToken(c.vm, c.receiver));
            SetChannelToken(c.vm, c.receiver, 0);
            return V::Void();
        },
        native);
    b.DirectMethod(
        "nativeGetName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& c) {
            const auto id = ChannelToken(c.vm, c.receiver);
            return id ? V::Ref(c.vm.NewStringUtf8(Endpoint(RomInput(context), id).channel->name)) : null;
        },
        native);
    for (bool dup : {false, true})
        b.DirectMethod(
            dup ? "nativeDup" : "nativeTransferTo", "(Landroid/view/InputChannel;)V",
            [context, dup](dx::IntrinsicContext& c) {
                auto& s = RomInput(context);
                const auto target = dx::IntrinsicCall(c).NonNullRef(0, "other");
                if (ChannelToken(c.vm, target))
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "target channel is initialized"};
                const auto source = ChannelToken(c.vm, c.receiver);
                const auto endpoint = Endpoint(s, source);
                const auto id = dup ? AllocateRomInputHandle(s) : source;
                if (dup)
                    s.endpoints.emplace(id, endpoint);
                else
                    SetChannelToken(c.vm, c.receiver, 0);
                SetChannelToken(c.vm, target, id);
                return V::Void();
            },
            native);
    return std::move(b).Build();
}

Decl Declare_android_view_InputEventReceiver_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/InputEventReceiver;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeInit", "(Ljava/lang/ref/WeakReference;Landroid/view/InputChannel;Landroid/os/MessageQueue;)I",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto queue = dx::IntrinsicCall(c).NonNullRef(2, "queue");
            const auto channel = Endpoint(s, ChannelToken(c.vm, c.arguments[1].ref));
            if (channel.server)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "receiver requires client endpoint"};
            if (std::ranges::any_of(s.receivers,
                                    [&](const auto& item) { return item.second.channel == channel.channel; }))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "InputChannel already has a receiver"};
            const auto q = static_cast<unsigned>(ChannelToken(c.vm, queue));
            {
                std::scoped_lock lock(context->scheduler_mutex);
                const auto found = context->message_queues.find(q);
                if (found == context->message_queues.end() ||
                    found->second->context_token != c.vm.CurrentContextToken())
                    throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                          "receiver requires its own queue thread"};
            }
            const auto id = AllocateRomInputHandle(s);
            RomInputState::Receiver r;
            r.weak = c.arguments[0].ref;
            r.queue = queue;
            r.thread = c.vm.CurrentContextToken();
            r.channel = channel.channel;
            s.receivers.emplace(id, std::move(r));
            return V::Int(id);
        },
        static_native);
    b.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto r = s.receivers.find(c.arguments[0].AsInt());
            if (r != s.receivers.end()) {
                r->second.channel->inflight -= r->second.inflight.size();
                s.receivers.erase(r);
            }
            return V::Void();
        },
        static_native);
    b.StaticMethod(
        "nativeFinishInputEvent", "(IIZ)V",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto r = s.receivers.find(c.arguments[0].AsInt());
            if (r == s.receivers.end() || !r->second.inflight.erase(c.arguments[1].AsInt()))
                throw dx::VmJavaThrow{"Ljava/lang/IllegalStateException;", "unknown input sequence"};
            --r->second.channel->inflight;
            return V::Void();
        },
        static_native);
    b.StaticMethod(
        "nativeConsumeBatchedInputEvents", "(IJ)Z",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const int id = c.arguments[0].AsInt();
            const auto time = c.arguments[1].AsLong();
            bool consumed = false;
            while (s.receivers.contains(id)) {
                auto& r = s.receivers.at(id);
                auto& queue = r.channel->pending;
                r.channel->batch_notified = false;
                if (queue.empty() || (queue.front().input.action & 255) != 2 ||
                    (time >= 0 && queue.front().input.event_time_ns > time))
                    break;
                auto e = queue.front();
                queue.pop_front();
                while (!queue.empty() && (queue.front().input.action & 255) == 2 &&
                       queue.front().input.device_id == e.input.device_id &&
                       queue.front().input.source == e.input.source && queue.front().input.flags == e.input.flags &&
                       queue.front().input.button_state == e.input.button_state &&
                       e.input.history.size() + queue.front().input.history.size() < 255 &&
                       std::ranges::equal(
                           queue.front().input.pointers, e.input.pointers,
                           [](const auto& a, const auto& b) { return a.id == b.id && a.tool_type == b.tool_type; }) &&
                       (time < 0 || queue.front().input.event_time_ns <= time)) {
                    auto next = queue.front();
                    queue.pop_front();
                    auto history = std::move(e.input.history);
                    history.push_back({e.input.event_time_ns, std::move(e.input.pointers)});
                    history.insert(history.end(), next.input.history.begin(), next.input.history.end());
                    next.input.history = std::move(history);
                    e = std::move(next);
                }
                consumed = true;
                Deliver(c.vm, context, r, e);
            }
            return V::Int(consumed);
        },
        static_native);
    return std::move(b).Build();
}

Decl Declare_android_view_InputEventSender_Natives(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/view/InputEventSender;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeInit", "(Ljava/lang/ref/WeakReference;Landroid/view/InputChannel;Landroid/os/MessageQueue;)I",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            const auto channel = dx::IntrinsicCall(c).NonNullRef(1, "input channel");
            if (!Endpoint(s, ChannelToken(c.vm, channel)).channel->local_ime)
                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "unsupported input sender channel"};
            const auto id = AllocateRomInputHandle(s);
            s.ime_senders.emplace(
                id, RomInputState::ImeSender{c.arguments[0].ref, c.arguments[2].ref, c.vm.CurrentContextToken(), {}});
            return V::Int(id);
        },
        static_native);
    b.StaticMethod(
        "nativeDispose", "(I)V",
        [context](dx::IntrinsicContext& c) {
            RomInput(context).ime_senders.erase(c.arguments[0].AsInt());
            return V::Void();
        },
        static_native);
    for (bool key : {true, false})
        b.StaticMethod(
            key ? "nativeSendKeyEvent" : "nativeSendMotionEvent",
            key ? "(IILandroid/view/KeyEvent;)Z" : "(IILandroid/view/MotionEvent;)Z",
            [context](dx::IntrinsicContext& c) {
                auto& s = RomInput(context);
                const auto found = s.ime_senders.find(c.arguments[0].AsInt());
                if (found == s.ime_senders.end() || found->second.pending.size() >= 4096) return V::Int(0);
                auto& sender = found->second;
                sender.pending.push_back(c.arguments[1].AsInt());
                Wake(c.vm, context, sender.queue, sender.thread);
                return V::Int(1);
            },
            static_native);
    return std::move(b).Build();
}

Decl Declare_local_InputMethodSession_Class(const Context& context) {
    constexpr auto stub = "Lcom/android/internal/view/IInputMethodSession$Stub;";
    auto b = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalInputMethodSession;", stub);
    b.Constructor("()V", [](dx::IntrinsicContext& c) {
        const auto ctor = c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor(stub), "<init>", "()V");
        const auto result = c.vm.Call(*ctor, std::array{V::Ref(c.receiver)});
        if (result.exception.IsValid()) throw dx::VmJavaThrow{"", "", result.exception};
        return V::Void();
    });
    for (const auto& [name, signature] :
         std::array{std::pair{"finishInput", "()V"}, std::pair{"finishSession", "()V"},
                    std::pair{"updateSelection", "(IIIIII)V"}, std::pair{"viewClicked", "(Z)V"},
                    std::pair{"updateExtractedText", "(ILandroid/view/inputmethod/ExtractedText;)V"},
                    std::pair{"updateCursor", "(Landroid/graphics/Rect;)V"},
                    std::pair{"displayCompletions", "([Landroid/view/inputmethod/CompletionInfo;)V"}})
        b.VirtualMethod(name, signature, [](dx::IntrinsicContext&) { return V::Void(); });
    b.VirtualMethod("appPrivateCommand", "(Ljava/lang/String;Landroid/os/Bundle;)V", [](dx::IntrinsicContext& call) {
        RecordUiDegraded(call.vm, "Input method private command");
        return V::Void();
    });
    b.VirtualMethod("toggleSoftInput", "(II)V", [context](dx::IntrinsicContext&) {
        auto& s = RomInput(context);
        if (s.active_context.IsValid()) ImeVisible(s, !s.ime_visible);
        return V::Void();
    });
    return std::move(b).Build();
}

Decl Declare_local_InputMethodService_Class(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalInputMethodService;",
                                              "Lcom/android/internal/view/IInputMethodManager$Stub;");
    b.VirtualMethod("addClient",
                    "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;II)V",
                    [context](dx::IntrinsicContext& c) {
                        auto client = dx::IntrinsicCall(c).NonNullRef(0, "client");
                        RomInput(context).ime_clients.insert_or_assign(
                            client.Value(), RomInputState::ImeClient{client, c.arguments[1].ref});
                        return V::Void();
                    });
    b.VirtualMethod("removeClient", "(Lcom/android/internal/view/IInputMethodClient;)V",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = RomInput(context);
                        if (s.active_client == c.arguments[0].ref) {
                            ImeVisible(s, false);
                            s.active_client = Ref{};
                            s.active_context = Ref{};
                        }
                        s.ime_clients.erase(c.arguments[0].ref.Value());
                        return V::Void();
                    });
    b.VirtualMethod(
        "startInput",
        "(Lcom/android/internal/view/IInputMethodClient;Lcom/android/internal/view/IInputContext;Landroid/view/inputmethod/EditorInfo;I)Lcom/android/internal/view/InputBindResult;",
        [context](dx::IntrinsicContext& c) {
            return V::Ref(BindInput(c.vm, RomInput(context), c.arguments[0].ref, c.arguments[1].ref));
        });
    b.VirtualMethod(
        "windowGainedFocus",
        "(Lcom/android/internal/view/IInputMethodClient;Landroid/os/IBinder;IIILandroid/view/inputmethod/EditorInfo;Lcom/android/internal/view/IInputContext;)Lcom/android/internal/view/InputBindResult;",
        [context](dx::IntrinsicContext& c) {
            auto& s = RomInput(context);
            s.focused_window = c.arguments[1].ref;
            return V::Ref(BindInput(c.vm, s, c.arguments[0].ref, c.arguments[6].ref));
        });
    b.VirtualMethod("finishInput", "(Lcom/android/internal/view/IInputMethodClient;)V",
                    [context](dx::IntrinsicContext& c) {
                        auto& s = RomInput(context);
                        if (s.active_client == c.arguments[0].ref) {
                            ImeVisible(s, false);
                            s.active_context = Ref{};
                        }
                        return V::Void();
                    });
    for (bool show : {false, true})
        b.VirtualMethod(show ? "showSoftInput" : "hideSoftInput",
                        "(Lcom/android/internal/view/IInputMethodClient;ILandroid/os/ResultReceiver;)Z",
                        [context, show](dx::IntrinsicContext& c) {
                            auto& s = RomInput(context);
                            if (s.active_client != c.arguments[0].ref || !s.active_context.IsValid()) return V::Int(0);
                            if (show && !s.text_input) {
                                if (auto* ledger = c.vm.Ledger())
                                    ledger->RecordUnimplemented("dexvm.input_method_sessions", 0);
                                throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                      "host text input is not connected"};
                            }
                            const bool before = s.ime_visible;
                            ImeVisible(s, show);
                            ImeResult(c.vm, c.arguments[2].ref, before == show ? (show ? 0 : 1) : (show ? 2 : 3));
                            return V::Int(1);
                        });
    for (const auto* name : {"getInputMethodList", "getEnabledInputMethodList", "getShortcutInputMethodsAndSubtypes"})
        b.VirtualMethod(name, "()Ljava/util/List;", [](dx::IntrinsicContext& c) {
            return V::Ref(NewPlatformObject(c.vm, "Ljava/util/ArrayList;", "()V", {}));
        });
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "getCurrentInputMethodSubtype",
                                   "()Landroid/view/inputmethod/InputMethodSubtype;");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "getEnabledInputMethodSubtypeList", "(Ljava/lang/String;Z)Ljava/util/List;");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "getLastInputMethodSubtype",
                                   "()Landroid/view/inputmethod/InputMethodSubtype;");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "hideMySoftInput",
                                   "(Landroid/os/IBinder;I)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "notifySuggestionPicked",
                                   "(Landroid/text/style/SuggestionSpan;Ljava/lang/String;I)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "registerSuggestionSpansForNotification", "([Landroid/text/style/SuggestionSpan;)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "setAdditionalInputMethodSubtypes",
                                   "(Ljava/lang/String;[Landroid/view/inputmethod/InputMethodSubtype;)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "setCurrentInputMethodSubtype",
                                   "(Landroid/view/inputmethod/InputMethodSubtype;)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "setImeWindowStatus",
                                   "(Landroid/os/IBinder;II)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "setInputMethod",
                                   "(Landroid/os/IBinder;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(
        b, "com.android.internal.view.IInputMethodManager", "setInputMethodAndSubtype",
        "(Landroid/os/IBinder;Ljava/lang/String;Landroid/view/inputmethod/InputMethodSubtype;)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "setInputMethodEnabled",
                                   "(Ljava/lang/String;Z)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "shouldOfferSwitchingToNextInputMethod", "(Landroid/os/IBinder;)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "showInputMethodAndSubtypeEnablerFromClient",
                                   "(Lcom/android/internal/view/IInputMethodClient;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager",
                                   "showInputMethodPickerFromClient",
                                   "(Lcom/android/internal/view/IInputMethodClient;)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "showMySoftInput",
                                   "(Landroid/os/IBinder;I)V");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "switchToLastInputMethod",
                                   "(Landroid/os/IBinder;)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "switchToNextInputMethod",
                                   "(Landroid/os/IBinder;Z)Z");
    RejectUnsupportedServiceMethod(b, "com.android.internal.view.IInputMethodManager", "updateStatusIcon",
                                   "(Landroid/os/IBinder;Ljava/lang/String;I)V");
    return std::move(b).Build();
}

Decl Declare_local_InputService_Class(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Lorg/ogplay/framework/LocalInputService;",
                                              "Landroid/hardware/input/IInputManager$Stub;");
    // The existing device factory already constructs genuine ROM InputDevice values.
    b.VirtualMethod("getInputDevice", "(I)Landroid/view/InputDevice;",
                    [context](dx::IntrinsicContext& c) { return MakeInputDevice(c, context); });
    b.VirtualMethod("getInputDeviceIds", "()[I", [context](dx::IntrinsicContext& c) {
        const auto ids =
            c.vm.Model().NewPrimitiveArray(c.vm.Linker().ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                                           static_cast<JniSize>(context->input_devices.size()));
        for (std::size_t i = 0; i < context->input_devices.size(); ++i)
            c.vm.Model().SetPrimitiveElement(ids, static_cast<JniSize>(i),
                                             static_cast<unsigned>(context->input_devices[i].id));
        return V::Ref(ids);
    });
    b.VirtualMethod("registerInputDevicesChangedListener", "(Landroid/hardware/input/IInputDevicesChangedListener;)V",
                    [](dx::IntrinsicContext&) { return V::Void(); });
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "addKeyboardLayoutForInputDevice",
                                   "(Ljava/lang/String;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "cancelVibrate",
                                   "(ILandroid/os/IBinder;)V");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "getCurrentKeyboardLayoutForInputDevice",
                                   "(Ljava/lang/String;)Ljava/lang/String;");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "getKeyboardLayout",
                                   "(Ljava/lang/String;)Landroid/hardware/input/KeyboardLayout;");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "getKeyboardLayouts",
                                   "()[Landroid/hardware/input/KeyboardLayout;");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "getKeyboardLayoutsForInputDevice",
                                   "(Ljava/lang/String;)[Ljava/lang/String;");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "hasKeys", "(II[I[Z)Z");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "injectInputEvent",
                                   "(Landroid/view/InputEvent;I)Z");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "removeKeyboardLayoutForInputDevice",
                                   "(Ljava/lang/String;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "setCurrentKeyboardLayoutForInputDevice",
                                   "(Ljava/lang/String;Ljava/lang/String;)V");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "tryPointerSpeed", "(I)V");
    RejectUnsupportedServiceMethod(b, "android.hardware.input.IInputManager", "vibrate", "(I[JILandroid/os/IBinder;)V");
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::android_intrinsics
