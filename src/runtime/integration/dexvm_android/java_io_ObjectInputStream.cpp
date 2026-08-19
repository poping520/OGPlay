#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ObjectInputStream(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/ObjectInputStream;", "Ljava/io/InputStream;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
