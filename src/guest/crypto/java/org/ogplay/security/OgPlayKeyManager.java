package org.ogplay.security;

import java.net.Socket;
import java.security.Key;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.Principal;
import java.security.PrivateKey;
import java.security.UnrecoverableKeyException;
import java.security.cert.Certificate;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Enumeration;
import javax.net.ssl.X509ExtendedKeyManager;

/** Client X509KeyManager over an explicit BKS KeyStore. */
public final class OgPlayKeyManager extends X509ExtendedKeyManager {
    private final KeyStore keyStore;
    private final char[] password;

    public OgPlayKeyManager(KeyStore keyStore, char[] password) {
        this.keyStore = keyStore;
        this.password = password == null ? null : (char[]) password.clone();
    }

    public String[] getClientAliases(String keyType, Principal[] issuers) {
        return aliases(keyType, issuers, true);
    }

    public String[] getServerAliases(String keyType, Principal[] issuers) {
        return aliases(keyType, issuers, false);
    }

    public String chooseClientAlias(String[] keyTypes, Principal[] issuers, Socket socket) {
        if (keyTypes == null) {
            return null;
        }
        for (int i = 0; i < keyTypes.length; i++) {
            String[] found = getClientAliases(keyTypes[i], issuers);
            if (found != null && found.length > 0) {
                return found[0];
            }
        }
        return null;
    }

    public String chooseServerAlias(String keyType, Principal[] issuers, Socket socket) {
        String[] found = getServerAliases(keyType, issuers);
        return found != null && found.length > 0 ? found[0] : null;
    }

    public X509Certificate[] getCertificateChain(String alias) {
        if (alias == null || keyStore == null) {
            return null;
        }
        try {
            Certificate[] chain = keyStore.getCertificateChain(alias);
            if (chain == null || chain.length == 0) {
                Certificate cert = keyStore.getCertificate(alias);
                if (!(cert instanceof X509Certificate)) {
                    return null;
                }
                return new X509Certificate[] {(X509Certificate) cert};
            }
            X509Certificate[] x509 = new X509Certificate[chain.length];
            for (int i = 0; i < chain.length; i++) {
                if (!(chain[i] instanceof X509Certificate)) {
                    return null;
                }
                x509[i] = (X509Certificate) chain[i];
            }
            return x509;
        } catch (KeyStoreException e) {
            return null;
        }
    }

    public PrivateKey getPrivateKey(String alias) {
        if (alias == null || keyStore == null) {
            return null;
        }
        try {
            Key key = keyStore.getKey(alias, password);
            if (key instanceof PrivateKey) {
                if (key.getEncoded() == null) {
                    throw new UnrecoverableKeyException("private key is not exportable");
                }
                return (PrivateKey) key;
            }
            return null;
        } catch (KeyStoreException e) {
            return null;
        } catch (NoSuchAlgorithmException e) {
            return null;
        } catch (UnrecoverableKeyException e) {
            return null;
        }
    }

    private String[] aliases(String keyType, Principal[] issuers, boolean client) {
        if (keyType == null || keyStore == null) {
            return null;
        }
        ArrayList matches = new ArrayList();
        try {
            Enumeration aliases = keyStore.aliases();
            while (aliases.hasMoreElements()) {
                String alias = (String) aliases.nextElement();
                Key key = keyStore.getKey(alias, password);
                if (!(key instanceof PrivateKey)) {
                    continue;
                }
                if (!keyType.equalsIgnoreCase(key.getAlgorithm())) {
                    continue;
                }
                X509Certificate[] chain = getCertificateChain(alias);
                if (chain == null || chain.length == 0) {
                    continue;
                }
                if (issuers != null && issuers.length > 0 && !issuerMatches(chain, issuers)) {
                    continue;
                }
                matches.add(alias);
            }
        } catch (Exception e) {
            return null;
        }
        if (matches.isEmpty()) {
            return null;
        }
        return (String[]) matches.toArray(new String[matches.size()]);
    }

    private static boolean issuerMatches(X509Certificate[] chain, Principal[] issuers) {
        for (int i = 0; i < chain.length; i++) {
            Principal issuer = chain[i].getIssuerX500Principal();
            Principal subject = chain[i].getSubjectX500Principal();
            for (int j = 0; j < issuers.length; j++) {
                if (issuers[j].equals(issuer) || issuers[j].equals(subject)) {
                    return true;
                }
            }
        }
        return false;
    }
}
