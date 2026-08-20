#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/AccessibleObject;", "Ljava/lang/Object;",
        {"Ljava/lang/reflect/AnnotatedElement;"});
    builder.InstanceField("flag", "Z", 0U);
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    }, 0x0004U);
    builder.StaticMethod(
        "setAccessible", "([Ljava/lang/reflect/AccessibleObject;Z)V",
        [](IntrinsicContext& context) {
            const auto objects = context.arguments[0].ref;
            if (!objects.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "objects == null"};
            }
            const auto accessible = context.arguments[1].AsInt() != 0;
            const auto length = context.vm.Model().ArrayLength(objects);
            for (JniSize index = 0; index < length; ++index) {
                const auto object =
                    context.vm.Model().GetObjectElement(objects, index);
                if (!object.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "objects contains null"};
                }
                context.vm.Reflection().SetAccessible(object, accessible);
            }
            return VmValue::Void();
        });
    builder.VirtualMethod("isAccessible", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                context.vm.Reflection().IsAccessible(context.receiver) ? 1 : 0);
        });
    builder.VirtualMethod("setAccessible", "(Z)V",
        [](IntrinsicContext& context) {
            context.vm.Reflection().SetAccessible(
                context.receiver, context.arguments[0].AsInt() != 0);
            return VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
