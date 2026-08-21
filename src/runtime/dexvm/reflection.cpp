#include "ogplay/runtime/dexvm/reflection.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm {
namespace {

struct ClassReflectionMetadata final {
    bool built{};
    std::vector<ReflectMethodMeta> methods;
    std::vector<ReflectConstructorMeta> constructors;
    std::vector<ReflectFieldMeta> fields;
};

constexpr std::uint32_t kAccPublic = 0x0001U;

template <typename Meta>
[[nodiscard]] bool SameParameters(
    const Meta& meta, const std::span<const DexClassId> parameters) {
    return meta.parameter_types.size() == parameters.size() &&
           std::equal(meta.parameter_types.begin(), meta.parameter_types.end(),
                      parameters.begin());
}

[[nodiscard]] std::uint32_t ReadIntField(DexClassLinker& linker,
                                         JavaObjectModel& model,
                                         const VmObjectRef object,
                                         const std::string_view name,
                                         const std::string_view descriptor =
                                             "I") {
    const auto field = linker.FindFieldRecursive(
        model.ObjectClass(object), std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection wrapper int field is missing: " +
                             std::string(name));
    }
    return model.InstanceSlots(object)[linker.Field(*field).slot].bits;
}

[[nodiscard]] VmObjectRef ReadRefField(DexClassLinker& linker,
                                       JavaObjectModel& model,
                                       const VmObjectRef object,
                                       const std::string_view name,
                                       const std::string_view descriptor) {
    const auto field = linker.FindFieldRecursive(
        model.ObjectClass(object), std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection wrapper ref field is missing: " +
                             std::string(name));
    }
    return VmObjectRef(
        model.InstanceSlots(object)[linker.Field(*field).slot].bits);
}

void WriteIntField(DexClassLinker& linker, JavaObjectModel& model,
                   const VmObjectRef object, const std::string_view name,
                   const std::uint32_t value,
                   const std::string_view descriptor = "I") {
    const auto field = linker.FindFieldRecursive(
        model.ObjectClass(object), std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection wrapper int field is missing: " +
                             std::string(name));
    }
    model.InstanceSlots(object)[linker.Field(*field).slot] = {
        value, SlotTag::cat1};
}

void WriteRefField(DexClassLinker& linker, JavaObjectModel& model,
                   const VmObjectRef object, const std::string_view name,
                   const std::string_view descriptor,
                   const VmObjectRef value) {
    const auto field = linker.FindFieldRecursive(
        model.ObjectClass(object), std::string(name), std::string(descriptor));
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection wrapper ref field is missing: " +
                             std::string(name));
    }
    model.InstanceSlots(object)[linker.Field(*field).slot] = {
        value.Value(), SlotTag::ref};
}

}  // namespace

class ReflectionRuntime::Impl final {
public:
    Impl(Interpreter& interpreter, DexClassLinker& linker,
         JavaObjectModel& model)
        : interpreter(&interpreter), linker(&linker), model(&model) {}

    ClassReflectionMetadata& Metadata(const DexClassId declaring_class) {
        auto& result = metadata[declaring_class.Value()];
        if (result.built) return result;
        ClassReflectionMetadata pending;

        try {
            linker->EnsureClassLinked(declaring_class);
        } catch (const DexVmError& error) {
            throw DexVmError(
                error.Reason(),
                "reflection metadata link failed for " +
                    linker->Class(declaring_class).descriptor + ": " +
                    error.what());
        }
        // Descriptor resolution may synthesize classes and grow the linker
        // directory. Copy ids before doing so; never retain a LinkedClass
        // reference across ResolveDescriptor.
        const auto direct_methods =
            linker->Class(declaring_class).own_direct_methods;
        const auto virtual_methods =
            linker->Class(declaring_class).own_virtual_methods;
        const auto static_fields =
            linker->Class(declaring_class).own_static_fields;
        const auto instance_fields =
            linker->Class(declaring_class).own_instance_fields;
        for (const auto method_id : direct_methods) {
            const auto& method = linker->Method(method_id);
            if (method.name == "<init>") {
                const auto parsed = ClassNameCodec::ParseMethod(
                    method.descriptor);
                ReflectConstructorMeta item;
                item.slot = static_cast<std::uint32_t>(
                    pending.constructors.size());
                item.method = method_id;
                item.declaring_class = declaring_class;
                item.access_flags = method.access_flags;
                for (const auto& type : parsed.parameters) {
                    item.parameter_types.push_back(
                        linker->ResolveDescriptor(type));
                }
                pending.constructors.push_back(std::move(item));
            } else if (method.name != "<clinit>") {
                AddMethodChecked(pending, declaring_class, method_id);
            }
        }
        for (const auto method_id : virtual_methods) {
            AddMethodChecked(pending, declaring_class, method_id);
        }
        for (const auto field_id : static_fields) {
            AddField(pending, declaring_class, field_id);
        }
        for (const auto field_id : instance_fields) {
            AddField(pending, declaring_class, field_id);
        }
        pending.built = true;
        result = std::move(pending);
        return result;
    }

    void AddMethod(ClassReflectionMetadata& target,
                   const DexClassId declaring_class,
                   const VmMethodId method_id) {
        const auto& method = linker->Method(method_id);
        const auto parsed = ClassNameCodec::ParseMethod(method.descriptor);
        ReflectMethodMeta item;
        item.slot = static_cast<std::uint32_t>(target.methods.size());
        item.method = method_id;
        item.declaring_class = declaring_class;
        item.access_flags = method.access_flags;
        item.invoke_kind = method.declared_invoke_kind;
        for (const auto& type : parsed.parameters) {
            item.parameter_types.push_back(linker->ResolveDescriptor(type));
        }
        item.return_type = linker->ResolveDescriptor(parsed.return_type);
        target.methods.push_back(std::move(item));
    }

    void AddMethodChecked(ClassReflectionMetadata& target,
                          const DexClassId declaring_class,
                          const VmMethodId method_id) {
        const auto member = linker->Method(method_id).name +
                            linker->Method(method_id).descriptor;
        try {
            AddMethod(target, declaring_class, method_id);
        } catch (const DexVmError& error) {
            throw DexVmError(
                error.Reason(), "reflection metadata method " + member +
                                    " failed: " + error.what());
        }
    }

    void AddField(ClassReflectionMetadata& target,
                  const DexClassId declaring_class,
                  const VmFieldId field_id) {
        const auto& field = linker->Field(field_id);
        ReflectFieldMeta item;
        item.slot = static_cast<std::uint32_t>(target.fields.size());
        item.field = field_id;
        item.declaring_class = declaring_class;
        item.type = linker->ResolveDescriptor(field.descriptor);
        item.access_flags = field.access_flags;
        target.fields.push_back(std::move(item));
    }

    [[nodiscard]] VmObjectRef TypeArray(
        const std::span<const DexClassId> types) {
        const auto class_class = linker->ResolveDescriptor("Ljava/lang/Class;");
        const auto array_class = linker->ResolveDescriptor("[Ljava/lang/Class;");
        const auto array = model->NewObjectArray(
            array_class, class_class, static_cast<JniSize>(types.size()));
        for (std::size_t index = 0; index < types.size(); ++index) {
            model->SetObjectElement(array, static_cast<JniSize>(index),
                                    model->ClassObject(types[index]));
        }
        return array;
    }

    [[nodiscard]] VmObjectRef Allocate(const std::string_view descriptor) {
        const auto java_class = linker->ResolveDescriptor(descriptor);
        const auto& linked = linker->Class(java_class);
        return model->NewInstance(java_class, linked.instance_slots);
    }

    void CommonMemberFields(const VmObjectRef wrapper,
                            const DexClassId declaring_class,
                            const std::uint32_t slot) {
        WriteIntField(*linker, *model, wrapper, "slot", slot);
        WriteRefField(*linker, *model, wrapper, "declaringClass",
                      "Ljava/lang/Class;",
                      model->ClassObject(declaring_class));
    }

    template <typename Append>
    void VisitPublicTypes(const DexClassId java_class, Append&& append) {
        std::unordered_set<std::uint32_t> visited_interfaces;
        const auto visit_interface = [&](const auto& self,
                                         const DexClassId interface_id)
            -> void {
            if (!visited_interfaces.insert(interface_id.Value()).second) {
                return;
            }
            append(interface_id);
            const auto parents = linker->Class(interface_id).direct_interfaces;
            for (const auto parent : parents) self(self, parent);
        };

        std::vector<DexClassId> class_chain;
        auto current = std::optional<DexClassId>(java_class);
        while (current.has_value()) {
            const auto linked = linker->Class(*current);
            class_chain.push_back(*current);
            current = linked.is_interface ? std::nullopt : linked.super;
        }
        for (const auto class_id : class_chain) append(class_id);
        for (const auto class_id : class_chain) {
            const auto interfaces = linker->Class(class_id).direct_interfaces;
            for (const auto interface_id : interfaces) {
                visit_interface(visit_interface, interface_id);
            }
        }
    }

    Interpreter* interpreter{};
    DexClassLinker* linker{};
    JavaObjectModel* model{};
    std::unordered_map<std::uint32_t, ClassReflectionMetadata> metadata;
};

ReflectionRuntime::ReflectionRuntime(Interpreter& interpreter,
                                     DexClassLinker& linker,
                                     JavaObjectModel& model)
    : impl_(std::make_unique<Impl>(interpreter, linker, model)) {}

ReflectionRuntime::~ReflectionRuntime() = default;

std::span<const ReflectMethodMeta> ReflectionRuntime::DeclaredMethods(
    const DexClassId declaring_class) {
    return impl_->Metadata(declaring_class).methods;
}

std::span<const ReflectConstructorMeta>
ReflectionRuntime::DeclaredConstructors(const DexClassId declaring_class) {
    return impl_->Metadata(declaring_class).constructors;
}

std::span<const ReflectFieldMeta> ReflectionRuntime::DeclaredFields(
    const DexClassId declaring_class) {
    return impl_->Metadata(declaring_class).fields;
}

std::vector<ReflectMethodMeta> ReflectionRuntime::PublicMethods(
    const DexClassId java_class) {
    std::vector<ReflectMethodMeta> result;
    std::unordered_set<std::string> signatures;
    impl_->VisitPublicTypes(java_class, [&](const DexClassId current) {
        for (const auto& meta : DeclaredMethods(current)) {
            if ((meta.access_flags & kAccPublic) == 0U) continue;
            const auto& method = impl_->linker->Method(meta.method);
            const auto signature = method.name + method.descriptor;
            if (signatures.insert(signature).second) result.push_back(meta);
        }
    });
    return result;
}

std::vector<ReflectFieldMeta> ReflectionRuntime::PublicFields(
    const DexClassId java_class) {
    std::vector<ReflectFieldMeta> result;
    std::unordered_set<std::uint32_t> fields;
    impl_->VisitPublicTypes(java_class, [&](const DexClassId current) {
        for (const auto& meta : DeclaredFields(current)) {
            if ((meta.access_flags & kAccPublic) != 0U &&
                fields.insert(meta.field.Value()).second) {
                result.push_back(meta);
            }
        }
    });
    return result;
}

std::optional<ReflectMethodMeta> ReflectionRuntime::FindDeclaredMethod(
    const DexClassId declaring_class, const std::string_view name,
    const std::span<const DexClassId> parameter_types) {
    for (const auto& meta : DeclaredMethods(declaring_class)) {
        if (impl_->linker->Method(meta.method).name == name &&
            SameParameters(meta, parameter_types)) {
            return meta;
        }
    }
    return std::nullopt;
}

std::optional<ReflectMethodMeta> ReflectionRuntime::FindPublicMethod(
    const DexClassId java_class, const std::string_view name,
    const std::span<const DexClassId> parameter_types) {
    for (const auto& meta : PublicMethods(java_class)) {
        if (impl_->linker->Method(meta.method).name == name &&
            SameParameters(meta, parameter_types)) {
            return meta;
        }
    }
    return std::nullopt;
}

std::optional<ReflectConstructorMeta> ReflectionRuntime::FindConstructor(
    const DexClassId declaring_class,
    const std::span<const DexClassId> parameter_types,
    const bool public_only) {
    for (const auto& meta : DeclaredConstructors(declaring_class)) {
        if ((!public_only || (meta.access_flags & kAccPublic) != 0U) &&
            SameParameters(meta, parameter_types)) {
            return meta;
        }
    }
    return std::nullopt;
}

std::optional<ReflectFieldMeta> ReflectionRuntime::FindDeclaredField(
    const DexClassId declaring_class, const std::string_view name) {
    for (const auto& meta : DeclaredFields(declaring_class)) {
        if (impl_->linker->Field(meta.field).name == name) return meta;
    }
    return std::nullopt;
}

std::optional<ReflectFieldMeta> ReflectionRuntime::FindPublicField(
    const DexClassId java_class, const std::string_view name) {
    for (const auto& meta : PublicFields(java_class)) {
        if (impl_->linker->Field(meta.field).name == name) return meta;
    }
    return std::nullopt;
}

VmObjectRef ReflectionRuntime::MaterializeMethod(
    const ReflectMethodMeta& meta) {
    std::string_view stage = "allocate";
    try {
        const auto wrapper = impl_->Allocate("Ljava/lang/reflect/Method;");
        stage = "common fields";
        impl_->CommonMemberFields(wrapper, meta.declaring_class, meta.slot);
        const auto& method = impl_->linker->Method(meta.method);
        WriteIntField(*impl_->linker, *impl_->model, wrapper, "methodDexIndex",
                      0xffffffffU);
        stage = "name";
        WriteRefField(*impl_->linker, *impl_->model, wrapper, "name",
                      "Ljava/lang/String;",
                      impl_->interpreter->NewStringUtf8(method.name));
        stage = "parameter types";
        WriteRefField(*impl_->linker, *impl_->model, wrapper, "parameterTypes",
                      "[Ljava/lang/Class;",
                      impl_->TypeArray(meta.parameter_types));
        stage = "exception types";
        WriteRefField(*impl_->linker, *impl_->model, wrapper, "exceptionTypes",
                      "[Ljava/lang/Class;",
                      impl_->TypeArray(meta.exception_types));
        stage = "return type";
        WriteRefField(*impl_->linker, *impl_->model, wrapper, "returnType",
                      "Ljava/lang/Class;",
                      impl_->model->ClassObject(meta.return_type));
        return wrapper;
    } catch (const DexVmError& error) {
        throw DexVmError(error.Reason(),
                         "reflection Method " + std::string(stage) +
                             " failed: " + error.what());
    }
}

VmObjectRef ReflectionRuntime::MaterializeConstructor(
    const ReflectConstructorMeta& meta) {
    const auto wrapper = impl_->Allocate("Ljava/lang/reflect/Constructor;");
    impl_->CommonMemberFields(wrapper, meta.declaring_class, meta.slot);
    WriteIntField(*impl_->linker, *impl_->model, wrapper, "methodDexIndex",
                  0xffffffffU);
    WriteRefField(*impl_->linker, *impl_->model, wrapper, "parameterTypes",
                  "[Ljava/lang/Class;", impl_->TypeArray(meta.parameter_types));
    WriteRefField(*impl_->linker, *impl_->model, wrapper, "exceptionTypes",
                  "[Ljava/lang/Class;", impl_->TypeArray(meta.exception_types));
    return wrapper;
}

VmObjectRef ReflectionRuntime::MaterializeField(const ReflectFieldMeta& meta) {
    const auto wrapper = impl_->Allocate("Ljava/lang/reflect/Field;");
    impl_->CommonMemberFields(wrapper, meta.declaring_class, meta.slot);
    const auto& field = impl_->linker->Field(meta.field);
    WriteIntField(*impl_->linker, *impl_->model, wrapper, "fieldDexIndex",
                  0xffffffffU);
    WriteRefField(*impl_->linker, *impl_->model, wrapper, "name",
                  "Ljava/lang/String;",
                  impl_->interpreter->NewStringUtf8(field.name));
    WriteRefField(*impl_->linker, *impl_->model, wrapper, "type",
                  "Ljava/lang/Class;", impl_->model->ClassObject(meta.type));
    return wrapper;
}

VmObjectRef ReflectionRuntime::MaterializeDeclaredMethods(
    const DexClassId declaring_class) {
    return MaterializeMethods(DeclaredMethods(declaring_class));
}

VmObjectRef ReflectionRuntime::MaterializeMethods(
    const std::span<const ReflectMethodMeta> methods) {
    const auto method_class =
        impl_->linker->ResolveDescriptor("Ljava/lang/reflect/Method;");
    const auto array_class =
        impl_->linker->ResolveDescriptor("[Ljava/lang/reflect/Method;");
    const auto array = impl_->model->NewObjectArray(
        array_class, method_class, static_cast<JniSize>(methods.size()));
    for (std::size_t index = 0; index < methods.size(); ++index) {
        impl_->model->SetObjectElement(
            array, static_cast<JniSize>(index), MaterializeMethod(methods[index]));
    }
    return array;
}

VmObjectRef ReflectionRuntime::MaterializeConstructors(
    const std::span<const ReflectConstructorMeta> constructors) {
    const auto member_class =
        impl_->linker->ResolveDescriptor("Ljava/lang/reflect/Constructor;");
    const auto array_class = impl_->linker->ResolveDescriptor(
        "[Ljava/lang/reflect/Constructor;");
    const auto array = impl_->model->NewObjectArray(
        array_class, member_class, static_cast<JniSize>(constructors.size()));
    for (std::size_t index = 0; index < constructors.size(); ++index) {
        impl_->model->SetObjectElement(
            array, static_cast<JniSize>(index),
            MaterializeConstructor(constructors[index]));
    }
    return array;
}

VmObjectRef ReflectionRuntime::MaterializeFields(
    const std::span<const ReflectFieldMeta> fields) {
    const auto member_class =
        impl_->linker->ResolveDescriptor("Ljava/lang/reflect/Field;");
    const auto array_class =
        impl_->linker->ResolveDescriptor("[Ljava/lang/reflect/Field;");
    const auto array = impl_->model->NewObjectArray(
        array_class, member_class, static_cast<JniSize>(fields.size()));
    for (std::size_t index = 0; index < fields.size(); ++index) {
        impl_->model->SetObjectElement(
            array, static_cast<JniSize>(index), MaterializeField(fields[index]));
    }
    return array;
}

VmObjectRef ReflectionRuntime::MaterializeTypeArray(
    const std::span<const DexClassId> types) {
    return impl_->TypeArray(types);
}

const ReflectMethodMeta& ReflectionRuntime::MethodMetadata(
    const VmObjectRef wrapper) {
    const auto declaring = impl_->model->ClassOfClassObject(ReadRefField(
        *impl_->linker, *impl_->model, wrapper, "declaringClass",
        "Ljava/lang/Class;"));
    const auto slot = ReadIntField(*impl_->linker, *impl_->model, wrapper,
                                   "slot");
    const auto methods = DeclaredMethods(declaring);
    if (slot >= methods.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection Method slot is invalid");
    }
    return methods[slot];
}

const ReflectConstructorMeta& ReflectionRuntime::ConstructorMetadata(
    const VmObjectRef wrapper) {
    const auto declaring = impl_->model->ClassOfClassObject(ReadRefField(
        *impl_->linker, *impl_->model, wrapper, "declaringClass",
        "Ljava/lang/Class;"));
    const auto slot = ReadIntField(*impl_->linker, *impl_->model, wrapper,
                                   "slot");
    const auto constructors = DeclaredConstructors(declaring);
    if (slot >= constructors.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection Constructor slot is invalid");
    }
    return constructors[slot];
}

const ReflectFieldMeta& ReflectionRuntime::FieldMetadata(
    const VmObjectRef wrapper) {
    const auto declaring = impl_->model->ClassOfClassObject(ReadRefField(
        *impl_->linker, *impl_->model, wrapper, "declaringClass",
        "Ljava/lang/Class;"));
    const auto slot = ReadIntField(*impl_->linker, *impl_->model, wrapper,
                                   "slot");
    const auto fields = DeclaredFields(declaring);
    if (slot >= fields.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection Field slot is invalid");
    }
    return fields[slot];
}

bool ReflectionRuntime::SemanticallyEqual(const VmObjectRef left,
                                          const VmObjectRef right) {
    if (!left.IsValid() || !right.IsValid()) return false;
    const auto left_class = impl_->model->ObjectClass(left);
    if (left_class != impl_->model->ObjectClass(right)) return false;
    const auto descriptor = impl_->linker->Class(left_class).descriptor;
    if (descriptor == "Ljava/lang/reflect/Method;") {
        return MethodMetadata(left).method == MethodMetadata(right).method;
    }
    if (descriptor == "Ljava/lang/reflect/Constructor;") {
        return ConstructorMetadata(left).method ==
               ConstructorMetadata(right).method;
    }
    if (descriptor == "Ljava/lang/reflect/Field;") {
        return FieldMetadata(left).field == FieldMetadata(right).field;
    }
    return false;
}

bool ReflectionRuntime::IsAccessible(const VmObjectRef wrapper) const {
    return ReadIntField(*impl_->linker, *impl_->model, wrapper, "flag", "Z") !=
           0U;
}

void ReflectionRuntime::SetAccessible(const VmObjectRef wrapper,
                                      const bool accessible) {
    WriteIntField(*impl_->linker, *impl_->model, wrapper, "flag",
                  accessible ? 1U : 0U, "Z");
}

}  // namespace ogplay::runtime::dexvm
