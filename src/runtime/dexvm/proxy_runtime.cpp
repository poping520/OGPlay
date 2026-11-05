#include "ogplay/runtime/dexvm/proxy_runtime.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace ogplay::runtime::dexvm {
namespace {

constexpr const char* kInvokeDescriptor =
    "(Ljava/lang/Object;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;";

struct Candidate final {
    VmMethodId method;
    DexClassId owner;
    std::string name;
    std::string descriptor;
    DexClassId return_type;
    std::vector<DexClassId> throws;

    [[nodiscard]] std::string ParameterKey() const { return name + descriptor.substr(0, descriptor.find(')') + 1); }
};

// What one generated method forwards: the interface method the handler sees and
// the checked exceptions it may rethrow without wrapping.
struct Forward final {
    VmMethodId declared;
    DexClassId owner;
    std::string name;
    std::string descriptor;
    DexClassId return_type;
    std::vector<DexClassId> throws;
};

[[nodiscard]] bool IsPrimitive(const DexClassLinker& linker, const DexClassId type) {
    return ClassNameCodec::IsPrimitive(linker.Class(type).descriptor);
}

[[nodiscard]] Candidate MakeCandidate(Interpreter& vm, const VmMethodId id) {
    auto& linker = vm.Linker();
    const auto& method = linker.Method(id);
    const auto parsed = ClassNameCodec::ParseMethod(method.descriptor);
    return {id,
            method.owner,
            method.name,
            method.descriptor,
            linker.ResolveDescriptor(parsed.return_type),
            linker.ReflectionExceptionTypes(id)};
}

void AddInterfaceMethods(Interpreter& vm, const DexClassId interface_id, std::vector<Candidate>& out,
                         std::set<std::uint32_t>& seen) {
    if (!seen.insert(interface_id.Value()).second) return;
    auto& linker = vm.Linker();
    linker.EnsureClassLinked(interface_id);
    const auto own = linker.Class(interface_id).own_virtual_methods;
    for (const auto id : own) {
        if (!linker.Method(id).is_static) out.push_back(MakeCandidate(vm, id));
    }
    const auto parents = linker.Class(interface_id).direct_interfaces;
    for (const auto parent : parents)
        AddInterfaceMethods(vm, parent, out, seen);
}

// Dalvik returnTypesAreCompatible: primitives and void match exactly, references by assignment.
[[nodiscard]] bool ReturnCompatible(DexClassLinker& linker, const Candidate& sub, const Candidate& base) {
    if (sub.return_type == base.return_type) return true;
    if (IsPrimitive(linker, sub.return_type) || IsPrimitive(linker, base.return_type)) return false;
    return linker.IsAssignable(base.return_type, sub.return_type);
}

// Dalvik updateExceptionClassList: an exception stays declared if every duplicate allows it.
[[nodiscard]] std::vector<DexClassId> IntersectThrows(DexClassLinker& linker, const std::vector<DexClassId>& left,
                                                      const std::vector<DexClassId>& right) {
    std::vector<DexClassId> merged;
    const auto add = [&merged](const DexClassId type) {
        if (std::find(merged.begin(), merged.end(), type) == merged.end()) merged.push_back(type);
    };
    for (const auto a : left) {
        for (const auto b : right) {
            if (linker.IsAssignable(b, a))
                add(a);
            else if (linker.IsAssignable(a, b))
                add(b);
        }
    }
    return merged;
}

[[nodiscard]] VmObjectRef WrapUndeclared(IntrinsicContext& call, const VmObjectRef thrown) {
    constexpr const char* kWrapper = "Ljava/lang/reflect/UndeclaredThrowableException;";
    auto& linker = call.vm.Linker();
    const auto type = linker.ResolveDescriptor(kWrapper);
    const auto initialized = call.vm.EnsureClassInitialized(type);
    if (initialized.exception.IsValid())
        throw VmJavaThrow{linker.Class(initialized.exception_class).descriptor, initialized.exception_message,
                          initialized.exception};
    const auto wrapper = call.vm.NewIntrinsicInstance(kWrapper);
    const auto root = call.vm.ProtectReferences(std::array{wrapper});
    const auto constructor = linker.FindDirectMethod(type, "<init>", "(Ljava/lang/Throwable;)V");
    if (!constructor) throw VmJavaThrow{"Ljava/lang/InternalError;", "UndeclaredThrowableException has no constructor"};
    const auto outcome = call.vm.Call(*constructor, std::array{VmValue::Ref(wrapper), VmValue::Ref(thrown)});
    if (outcome.exception.IsValid())
        throw VmJavaThrow{linker.Class(outcome.exception_class).descriptor, outcome.exception_message,
                          outcome.exception};
    return wrapper;
}

[[nodiscard]] VmValue ForwardToHandler(IntrinsicContext& call, const Forward& forward) {
    auto& vm = call.vm;
    auto& linker = vm.Linker();
    auto& model = vm.Model();
    const auto handler_field =
        linker.FindFieldRecursive(model.ObjectClass(call.receiver), "h", "Ljava/lang/reflect/InvocationHandler;");
    if (!handler_field) throw VmJavaThrow{"Ljava/lang/InternalError;", "Proxy has no InvocationHandler field"};
    const auto handler = VmObjectRef{
        static_cast<std::uint32_t>(model.InstanceSlots(call.receiver)[linker.Field(*handler_field).slot].bits)};
    if (!handler.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "InvocationHandler is null"};

    auto& reflection = vm.Reflection();
    const auto meta = reflection.FindDeclaredMethodByDescriptor(forward.owner, forward.name, forward.descriptor);
    if (!meta) throw VmJavaThrow{"Ljava/lang/InternalError;", "proxied method has no reflection metadata"};
    const auto method_object = reflection.MaterializeMethod(*meta);
    const auto method_root = vm.ProtectReferences(std::array{method_object});

    // Dalvik passes null, not an empty array, for a method without parameters.
    VmObjectRef boxed;
    if (!meta->parameter_types.empty()) {
        const auto count = static_cast<JniSize>(meta->parameter_types.size());
        boxed = model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/Object;"),
                                     linker.ResolveDescriptor("Ljava/lang/Object;"), count);
    }
    const auto boxed_root = vm.ProtectReferences(std::array{boxed});
    for (std::size_t index = 0; index < meta->parameter_types.size(); ++index) {
        const auto value = reflection.Codec().BoxReturn(meta->parameter_types[index], call.arguments[index]);
        model.SetObjectElement(boxed, static_cast<JniSize>(index), value);
    }

    const auto handler_class = model.ObjectClass(handler);
    const auto slot = linker.FindVtableIndex(handler_class, "invoke", kInvokeDescriptor);
    if (!slot) throw VmJavaThrow{"Ljava/lang/AbstractMethodError;", "InvocationHandler.invoke"};
    const auto outcome = vm.Call(linker.Class(handler_class).vtable[*slot],
                                 std::array{VmValue::Ref(handler), VmValue::Ref(call.receiver),
                                            VmValue::Ref(method_object), VmValue::Ref(boxed)});
    if (outcome.exception.IsValid()) {
        const auto exception_root = vm.ProtectReferences(std::array{outcome.exception});
        const auto thrown = outcome.exception_class;
        const auto unchecked = linker.IsAssignable(linker.ResolveDescriptor("Ljava/lang/RuntimeException;"), thrown) ||
                               linker.IsAssignable(linker.ResolveDescriptor("Ljava/lang/Error;"), thrown);
        const auto declared = std::any_of(forward.throws.begin(), forward.throws.end(), [&](const DexClassId allowed) {
            return linker.IsAssignable(allowed, thrown);
        });
        if (unchecked || declared)
            throw VmJavaThrow{linker.Class(thrown).descriptor, outcome.exception_message, outcome.exception};
        const auto wrapper = WrapUndeclared(call, outcome.exception);
        throw VmJavaThrow{linker.Class(model.ObjectClass(wrapper)).descriptor, outcome.exception_message, wrapper};
    }

    const auto& return_descriptor = linker.Class(forward.return_type).descriptor;
    if (return_descriptor == "V") return VmValue::Void();
    const auto result = outcome.value.ref;
    const auto result_root = vm.ProtectReferences(std::array{result});
    if (!result.IsValid()) {
        if (IsPrimitive(linker, forward.return_type))
            throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null result when primitive expected"};
        return VmValue::Ref(VmObjectRef{});
    }
    const auto class_cast = [&] {
        return VmJavaThrow{"Ljava/lang/ClassCastException;", linker.Class(model.ObjectClass(result)).descriptor +
                                                                 " cannot be cast to " + return_descriptor};
    };
    if (!IsPrimitive(linker, forward.return_type)) {
        if (!linker.IsAssignable(forward.return_type, model.ObjectClass(result))) throw class_cast();
        return VmValue::Ref(result);
    }
    try {
        return reflection.Codec().ConvertArgument(result, forward.return_type);
    } catch (const VmJavaThrow& error) {
        if (error.descriptor != "Ljava/lang/IllegalArgumentException;") throw;
        throw class_cast();
    }
}

[[nodiscard]] IntrinsicMethodDecl VirtualDecl(const std::string& name, const std::string& descriptor,
                                              const std::uint32_t flags, Forward forward) {
    IntrinsicMethodDecl decl;
    decl.name = name;
    decl.descriptor = descriptor;
    decl.access_flags = flags;
    decl.overridable = false;
    decl.invoke_kind = DeclaredInvokeKind::virtual_call;
    decl.implementation = [forward = std::make_shared<const Forward>(std::move(forward))](IntrinsicContext& call) {
        return ForwardToHandler(call, *forward);
    };
    return decl;
}

}  // namespace

DexClassId GenerateProxyClass(IntrinsicContext& call, const std::string& name, const VmObjectRef interfaces,
                              const VmObjectRef loader) {
    auto& vm = call.vm;
    auto& linker = vm.Linker();
    auto& model = vm.Model();
    if (!interfaces.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "interfaces == null"};
    if (name.empty()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "proxy class name is empty"};

    VmClassLoaderId defining_loader = kBootstrapLoader;
    if (loader.IsValid()) {
        const auto role = vm.ClassLoaders().RoleOf(loader);
        if (!role) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "proxy class loader is not a VM loader"};
        }
        defining_loader = *role;
    }

    std::vector<DexClassId> interface_ids;
    const auto interface_count = model.ArrayLength(interfaces);
    for (JniSize index = 0; index < interface_count; ++index) {
        const auto element = model.GetObjectElement(interfaces, index);
        if (!element.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "interface is null"};
        interface_ids.push_back(model.ClassOfClassObject(element));
    }

    const auto object_class = linker.ResolveDescriptor("Ljava/lang/Object;");
    std::vector<Candidate> all;
    for (const auto& [method_name, descriptor] :
         {std::pair{"equals", "(Ljava/lang/Object;)Z"}, std::pair{"hashCode", "()I"},
          std::pair{"toString", "()Ljava/lang/String;"}}) {
        const auto slot = linker.FindVtableIndex(object_class, method_name, descriptor);
        if (!slot) throw VmJavaThrow{"Ljava/lang/InternalError;", "java.lang.Object method is missing"};
        all.push_back(MakeCandidate(vm, linker.Class(object_class).vtable[*slot]));
    }
    std::set<std::uint32_t> seen;
    for (const auto id : interface_ids)
        AddInterfaceMethods(vm, id, all, seen);

    // Dalvik copyWithoutDuplicates: a signature is published where its best member (the one
    // whose return type every duplicate accepts) appears. Other return types of that signature
    // stay reachable as hidden bridges because dispatch here matches full descriptors.
    std::vector<IntrinsicMethodDecl> methods;
    std::set<std::string> handled;
    constexpr auto kFlags = kAccPublic | kAccNative;
    for (const auto& candidate : all) {
        const auto key = candidate.ParameterKey();
        if (handled.contains(key)) continue;
        std::vector<const Candidate*> group;
        for (const auto& other : all) {
            if (other.ParameterKey() == key) group.push_back(&other);
        }
        if (!std::all_of(group.begin(), group.end(),
                         [&](const Candidate* other) { return ReturnCompatible(linker, candidate, *other); }))
            continue;
        handled.insert(key);
        auto throws = candidate.throws;
        for (const auto* other : group) {
            if (other != &candidate) throws = IntersectThrows(linker, throws, other->throws);
        }
        const auto forward_for = [&](const Candidate& member) {
            return Forward{candidate.method,     candidate.owner,    candidate.name,
                           candidate.descriptor, member.return_type, throws};
        };
        auto decl = VirtualDecl(candidate.name, candidate.descriptor, kFlags, forward_for(candidate));
        decl.must_override = candidate.owner == object_class;
        methods.push_back(std::move(decl));
        std::set<std::string> descriptors{candidate.descriptor};
        for (const auto* other : group) {
            if (!descriptors.insert(other->descriptor).second) continue;
            methods.push_back(
                VirtualDecl(other->name, other->descriptor, kFlags | kAccBridge | kAccSynthetic, forward_for(*other)));
        }
    }
    if (std::any_of(all.begin(), all.end(),
                    [&](const Candidate& item) { return !handled.contains(item.ParameterKey()); })) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "incompatible return types in proxied interfaces"};
    }

    IntrinsicMethodDecl constructor;
    constructor.name = "<init>";
    constructor.descriptor = "(Ljava/lang/reflect/InvocationHandler;)V";
    constructor.access_flags = kAccPublic | kAccNative | kAccConstructor;
    constructor.invoke_kind = DeclaredInvokeKind::direct;
    constructor.implementation = [](IntrinsicContext& context) {
        auto& context_linker = context.vm.Linker();
        auto& context_model = context.vm.Model();
        const auto field = context_linker.FindFieldRecursive(context_model.ObjectClass(context.receiver), "h",
                                                             "Ljava/lang/reflect/InvocationHandler;");
        if (!field) throw VmJavaThrow{"Ljava/lang/InternalError;", "Proxy has no InvocationHandler field"};
        context_model.InstanceSlots(context.receiver)[context_linker.Field(*field).slot] = {
            context.arguments[0].ref.Value(), SlotTag::ref};
        return VmValue::Void();
    };
    methods.push_back(std::move(constructor));

    return linker.DefineProxyClass("L" + name + ";", linker.ResolveDescriptor("Ljava/lang/reflect/Proxy;"),
                                   std::move(interface_ids), defining_loader, methods);
}

}  // namespace ogplay::runtime::dexvm
