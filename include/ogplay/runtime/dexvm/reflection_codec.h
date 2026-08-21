#pragma once

#include <span>
#include <vector>

#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

// Shared reflection boundary conversion. Object arguments are checked against
// the exact guest parameter Class and primitive wrappers follow the API-19
// widening matrix; callers never reinterpret all int-like values as Integer.
class ReflectionCodec final {
public:
    ReflectionCodec(Interpreter& interpreter, DexClassLinker& linker,
                    JavaObjectModel& model);

    [[nodiscard]] VmValue ConvertArgument(VmObjectRef argument,
                                          DexClassId target_type);
    [[nodiscard]] std::vector<VmValue> ConvertArguments(
        VmObjectRef arguments,
        std::span<const DexClassId> parameter_types);
    [[nodiscard]] VmObjectRef BoxReturn(DexClassId return_type,
                                        const VmValue& value);

private:
    Interpreter* interpreter_{};
    DexClassLinker* linker_{};
    JavaObjectModel* model_{};
};

}  // namespace ogplay::runtime::dexvm
