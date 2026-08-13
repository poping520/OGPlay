#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_org_xml_sax_helpers_DefaultHandler(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Lorg/xml/sax/helpers/DefaultHandler;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
