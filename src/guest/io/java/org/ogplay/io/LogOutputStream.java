package org.ogplay.io;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintStream;
import java.io.UnsupportedEncodingException;

/** UTF-8 diagnostic endpoint used only by System.out and System.err. */
public final class LogOutputStream extends OutputStream {
    private final ByteArrayOutputStream pending = new ByteArrayOutputStream();
    private boolean closed;

    public static PrintStream createPrintStream() throws UnsupportedEncodingException {
        return new PrintStream(new LogOutputStream(), true, "UTF-8");
    }

    private void requireOpen() throws IOException {
        if (closed) throw new IOException("diagnostic stream is closed");
    }

    private void emitPending() throws IOException {
        if (pending.size() != 0) {
            emit(pending.toString("UTF-8"));
            pending.reset();
        }
    }

    @Override public synchronized void write(int value) throws IOException {
        requireOpen();
        if ((value & 255) == 10) emitPending();
        else pending.write(value);
    }

    @Override public synchronized void write(byte[] bytes, int offset, int count) throws IOException {
        if (bytes == null) throw new NullPointerException("bytes");
        if (offset < 0 || count < 0 || offset > bytes.length - count)
            throw new IndexOutOfBoundsException("write range");
        requireOpen();
        int end = offset + count;
        int start = offset;
        for (int i = offset; i < end; ++i) {
            if (bytes[i] == 10) {
                pending.write(bytes, start, i - start);
                emitPending();
                start = i + 1;
            }
        }
        pending.write(bytes, start, end - start);
    }

    @Override public synchronized void flush() throws IOException {
        requireOpen();
        emitPending();
    }

    @Override public synchronized void close() throws IOException {
        if (!closed) {
            emitPending();
            closed = true;
        }
    }

    private static native void emit(String text);
}
