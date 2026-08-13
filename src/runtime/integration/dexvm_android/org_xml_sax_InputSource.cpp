#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_InputSource(const Context& context) {
    return DeclareAndroidClass(context, "Lorg/xml/sax/InputSource;");
}

}  // namespace ogplay::runtime::android_intrinsics
