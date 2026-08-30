// DVM-80: reserved API-family translation unit; no declarations yet.
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
    builder.FinalMethod(
        "getXMLReader", "()Lorg/xml/sax/XMLReader;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Lorg/xml/sax/XMLReader$Impl;"));
        });
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
    builder.FinalMethod(
        "newSAXParser", "()Ljavax/xml/parsers/SAXParser;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewIntrinsicInstance(
                "Ljavax/xml/parsers/SAXParser;"));
        });
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

IntrinsicClassDecl DeclareXmlReader(
    const CoreIntrinsicServices& services) {
    auto builder =
        IntrinsicClassBuilder::Interface("Lorg/xml/sax/XMLReader;");
    builder.FinalMethod(
        "setContentHandler", "(Lorg/xml/sax/ContentHandler;)V",
        SaxSetContentHandler(services));
    builder.FinalMethod("parse", "(Lorg/xml/sax/InputSource;)V",
                        SaxParseUnsupported());
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDefaultHandler() {
    auto builder = IntrinsicClassBuilder::Class(
        "Lorg/xml/sax/helpers/DefaultHandler;", "Ljava/lang/Object;");
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclareXmlPullParser() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Lorg/xmlpull/v1/XmlPullParser;");
    constexpr std::uint32_t kPublicAbstract = 0x0401U;
    builder.UnimplementedVirtual("getEventType", "()I", kPublicAbstract);
    builder.UnimplementedVirtual("next", "()I", kPublicAbstract);
    builder.UnimplementedVirtual("getName", "()Ljava/lang/String;",
                                 kPublicAbstract);
    builder.UnimplementedVirtual("getText", "()Ljava/lang/String;",
                                 kPublicAbstract);
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
    catalog.push_back(DeclareXmlPullParser());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
