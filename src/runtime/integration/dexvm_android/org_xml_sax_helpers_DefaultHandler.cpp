#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_helpers_DefaultHandler(const Context& context) {
    return DeclareAndroidClass(context, "Lorg/xml/sax/helpers/DefaultHandler;");
}

}  // namespace ogplay::runtime::android_intrinsics
