#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
using namespace detail;

IntrinsicClassDecl Declare_java_lang_Math() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Math;", "Ljava/lang/Object;");
    builder.StaticMethod("abs", "(I)I",
        [](IntrinsicContext& context) {
                const auto value = context.arguments[0].AsInt();
                return VmValue::Int(value < 0 ? -value : value);
            });
    builder.StaticMethod("abs", "(J)J",
        [](IntrinsicContext& context) {
                const auto value = context.arguments[0].AsLong();
                return VmValue::Long(value < 0 ? -value : value);
            });
    builder.StaticMethod("abs", "(F)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fabs(context.arguments[0].AsFloat()));
            });
    builder.StaticMethod("abs", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::fabs(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("max", "(II)I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                std::max(context.arguments[0].AsInt(), context.arguments[1].AsInt()));
            });
    builder.StaticMethod("min", "(II)I",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                std::min(context.arguments[0].AsInt(), context.arguments[1].AsInt()));
            });
    builder.StaticMethod("max", "(FF)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fmax(context.arguments[0].AsFloat(),
                                                context.arguments[1].AsFloat()));
            });
    builder.StaticMethod("min", "(FF)F",
        [](IntrinsicContext& context) {
                return VmValue::Float(std::fmin(context.arguments[0].AsFloat(),
                                                context.arguments[1].AsFloat()));
            });
    builder.StaticMethod("sqrt", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::sqrt(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("sin", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::sin(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("cos", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::cos(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("atan2", "(DD)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::atan2(context.arguments[0].AsDouble(),
                                                  context.arguments[1].AsDouble()));
            });
    builder.StaticMethod("pow", "(DD)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::pow(context.arguments[0].AsDouble(),
                                                context.arguments[1].AsDouble()));
            });
    builder.StaticMethod("floor", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::floor(context.arguments[0].AsDouble()));
            });
    builder.StaticMethod("ceil", "(D)D",
        [](IntrinsicContext& context) {
                return VmValue::Double(std::ceil(context.arguments[0].AsDouble()));
            });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics
