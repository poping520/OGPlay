package org.ogplay.security;

import java.io.ByteArrayOutputStream;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;

/** Private path-validation primitives over guest libcrypto. */
final class NativeTrust {
    private NativeTrust() {}

    static native int subjectHashOld(byte[] der) throws CertificateException;

    /** Same JNI shape as NativeVerification.verify: three byte[] plus a string. */
    static native boolean verifyPath(byte[] leaf, byte[] packedExtras, byte[] packedAnchors,
                                     String purpose) throws CertificateException;

    /** Host overlay copies Harmony's cached DER; this is the BootDex fallback. */
    static byte[] encodedCopy(X509Certificate cert) throws CertificateException {
        if (cert == null) {
            throw new CertificateException("certificate encoding is missing");
        }
        byte[] raw = cert.getEncoded();
        if (raw == null || raw.length == 0) {
            throw new CertificateException("certificate encoding is missing");
        }
        byte[] copy = new byte[raw.length];
        System.arraycopy(raw, 0, copy, 0, raw.length);
        return copy;
    }

    static byte[] emptyPacked() {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        writeInt(out, 0);
        return out.toByteArray();
    }

    static void writeInt(ByteArrayOutputStream out, int value) {
        out.write(value);
        out.write(value >> 8);
        out.write(value >> 16);
        out.write(value >> 24);
    }

    static int readInt(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff)
                | ((bytes[offset + 1] & 0xff) << 8)
                | ((bytes[offset + 2] & 0xff) << 16)
                | ((bytes[offset + 3] & 0xff) << 24);
    }
}
