#include "ogplay/runtime/dexvm/reflection.h"

#include <array>
#include <string>

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {
namespace {
VmValue Call(Interpreter& vm, VmMethodId method, std::span<const VmValue> args) {
    const auto result = vm.Call(method, args);
    if (result.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(result.exception_class).descriptor,
                          result.exception_message, result.exception};
    return result.value;
}
VmMethodId Method(Interpreter& vm, DexClassId type, std::string_view name, std::string_view descriptor) {
    const auto found = vm.Linker().FindDirectMethod(type, std::string(name), std::string(descriptor));
    if (!found) {
        const auto slot = vm.Linker().FindVtableIndex(type, std::string(name), std::string(descriptor));
        if (slot) return vm.Linker().Class(type).vtable[*slot];
    }
    if (!found) throw DexVmError(DexVmErrorReason::unresolved_reference, "generic reflection method " + std::string(name));
    return *found;
}
}

VmObjectRef ReflectGenericType(Interpreter& vm, VmObjectRef declaration, std::string_view query) {
    const auto& kind = vm.Linker().Class(vm.Model().ObjectClass(declaration)).descriptor;
    DexClassId owner;
    std::optional<std::u16string> signature;
    VmObjectRef generic_declaration = declaration;
    std::string parse;
    VmObjectRef exceptions;
    if (kind == "Ljava/lang/Class;") {
        owner = vm.Model().ClassOfClassObject(declaration);
        signature = vm.Linker().Class(owner).generic_signature;
        parse = "parseForClass";
    } else if (kind == "Ljava/lang/reflect/Field;") {
        const auto meta = vm.Reflection().FieldMetadata(declaration);
        owner = meta.declaring_class;
        signature = vm.Linker().Field(meta.field).generic_signature;
        generic_declaration = vm.Model().ClassObject(owner);
        if (!signature) return vm.Model().ClassObject(meta.type);
        parse = "parseForField";
    } else if (kind == "Ljava/lang/reflect/Method;") {
        const auto meta = vm.Reflection().MethodMetadata(declaration);
        owner = meta.declaring_class;
        signature = vm.Linker().Method(meta.method).generic_signature;
        exceptions = vm.Reflection().MaterializeTypeArray(meta.exception_types);
        parse = "parseForMethod";
    } else {
        const auto meta = vm.Reflection().ConstructorMetadata(declaration);
        owner = meta.declaring_class;
        signature = vm.Linker().Method(meta.method).generic_signature;
        exceptions = vm.Reflection().MaterializeTypeArray(meta.exception_types);
        parse = "parseForConstructor";
    }
    const auto input_roots = vm.ProtectReferences(std::array{declaration, generic_declaration, exceptions});
    auto& linker = vm.Linker();
    const auto parser_type = linker.ResolveDescriptor("Llibcore/reflect/GenericSignatureParser;");
    const auto initialized = vm.EnsureClassInitialized(parser_type);
    if (initialized.exception.IsValid()) throw VmJavaThrow{linker.Class(initialized.exception_class).descriptor, initialized.exception_message, initialized.exception};
    const auto parser = vm.Model().NewInstance(parser_type, linker.Class(parser_type).instance_slots);
    const auto parser_root = vm.ProtectReferences(std::array{parser});
    const auto loader = vm.ClassLoaders().LoaderForClass(owner);
    Call(vm, Method(vm, parser_type, "<init>", "(Ljava/lang/ClassLoader;)V"), std::array{VmValue::Ref(parser), VmValue::Ref(loader)});
    const auto text = signature ? vm.Model().NewString(*signature) : VmObjectRef{};
    const auto text_root = vm.ProtectReferences(std::array{text});
    if (parse == "parseForClass" || parse == "parseForField")
        Call(vm, Method(vm, parser_type, parse, "(Ljava/lang/reflect/GenericDeclaration;Ljava/lang/String;)V"),
             std::array{VmValue::Ref(parser), VmValue::Ref(generic_declaration), VmValue::Ref(text)});
    else
        Call(vm, Method(vm, parser_type, parse, "(Ljava/lang/reflect/GenericDeclaration;Ljava/lang/String;[Ljava/lang/Class;)V"),
             std::array{VmValue::Ref(parser), VmValue::Ref(generic_declaration), VmValue::Ref(text), VmValue::Ref(exceptions)});
    std::string field, descriptor;
    if (query == "getTypeParameters") { field = "formalTypeParameters"; descriptor = "[Ljava/lang/reflect/TypeVariable;"; }
    else if (query == "getGenericSuperclass") { field = "superclassType"; descriptor = "Ljava/lang/reflect/Type;"; }
    else if (query == "getGenericInterfaces") { field = "interfaceTypes"; descriptor = "Llibcore/reflect/ListOfTypes;"; }
    else if (query == "getGenericType") { field = "fieldType"; descriptor = "Ljava/lang/reflect/Type;"; }
    else if (query == "getGenericReturnType") { field = "returnType"; descriptor = "Ljava/lang/reflect/Type;"; }
    else { field = query == "getGenericExceptionTypes" ? "exceptionTypes" : "parameterTypes"; descriptor = "Llibcore/reflect/ListOfTypes;"; }
    const auto id = linker.FindFieldRecursive(parser_type, field, descriptor);
    if (!id) throw DexVmError(DexVmErrorReason::unresolved_reference, "generic parser field " + field);
    const auto value = VmObjectRef(vm.Model().InstanceSlots(parser)[linker.Field(*id).slot].bits);
    if (query == "getTypeParameters") return value;
    const auto types = linker.ResolveDescriptor("Llibcore/reflect/Types;");
    if (descriptor == "Llibcore/reflect/ListOfTypes;")
        return Call(vm, Method(vm, types, "getTypeArray", "(Llibcore/reflect/ListOfTypes;Z)[Ljava/lang/reflect/Type;"),
                    std::array{VmValue::Ref(value), VmValue::Int(1)}).ref;
    return Call(vm, Method(vm, types, "getType", "(Ljava/lang/reflect/Type;)Ljava/lang/reflect/Type;"), std::array{VmValue::Ref(value)}).ref;
}
}  // namespace ogplay::runtime::dexvm
