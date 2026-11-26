package org.ogplay.security;

import java.security.cert.CertificateException;

/**
 * Host TLS engine natives (OpenSSL 3 with memory buffers) and host trust
 * evaluation. Handshake statuses match NativeTls; read/write return a byte
 * count or Tls.IO_* codes. The handshake never verifies the peer: callers
 * pass the chain to the TrustManager.
 */
final class HostTls {
    private HostTls() {}

    /** True when this process uses the host engine. */
    static native boolean selected();

    static native long createContext(String[] protocols, String[] cipherSuites);

    static native void freeContext(long context);

    static native long createSsl(long context, String hostname);

    static native void configure(long ssl, String[] protocols, String[] cipherSuites);

    static native void setClientKey(long ssl, byte[] pkcs8, byte[] packedChain);

    static native int handshake(long ssl);

    static native byte[] peerCertificates(long ssl);

    static native String protocol(long ssl);

    static native String cipherSuite(long ssl);

    static native String authType(long ssl);

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

    static native String[] supportedCipherSuites();

    static native String[] defaultCipherSuites();

    /** Server chain (NativeTrust packing) against the host's trusted roots. */
    static native void verifyServer(byte[] packedChain) throws CertificateException;
}
