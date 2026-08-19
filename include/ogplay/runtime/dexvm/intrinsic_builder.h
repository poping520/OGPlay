#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {
    class IntrinsicClassBuilder final {
    public:

        // Defines a class without a superclass.
        // Primarily used for java.lang.Object.
        [[nodiscard]] static IntrinsicClassBuilder RootClass(std::string descriptor);

        [[nodiscard]] static IntrinsicClassBuilder Class(
            std::string descriptor,
            std::optional<std::string> superclass = std::string{"Ljava/lang/Object;"},
            std::vector<std::string> interfaces = {});

        [[nodiscard]] static IntrinsicClassBuilder Interface(
            std::string descriptor,
            std::vector<std::string> super_interfaces = {});

        IntrinsicClassBuilder& Constructor(std::string descriptor,
                                           IntrinsicHandler handler);

        IntrinsicClassBuilder& StaticMethod(std::string name,
                                            std::string descriptor,
                                            IntrinsicHandler handler);

        IntrinsicClassBuilder& VirtualMethod(std::string name,
                                             std::string descriptor,
                                             IntrinsicHandler handler);

        IntrinsicClassBuilder& FinalMethod(std::string name,
                                           std::string descriptor,
                                           IntrinsicHandler handler);

        IntrinsicClassBuilder& UnimplementedStatic(std::string name,
                                                   std::string descriptor);

        IntrinsicClassBuilder& UnimplementedConstructor(std::string descriptor);

        IntrinsicClassBuilder& UnimplementedVirtual(std::string name,
                                                    std::string descriptor);

        IntrinsicClassBuilder& UnimplementedFinal(std::string name,
                                                  std::string descriptor);

        IntrinsicClassBuilder& InstanceField(std::string name,
                                             std::string descriptor);

        IntrinsicClassBuilder& StaticField(std::string name,
                                           std::string descriptor);

        IntrinsicClassBuilder& ConstantInt(std::string name, std::string descriptor, std::int64_t value);

        IntrinsicClassBuilder& ConstantString(std::string name, std::string value);

        IntrinsicClassBuilder& ClassInitializer(IntrinsicHandler handler);

        IntrinsicClassBuilder& HostStateDestructor(
            ogplay::runtime::dexvm::HostStateDestructor destructor);

        [[nodiscard]] IntrinsicClassDecl Build() &&;

    private:
        enum class MethodType : std::uint8_t {
            constructor,
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
                                      IntrinsicHandler handler);

        IntrinsicClassBuilder& UnimplementedMethod(std::string name,
                                                   std::string descriptor,
                                                   MethodType type);

        IntrinsicClassBuilder& Field(std::string name,
                                     std::string descriptor,
                                     FieldType type);

        IntrinsicClassDecl declaration_;
    };
} // namespace ogplay::runtime::dexvm
