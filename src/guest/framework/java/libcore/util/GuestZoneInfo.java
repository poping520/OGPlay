package libcore.util;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.util.Arrays;
import java.util.TimeZone;
import libcore.io.BufferIterator;

/** Supplies the pinned guest tzdata to the original API 19 ZoneInfo reader. */
public final class GuestZoneInfo {
    private static final String PATH = "/system/usr/share/zoneinfo/tzdata";
    private static final int INDEX_ENTRY_SIZE = 52;
    private static final int TZIF_HEADER_SIZE = 44;
    private static final int TYPE_SIZE = 6;
    private static final Charset ASCII = Charset.forName("US-ASCII");
    private static byte[] data;

    private GuestZoneInfo() {}

    private static synchronized ByteBuffer record(String id) {
        if (data == null) {
            try (RandomAccessFile file = new RandomAccessFile(PATH, "r")) {
                long length = file.length();
                if (length < 24 || length > 1024 * 1024) {
                    throw new IOException("Invalid tzdata length");
                }
                byte[] bytes = new byte[(int) length];
                file.readFully(bytes);
                data = bytes;
            } catch (IOException e) {
                throw new UnsupportedOperationException("Guest tzdata is unavailable", e);
            }
        }

        ByteBuffer buffer = ByteBuffer.wrap(data);
        int index = buffer.getInt(12);
        int records = buffer.getInt(16);
        int low = 0;
        int high = (records - index) / INDEX_ENTRY_SIZE - 1;
        while (low <= high) {
            int mid = (low + high) >>> 1;
            int entry = index + mid * INDEX_ENTRY_SIZE;
            int end = entry;
            while (end < entry + 40 && data[end] != 0) ++end;
            int order = id.compareTo(new String(data, entry, end - entry, Charset.forName("US-ASCII")));
            if (order == 0) {
                buffer.position(records + buffer.getInt(entry + 40));
                buffer.limit(buffer.position() + buffer.getInt(entry + 44));
                return buffer.slice();
            }
            if (order < 0) high = mid - 1;
            else low = mid + 1;
        }
        return null;
    }

    public static TimeZone find(String id) {
        ByteBuffer record = record(id);
        return record == null ? null : ZoneInfo.makeTimeZone(id, new Iterator(record));
    }

    // Bionic tries the most recently used transition types first when isDst
    // explicitly requests an offset that does not apply at the given wall time.
    public static int[] offsets(String id, boolean daylight) {
        ByteBuffer record = record(id);
        if (record == null) return new int[0];
        int transitions = record.getInt(32);
        int types = record.getInt(36);
        int indices = TZIF_HEADER_SIZE + transitions * 4;
        int typesStart = indices + transitions;
        int[] offsets = new int[types];
        boolean[] seen = new boolean[types];
        int count = 0;
        for (int i = transitions - 1; i >= 0; --i) {
            int type = record.get(indices + i) & 0xff;
            int at = typesStart + type * TYPE_SIZE;
            if (!seen[type] && (record.get(at + 4) != 0) == daylight) {
                offsets[count++] = record.getInt(at) * 1000;
                seen[type] = true;
            }
        }
        return Arrays.copyOf(offsets, count);
    }

    public static String abbreviation(String id, long millis) {
        ByteBuffer record = record(id);
        if (record == null) return id;
        int transitions = record.getInt(32);
        int types = record.getInt(36);
        int indices = TZIF_HEADER_SIZE + transitions * 4;
        int typesStart = indices + transitions;
        int low = 0;
        int high = transitions;
        while (low < high) {
            int mid = (low + high) >>> 1;
            if (record.getInt(TZIF_HEADER_SIZE + mid * 4) <= millis / 1000) low = mid + 1;
            else high = mid;
        }
        int type = low > 0 ? record.get(indices + low - 1) & 0xff : 0;
        if (low == 0) {
            while (type + 1 < types && record.get(typesStart + type * TYPE_SIZE + 4) != 0) ++type;
        }
        int strings = typesStart + types * TYPE_SIZE;
        int start = strings + (record.get(typesStart + type * TYPE_SIZE + 5) & 0xff);
        int end = start;
        while (end < record.limit() && record.get(end) != 0) ++end;
        byte[] name = new byte[end - start];
        record.position(start);
        record.get(name);
        return new String(name, ASCII);
    }

    private static final class Iterator extends BufferIterator {
        private final ByteBuffer bytes;

        Iterator(ByteBuffer bytes) { this.bytes = bytes; }

        @Override public void seek(int offset) { bytes.position(offset); }
        @Override public void skip(int count) { bytes.position(bytes.position() + count); }
        @Override public void readByteArray(byte[] dest, int offset, int count) {
            bytes.get(dest, offset, count);
        }
        @Override public byte readByte() { return bytes.get(); }
        @Override public int readInt() { return bytes.getInt(); }
        @Override public void readIntArray(int[] dest, int offset, int count) {
            for (int i = 0; i < count; ++i) dest[offset + i] = bytes.getInt();
        }
        @Override public short readShort() { return bytes.getShort(); }
    }
}
