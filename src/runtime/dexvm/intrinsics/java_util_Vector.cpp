#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Vector() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Vector;", "Ljava/lang/Object;", {"Ljava/util/List;"});
    builder.Constructor("()V",
        [](IntrinsicContext& context) {
                context.vm.ListStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Constructor("(I)V",
        [](IntrinsicContext &context) {
                context.vm.ListStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.FinalMethod("add", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
                context.vm.ListStorage(context.receiver)
                    .push_back(context.arguments[0].ref);
                return VmValue::Int(1);
            });
    builder.FinalMethod("addElement", "(Ljava/lang/Object;)V",
        [](IntrinsicContext &context) {
                context.vm.ListStorage(context.receiver)
                    .push_back(context.arguments[0].ref);
                return VmValue::Void();
            });
    builder.FinalMethod("get", "(I)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                return VmValue::Ref(elements[static_cast<std::size_t>(index)]);
            });
    builder.FinalMethod("elementAt", "(I)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                return VmValue::Ref(elements[static_cast<std::size_t>(index)]);
            });
    builder.FinalMethod("set", "(ILjava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                const auto previous = elements[static_cast<std::size_t>(index)];
            elements[static_cast<std::size_t>(index)] = context.arguments[1].ref;
                return VmValue::Ref(previous);
            });
    builder.FinalMethod("setElementAt", "(Ljava/lang/Object;I)V",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[1].AsInt();
                CheckListIndex(elements, index);
                elements[static_cast<std::size_t>(index)] = context.arguments[0].ref;
                return VmValue::Void();
            });
    builder.FinalMethod("removeElementAt", "(I)V",
        [](IntrinsicContext& context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                elements.erase(elements.begin() + index);
                return VmValue::Void();
            });
    builder.FinalMethod("remove", "(I)Ljava/lang/Object;",
        [](IntrinsicContext &context) {
                auto& elements = context.vm.ListStorage(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckListIndex(elements, index);
                const auto previous = elements[static_cast<std::size_t>(index)];
                elements.erase(elements.begin() + index);
                return VmValue::Ref(previous);
            });
    builder.FinalMethod("size", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.ListStorage(context.receiver).size()));
            });
    builder.FinalMethod("clear", "()V",
        [](IntrinsicContext& context) {
                context.vm.ListStorage(context.receiver).clear();
                return VmValue::Void();
            });
    builder.FinalMethod("isEmpty", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.ListStorage(context.receiver).empty() ? 1
                                                                                 : 0);
            });
    builder.FinalMethod("iterator", "()Ljava/util/Iterator;",
        [](IntrinsicContext& context) {
                auto& vm = context.vm;
                const auto instance =
                    vm.NewIntrinsicInstance("Ljava/util/CollectionIterator;");
                const auto slots = vm.Model().InstanceSlots(instance);
                slots[0] = {context.receiver.Value(), SlotTag::ref};
                slots[1] = {0, SlotTag::cat1};
                return VmValue::Ref(instance);
            });
    builder.FinalMethod("contains", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
                const auto& elements = context.vm.ListStorage(context.receiver);
                for (const auto element : elements) {
                    if (context.vm.JavaEquals(element, context.arguments[0].ref)) {
                        return VmValue::Int(1);
                    }
                }
                return VmValue::Int(0);
            });
    builder.FinalMethod("copyInto", "([Ljava/lang/Object;)V",
        [](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto& elements = context.vm.ListStorage(context.receiver);
                const auto target = context.arguments[0].ref;
                if (!target.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "copyInto target array is null"};
                }
            if (static_cast<std::size_t>(model.ArrayLength(target)) < elements.size()) {
                    throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                      "copyInto target array is too small"};
                }
                for (std::size_t index = 0; index < elements.size(); ++index) {
                    model.SetObjectElement(target, static_cast<JniSize>(index),
                                           elements[index]);
                }
                return VmValue::Void();
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
