package org.ogplay.security;

import java.security.KeyManagementException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import javax.net.ssl.KeyManager;
import javax.net.ssl.SSLContextSpi;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.SSLServerSocketFactory;
import javax.net.ssl.SSLSessionContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509KeyManager;
import javax.net.ssl.X509TrustManager;

/** Client-only SSLContextSpi. Server sockets and SSLEngine fail explicitly. */
public class OgPlaySslContextSpi extends SSLContextSpi {
    private final String[] defaultProtocols;
    private boolean initialized;
    private X509TrustManager trustManager;
    private X509KeyManager keyManager;
    private SecureRandom secureRandom;
    private OgPlaySslSessionContext clientSessions;
    private OgPlaySslSocketFactory socketFactory;
    private long nativeContext;

    public OgPlaySslContextSpi() {
        this(new String[] {"TLSv1.2", "TLSv1.1", "TLSv1"});
    }

    OgPlaySslContextSpi(String[] defaultProtocols) {
        this.defaultProtocols = defaultProtocols;
    }

    protected void engineInit(KeyManager[] km, TrustManager[] tm, SecureRandom sr)
            throws KeyManagementException {
        try {
            trustManager = selectTrustManager(tm);
            keyManager = selectKeyManager(km);
            secureRandom = sr != null ? sr : new SecureRandom();
            byte[] entropy = new byte[64];
            secureRandom.nextBytes(entropy);
            NativeTls.seed(entropy);
            if (nativeContext != 0) {
                NativeTls.freeContext(nativeContext);
                nativeContext = 0;
            }
            nativeContext = NativeTls.createContext(defaultProtocols,
                    NativeTls.defaultCipherSuites());
            clientSessions = new OgPlaySslSessionContext();
            socketFactory = new OgPlaySslSocketFactory(this);
            initialized = true;
        } catch (Exception e) {
            throw new KeyManagementException(e);
        }
    }

    protected SSLSocketFactory engineGetSocketFactory() {
        requireInit();
        return socketFactory;
    }

    protected SSLServerSocketFactory engineGetServerSocketFactory() {
        requireInit();
        throw new UnsupportedOperationException("server TLS is not supported");
    }

    protected SSLEngine engineCreateSSLEngine() {
        requireInit();
        throw new UnsupportedOperationException("SSLEngine is not supported");
    }

    protected SSLEngine engineCreateSSLEngine(String host, int port) {
        requireInit();
        throw new UnsupportedOperationException("SSLEngine is not supported");
    }

    protected SSLSessionContext engineGetServerSessionContext() {
        return null;
    }

    protected SSLSessionContext engineGetClientSessionContext() {
        requireInit();
        return clientSessions;
    }

    X509TrustManager trustManager() {
        requireInit();
        return trustManager;
    }

    X509KeyManager keyManager() {
        requireInit();
        return keyManager;
    }

    SecureRandom secureRandom() {
        return secureRandom;
    }

    String[] protocols() {
        return copy(defaultProtocols);
    }

    OgPlaySslSessionContext sessions() {
        requireInit();
        return clientSessions;
    }

    long nativeContextToken() {
        requireInit();
        return nativeContext;
    }

    private void requireInit() {
        if (!initialized) {
            throw new IllegalStateException("SSLContext is not initialized");
        }
    }

    private static X509TrustManager selectTrustManager(TrustManager[] managers)
            throws KeyStoreException, NoSuchAlgorithmException {
        if (managers != null) {
            for (int i = 0; i < managers.length; i++) {
                if (managers[i] instanceof X509TrustManager) {
                    return (X509TrustManager) managers[i];
                }
            }
            throw new KeyStoreException("no X509TrustManager provided");
        }
        TrustManagerFactory factory = TrustManagerFactory.getInstance(
                TrustManagerFactory.getDefaultAlgorithm());
        factory.init((KeyStore) null);
        TrustManager[] created = factory.getTrustManagers();
        for (int i = 0; i < created.length; i++) {
            if (created[i] instanceof X509TrustManager) {
                return (X509TrustManager) created[i];
            }
        }
        throw new KeyStoreException("default TrustManagerFactory produced no X509TrustManager");
    }

    private static X509KeyManager selectKeyManager(KeyManager[] managers) {
        if (managers == null) {
            return null;
        }
        for (int i = 0; i < managers.length; i++) {
            if (managers[i] instanceof X509KeyManager) {
                return (X509KeyManager) managers[i];
            }
        }
        return null;
    }

    static String[] copy(String[] values) {
        String[] copy = new String[values.length];
        System.arraycopy(values, 0, copy, 0, values.length);
        return copy;
    }

    public static final class TLSv1 extends OgPlaySslContextSpi {
        public TLSv1() {
            super(new String[] {"TLSv1"});
        }
    }

    public static final class TLSv11 extends OgPlaySslContextSpi {
        public TLSv11() {
            super(new String[] {"TLSv1.1"});
        }
    }

    public static final class TLSv12 extends OgPlaySslContextSpi {
        public TLSv12() {
            super(new String[] {"TLSv1.2"});
        }
    }

    public static final class DefaultContext extends OgPlaySslContextSpi {
        public DefaultContext() {
            super(new String[] {"TLSv1.2", "TLSv1.1", "TLSv1"});
            try {
                engineInit(null, null, null);
            } catch (Exception e) {
                throw new java.security.ProviderException(e);
            }
        }
    }
}
