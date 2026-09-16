package org.ogplay.security;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.security.Key;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.KeyStoreException;
import java.security.KeyStoreSpi;
import java.security.NoSuchAlgorithmException;
import java.security.UnrecoverableKeyException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.SecureRandom;
import java.util.Date;
import java.util.Enumeration;
import java.util.Hashtable;
import javax.crypto.Mac;
import javax.crypto.SecretKey;
import javax.crypto.BadPaddingException;
import javax.crypto.spec.SecretKeySpec;

/**
 * Guest-owned BKS state. The format boundary intentionally remains unavailable until
 * the authenticated codec is installed; this class is not registered globally yet.
 */
public final class BksKeyStoreSpi extends KeyStoreSpi {
    private static final int STORE_VERSION = 2;
    private static final int MAC_SIZE = 20;
    private static final int TYPE_CERTIFICATE = 1;
    private static final int TYPE_KEY = 2;
    private static final int TYPE_SECRET = 3;
    private static final int TYPE_SEALED = 4;

    private Hashtable<String, StoreEntry> entries = new Hashtable<String, StoreEntry>();

    private static final class LimitedOutputStream extends OutputStream {
        private final OutputStream output;
        private final int maximum;
        private int count;

        LimitedOutputStream(OutputStream output, int maximum) {
            this.output = output;
            this.maximum = maximum;
        }

        private void reserve(int length) throws IOException {
            if (length < 0 || count > maximum - length) {
                throw new IOException("BKS store exceeds limit");
            }
            count += length;
        }

        public void write(int value) throws IOException {
            reserve(1);
            output.write(value);
        }

        public void write(byte[] bytes, int offset, int length) throws IOException {
            reserve(length);
            output.write(bytes, offset, length);
        }
    }

    private static final class StoreEntry {
        final int type;
        final Date date;
        final Object value;
        final Certificate[] chain;

        StoreEntry(int type, Object value, Certificate[] chain) {
            this.type = type;
            this.date = new Date(System.currentTimeMillis());
            this.value = value;
            this.chain = copyChain(chain);
        }
    }

    private static Certificate[] copyChain(Certificate[] chain) {
        return chain == null ? null : chain.clone();
    }

    private static void checkLength(int value, int maximum, String field) throws IOException {
        if (value < 0 || value > maximum) throw new IOException("invalid BKS " + field);
    }

    private static byte[] readBlob(DataInputStream input, String field) throws IOException {
        int length = input.readInt();
        checkLength(length, BksLimits.MAX_BLOB_BYTES, field);
        byte[] result = new byte[length];
        input.readFully(result);
        return result;
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int total = 0;
        for (;;) {
            int count = stream.read(buffer);
            if (count == -1) break;
            if (count == 0) continue;
            total += count;
            if (total > BksLimits.MAX_STORE_BYTES) throw new IOException("BKS store exceeds limit");
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    private static byte[] mac(char[] password, byte[] salt, int iterations,
            int version, byte[] data, int offset, int length)
            throws NoSuchAlgorithmException {
        int keyLength = version == 2 ? MAC_SIZE : 2;
        byte[] key = Pkcs12Kdf.derive(
            password, salt, iterations, Pkcs12Kdf.PURPOSE_MAC, keyLength);
        Mac mac = Mac.getInstance("HmacSHA1");
        try {
            mac.init(new SecretKeySpec(key, "HmacSHA1"));
        } catch (java.security.InvalidKeyException exception) {
            throw new IllegalArgumentException(exception);
        }
        mac.update(data, offset, length);
        byte[] result = mac.doFinal();
        for (int i = 0; i < key.length; ++i) key[i] = 0;
        return result;
    }

    private static boolean equalBytes(byte[] first, byte[] second) {
        int different = first.length ^ second.length;
        int count = first.length > second.length ? first.length : second.length;
        for (int i = 0; i < count; ++i) {
            int a = i < first.length ? first[i] : 0;
            int b = i < second.length ? second[i] : 0;
            different |= a ^ b;
        }
        return different == 0;
    }

    private static void writeCertificate(DataOutputStream output, Certificate certificate)
            throws IOException, CertificateException {
        output.writeUTF(certificate.getType());
        byte[] encoded = certificate.getEncoded();
        checkLength(encoded.length, BksLimits.MAX_BLOB_BYTES, "certificate length");
        output.writeInt(encoded.length);
        output.write(encoded);
    }

    private static Certificate readCertificate(DataInputStream input)
            throws IOException, CertificateException {
        String type = input.readUTF();
        byte[] encoded = readBlob(input, "certificate length");
        return CertificateFactory.getInstance(type).generateCertificate(
            new ByteArrayInputStream(encoded));
    }

    private static void writeChain(DataOutputStream output, Certificate[] chain)
            throws IOException, CertificateException {
        int length = chain == null ? 0 : chain.length;
        checkLength(length, BksLimits.MAX_CHAIN_LENGTH, "chain length");
        output.writeInt(length);
        for (int i = 0; i < length; ++i) writeCertificate(output, chain[i]);
    }

    private static Certificate[] readChain(DataInputStream input)
            throws IOException, CertificateException {
        int length = input.readInt();
        checkLength(length, BksLimits.MAX_CHAIN_LENGTH, "chain length");
        if (length == 0) return null;
        Certificate[] chain = new Certificate[length];
        for (int i = 0; i < length; ++i) chain[i] = readCertificate(input);
        return chain;
    }

    private static void encodeKey(Key key, DataOutputStream output) throws IOException {
        int kind = key instanceof PrivateKey ? 0 : key instanceof PublicKey ? 1 : 2;
        String format = key.getFormat();
        String algorithm = key.getAlgorithm();
        byte[] encoded = key.getEncoded();
        if (format == null || algorithm == null || encoded == null) {
            throw new IOException("key is not encodable");
        }
        checkLength(encoded.length, BksLimits.MAX_BLOB_BYTES, "key length");
        output.writeByte(kind);
        output.writeUTF(format);
        output.writeUTF(algorithm);
        output.writeInt(encoded.length);
        output.write(encoded);
    }

    private static Key decodeKey(DataInputStream input) throws IOException {
        int kind = input.readUnsignedByte();
        String format = input.readUTF();
        String algorithm = input.readUTF();
        byte[] encoded = readBlob(input, "key length");
        if (kind == 0 && "PKCS#8".equals(format) &&
                ("RSA".equals(algorithm) || "EC".equals(algorithm))) {
            try { return new BksPrivateKey(algorithm, encoded); }
            catch (RuntimeException exception) { throw new IOException("invalid private key", exception); }
        }
        if (kind == 2 && "RAW".equals(format)) return new SecretKeySpec(encoded, algorithm);
        throw new IOException("unsupported BKS key: " + kind + "/" + format + "/" + algorithm);
    }

    private static byte[] protectKey(Key key, char[] password)
            throws IOException, NoSuchAlgorithmException {
        byte[] salt = new byte[20];
        SecureRandom random = new SecureRandom();
        random.nextBytes(salt);
        int iterations = 1024 + (random.nextInt() & 1023);
        ByteArrayOutputStream plainBytes = new ByteArrayOutputStream();
        DataOutputStream plain = new DataOutputStream(plainBytes);
        encodeKey(key, plain);
        plain.flush();
        byte[] cipherKey = Pkcs12Kdf.derive(password, salt, iterations,
            Pkcs12Kdf.PURPOSE_KEY, 24);
        byte[] iv = Pkcs12Kdf.derive(password, salt, iterations,
            Pkcs12Kdf.PURPOSE_IV, 8);
        byte[] encrypted;
        try {
            encrypted = NativeKeyStoreCrypto.desEdeCbc(
                true, cipherKey, iv, plainBytes.toByteArray());
        } catch (BadPaddingException impossible) {
            throw new IOException("3DES encryption failed", impossible);
        }
        ByteArrayOutputStream resultBytes = new ByteArrayOutputStream();
        DataOutputStream result = new DataOutputStream(resultBytes);
        result.writeInt(salt.length);
        result.write(salt);
        result.writeInt(iterations);
        result.write(encrypted);
        result.flush();
        return resultBytes.toByteArray();
    }

    private static Key recoverKey(byte[] protectedKey, char[] password)
            throws NoSuchAlgorithmException, UnrecoverableKeyException {
        try {
            return recoverKey(protectedKey, password, 0);
        } catch (UnrecoverableKeyException standardFailure) {
            try {
                return recoverKey(protectedKey, password, 1);
            } catch (UnrecoverableKeyException brokenFailure) {
                return recoverKey(protectedKey, password, 2);
            }
        }
    }

    private static Key recoverKey(byte[] protectedKey, char[] password,
            int historicalMode)
            throws NoSuchAlgorithmException, UnrecoverableKeyException {
        try {
            DataInputStream input = new DataInputStream(new ByteArrayInputStream(protectedKey));
            int saltLength = input.readInt();
            if (saltLength < 1 || saltLength > BksLimits.MAX_SALT_BYTES) {
                throw new IOException("invalid key salt length");
            }
            byte[] salt = new byte[saltLength];
            input.readFully(salt);
            int iterations = input.readInt();
            if (iterations < 1 || iterations > BksLimits.MAX_ITERATIONS) {
                throw new IOException("invalid key iteration count");
            }
            byte[] encrypted = new byte[input.available()];
            input.readFully(encrypted);
            byte[] cipherKey = historicalMode == 2
                ? Pkcs12Kdf.deriveOld(password, salt, iterations, Pkcs12Kdf.PURPOSE_KEY, 24)
                : Pkcs12Kdf.derive(password, salt, iterations, Pkcs12Kdf.PURPOSE_KEY, 24);
            byte[] iv = historicalMode == 2
                ? Pkcs12Kdf.deriveOld(password, salt, iterations, Pkcs12Kdf.PURPOSE_IV, 8)
                : Pkcs12Kdf.derive(password, salt, iterations, Pkcs12Kdf.PURPOSE_IV, 8);
            if (historicalMode != 0) applyBrokenDesParity(cipherKey);
            byte[] plain = NativeKeyStoreCrypto.desEdeCbc(false, cipherKey, iv, encrypted);
            DataInputStream keyInput = new DataInputStream(new ByteArrayInputStream(plain));
            Key key = decodeKey(keyInput);
            if (keyInput.available() != 0) throw new IOException("trailing protected key data");
            return key;
        } catch (BadPaddingException exception) {
            throw new UnrecoverableKeyException("incorrect key password");
        } catch (IOException exception) {
            UnrecoverableKeyException failure = new UnrecoverableKeyException("no match");
            failure.initCause(exception);
            throw failure;
        }
    }

    private static void applyBrokenDesParity(byte[] bytes) {
        for (int i = 0; i < bytes.length; ++i) {
            int b = bytes[i];
            bytes[i] = (byte) ((b & 0xfe) |
                (((b >> 1) ^ (b >> 2) ^ (b >> 3) ^ (b >> 4) ^
                  (b >> 5) ^ (b >> 6) ^ (b >> 7)) ^ 1));
        }
    }

    public synchronized Key engineGetKey(String alias, char[] password)
            throws NoSuchAlgorithmException, UnrecoverableKeyException {
        StoreEntry entry = entries.get(alias);
        if (entry == null) return null;
        if (entry.type == TYPE_SEALED) return recoverKey((byte[]) entry.value, password);
        if (entry.type == TYPE_KEY) return (Key) entry.value;
        if (entry.type == TYPE_SECRET) throw new RuntimeException("forget something!");
        return null;
    }

    public synchronized Certificate[] engineGetCertificateChain(String alias) {
        StoreEntry entry = entries.get(alias);
        return entry == null ? null : copyChain(entry.chain);
    }

    public synchronized Certificate engineGetCertificate(String alias) {
        StoreEntry entry = entries.get(alias);
        if (entry == null) return null;
        if (entry.type == TYPE_CERTIFICATE) return (Certificate) entry.value;
        return entry.chain == null || entry.chain.length == 0 ? null : entry.chain[0];
    }

    public synchronized Date engineGetCreationDate(String alias) {
        StoreEntry entry = entries.get(alias);
        return entry == null ? null : new Date(entry.date.getTime());
    }

    public synchronized void engineSetKeyEntry(String alias, Key key, char[] password,
            Certificate[] chain) throws KeyStoreException {
        if (alias == null || key == null) throw new NullPointerException();
        try {
            entries.put(alias, new StoreEntry(TYPE_SEALED, protectKey(key, password), chain));
        } catch (Exception exception) {
            throw new KeyStoreException("key protection failed", exception);
        }
    }

    public synchronized void engineSetKeyEntry(String alias, byte[] key, Certificate[] chain)
            throws KeyStoreException {
        if (alias == null || key == null) throw new NullPointerException();
        entries.put(alias, new StoreEntry(TYPE_SECRET, key.clone(), chain));
    }

    public synchronized void engineSetCertificateEntry(String alias, Certificate certificate)
            throws KeyStoreException {
        if (alias == null || certificate == null) throw new NullPointerException();
        StoreEntry current = entries.get(alias);
        if (current != null && current.type != TYPE_CERTIFICATE) {
            throw new KeyStoreException("alias already contains a key entry");
        }
        entries.put(alias, new StoreEntry(TYPE_CERTIFICATE, certificate, null));
    }

    public synchronized void engineDeleteEntry(String alias) {
        entries.remove(alias);
    }

    public synchronized Enumeration<String> engineAliases() {
        return entries.keys();
    }

    public synchronized boolean engineContainsAlias(String alias) {
        return entries.containsKey(alias);
    }

    public synchronized int engineSize() {
        return entries.size();
    }

    public synchronized boolean engineIsKeyEntry(String alias) {
        StoreEntry entry = entries.get(alias);
        return entry != null && (entry.type == TYPE_KEY || entry.type == TYPE_SECRET ||
                                 entry.type == TYPE_SEALED);
    }

    public synchronized boolean engineIsCertificateEntry(String alias) {
        StoreEntry entry = entries.get(alias);
        return entry != null && entry.type == TYPE_CERTIFICATE;
    }

    public synchronized String engineGetCertificateAlias(Certificate certificate) {
        if (certificate == null) return null;
        Enumeration<String> aliases = entries.keys();
        while (aliases.hasMoreElements()) {
            String alias = aliases.nextElement();
            Certificate candidate = engineGetCertificate(alias);
            if (certificate.equals(candidate)) return alias;
        }
        return null;
    }

    public synchronized void engineStore(OutputStream stream, char[] password)
            throws IOException, NoSuchAlgorithmException, CertificateException {
        if (stream == null) throw new IOException("stream == null");
        final int envelopeBytes = 4 + 4 + 20 + 4 + MAC_SIZE;
        ByteArrayOutputStream bodyBytes = new ByteArrayOutputStream();
        DataOutputStream body = new DataOutputStream(new LimitedOutputStream(
            bodyBytes, BksLimits.MAX_STORE_BYTES - envelopeBytes));
        Enumeration<String> aliases = entries.keys();
        int count = 0;
        while (aliases.hasMoreElements()) {
            if (++count > BksLimits.MAX_ENTRIES) throw new IOException("too many BKS entries");
            String alias = aliases.nextElement();
            StoreEntry entry = entries.get(alias);
            body.writeByte(entry.type);
            body.writeUTF(alias);
            body.writeLong(entry.date.getTime());
            writeChain(body, entry.chain);
            if (entry.type == TYPE_CERTIFICATE) {
                writeCertificate(body, (Certificate) entry.value);
            } else if (entry.type == TYPE_KEY) {
                encodeKey((Key) entry.value, body);
            } else if (entry.type == TYPE_SECRET || entry.type == TYPE_SEALED) {
                byte[] blob = (byte[]) entry.value;
                checkLength(blob.length, BksLimits.MAX_BLOB_BYTES, "entry length");
                body.writeInt(blob.length);
                body.write(blob);
            } else {
                throw new IOException("unsupported BKS entry type");
            }
        }
        body.writeByte(0);
        body.flush();
        byte[] data = bodyBytes.toByteArray();
        byte[] salt = new byte[20];
        SecureRandom random = new SecureRandom();
        random.nextBytes(salt);
        int iterations = 1024 + (random.nextInt() & 1023);
        byte[] signature = mac(password, salt, iterations, STORE_VERSION, data, 0, data.length);
        DataOutputStream output = new DataOutputStream(stream);
        output.writeInt(STORE_VERSION);
        output.writeInt(salt.length);
        output.write(salt);
        output.writeInt(iterations);
        output.write(data);
        output.write(signature);
        output.close();
    }

    public synchronized void engineLoad(InputStream stream, char[] password)
            throws IOException, NoSuchAlgorithmException, CertificateException {
        if (stream == null) {
            entries = new Hashtable<String, StoreEntry>();
            return;
        }
        entries = new Hashtable<String, StoreEntry>();
        byte[] encoded = readAll(stream);
        if (encoded.length < 4 + 4 + 1 + 4 + 1 + MAC_SIZE) {
            throw new IOException("truncated BKS store");
        }
        DataInputStream input = new DataInputStream(new ByteArrayInputStream(encoded));
        int version = input.readInt();
        if (version < 0 || version > STORE_VERSION) throw new IOException("unsupported BKS version");
        int saltLength = input.readInt();
        if (saltLength < 1 || saltLength > BksLimits.MAX_SALT_BYTES) {
            throw new IOException("invalid BKS salt length");
        }
        byte[] salt = new byte[saltLength];
        input.readFully(salt);
        int iterations = input.readInt();
        if (iterations < 1 || iterations > BksLimits.MAX_ITERATIONS) {
            throw new IOException("invalid BKS iteration count");
        }
        int dataOffset = encoded.length - input.available();
        int dataLength = encoded.length - dataOffset - MAC_SIZE;
        if (dataLength < 1) throw new IOException("truncated BKS data");
        if (password != null && password.length != 0) {
            byte[] expected = mac(password, salt, iterations, version,
                                  encoded, dataOffset, dataLength);
            byte[] actual = new byte[MAC_SIZE];
            System.arraycopy(encoded, encoded.length - MAC_SIZE, actual, 0, MAC_SIZE);
            if (!equalBytes(expected, actual)) {
                entries = new Hashtable<String, StoreEntry>();
                throw new IOException("BKS integrity check failed");
            }
        }

        DataInputStream body = new DataInputStream(
            new ByteArrayInputStream(encoded, dataOffset, dataLength));
        Hashtable<String, StoreEntry> loaded = new Hashtable<String, StoreEntry>();
        int count = 0;
        for (;;) {
            int type = body.readUnsignedByte();
            if (type == 0) break;
            if (++count > BksLimits.MAX_ENTRIES) throw new IOException("too many BKS entries");
            String alias = body.readUTF();
            long date = body.readLong();
            Certificate[] chain = readChain(body);
            Object value;
            if (type == TYPE_CERTIFICATE) value = readCertificate(body);
            else if (type == TYPE_KEY) value = decodeKey(body);
            else if (type == TYPE_SECRET || type == TYPE_SEALED)
                value = readBlob(body, "entry length");
            else throw new IOException("unknown BKS entry type");
            StoreEntry entry = new StoreEntry(type, value, chain);
            entry.date.setTime(date);
            loaded.put(alias, entry);
        }
        if (body.available() != 0) throw new IOException("trailing BKS entry data");
        entries = loaded;
    }
}
