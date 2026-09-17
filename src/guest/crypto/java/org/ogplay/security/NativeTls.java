package org.ogplay.security;

import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.net.SocketAddress;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.X509TrustManager;

/** Private guest libssl session tokens. Ciphertext stays in memory BIOs. */
final class NativeTls {
    static final int OK = 1;
    static final int WANT_READ = 2;
    static final int WANT_WRITE = 3;
    static final int CLOSED = 0;
    static final int FAILED = -1;

    private NativeTls() {}

    /** Host overlay: mark this Socket as a TLS policy channel. Transport stays raw. */
    static void markTls(Socket socket) {}

    /** Host overlay: enforce allow_tls/allowed_hosts without connecting this socket. */
    static void requireTls(String host) {}

    /** Host overlay: connect over the unique raw NetworkRuntime channel. */
    static void connectRaw(Socket socket, SocketAddress endpoint, int timeout) {}

    static InputStream socketInput(Socket socket) { return null; }

    static OutputStream socketOutput(Socket socket) { return null; }

    static native void seed(byte[] entropy);

    static native long createContext(String[] protocols, String[] cipherSuites);

    static native long createSsl(long context, String hostname);

    static native void configure(long ssl, String[] protocols, String[] cipherSuites);

    static native void setClientKey(long ssl, byte[] pkcs8, byte[] packedChain);

    static native int handshake(long ssl, Object trustManager, boolean clientAuth);

    static native byte[] peerCertificates(long ssl);

    static native String protocol(long ssl);

    static native String cipherSuite(long ssl);

    static native byte[] sessionId(long ssl);

    static native byte[] sessionState(long ssl);

    static native boolean setSession(long ssl, byte[] encoded);

    static native boolean sessionReused(long ssl);

    static native int read(long ssl, byte[] buffer, int offset, int length);

    static native int write(long ssl, byte[] buffer, int offset, int length);

    static native byte[] pullWire(long ssl);

    static native void pushWire(long ssl, byte[] buffer, int offset, int length);

    static native int shutdown(long ssl);

    static native void freeSsl(long ssl);

    static native void freeContext(long context);

    static native String[] supportedCipherSuites();

    static native String[] defaultCipherSuites();

    static void dispatchVerify(Object trustManager, byte[] packed, String authType,
                               boolean clientAuth) throws CertificateException {
        if (!(trustManager instanceof X509TrustManager)) {
            throw new CertificateException("TrustManager is not an X509TrustManager");
        }
        if (packed == null || packed.length < 8) {
            throw new CertificateException("peer certificate chain is empty");
        }
        try {
            CertificateFactory factory = CertificateFactory.getInstance("X.509");
            int count = NativeTrust.readInt(packed, 0);
            if (count < 1 || count > TrustLimits.MAX_CHAIN_CERTS) {
                throw new CertificateException("peer certificate chain is empty");
            }
            X509Certificate[] chain = new X509Certificate[count];
            int offset = 4;
            for (int i = 0; i < count; i++) {
                if (offset > packed.length - 4) {
                    throw new CertificateException("peer certificate encodings do not match");
                }
                int n = NativeTrust.readInt(packed, offset);
                offset += 4;
                if (n < 1 || n > TrustLimits.MAX_CERT_DER || n > packed.length - offset) {
                    throw new CertificateException("peer certificate encoding exceeds limit");
                }
                byte[] der = new byte[n];
                System.arraycopy(packed, offset, der, 0, n);
                offset += n;
                chain[i] = (X509Certificate) factory.generateCertificate(
                        new ByteArrayInputStream(der));
            }
            X509TrustManager manager = (X509TrustManager) trustManager;
            if (clientAuth) {
                manager.checkClientTrusted(chain, authType);
            } else {
                manager.checkServerTrusted(chain, authType);
            }
        } catch (CertificateException e) {
            throw e;
        } catch (Exception e) {
            throw new CertificateException(e);
        }
    }
}
