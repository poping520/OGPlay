#include "ogplay/runtime/integration/dexvm_bridge.h"

#include <cstring>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "dexvm_android/shared.h"
#include "ogplay/runtime/jni/jni_invocation.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kRootThreadId = 1;

namespace dx = ogplay::runtime::dexvm;

struct DescriptorWalk final {
    const std::string& descriptor;
    std::size_t index{1};  // past '('

    [[nodiscard]] bool Done() const { return descriptor[index] == ')'; }
    [[nodiscard]] char Next() {
        const char shorty = descriptor[index];
        if (shorty == 'L' || shorty == '[') {
      while (descriptor[index] == '[')
        ++index;
            if (descriptor[index] == 'L') {
                index = descriptor.find(';', index) + 1;
            } else {
                ++index;
            }
            return 'L';
        }
        ++index;
        return shorty;
    }
};

[[nodiscard]] char ReturnShorty(const std::string& descriptor) {
    const auto close = descriptor.find(')');
    const char first = descriptor[close + 1];
    return (first == '[' ) ? 'L' : first;
}

void BindPlatformCoreHandlers(
    std::vector<dx::IntrinsicClassDecl>& catalog,
    const std::shared_ptr<DexVmAndroidContext>& android_context) {
    struct Binding final {
        std::string_view owner;
        std::string_view name;
        std::string_view descriptor;
        dx::IntrinsicHandler implementation;
    };
    const Binding bindings[] = {
        {"Ljava/lang/System;", "currentTimeMillis", "()J",
         android_intrinsics::PlatformSystemCurrentTimeMillisHandler(
             android_context)},
        {"Ljava/lang/System;", "nanoTime", "()J",
         android_intrinsics::PlatformSystemNanoTimeHandler(android_context)},
        {"Ljava/lang/System;", "load", "(Ljava/lang/String;)V",
         android_intrinsics::PlatformSystemLoadHandler(android_context)},
        {"Ljava/lang/System;", "loadLibrary", "(Ljava/lang/String;)V",
         android_intrinsics::PlatformSystemLoadLibraryHandler(android_context)},
        {"Ljava/lang/System;", "exit", "(I)V",
         android_intrinsics::PlatformSystemExitHandler(android_context)},
        {"Ljava/util/Date;", "<init>", "()V",
         android_intrinsics::PlatformDateInitHandler(android_context)},
        {"Ljava/util/Date;", "getTime", "()J",
         android_intrinsics::PlatformDateGetTimeHandler()},
        {"Ljava/util/Date;", "getYear", "()I",
         android_intrinsics::PlatformDateGetYearHandler()},
    };
    for (const auto& binding : bindings) {
        bool found = false;
        for (auto& declaration : catalog) {
            if (declaration.descriptor != binding.owner) continue;
            for (auto& method : declaration.methods) {
                if (method.name == binding.name &&
                    method.descriptor == binding.descriptor) {
                    method.implementation = binding.implementation;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            throw DexVmBridgeError(
                "platform core handler target is not declared: " +
                std::string(binding.owner) + "." +
                std::string(binding.name) +
                std::string(binding.descriptor));
        }
    }
}

void VisitAndroidSessionRoots(const DexVmAndroidContext& context,
                              const dx::VmRootVisitor& visit) {
    const auto root = [&](const dx::VmObjectRef ref) {
        if (ref.IsValid()) visit(ref);
    };
    const auto key_root = [&](const auto& table) {
        for (const auto& [handle, _] : table) {
            root(dx::VmObjectRef(static_cast<std::uint32_t>(handle)));
        }
    };
    root(context.activity);
    root(context.application);
    root(context.application_base_context);
    root(context.renderer);
    root(context.content_view);
    root(context.current_intent);
    for (const auto& [_, value] : context.singletons) root(value);
    for (const auto& [_, values] : context.bundles) {
        for (const auto& [__, value] : values) {
            if (const auto* ref = std::get_if<dx::VmObjectRef>(&value)) {
                root(*ref);
            }
        }
    }
    for (const auto& [owner, holder] : context.surface_holders) {
        root(dx::VmObjectRef(owner));
        root(holder);
    }
    for (const auto& [owner, callbacks] : context.surface_callbacks) {
        root(dx::VmObjectRef(owner));
        for (const auto callback : callbacks) root(callback);
    }
    root(context.egl.display);
    root(context.egl.config);
    root(context.egl.no_display);
    root(context.egl.no_context);
    root(context.egl.no_surface);
    root(context.egl.window_surface);
    root(context.egl.current_display);
    root(context.egl.current_surface);
    root(context.egl.current_context);
    key_root(context.egl.contexts);
    const auto visit_ref_map = [&](const auto& table) {
        for (const auto& [_, ref] : table) root(ref);
    };
    visit_ref_map(context.view_tree_observers);
    visit_ref_map(context.global_layout_listeners);
    visit_ref_map(context.sax_content_handlers);
    for (const auto& [owner, state] : context.java_threads) {
        root(dx::VmObjectRef(owner));
        root(state.runnable);
    }
    for (const auto task : context.java_thread_queue) root(task);
    visit_ref_map(context.ui_node_to_object);
    visit_ref_map(context.ui_click_listeners);
    visit_ref_map(context.ui_touch_listeners);
    visit_ref_map(context.ui_view_layout_params);
    visit_ref_map(context.video_completion);
    visit_ref_map(context.video_errors);

    // Host side state keyed by a VM owner is logically part of that owner.
    // Keep the owner live until the explicit close/recycle/lifecycle path
    // removes its state; this closes the pre-GC raw-handle compatibility gap.
    key_root(context.streams);
    key_root(context.output_streams);
    key_root(context.zip_streams);
    key_root(context.preference_names);
    key_root(context.editable_owner);
    key_root(context.telephony_listeners);
    key_root(context.broadcast_receivers);
    key_root(context.bitmaps);
    key_root(context.media_resources);
    key_root(context.media_playing);
    key_root(context.media_looping);
    key_root(context.intent_components);
    key_root(context.intent_string_extras);
    key_root(context.intent_int_extras);
    for (const auto& [handle, _] : context.object_to_ui_node) {
        root(dx::VmObjectRef(static_cast<std::uint32_t>(handle)));
    }
    key_root(context.ui_layout_params);
    key_root(context.ui_image_scale_types);
    for (const auto handle : context.pending_video_completion) {
        root(dx::VmObjectRef(static_cast<std::uint32_t>(handle)));
    }
    for (const auto& [handle, _] : context.video_views) {
        root(dx::VmObjectRef(static_cast<std::uint32_t>(handle)));
    }
}

}  // namespace

class DexVmGuestBridge::Impl final {
public:
    AndroidGuestCallSession* session{};
    core::CapabilityLedger* ledger{};
    core::Logger* logger{};
    DexVmBridgeConfig config;

    dx::DexClassLinker linker;
    std::unique_ptr<dx::JavaObjectModel> model;
    std::unique_ptr<dx::Interpreter> vm;
    // Declared after vm so destruction stops every guest Java thread before
    // the interpreter and object model go away.
    std::unique_ptr<dx::VmThreadRuntime> threads;

    std::unordered_map<std::uint32_t, JniObjectIdentity> class_identities;
    std::unordered_map<std::uint32_t, JniReference> class_global_refs;
  std::unordered_set<std::uint32_t> registering_classes;

    DexVmGuestBridge* owner{};

    // ---- reference conversion ------------------------------------------

    [[nodiscard]] JniReference PublishLocal(const dx::VmObjectRef ref) {
    if (!ref.IsValid())
      return JniReference{};
    return session->Environment().PublishLocalObject(kRootThreadId,
                                                     model->ToIdentity(ref));
    }

    [[nodiscard]] dx::VmObjectRef FromReference(const JniReference reference,
                                                const std::uint64_t thread) {
    if (reference.IsNull())
      return dx::VmObjectRef{};
        const auto identity =
            session->Environment().ResolveObjectForHle(thread, reference);
        if (!identity.has_value()) {
      throw DexVmBridgeError("dexvm bridge cannot resolve a JNI reference");
        }
        return model->FromIdentity(*identity);
    }

    // ---- inbound (native -> interpreter) --------------------------------

  void PublishMissingMethods(const dx::DexClassId class_id,
                             const JniObjectIdentity identity) {
    auto &classes = session->Classes();
    auto &invocations = session->Invocations();
    for (const auto method_id : linker.MethodsOf(class_id)) {
      const auto &method = linker.Method(method_id);
      if (method.name == "<clinit>")
        continue;
      const auto existing = classes.GetMethodId(
          identity, method.name, method.descriptor, method.is_static);
      if (existing.has_value() &&
          classes.ResolveMethod(*existing).declaring_class == identity) {
        continue;
      }
      const auto implementation = "dexvm.m" + std::to_string(method_id.Value());
      try {
        static_cast<void>(classes.RegisterMethod(
            identity, {method.name, method.descriptor, implementation,
                       method.is_static}));
      } catch (const JniClassRegistryError &error) {
        throw DexVmBridgeError("cannot publish DexVM method to JNI: " +
                               linker.Class(class_id).descriptor + "." +
                               method.name + method.descriptor + ": " +
                               error.what());
      }
      invocations.RegisterHandler(
          implementation, [this, method_id](const JniInvocation &invocation) {
            return InvokeInterpreted(method_id, invocation);
          });
    }
  }

  [[nodiscard]] JniObjectIdentity
  RegisterClassForNative(const dx::DexClassId class_id) {
    auto &classes = session->Classes();
    auto &invocations = session->Invocations();
    const auto &linked = linker.Class(class_id);
    if (linked.is_array) {
      throw DexVmBridgeError("dexvm bridge cannot register an array JNI class");
    }
    const auto name = linked.descriptor.substr(1, linked.descriptor.size() - 2);
    if (const auto existing = classes.FindClass(name); existing.has_value()) {
      class_identities.emplace(class_id.Value(), *existing);
      PublishMissingMethods(class_id, *existing);
      return *existing;
    }
    if (!registering_classes.emplace(class_id.Value()).second) {
      throw DexVmBridgeError("dexvm JNI class hierarchy contains a cycle");
    }
    JniClassDeclaration declaration;
    declaration.name = name;
    if (linked.super.has_value()) {
      const auto &super = linker.Class(*linked.super);
      if (!super.is_array) {
        static_cast<void>(RegisterClassForNative(*linked.super));
        declaration.superclass =
            super.descriptor.substr(1, super.descriptor.size() - 2);
      }
    }
    std::vector<std::pair<std::string, dx::VmMethodId>> handlers;
    for (const auto method_id : linker.MethodsOf(class_id)) {
      const auto &method = linker.Method(method_id);
      if (method.name == "<clinit>")
        continue;
      const auto implementation = "dexvm.m" + std::to_string(method_id.Value());
      declaration.methods.push_back(
          {method.name, method.descriptor, implementation, method.is_static});
      handlers.emplace_back(implementation, method_id);
    }
    JniObjectIdentity identity;
    try {
      identity = classes.RegisterClass(declaration);
    } catch (const JniClassRegistryError &error) {
      throw DexVmBridgeError("cannot publish DexVM class to JNI: " + name +
                             ": " + error.what());
    }
    registering_classes.erase(class_id.Value());
    class_identities.emplace(class_id.Value(), identity);
    for (const auto &[implementation, method_id] : handlers) {
      invocations.RegisterHandler(
          implementation, [this, method_id](const JniInvocation &invocation) {
            return InvokeInterpreted(method_id, invocation);
          });
    }
    return identity;
  }

  [[nodiscard]] JniReference
  GlobalClassReference(const dx::DexClassId class_id) {
    if (const auto found = class_global_refs.find(class_id.Value());
        found != class_global_refs.end()) {
      return found->second;
    }
    auto identity = class_identities.find(class_id.Value());
    if (identity == class_identities.end()) {
      static_cast<void>(RegisterClassForNative(class_id));
      identity = class_identities.find(class_id.Value());
    }
    if (identity == class_identities.end()) {
      throw DexVmBridgeError("dexvm bridge cannot publish a class reference");
    }
    const auto reference = session->Environment().PublishGlobalObjectForHle(
        kRootThreadId, identity->second);
    class_global_refs.emplace(class_id.Value(), reference);
    return reference;
  }

    void RegisterDexClasses() {
    // Native JNI sees the same code-defined intrinsic classes as the
    // interpreter. Existing session platform classes retain ownership;
    // missing classes are registered with handlers that route back into
    // the linked VM method. This keeps FindClass/GetMethodID honest and
    // avoids a second, title-specific platform registry.
    for (const auto class_id : linker.AllClasses()) {
      const auto &linked = linker.Class(class_id);
      if (!linked.is_intrinsic || linked.is_array)
        continue;
      static_cast<void>(RegisterClassForNative(class_id));
    }

        auto& classes = session->Classes();
        auto& invocations = session->Invocations();
        for (const auto class_id : linker.AllClasses()) {
            const auto& linked = linker.Class(class_id);
      if (linked.is_intrinsic || linked.is_array)
        continue;
            JniClassDeclaration declaration;
            declaration.name =
                linked.descriptor.substr(1, linked.descriptor.size() - 2);
            if (linked.super.has_value()) {
                const auto& super = linker.Class(*linked.super);
                if (!super.is_intrinsic) {
          declaration.superclass =
              super.descriptor.substr(1, super.descriptor.size() - 2);
                }
            }
            std::vector<std::pair<std::string, dx::VmMethodId>> handlers;
            for (const auto method_id : linker.MethodsOf(class_id)) {
                const auto& method = linker.Method(method_id);
                if (method.kind != dx::MethodKind::interpreted ||
                    method.name == "<clinit>") {
                    continue;
                }
                const auto implementation =
                    "dexvm.m" + std::to_string(method_id.Value());
                declaration.methods.push_back(
            {method.name, method.descriptor, implementation, method.is_static});
                handlers.emplace_back(implementation, method_id);
            }
            const auto identity = classes.RegisterClass(declaration);
            class_identities.emplace(class_id.Value(), identity);
            for (const auto& [implementation, method_id] : handlers) {
                invocations.RegisterHandler(
            implementation, [this, method_id](const JniInvocation &invocation) {
                        return InvokeInterpreted(method_id, invocation);
                    });
            }
        }
    }

  [[nodiscard]] JniValue InvokeInterpreted(const dx::VmMethodId method_id,
                                           const JniInvocation &invocation) {
        const auto& method = linker.Method(method_id);
        std::vector<dx::VmValue> arguments;
        arguments.reserve(invocation.arguments.size() + 1);
        if (!method.is_static) {
      arguments.push_back(dx::VmValue::Ref(
          FromReference(invocation.receiver, invocation.thread_id)));
        }
        for (const auto& value : invocation.arguments) {
            arguments.push_back(ToVmValue(value, invocation.thread_id));
        }
        const auto outcome = vm->Call(method_id, arguments);
        if (outcome.exception.IsValid()) {
            // JNI semantics: leave the exception pending for the caller.
            if (logger != nullptr) {
                std::string rendered =
            linker.Class(method.owner).descriptor + "." + method.name +
            " threw " + linker.Class(outcome.exception_class).descriptor +
            ": " + outcome.exception_message;
                for (const auto& entry : outcome.exception_stack) {
                    rendered += " | at " + entry.class_descriptor + "." +
                      entry.method_name + " pc " + std::to_string(entry.pc);
                }
        logger->Write(core::LogLevel::warn, "runtime.dexvm", rendered);
            }
            const auto throwable = session->Environment().PublishLocalObject(
          invocation.thread_id, model->ToIdentity(outcome.exception));
            session->Environment().Throw(invocation.thread_id, throwable);
            return DefaultReturn(method.return_shorty);
        }
        return FromVmValue(outcome.value, method.return_shorty,
                           invocation.thread_id);
    }

    [[nodiscard]] dx::VmValue ToVmValue(const JniValue& value,
                                        const std::uint64_t thread) {
        if (std::holds_alternative<JniBoolean>(value)) {
            return dx::VmValue::Int(std::get<JniBoolean>(value) != 0 ? 1 : 0);
        }
        if (std::holds_alternative<JniByte>(value)) {
            return dx::VmValue::Int(std::get<JniByte>(value));
        }
        if (std::holds_alternative<JniChar>(value)) {
            return dx::VmValue::Int(std::get<JniChar>(value));
        }
        if (std::holds_alternative<JniShort>(value)) {
            return dx::VmValue::Int(std::get<JniShort>(value));
        }
        if (std::holds_alternative<JniInt>(value)) {
            return dx::VmValue::Int(std::get<JniInt>(value));
        }
        if (std::holds_alternative<JniLong>(value)) {
            return dx::VmValue::Long(std::get<JniLong>(value));
        }
        if (std::holds_alternative<JniFloat>(value)) {
            return dx::VmValue::Float(std::get<JniFloat>(value));
        }
        if (std::holds_alternative<JniDouble>(value)) {
            return dx::VmValue::Double(std::get<JniDouble>(value));
        }
        if (std::holds_alternative<JniReference>(value)) {
            return dx::VmValue::Ref(
                FromReference(std::get<JniReference>(value), thread));
        }
        throw DexVmBridgeError("dexvm bridge received a void argument");
    }

    [[nodiscard]] JniValue FromVmValue(const dx::VmValue& value,
                                       const char shorty,
                                       const std::uint64_t thread) {
        switch (shorty) {
            case 'V':
                return JniValue{};
            case 'Z':
                return JniValue{static_cast<JniBoolean>(value.cat1 != 0)};
            case 'B':
                return JniValue{static_cast<JniByte>(value.cat1 & 0xffU)};
            case 'C':
                return JniValue{static_cast<JniChar>(value.cat1 & 0xffffU)};
            case 'S':
      return JniValue{static_cast<JniShort>(value.cat1 & 0xffffU)};
            case 'I':
                return JniValue{static_cast<JniInt>(value.cat1)};
            case 'J':
                return JniValue{static_cast<JniLong>(value.wide)};
            case 'F': {
                JniFloat as_float{};
                const auto bits = value.cat1;
                std::memcpy(&as_float, &bits, sizeof(as_float));
                return JniValue{as_float};
            }
            case 'D': {
                JniDouble as_double{};
                const auto bits = value.wide;
                std::memcpy(&as_double, &bits, sizeof(as_double));
                return JniValue{as_double};
            }
            case 'L': {
      if (!value.ref.IsValid())
        return JniValue{JniReference{}};
                return JniValue{session->Environment().PublishLocalObject(
                    thread, model->ToIdentity(value.ref))};
            }
            default:
      throw DexVmBridgeError("dexvm bridge cannot convert return shorty");
        }
    }

    [[nodiscard]] static JniValue DefaultReturn(const char shorty) {
        switch (shorty) {
    case 'V':
      return JniValue{};
    case 'Z':
      return JniValue{JniBoolean{}};
    case 'B':
      return JniValue{JniByte{}};
    case 'C':
      return JniValue{JniChar{}};
    case 'S':
      return JniValue{JniShort{}};
    case 'I':
      return JniValue{JniInt{}};
    case 'J':
      return JniValue{JniLong{}};
    case 'F':
      return JniValue{JniFloat{}};
    case 'D':
      return JniValue{JniDouble{}};
    default:
      return JniValue{JniReference{}};
        }
    }

    // ---- outbound (interpreter -> native) --------------------------------

  [[nodiscard]] dx::VmValue
  InvokeNative(const dx::LinkedMethod &method, const dx::VmObjectRef receiver,
        const std::span<const dx::VmValue> arguments) {
        const auto& owner_class = linker.Class(method.owner);
        const auto class_name =
        owner_class.descriptor.substr(1, owner_class.descriptor.size() - 2);

        // AAPCS soft-float marshaling (04 §1 table): r0 = JNIEnv, r1 =
        // receiver or jclass, arguments from r2, 64-bit values on even
        // register pairs and 8-byte-aligned stack slots.
        std::vector<std::uint32_t> words;
        words.push_back(session->GuestEnvironment().Value());
        if (method.is_static) {
            words.push_back(GlobalClassReference(method.owner).Value());
        } else {
            words.push_back(PublishLocal(receiver).Value());
        }

        DescriptorWalk walk{method.descriptor};
        std::size_t argument_index = 0;
        std::vector<std::uint32_t> stack;
        const auto push_word = [&](const std::uint32_t word) {
            if (words.size() < 4) {
                words.push_back(word);
            } else {
                stack.push_back(word);
            }
        };
        const auto push_pair = [&](const std::uint64_t bits) {
            if (words.size() <= 2) {
        if (words.size() % 2 != 0)
          words.push_back(0); // align pair
                words.push_back(static_cast<std::uint32_t>(bits));
                words.push_back(static_cast<std::uint32_t>(bits >> 32U));
            } else {
        if (stack.size() % 2 != 0)
          stack.push_back(0);
        while (words.size() < 4)
          words.push_back(0);
                stack.push_back(static_cast<std::uint32_t>(bits));
                stack.push_back(static_cast<std::uint32_t>(bits >> 32U));
            }
        };
        while (!walk.Done()) {
            const char shorty = walk.Next();
            const auto& value = arguments[argument_index++];
            switch (shorty) {
                case 'J':
                case 'D':
                    push_pair(value.wide);
                    break;
                case 'L':
                    push_word(PublishLocal(value.ref).Value());
                    break;
                default:
                    push_word(value.cat1);
                    break;
            }
        }

        // The executor requires an 8-byte aligned stack tail.
    if (stack.size() % 2 != 0)
      stack.push_back(0);

        A32GuestCallFrame frame;
        std::size_t register_count = words.size() < 4 ? words.size() : 4;
        for (std::size_t index = 0; index < register_count; ++index) {
            frame.registers[index] = words[index];
        }
        frame.stack_words = stack;

        // Resolution: RegisterNatives mapping first, then Java_ exports.
        A32GuestCallResult result{};
        const auto identity = class_identities.find(method.owner.Value());
        bool invoked = false;
        if (identity != class_identities.end()) {
            try {
        result = session->InvokeRegisteredNative(identity->second, method.name,
                                                 method.descriptor, frame);
                invoked = true;
            } catch (const std::exception&) {
                invoked = false;
            }
        }
        if (!invoked) {
      const auto target =
          session->FindNativeExport(class_name, method.name, method.descriptor);
            if (!target.has_value()) {
                if (ledger != nullptr) {
                    ledger->RecordUnimplemented(
                        "dexvm.native." + class_name + "." + method.name, 0);
                }
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsatisfiedLinkError;",
            "native method has no registered mapping or export: " + class_name +
                "." + method.name + method.descriptor};
            }
            frame.target = *target;
            result = session->Invoke(frame);
        }

        // Pending exception propagates into the interpreter.
        if (session->Environment().ExceptionCheck(kRootThreadId)) {
            session->Environment().ExceptionClear(kRootThreadId);
      throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                "native method raised a pending JNI exception: " +
                    class_name + "." + method.name};
        }

        const char shorty = ReturnShorty(method.descriptor);
        switch (shorty) {
            case 'V':
                return dx::VmValue::Void();
            case 'J':
            case 'D':
                if (ledger != nullptr) {
        ledger->RecordUnimplemented("dexvm.bridge.wide_native_return", 0);
                }
                throw DexVmBridgeError(
                    "dexvm bridge does not decode 64-bit native returns "
          "yet: " +
          method.name);
            case 'L': {
                const JniReference reference(result.return_value);
      return dx::VmValue::Ref(FromReference(reference, kRootThreadId));
            }
            default: {
                dx::VmValue value;
                value.kind = dx::VmValue::Kind::cat1;
                value.cat1 = result.return_value;
                return value;
            }
        }
    }
};

DexVmGuestBridge::DexVmGuestBridge(
    AndroidGuestCallSession& session, std::vector<std::uint8_t> dex_bytes,
    const std::span<const dexvm::IntrinsicClassDecl> platform_catalog,
    const std::shared_ptr<DexVmAndroidContext>& android_context,
    core::CapabilityLedger& ledger, core::Logger* logger,
    const DexVmBridgeConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->session = &session;
    impl_->ledger = &ledger;
    impl_->logger = logger;
    impl_->config = config;
    impl_->owner = this;

    auto core_catalog = dx::CoreIntrinsicCatalog();
    BindPlatformCoreHandlers(core_catalog, android_context);
    impl_->linker.RegisterIntrinsics(core_catalog);
    if (!platform_catalog.empty()) {
        impl_->linker.RegisterIntrinsics(platform_catalog);
    }
    impl_->linker.RegisterDex(std::move(dex_bytes));
    impl_->linker.Link();

    impl_->model = std::make_unique<dx::JavaObjectModel>(
        session.Strings(), session.Arrays(), config.heap);

    impl_->vm = std::make_unique<dx::Interpreter>(
        impl_->linker, *impl_->model, this, ledger, config.interpreter);
    impl_->vm->SetLogger(logger);
    impl_->threads = std::make_unique<dx::VmThreadRuntime>(*impl_->vm);
    impl_->vm->SetThreadRuntime(impl_->threads.get());
    impl_->vm->SetGcIntegration(dx::InterpreterGcIntegration{
        [&session](const std::function<void(JniObjectIdentity)>& visit) {
            session.Environment().VisitReferenceRoots(visit);
        },
        [&session](const JniObjectIdentity identity) {
            session.Environment().ClearWeakReferencesTo(identity);
        },
        [android_context](const dx::VmRootVisitor& visit) {
            if (android_context) VisitAndroidSessionRoots(*android_context, visit);
        }});

    impl_->RegisterDexClasses();
}

DexVmGuestBridge::~DexVmGuestBridge() {
    if (impl_->threads) impl_->threads->Shutdown();
}

dexvm::Interpreter& DexVmGuestBridge::Vm() noexcept { return *impl_->vm; }
dexvm::VmThreadRuntime& DexVmGuestBridge::Threads() noexcept {
    return *impl_->threads;
}
dexvm::DexClassLinker& DexVmGuestBridge::Linker() noexcept {
    return impl_->linker;
}
dexvm::JavaObjectModel& DexVmGuestBridge::Model() noexcept {
    return *impl_->model;
}
AndroidGuestCallSession& DexVmGuestBridge::Session() noexcept {
    return *impl_->session;
}

JniReference DexVmGuestBridge::PublishLocal(const dexvm::VmObjectRef ref) {
    return impl_->PublishLocal(ref);
}

dexvm::VmObjectRef
DexVmGuestBridge::FromReference(const JniReference reference) {
    return impl_->FromReference(reference, kRootThreadId);
}

std::optional<JniObjectIdentity> DexVmGuestBridge::RegisteredClassIdentity(
    const dexvm::DexClassId java_class) const {
    const auto found = impl_->class_identities.find(java_class.Value());
  if (found == impl_->class_identities.end())
    return std::nullopt;
    return found->second;
}

dexvm::VmValue
DexVmGuestBridge::Invoke(const dexvm::LinkedMethod &method,
                         const dexvm::VmObjectRef receiver,
    const std::span<const dexvm::VmValue> arguments) {
    return impl_->InvokeNative(method, receiver, arguments);
}

}  // namespace ogplay::runtime
