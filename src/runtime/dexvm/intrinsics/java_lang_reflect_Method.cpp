#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_reflect_Method() {
    IntrinsicClassBuilder builder("Ljava/lang/reflect/Method;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("vmMethodId", "I", false);
    builder.Field("name", "Ljava/lang/String;", false);
    builder.Virtual("getName", "()Ljava/lang/String;",
        [](IntrinsicContext &context) {
                const auto slots = context.vm.Model().InstanceSlots(context.receiver);
                return VmValue::Ref(VmObjectRef(slots[1].bits));
              });
    builder.Virtual("invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto &vm = context.vm;
                const auto slots = vm.Model().InstanceSlots(context.receiver);
                const auto method_id = VmMethodId(slots[0].bits);
                const auto &method = vm.Linker().Method(method_id);
                const auto arguments = context.arguments[1].ref;
                if (!arguments.IsValid() || vm.Model().ArrayLength(arguments) != 0) {
                  throw VmJavaThrow{
                      "Ljava/lang/UnsupportedOperationException;",
                      "reflective Method.invoke supports zero arguments only"};
                }
                if (method.return_shorty != 'I' && method.return_shorty != 'Z' &&
                    method.return_shorty != 'B' && method.return_shorty != 'C' &&
                    method.return_shorty != 'S') {
                  throw VmJavaThrow{
                      "Ljava/lang/UnsupportedOperationException;",
                      "reflective Method.invoke supports int-like returns only"};
                }
                std::vector<VmValue> call_arguments;
                if (!method.is_static) {
                  if (!context.arguments[0].ref.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "reflective receiver is null"};
                  }
                  call_arguments.push_back(VmValue::Ref(context.arguments[0].ref));
                }
                const auto outcome = vm.Call(method_id, call_arguments);
                if (outcome.exception.IsValid()) {
                  throw VmJavaThrow{"Ljava/lang/RuntimeException;",
                                    "reflected method raised: " +
                                        outcome.exception_message};
                }
                const auto boxed = vm.NewIntrinsicInstance("Ljava/lang/Integer;");
                vm.Model().InstanceSlots(boxed)[0] = {
                    static_cast<std::uint32_t>(outcome.value.AsInt()), SlotTag::cat1};
                return VmValue::Ref(boxed);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
