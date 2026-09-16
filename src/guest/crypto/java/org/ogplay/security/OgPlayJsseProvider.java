package org.ogplay.security;

import java.security.Provider;

/** OGPlay JSSE provider. Does not impersonate AndroidOpenSSL or BouncyCastle. */
public final class OgPlayJsseProvider extends Provider {
    public OgPlayJsseProvider() {
        super("OGPlayJSSE", 1.0, "OGPlay API 19 JSSE provider");
        put("TrustManagerFactory.PKIX", "org.ogplay.security.PkixTrustManagerFactorySpi");
        put("Alg.Alias.TrustManagerFactory.X509", "PKIX");
        put("KeyStore.AndroidCAStore", "org.ogplay.security.AndroidCaStoreSpi");
        put("KeyManagerFactory.PKIX", "org.ogplay.security.OgPlayKeyManagerFactorySpi");
        put("Alg.Alias.KeyManagerFactory.X509", "PKIX");
        put("SSLContext.TLS", "org.ogplay.security.OgPlaySslContextSpi");
        put("SSLContext.TLSv1", "org.ogplay.security.OgPlaySslContextSpi$TLSv1");
        put("SSLContext.TLSv1.1", "org.ogplay.security.OgPlaySslContextSpi$TLSv11");
        put("SSLContext.TLSv1.2", "org.ogplay.security.OgPlaySslContextSpi$TLSv12");
        put("SSLContext.Default", "org.ogplay.security.OgPlaySslContextSpi$DefaultContext");
    }
}
