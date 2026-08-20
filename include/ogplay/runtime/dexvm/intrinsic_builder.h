#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {
    class Interpreter;

    // Stable declaration token resolved to a VmFieldId by each linker that
    // consumes the catalog. Handlers capture this value instead of repeating
    // owner/name/descriptor lookups or touching raw field slots.
    class IntrinsicFieldHandle final {
    public:
        [[nodiscard]] bool IsValid() const noexcept { return token_ != 0U; }

    private:
        explicit IntrinsicFieldHandle(std::uint64_t token) : token_(token) {}
        std::uint64_t token_{};
        friend class IntrinsicCall;
        friend class IntrinsicClassBuilder;
    };

    // Typed façade for intrinsic handlers. Descriptor mismatches are VM
    // invariants; NonNullRef reports the Java-visible null contract.
    class IntrinsicCall final {
    public:
        explicit IntrinsicCall(IntrinsicContext& context) : context_(&context) {}

        [[nodiscard]] Interpreter& Vm() const noexcept;
        [[nodiscard]] VmObjectRef Receiver() const;
        [[nodiscard]] std::int32_t Int(std::size_t index) const;
        [[nodiscard]] std::int64_t Long(std::size_t index) const;
        [[nodiscard]] float Float(std::size_t index) const;
        [[nodiscard]] double Double(std::size_t index) const;
        [[nodiscard]] VmObjectRef Ref(std::size_t index) const;
        [[nodiscard]] VmObjectRef NonNullRef(
            std::size_t index, std::string_view parameter) const;

        [[nodiscard]] std::int32_t GetInt(IntrinsicFieldHandle field) const;
        [[nodiscard]] std::int32_t GetInt(IntrinsicFieldHandle field,
                                          VmObjectRef object) const;
        void SetInt(IntrinsicFieldHandle field, std::int32_t value) const;
        void SetInt(IntrinsicFieldHandle field, VmObjectRef object,
                    std::int32_t value) const;
        [[nodiscard]] std::int64_t GetLong(IntrinsicFieldHandle field) const;
        [[nodiscard]] std::int64_t GetLong(IntrinsicFieldHandle field,
                                           VmObjectRef object) const;
        void SetLong(IntrinsicFieldHandle field, std::int64_t value) const;
        void SetLong(IntrinsicFieldHandle field, VmObjectRef object,
                     std::int64_t value) const;
        [[nodiscard]] float GetFloat(IntrinsicFieldHandle field) const;
        [[nodiscard]] float GetFloat(IntrinsicFieldHandle field,
                                     VmObjectRef object) const;
        void SetFloat(IntrinsicFieldHandle field, float value) const;
        void SetFloat(IntrinsicFieldHandle field, VmObjectRef object,
                      float value) const;
        [[nodiscard]] double GetDouble(IntrinsicFieldHandle field) const;
        [[nodiscard]] double GetDouble(IntrinsicFieldHandle field,
                                       VmObjectRef object) const;
        void SetDouble(IntrinsicFieldHandle field, double value) const;
        void SetDouble(IntrinsicFieldHandle field, VmObjectRef object,
                       double value) const;
        [[nodiscard]] VmObjectRef GetRef(IntrinsicFieldHandle field) const;
        [[nodiscard]] VmObjectRef GetRef(IntrinsicFieldHandle field,
                                         VmObjectRef object) const;
        void SetRef(IntrinsicFieldHandle field, VmObjectRef value) const;
        void SetRef(IntrinsicFieldHandle field, VmObjectRef object,
                    VmObjectRef value) const;

    private:
        IntrinsicContext* context_{};
    };

    class IntrinsicClassBuilder final {
    public:

        // Defines a class without a superclass.
        // Primarily used for java.lang.Object.
        [[nodiscard]] static IntrinsicClassBuilder RootClass(
            std::string descriptor, std::uint32_t access_flags = 0x0001U);

        [[nodiscard]] static IntrinsicClassBuilder Class(
            std::string descriptor,
            std::optional<std::string> superclass = std::string{"Ljava/lang/Object;"},
            std::vector<std::string> interfaces = {},
            std::uint32_t access_flags = 0x0001U);

        [[nodiscard]] static IntrinsicClassBuilder Interface(
            std::string descriptor,
            std::vector<std::string> super_interfaces = {},
            std::uint32_t access_flags = 0x0601U);

        IntrinsicClassBuilder& Constructor(std::string descriptor,
                                           IntrinsicHandler handler,
                                           std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& DirectMethod(
            std::string name, std::string descriptor,
            IntrinsicHandler handler,
            std::uint32_t access_flags = 0x0002U);

        IntrinsicClassBuilder& StaticMethod(std::string name,
                                            std::string descriptor,
                                            IntrinsicHandler handler,
                                            std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& VirtualMethod(std::string name,
                                             std::string descriptor,
                                             IntrinsicHandler handler,
                                             std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& FinalMethod(std::string name,
                                           std::string descriptor,
                                           IntrinsicHandler handler,
                                           std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& UnimplementedStatic(std::string name,
                                                   std::string descriptor,
                                                   std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& UnimplementedConstructor(
            std::string descriptor, std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& UnimplementedDirect(
            std::string name, std::string descriptor,
            std::uint32_t access_flags = 0x0002U);

        IntrinsicClassBuilder& UnimplementedVirtual(std::string name,
                                                    std::string descriptor,
                                                    std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& UnimplementedFinal(std::string name,
                                                  std::string descriptor,
                                                  std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& InstanceField(std::string name,
                                             std::string descriptor,
                                             std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& StaticField(std::string name,
                                           std::string descriptor,
                                           std::uint32_t access_flags = 0x0001U);

        [[nodiscard]] IntrinsicFieldHandle BoundInstanceField(
            std::string name, std::string descriptor,
            std::uint32_t access_flags = 0x0001U);

        [[nodiscard]] IntrinsicFieldHandle BoundStaticField(
            std::string name, std::string descriptor,
            std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& ConstantInt(
            std::string name, std::string descriptor, std::int64_t value,
            std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& ConstantString(
            std::string name, std::string value,
            std::uint32_t access_flags = 0x0001U);

        IntrinsicClassBuilder& ClassInitializer(IntrinsicHandler handler);

        IntrinsicClassBuilder& HostStateDestructor(
            ogplay::runtime::dexvm::HostStateDestructor destructor);

        [[nodiscard]] IntrinsicClassDecl Build() &&;

    private:
        enum class MethodType : std::uint8_t {
            constructor,
            direct_method,
            static_method,
            virtual_method,
            final_method,
        };

        enum class FieldType : std::uint8_t {
            instance,
            static_field,
        };

        explicit IntrinsicClassBuilder(std::string descriptor);

        IntrinsicClassBuilder& Method(std::string name,
                                      std::string descriptor,
                                      MethodType type,
                                      IntrinsicHandler handler,
                                      std::uint32_t access_flags);

        IntrinsicClassBuilder& UnimplementedMethod(std::string name,
                                                   std::string descriptor,
                                                   MethodType type,
                                                   std::uint32_t access_flags);

        IntrinsicClassBuilder& Field(std::string name,
                                     std::string descriptor,
                                     FieldType type,
                                     std::uint32_t access_flags);

        [[nodiscard]] IntrinsicFieldHandle BoundField(
            std::string name, std::string descriptor, FieldType type,
            std::uint32_t access_flags);

        IntrinsicClassDecl declaration_;
    };
} // namespace ogplay::runtime::dexvm
