package org.ogplay.security;

import java.io.ByteArrayOutputStream;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.Date;
import javax.net.ssl.X509TrustManager;

/**
 * Default X509TrustManager of the host engine: server chains are judged by
 * the host operating system's trusted roots. Client chains have no host
 * policy and are rejected.
 */
final class HostX509TrustManager implements X509TrustManager {
    private X509Certificate[] acceptedIssuers;

    public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
        check(chain, authType);
        throw new CertificateException("client certificates have no host trust policy");
    }

    public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
        check(chain, authType);
        for (int i = 0; i < chain.length; i++) {
            chain[i].checkValidity(new Date(System.currentTimeMillis()));
        }
        HostTls.verifyServer(pack(chain));
    }

    /** The bundled AndroidCAStore view; the host keeps its own roots. */
    public synchronized X509Certificate[] getAcceptedIssuers() {
        if (acceptedIssuers == null) {
            try {
                KeyStore store = KeyStore.getInstance("AndroidCAStore");
                store.load(null, null);
                acceptedIssuers = new X509TrustManagerImpl(store).getAcceptedIssuers();
            } catch (Exception e) {
                acceptedIssuers = new X509Certificate[0];
            }
        }
        X509Certificate[] copy = new X509Certificate[acceptedIssuers.length];
        System.arraycopy(acceptedIssuers, 0, copy, 0, acceptedIssuers.length);
        return copy;
    }

    private static void check(X509Certificate[] chain, String authType) throws CertificateException {
        if (chain == null || chain.length == 0 || authType == null || authType.length() == 0) {
            throw new IllegalArgumentException("null or zero-length parameter");
        }
        if (chain.length > TrustLimits.MAX_CHAIN_CERTS) {
            throw new CertificateException("certificate chain exceeds limit");
        }
        for (int i = 0; i < chain.length; i++) {
            if (chain[i] == null) {
                throw new CertificateException("certificate chain contains a null entry");
            }
        }
    }

    private static byte[] pack(X509Certificate[] chain) throws CertificateException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        NativeTrust.writeInt(out, chain.length);
        int total = 0;
        for (int i = 0; i < chain.length; i++) {
            byte[] der = NativeTrust.encodedCopy(chain[i]);
            total += der.length;
            if (der.length > TrustLimits.MAX_CERT_DER || total > TrustLimits.MAX_CHAIN_BYTES) {
                throw new CertificateException("certificate chain exceeds size limit");
            }
            NativeTrust.writeInt(out, der.length);
            out.write(der, 0, der.length);
        }
        return out.toByteArray();
    }
}
