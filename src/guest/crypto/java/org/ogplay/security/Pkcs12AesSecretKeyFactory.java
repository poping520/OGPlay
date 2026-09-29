package org.ogplay.security;

import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.KeySpec;
import java.util.Arrays;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactorySpi;
import javax.crypto.interfaces.PBEKey;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;

/** API 19 PKCS12/SHA1 AES-256 key derivation. Not PBKDF2 or a PBE Cipher. */
public final class Pkcs12AesSecretKeyFactory extends SecretKeyFactorySpi {
    private static final String ALGORITHM = "PBEWithSHA1And256BitAES-CBC-BC";

    @Override
    protected SecretKey engineGenerateSecret(KeySpec keySpec) throws InvalidKeySpecException {
        if (!(keySpec instanceof PBEKeySpec)) throw new InvalidKeySpecException("Expected PBEKeySpec");
        PBEKeySpec spec = (PBEKeySpec) keySpec;
        char[] password = spec.getPassword();
        byte[] salt = spec.getSalt();
        try {
            if (password.length > Pkcs12Kdf.MAX_INPUT_LENGTH ||
                    (salt != null && salt.length > Pkcs12Kdf.MAX_INPUT_LENGTH)) {
                throw new InvalidKeySpecException("PKCS12 input exceeds resource limit");
            }
            // API 19's named factory fixes key size, independently of spec.getKeyLength().
            byte[] encoded = salt == null ? null : Pkcs12Kdf.derive(password, salt,
                    spec.getIterationCount(), Pkcs12Kdf.PURPOSE_KEY, 32);
            return new Key(spec, encoded);
        } catch (NoSuchAlgorithmException e) {
            throw new InvalidKeySpecException("SHA-1 unavailable", e);
        } catch (IllegalArgumentException e) {
            throw new InvalidKeySpecException(e.getMessage(), e);
        } finally {
            Arrays.fill(password, '\0');
        }
    }

    @Override
    protected KeySpec engineGetKeySpec(SecretKey key, Class keySpec) throws InvalidKeySpecException {
        if (key == null || keySpec == null) throw new InvalidKeySpecException("Null key or specification");
        byte[] encoded = key.getEncoded();
        if (encoded == null) throw new InvalidKeySpecException("Key has no encoding");
        try {
            if (SecretKeySpec.class.isAssignableFrom(keySpec)) return new SecretKeySpec(encoded, ALGORITHM);
            // Match API 19's raw-byte specification conversion, including DES/DESede.
            return (KeySpec) keySpec.getConstructor(byte[].class).newInstance(new Object[] {encoded});
        } catch (Exception e) {
            throw new InvalidKeySpecException(e.toString(), e);
        }
    }

    @Override
    protected SecretKey engineTranslateKey(SecretKey key) throws InvalidKeyException {
        if (key == null || !ALGORITHM.equalsIgnoreCase(key.getAlgorithm())) {
            throw new InvalidKeyException("Key not of type " + ALGORITHM);
        }
        byte[] encoded = key.getEncoded();
        if (encoded == null || encoded.length == 0) throw new InvalidKeyException("Key has no encoding");
        return new SecretKeySpec(encoded, ALGORITHM);
    }

    private static final class Key implements PBEKey {
        private static final long serialVersionUID = 1L;
        private final PBEKeySpec spec;
        private final byte[] encoded;

        Key(PBEKeySpec spec, byte[] encoded) {
            this.spec = spec;
            this.encoded = encoded;
        }

        public String getAlgorithm() { return ALGORITHM; }
        public String getFormat() { return "RAW"; }
        public char[] getPassword() { return spec.getPassword(); }
        public byte[] getSalt() { return spec.getSalt(); }
        public int getIterationCount() { return spec.getIterationCount(); }
        public byte[] getEncoded() {
            if (encoded != null) return encoded.clone();
            char[] password = spec.getPassword();
            try { return Pkcs12Kdf.passwordBytes(password); }
            finally { Arrays.fill(password, '\0'); }
        }
    }
}
