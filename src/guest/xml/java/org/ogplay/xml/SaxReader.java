package org.ogplay.xml;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.Reader;
import org.kxml2.io.KXmlParser;
import org.xml.sax.InputSource;
import org.xml.sax.SAXException;
import org.xml.sax.SAXNotSupportedException;
import org.xml.sax.SAXParseException;
import org.xml.sax.XMLReader;
import org.xml.sax.helpers.AttributesImpl;
import org.xml.sax.helpers.LocatorImpl;
import org.xml.sax.helpers.XMLFilterImpl;
import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserException;
import org.xmlpull.v1.XmlPullParserFactory;

public final class SaxReader extends XMLFilterImpl {
    private SaxReader() {}

    public static XMLReader create() {
        return new SaxReader();
    }

    @Override
    public void parse(InputSource input) throws IOException, SAXException {
        Reader characters = input.getCharacterStream();
        InputStream bytes = characters == null ? input.getByteStream() : null;
        if (characters == null && bytes == null) {
            throw new SAXNotSupportedException("SAX requires an explicit input stream");
        }
        Closeable stream = characters != null ? characters : bytes;
        LocatorImpl location = new LocatorImpl();
        location.setPublicId(input.getPublicId());
        location.setSystemId(input.getSystemId());
        try {
            XmlPullParserFactory factory = XmlPullParserFactory.newInstance();
            factory.setNamespaceAware(true);
            KXmlParser parser = (KXmlParser) factory.newPullParser();
            if (characters != null) {
                parser.setInput(characters);
            } else {
                parser.setInput(bytes, input.getEncoding());
            }
            setDocumentLocator(location);
            startDocument();
            readDocument(parser, location);
            endDocument();
        } catch (XmlPullParserException error) {
            if (error.getDetail() instanceof IOException) {
                throw (IOException) error.getDetail();
            }
            reportFatal(new SAXParseException(error.getMessage(), input.getPublicId(),
                    input.getSystemId(), error.getLineNumber(), error.getColumnNumber(), error));
        } finally {
            try {
                stream.close();
            } catch (IOException ignored) {
                // Android's SAX reader closes the selected input quietly.
            }
        }
    }

    private void readDocument(KXmlParser parser, LocatorImpl location)
            throws IOException, SAXException, XmlPullParserException {
        boolean sawRoot = false;
        int[] textRange = new int[2];
        for (int event = parser.next(); event != XmlPullParser.END_DOCUMENT; event = parser.next()) {
            location.setLineNumber(parser.getLineNumber());
            location.setColumnNumber(parser.getColumnNumber());
            if (parser.getRootElementName() != null) {
                throw new SAXNotSupportedException("SAX document type declarations are unavailable");
            }
            switch (event) {
                case XmlPullParser.START_TAG:
                    if (parser.getDepth() == 1) {
                        if (sawRoot) reportFatal(new SAXParseException("Multiple root elements", location));
                        sawRoot = true;
                    }
                    startTag(parser);
                    break;
                case XmlPullParser.END_TAG:
                    endTag(parser);
                    break;
                case XmlPullParser.TEXT:
                    char[] text = parser.getTextCharacters(textRange);
                    characters(text, textRange[0], textRange[1]);
                    break;
                default:
                    break;
            }
        }
        location.setLineNumber(parser.getLineNumber());
        location.setColumnNumber(parser.getColumnNumber());
        if (parser.getRootElementName() != null) {
            throw new SAXNotSupportedException("SAX document type declarations are unavailable");
        }
        if (!sawRoot || parser.getDepth() != 0) {
            reportFatal(new SAXParseException("Incomplete XML document", location));
        }
    }

    private void startTag(XmlPullParser parser) throws SAXException, XmlPullParserException {
        int depth = parser.getDepth();
        int inherited = parser.getNamespaceCount(depth - 1);
        for (int i = inherited; i < parser.getNamespaceCount(depth); i++) {
            String prefix = parser.getNamespacePrefix(i);
            startPrefixMapping(prefix == null ? "" : prefix, parser.getNamespaceUri(i));
        }
        AttributesImpl attributes = new AttributesImpl();
        for (int i = 0; i < parser.getAttributeCount(); i++) {
            String name = parser.getAttributeName(i);
            attributes.addAttribute(parser.getAttributeNamespace(i), name,
                    qualifiedName(parser.getAttributePrefix(i), name),
                    parser.getAttributeType(i), parser.getAttributeValue(i));
        }
        String name = parser.getName();
        startElement(parser.getNamespace(), name, qualifiedName(parser.getPrefix(), name), attributes);
    }

    private void endTag(XmlPullParser parser) throws SAXException, XmlPullParserException {
        String name = parser.getName();
        endElement(parser.getNamespace(), name, qualifiedName(parser.getPrefix(), name));
        int depth = parser.getDepth();
        int inherited = parser.getNamespaceCount(depth - 1);
        for (int i = parser.getNamespaceCount(depth) - 1; i >= inherited; i--) {
            String prefix = parser.getNamespacePrefix(i);
            endPrefixMapping(prefix == null ? "" : prefix);
        }
    }

    private void reportFatal(SAXParseException error) throws SAXException {
        fatalError(error);
        throw error;
    }

    private static String qualifiedName(String prefix, String localName) {
        return prefix == null ? localName : prefix + ":" + localName;
    }
}
