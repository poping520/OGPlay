package org.ogplay.security;

import java.io.IOException;
import java.security.AlgorithmParametersSpi;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.InvalidParameterSpecException;
import javax.crypto.spec.IvParameterSpec;

/** API 19 BC AES IV parameters. */
public final class AesParameters extends AlgorithmParametersSpi {
    private byte[] iv;

    @Override protected void engineInit(AlgorithmParameterSpec spec) throws InvalidParameterSpecException {
        if (!(spec instanceof IvParameterSpec)) {
            throw new InvalidParameterSpecException("IvParameterSpec required to initialise a IV parameters algorithm parameters object");
        }
        iv = ((IvParameterSpec) spec).getIV();
    }

    @Override protected <T extends AlgorithmParameterSpec> T engineGetParameterSpec(Class<T> type)
            throws InvalidParameterSpecException {
        if (type == null) throw new NullPointerException("argument to getParameterSpec must not be null");
        if (type != IvParameterSpec.class) {
            throw new InvalidParameterSpecException("unknown parameter spec passed to IV parameters object.");
        }
        return type.cast(new IvParameterSpec(iv));
    }

    @Override protected void engineInit(byte[] encoded) throws IOException {
        // BC also accepts raw IVs, recognizing short DER by its tag and length.
        if (encoded.length >= 2 && encoded.length % 8 != 0 && encoded[0] == 4 &&
                encoded[1] == encoded.length - 2) {
            iv = decodeOctets(encoded);
        } else {
            iv = encoded.clone();
        }
    }

    @Override protected void engineInit(byte[] encoded, String format) throws IOException {
        if (isAsn1(format)) {
            engineInit(decodeOctets(encoded));
        } else if ("RAW".equals(format)) {
            engineInit(encoded);
        } else {
            throw new IOException("Unknown parameters format in IV parameters object");
        }
    }

    @Override protected byte[] engineGetEncoded() {
        int length = iv.length;
        int octets = length < 128 ? 0 : (32 - Integer.numberOfLeadingZeros(length) + 7) / 8;
        int header = 2 + octets;
        byte[] encoded = new byte[header + length];
        encoded[0] = 4;
        encoded[1] = (byte) (octets == 0 ? length : 128 | octets);
        for (int i = 0; i < octets; ++i) encoded[header - 1 - i] = (byte) (length >>> (8 * i));
        System.arraycopy(iv, 0, encoded, header, length);
        return encoded;
    }

    @Override protected byte[] engineGetEncoded(String format) {
        if (isAsn1(format)) return engineGetEncoded();
        if ("RAW".equals(format)) return iv.clone();
        return null;
    }

    @Override protected String engineToString() { return "AES IV"; }

    private static boolean isAsn1(String format) { return format == null || "ASN.1".equals(format); }

    private static byte[] decodeOctets(byte[] encoded) throws IOException {
        DerReader input = new DerReader(encoded, 0, encoded.length);
        DerReader value = input.read(4);
        input.end();
        return value.bytes();
    }
}
