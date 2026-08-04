#include "ogplay/runtime/jni_invocation.h"

#include <map>
#include <mutex>
#include <utility>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniInvocationErrorReason reason,
                       std::string message) {
    throw JniInvocationError(reason, std::move(message));
}

[[nodiscard]] bool Matches(const JniValue& value,
                           const JniTypeDescriptor& type) {
    switch (type.kind) {
    case JniTypeKind::boolean: return std::holds_alternative<JniBoolean>(value);
    case JniTypeKind::byte: return std::holds_alternative<JniByte>(value);
    case JniTypeKind::character: return std::holds_alternative<JniChar>(value);
    case JniTypeKind::short_integer:
        return std::holds_alternative<JniShort>(value);
    case JniTypeKind::integer: return std::holds_alternative<JniInt>(value);
    case JniTypeKind::long_integer: return std::holds_alternative<JniLong>(value);
    case JniTypeKind::float_value:
        return std::holds_alternative<JniFloat>(value);
    case JniTypeKind::double_value:
        return std::holds_alternative<JniDouble>(value);
    case JniTypeKind::void_value:
        return std::holds_alternative<std::monostate>(value);
    case JniTypeKind::object:
    case JniTypeKind::array:
        return std::holds_alternative<JniReference>(value);
    }
    return false;
}

void ValidateArguments(const JniResolvedMethod& method,
                       const std::span<const JniValue> arguments) {
    if (arguments.size() != method.layout.parameters.size()) {
        Fail(JniInvocationErrorReason::argument_count_mismatch,
             "JNI method argument count does not match descriptor");
    }
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (!Matches(arguments[index], method.layout.parameters[index])) {
            Fail(JniInvocationErrorReason::argument_type_mismatch,
                 "JNI method argument type does not match descriptor");
        }
    }
}

}  // namespace

JniInvocationError::JniInvocationError(const JniInvocationErrorReason reason,
                                       std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

JniInvocationErrorReason JniInvocationError::Reason() const noexcept {
    return reason_;
}

class JniInvocationEngine::Impl final {
public:
    explicit Impl(const JniClassRegistry& classes) : classes_(&classes) {}

    void Register(std::string implementation, JniMethodHandler handler) {
        if (implementation.empty() || !handler) {
            Fail(JniInvocationErrorReason::missing_handler,
                 "JNI method handler requires an implementation ID and target");
        }
        std::scoped_lock lock(mutex_);
        if (!handlers_.emplace(std::move(implementation), std::move(handler))
                 .second) {
            Fail(JniInvocationErrorReason::duplicate_handler,
                 "JNI implementation handler is already registered");
        }
    }

    [[nodiscard]] JniValue Virtual(
        const std::uint64_t thread_id, const JniReference receiver,
        const JniObjectIdentity receiver_class, const JniMethodId method_id,
        const std::span<const JniValue> arguments,
        const JniArgumentSource source) const {
        ValidateThreadAndReceiver(thread_id, receiver);
        const auto declared = classes_->ResolveMethod(method_id);
        if (declared.declaration.is_static) WrongKind();
        if (!classes_->IsAssignableFrom(declared.declaring_class,
                                        receiver_class)) {
            Incompatible();
        }
        const auto selected_id = classes_->GetMethodId(
            receiver_class, declared.declaration.name,
            declared.declaration.descriptor, false);
        if (!selected_id.has_value()) Incompatible();
        const auto selected = classes_->ResolveMethod(*selected_id);
        return Invoke({thread_id, JniInvocationKind::virtual_instance, source,
                       receiver, receiver_class, selected, arguments});
    }

    [[nodiscard]] JniValue Nonvirtual(
        const std::uint64_t thread_id, const JniReference receiver,
        const JniObjectIdentity receiver_class,
        const JniObjectIdentity dispatch_class, const JniMethodId method_id,
        const std::span<const JniValue> arguments,
        const JniArgumentSource source) const {
        ValidateThreadAndReceiver(thread_id, receiver);
        const auto method = classes_->ResolveMethod(method_id);
        if (method.declaration.is_static) WrongKind();
        if (!classes_->IsAssignableFrom(method.declaring_class,
                                        dispatch_class) ||
            !classes_->IsAssignableFrom(dispatch_class, receiver_class)) {
            Incompatible();
        }
        return Invoke({thread_id, JniInvocationKind::nonvirtual_instance,
                       source, receiver, dispatch_class, method, arguments});
    }

    [[nodiscard]] JniValue Static(
        const std::uint64_t thread_id,
        const JniObjectIdentity dispatch_class, const JniMethodId method_id,
        const std::span<const JniValue> arguments,
        const JniArgumentSource source) const {
        if (thread_id == 0) InvalidThread();
        const auto method = classes_->ResolveMethod(method_id);
        if (!method.declaration.is_static) WrongKind();
        if (!classes_->IsAssignableFrom(method.declaring_class,
                                        dispatch_class)) {
            Incompatible();
        }
        return Invoke({thread_id, JniInvocationKind::static_method, source,
                       JniReference{}, dispatch_class, method, arguments});
    }

private:
    [[nodiscard]] JniValue Invoke(const JniInvocation& invocation) const {
        ValidateArguments(invocation.method, invocation.arguments);
        JniMethodHandler handler;
        {
            std::scoped_lock lock(mutex_);
            const auto found = handlers_.find(
                invocation.method.declaration.implementation);
            if (found == handlers_.end()) {
                Fail(JniInvocationErrorReason::missing_handler,
                     "JNI method implementation has no registered handler");
            }
            handler = found->second;
        }
        auto result = handler(invocation);
        if (!Matches(result, invocation.method.layout.result)) {
            Fail(JniInvocationErrorReason::return_type_mismatch,
                 "JNI method return type does not match descriptor");
        }
        return result;
    }

    static void ValidateThreadAndReceiver(const std::uint64_t thread_id,
                                          const JniReference receiver) {
        if (thread_id == 0) InvalidThread();
        if (receiver.IsNull()) {
            Fail(JniInvocationErrorReason::invalid_receiver,
                 "JNI instance method receiver cannot be null");
        }
    }

    [[noreturn]] static void InvalidThread() {
        Fail(JniInvocationErrorReason::invalid_thread,
             "JNI invocation thread ID cannot be zero");
    }

    [[noreturn]] static void WrongKind() {
        Fail(JniInvocationErrorReason::wrong_method_kind,
             "JNI invocation kind does not match method declaration");
    }

    [[noreturn]] static void Incompatible() {
        Fail(JniInvocationErrorReason::incompatible_class,
             "JNI receiver or dispatch class is incompatible with method");
    }

    const JniClassRegistry* classes_{};
    mutable std::mutex mutex_;
    std::map<std::string, JniMethodHandler> handlers_;
};

JniInvocationEngine::JniInvocationEngine(const JniClassRegistry& classes)
    : impl_(std::make_unique<Impl>(classes)) {}
JniInvocationEngine::~JniInvocationEngine() = default;
JniInvocationEngine::JniInvocationEngine(JniInvocationEngine&&) noexcept =
    default;
JniInvocationEngine& JniInvocationEngine::operator=(
    JniInvocationEngine&&) noexcept = default;

void JniInvocationEngine::RegisterHandler(std::string implementation,
                                          JniMethodHandler handler) {
    impl_->Register(std::move(implementation), std::move(handler));
}
JniValue JniInvocationEngine::InvokeVirtual(
    const std::uint64_t thread_id, const JniReference receiver,
    const JniObjectIdentity receiver_class, const JniMethodId method,
    const std::span<const JniValue> arguments,
    const JniArgumentSource source) const {
    return impl_->Virtual(thread_id, receiver, receiver_class, method, arguments,
                          source);
}
JniValue JniInvocationEngine::InvokeNonvirtual(
    const std::uint64_t thread_id, const JniReference receiver,
    const JniObjectIdentity receiver_class,
    const JniObjectIdentity dispatch_class, const JniMethodId method,
    const std::span<const JniValue> arguments,
    const JniArgumentSource source) const {
    return impl_->Nonvirtual(thread_id, receiver, receiver_class, dispatch_class,
                             method, arguments, source);
}
JniValue JniInvocationEngine::InvokeStatic(
    const std::uint64_t thread_id, const JniObjectIdentity dispatch_class,
    const JniMethodId method, const std::span<const JniValue> arguments,
    const JniArgumentSource source) const {
    return impl_->Static(thread_id, dispatch_class, method, arguments, source);
}

}  // namespace ogplay::runtime
