#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParserFactory(const Context& context) {
    return DeclareAndroidClass(context, "Ljavax/xml/parsers/SAXParserFactory;");
}

}  // namespace ogplay::runtime::android_intrinsics
