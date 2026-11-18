package org.ogplay.security;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.security.AlgorithmParametersSpi;
import java.security.InvalidAlgorithmParameterException;
import java.security.spec.AlgorithmParameterSpec;
import java.security.spec.InvalidParameterSpecException;
import java.security.spec.MGF1ParameterSpec;
import java.util.Arrays;
import javax.crypto.spec.OAEPParameterSpec;
import javax.crypto.spec.PSource;

/** DER RSAES-OAEP-params (PKCS #1). Defaults are omitted when encoded. */
public final class OaepParameters extends AlgorithmParametersSpi {
    private static final String[] HASHES = { "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512", "MD5" };
    private static final String[] HASH_OIDS = {
        "1.3.14.3.2.26", "2.16.840.1.101.3.4.2.4", "2.16.840.1.101.3.4.2.1",
        "2.16.840.1.101.3.4.2.2", "2.16.840.1.101.3.4.2.3", "1.2.840.113549.2.5"
    };
    private static final byte[][] HASH_DER = {
        hex("2b0e03021a"), hex("608648016503040204"), hex("608648016503040201"),
        hex("608648016503040202"), hex("608648016503040203"), hex("2a864886f70d0205")
    };
    private static final byte[] MGF1 = hex("2a864886f70d010108");
    private static final byte[] PSPECIFIED = hex("2a864886f70d010109");
    private static final int MAX_LABEL = 1024 * 1024;

    private OAEPParameterSpec spec;

    private static int hashIndex(String name) {
        for (int i = 0; i < HASHES.length; ++i) {
            if (HASHES[i].equalsIgnoreCase(name) || HASHES[i].replace("-", "").equalsIgnoreCase(name) ||
                    HASH_OIDS[i].equals(name)) return i;
        }
        return -1;
    }

    static String digestName(String name) throws InvalidAlgorithmParameterException {
        int index = hashIndex(name);
        if (index < 0) throw new InvalidAlgorithmParameterException("unsupported OAEP digest " + name);
        return HASHES[index];
    }

    static void validate(OAEPParameterSpec input) throws InvalidAlgorithmParameterException {
        if (!"MGF1".equalsIgnoreCase(input.getMGFAlgorithm()) &&
                !"1.2.840.113549.1.1.8".equals(input.getMGFAlgorithm())) {
            throw new InvalidAlgorithmParameterException("OAEP requires MGF1");
        }
        if (!(input.getMGFParameters() instanceof MGF1ParameterSpec) ||
                !(input.getPSource() instanceof PSource.PSpecified)) {
            throw new InvalidAlgorithmParameterException("OAEP requires MGF1ParameterSpec and PSpecified");
        }
        digestName(input.getDigestAlgorithm());
        digestName(((MGF1ParameterSpec) input.getMGFParameters()).getDigestAlgorithm());
        if (((PSource.PSpecified) input.getPSource()).getValue().length > MAX_LABEL) {
            throw new InvalidAlgorithmParameterException("OAEP label exceeds 1 MiB");
        }
    }

    protected void engineInit(AlgorithmParameterSpec input) throws InvalidParameterSpecException {
        if (!(input instanceof OAEPParameterSpec)) throw new InvalidParameterSpecException("Expected OAEPParameterSpec");
        try {
            validate((OAEPParameterSpec) input);
            spec = (OAEPParameterSpec) input;
        } catch (InvalidAlgorithmParameterException e) {
            throw new InvalidParameterSpecException(e.getMessage());
        }
    }

    protected <T extends AlgorithmParameterSpec> T engineGetParameterSpec(Class<T> type)
            throws InvalidParameterSpecException {
        if (!type.isAssignableFrom(OAEPParameterSpec.class)) {
            throw new InvalidParameterSpecException("Expected OAEPParameterSpec");
        }
        return type.cast(spec);
    }

    protected byte[] engineGetEncoded() {
        ByteArrayOutputStream values = new ByteArrayOutputStream();
        int hash = hashIndex(spec.getDigestAlgorithm());
        int mask = hashIndex(((MGF1ParameterSpec) spec.getMGFParameters()).getDigestAlgorithm());
        byte[] label = ((PSource.PSpecified) spec.getPSource()).getValue();
        if (hash != 0) write(values, tag(0xa0, digestIdentifier(hash)));
        if (mask != 0) write(values, tag(0xa1, identifier(MGF1, digestIdentifier(mask))));
        if (label.length != 0) write(values, tag(0xa2, identifier(PSPECIFIED, tag(4, label))));
        return tag(0x30, values.toByteArray());
    }

    protected byte[] engineGetEncoded(String format) throws IOException {
        checkFormat(format);
        return engineGetEncoded();
    }

    protected void engineInit(byte[] encoded) throws IOException {
        if (encoded.length > MAX_LABEL + 128) throw new IOException("OAEP parameters exceed limit");
        DerReader top = new DerReader(encoded, 0, encoded.length);
        DerReader seq = top.read(0x30);
        top.end();
        String hash = "SHA-1";
        String mask = "SHA-1";
        byte[] label = new byte[0];
        if (seq.has(0xa0)) {
            DerReader field = seq.read(0xa0);
            hash = readDigest(field);
            field.end();
        }
        if (seq.has(0xa1)) {
            DerReader field = seq.read(0xa1);
            DerReader alg = field.read(0x30);
            if (!Arrays.equals(alg.read(6).bytes(), MGF1)) throw new IOException("OAEP requires MGF1");
            mask = readDigest(alg);
            alg.end();
            field.end();
        }
        if (seq.has(0xa2)) {
            DerReader field = seq.read(0xa2);
            DerReader alg = field.read(0x30);
            if (!Arrays.equals(alg.read(6).bytes(), PSPECIFIED)) throw new IOException("OAEP requires PSpecified");
            label = alg.read(4).bytes();
            if (label.length > MAX_LABEL) throw new IOException("OAEP label exceeds 1 MiB");
            alg.end();
            field.end();
        }
        seq.end();
        spec = new OAEPParameterSpec(hash, "MGF1", new MGF1ParameterSpec(mask), new PSource.PSpecified(label));
    }

    protected void engineInit(byte[] encoded, String format) throws IOException {
        checkFormat(format);
        engineInit(encoded);
    }

    protected String engineToString() { return "OAEP Parameters"; }

    private static void checkFormat(String format) throws IOException {
        if (format != null && !"ASN.1".equalsIgnoreCase(format) && !"X.509".equalsIgnoreCase(format)) {
            throw new IOException("unsupported OAEP parameters format " + format);
        }
    }

    private static String readDigest(DerReader source) throws IOException {
        DerReader alg = source.read(0x30);
        byte[] oid = alg.read(6).bytes();
        if (alg.has(5)) alg.read(5).end();
        alg.end();
        for (int i = 0; i < HASH_DER.length; ++i) {
            if (Arrays.equals(oid, HASH_DER[i])) return HASHES[i];
        }
        throw new IOException("unsupported OAEP digest OID");
    }

    private static byte[] digestIdentifier(int hash) {
        return identifier(HASH_DER[hash], new byte[] { 5, 0 });
    }

    private static byte[] identifier(byte[] oid, byte[] parameters) {
        ByteArrayOutputStream values = new ByteArrayOutputStream();
        write(values, tag(6, oid));
        write(values, parameters);
        return tag(0x30, values.toByteArray());
    }

    private static byte[] tag(int tag, byte[] bytes) {
        ByteArrayOutputStream result = new ByteArrayOutputStream();
        result.write(tag);
        int size = bytes.length;
        if (size < 128) {
            result.write(size);
        } else {
            int octets = (32 - Integer.numberOfLeadingZeros(size) + 7) / 8;
            result.write(128 | octets);
            for (int i = octets - 1; i >= 0; --i) result.write(size >>> (8 * i));
        }
        write(result, bytes);
        return result.toByteArray();
    }

    private static void write(ByteArrayOutputStream output, byte[] bytes) {
        output.write(bytes, 0, bytes.length);
    }

    private static byte[] hex(String text) {
        byte[] result = new byte[text.length() / 2];
        for (int i = 0; i < result.length; ++i) result[i] = (byte) Integer.parseInt(text.substring(2 * i, 2 * i + 2), 16);
        return result;
    }
}
