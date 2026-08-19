#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_InputSource(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/xml/sax/InputSource;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/io/InputStream;)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
