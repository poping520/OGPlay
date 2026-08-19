#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Lorg/xml/sax/XMLReader;");
    builder.FinalMethod("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", SaxSetContentHandlerHandler(context));
    builder.FinalMethod("parse", "(Lorg/xml/sax/InputSource;)V", SaxParseUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
