package org.ogplay.security;

import java.security.InvalidAlgorithmParameterException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.io.IOException;
import javax.net.ssl.ManagerFactoryParameters;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactorySpi;

/**
 * PKIX TrustManagerFactory SPI. init(null) uses the host roots with the host engine,
 * otherwise the injected AndroidCAStore snapshot.
 */
public final class PkixTrustManagerFactorySpi extends TrustManagerFactorySpi {
    private KeyStore keyStore;
    private boolean initialized;
    private boolean hostDefault;

    public void engineInit(KeyStore ks) throws KeyStoreException {
        hostDefault = ks == null && Tls.HOST;
        if (ks != null) {
            keyStore = ks;
        } else if (hostDefault) {
            keyStore = null;
        } else {
            keyStore = KeyStore.getInstance("AndroidCAStore");
            try {
                keyStore.load(null, null);
            } catch (IOException e) {
                throw new KeyStoreException(e);
            } catch (CertificateException e) {
                throw new KeyStoreException(e);
            } catch (NoSuchAlgorithmException e) {
                throw new KeyStoreException(e);
            }
        }
        initialized = true;
    }

    public void engineInit(ManagerFactoryParameters spec)
            throws InvalidAlgorithmParameterException {
        throw new InvalidAlgorithmParameterException("ManagerFactoryParameters not supported");
    }

    public TrustManager[] engineGetTrustManagers() {
        if (!initialized || (keyStore == null && !hostDefault)) {
            throw new IllegalStateException("TrustManagerFactory is not initialized");
        }
        if (hostDefault) {
            return new TrustManager[] { new HostX509TrustManager() };
        }
        try {
            return new TrustManager[] { new X509TrustManagerImpl(keyStore) };
        } catch (KeyStoreException e) {
            throw new IllegalStateException(e);
        }
    }
}
