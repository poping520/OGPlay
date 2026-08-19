#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Lorg/xml/sax/XMLReader$Impl;", "Ljava/lang/Object;", {"Lorg/xml/sax/XMLReader;"});
    builder.FinalMethod("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", SaxSetContentHandlerHandler(context));
    builder.FinalMethod("parse", "(Lorg/xml/sax/InputSource;)V", SaxParseUnsupportedHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
