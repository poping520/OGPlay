package org.ogplay.security;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** PKCS#12 v1.0 Appendix B derivation (not PBKDF2). */
final class Pkcs12Kdf {
    static final int PURPOSE_MAC = 3;
    static final int PURPOSE_KEY = 1;
    static final int PURPOSE_IV = 2;
    private static final int U = 20;
    private static final int V = 64;

    private Pkcs12Kdf() {}

    static byte[] passwordBytes(char[] password) {
        if (password == null || password.length == 0) return new byte[0];
        byte[] result = new byte[(password.length + 1) * 2];
        for (int i = 0; i < password.length; ++i) {
            result[i * 2] = (byte) (password[i] >>> 8);
            result[i * 2 + 1] = (byte) password[i];
        }
        return result;
    }

    static byte[] derive(char[] password, byte[] salt, int iterations,
            int purpose, int length) throws NoSuchAlgorithmException {
        return derive(password, salt, iterations, purpose, length, false);
    }

    static byte[] deriveOld(char[] password, byte[] salt, int iterations,
            int purpose, int length) throws NoSuchAlgorithmException {
        return derive(password, salt, iterations, purpose, length, true);
    }

    private static byte[] derive(char[] password, byte[] salt, int iterations,
            int purpose, int length, boolean oldBrokenMixer) throws NoSuchAlgorithmException {
        if (salt == null || iterations < 1 || iterations > BksLimits.MAX_ITERATIONS ||
                purpose < 1 || purpose > 3 || length < 0 || length > 1024) {
            throw new IllegalArgumentException("invalid PKCS12 KDF parameters");
        }
        byte[] p = passwordBytes(password);
        byte[] d = new byte[V];
        for (int i = 0; i < d.length; ++i) d[i] = (byte) purpose;
        int sLength = salt.length == 0 ? 0 : V * ((salt.length + V - 1) / V);
        int pLength = p.length == 0 ? 0 : V * ((p.length + V - 1) / V);
        byte[] input = new byte[sLength + pLength];
        for (int i = 0; i < sLength; ++i) input[i] = salt[i % salt.length];
        for (int i = 0; i < pLength; ++i) input[sLength + i] = p[i % p.length];
        byte[] result = new byte[length];
        MessageDigest digest = MessageDigest.getInstance("SHA-1");
        int blocks = (length + U - 1) / U;
        byte[] b = new byte[V];
        for (int block = 0; block < blocks; ++block) {
            digest.update(d);
            byte[] a = digest.digest(input);
            for (int round = 1; round < iterations; ++round) a = digest.digest(a);
            if (oldBrokenMixer) {
                // Historical BC v0 bug: the loop repeatedly assigned B[block + 1]
                // instead of B[index]. Required only for old sealed-key recovery.
                int oldIndex = block + 1;
                for (int i = 0; i < b.length; ++i) b[oldIndex] = a[i % a.length];
            } else {
                for (int i = 0; i < b.length; ++i) b[i] = a[i % a.length];
            }
            for (int offset = 0; offset < input.length; offset += V) adjust(input, offset, b);
            int count = Math.min(a.length, length - block * U);
            System.arraycopy(a, 0, result, block * U, count);
        }
        for (int i = 0; i < p.length; ++i) p[i] = 0;
        for (int i = 0; i < input.length; ++i) input[i] = 0;
        return result;
    }

    private static void adjust(byte[] target, int offset, byte[] addend) {
        int carry = 1;
        for (int i = addend.length - 1; i >= 0; --i) {
            int sum = (target[offset + i] & 0xff) + (addend[i] & 0xff) + carry;
            target[offset + i] = (byte) sum;
            carry = sum >>> 8;
        }
    }
}
