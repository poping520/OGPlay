#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_InputSource(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/InputSource;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", handlers.handler_android_graphics_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
