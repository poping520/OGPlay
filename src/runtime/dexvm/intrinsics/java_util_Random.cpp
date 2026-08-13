#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Random() {
    const auto engines =
        std::make_shared<std::unordered_map<std::uint32_t, std::mt19937_64>>();
    const auto counter = std::make_shared<std::uint64_t>(0x5DEECE66DULL);
    const auto engine_of =
        [engines](IntrinsicContext& context) -> std::mt19937_64& {
        const auto found = engines->find(context.receiver.Value());
        if (found == engines->end()) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "Random instance was never constructed"};
        }
        return found->second;
    };
    IntrinsicClassBuilder builder("Ljava/util/Random;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V",
        [engines, counter](IntrinsicContext &context) {
                  (*engines)[context.receiver.Value()] = std::mt19937_64((*counter)++);
                    return VmValue::Void();
                });
    builder.Virtual("<init>", "(J)V",
        [engines](IntrinsicContext &context) {
                    (*engines)[context.receiver.Value()] = std::mt19937_64(
                        static_cast<std::uint64_t>(context.arguments[0].AsLong()));
                    return VmValue::Void();
                });
    builder.Virtual("nextDouble", "()D",
        [engine_of](IntrinsicContext &context) {
                    auto& engine = engine_of(context);
                    VmValue out;
                    out.kind = VmValue::Kind::wide;
                  const double value = static_cast<double>(engine() >> 11U) * 0x1.0p-53;
                    out.wide = std::bit_cast<std::uint64_t>(value);
                    return out;
                });
    builder.Virtual("nextInt", "(I)I",
        [engine_of](IntrinsicContext &context) {
                    const auto bound = context.arguments[0].AsInt();
                    if (bound <= 0) {
                        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "bound must be positive"};
                    }
                    return VmValue::Int(static_cast<std::int32_t>(
                      engine_of(context)() % static_cast<std::uint64_t>(bound)));
                });
    builder.Virtual("nextInt", "()I",
        [engine_of](IntrinsicContext &context) {
                  return VmValue::Int(static_cast<std::int32_t>(engine_of(context)()));
                });
    builder.Virtual("nextFloat", "()F",
        [engine_of](IntrinsicContext &context) {
                    auto& engine = engine_of(context);
                  const float value = static_cast<float>(engine() >> 40U) * 0x1.0p-24F;
                    VmValue out = VmValue::Int(0);
                    out.cat1 = std::bit_cast<std::uint32_t>(value);
                    return out;
                });
    builder.Virtual("nextLong", "()J",
        [engine_of](IntrinsicContext &context) {
                  return VmValue::Long(static_cast<std::int64_t>(engine_of(context)()));
                });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
