package org.ogplay.security;

import java.io.IOException;
import java.math.BigInteger;
import java.util.Arrays;

/** Bounded DER TLV reader for definite lengths up to 3 octets. */
final class DerReader {
    private final byte[] encoded;
    private final int end;
    private int cursor;

    DerReader(byte[] encoded, int start, int end) {
        this.encoded = encoded;
        this.cursor = start;
        this.end = end;
    }

    boolean has(int tag) { return cursor < end && (encoded[cursor] & 0xff) == tag; }

    DerReader read(int tag) throws IOException {
        if (!has(tag) || end - cursor < 2) throw new IOException("invalid DER tag");
        ++cursor;
        int size = encoded[cursor++] & 0xff;
        if (size >= 128) {
            int octets = size & 127;
            if (octets < 1 || octets > 3 || end - cursor < octets || encoded[cursor] == 0) {
                throw new IOException("invalid DER length");
            }
            size = 0;
            for (int i = 0; i < octets; ++i) size = (size << 8) | (encoded[cursor++] & 0xff);
            if (size < 128) throw new IOException("invalid DER length");
        }
        if (size > end - cursor) throw new IOException("truncated DER value");
        DerReader content = new DerReader(encoded, cursor, cursor + size);
        cursor += size;
        return content;
    }

    BigInteger integer() throws IOException {
        DerReader value = read(2);
        if (value.cursor == value.end) throw new IOException("empty DER INTEGER");
        return new BigInteger(value.bytes());
    }

    byte[] bytes() { return Arrays.copyOfRange(encoded, cursor, end); }

    void end() throws IOException {
        if (cursor != end) throw new IOException("unexpected DER content");
    }
}
