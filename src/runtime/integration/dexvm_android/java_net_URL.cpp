#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URL(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/net/URL;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V", NetUnsupportedHandler());
    builder.FinalMethod("openConnection", "()Ljava/net/URLConnection;", NetUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
