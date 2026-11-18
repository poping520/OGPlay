package org.ogplay.security;

import java.security.AlgorithmParameters;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.KeyFactory;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.security.interfaces.RSAKey;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.InvalidParameterSpecException;
import java.security.spec.MGF1ParameterSpec;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;
import java.util.Locale;
import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.CipherSpi;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import javax.crypto.ShortBufferException;
import javax.crypto.spec.OAEPParameterSpec;
import javax.crypto.spec.PSource;
import javax.crypto.spec.SecretKeySpec;

/** API 19's generic RSA transformation backed by the exact Conscrypt transformations. */
public final class RsaCipher extends CipherSpi {
    private static final String RAW = "NoPadding";
    private static final String PKCS1 = "PKCS1Padding";
    private static final String[] OAEP_DIGESTS = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512" };

    private String padding = RAW;
    private OAEPParameterSpec defaultOaep;
    private OAEPParameterSpec parameters;
    private Cipher delegate;
    private boolean encrypting;
    private SecureRandom random;
    private byte[] labelHash;
    private MessageDigest mgfDigest;
    private byte[] buffer;
    private int used;
    private boolean overflow;
    private int modulusSize;
    private int modulusBits;

    protected void engineSetMode(String mode) throws NoSuchAlgorithmException {
        if (!"ECB".equalsIgnoreCase(mode) && !"NONE".equalsIgnoreCase(mode)) {
            throw new NoSuchAlgorithmException(mode + " unavailable with RSA");
        }
    }

    protected void engineSetPadding(String name) throws NoSuchPaddingException {
        if (RAW.equalsIgnoreCase(name) || PKCS1.equalsIgnoreCase(name)) {
            padding = RAW.equalsIgnoreCase(name) ? RAW : PKCS1;
            defaultOaep = null;
            return;
        }
        if ("OAEPPadding".equalsIgnoreCase(name)) {
            defaultOaep = OAEPParameterSpec.DEFAULT;
            return;
        }
        String upper = name.toUpperCase(Locale.US);
        for (String hash : OAEP_DIGESTS) {
            if (upper.equals("OAEPWITH" + hash + "ANDMGF1PADDING") ||
                    upper.equals("OAEPWITH" + hash.replace("-", "") + "ANDMGF1PADDING")) {
                defaultOaep = new OAEPParameterSpec(hash, "MGF1",
                        new MGF1ParameterSpec(hash), PSource.PSpecified.DEFAULT);
                return;
            }
        }
        throw new NoSuchPaddingException(name + " unavailable with RSA");
    }

    protected int engineGetKeySize(Key key) throws InvalidKeyException {
        if (!(key instanceof RSAPublicKey) && !(key instanceof RSAPrivateKey)) {
            throw new InvalidKeyException("Need RSA private or public key");
        }
        return ((RSAKey) key).getModulus().bitLength();
    }

    protected int engineGetBlockSize() {
        if (parameters != null) return buffer.length;
        if (delegate == null) throw new IllegalStateException("RSA Cipher not initialised");
        return RAW.equals(padding) && encrypting ? (modulusBits - 1) / 8 : delegate.getBlockSize();
    }

    protected int engineGetOutputSize(int inputLength) {
        if (parameters != null) {
            return encrypting ? modulusSize : modulusSize - 2 * labelHash.length - 2;
        }
        if (delegate == null) throw new IllegalStateException("RSA Cipher not initialised");
        return RAW.equals(padding) && !encrypting ? (modulusBits - 1) / 8 : delegate.getOutputSize(inputLength);
    }

    protected byte[] engineGetIV() { return null; }

    protected AlgorithmParameters engineGetParameters() {
        OAEPParameterSpec spec = parameters != null ? parameters : defaultOaep;
        if (spec == null) return null;
        try {
            AlgorithmParameters result = AlgorithmParameters.getInstance("OAEP", "OGPlayCrypto");
            result.init(spec);
            return result;
        } catch (java.security.GeneralSecurityException e) {
            throw new java.security.ProviderException("OAEP parameters unavailable", e);
        }
    }

    protected void engineInit(int opmode, Key key, SecureRandom entropy) throws InvalidKeyException {
        try {
            engineInit(opmode, key, (AlgorithmParameterSpec) null, entropy);
        } catch (InvalidAlgorithmParameterException e) {
            throw new InvalidKeyException(e);
        }
    }

    protected void engineInit(int opmode, Key key, AlgorithmParameters params, SecureRandom entropy)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        try {
            engineInit(opmode, key, params == null ? null : params.getParameterSpec(OAEPParameterSpec.class), entropy);
        } catch (InvalidParameterSpecException e) {
            throw new InvalidAlgorithmParameterException(e);
        }
    }

    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec spec, SecureRandom entropy)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (spec != null && !(spec instanceof OAEPParameterSpec)) {
            throw new InvalidAlgorithmParameterException("Expected OAEPParameterSpec");
        }
        int bits = engineGetKeySize(key);
        int size = (bits + 7) / 8;
        boolean forEncryption = opmode == Cipher.ENCRYPT_MODE || opmode == Cipher.WRAP_MODE;
        OAEPParameterSpec oaep = spec == null ? defaultOaep : (OAEPParameterSpec) spec;
        byte[] hash = null;
        MessageDigest mask = null;
        int capacity = 0;
        if (oaep != null) {
            OaepParameters.validate(oaep);
            try {
                MessageDigest digest = MessageDigest.getInstance(OaepParameters.digestName(oaep.getDigestAlgorithm()));
                mask = MessageDigest.getInstance(OaepParameters.digestName(
                        ((MGF1ParameterSpec) oaep.getMGFParameters()).getDigestAlgorithm()));
                hash = digest.digest(((PSource.PSpecified) oaep.getPSource()).getValue());
            } catch (NoSuchAlgorithmException e) {
                throw new InvalidAlgorithmParameterException(e);
            }
            int maxMessage = size - 2 * hash.length - 2;
            if (maxMessage < 0) throw new InvalidKeyException("RSA key is too short for OAEP");
            capacity = forEncryption ? maxMessage : size;
        }
        Cipher next;
        try {
            next = Cipher.getInstance("RSA/ECB/" + (oaep == null ? padding : RAW), "AndroidOpenSSL");
        } catch (java.security.GeneralSecurityException e) {
            throw new java.security.ProviderException("Conscrypt RSA unavailable", e);
        }
        next.init(oaep == null ? opmode : forEncryption ? Cipher.ENCRYPT_MODE : Cipher.DECRYPT_MODE, key, entropy);
        if (buffer != null) Arrays.fill(buffer, (byte) 0);
        used = 0;
        overflow = false;
        delegate = next;
        encrypting = forEncryption;
        modulusSize = size;
        modulusBits = bits;
        parameters = oaep;
        labelHash = hash;
        mgfDigest = mask;
        random = oaep != null && forEncryption ? (entropy != null ? entropy : new SecureRandom()) : null;
        buffer = oaep == null ? null : new byte[capacity];
    }

    private void append(byte[] input, int offset, int length) {
        if (length > buffer.length - used) {
            overflow = true;
        } else if (!overflow && length != 0) {
            System.arraycopy(input, offset, buffer, used, length);
            used += length;
        }
    }

    protected byte[] engineUpdate(byte[] input, int offset, int length) {
        if (parameters == null) return delegate.update(input, offset, length);
        append(input, offset, length);
        return null;
    }

    protected int engineUpdate(byte[] input, int offset, int length, byte[] output, int outputOffset)
            throws ShortBufferException {
        if (parameters == null) return delegate.update(input, offset, length, output, outputOffset);
        append(input, offset, length);
        return 0;
    }

    protected byte[] engineDoFinal(byte[] input, int offset, int length)
            throws IllegalBlockSizeException, BadPaddingException {
        if (parameters == null) {
            byte[] result = input == null ? delegate.doFinal() : delegate.doFinal(input, offset, length);
            if (!RAW.equals(padding) || encrypting) return result;
            int first = 0;
            while (first < result.length && result[first] == 0) ++first;
            if (first == 0) return result;
            byte[] trimmed = Arrays.copyOfRange(result, first, result.length);
            Arrays.fill(result, (byte) 0);
            return trimmed;
        }
        try {
            append(input, offset, length);
            if (encrypting) {
                if (overflow) throw new IllegalBlockSizeException("input too long for OAEP");
                return encodeOaep();
            }
            if (overflow || used == 0) throw invalidOaep();
            return decodeOaep();
        } finally {
            Arrays.fill(buffer, (byte) 0);
            used = 0;
            overflow = false;
        }
    }

    protected int engineDoFinal(byte[] input, int offset, int length, byte[] output, int outputOffset)
            throws ShortBufferException, IllegalBlockSizeException, BadPaddingException {
        byte[] result = engineDoFinal(input, offset, length);
        try {
            if (outputOffset < 0 || result.length > output.length - outputOffset) {
                throw new ShortBufferException("output buffer is too small");
            }
            System.arraycopy(result, 0, output, outputOffset, result.length);
            return result.length;
        } finally {
            Arrays.fill(result, (byte) 0);
        }
    }

    protected byte[] engineWrap(Key key) throws IllegalBlockSizeException, InvalidKeyException {
        if (parameters == null) return delegate.wrap(key);
        byte[] encoded = key.getEncoded();
        if (encoded == null) throw new InvalidKeyException("key is not encodable");
        try {
            return engineDoFinal(encoded, 0, encoded.length);
        } catch (BadPaddingException e) {
            IllegalBlockSizeException failure = new IllegalBlockSizeException(e.getMessage());
            failure.initCause(e);
            throw failure;
        }
    }

    protected Key engineUnwrap(byte[] wrapped, String algorithm, int type)
            throws InvalidKeyException, NoSuchAlgorithmException {
        if (parameters == null) return delegate.unwrap(wrapped, algorithm, type);
        byte[] encoded = null;
        try {
            encoded = engineDoFinal(wrapped, 0, wrapped.length);
            switch (type) {
                case Cipher.SECRET_KEY: return new SecretKeySpec(encoded, algorithm);
                case Cipher.PUBLIC_KEY: return KeyFactory.getInstance(algorithm).generatePublic(new X509EncodedKeySpec(encoded));
                case Cipher.PRIVATE_KEY: return KeyFactory.getInstance(algorithm).generatePrivate(new PKCS8EncodedKeySpec(encoded));
                default: throw new InvalidKeyException("unknown wrapped key type");
            }
        } catch (BadPaddingException | IllegalBlockSizeException | InvalidKeySpecException e) {
            throw new InvalidKeyException(e);
        } finally {
            if (encoded != null) Arrays.fill(encoded, (byte) 0);
        }
    }

    private byte[] encodeOaep() throws IllegalBlockSizeException, BadPaddingException {
        int h = labelHash.length;
        int db = 1 + h;
        byte[] block = new byte[modulusSize];
        byte[] seed = new byte[h];
        try {
            random.nextBytes(seed);
            System.arraycopy(seed, 0, block, 1, h);
            System.arraycopy(labelHash, 0, block, db, h);
            int message = block.length - used;
            block[message - 1] = 1;
            System.arraycopy(buffer, 0, block, message, used);
            xorMask(block, 1, h, db, block.length - db);
            xorMask(block, db, block.length - db, 1, h);
            return delegate.doFinal(block);
        } finally {
            Arrays.fill(seed, (byte) 0);
            Arrays.fill(block, (byte) 0);
        }
    }

    private byte[] decodeOaep() throws BadPaddingException {
        byte[] block;
        try {
            block = delegate.doFinal(buffer, 0, used);
        } catch (IllegalBlockSizeException | BadPaddingException invalid) {
            throw invalidOaep();
        }
        int h = labelHash.length;
        int db = 1 + h;
        try {
            xorMask(block, db, block.length - db, 1, h);
            xorMask(block, 1, h, db, block.length - db);
            int bad = block[0] & 0xff;
            for (int i = 0; i < h; ++i) bad |= (block[db + i] ^ labelHash[i]) & 0xff;
            int waiting = 1;
            int message = 0;
            for (int i = db + h; i < block.length; ++i) {
                int value = block[i] & 0xff;
                int zero = (value - 1) >>> 31;
                int one = ((value ^ 1) - 1) >>> 31;
                bad |= waiting & (1 ^ (zero | one));
                int match = -(waiting & one);
                message = (message & ~match) | ((i + 1) & match);
                waiting &= 1 ^ one;
            }
            if ((bad | waiting) != 0) throw invalidOaep();
            return Arrays.copyOfRange(block, message, block.length);
        } finally {
            Arrays.fill(block, (byte) 0);
        }
    }

    private void xorMask(byte[] block, int seedOffset, int seedLength, int target, int count) {
        byte[] counter = new byte[4];
        for (int index = 0; count > 0; ++index) {
            counter[0] = (byte) (index >>> 24);
            counter[1] = (byte) (index >>> 16);
            counter[2] = (byte) (index >>> 8);
            counter[3] = (byte) index;
            mgfDigest.update(block, seedOffset, seedLength);
            byte[] mask = mgfDigest.digest(counter);
            int chunk = Math.min(count, mask.length);
            for (int i = 0; i < chunk; ++i) block[target + i] ^= mask[i];
            Arrays.fill(mask, (byte) 0);
            target += chunk;
            count -= chunk;
        }
    }

    private static BadPaddingException invalidOaep() {
        return new BadPaddingException("OAEP decoding error");
    }
}
