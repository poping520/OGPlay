#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_ContentHandler(const Context& context) {
    return DeclareAndroidClass(context, "Lorg/xml/sax/ContentHandler;");
}

}  // namespace ogplay::runtime::android_intrinsics
