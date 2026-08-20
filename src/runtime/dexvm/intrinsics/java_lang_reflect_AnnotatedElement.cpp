#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_AnnotatedElement() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/AnnotatedElement;");
    builder.UnimplementedVirtual(
        "isAnnotationPresent", "(Ljava/lang/Class;)Z");
    builder.UnimplementedVirtual(
        "getAnnotation", "(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;");
    builder.UnimplementedVirtual(
        "getAnnotations", "()[Ljava/lang/annotation/Annotation;");
    builder.UnimplementedVirtual(
        "getDeclaredAnnotations", "()[Ljava/lang/annotation/Annotation;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
