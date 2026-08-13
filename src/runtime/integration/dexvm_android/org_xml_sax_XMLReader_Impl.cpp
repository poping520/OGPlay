#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader_Impl(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/XMLReader$Impl;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Lorg/xml/sax/XMLReader;");
    builder.Virtual("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", handlers.handler_android_sax_set_content_handler);
    builder.Virtual("parse", "(Lorg/xml/sax/InputSource;)V", handlers.handler_android_sax_parse_unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
