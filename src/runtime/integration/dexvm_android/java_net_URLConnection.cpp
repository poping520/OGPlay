#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLConnection(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/net/URLConnection;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
