package org.ogplay.security;

import java.security.InvalidAlgorithmParameterException;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;
import javax.crypto.KeyGeneratorSpi;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;

/** API 19 BC AES key generation, including its default strength and rounding. */
public final class AesKeyGenerator extends KeyGeneratorSpi {
    private static final int DEFAULT_BITS = 192;

    private SecureRandom random;
    private int keyBytes;

    @Override protected void engineInit(SecureRandom source) {
        if (source != null) engineInit(DEFAULT_BITS, source);
    }

    @Override protected void engineInit(int bits, SecureRandom source) {
        random = source == null ? new SecureRandom() : source;
        keyBytes = (bits + 7) / 8;
    }

    @Override protected void engineInit(AlgorithmParameterSpec spec, SecureRandom source)
            throws InvalidAlgorithmParameterException {
        throw new InvalidAlgorithmParameterException("Not Implemented");
    }

    @Override protected SecretKey engineGenerateKey() {
        if (random == null) engineInit(DEFAULT_BITS, null);
        byte[] key = new byte[keyBytes];
        random.nextBytes(key);
        return new SecretKeySpec(key, "AES");
    }
}
