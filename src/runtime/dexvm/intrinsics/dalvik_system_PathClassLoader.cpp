#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_dalvik_system_PathClassLoader() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ldalvik/system/PathClassLoader;", "Ljava/lang/ClassLoader;");
    builder.UnimplementedConstructor(
        "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    builder.UnimplementedConstructor(
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
