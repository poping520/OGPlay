#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_Writer(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/Writer;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
