#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_reflect_Array() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Array;", "Ljava/lang/Object;");
    builder.StaticMethod("newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& vm = context.vm;
                auto& model = vm.Model();
                const auto class_object = context.arguments[0].ref;
                const auto dims_ref = context.arguments[1].ref;
                if (!class_object.IsValid() || !dims_ref.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "Array.newInstance argument is null"};
                }
                const auto element_class = model.ClassOfClassObject(class_object);
                const auto dim_count = model.ArrayLength(dims_ref);
                if (dim_count <= 0 || dim_count > 255) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "invalid dimension count"};
                }
                std::vector<std::int32_t> dims;
                dims.reserve(static_cast<std::size_t>(dim_count));
                for (JniSize index = 0; index < dim_count; ++index) {
                    const auto length = static_cast<std::int32_t>(
                        model.GetPrimitiveElement(dims_ref, index));
                    if (length < 0) {
                        throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;",
                                          std::to_string(length)};
                    }
                    dims.push_back(length);
                }
                const auto element_descriptor =
                    vm.Linker().Class(element_class).descriptor;
                if (element_descriptor == "V") {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "void array"};
                }
                return VmValue::Ref(BuildReflectArray(vm, element_descriptor, dims));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
