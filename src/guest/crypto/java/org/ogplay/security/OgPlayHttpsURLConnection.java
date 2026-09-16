package org.ogplay.security;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ProtocolException;
import java.net.Socket;
import java.net.URL;
import java.security.cert.Certificate;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.net.ssl.HostnameVerifier;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/** Bounded HTTP/1.1 HTTPS connection over OgPlay SSLSocket. */
public final class OgPlayHttpsURLConnection extends HttpsURLConnection {
    private SSLSocket socket;
    private InputStream socketIn;
    private OutputStream socketOut;
    private InputStream responseStream;
    private ByteArrayOutputStream requestBody;
    private int statusCode = -1;
    private String statusMessage = "";
    private final ArrayList headerKeys = new ArrayList();
    private final ArrayList headerValues = new ArrayList();
    private boolean requestSent;

    public OgPlayHttpsURLConnection(URL url) {
        super(url);
    }

    public void connect() throws IOException {
        if (connected) {
            return;
        }
        SSLSocketFactory factory = getSSLSocketFactory();
        try {
            SSLContext context = SSLContext.getDefault();
            if (context != null && context.getSocketFactory() != null) {
                factory = context.getSocketFactory();
            }
        } catch (Exception ignored) {
        }
        if (factory == null) {
            factory = getDefaultSSLSocketFactory();
        }
        if (factory == null) {
            throw new IOException("no SSLSocketFactory is available");
        }
        String host = url.getHost();
        int port = url.getPort();
        if (port < 0) {
            port = url.getDefaultPort();
        }
        Socket created;
        try {
            created = factory.createSocket();
        } catch (IOException e) {
            throw new IOException(factory.getClass().getName() + " createSocket: " + e.getMessage());
        }
        if (!(created instanceof SSLSocket)) {
            created.close();
            throw new IOException(factory.getClass().getName() + " did not return an SSLSocket");
        }
        socket = (SSLSocket) created;
        try {
            if (!socket.isConnected()) {
                int connectMs = getConnectTimeout();
                if (connectMs > 0) {
                    socket.connect(new InetSocketAddress(host, port), connectMs);
                } else {
                    socket.connect(new InetSocketAddress(host, port));
                }
            }
            int readMs = getReadTimeout();
            if (readMs > 0) {
                socket.setSoTimeout(readMs);
            }
            socket.startHandshake();
        } catch (IOException e) {
            throw new IOException(factory.getClass().getName() + " tls connected="
                    + socket.isConnected() + " " + e.getMessage());
        }
        HostnameVerifier verifier = getHostnameVerifier();
        if (verifier != null && !verifier.verify(host, socket.getSession())) {
            socket.close();
            throw new IOException("hostname verification failed for " + host);
        }
        socketIn = socket.getInputStream();
        socketOut = socket.getOutputStream();
        connected = true;
    }

    public void disconnect() {
        connected = false;
        try {
            if (socket != null) {
                socket.close();
            }
        } catch (IOException ignored) {
        }
    }

    public boolean usingProxy() {
        return false;
    }

    public OutputStream getOutputStream() throws IOException {
        if (!doOutput) {
            throw new ProtocolException("cannot write to a GET request");
        }
        if (requestSent) {
            throw new ProtocolException("request has already been sent");
        }
        if (requestBody == null) {
            requestBody = new ByteArrayOutputStream();
        }
        return requestBody;
    }

    public InputStream getInputStream() throws IOException {
        sendRequest();
        if (statusCode >= 400) {
            throw new IOException("HTTP " + statusCode + " " + statusMessage);
        }
        return responseStream;
    }

    public int getResponseCode() throws IOException {
        sendRequest();
        return statusCode;
    }

    public String getResponseMessage() throws IOException {
        sendRequest();
        return statusMessage;
    }

    public String getHeaderField(String name) {
        try {
            sendRequest();
        } catch (IOException e) {
            return null;
        }
        if (name == null) {
            return null;
        }
        for (int i = headerKeys.size() - 1; i >= 0; i--) {
            if (name.equalsIgnoreCase((String) headerKeys.get(i))) {
                return (String) headerValues.get(i);
            }
        }
        return null;
    }

    public String getHeaderFieldKey(int position) {
        try {
            sendRequest();
        } catch (IOException e) {
            return null;
        }
        if (position < 0 || position >= headerKeys.size()) {
            return null;
        }
        return (String) headerKeys.get(position);
    }

    public String getHeaderField(int position) {
        try {
            sendRequest();
        } catch (IOException e) {
            return null;
        }
        if (position < 0 || position >= headerValues.size()) {
            return null;
        }
        return (String) headerValues.get(position);
    }

    public String getCipherSuite() {
        if (socket == null) {
            return null;
        }
        return socket.getSession().getCipherSuite();
    }

    public Certificate[] getLocalCertificates() {
        if (socket == null) {
            return null;
        }
        return socket.getSession().getLocalCertificates();
    }

    public Certificate[] getServerCertificates() throws javax.net.ssl.SSLPeerUnverifiedException {
        if (socket == null) {
            throw new javax.net.ssl.SSLPeerUnverifiedException("not connected");
        }
        return socket.getSession().getPeerCertificates();
    }

    private void sendRequest() throws IOException {
        if (requestSent) {
            return;
        }
        connect();
        String path = url.getFile();
        if (path == null || path.length() == 0) {
            path = "/";
        }
        String method = getRequestMethod();
        byte[] body = requestBody == null ? new byte[0] : requestBody.toByteArray();
        StringBuffer header = new StringBuffer();
        header.append(method).append(' ').append(path).append(" HTTP/1.1\r\n");
        header.append("Host: ").append(url.getHost()).append("\r\n");
        header.append("Connection: close\r\n");
        Map properties = getRequestProperties();
        Object[] keys = properties.keySet().toArray();
        for (int i = 0; i < keys.length; i++) {
            String key = (String) keys[i];
            List values = (List) properties.get(key);
            for (int j = 0; j < values.size(); j++) {
                header.append(key).append(": ").append(values.get(j)).append("\r\n");
            }
        }
        if (doOutput) {
            header.append("Content-Length: ").append(body.length).append("\r\n");
        }
        header.append("\r\n");
        try {
            socketOut.write(header.toString().getBytes("ISO-8859-1"));
            if (body.length > 0) {
                socketOut.write(body);
            }
            socketOut.flush();
            parseResponse();
        } catch (IOException e) {
            throw new IOException("https-write");
        }
        requestSent = true;
    }

    private void parseResponse() throws IOException {
        String status = readLine(socketIn);
        if (status == null || !status.startsWith("HTTP/1.")) {
            throw new IOException("invalid HTTP status line");
        }
        int first = status.indexOf(' ');
        int second = first < 0 ? -1 : status.indexOf(' ', first + 1);
        if (first < 0) {
            throw new IOException("invalid HTTP status line");
        }
        String codeText = second < 0 ? status.substring(first + 1) : status.substring(first + 1, second);
        statusCode = Integer.parseInt(codeText.trim());
        statusMessage = second < 0 ? "" : status.substring(second + 1);
        while (true) {
            String line = readLine(socketIn);
            if (line == null || line.length() == 0) {
                break;
            }
            int colon = line.indexOf(':');
            if (colon <= 0) {
                continue;
            }
            headerKeys.add(line.substring(0, colon).trim());
            headerValues.add(line.substring(colon + 1).trim());
        }
        responseStream = socketIn;
    }

    private static String readLine(InputStream in) throws IOException {
        StringBuffer line = new StringBuffer();
        while (true) {
            int b = in.read();
            if (b < 0) {
                return line.length() == 0 ? null : line.toString();
            }
            if (b == '\n') {
                int last = line.length() - 1;
                if (last >= 0 && line.charAt(last) == '\r') {
                    line.setLength(last);
                }
                return line.toString();
            }
            line.append((char) b);
        }
    }
}
