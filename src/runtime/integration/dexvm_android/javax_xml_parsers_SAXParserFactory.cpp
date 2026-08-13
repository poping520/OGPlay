#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParserFactory(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Ljavax/xml/parsers/SAXParserFactory;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("newInstance", "()Ljavax/xml/parsers/SAXParserFactory;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/xml/parsers/SAXParserFactory;"));
        });
    builder.Virtual("newSAXParser", "()Ljavax/xml/parsers/SAXParser;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParser;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
