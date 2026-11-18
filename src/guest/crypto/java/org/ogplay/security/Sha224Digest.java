package org.ogplay.security;

import com.android.org.conscrypt.NativeCrypto;
import java.security.MessageDigestSpi;

/** SHA-224 for OAEP, using the existing Conscrypt digest ABI. */
public final class Sha224Digest extends MessageDigestSpi implements Cloneable {
    private static final long ALGORITHM = NativeCrypto.EVP_get_digestbyname("sha224");
    private long ctx;

    private long context() {
        if (ctx == 0) ctx = NativeCrypto.EVP_DigestInit(ALGORITHM);
        return ctx;
    }

    protected int engineGetDigestLength() { return 28; }

    protected void engineUpdate(byte input) {
        engineUpdate(new byte[] { input }, 0, 1);
    }

    protected void engineUpdate(byte[] input, int offset, int count) {
        NativeCrypto.EVP_DigestUpdate(context(), input, offset, count);
    }

    protected byte[] engineDigest() {
        byte[] result = new byte[28];
        NativeCrypto.EVP_DigestFinal(context(), result, 0);
        ctx = 0;
        return result;
    }

    protected void engineReset() {
        if (ctx != 0) {
            NativeCrypto.EVP_MD_CTX_destroy(ctx);
            ctx = 0;
        }
    }

    public Object clone() throws CloneNotSupportedException {
        Sha224Digest result = (Sha224Digest) super.clone();
        if (ctx != 0) result.ctx = NativeCrypto.EVP_MD_CTX_copy(ctx);
        return result;
    }
}
