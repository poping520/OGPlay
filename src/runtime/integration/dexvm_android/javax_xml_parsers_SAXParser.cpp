#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParser(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljavax/xml/parsers/SAXParser;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getXMLReader", "()Lorg/xml/sax/XMLReader;", handlers.handler_android_sax_get_reader);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
