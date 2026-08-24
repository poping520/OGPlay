// DVM-80: reserved API-family translation unit; no declarations yet.
#include "catalog.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicClassDecl Declare_java_nio_charset_Charset() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/nio/charset/Charset;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "forName", "(Ljava/lang/String;)Ljava/nio/charset/Charset;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljava/nio/charset/Charset;"));
        });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaNio(std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(Declare_java_nio_charset_Charset());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
