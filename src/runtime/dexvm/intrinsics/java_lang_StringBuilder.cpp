#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_StringBuilder() {
    IntrinsicClassBuilder builder("Ljava/lang/StringBuilder;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Ljava/lang/CharSequence;");
    builder.Virtual("<init>", "()V",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(I)V",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver).clear();
                return VmValue::Void();
            });
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) =
                    Value(context, context.arguments[0].ref);
                return VmValue::Void();
            });
    builder.Virtual("append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                const auto argument = context.arguments[0].ref;
                context.vm.BuilderBuffer(context.receiver) +=
                    argument.IsValid() ? Value(context, argument)
                                       : std::u16string(u"null");
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(Ljava/lang/Object;)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                const auto argument = context.arguments[0].ref;
                auto& buffer = context.vm.BuilderBuffer(context.receiver);
                if (!argument.IsValid()) {
                    buffer += u"null";
                } else {
                    auto& model = context.vm.Model();
                    const auto kind = model.Kind(argument);
                    if (kind == VmObjectKind::string ||
                        kind == VmObjectKind::external) {
                        buffer += Value(context, argument);
                    } else {
                        buffer += Widen("@" + std::to_string(argument.Value()));
                    }
                }
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(I)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) +=
                    Widen(std::to_string(context.arguments[0].AsInt()));
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(J)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) +=
                    Widen(std::to_string(context.arguments[0].AsLong()));
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(Z)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) +=
                    context.arguments[0].AsInt() != 0 ? u"true"
                                                      : std::u16string(u"false");
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(C)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) += static_cast<char16_t>(
                    context.arguments[0].cat1 & 0xffffU);
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(F)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) +=
                    Widen(std::to_string(context.arguments[0].AsFloat()));
                return BuilderSelf(context);
            });
    builder.Virtual("append", "(D)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                context.vm.BuilderBuffer(context.receiver) +=
                    Widen(std::to_string(context.arguments[0].AsDouble()));
                return BuilderSelf(context);
            });
    builder.Virtual("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
                return Make(context, context.vm.BuilderBuffer(context.receiver));
            });
    builder.Virtual("length", "()I",
        [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                    context.vm.BuilderBuffer(context.receiver).size()));
            });
    builder.Virtual("charAt", "(I)C",
        [](IntrinsicContext& context) {
                auto& buffer = context.vm.BuilderBuffer(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckBuilderIndex(buffer, index);
                return VmValue::Int(buffer[static_cast<std::size_t>(index)]);
            });
    builder.Virtual("setCharAt", "(IC)V",
        [](IntrinsicContext& context) {
                auto& buffer = context.vm.BuilderBuffer(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckBuilderIndex(buffer, index);
                buffer[static_cast<std::size_t>(index)] = static_cast<char16_t>(
                    context.arguments[1].cat1 & 0xffffU);
                return VmValue::Void();
            });
    builder.Virtual("deleteCharAt", "(I)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                auto& buffer = context.vm.BuilderBuffer(context.receiver);
                const auto index = context.arguments[0].AsInt();
                CheckBuilderIndex(buffer, index);
                buffer.erase(buffer.begin() + index);
                return VmValue::Ref(context.receiver);
            });
    builder.Virtual("insert", "(IC)Ljava/lang/StringBuilder;",
        [](IntrinsicContext& context) {
                auto& buffer = context.vm.BuilderBuffer(context.receiver);
                const auto index = context.arguments[0].AsInt();
                if (index < 0 || static_cast<std::size_t>(index) > buffer.size()) {
                    throw VmJavaThrow{
                        "Ljava/lang/StringIndexOutOfBoundsException;",
                        "insert offset " + std::to_string(index)};
                }
                buffer.insert(buffer.begin() + index,
                              static_cast<char16_t>(context.arguments[1].cat1 &
                                                    0xffffU));
                return VmValue::Ref(context.receiver);
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
