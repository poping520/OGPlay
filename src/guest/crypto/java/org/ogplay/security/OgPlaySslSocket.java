package org.ogplay.security;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.security.PrivateKey;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import javax.net.ssl.HandshakeCompletedEvent;
import javax.net.ssl.HandshakeCompletedListener;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.X509KeyManager;
import javax.net.ssl.X509TrustManager;

/** Client SSLSocket. Ciphertext uses the wrapped or inherited Socket streams. */
public final class OgPlaySslSocket extends SSLSocket {
    private static final int NEW = 0;
    private static final int HANDSHAKING = 1;
    private static final int OPEN = 2;
    private static final int CLOSING = 3;
    private static final int CLOSED = 4;
    private static final int FAILED = 5;

    private final OgPlaySslContextSpi context;
    private final Socket raw;
    private final boolean autoClose;
    private String peerHost;
    private int peerPort;
    private final boolean layered;
    private final ArrayList listeners = new ArrayList();
    private String[] enabledProtocols;
    private String[] enabledCiphers;
    private boolean clientMode = true;
    private boolean needClientAuth;
    private boolean wantClientAuth;
    private boolean enableSessionCreation = true;
    private int state = NEW;
    private long ssl;
    private OgPlaySslSession session;
    private InputStream appIn;
    private OutputStream appOut;
    private InputStream rawIn;
    private OutputStream rawOut;

    OgPlaySslSocket(OgPlaySslContextSpi context) {
        this(context, null, 0, null, true, false);
    }

    OgPlaySslSocket(OgPlaySslContextSpi context, String host) {
        this(context, host, 0, null, true, false);
    }

    OgPlaySslSocket(OgPlaySslContextSpi context, Socket existing, String host, int port,
                    boolean autoClose) {
        this(context, host, port, existing, autoClose, true);
    }

    private OgPlaySslSocket(OgPlaySslContextSpi context, String host, int port, Socket existing,
                            boolean autoClose, boolean layered) {
        this.context = context;
        this.layered = layered;
        this.autoClose = autoClose;
        this.raw = existing;
        this.peerHost = host;
        this.peerPort = port;
        this.enabledProtocols = context.protocols();
        this.enabledCiphers = NativeTls.defaultCipherSuites();
        NativeTls.markTls(this);
        if (layered) {
            NativeTls.requireTls(host);
        }
    }

    void connectTo(String host, int port) throws IOException {
        NativeTls.markTls(this);
        connect(new InetSocketAddress(host, port));
    }

    void connectTo(InetAddress address, int port) throws IOException {
        NativeTls.markTls(this);
        connect(new InetSocketAddress(address, port));
    }

    public void connect(SocketAddress endpoint) throws IOException {
        connect(endpoint, 0);
    }

    public void connect(SocketAddress endpoint, int timeout) throws IOException {
        NativeTls.markTls(this);
        if (endpoint instanceof InetSocketAddress) {
            InetSocketAddress inet = (InetSocketAddress) endpoint;
            String host = inet.getHostName();
            if (host != null && host.length() > 0) {
                peerHost = host;
            }
            peerPort = inet.getPort();
        }
        NativeTls.connectRaw(this, endpoint, timeout);
    }

    public void startHandshake() throws IOException {
        if (!clientMode) {
            throw new SocketException("server TLS is not supported");
        }
        synchronized (this) {
            if (state == OPEN) {
                return;
            }
            if (state == CLOSED || state == FAILED || state == CLOSING) {
                throw new SocketException("socket is closed");
            }
            state = HANDSHAKING;
        }
        try {
            ensureRawStreams();
            int savedTimeout = getSoTimeout();
            if (savedTimeout <= 0) {
                setSoTimeout(15000);
            }
            if (!enableSessionCreation) {
                setSoTimeout(savedTimeout);
                throw new javax.net.ssl.SSLException("SSL session creation is disabled");
            }
            if (ssl == 0) {
                ssl = NativeTls.createSsl(context.nativeContextToken(), peerHost);
                NativeTls.configure(ssl, enabledProtocols, enabledCiphers);
            }
            installClientKey();
            long deadline = System.currentTimeMillis() + TrustLimits.MAX_HANDSHAKE_MILLIS;
            while (true) {
                if (System.currentTimeMillis() > deadline) {
                    setSoTimeout(savedTimeout);
                    throw new SocketException("TLS handshake timed out");
                }
                int status = NativeTls.handshake(ssl, context.trustManager(), false);
                pumpWire();
                if (status == NativeTls.OK) {
                    setSoTimeout(savedTimeout);
                    break;
                }
                if (status == NativeTls.WANT_WRITE) {
                    pumpWire();
                    continue;
                }
                if (status == NativeTls.WANT_READ) {
                    if (!readWire()) {
                        setSoTimeout(savedTimeout);
                        throw new SocketException("TLS handshake truncated");
                    }
                    continue;
                }
                setSoTimeout(savedTimeout);
                if (status == NativeTls.CLOSED) {
                    throw new javax.net.ssl.SSLHandshakeException("TLS handshake closed");
                }
                throw new javax.net.ssl.SSLHandshakeException("TLS handshake failed");
            }
            Certificate[] peer = certificates(NativeTls.peerCertificates(ssl));
            session = new OgPlaySslSession(NativeTls.protocol(ssl), NativeTls.cipherSuite(ssl),
                    peerHost, peerPort != 0 ? peerPort : getPort(), peer, context.sessions(),
                    NativeTls.sessionId(ssl));
            context.sessions().put(session);
            synchronized (this) {
                state = OPEN;
                appIn = new AppInputStream();
                appOut = new AppOutputStream();
            }
            fireHandshakeCompleted();
        } catch (IOException e) {
            fail(e);
            throw e;
        } catch (RuntimeException e) {
            fail(e);
            throw e;
        }
    }

    public InputStream getInputStream() throws IOException {
        startHandshake();
        return appIn;
    }

    public OutputStream getOutputStream() throws IOException {
        startHandshake();
        return appOut;
    }

    public synchronized void close() throws IOException {
        if (state == CLOSED) {
            return;
        }
        state = CLOSING;
        try {
            if (ssl != 0 && session != null) {
                NativeTls.shutdown(ssl);
                pumpWire();
            }
        } catch (IOException ignored) {
        } finally {
            freeSsl();
            state = CLOSED;
            if (layered) {
                if (autoClose && raw != null) {
                    raw.close();
                }
            } else {
                super.close();
            }
        }
    }

    public String[] getSupportedCipherSuites() {
        return NativeTls.supportedCipherSuites();
    }

    public String[] getEnabledCipherSuites() {
        return OgPlaySslContextSpi.copy(enabledCiphers);
    }

    public void setEnabledCipherSuites(String[] suites) {
        enabledCiphers = requireSubset(suites, getSupportedCipherSuites(), "cipher suite");
    }

    public String[] getSupportedProtocols() {
        return new String[] {"TLSv1.2", "TLSv1.1", "TLSv1"};
    }

    public String[] getEnabledProtocols() {
        return OgPlaySslContextSpi.copy(enabledProtocols);
    }

    public void setEnabledProtocols(String[] protocols) {
        enabledProtocols = requireSubset(protocols, getSupportedProtocols(), "protocol");
    }

    public SSLSession getSession() {
        try {
            startHandshake();
        } catch (IOException e) {
            return new FailedSession();
        }
        return session;
    }

    public void addHandshakeCompletedListener(HandshakeCompletedListener listener) {
        if (listener == null) {
            throw new IllegalArgumentException("listener == null");
        }
        listeners.add(listener);
    }

    public void removeHandshakeCompletedListener(HandshakeCompletedListener listener) {
        if (listener == null) {
            throw new IllegalArgumentException("listener == null");
        }
        if (!listeners.remove(listener)) {
            throw new IllegalArgumentException("listener is not registered");
        }
    }

    public void setUseClientMode(boolean mode) {
        if (state != NEW && mode != clientMode) {
            throw new IllegalArgumentException("client mode cannot change after handshake");
        }
        if (!mode) {
            throw new IllegalArgumentException("server TLS is not supported");
        }
        clientMode = mode;
    }

    public boolean getUseClientMode() {
        return clientMode;
    }

    public void setNeedClientAuth(boolean need) {
        needClientAuth = need;
        if (need) {
            wantClientAuth = false;
        }
    }

    public boolean getNeedClientAuth() {
        return needClientAuth;
    }

    public void setWantClientAuth(boolean want) {
        wantClientAuth = want;
        if (want) {
            needClientAuth = false;
        }
    }

    public boolean getWantClientAuth() {
        return wantClientAuth;
    }

    public void setEnableSessionCreation(boolean flag) {
        enableSessionCreation = flag;
    }

    public boolean getEnableSessionCreation() {
        return enableSessionCreation;
    }

    public SSLParameters getSSLParameters() {
        SSLParameters parameters = new SSLParameters();
        parameters.setCipherSuites(getEnabledCipherSuites());
        parameters.setProtocols(getEnabledProtocols());
        if (needClientAuth) {
            parameters.setNeedClientAuth(true);
        } else if (wantClientAuth) {
            parameters.setWantClientAuth(true);
        }
        return parameters;
    }

    public void setSSLParameters(SSLParameters parameters) {
        if (parameters == null) {
            throw new NullPointerException("parameters == null");
        }
        String[] ciphers = parameters.getCipherSuites();
        if (ciphers != null) {
            setEnabledCipherSuites(ciphers);
        }
        String[] protocols = parameters.getProtocols();
        if (protocols != null) {
            setEnabledProtocols(protocols);
        }
        // API 19 SSLParameters has no endpointIdentificationAlgorithm.
        // HTTPS hostname checking stays on HostnameVerifier.
        if (parameters.getNeedClientAuth()) {
            setNeedClientAuth(true);
        } else if (parameters.getWantClientAuth()) {
            setWantClientAuth(true);
        } else {
            setNeedClientAuth(false);
            setWantClientAuth(false);
        }
    }

    private void ensureRawStreams() throws IOException {
        if (rawIn != null) {
            return;
        }
        if (layered) {
            rawIn = raw.getInputStream();
            rawOut = raw.getOutputStream();
        } else {
            if (!isConnected()) {
                throw new SocketException("socket is not connected");
            }
            rawIn = NativeTls.socketInput(this);
            rawOut = NativeTls.socketOutput(this);
        }
    }

    private void pumpWire() throws IOException {
        byte[] pending = NativeTls.pullWire(ssl);
        if (pending != null && pending.length > 0) {
            rawOut.write(pending);
            rawOut.flush();
        }
    }

    private boolean readWire() throws IOException {
        byte[] buffer = new byte[Math.min(TrustLimits.MAX_WIRE_BUFFER, 4096)];
        int n = rawIn.read(buffer);
        if (n < 0) {
            return false;
        }
        NativeTls.pushWire(ssl, buffer, 0, n);
        return true;
    }

    private void installClientKey() throws IOException {
        X509KeyManager manager = context.keyManager();
        if (manager == null) {
            return;
        }
        String alias = manager.chooseClientAlias(new String[] {"RSA", "EC"}, null, this);
        if (alias == null) {
            return;
        }
        PrivateKey key = manager.getPrivateKey(alias);
        X509Certificate[] chain = manager.getCertificateChain(alias);
        if (key == null || key.getEncoded() == null) {
            throw new javax.net.ssl.SSLHandshakeException("client private key is not exportable");
        }
        if (chain == null || chain.length == 0) {
            throw new javax.net.ssl.SSLHandshakeException("client certificate chain is missing");
        }
        try {
            NativeTls.setClientKey(ssl, key.getEncoded(), packCertificates(chain));
        } catch (CertificateException e) {
            throw new javax.net.ssl.SSLHandshakeException(e.toString());
        }
    }

    private void fireHandshakeCompleted() {
        if (listeners.isEmpty() || session == null) {
            return;
        }
        HandshakeCompletedEvent event = new HandshakeCompletedEvent(this, session);
        for (int i = 0; i < listeners.size(); i++) {
            ((HandshakeCompletedListener) listeners.get(i)).handshakeCompleted(event);
        }
    }

    private void fail(Exception error) {
        state = FAILED;
        freeSsl();
        try {
            if (layered) {
                if (autoClose && raw != null) {
                    raw.close();
                }
            } else {
                super.close();
            }
        } catch (IOException ignored) {
        }
    }

    private void freeSsl() {
        if (ssl != 0) {
            NativeTls.freeSsl(ssl);
            ssl = 0;
        }
    }

    private static Certificate[] certificates(byte[] packed) throws IOException {
        if (packed == null || packed.length < 8) {
            throw new javax.net.ssl.SSLHandshakeException("peer certificates are missing");
        }
        try {
            java.security.cert.CertificateFactory factory =
                    java.security.cert.CertificateFactory.getInstance("X.509");
            int count = NativeTrust.readInt(packed, 0);
            if (count < 1 || count > TrustLimits.MAX_CHAIN_CERTS) {
                throw new javax.net.ssl.SSLHandshakeException("peer certificates are missing");
            }
            Certificate[] certs = new Certificate[count];
            int offset = 4;
            for (int i = 0; i < count; i++) {
                if (offset > packed.length - 4) {
                    throw new javax.net.ssl.SSLHandshakeException("peer certificate encodings do not match");
                }
                int n = NativeTrust.readInt(packed, offset);
                offset += 4;
                if (n < 1 || n > TrustLimits.MAX_CERT_DER || n > packed.length - offset) {
                    throw new javax.net.ssl.SSLHandshakeException("peer certificate encoding exceeds limit");
                }
                byte[] der = new byte[n];
                System.arraycopy(packed, offset, der, 0, n);
                offset += n;
                certs[i] = factory.generateCertificate(new java.io.ByteArrayInputStream(der));
            }
            return certs;
        } catch (javax.net.ssl.SSLHandshakeException e) {
            throw e;
        } catch (CertificateException e) {
            throw new javax.net.ssl.SSLHandshakeException(e.toString());
        }
    }

    private static byte[] packCertificates(X509Certificate[] chain) throws CertificateException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        NativeTrust.writeInt(out, chain.length);
        for (int i = 0; i < chain.length; i++) {
            byte[] raw = NativeTrust.encodedCopy(chain[i]);
            if (raw == null || raw.length == 0) {
                throw new CertificateException("client certificate encoding is missing");
            }
            byte[] copy = new byte[raw.length];
            System.arraycopy(raw, 0, copy, 0, raw.length);
            NativeTrust.writeInt(out, copy.length);
            out.write(copy, 0, copy.length);
        }
        return out.toByteArray();
    }

    private static String[] requireSubset(String[] requested, String[] allowed, String label) {
        if (requested == null || requested.length == 0) {
            throw new IllegalArgumentException(label + " array is empty");
        }
        String[] copy = new String[requested.length];
        for (int i = 0; i < requested.length; i++) {
            boolean found = false;
            for (int j = 0; j < allowed.length; j++) {
                if (allowed[j].equals(requested[i])) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw new IllegalArgumentException("unsupported " + label + ": " + requested[i]);
            }
            copy[i] = requested[i];
        }
        return copy;
    }

    private final class AppInputStream extends InputStream {
        public int read() throws IOException {
            byte[] one = new byte[1];
            int n = read(one, 0, 1);
            return n < 0 ? -1 : (one[0] & 0xff);
        }

        public int read(byte[] buffer, int offset, int length) throws IOException {
            if (state != OPEN) {
                throw new SocketException("socket is closed");
            }
            while (true) {
                int status = NativeTls.read(ssl, buffer, offset, length);
                pumpWire();
                if (status == NativeTls.CLOSED) {
                    return -1;
                }
                if (status == NativeTls.WANT_READ) {
                    if (!readWire()) {
                        return -1;
                    }
                    continue;
                }
                if (status == NativeTls.WANT_WRITE) {
                    pumpWire();
                    continue;
                }
                if (status < 0) {
                    throw new SocketException("TLS read failed");
                }
                return status;
            }
        }

        public void close() throws IOException {
            OgPlaySslSocket.this.close();
        }
    }

    private final class AppOutputStream extends OutputStream {
        public void write(int b) throws IOException {
            write(new byte[] {(byte) b}, 0, 1);
        }

        public void write(byte[] buffer, int offset, int length) throws IOException {
            if (state != OPEN) {
                throw new SocketException("socket is closed");
            }
            int remaining = length;
            int position = offset;
            while (remaining > 0) {
                int status = NativeTls.write(ssl, buffer, position, remaining);
                pumpWire();
                if (status == NativeTls.WANT_READ) {
                    if (!readWire()) {
                        throw new SocketException("TLS write truncated");
                    }
                    continue;
                }
                if (status == NativeTls.WANT_WRITE) {
                    pumpWire();
                    continue;
                }
                if (status <= 0) {
                    throw new SocketException("TLS write failed");
                }
                position += status;
                remaining -= status;
            }
        }

        public void close() throws IOException {
            OgPlaySslSocket.this.close();
        }
    }

    private static final class FailedSession implements SSLSession {
        public byte[] getId() {
            return new byte[0];
        }

        public javax.net.ssl.SSLSessionContext getSessionContext() {
            return null;
        }

        public long getCreationTime() {
            return 0;
        }

        public long getLastAccessedTime() {
            return 0;
        }

        public void invalidate() {}

        public boolean isValid() {
            return false;
        }

        public void putValue(String name, Object value) {}

        public Object getValue(String name) {
            return null;
        }

        public void removeValue(String name) {}

        public String[] getValueNames() {
            return new String[0];
        }

        public Certificate[] getPeerCertificates() throws javax.net.ssl.SSLPeerUnverifiedException {
            throw new javax.net.ssl.SSLPeerUnverifiedException("handshake failed");
        }

        public Certificate[] getLocalCertificates() {
            return null;
        }

        public javax.security.cert.X509Certificate[] getPeerCertificateChain()
                throws javax.net.ssl.SSLPeerUnverifiedException {
            throw new javax.net.ssl.SSLPeerUnverifiedException("handshake failed");
        }

        public java.security.Principal getPeerPrincipal()
                throws javax.net.ssl.SSLPeerUnverifiedException {
            throw new javax.net.ssl.SSLPeerUnverifiedException("handshake failed");
        }

        public java.security.Principal getLocalPrincipal() {
            return null;
        }

        public String getCipherSuite() {
            return "SSL_NULL_WITH_NULL_NULL";
        }

        public String getProtocol() {
            return "NONE";
        }

        public String getPeerHost() {
            return null;
        }

        public int getPeerPort() {
            return -1;
        }

        public int getPacketBufferSize() {
            return TrustLimits.MAX_WIRE_BUFFER;
        }

        public int getApplicationBufferSize() {
            return TrustLimits.MAX_APP_BUFFER;
        }
    }
}
