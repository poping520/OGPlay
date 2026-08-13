#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLConnection(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljava/net/URLConnection;");
    builder.Super("Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
