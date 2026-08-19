#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_javax_xml_parsers_SAXParserFactory(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljavax/xml/parsers/SAXParserFactory;", "Ljava/lang/Object;");
    builder.StaticMethod("newInstance", "()Ljavax/xml/parsers/SAXParserFactory;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/xml/parsers/SAXParserFactory;"));
        });
    builder.FinalMethod("newSAXParser", "()Ljavax/xml/parsers/SAXParser;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParser;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
