package org.ogplay.security;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import javax.net.ssl.SSLSocketFactory;

/** Client SSLSocketFactory bound to an initialized OGPlay SSLContextSpi. */
public final class OgPlaySslSocketFactory extends SSLSocketFactory {
    private final OgPlaySslContextSpi context;

    OgPlaySslSocketFactory(OgPlaySslContextSpi context) {
        this.context = context;
    }

    public Socket createSocket() throws IOException {
        return new OgPlaySslSocket(context);
    }

    public Socket createSocket(String host, int port) throws IOException {
        OgPlaySslSocket socket = new OgPlaySslSocket(context, host);
        socket.connectTo(host, port);
        return socket;
    }

    public Socket createSocket(InetAddress host, int port) throws IOException {
        String name = host == null ? null : host.getHostName();
        OgPlaySslSocket socket = new OgPlaySslSocket(context, name);
        socket.connectTo(host, port);
        return socket;
    }

    public Socket createSocket(String host, int port, InetAddress localHost, int localPort)
            throws IOException {
        if (localHost != null || localPort != 0) {
            throw new IOException("binding the local address of an SSL socket is not supported");
        }
        return createSocket(host, port);
    }

    public Socket createSocket(InetAddress address, int port, InetAddress localAddress,
                               int localPort) throws IOException {
        if (localAddress != null || localPort != 0) {
            throw new IOException("binding the local address of an SSL socket is not supported");
        }
        return createSocket(address, port);
    }

    public Socket createSocket(Socket s, String host, int port, boolean autoClose)
            throws IOException {
        if (s == null) {
            throw new NullPointerException("socket == null");
        }
        if (!s.isConnected()) {
            throw new IOException("underlying socket is not connected");
        }
        OgPlaySslSocket socket = new OgPlaySslSocket(context, s, host, port, autoClose);
        socket.startHandshake();
        return socket;
    }

    public String[] getDefaultCipherSuites() {
        return NativeTls.defaultCipherSuites();
    }

    public String[] getSupportedCipherSuites() {
        return NativeTls.supportedCipherSuites();
    }
}
