package org.ogplay.security;

import java.io.ByteArrayOutputStream;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Date;
import java.util.Enumeration;
import javax.net.ssl.X509TrustManager;

/** X509TrustManager backed by an explicit KeyStore snapshot and native path verification. */
public final class X509TrustManagerImpl implements X509TrustManager {
    private final X509Certificate[] acceptedIssuers;
    private final byte[] packedAnchors;

    public X509TrustManagerImpl(KeyStore keyStore) throws KeyStoreException {
        if (keyStore == null) {
            throw new KeyStoreException("trust store is null");
        }
        ArrayList trusted = new ArrayList();
        Enumeration aliases = keyStore.aliases();
        while (aliases.hasMoreElements()) {
            String alias = (String) aliases.nextElement();
            Certificate cert = keyStore.getCertificate(alias);
            if (cert instanceof X509Certificate) {
                trusted.add((X509Certificate) cert);
            }
        }
        acceptedIssuers = (X509Certificate[]) trusted.toArray(new X509Certificate[trusted.size()]);
        try {
            packedAnchors = packCertificates(acceptedIssuers);
        } catch (CertificateException e) {
            throw new KeyStoreException("failed to encode trust anchors", e);
        }
    }

    public void checkClientTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        checkTrusted(chain, authType, true);
    }

    public void checkServerTrusted(X509Certificate[] chain, String authType)
            throws CertificateException {
        checkTrusted(chain, authType, false);
    }

    public X509Certificate[] getAcceptedIssuers() {
        X509Certificate[] copy = new X509Certificate[acceptedIssuers.length];
        System.arraycopy(acceptedIssuers, 0, copy, 0, acceptedIssuers.length);
        return copy;
    }

    private void checkTrusted(X509Certificate[] chain, String authType, boolean clientAuth)
            throws CertificateException {
        if (chain == null || chain.length == 0 || authType == null || authType.length() == 0) {
            throw new IllegalArgumentException("null or zero-length parameter");
        }
        if (chain.length > TrustLimits.MAX_CHAIN_CERTS) {
            throw new CertificateException("certificate chain exceeds limit");
        }
        byte[] anchors = packedAnchors;
        int bytes = 0;
        for (int i = 0; i < chain.length; i++) {
            if (chain[i] == null) {
                throw new CertificateException("certificate chain contains a null entry");
            }
            chain[i].checkValidity(new Date(1704067200000L));
            rejectWeakSignature(chain[i]);
            bytes += copyEncoded(chain[i]).length;
            if (bytes > TrustLimits.MAX_CHAIN_BYTES) {
                throw new CertificateException("certificate chain exceeds size limit");
            }
        }
        requireAuthType(chain[0], authType);
        byte[] extras;
        if (chain.length == 1) {
            extras = NativeTrust.emptyPacked();
        } else {
            extras = packCertificates(chain, 1);
        }
        NativeTrust.verifyPath(copyEncoded(chain[0]), extras, anchors,
                clientAuth ? "CLIENT" : "SERVER");
    }

    private static void requireAuthType(X509Certificate leaf, String authType)
            throws CertificateException {
        String algorithm = normalizeKeyAlgorithm(leaf.getPublicKey().getAlgorithm());
        String expected = expectedKeyAlgorithm(authType);
        if (!expected.equals(algorithm)) {
            throw new CertificateException(
                    "authType " + authType + " does not match certificate key " + algorithm);
        }
    }

    private static String normalizeKeyAlgorithm(String algorithm) throws CertificateException {
        if (algorithm == null || algorithm.length() == 0) {
            throw new CertificateException("certificate public key algorithm is missing");
        }
        String upper = algorithm.toUpperCase();
        if (upper.equals("RSA") || algorithm.equals("1.2.840.113549.1.1.1")) {
            return "RSA";
        }
        if (upper.equals("EC") || upper.equals("ECDSA") || algorithm.equals("1.2.840.10045.2.1")) {
            return "EC";
        }
        if (upper.equals("DSA") || algorithm.equals("1.2.840.10040.4.1")) {
            return "DSA";
        }
        return algorithm;
    }

    private static String expectedKeyAlgorithm(String authType) throws CertificateException {
        String upper = authType.toUpperCase();
        if (upper.indexOf("ECDSA") >= 0 || upper.equals("EC")) {
            return "EC";
        }
        if (upper.indexOf("RSA") >= 0) {
            return "RSA";
        }
        if (upper.indexOf("DSA") >= 0) {
            return "DSA";
        }
        throw new CertificateException("unsupported authType " + authType);
    }

    private static void rejectWeakSignature(X509Certificate cert) throws CertificateException {
        String name = cert.getSigAlgName();
        if (name == null) {
            throw new CertificateException("certificate signature algorithm is missing");
        }
        String upper = name.toUpperCase();
        if (upper.indexOf("MD2") >= 0 || upper.indexOf("MD5") >= 0) {
            throw new CertificateException("weak certificate signature algorithm: " + name);
        }
    }

    private static byte[] copyEncoded(X509Certificate cert) throws CertificateException {
        byte[] raw = NativeTrust.encodedCopy(cert);
        if (raw == null || raw.length == 0 || raw.length > TrustLimits.MAX_CERT_DER) {
            throw new CertificateException("certificate encoding exceeds limit");
        }
        byte[] copy = new byte[raw.length];
        System.arraycopy(raw, 0, copy, 0, raw.length);
        return copy;
    }

    private static byte[] packCertificates(X509Certificate[] certs) throws CertificateException {
        return packCertificates(certs, 0);
    }

    private static byte[] packCertificates(X509Certificate[] certs, int first)
            throws CertificateException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        NativeTrust.writeInt(out, certs.length - first);
        int total = 0;
        for (int i = first; i < certs.length; i++) {
            byte[] copy = copyEncoded(certs[i]);
            total += copy.length;
            if (total > TrustLimits.MAX_CHAIN_BYTES) {
                throw new CertificateException("certificate chain exceeds size limit");
            }
            NativeTrust.writeInt(out, copy.length);
            out.write(copy, 0, copy.length);
        }
        return out.toByteArray();
    }
}
