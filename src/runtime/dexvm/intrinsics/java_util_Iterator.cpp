#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Iterator() {
    auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Iterator;");
    builder.FinalMethod("hasNext", "()Z",
        [](IntrinsicContext &context) {
            const auto slots = context.vm.Model().InstanceSlots(context.receiver);
            const auto &elements = context.vm.ListStorage(VmObjectRef(slots[0].bits));
                return VmValue::Int(
                static_cast<std::size_t>(slots[1].bits) < elements.size() ? 1 : 0);
            });
    builder.FinalMethod("next", "()Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            const auto slots = context.vm.Model().InstanceSlots(context.receiver);
            const auto &elements = context.vm.ListStorage(VmObjectRef(slots[0].bits));
                const auto index = static_cast<std::size_t>(slots[1].bits);
                if (index >= elements.size()) {
                    throw VmJavaThrow{"Ljava/util/NoSuchElementException;",
                                      "iterator is exhausted"};
                }
                slots[1] = {static_cast<std::uint32_t>(index + 1), SlotTag::cat1};
                return VmValue::Ref(elements[index]);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
