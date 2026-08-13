#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_ContentHandler(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/ContentHandler;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
