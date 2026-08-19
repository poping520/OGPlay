#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_HashMap() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/HashMap;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [](IntrinsicContext& context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Constructor("(I)V",
        [](IntrinsicContext &context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.FinalMethod("put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
                auto& entries = context.vm.MapStorage(context.receiver);
                const auto key = context.arguments[0].ref;
                const auto value = context.arguments[1].ref;
                const auto found = MapFind(context, key);
                if (found >= 0) {
              const auto previous = entries[static_cast<std::size_t>(found)].second;
                    entries[static_cast<std::size_t>(found)].second = value;
                    return VmValue::Ref(previous);
                }
                entries.emplace_back(key, value);
                return VmValue::Ref(VmObjectRef{});
            });
    builder.FinalMethod("get", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
                const auto found = MapFind(context, context.arguments[0].ref);
            if (found < 0)
              return VmValue::Ref(VmObjectRef{});
            return VmValue::Ref(
                context.vm.MapStorage(context.receiver)[static_cast<std::size_t>(found)]
                                            .second);
            });
    builder.FinalMethod("containsKey", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext &context) {
                return VmValue::Int(
                    MapFind(context, context.arguments[0].ref) >= 0 ? 1 : 0);
            });
    builder.FinalMethod("remove", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& entries = context.vm.MapStorage(context.receiver);
                const auto found = MapFind(context, context.arguments[0].ref);
            if (found < 0)
              return VmValue::Ref(VmObjectRef{});
            const auto previous = entries[static_cast<std::size_t>(found)].second;
                entries.erase(entries.begin() + found);
                return VmValue::Ref(previous);
            });
    builder.FinalMethod("size", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.MapStorage(context.receiver).size()));
            });
    builder.FinalMethod("clear", "()V",
        [](IntrinsicContext& context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.FinalMethod("isEmpty", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.MapStorage(context.receiver).empty() ? 1
                                                                                : 0);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
