#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParserFactory(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Ljavax/xml/parsers/SAXParserFactory;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("newInstance", "()Ljavax/xml/parsers/SAXParserFactory;", handlers.handler_android_sax_factory_instance);
    builder.Virtual("newSAXParser", "()Ljavax/xml/parsers/SAXParser;", handlers.handler_android_sax_new_parser);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
