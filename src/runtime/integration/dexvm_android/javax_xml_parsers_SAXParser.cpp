#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParser(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/xml/parsers/SAXParser;");
}

}  // namespace ogplay::runtime::android_intrinsics
