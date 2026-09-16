package org.ogplay.security;

/** Private native primitives required by the BKS format. */
final class NativeKeyStoreCrypto {
    private NativeKeyStoreCrypto() {}

    static native byte[] desEdeCbc(boolean encrypt, byte[] key, byte[] iv, byte[] input)
            throws javax.crypto.BadPaddingException;
    static native long decodePrivateKey(byte[] pkcs8);
    static native byte[] encodePrivateKey(long token);
    static native int privateKeyType(long token);
    static native void freePrivateKey(long token);
}
