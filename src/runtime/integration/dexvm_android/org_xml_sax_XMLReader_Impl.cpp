#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader_Impl(const Context& context) {
    return DeclareAndroidClass(context, "Lorg/xml/sax/XMLReader$Impl;");
}

}  // namespace ogplay::runtime::android_intrinsics
