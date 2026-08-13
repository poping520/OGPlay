#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader(const Context& context) {
    return DeclareAndroidClass(context, "Lorg/xml/sax/XMLReader;");
}

}  // namespace ogplay::runtime::android_intrinsics
