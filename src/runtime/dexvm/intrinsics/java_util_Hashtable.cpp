#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Hashtable() {
    IntrinsicClassBuilder builder("Ljava/util/Hashtable;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext& context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(I)V",
        [](IntrinsicContext &context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
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
    builder.Virtual("get", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
                const auto found = MapFind(context, context.arguments[0].ref);
            if (found < 0)
              return VmValue::Ref(VmObjectRef{});
            return VmValue::Ref(
                context.vm.MapStorage(context.receiver)[static_cast<std::size_t>(found)]
                                            .second);
            });
    builder.Virtual("containsKey", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext &context) {
                return VmValue::Int(
                    MapFind(context, context.arguments[0].ref) >= 0 ? 1 : 0);
            });
    builder.Virtual("remove", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& entries = context.vm.MapStorage(context.receiver);
                const auto found = MapFind(context, context.arguments[0].ref);
            if (found < 0)
              return VmValue::Ref(VmObjectRef{});
            const auto previous = entries[static_cast<std::size_t>(found)].second;
                entries.erase(entries.begin() + found);
                return VmValue::Ref(previous);
            });
    builder.Virtual("size", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.MapStorage(context.receiver).size()));
            });
    builder.Virtual("clear", "()V",
        [](IntrinsicContext& context) {
                context.vm.MapStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("isEmpty", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.MapStorage(context.receiver).empty() ? 1
                                                                                : 0);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
