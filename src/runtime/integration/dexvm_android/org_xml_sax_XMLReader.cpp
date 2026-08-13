#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader(const Context& context) {
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/XMLReader;");
    builder.MarkInterface();
    builder.Virtual("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", SaxSetContentHandlerHandler(context));
    builder.Virtual("parse", "(Lorg/xml/sax/InputSource;)V", SaxParseUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
