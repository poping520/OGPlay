// SAX factory/parser construction. Readers and XML Pull execute in BootDex.
#include "catalog.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicClassDecl DeclareSaxParser() {
    auto builder = IntrinsicClassBuilder::Class("Ljavax/xml/parsers/SAXParser;", "Ljava/lang/Object;");
    const auto reader_field = builder.BoundInstanceField("ogplayReader", "Lorg/xml/sax/XMLReader;", kAccPrivate);
    builder.VirtualMethod("getXMLReader", "()Lorg/xml/sax/XMLReader;", [reader_field](IntrinsicContext& call) {
        IntrinsicCall fields(call);
        if (const auto reader = fields.GetRef(reader_field); reader.IsValid()) return VmValue::Ref(reader);
        const auto adapter = call.vm.Linker().ResolveDescriptor("Lorg/ogplay/xml/SaxReader;");
        const auto create = call.vm.Linker().FindDirectMethod(adapter, "create", "()Lorg/xml/sax/XMLReader;");
        const auto result = call.vm.Call(*create, {});
        if (result.exception.IsValid())
            throw VmJavaThrow{call.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                              result.exception};
        fields.SetRef(reader_field, result.value.ref);
        return result.value;
    });
    // AOSP 4.4 ROM line 80.
    builder.Constructor(
        "()V",
        [](IntrinsicContext& c) {
            const auto target =
                c.vm.Linker().FindDirectMethod(c.vm.Linker().ResolveDescriptor("Ljava/lang/Object;"), "<init>", "()V");
            const auto result = c.vm.Call(*target, std::vector<VmValue>{VmValue::Ref(c.receiver)});
            if (result.exception.IsValid())
                throw VmJavaThrow{c.vm.Linker().Class(result.exception_class).descriptor, result.exception_message,
                                  result.exception};
            return result.value;
        },
        kAccProtected);

    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSaxParserFactory() {
    auto builder = IntrinsicClassBuilder::Class("Ljavax/xml/parsers/SAXParserFactory;", "Ljava/lang/Object;");
    builder.StaticMethod("newInstance", "()Ljavax/xml/parsers/SAXParserFactory;", [](IntrinsicContext& call) {
        return VmValue::Ref(call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParserFactory;"));
    });
    builder.VirtualMethod("newSAXParser", "()Ljavax/xml/parsers/SAXParser;", [](IntrinsicContext& call) {
        return VmValue::Ref(call.vm.NewIntrinsicInstance("Ljavax/xml/parsers/SAXParser;"));
    });
    const auto wu_validating = builder.BoundInstanceField("validating", "Z", kAccPrivate);
    const auto wu_namespaceAware = builder.BoundInstanceField("namespaceAware", "Z", kAccPrivate);
    // AOSP 4.4 ROM line 50.
    builder.Constructor(
        "()V",
        [wu_validating, wu_namespaceAware](IntrinsicContext& c) {
            IntrinsicCall api(c);
            api.SetInt(wu_validating, 0);
            api.SetInt(wu_namespaceAware, 0);
            return VmValue::Void();
        },
        kAccProtected);

    return std::move(builder).Build();
}

}  // namespace

void AppendJavaXml(std::vector<IntrinsicClassDecl>& catalog) {
    catalog.push_back(DeclareSaxParser());
    catalog.push_back(DeclareSaxParserFactory());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
