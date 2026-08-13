#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_List() {
    IntrinsicClassBuilder builder("Ljava/util/List;");
    builder.MarkInterface();
    builder.Virtual("add", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
                context.vm.ListStorage(context.receiver)
                    .push_back(context.arguments[0].ref);
                return VmValue::Int(1);
            });
    builder.Virtual("get", "(I)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                return VmValue::Ref(elements[static_cast<std::size_t>(index)]);
            });
    builder.Virtual("size", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.ListStorage(context.receiver).size()));
            });
    builder.Virtual("iterator", "()Ljava/util/Iterator;",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto instance =
                    vm.NewIntrinsicInstance("Ljava/util/CollectionIterator;");
                const auto slots = vm.Model().InstanceSlots(instance);
                slots[0] = {context.receiver.Value(), SlotTag::ref};
                slots[1] = {0, SlotTag::cat1};
                return VmValue::Ref(instance);
            });
    builder.Virtual("contains", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
                const auto& elements = context.vm.ListStorage(context.receiver);
                for (const auto element : elements) {
                    if (context.vm.JavaEquals(element, context.arguments[0].ref)) {
                        return VmValue::Int(1);
                    }
                }
                return VmValue::Int(0);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
