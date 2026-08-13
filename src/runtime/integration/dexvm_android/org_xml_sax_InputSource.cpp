#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_InputSource(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/InputSource;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
