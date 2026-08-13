#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_ObjectInputStream(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/io/ObjectInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
