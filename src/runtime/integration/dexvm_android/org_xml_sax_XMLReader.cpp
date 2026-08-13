#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_XMLReader(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/XMLReader;");
    builder.MarkInterface();
    builder.Virtual("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", handlers.handler_android_sax_set_content_handler);
    builder.Virtual("parse", "(Lorg/xml/sax/InputSource;)V", handlers.handler_android_sax_parse_unsupported);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
