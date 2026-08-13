#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParser(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/xml/parsers/SAXParser;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("getXMLReader", "()Lorg/xml/sax/XMLReader;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Lorg/xml/sax/XMLReader$Impl;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
