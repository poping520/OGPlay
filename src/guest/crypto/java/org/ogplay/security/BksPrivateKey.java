package org.ogplay.security;

import java.security.PrivateKey;
import java.util.Arrays;

/** Validated RSA/EC PKCS#8 key backed by a guest libcrypto EVP_PKEY token. */
final class BksPrivateKey implements PrivateKey {
    private static final long serialVersionUID = 1L;
    private final long token;
    private final String algorithm;

    BksPrivateKey(String algorithm, byte[] encoded) {
        if (!"RSA".equals(algorithm) && !"EC".equals(algorithm)) {
            throw new IllegalArgumentException("unsupported private key algorithm");
        }
        token = NativeKeyStoreCrypto.decodePrivateKey(encoded);
        int expected = "RSA".equals(algorithm) ? 6 : 408;
        if (NativeKeyStoreCrypto.privateKeyType(token) != expected) {
            NativeKeyStoreCrypto.freePrivateKey(token);
            throw new IllegalArgumentException("private key algorithm mismatch");
        }
        this.algorithm = algorithm;
    }

    public String getAlgorithm() { return algorithm; }
    public String getFormat() { return "PKCS#8"; }
    public byte[] getEncoded() { return NativeKeyStoreCrypto.encodePrivateKey(token); }

    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof PrivateKey)) return false;
        PrivateKey key = (PrivateKey) other;
        return algorithm.equals(key.getAlgorithm()) && "PKCS#8".equals(key.getFormat()) &&
               Arrays.equals(getEncoded(), key.getEncoded());
    }
    public int hashCode() { return 31 * algorithm.hashCode() + Arrays.hashCode(getEncoded()); }
}
