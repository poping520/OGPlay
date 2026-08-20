#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_BootClassLoader() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/BootClassLoader;", "Ljava/lang/ClassLoader;", {}, 0U);
    builder.UnimplementedConstructor("()V");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
