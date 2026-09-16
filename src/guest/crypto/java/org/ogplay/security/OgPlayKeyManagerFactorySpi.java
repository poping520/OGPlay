package org.ogplay.security;

import java.security.InvalidAlgorithmParameterException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.UnrecoverableKeyException;
import javax.net.ssl.KeyManager;
import javax.net.ssl.KeyManagerFactorySpi;
import javax.net.ssl.ManagerFactoryParameters;

/** PKIX KeyManagerFactory SPI over an explicit KeyStore. */
public final class OgPlayKeyManagerFactorySpi extends KeyManagerFactorySpi {
    private KeyStore keyStore;
    private char[] password;
    private boolean initialized;

    protected void engineInit(KeyStore ks, char[] password)
            throws KeyStoreException, NoSuchAlgorithmException, UnrecoverableKeyException {
        keyStore = ks;
        this.password = password == null ? null : (char[]) password.clone();
        initialized = true;
    }

    protected void engineInit(ManagerFactoryParameters spec)
            throws InvalidAlgorithmParameterException {
        throw new InvalidAlgorithmParameterException("ManagerFactoryParameters not supported");
    }

    protected KeyManager[] engineGetKeyManagers() {
        if (!initialized) {
            throw new IllegalStateException("KeyManagerFactory is not initialized");
        }
        return new KeyManager[] { new OgPlayKeyManager(keyStore, password) };
    }
}
