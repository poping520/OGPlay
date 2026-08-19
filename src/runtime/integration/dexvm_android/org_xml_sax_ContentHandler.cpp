#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_ContentHandler(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Lorg/xml/sax/ContentHandler;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
