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
    private final boolean error;

    public LogOutputStream() { this(false); }
    private LogOutputStream(boolean error) { this.error = error; }

    public static PrintStream createPrintStream() throws UnsupportedEncodingException {
        return new PrintStream(new LogOutputStream(), true, "UTF-8");
    }

    public static PrintStream createErrorPrintStream() throws UnsupportedEncodingException {
        return new PrintStream(new LogOutputStream(true), true, "UTF-8");
    }

    private void requireOpen() throws IOException {
        if (closed) throw new IOException("diagnostic stream is closed");
    }

    // A line end commits even an empty line; flush/close commit only a
    // non-empty fragment. The host keeps the line-end fact for raw consoles.
    private void emitPending(boolean lineEnd) throws IOException {
        if (lineEnd || pending.size() != 0) {
            emit(pending.toString("UTF-8"), lineEnd, error);
            pending.reset();
        }
    }

    @Override public synchronized void write(int value) throws IOException {
        requireOpen();
        if ((value & 255) == 10) emitPending(true);
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
                emitPending(true);
                start = i + 1;
            }
        }
        pending.write(bytes, start, end - start);
    }

    @Override public synchronized void flush() throws IOException {
        requireOpen();
        emitPending(false);
    }

    @Override public synchronized void close() throws IOException {
        if (!closed) {
            emitPending(false);
            closed = true;
        }
    }

    private static native void emit(String text, boolean lineEnd, boolean error);
}
