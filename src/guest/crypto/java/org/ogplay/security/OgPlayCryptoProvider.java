package org.ogplay.security;

import java.security.Provider;

/** Bounded software algorithms not supplied by the API 19 Conscrypt provider. */
public final class OgPlayCryptoProvider extends Provider {
    public OgPlayCryptoProvider() {
        super("OGPlayCrypto", 1.0, "OGPlay software crypto provider");
        String name = "PBEWITHSHAAND256BITAES-CBC-BC";
        put("SecretKeyFactory." + name, "org.ogplay.security.Pkcs12AesSecretKeyFactory");
        put("Alg.Alias.SecretKeyFactory.PBEWITHSHA1AND256BITAES-CBC-BC", name);
        put("Alg.Alias.SecretKeyFactory.PBEWITHSHA-1AND256BITAES-CBC-BC", name);
    }
}
