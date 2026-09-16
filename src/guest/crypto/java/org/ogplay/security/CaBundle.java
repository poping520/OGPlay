package org.ogplay.security;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;

/** Versioned read-only CA snapshot loaded from the injected VFS pack. */
final class CaBundle {
    static final int VERSION = 1;
    private static final byte[] MAGIC = new byte[] {'O', 'G', 'P', 'L', 'A', 'Y', 'C', 'A'};

    final int version;
    final long createdMillis;
    final String source;
    final String license;
    final String[] aliases;
    final Date[] dates;
    final X509Certificate[] certificates;

    private CaBundle(int version, long createdMillis, String source, String license,
                     String[] aliases, Date[] dates, X509Certificate[] certificates) {
        this.version = version;
        this.createdMillis = createdMillis;
        this.source = source;
        this.license = license;
        this.aliases = aliases;
        this.dates = dates;
        this.certificates = certificates;
    }

    static CaBundle loadDefault() throws IOException, CertificateException {
        return loadPath(TrustLimits.DEFAULT_CA_PATH);
    }

    static CaBundle loadPath(String path) throws IOException, CertificateException {
        FileInputStream file = new FileInputStream(path);
        try {
            byte[] bytes = readAll(file, TrustLimits.MAX_BUNDLE_BYTES);
            return parse(bytes);
        } finally {
            file.close();
        }
    }

    static CaBundle parse(byte[] bytes) throws IOException, CertificateException {
        if (bytes == null || bytes.length < 8 + 4 + 8 + 2 + 2 + 4 + 32) {
            throw new IOException("CA bundle is truncated");
        }
        if (bytes.length > TrustLimits.MAX_BUNDLE_BYTES) {
            throw new IOException("CA bundle exceeds size limit");
        }
        int payloadEnd = bytes.length - 32;
        byte[] digest;
        try {
            digest = MessageDigest.getInstance("SHA-256").digest(
                    Arrays.copyOf(bytes, payloadEnd));
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 is unavailable for CA bundle", e);
        }
        byte[] expected = Arrays.copyOfRange(bytes, payloadEnd, bytes.length);
        if (!Arrays.equals(digest, expected)) {
            throw new IOException("CA bundle hash mismatch");
        }
        DataInputStream in = new DataInputStream(new ByteArrayInputStream(bytes, 0, payloadEnd));
        byte[] magic = new byte[8];
        in.readFully(magic);
        if (!Arrays.equals(magic, MAGIC)) {
            throw new IOException("CA bundle magic is invalid");
        }
        int version = in.readInt();
        if (version != VERSION) {
            throw new IOException("unsupported CA bundle version");
        }
        long created = in.readLong();
        String source = readUtf16(in, 1024);
        String license = readUtf16(in, 4096);
        int count = in.readInt();
        if (count < 0 || count > TrustLimits.MAX_CA_CERTS) {
            throw new IOException("CA bundle entry count is invalid");
        }
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        ArrayList aliases = new ArrayList(count);
        ArrayList dates = new ArrayList(count);
        ArrayList certs = new ArrayList(count);
        for (int i = 0; i < count; i++) {
            String alias = readUtf16(in, TrustLimits.MAX_ALIAS);
            if (alias.length() == 0 || aliases.contains(alias)) {
                throw new IOException("CA bundle alias is missing or duplicated");
            }
            long date = in.readLong();
            int derLen = in.readInt();
            if (derLen <= 0 || derLen > TrustLimits.MAX_CERT_DER) {
                throw new IOException("CA certificate exceeds size limit");
            }
            byte[] der = new byte[derLen];
            in.readFully(der);
            X509Certificate cert = (X509Certificate) factory.generateCertificate(
                    new ByteArrayInputStream(der));
            aliases.add(alias);
            dates.add(new Date(date));
            certs.add(cert);
        }
        if (in.available() != 0) {
            throw new IOException("CA bundle has trailing payload");
        }
        return new CaBundle(version, created, source, license,
                (String[]) aliases.toArray(new String[aliases.size()]),
                (Date[]) dates.toArray(new Date[dates.size()]),
                (X509Certificate[]) certs.toArray(new X509Certificate[certs.size()]));
    }

    static byte[] encode(long createdMillis, String source, String license,
                         String[] aliases, long[] dates, byte[][] ders) throws IOException {
        java.io.ByteArrayOutputStream raw = new java.io.ByteArrayOutputStream();
        java.io.DataOutputStream out = new java.io.DataOutputStream(raw);
        out.write(MAGIC);
        out.writeInt(VERSION);
        out.writeLong(createdMillis);
        writeUtf16(out, source == null ? "" : source);
        writeUtf16(out, license == null ? "" : license);
        if (aliases == null || dates == null || ders == null ||
                aliases.length != dates.length || aliases.length != ders.length) {
            throw new IOException("CA bundle entries are inconsistent");
        }
        if (aliases.length > TrustLimits.MAX_CA_CERTS) {
            throw new IOException("CA bundle entry count exceeds limit");
        }
        out.writeInt(aliases.length);
        for (int i = 0; i < aliases.length; i++) {
            writeUtf16(out, aliases[i]);
            out.writeLong(dates[i]);
            if (ders[i] == null || ders[i].length == 0 || ders[i].length > TrustLimits.MAX_CERT_DER) {
                throw new IOException("CA certificate encoding is invalid");
            }
            out.writeInt(ders[i].length);
            out.write(ders[i]);
        }
        out.flush();
        byte[] payload = raw.toByteArray();
        byte[] digest;
        try {
            digest = MessageDigest.getInstance("SHA-256").digest(payload);
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 is unavailable for CA bundle", e);
        }
        byte[] complete = new byte[payload.length + digest.length];
        System.arraycopy(payload, 0, complete, 0, payload.length);
        System.arraycopy(digest, 0, complete, payload.length, digest.length);
        return complete;
    }

    private static String readUtf16(DataInputStream in, int maxChars) throws IOException {
        int length = in.readUnsignedShort();
        if (length > maxChars) {
            throw new IOException("CA bundle string exceeds limit");
        }
        char[] chars = new char[length];
        for (int i = 0; i < length; i++) {
            chars[i] = in.readChar();
        }
        return new String(chars);
    }

    private static void writeUtf16(java.io.DataOutputStream out, String value) throws IOException {
        if (value.length() > 0xffff) {
            throw new IOException("CA bundle string exceeds limit");
        }
        out.writeShort(value.length());
        out.writeChars(value);
    }

    private static byte[] readAll(FileInputStream file, int maximum) throws IOException {
        byte[] buffer = new byte[Math.min(maximum, 8192)];
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        int n;
        while ((n = file.read(buffer)) >= 0) {
            if (out.size() > maximum - n) {
                throw new IOException("CA bundle exceeds size limit");
            }
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }
}
