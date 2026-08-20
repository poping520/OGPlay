#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

class Interpreter;

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

// Immutable member metadata plus the sole guest-wrapper factory. Reflection
// slots are declaring-class-local opaque ordinals, never linker/Dex ids.
class ReflectionRuntime final {
public:
    ReflectionRuntime(Interpreter& interpreter, DexClassLinker& linker,
                      JavaObjectModel& model);
    ~ReflectionRuntime();
    ReflectionRuntime(const ReflectionRuntime&) = delete;
    ReflectionRuntime& operator=(const ReflectionRuntime&) = delete;

    [[nodiscard]] std::span<const ReflectMethodMeta> DeclaredMethods(
        DexClassId declaring_class);
    [[nodiscard]] std::span<const ReflectConstructorMeta> DeclaredConstructors(
        DexClassId declaring_class);
    [[nodiscard]] std::span<const ReflectFieldMeta> DeclaredFields(
        DexClassId declaring_class);

    [[nodiscard]] VmObjectRef MaterializeMethod(const ReflectMethodMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeConstructor(
        const ReflectConstructorMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeField(const ReflectFieldMeta& meta);
    [[nodiscard]] VmObjectRef MaterializeDeclaredMethods(
        DexClassId declaring_class);
    [[nodiscard]] VmObjectRef MaterializeTypeArray(
        std::span<const DexClassId> types);

    [[nodiscard]] const ReflectMethodMeta& MethodMetadata(
        VmObjectRef wrapper);
    [[nodiscard]] const ReflectConstructorMeta& ConstructorMetadata(
        VmObjectRef wrapper);
    [[nodiscard]] const ReflectFieldMeta& FieldMetadata(VmObjectRef wrapper);

    [[nodiscard]] bool SemanticallyEqual(VmObjectRef left,
                                         VmObjectRef right);
    [[nodiscard]] bool IsAccessible(VmObjectRef wrapper) const;
    void SetAccessible(VmObjectRef wrapper, bool accessible);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime::dexvm
