#pragma once

#include <cstdint>
#include <string>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {
    class IntrinsicClassBuilder final {
    public:
        explicit IntrinsicClassBuilder(std::string descriptor);

        IntrinsicClassBuilder& Super(std::string descriptor);

        IntrinsicClassBuilder& Implements(std::string descriptor);

        IntrinsicClassBuilder& MarkInterface();

        IntrinsicClassBuilder& Static(std::string name, std::string descriptor, IntrinsicHandler handler);

        IntrinsicClassBuilder& Virtual(std::string name, std::string descriptor, IntrinsicHandler handler);

        IntrinsicClassBuilder& Overridable(std::string name, std::string descriptor, IntrinsicHandler handler);

        IntrinsicClassBuilder& Unimplemented(std::string name, std::string descriptor, bool is_static,
                                             bool overridable);

        IntrinsicClassBuilder& Field(std::string name, std::string descriptor, bool is_static);

        IntrinsicClassBuilder& ConstantInt(std::string name, std::string descriptor, std::int64_t value);

        IntrinsicClassBuilder& ConstantString(std::string name, std::string value);

        IntrinsicClassBuilder& Clinit(IntrinsicHandler handler);

        [[nodiscard]] IntrinsicClassDecl Build() &&;

    private:
        IntrinsicClassBuilder& Method(std::string name, std::string descriptor, bool is_static, bool overridable,
                                      IntrinsicHandler handler);

        IntrinsicClassDecl declaration_;
    };
} // namespace ogplay::runtime::dexvm
