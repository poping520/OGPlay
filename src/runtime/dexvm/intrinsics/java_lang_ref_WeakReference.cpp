#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_ref_WeakReference() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ref/WeakReference;", "Ljava/lang/Object;");
    builder.InstanceField("referent", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/Object;)V",
        [](IntrinsicContext &context) {
                const auto slots = context.vm.Model().InstanceSlots(context.receiver);
                slots[0] = {context.arguments[0].ref.Value(), SlotTag::ref};
                return VmValue::Void();
            });
    builder.FinalMethod("get", "()Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                const auto slots = context.vm.Model().InstanceSlots(context.receiver);
                return VmValue::Ref(VmObjectRef(slots[0].bits));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
