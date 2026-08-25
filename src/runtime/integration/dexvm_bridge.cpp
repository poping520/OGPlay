#include "ogplay/runtime/integration/dexvm_bridge.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "dexvm_android/shared.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint64_t kRootThreadId = 1;
// API 19 Bionic stores pthread mutex owners in the high 16 bits of the mutex
// word and compares that value with pthread_internal_t::tid. Keep the native
// execution contexts in a compact, reserved guest-TID range.
constexpr std::uint64_t kDexVmProcessThreadBase = UINT64_C(0x4000);
constexpr std::uint32_t kDexVmNativeContextSlots = 32;

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
    root(context.egl_context_factory);
    root(context.egl_config_chooser);
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
    {
        std::scoped_lock lock(context.scheduler_mutex);
        root(context.main_looper);
        for (const auto& work : context.scheduled_work) {
            root(work.looper);
            root(work.owner);
            root(work.target);
            root(work.payload);
            root(work.token);
        }
    }
    visit_ref_map(context.ui_node_to_object);
    visit_ref_map(context.ui_click_listeners);
    visit_ref_map(context.ui_touch_listeners);
    visit_ref_map(context.ui_view_layout_params);
    visit_ref_map(context.video_completion);
    visit_ref_map(context.video_errors);

    // Host side state keyed by a VM owner is logically part of that owner.
    // Keep the owner live until the explicit close/recycle/lifecycle path
    // removes its state; this closes the pre-GC raw-handle compatibility gap.
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
    std::shared_ptr<DexVmAndroidContext> android_context;

    dx::DexClassLinker linker;
    std::unique_ptr<dx::JavaObjectModel> model;
    std::unique_ptr<DexVmIoVfsAdapter> io_file_system;
    std::unique_ptr<dx::Interpreter> vm;
    // Declared after vm so destruction stops every guest Java thread before
    // the interpreter and object model go away.
    std::unique_ptr<dx::VmThreadRuntime> threads;

    std::unordered_map<std::uint32_t, JniObjectIdentity> class_identities;
    std::unordered_map<std::uint32_t, dx::VmFieldId> field_identities;
    std::unordered_map<std::uint32_t, JniReference> class_global_refs;
    std::unordered_set<std::uint32_t> registering_classes;
    mutable std::mutex thread_contexts_mutex;
    std::unordered_map<std::uint64_t, std::uint64_t> token_to_process_thread{
        {1, kRootThreadId}};
    std::unordered_map<std::uint64_t, std::uint64_t> process_thread_to_token{
        {kRootThreadId, 1}};
    std::unordered_map<std::uint64_t, std::uint32_t> process_thread_slots;

    DexVmGuestBridge* owner{};

    [[nodiscard]] JniObjectIdentity JniClassIdentity(
        const dx::DexClassId java_class) {
        const auto& linked = linker.Class(java_class);
        if (linked.is_array) {
            return {JniObjectDomain::dex_vm, java_class.Value()};
        }
        return RegisterClassForNative(java_class);
    }

    [[nodiscard]] dx::DexClassId DexClassIdentity(
        const JniObjectIdentity identity) const {
        if (identity.domain == JniObjectDomain::dex_vm) {
            const auto java_class = dx::DexClassId(
                static_cast<std::uint32_t>(identity.value));
            static_cast<void>(linker.Class(java_class));
            return java_class;
        }
        for (const auto& [raw, registered] : class_identities) {
            if (registered == identity) return dx::DexClassId(raw);
        }
        throw DexVmBridgeError(
            "JNI class identity is not published in DexVM");
    }

    [[nodiscard]] std::optional<dx::DexClassId> ClassForJniIdentity(
        const JniObjectIdentity identity) const {
        for (const auto& [raw, registered] : class_identities) {
            if (registered == identity) return dx::DexClassId(raw);
        }
        return std::nullopt;
    }

    [[nodiscard]] dx::DexClassId ObjectClassIdentity(
        const JniObjectIdentity identity) const {
        try {
            return DexClassIdentity(session->Objects().ClassOf(identity));
        } catch (const JniGuestBindingError&) {
            return dx::DexClassId(0);
        }
    }

    [[nodiscard]] std::pair<dx::DexClassId, dx::DexClassId>
    ObjectArrayClassIdentity(const JniObjectIdentity element_identity) {
        const auto element_class = DexClassIdentity(element_identity);
        const auto array_class = linker.ResolveDescriptor(
            "[" + linker.Class(element_class).descriptor);
        return {array_class, element_class};
    }

    [[nodiscard]] dx::VmObjectRef MonitorObject(
        const JniObjectIdentity identity) {
        for (const auto& [raw_class, registered] : class_identities) {
            if (registered == identity) {
                return model->ClassObject(dx::DexClassId(raw_class));
            }
        }
        return model->FromIdentity(identity);
    }

    // ---- reference conversion ------------------------------------------

    [[nodiscard]] JniReference PublishLocal(
        const dx::VmObjectRef ref,
        const std::uint64_t thread = kRootThreadId) {
    if (!ref.IsValid())
      return JniReference{};
    const auto identity = model->Kind(ref) == dx::VmObjectKind::class_object
        ? JniClassIdentity(model->ClassOfClassObject(ref))
        : model->ToIdentity(ref);
    const auto java_class = model->ObjectClass(ref);
    if (java_class.IsValid()) {
      const auto class_identity = JniClassIdentity(java_class);
      try {
        if (session->Objects().ClassOf(identity) != class_identity) {
          throw DexVmBridgeError(
              "DexVM object identity is published with another JNI class");
        }
      } catch (const JniGuestBindingError&) {
        session->Objects().Register(identity, class_identity);
      }
    }
    return session->Environment().PublishLocalObject(thread, identity);
    }

    [[nodiscard]] std::uint64_t ProcessThreadForToken(
        const std::uint64_t token) const {
        const std::scoped_lock lock(thread_contexts_mutex);
        const auto found = token_to_process_thread.find(token);
        if (found == token_to_process_thread.end()) {
            throw DexVmBridgeError(
                "DexVM execution context has no native thread context");
        }
        return found->second;
    }

    [[nodiscard]] std::uint64_t TokenForProcessThread(
        const std::uint64_t thread) const {
        const std::scoped_lock lock(thread_contexts_mutex);
        const auto found = process_thread_to_token.find(thread);
        if (found == process_thread_to_token.end()) {
            throw JniMonitorError(JniMonitorErrorReason::invalid_thread,
                                  "JNI monitor thread is not a DexVM thread");
        }
        return found->second;
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
        if (const auto represented = ClassForJniIdentity(*identity);
            represented.has_value()) {
            return model->ClassObject(*represented);
        }
        return model->FromIdentity(*identity);
    }

    // ---- inbound (native -> interpreter) --------------------------------

  [[nodiscard]] std::optional<dx::VmFieldId> DexField(
      const JniResolvedField& field) const {
    const auto found = field_identities.find(field.id.Value());
    return found == field_identities.end()
               ? std::nullopt
               : std::optional<dx::VmFieldId>{found->second};
  }

  [[nodiscard]] std::optional<bool> EnsureJniClassInitialized(
      const JniObjectIdentity identity, const std::uint64_t thread) {
    const auto java_class = ClassForJniIdentity(identity);
    if (!java_class.has_value()) return std::nullopt;
    const auto outcome = vm->EnsureClassInitialized(*java_class);
    if (!outcome.exception.IsValid()) return true;
    session->Environment().Throw(thread,
                                 PublishLocal(outcome.exception, thread));
    return false;
  }

  [[nodiscard]] JniValue ReadFieldSlots(
      const dx::LinkedField& field, const std::span<const dx::Slot> slots,
      const std::uint64_t thread) {
    if (field.slot + (field.is_wide ? 2U : 1U) > slots.size()) {
      throw DexVmBridgeError("DexVM JNI field slot is out of range: " +
                             linker.Class(field.owner).descriptor + "." +
                             field.name + field.descriptor + " slot=" +
                             std::to_string(field.slot) + " slots=" +
                             std::to_string(slots.size()));
    }
    if (field.is_ref) {
      return JniValue{PublishLocal(dx::VmObjectRef(slots[field.slot].bits),
                                  thread)};
    }
    if (field.is_wide) {
      dx::VmValue value;
      value.kind = dx::VmValue::Kind::wide;
      value.wide = static_cast<std::uint64_t>(slots[field.slot].bits) |
                   (static_cast<std::uint64_t>(slots[field.slot + 1U].bits)
                    << 32U);
      return FromVmValue(value, field.descriptor.front(), thread);
    }
    return FromVmValue(dx::VmValue::Int(slots[field.slot].bits),
                       field.descriptor.front(), thread);
  }

  void WriteFieldSlots(const dx::LinkedField& field,
                       const std::span<dx::Slot> slots,
                       const JniValue& value, const std::uint64_t thread) {
    if (field.slot + (field.is_wide ? 2U : 1U) > slots.size()) {
      throw DexVmBridgeError("DexVM JNI field slot is out of range: " +
                             linker.Class(field.owner).descriptor + "." +
                             field.name + field.descriptor + " slot=" +
                             std::to_string(field.slot) + " slots=" +
                             std::to_string(slots.size()));
    }
    const auto decoded = ToVmValue(value, thread);
    if (field.is_ref) {
      slots[field.slot] = {decoded.ref.Value(), dx::SlotTag::ref};
    } else if (field.is_wide) {
      slots[field.slot] = {static_cast<std::uint32_t>(decoded.wide),
                           dx::SlotTag::wide_lo};
      slots[field.slot + 1U] = {
          static_cast<std::uint32_t>(decoded.wide >> 32U),
          dx::SlotTag::wide_hi};
    } else {
      slots[field.slot] = {decoded.cat1, dx::SlotTag::cat1};
    }
  }

  [[nodiscard]] std::optional<JniValue> GetJniInstanceField(
      const JniObjectIdentity object, const JniResolvedField& resolved,
      const std::uint64_t thread) {
    const auto field_id = DexField(resolved);
    if (!field_id.has_value()) return std::nullopt;
    const auto& field = linker.Field(*field_id);
    const auto instance = model->FromIdentity(object);
    return ReadFieldSlots(field, model->InstanceSlots(instance), thread);
  }

  [[nodiscard]] bool SetJniInstanceField(
      const JniObjectIdentity object, const JniResolvedField& resolved,
      const JniValue& value, const std::uint64_t thread) {
    const auto field_id = DexField(resolved);
    if (!field_id.has_value()) return false;
    const auto& field = linker.Field(*field_id);
    const auto instance = model->FromIdentity(object);
    WriteFieldSlots(field, model->InstanceSlots(instance), value, thread);
    return true;
  }

  [[nodiscard]] std::optional<JniValue> GetJniStaticField(
      const JniResolvedField& resolved, const std::uint64_t thread) {
    const auto field_id = DexField(resolved);
    if (!field_id.has_value()) return std::nullopt;
    const auto& field = linker.Field(*field_id);
    auto& storage = linker.MutableClass(field.owner).static_storage;
    std::vector<dx::Slot> slots;
    slots.reserve(storage.size());
    for (const auto bits : storage) slots.push_back({bits, dx::SlotTag::cat1});
    return ReadFieldSlots(field, slots, thread);
  }

  [[nodiscard]] bool SetJniStaticField(
      const JniResolvedField& resolved, const JniValue& value,
      const std::uint64_t thread) {
    const auto field_id = DexField(resolved);
    if (!field_id.has_value()) return false;
    const auto& field = linker.Field(*field_id);
    auto& storage = linker.MutableClass(field.owner).static_storage;
    std::vector<dx::Slot> slots;
    slots.reserve(storage.size());
    for (const auto bits : storage) slots.push_back({bits, dx::SlotTag::cat1});
    WriteFieldSlots(field, slots, value, thread);
    for (std::size_t index = 0; index < slots.size(); ++index) {
      storage[index] = slots[index].bits;
    }
    return true;
  }

  void BindField(const JniObjectIdentity identity,
                 const dx::VmFieldId field_id) {
    const auto& field = linker.Field(field_id);
    const auto jni_id = session->Classes().GetFieldId(
        identity, field.name, field.descriptor, field.is_static);
    if (!jni_id.has_value() ||
        session->Classes().ResolveField(*jni_id).declaring_class != identity) {
      throw DexVmBridgeError("cannot bind DexVM field to JNI registry");
    }
    field_identities.insert_or_assign(jni_id->Value(), field_id);
  }

  void PublishMissingFields(const dx::DexClassId class_id,
                            const JniObjectIdentity identity) {
    auto& classes = session->Classes();
    const auto publish = [&](const dx::VmFieldId field_id) {
      const auto& field = linker.Field(field_id);
      const auto existing = classes.GetFieldId(
          identity, field.name, field.descriptor, field.is_static);
      if (!existing.has_value() ||
          classes.ResolveField(*existing).declaring_class != identity) {
        static_cast<void>(classes.RegisterField(
            identity, {field.name, field.descriptor,
                       "dexvm.f" + std::to_string(field_id.Value()),
                       field.is_static}));
      }
      BindField(identity, field_id);
    };
    for (const auto field : linker.Class(class_id).own_instance_fields)
      publish(field);
    for (const auto field : linker.Class(class_id).own_static_fields)
      publish(field);
  }

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
      PublishMissingFields(class_id, *existing);
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
    const auto append_field = [&](const dx::VmFieldId field_id) {
      const auto& field = linker.Field(field_id);
      declaration.fields.push_back(
          {field.name, field.descriptor,
           "dexvm.f" + std::to_string(field_id.Value()), field.is_static});
    };
    for (const auto field : linked.own_instance_fields) append_field(field);
    for (const auto field : linked.own_static_fields) append_field(field);
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
    for (const auto field : linked.own_instance_fields) BindField(identity, field);
    for (const auto field : linked.own_static_fields) BindField(identity, field);
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
    // avoids a second, title-specific platform registry. The recursive
    // registration path is also the single owner of superclass publication:
    // interpreted classes must retain intrinsic parents in JNI type checks.
    for (const auto class_id : linker.AllClasses()) {
      const auto &linked = linker.Class(class_id);
      if (linked.is_array) continue;
      static_cast<void>(RegisterClassForNative(class_id));
    }
  }

  [[nodiscard]] JniValue InvokeInterpreted(const dx::VmMethodId method_id,
                                           const JniInvocation &invocation) {
        const auto& method = linker.Method(method_id);
        // A native method executes outside the interpreter lock. Every JNI
        // re-entry reacquires it so object-model and linker state still have
        // one writer, while unrelated Java threads may run between calls.
        const dx::VmExecutionLockScope execution_guard(vm->ExecutionLock());
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
            const auto throwable = PublishLocal(outcome.exception,
                                                invocation.thread_id);
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
                return JniValue{PublishLocal(value.ref, thread)};
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
        const auto process_thread =
            ProcessThreadForToken(vm->CurrentContextToken());
        auto& environment = session->Environment();
        environment.PushLocalFrame(process_thread, arguments.size() + 8U);
        struct LocalFrameScope final {
            JniEnvironment* environment{};
            std::uint64_t thread{};
            ~LocalFrameScope() {
                try {
                    static_cast<void>(environment->PopLocalFrame(thread));
                } catch (const std::exception&) {
                }
            }
        } local_frame{&environment, process_thread};
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
            words.push_back(PublishLocal(receiver, process_thread).Value());
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
                    push_word(PublishLocal(value.ref, process_thread).Value());
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
        frame.thread_id = process_thread;
        frame.refresh_tick_budget_at_handled_boundary = true;

        // Resolution: RegisterNatives mapping first, then Java_ exports. A
        // native body may be a process-lifetime loop, so it must not retain
        // the interpreter's single-writer lock. JNI callbacks reacquire the
        // lock at their bridge entry points.
        A32GuestCallResult result{};
        auto& execution_lock = vm->ExecutionLock();
        const auto execution_depth = execution_lock.ReleaseForBlocking();
        try {
            const auto identity = class_identities.find(method.owner.Value());
            bool invoked = false;
            if (identity != class_identities.end()) {
                const auto registered = session->TryInvokeRegisteredNative(
                    identity->second, method.name, method.descriptor, frame);
                if (registered.has_value()) {
                    result = *registered;
                    invoked = true;
                }
            }
            if (!invoked) {
                const auto target = session->FindNativeExport(
                    class_name, method.name, method.descriptor);
                if (!target.has_value()) {
                    if (ledger != nullptr) {
                        ledger->RecordUnimplemented(
                            "dexvm.native." + class_name + "." + method.name,
                            0);
                    }
                    throw dx::VmJavaThrow{
                        "Ljava/lang/UnsatisfiedLinkError;",
                        "native method has no registered mapping or export: " +
                            class_name + "." + method.name +
                            method.descriptor};
                }
                frame.target = *target;
                result = session->Invoke(frame);
            }
        } catch (...) {
            execution_lock.ReacquireAfterBlocking(execution_depth);
            throw;
        }
        execution_lock.ReacquireAfterBlocking(execution_depth);

        // Pending exception propagates into the interpreter.
        if (session->Environment().ExceptionCheck(process_thread)) {
            session->Environment().ExceptionClear(process_thread);
      throw dx::VmJavaThrow{"Ljava/lang/RuntimeException;",
                "native method raised a pending JNI exception: " +
                    class_name + "." + method.name};
        }

        const char shorty = ReturnShorty(method.descriptor);
        switch (shorty) {
            case 'V':
                return dx::VmValue::Void();
            case 'J':
            case 'D': {
                const auto bits = static_cast<std::uint64_t>(
                                      result.return_value) |
                                  (static_cast<std::uint64_t>(
                                       result.return_value_high)
                                   << 32U);
                dx::VmValue value;
                value.kind = dx::VmValue::Kind::wide;
                value.wide = bits;
                return value;
            }
            case 'L': {
                const JniReference reference(result.return_value);
      return dx::VmValue::Ref(FromReference(reference, process_thread));
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
    impl_->android_context = android_context;
    impl_->owner = this;

    auto core_catalog = dx::CoreIntrinsicCatalog(
        AndroidCoreIntrinsicServices(android_context));
    BindPlatformCoreHandlers(core_catalog, android_context);
    impl_->linker.RegisterIntrinsics(core_catalog);
    if (!platform_catalog.empty()) {
        impl_->linker.RegisterIntrinsics(platform_catalog);
    }
    impl_->linker.RegisterDex(std::move(dex_bytes));
    impl_->linker.Link();

    auto* const bridge_state = impl_.get();
    impl_->model = std::make_unique<dx::JavaObjectModel>(
        session.Strings(), session.Arrays(), config.heap,
        dx::JavaObjectInterop{
            &session.Objects().ObjectArrays(),
            [bridge_state](const dx::DexClassId java_class) {
                return bridge_state->JniClassIdentity(java_class);
            },
            [bridge_state](const JniObjectIdentity identity) {
                return bridge_state->DexClassIdentity(identity);
            },
            [bridge_state](const JniObjectIdentity identity) {
                return bridge_state->ObjectClassIdentity(identity);
            },
            [bridge_state](const JniObjectIdentity identity) {
                return bridge_state->ObjectArrayClassIdentity(identity);
            },
            [bridge_state](const dx::DexClassId java_class)
                -> std::optional<std::uint16_t> {
                bridge_state->linker.EnsureClassLinked(java_class);
                const auto& linked = bridge_state->linker.Class(java_class);
                if (linked.is_intrinsic || linked.is_array) {
                    return std::nullopt;
                }
                return linked.instance_slots;
            }});

    impl_->vm = std::make_unique<dx::Interpreter>(
        impl_->linker, *impl_->model, this, ledger, config.interpreter);
    impl_->vm->SetNioRuntime(&session.NIO());
    if (android_context != nullptr) {
        impl_->vm->Network().Configure(android_context->network_policy,
                                       android_context->network_transport);
    }
    RegisterAndroidAudioTrackStateTable(*impl_->vm, android_context);
    RegisterAndroidSchedulerStateTable(*impl_->vm, android_context);
    RegisterAndroidValueStateTables(*impl_->vm, android_context);
    RegisterAndroidDatabaseStateTables(*impl_->vm, android_context);
    if (android_context != nullptr && android_context->vfs != nullptr) {
        impl_->io_file_system = std::make_unique<DexVmIoVfsAdapter>(
            *android_context->vfs);
        impl_->vm->IO().SetFileSystem(impl_->io_file_system.get());
    }
    impl_->vm->SetLogger(logger);
    impl_->threads = std::make_unique<dx::VmThreadRuntime>(*impl_->vm);
    session.Fields().SetAccessHooks(JniFieldAccessHooks{
        [bridge_state](const JniObjectIdentity java_class,
                       const std::uint64_t thread) {
          const dx::VmExecutionLockScope guard(
              bridge_state->vm->ExecutionLock());
          return bridge_state->EnsureJniClassInitialized(java_class, thread);
        },
        [bridge_state](const JniObjectIdentity object,
                       const JniObjectIdentity,
                       const JniResolvedField& field,
                       const std::uint64_t thread) {
          const dx::VmExecutionLockScope guard(
              bridge_state->vm->ExecutionLock());
          return bridge_state->GetJniInstanceField(object, field, thread);
        },
        [bridge_state](const JniObjectIdentity object,
                       const JniObjectIdentity,
                       const JniResolvedField& field, const JniValue& value,
                       const std::uint64_t thread) {
          const dx::VmExecutionLockScope guard(
              bridge_state->vm->ExecutionLock());
          return bridge_state->SetJniInstanceField(object, field, value,
                                                   thread);
        },
        [bridge_state](const JniObjectIdentity,
                       const JniResolvedField& field,
                       const std::uint64_t thread) {
          const dx::VmExecutionLockScope guard(
              bridge_state->vm->ExecutionLock());
          return bridge_state->GetJniStaticField(field, thread);
        },
        [bridge_state](const JniObjectIdentity,
                       const JniResolvedField& field, const JniValue& value,
                       const std::uint64_t thread) {
          const dx::VmExecutionLockScope guard(
              bridge_state->vm->ExecutionLock());
          return bridge_state->SetJniStaticField(field, value, thread);
        }});
    if (android_context != nullptr) {
        android_context->threads = impl_->threads.get();
        impl_->vm->Monitors().SetTimeSource([android_context] {
            return android_context->uptime_millis.load();
        });
    }
    session.Environment().SetMonitorHooks(JniMonitorHooks{
        [bridge_state](const JniObjectIdentity identity,
                       const std::uint64_t thread) {
            const dx::VmExecutionLockScope guard(
                bridge_state->vm->ExecutionLock());
            if (identity.value == 0U) {
                throw JniMonitorError(JniMonitorErrorReason::invalid_object,
                                      "JNI monitor object is null");
            }
            bridge_state->vm->Monitors().Enter(
                bridge_state->MonitorObject(identity),
                bridge_state->TokenForProcessThread(thread));
        },
        [bridge_state](const JniObjectIdentity identity,
                       const std::uint64_t thread) {
            const dx::VmExecutionLockScope guard(
                bridge_state->vm->ExecutionLock());
            if (identity.value == 0U) {
                throw JniMonitorError(JniMonitorErrorReason::invalid_object,
                                      "JNI monitor object is null");
            }
            try {
                bridge_state->vm->Monitors().Exit(
                    bridge_state->MonitorObject(identity),
                    bridge_state->TokenForProcessThread(thread));
            } catch (const dx::VmJavaThrow&) {
                throw JniMonitorError(JniMonitorErrorReason::not_owner,
                                      "JNI monitor exit by non-owner");
            }
        },
        [bridge_state](const std::uint64_t thread) {
            const dx::VmExecutionLockScope guard(
                bridge_state->vm->ExecutionLock());
            const auto token = bridge_state->TokenForProcessThread(thread);
            const auto held = bridge_state->vm->Monitors().HeldCount(token);
            bridge_state->vm->Monitors().ReleaseAll(token);
            return held;
        },
        [] { return std::size_t{}; },
        [bridge_state] {
            const dx::VmExecutionLockScope guard(
                bridge_state->vm->ExecutionLock());
            bridge_state->vm->Monitors().Shutdown();
            return std::size_t{};
        },
        [bridge_state](const JniObjectIdentity identity) {
            const dx::VmExecutionLockScope guard(
                bridge_state->vm->ExecutionLock());
            if (identity.value == 0U) return JniMonitorSnapshot{};
            const auto snapshot = bridge_state->vm->Monitors().Snapshot(
                bridge_state->MonitorObject(identity));
            std::uint64_t owner_thread{};
            if (snapshot.owner != 0U) {
                owner_thread = bridge_state->ProcessThreadForToken(
                    snapshot.owner);
            }
            return JniMonitorSnapshot{owner_thread, snapshot.recursion,
                                      snapshot.waiting, 0,
                                      snapshot.shutting_down};
        }});
    impl_->vm->SetGcIntegration(dx::InterpreterGcIntegration{
        [&session](const std::function<void(JniObjectIdentity)>& visit) {
            session.Environment().VisitReferenceRoots(visit);
        },
        [&session](const JniObjectIdentity identity) {
            session.Environment().ClearWeakReferencesTo(identity);
            try {
                session.Objects().Forget(identity);
            } catch (const JniGuestBindingError&) {
                // Strings and primitive arrays have dedicated stores and are
                // intentionally absent from the generic object registry.
            }
        },
        [android_context](const dx::VmRootVisitor& visit) {
            if (android_context) VisitAndroidSessionRoots(*android_context, visit);
        }});

    impl_->RegisterDexClasses();
}

DexVmGuestBridge::~DexVmGuestBridge() {
    if (impl_->android_context) {
        ShutdownAndroidScheduler(*impl_->android_context);
    }
    if (impl_->threads) impl_->threads->Shutdown();
    if (impl_->android_context) impl_->android_context->threads = nullptr;
    if (impl_->session) {
        impl_->session->Environment().SetMonitorHooks({});
        impl_->session->Fields().SetAccessHooks({});
    }
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

void DexVmGuestBridge::AttachThread(const std::uint64_t guest_thread_id,
                                    const std::uint64_t execution_token) {
    if (guest_thread_id < 2U) {
        throw DexVmBridgeError("DexVM child thread id is invalid");
    }
    std::uint32_t slot = kDexVmNativeContextSlots;
    std::uint64_t process_thread{};
    {
        const std::scoped_lock lock(impl_->thread_contexts_mutex);
        for (std::uint32_t candidate = 0;
             candidate < kDexVmNativeContextSlots; ++candidate) {
            const auto used = std::find_if(
                impl_->process_thread_slots.begin(),
                impl_->process_thread_slots.end(),
                [candidate](const auto& entry) {
                    return entry.second == candidate;
                });
            if (used == impl_->process_thread_slots.end()) {
                slot = candidate;
                break;
            }
        }
        if (slot == kDexVmNativeContextSlots) {
            throw DexVmBridgeError(
                "DexVM native thread context pool is exhausted");
        }
        process_thread = kDexVmProcessThreadBase + slot;
        impl_->process_thread_slots.emplace(process_thread, slot);
    }
    try {
        impl_->session->PrepareDexVmThread(process_thread, slot);
        const std::scoped_lock lock(impl_->thread_contexts_mutex);
        impl_->token_to_process_thread.emplace(execution_token,
                                               process_thread);
        impl_->process_thread_to_token.emplace(process_thread,
                                               execution_token);
    } catch (...) {
        const std::scoped_lock lock(impl_->thread_contexts_mutex);
        impl_->process_thread_slots.erase(process_thread);
        throw;
    }
}

void DexVmGuestBridge::DetachThread(const std::uint64_t guest_thread_id,
                                    const std::uint64_t execution_token) noexcept {
    static_cast<void>(guest_thread_id);
    std::uint64_t process_thread{};
    {
        const std::scoped_lock lock(impl_->thread_contexts_mutex);
        const auto found = impl_->token_to_process_thread.find(execution_token);
        if (found == impl_->token_to_process_thread.end()) return;
        process_thread = found->second;
    }
    impl_->session->ReleaseDexVmThread(process_thread);
    const std::scoped_lock lock(impl_->thread_contexts_mutex);
    impl_->token_to_process_thread.erase(execution_token);
    impl_->process_thread_to_token.erase(process_thread);
    impl_->process_thread_slots.erase(process_thread);
}

}  // namespace ogplay::runtime
