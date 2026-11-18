package org.ogplay.security;

import java.security.Provider;

/** Bounded software algorithms not supplied by the API 19 Conscrypt provider. */
public final class OgPlayCryptoProvider extends Provider {
    public OgPlayCryptoProvider() {
        super("OGPlayCrypto", 1.0, "OGPlay software crypto provider");
        put("KeyGenerator.AES", "org.ogplay.security.AesKeyGenerator");
        put("AlgorithmParameters.AES", "org.ogplay.security.AesParameters");
        put("Cipher.RSA", "org.ogplay.security.RsaCipher");
        put("AlgorithmParameters.OAEP", "org.ogplay.security.OaepParameters");
        put("MessageDigest.SHA-224", "org.ogplay.security.Sha224Digest");
        put("Alg.Alias.MessageDigest.SHA224", "SHA-224");
        put("Alg.Alias.MessageDigest.2.16.840.1.101.3.4.2.4", "SHA-224");
        String name = "PBEWITHSHAAND256BITAES-CBC-BC";
        put("SecretKeyFactory." + name, "org.ogplay.security.Pkcs12AesSecretKeyFactory");
        put("Alg.Alias.SecretKeyFactory.PBEWITHSHA1AND256BITAES-CBC-BC", name);
        put("Alg.Alias.SecretKeyFactory.PBEWITHSHA-1AND256BITAES-CBC-BC", name);
    }
}
