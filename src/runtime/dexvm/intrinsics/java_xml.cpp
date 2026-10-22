// SAX construction and explicit unsupported parsing boundary. XML Pull is BootDex.
#include "catalog.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicHandler NoopVoid() {
    return [](IntrinsicContext&) { return VmValue::Void(); };
}

IntrinsicHandler SaxParseUnsupported() {
    return [](IntrinsicContext&) -> VmValue {
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "SAX parsing is not implemented"};
    };
}

IntrinsicHandler SaxSetContentHandler(
    const CoreIntrinsicServices& services) {
    return [set_handler = services.set_sax_content_handler](
               IntrinsicContext& call) {
        const auto handler = call.arguments[0].ref;
        if (!handler.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                              "SAX content handler is null"};
        }
        if (set_handler) set_handler(call.receiver, handler);
        return VmValue::Void();
    };
}

IntrinsicClassDecl DeclareSaxParser() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/xml/parsers/SAXParser;", "Ljava/lang/Object;");
    builder.VirtualMethod("getXMLReader", "()Lorg/xml/sax/XMLReader;", [](IntrinsicContext& call) {
        return VmValue::Ref(call.vm.NewIntrinsicInstance("Lorg/xml/sax/XMLReader$Impl;"));
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
    auto builder = IntrinsicClassBuilder::Class(
        "Ljavax/xml/parsers/SAXParserFactory;", "Ljava/lang/Object;");
    builder.StaticMethod(
        "newInstance", "()Ljavax/xml/parsers/SAXParserFactory;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/xml/parsers/SAXParserFactory;"));
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

IntrinsicClassDecl DeclareContentHandler() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Lorg/xml/sax/ContentHandler;"))
        .Build();
}

IntrinsicClassDecl DeclareInputSource() {
    auto builder = IntrinsicClassBuilder::Class(
        "Lorg/xml/sax/InputSource;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/io/InputStream;)V", NoopVoid());
    // AOSP 4.4 ROM line 72.
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
        kAccPublic);

    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareXmlReaderImpl(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Lorg/xml/sax/XMLReader$Impl;", "Ljava/lang/Object;",
        {"Lorg/xml/sax/XMLReader;"});
    builder.FinalMethod(
        "setContentHandler", "(Lorg/xml/sax/ContentHandler;)V",
        SaxSetContentHandler(services));
    builder.FinalMethod("parse", "(Lorg/xml/sax/InputSource;)V",
                        SaxParseUnsupported());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareXmlReader(const CoreIntrinsicServices&) {
    auto builder =
        IntrinsicClassBuilder::Interface("Lorg/xml/sax/XMLReader;");
    builder.UnimplementedVirtual("setContentHandler", "(Lorg/xml/sax/ContentHandler;)V", kAccPublic | kAccAbstract);
    builder.UnimplementedVirtual("parse", "(Lorg/xml/sax/InputSource;)V", kAccPublic | kAccAbstract);
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDefaultHandler() {
    auto builder = IntrinsicClassBuilder::Class(
        "Lorg/xml/sax/helpers/DefaultHandler;", "Ljava/lang/Object;");
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaXml(std::vector<IntrinsicClassDecl>& catalog,
                   const CoreIntrinsicServices& services) {
    catalog.push_back(DeclareSaxParser());
    catalog.push_back(DeclareSaxParserFactory());
    catalog.push_back(DeclareContentHandler());
    catalog.push_back(DeclareInputSource());
    catalog.push_back(DeclareXmlReaderImpl(services));
    catalog.push_back(DeclareXmlReader(services));
    catalog.push_back(DeclareDefaultHandler());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
