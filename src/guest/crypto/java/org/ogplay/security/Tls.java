package org.ogplay.security;

/**
 * The TLS engine of this process: the host engine (host roots, TLS 1.3) or
 * guest libssl (bundled CA set, TLS 1.2). Chosen once per process.
 */
final class Tls {
    static final int OK = NativeTls.OK;
    static final int WANT_READ = NativeTls.WANT_READ;
    static final int WANT_WRITE = NativeTls.WANT_WRITE;
    static final int CLOSED = NativeTls.CLOSED;
    static final int FAILED = NativeTls.FAILED;
    /** read/write results besides a positive byte count. */
    static final int IO_CLOSED = 0;
    static final int IO_FAILED = -1;
    static final int IO_WANT_READ = -2;
    static final int IO_WANT_WRITE = -3;

    static final boolean HOST = HostTls.selected();

    private Tls() {}

    /** The host engine leaves peer verification to the caller. */
    static boolean verifiesDuringHandshake() {
        return !HOST;
    }

    static void seed(byte[] entropy) {
        // The host engine draws from the host CSPRNG.
        if (!HOST) {
            NativeTls.seed(entropy);
        }
    }

    static long createContext(String[] protocols, String[] cipherSuites) {
        return HOST ? HostTls.createContext(protocols, cipherSuites)
                : NativeTls.createContext(protocols, cipherSuites);
    }

    static void freeContext(long context) {
        if (HOST) {
            HostTls.freeContext(context);
        } else {
            NativeTls.freeContext(context);
        }
    }

    static long createSsl(long context, String hostname) {
        return HOST ? HostTls.createSsl(context, hostname) : NativeTls.createSsl(context, hostname);
    }

    static void configure(long ssl, String[] protocols, String[] cipherSuites) {
        if (HOST) {
            HostTls.configure(ssl, protocols, cipherSuites);
        } else {
            NativeTls.configure(ssl, protocols, cipherSuites);
        }
    }

    static void setClientKey(long ssl, byte[] pkcs8, byte[] packedChain) {
        if (HOST) {
            HostTls.setClientKey(ssl, pkcs8, packedChain);
        } else {
            NativeTls.setClientKey(ssl, pkcs8, packedChain);
        }
    }

    static int handshake(long ssl, Object trustManager, boolean clientAuth) {
        return HOST ? HostTls.handshake(ssl) : NativeTls.handshake(ssl, trustManager, clientAuth);
    }

    static byte[] peerCertificates(long ssl) {
        return HOST ? HostTls.peerCertificates(ssl) : NativeTls.peerCertificates(ssl);
    }

    static String protocol(long ssl) {
        return HOST ? HostTls.protocol(ssl) : NativeTls.protocol(ssl);
    }

    static String cipherSuite(long ssl) {
        return HOST ? HostTls.cipherSuite(ssl) : NativeTls.cipherSuite(ssl);
    }

    static String authType(long ssl) {
        return HOST ? HostTls.authType(ssl) : "RSA";
    }

    static byte[] sessionId(long ssl) {
        return HOST ? HostTls.sessionId(ssl) : NativeTls.sessionId(ssl);
    }

    static byte[] sessionState(long ssl) {
        return HOST ? HostTls.sessionState(ssl) : NativeTls.sessionState(ssl);
    }

    static boolean setSession(long ssl, byte[] encoded) {
        return HOST ? HostTls.setSession(ssl, encoded) : NativeTls.setSession(ssl, encoded);
    }

    static boolean sessionReused(long ssl) {
        return HOST ? HostTls.sessionReused(ssl) : NativeTls.sessionReused(ssl);
    }

    static int read(long ssl, byte[] buffer, int offset, int length) {
        if (HOST) {
            return HostTls.read(ssl, buffer, offset, length);
        }
        return guestIo(NativeTls.read(ssl, buffer, offset, length));
    }

    static int write(long ssl, byte[] buffer, int offset, int length) {
        if (HOST) {
            return HostTls.write(ssl, buffer, offset, length);
        }
        return guestIo(NativeTls.write(ssl, buffer, offset, length));
    }

    /** Guest libssl reports WANT_* with the positive handshake codes. */
    private static int guestIo(int status) {
        if (status == NativeTls.WANT_READ) {
            return IO_WANT_READ;
        }
        if (status == NativeTls.WANT_WRITE) {
            return IO_WANT_WRITE;
        }
        return status;
    }

    static byte[] pullWire(long ssl) {
        return HOST ? HostTls.pullWire(ssl) : NativeTls.pullWire(ssl);
    }

    static void pushWire(long ssl, byte[] buffer, int offset, int length) {
        if (HOST) {
            HostTls.pushWire(ssl, buffer, offset, length);
        } else {
            NativeTls.pushWire(ssl, buffer, offset, length);
        }
    }

    static int shutdown(long ssl) {
        return HOST ? HostTls.shutdown(ssl) : NativeTls.shutdown(ssl);
    }

    static void freeSsl(long ssl) {
        if (HOST) {
            HostTls.freeSsl(ssl);
        } else {
            NativeTls.freeSsl(ssl);
        }
    }

    static String[] supportedCipherSuites() {
        return HOST ? HostTls.supportedCipherSuites() : NativeTls.supportedCipherSuites();
    }

    static String[] defaultCipherSuites() {
        return HOST ? HostTls.defaultCipherSuites() : NativeTls.defaultCipherSuites();
    }

    static String[] supportedProtocols() {
        return HOST ? new String[] {"TLSv1.3", "TLSv1.2", "TLSv1.1", "TLSv1"}
                : new String[] {"TLSv1.2", "TLSv1.1", "TLSv1"};
    }
}
