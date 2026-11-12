#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

class Interpreter;
class ReflectionCodec;

[[nodiscard]] VmObjectRef ReflectGenericType(Interpreter& vm, VmObjectRef declaration, std::string_view query);

struct ReflectMethodMeta final {
    std::uint32_t slot{};
    VmMethodId method;
    DexClassId declaring_class;
    std::uint32_t access_flags{};
    DeclaredInvokeKind invoke_kind{DeclaredInvokeKind::direct};
    std::vector<DexClassId> parameter_types;
    DexClassId return_type;
    std::vector<DexClassId> exception_types;
};

struct ReflectConstructorMeta final {
    std::uint32_t slot{};
    VmMethodId method;
    DexClassId declaring_class;
    std::uint32_t access_flags{};
    std::vector<DexClassId> parameter_types;
    std::vector<DexClassId> exception_types;
};

struct ReflectFieldMeta final {
    std::uint32_t slot{};
    VmFieldId field;
    DexClassId declaring_class;
    DexClassId type;
    std::uint32_t access_flags{};
};

// Immutable member metadata plus the sole guest-wrapper factory. Public guest
// fields remain opaque; host lookup identity is never a raw pointer.
class ReflectionRuntime final {
public:
    ReflectionRuntime(Interpreter& interpreter, DexClassLinker& linker, JavaObjectModel& model);
    ~ReflectionRuntime();
    ReflectionRuntime(const ReflectionRuntime&) = delete;
    ReflectionRuntime& operator=(const ReflectionRuntime&) = delete;

    [[nodiscard]] std::span<const ReflectMethodMeta> DeclaredMethods(DexClassId declaring_class);
    [[nodiscard]] std::span<const ReflectConstructorMeta> DeclaredConstructors(DexClassId declaring_class);
    [[nodiscard]] std::span<const ReflectFieldMeta> DeclaredFields(DexClassId declaring_class);
    [[nodiscard]] std::vector<ReflectMethodMeta> PublicMethods(DexClassId java_class);
    [[nodiscard]] std::vector<ReflectFieldMeta> PublicFields(DexClassId java_class);

    [[nodiscard]] std::optional<ReflectMethodMeta> FindDeclaredMethod(DexClassId declaring_class, std::string_view name,
                                                                      std::span<const DexClassId> parameter_types);
    // JNI IDs select the exact descriptor, including the return type.
    [[nodiscard]] std::optional<ReflectMethodMeta>
    FindDeclaredMethodByDescriptor(DexClassId declaring_class, std::string_view name, std::string_view descriptor);
    [[nodiscard]] std::optional<ReflectMethodMeta> FindPublicMethod(DexClassId java_class, std::string_view name,
                                                                    std::span<const DexClassId> parameter_types);
    [[nodiscard]] std::optional<ReflectConstructorMeta>
    FindConstructor(DexClassId declaring_class, std::span<const DexClassId> parameter_types, bool public_only);
    [[nodiscard]] std::optional<ReflectFieldMeta> FindDeclaredField(DexClassId declaring_class, std::string_view name);
    [[nodiscard]] std::optional<ReflectFieldMeta> FindPublicField(DexClassId java_class, std::string_view name);

    [[nodiscard]] VmObjectRef MaterializeMethod(const ReflectMethodMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeConstructor(const ReflectConstructorMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeField(const ReflectFieldMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeDeclaredMethods(DexClassId declaring_class);
    [[nodiscard]] VmObjectRef MaterializeMethods(std::span<const ReflectMethodMeta> methods);
    [[nodiscard]] VmObjectRef MaterializeConstructors(std::span<const ReflectConstructorMeta> constructors);
    [[nodiscard]] VmObjectRef MaterializeFields(std::span<const ReflectFieldMeta> fields);
    [[nodiscard]] VmObjectRef MaterializeTypeArray(std::span<const DexClassId> types);

    [[nodiscard]] const ReflectMethodMeta& MethodMetadata(VmObjectRef wrapper);
    [[nodiscard]] const ReflectConstructorMeta& ConstructorMetadata(VmObjectRef wrapper);
    [[nodiscard]] const ReflectFieldMeta& FieldMetadata(VmObjectRef wrapper);

    // Slots index own direct methods followed by own virtual methods (constructors included).
    [[nodiscard]] const ReflectMethodMeta& MethodMetadata(DexClassId declaring, std::uint32_t slot);
    [[nodiscard]] const ReflectConstructorMeta& ConstructorMetadata(DexClassId declaring, std::uint32_t slot);
    [[nodiscard]] const ReflectFieldMeta& FieldMetadata(DexClassId declaring, std::uint32_t slot);
    [[nodiscard]] VmObjectRef InvokeMethod(const ReflectMethodMeta& meta, VmObjectRef receiver, VmObjectRef arguments,
                                           std::optional<DexClassId> caller, bool no_access_check);
    [[nodiscard]] VmObjectRef InvokeConstructor(const ReflectConstructorMeta& meta, VmObjectRef arguments,
                                                std::optional<DexClassId> caller, bool no_access_check);
    [[nodiscard]] VmValue GetField(const ReflectFieldMeta& meta, VmObjectRef receiver,
                                   std::optional<DexClassId> requested_type, std::optional<DexClassId> caller,
                                   bool no_access_check);
    void SetField(const ReflectFieldMeta& meta, VmObjectRef receiver, VmValue value,
                  std::optional<DexClassId> source_type, std::optional<DexClassId> caller, bool no_access_check);

    [[nodiscard]] bool SemanticallyEqual(VmObjectRef left, VmObjectRef right);
    [[nodiscard]] bool IsAccessible(VmObjectRef wrapper) const;
    void SetAccessible(VmObjectRef wrapper, bool accessible);
    [[nodiscard]] ReflectionCodec& Codec() noexcept;
    [[nodiscard]] VmObjectRef NewInstance(DexClassId java_class, std::optional<DexClassId> caller);

    // API19 serialization VM primitives; tokens are local opaque ordinals.
    [[nodiscard]] std::int64_t SerializationConstructor(DexClassId declaring_class);
    [[nodiscard]] VmObjectRef NewSerializationInstance(DexClassId java_class, std::int64_t token);
    [[nodiscard]] std::string_view MemberDescriptor(VmObjectRef wrapper);

    [[nodiscard]] VmValue GetField(VmObjectRef wrapper, VmObjectRef receiver, std::optional<DexClassId> requested_type,
                                   std::optional<DexClassId> caller);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
