package org.ogplay.security;

import java.security.AlgorithmParameters;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Arrays;
import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.CipherSpi;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import javax.crypto.ShortBufferException;

/** AES/CBC ZeroBytePadding adapter over the existing guest OpenSSL NoPadding cipher. */
public final class AesCbcZeroBytePadding extends CipherSpi {
    private static final int BLOCK_SIZE = 16;

    private Cipher cipher;
    private boolean encrypting;
    private byte[] buffered = new byte[0];

    public AesCbcZeroBytePadding() {
    }

    protected void engineSetMode(String mode) throws NoSuchAlgorithmException {
        if (!"CBC".equalsIgnoreCase(mode)) {
            throw new NoSuchAlgorithmException("ZeroBytePadding AES only supports CBC");
        }
    }

    protected void engineSetPadding(String padding) throws NoSuchPaddingException {
        if (!"ZeroBytePadding".equalsIgnoreCase(padding)) {
            throw new NoSuchPaddingException("Only ZeroBytePadding is supported");
        }
    }

    protected int engineGetBlockSize() {
        return BLOCK_SIZE;
    }

    protected int engineGetOutputSize(int inputLen) {
        int total = buffered.length + inputLen;
        if (!encrypting || total == 0 || total % BLOCK_SIZE == 0) {
            return total;
        }
        return total + BLOCK_SIZE - total % BLOCK_SIZE;
    }

    protected byte[] engineGetIV() {
        return cipher == null ? null : cipher.getIV();
    }

    protected AlgorithmParameters engineGetParameters() {
        return cipher == null ? null : cipher.getParameters();
    }

    protected void engineInit(int opmode, Key key, SecureRandom random)
            throws InvalidKeyException {
        try {
            initCipher(opmode, key, null, null, random);
        } catch (InvalidAlgorithmParameterException e) {
            InvalidKeyException wrapped = new InvalidKeyException(e.getMessage());
            wrapped.initCause(e);
            throw wrapped;
        }
    }

    protected void engineInit(int opmode, Key key, AlgorithmParameterSpec params,
            SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        initCipher(opmode, key, params, null, random);
    }

    protected void engineInit(int opmode, Key key, AlgorithmParameters params,
            SecureRandom random) throws InvalidKeyException, InvalidAlgorithmParameterException {
        initCipher(opmode, key, null, params, random);
    }

    private void initCipher(int opmode, Key key, AlgorithmParameterSpec spec,
            AlgorithmParameters parameters, SecureRandom random)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (opmode != Cipher.ENCRYPT_MODE && opmode != Cipher.DECRYPT_MODE
                && opmode != Cipher.WRAP_MODE && opmode != Cipher.UNWRAP_MODE) {
            throw new InvalidKeyException("Unsupported opmode " + opmode);
        }
        try {
            cipher = Cipher.getInstance("AES/CBC/NoPadding");
        } catch (NoSuchAlgorithmException e) {
            throw new InvalidKeyException(e);
        } catch (NoSuchPaddingException e) {
            throw new InvalidKeyException(e);
        }
        if (spec != null) {
            cipher.init(opmode, key, spec, random);
        } else if (parameters != null) {
            cipher.init(opmode, key, parameters, random);
        } else {
            cipher.init(opmode, key, random);
        }
        encrypting = opmode == Cipher.ENCRYPT_MODE || opmode == Cipher.WRAP_MODE;
        buffered = new byte[0];
    }

    protected byte[] engineUpdate(byte[] input, int inputOffset, int inputLen) {
        append(input, inputOffset, inputLen);
        return new byte[0];
    }

    protected int engineUpdate(byte[] input, int inputOffset, int inputLen,
            byte[] output, int outputOffset) throws ShortBufferException {
        requireOutput(output, outputOffset, 0);
        append(input, inputOffset, inputLen);
        return 0;
    }

    protected byte[] engineDoFinal(byte[] input, int inputOffset, int inputLen)
            throws IllegalBlockSizeException, BadPaddingException {
        if (input == null) {
            if (inputOffset != 0 || inputLen != 0) {
                throw new NullPointerException("input == null");
            }
        } else {
            append(input, inputOffset, inputLen);
        }
        byte[] source = buffered;
        buffered = new byte[0];
        if (encrypting && source.length % BLOCK_SIZE != 0) {
            source = Arrays.copyOf(source,
                    source.length + BLOCK_SIZE - source.length % BLOCK_SIZE);
        }
        byte[] result = cipher.doFinal(source);
        if (result == null) {
            result = new byte[0];
        }
        if (!encrypting) {
            int length = result.length;
            while (length > 0 && result[length - 1] == 0) {
                --length;
            }
            if (length != result.length) {
                result = Arrays.copyOf(result, length);
            }
        }
        return result;
    }

    protected int engineDoFinal(byte[] input, int inputOffset, int inputLen,
            byte[] output, int outputOffset) throws ShortBufferException,
            IllegalBlockSizeException, BadPaddingException {
        int required = engineGetOutputSize(inputLen);
        requireOutput(output, outputOffset, required);
        byte[] result = engineDoFinal(input, inputOffset, inputLen);
        System.arraycopy(result, 0, output, outputOffset, result.length);
        return result.length;
    }

    private void append(byte[] input, int offset, int length) {
        if (input == null) {
            throw new NullPointerException("input == null");
        }
        if (offset < 0 || length < 0 || offset > input.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        if (length == 0) {
            return;
        }
        int oldLength = buffered.length;
        buffered = Arrays.copyOf(buffered, oldLength + length);
        System.arraycopy(input, offset, buffered, oldLength, length);
    }

    private static void requireOutput(byte[] output, int offset, int required)
            throws ShortBufferException {
        if (output == null) {
            throw new NullPointerException("output == null");
        }
        if (offset < 0 || offset > output.length) {
            throw new ArrayIndexOutOfBoundsException(offset);
        }
        if (output.length - offset < required) {
            throw new ShortBufferException("output buffer too short");
        }
    }
}
