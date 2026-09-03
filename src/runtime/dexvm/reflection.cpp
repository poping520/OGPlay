#include "ogplay/runtime/dexvm/reflection.h"

#include "ogplay/runtime/dexvm/access_flags.h"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace ogplay::runtime::dexvm {
namespace {

struct ClassReflectionMetadata final {
    bool built{};
    std::vector<ReflectMethodMeta> methods;
    std::vector<ReflectConstructorMeta> constructors;
    std::vector<ReflectFieldMeta> fields;
};

[[nodiscard]] std::string_view RuntimePackage(
    const std::string_view descriptor) {
    const auto separator = descriptor.rfind('/');
    return separator == std::string_view::npos
        ? std::string_view{}
        : descriptor.substr(0, separator);
}

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

template <typename Meta, typename Materialize>
[[nodiscard]] VmObjectRef MaterializeMemberArray(
    DexClassLinker& linker, JavaObjectModel& model,
    const std::span<const Meta> members,
    const std::string_view member_descriptor,
    const std::string_view array_descriptor, Materialize&& materialize) {
    const auto member_class = linker.ResolveDescriptor(member_descriptor);
    const auto array_class = linker.ResolveDescriptor(array_descriptor);
    const auto array = model.NewObjectArray(
        array_class, member_class, static_cast<JniSize>(members.size()));
    for (std::size_t index = 0; index < members.size(); ++index) {
        model.SetObjectElement(
            array, static_cast<JniSize>(index),
            std::invoke(materialize, members[index]));
    }
    return array;
}

template <typename Meta>
[[nodiscard]] const Meta& MetadataBySlot(
    DexClassLinker& linker, JavaObjectModel& model,
    const VmObjectRef wrapper, const std::span<const Meta> members,
    const std::string_view kind) {
    const auto slot = ReadIntField(linker, model, wrapper, "slot");
    if (slot >= members.size()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "reflection " + std::string(kind) +
                             " slot is invalid");
    }
    return members[slot];
}

}  // namespace

class ReflectionRuntime::Impl final {
public:
    Impl(Interpreter& interpreter, DexClassLinker& linker,
         JavaObjectModel& model)
        : interpreter(&interpreter), linker(&linker), model(&model),
          codec(std::make_unique<ReflectionCodec>(interpreter, linker, model)) {}

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
                item.exception_types =
                    linker->ReflectionExceptionTypes(method_id);
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
        item.exception_types = linker->ReflectionExceptionTypes(method_id);
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

    [[nodiscard]] bool SameRuntimePackage(const DexClassId left,
                                          const DexClassId right) const {
        const auto& left_class = linker->Class(left);
        const auto& right_class = linker->Class(right);
        return left_class.defining_loader == right_class.defining_loader &&
               RuntimePackage(left_class.descriptor) ==
                   RuntimePackage(right_class.descriptor);
    }

    [[nodiscard]] bool CanAccessMethod(
        const ReflectMethodMeta& meta,
        const std::optional<DexClassId> caller,
        const VmObjectRef receiver) const {
        const auto& declaring = linker->Class(meta.declaring_class);
        if (!caller.has_value()) {
            return (meta.access_flags & kAccPublic) != 0U &&
                   (declaring.access_flags & kAccPublic) != 0U;
        }
        if ((declaring.access_flags & kAccPublic) == 0U &&
            !SameRuntimePackage(*caller, meta.declaring_class)) {
            return false;
        }
        if ((meta.access_flags & kAccPublic) != 0U ||
            *caller == meta.declaring_class) {
            return true;
        }
        if ((meta.access_flags & kAccPrivate) != 0U) return false;
        if (SameRuntimePackage(*caller, meta.declaring_class)) return true;
        if ((meta.access_flags & kAccProtected) == 0U ||
            !linker->IsAssignable(meta.declaring_class, *caller)) {
            return false;
        }
        const auto& method = linker->Method(meta.method);
        return method.is_static ||
               (receiver.IsValid() && linker->IsAssignable(
                    *caller, model->ObjectClass(receiver)));
    }

    [[nodiscard]] bool CanAccessMember(
        const DexClassId declaring_class, const std::uint32_t access_flags,
        const std::optional<DexClassId> caller,
        const VmObjectRef protected_receiver = VmObjectRef{}) const {
        const auto& declaring = linker->Class(declaring_class);
        if (!caller.has_value()) {
            return (access_flags & kAccPublic) != 0U &&
                   (declaring.access_flags & kAccPublic) != 0U;
        }
        if ((declaring.access_flags & kAccPublic) == 0U &&
            !SameRuntimePackage(*caller, declaring_class)) {
            return false;
        }
        if ((access_flags & kAccPublic) != 0U || *caller == declaring_class) {
            return true;
        }
        if ((access_flags & kAccPrivate) != 0U) return false;
        if (SameRuntimePackage(*caller, declaring_class)) return true;
        if ((access_flags & kAccProtected) == 0U ||
            !linker->IsAssignable(declaring_class, *caller)) {
            return false;
        }
        return !protected_receiver.IsValid() || linker->IsAssignable(
            *caller, model->ObjectClass(protected_receiver));
    }

    void RequireInstantiable(const DexClassId java_class) const {
        const auto& linked = linker->Class(java_class);
        const bool primitive = linked.descriptor.size() == 1U;
        if (primitive || linked.is_interface || linked.is_array ||
            (linked.access_flags & kAccAbstract) != 0U) {
            throw VmJavaThrow{"Ljava/lang/InstantiationException;",
                              linked.descriptor};
        }
    }

    [[nodiscard]] VmValue ReadFieldValue(const LinkedField& field,
                                         const VmObjectRef receiver) const {
        const auto low = field.is_static
            ? linker->Class(field.owner).static_storage[field.slot]
            : model->InstanceSlots(receiver)[field.slot].bits;
        auto bits = static_cast<std::uint64_t>(low);
        if (field.is_wide) {
            const auto high = field.is_static
                ? linker->Class(field.owner).static_storage[field.slot + 1U]
                : model->InstanceSlots(receiver)[field.slot + 1U].bits;
            bits |= static_cast<std::uint64_t>(high) << 32U;
        }
        if (field.is_ref) return VmValue::Ref(VmObjectRef(low));
        switch (field.descriptor.front()) {
            case 'J': return VmValue::Long(static_cast<std::int64_t>(bits));
            case 'D': return VmValue::Double(std::bit_cast<double>(bits));
            case 'F': return VmValue::Float(
                std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
            default: return VmValue::Int(static_cast<std::int32_t>(bits));
        }
    }

    void WriteFieldValue(const LinkedField& field, const VmObjectRef receiver,
                         const VmValue& value) const {
        const auto bits = field.is_ref
            ? static_cast<std::uint64_t>(value.ref.Value())
            : field.is_wide ? value.wide
                            : static_cast<std::uint64_t>(value.cat1);
        if (field.is_static) {
            auto& storage = linker->MutableClass(field.owner).static_storage;
            storage[field.slot] = static_cast<std::uint32_t>(bits);
            if (field.is_wide) {
                storage[field.slot + 1U] =
                    static_cast<std::uint32_t>(bits >> 32U);
            }
            return;
        }
        auto slots = model->InstanceSlots(receiver);
        slots[field.slot] = {static_cast<std::uint32_t>(bits),
                             field.is_ref ? SlotTag::ref
                                          : field.is_wide ? SlotTag::wide_lo
                                                          : SlotTag::cat1};
        if (field.is_wide) {
            slots[field.slot + 1U] = {
                static_cast<std::uint32_t>(bits >> 32U), SlotTag::wide_hi};
        }
    }

    [[nodiscard]] VmObjectRef InvocationTargetException(
        const VmObjectRef target) const {
        const auto wrapper = interpreter->MakeThrowable(
            "Ljava/lang/reflect/InvocationTargetException;", {});
        WriteRefField(*linker, *model, wrapper, "target",
                      "Ljava/lang/Throwable;", target);
        return wrapper;
    }

    Interpreter* interpreter{};
    DexClassLinker* linker{};
    JavaObjectModel* model{};
    std::unique_ptr<ReflectionCodec> codec;
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
    return MaterializeMemberArray(
        *impl_->linker, *impl_->model, methods,
        "Ljava/lang/reflect/Method;", "[Ljava/lang/reflect/Method;",
        [this](const ReflectMethodMeta& meta) {
            return MaterializeMethod(meta);
        });
}

VmObjectRef ReflectionRuntime::MaterializeConstructors(
    const std::span<const ReflectConstructorMeta> constructors) {
    return MaterializeMemberArray(
        *impl_->linker, *impl_->model, constructors,
        "Ljava/lang/reflect/Constructor;",
        "[Ljava/lang/reflect/Constructor;",
        [this](const ReflectConstructorMeta& meta) {
            return MaterializeConstructor(meta);
        });
}

VmObjectRef ReflectionRuntime::MaterializeFields(
    const std::span<const ReflectFieldMeta> fields) {
    return MaterializeMemberArray(
        *impl_->linker, *impl_->model, fields,
        "Ljava/lang/reflect/Field;", "[Ljava/lang/reflect/Field;",
        [this](const ReflectFieldMeta& meta) {
            return MaterializeField(meta);
        });
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
    const auto methods = DeclaredMethods(declaring);
    return MetadataBySlot(*impl_->linker, *impl_->model, wrapper, methods,
                          "Method");
}

const ReflectConstructorMeta& ReflectionRuntime::ConstructorMetadata(
    const VmObjectRef wrapper) {
    const auto declaring = impl_->model->ClassOfClassObject(ReadRefField(
        *impl_->linker, *impl_->model, wrapper, "declaringClass",
        "Ljava/lang/Class;"));
    const auto constructors = DeclaredConstructors(declaring);
    return MetadataBySlot(*impl_->linker, *impl_->model, wrapper, constructors,
                          "Constructor");
}

const ReflectFieldMeta& ReflectionRuntime::FieldMetadata(
    const VmObjectRef wrapper) {
    const auto declaring = impl_->model->ClassOfClassObject(ReadRefField(
        *impl_->linker, *impl_->model, wrapper, "declaringClass",
        "Ljava/lang/Class;"));
    const auto fields = DeclaredFields(declaring);
    return MetadataBySlot(*impl_->linker, *impl_->model, wrapper, fields,
                          "Field");
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

ReflectionCodec& ReflectionRuntime::Codec() noexcept { return *impl_->codec; }

VmObjectRef ReflectionRuntime::InvokeMethod(
    const VmObjectRef wrapper, const VmObjectRef receiver,
    const VmObjectRef arguments, const std::optional<DexClassId> caller) {
    const auto& meta = MethodMetadata(wrapper);
    const auto& declared = impl_->linker->Method(meta.method);

    VmMethodId target = meta.method;
    if (!declared.is_static) {
        if (!receiver.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "reflective receiver is null"};
        }
        const auto receiver_class = impl_->model->ObjectClass(receiver);
        if (!impl_->linker->IsAssignable(meta.declaring_class,
                                         receiver_class)) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "receiver has incompatible type"};
        }
        if (meta.invoke_kind == DeclaredInvokeKind::virtual_call ||
            meta.invoke_kind == DeclaredInvokeKind::interface_call) {
            const auto index = impl_->linker->FindVtableIndex(
                receiver_class, declared.name, declared.descriptor);
            if (!index.has_value()) {
                throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                                  declared.name};
            }
            target = impl_->linker->Class(receiver_class).vtable[*index];
        }
    }

    const auto argument_count = arguments.IsValid()
        ? static_cast<std::size_t>(impl_->model->ArrayLength(arguments))
        : 0U;
    if (argument_count != meta.parameter_types.size()) {
        throw VmJavaThrow{
            "Ljava/lang/IllegalArgumentException;",
            "wrong number of arguments; expected " +
                std::to_string(meta.parameter_types.size()) + ", got " +
                std::to_string(argument_count)};
    }
    if (!IsAccessible(wrapper) &&
        !impl_->CanAccessMethod(meta, caller, receiver)) {
        throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                          "access to method denied"};
    }

    auto call_arguments =
        impl_->codec->ConvertArguments(arguments, meta.parameter_types);
    if (!declared.is_static) {
        call_arguments.insert(call_arguments.begin(), VmValue::Ref(receiver));
    }

    if (declared.is_static ||
        impl_->linker->Class(meta.declaring_class).is_interface) {
        const auto initialized =
            impl_->interpreter->EnsureClassInitialized(meta.declaring_class);
        if (initialized.exception.IsValid()) {
            impl_->interpreter->SetPendingException(initialized.exception);
            return VmObjectRef{};
        }
    }
    const auto outcome = impl_->interpreter->Call(target, call_arguments);
    if (outcome.exception.IsValid()) {
        impl_->interpreter->SetPendingException(
            impl_->InvocationTargetException(outcome.exception));
        return VmObjectRef{};
    }
    return impl_->codec->BoxReturn(meta.return_type, outcome.value);
}

VmObjectRef ReflectionRuntime::InvokeConstructor(
    const VmObjectRef wrapper, const VmObjectRef arguments,
    const std::optional<DexClassId> caller) {
    const auto& meta = ConstructorMetadata(wrapper);
    impl_->RequireInstantiable(meta.declaring_class);
    if (!IsAccessible(wrapper) && !impl_->CanAccessMember(
            meta.declaring_class, meta.access_flags, caller)) {
        throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                          "access to constructor denied"};
    }
    auto call_arguments =
        impl_->codec->ConvertArguments(arguments, meta.parameter_types);
    const auto initialized =
        impl_->interpreter->EnsureClassInitialized(meta.declaring_class);
    if (initialized.exception.IsValid()) {
        impl_->interpreter->SetPendingException(initialized.exception);
        return VmObjectRef{};
    }
    const auto& linked = impl_->linker->Class(meta.declaring_class);
    const auto instance =
        impl_->model->NewInstance(meta.declaring_class, linked.instance_slots);
    call_arguments.insert(call_arguments.begin(), VmValue::Ref(instance));
    const auto outcome = impl_->interpreter->Call(meta.method, call_arguments);
    if (outcome.exception.IsValid()) {
        impl_->interpreter->SetPendingException(
            impl_->InvocationTargetException(outcome.exception));
        return VmObjectRef{};
    }
    return instance;
}

VmObjectRef ReflectionRuntime::NewInstance(
    const DexClassId java_class, const std::optional<DexClassId> caller) {
    impl_->RequireInstantiable(java_class);
    const auto initialized = impl_->interpreter->EnsureClassInitialized(java_class);
    if (initialized.exception.IsValid()) {
        impl_->interpreter->SetPendingException(initialized.exception);
        return VmObjectRef{};
    }
    const auto constructor = FindConstructor(java_class, {}, false);
    if (!constructor.has_value()) {
        throw VmJavaThrow{"Ljava/lang/InstantiationException;",
                          "no empty constructor"};
    }
    if (!impl_->CanAccessMember(java_class, constructor->access_flags, caller)) {
        throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                          "access to constructor denied"};
    }
    const auto& linked = impl_->linker->Class(java_class);
    const auto instance =
        impl_->model->NewInstance(java_class, linked.instance_slots);
    const std::array call_arguments{VmValue::Ref(instance)};
    const auto outcome = impl_->interpreter->Call(
        constructor->method, call_arguments);
    if (outcome.exception.IsValid()) {
        impl_->interpreter->SetPendingException(outcome.exception);
        return VmObjectRef{};
    }
    return instance;
}

VmValue ReflectionRuntime::GetField(
    const VmObjectRef wrapper, const VmObjectRef receiver,
    const std::optional<DexClassId> requested_type,
    const std::optional<DexClassId> caller) {
    const auto& meta = FieldMetadata(wrapper);
    const auto& field = impl_->linker->Field(meta.field);
    if (!IsAccessible(wrapper) && !impl_->CanAccessMember(
            meta.declaring_class, meta.access_flags, caller,
            field.is_static ? VmObjectRef{} : receiver)) {
        throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                          "access to field denied"};
    }
    if (field.is_static) {
        const auto initialized =
            impl_->interpreter->EnsureClassInitialized(meta.declaring_class);
        if (initialized.exception.IsValid()) {
            impl_->interpreter->SetPendingException(initialized.exception);
            return VmValue::Void();
        }
    } else {
        if (!receiver.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "reflective field receiver is null"};
        }
        if (!impl_->linker->IsAssignable(
                meta.declaring_class, impl_->model->ObjectClass(receiver))) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "field receiver has incompatible type"};
        }
    }
    const auto raw = impl_->ReadFieldValue(field, receiver);
    if (!requested_type.has_value()) {
        return VmValue::Ref(impl_->codec->BoxReturn(meta.type, raw));
    }
    const auto& target = impl_->linker->Class(*requested_type).descriptor;
    if (target.size() != 1U || target == "V") {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid primitive field conversion"};
    }
    const auto boxed = impl_->codec->BoxReturn(meta.type, raw);
    return impl_->codec->ConvertArgument(boxed, *requested_type);
}

void ReflectionRuntime::SetField(
    const VmObjectRef wrapper, const VmObjectRef receiver, const VmValue value,
    const std::optional<DexClassId> source_type,
    const std::optional<DexClassId> caller) {
    const auto& meta = FieldMetadata(wrapper);
    const auto& field = impl_->linker->Field(meta.field);
    if (!IsAccessible(wrapper)) {
        if ((meta.access_flags & kAccFinal) != 0U) {
            throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                              "field is marked final"};
        }
        if (!impl_->CanAccessMember(
                meta.declaring_class, meta.access_flags, caller,
                field.is_static ? VmObjectRef{} : receiver)) {
            throw VmJavaThrow{"Ljava/lang/IllegalAccessException;",
                              "access to field denied"};
        }
    }
    if (field.is_static) {
        const auto initialized =
            impl_->interpreter->EnsureClassInitialized(meta.declaring_class);
        if (initialized.exception.IsValid()) {
            impl_->interpreter->SetPendingException(initialized.exception);
            return;
        }
    } else {
        if (!receiver.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "reflective field receiver is null"};
        }
        if (!impl_->linker->IsAssignable(
                meta.declaring_class, impl_->model->ObjectClass(receiver))) {
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "field receiver has incompatible type"};
        }
    }
    VmValue converted;
    if (source_type.has_value()) {
        const auto boxed = impl_->codec->BoxReturn(*source_type, value);
        converted = impl_->codec->ConvertArgument(boxed, meta.type);
    } else {
        converted = impl_->codec->ConvertArgument(value.ref, meta.type);
    }
    impl_->WriteFieldValue(field, receiver, converted);
}

}  // namespace ogplay::runtime::dexvm
