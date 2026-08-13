#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Stack() {
    IntrinsicClassBuilder builder("Ljava/util/Stack;");
    builder.Super("Ljava/util/Vector;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext& context) {
                context.vm.ListStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("push", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
            const auto value = context.arguments[0].ref;
            context.vm.ListStorage(context.receiver).push_back(value);
            return VmValue::Ref(value);
          });
    builder.Virtual("pop", "()Ljava/lang/Object;",
        [](IntrinsicContext &context) {
            auto &elements = context.vm.ListStorage(context.receiver);
            if (elements.empty()) {
              throw VmJavaThrow{"Ljava/util/EmptyStackException;", "empty stack"};
            }
            const auto value = elements.back();
            elements.pop_back();
            return VmValue::Ref(value);
          });
    builder.Virtual("peek", "()Ljava/lang/Object;",
        [](IntrinsicContext &context) {
            const auto &elements = context.vm.ListStorage(context.receiver);
            if (elements.empty()) {
              throw VmJavaThrow{"Ljava/util/EmptyStackException;", "empty stack"};
            }
            return VmValue::Ref(elements.back());
          });
    builder.Virtual("empty", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.ListStorage(context.receiver).empty() ? 1
                                                                                 : 0);
            });
    builder.Virtual("search", "(Ljava/lang/Object;)I",
        [](IntrinsicContext &context) {
            const auto &elements = context.vm.ListStorage(context.receiver);
            std::int32_t distance = 1;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it, ++distance) {
              if (context.vm.JavaEquals(*it, context.arguments[0].ref)) {
                return VmValue::Int(distance);
              }
            }
            return VmValue::Int(-1);
          });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
