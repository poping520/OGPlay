#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_Writer(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/Writer;");
    builder.Super("Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
