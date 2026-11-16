package org.ogplay.probe;

import android.os.AsyncTask;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Base64;
import dalvik.system.DexClassLoader;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.ShortBuffer;
import java.nio.channels.FileChannel;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Map;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.Callable;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.zip.Adler32;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.GZIPInputStream;
import java.util.zip.GZIPOutputStream;
import java.util.zip.Inflater;
import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import org.json.JSONArray;
import org.json.JSONException;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class RuntimeProbes {
    static void register(Map<String, Probe> probes) {
        threads(probes);
        classes(probes);
        ResourceTableProbes.register(probes);
        crypto(probes);
        buffers(probes);
        network(probes);
    }

    private static void threads(Map<String, Probe> probes) {
        add(probes, new Probe("runtime.thread.handler_delay", "in_scope", false,
                "Landroid/os/Handler;.postDelayed(Ljava/lang/Runnable;J)Z",
                "Landroid/os/SystemClock;.uptimeMillis()J") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final CountDownLatch latch = new CountDownLatch(1);
                final long start = SystemClock.uptimeMillis();
                final long[] elapsed = {-1};
                Handler handler = new Handler(Looper.getMainLooper());
                Runnable delayed = new Runnable() {
                    @Override public void run() {
                        elapsed[0] = SystemClock.uptimeMillis() - start;
                        latch.countDown();
                    }
                };
                boolean posted = handler.postDelayed(delayed, 120);
                boolean ran;
                try { ran = posted && latch.await(1, TimeUnit.SECONDS); }
                finally { handler.removeCallbacks(delayed); }
                return check(ran && elapsed[0] >= 120 && elapsed[0] <= 600,
                        obj("posted", posted, "called", ran, "elapsed_ms", ran ? elapsed[0] : null),
                        obj("minimum_ms", 120, "maximum_ms", 600), "A delayed Handler callback is not delivered early");
            }
        });
        add(probes, new Probe("runtime.thread.handler_thread", "in_scope", false,
                "Landroid/os/HandlerThread;.getLooper()Landroid/os/Looper;",
                "Landroid/os/Handler;.post(Ljava/lang/Runnable;)Z", "Landroid/os/HandlerThread;.quit()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final HandlerThread thread = new HandlerThread("probe-handler");
                final CountDownLatch latch = new CountDownLatch(1);
                final boolean[] correct = {false};
                thread.start();
                boolean delivered;
                try {
                    Handler handler = new Handler(thread.getLooper());
                    boolean enqueued = handler.post(new Runnable() {
                        @Override public void run() {
                            correct[0] = Thread.currentThread() == thread && Looper.myLooper() == thread.getLooper();
                            latch.countDown();
                        }
                    });
                    delivered = enqueued && latch.await(1, TimeUnit.SECONDS);
                } finally {
                    thread.quit();
                    thread.join(500);
                }
                return check(delivered && correct[0] && !thread.isAlive(),
                        obj("delivered", delivered, "thread_matches", correct[0], "stopped", !thread.isAlive()),
                        obj("delivered", true, "thread_matches", true, "stopped", true),
                        "HandlerThread handles work on its own Looper and stops after quit");
            }
        });
        add(probes, new Probe("runtime.thread.async_task", "in_scope", false,
                "Landroid/os/AsyncTask;.execute([Ljava/lang/Object;)Landroid/os/AsyncTask;",
                "Landroid/os/AsyncTask;.doInBackground([Ljava/lang/Object;)Ljava/lang/Object;",
                "Landroid/os/AsyncTask;.onPostExecute(Ljava/lang/Object;)V") {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final CountDownLatch latch = new CountDownLatch(1);
                final boolean[] mainResult = {false};
                final int[] value = {-1};
                final Throwable[] failure = {null};
                session.activity.runOnUiThread(new Runnable() {
                    @Override public void run() {
                        try {
                            new AsyncTask<Integer, Void, Integer>() {
                                @Override protected Integer doInBackground(Integer... input) {
                                    return Looper.myLooper() == Looper.getMainLooper() ? -2 : input[0] + 1;
                                }
                                @Override protected void onPostExecute(Integer result) {
                                    value[0] = result;
                                    mainResult[0] = Looper.myLooper() == Looper.getMainLooper();
                                    latch.countDown();
                                }
                            }.execute(18);
                        } catch (Throwable error) {
                            failure[0] = error;
                            latch.countDown();
                        }
                    }
                });
                boolean delivered = latch.await(1500, TimeUnit.MILLISECONDS);
                if (delivered && failure[0] != null) return error(failure[0]);
                return check(delivered && mainResult[0] && value[0] == 19,
                        obj("delivered", delivered, "on_main", delivered && mainResult[0], "value", delivered ? value[0] : null),
                        obj("delivered", true, "on_main", true, "value", 19),
                        "AsyncTask computes off the UI thread and posts the result back to it");
            }
        });
        add(probes, new Probe("runtime.thread.timer", "in_scope", false, "Ljava/util/Timer;.schedule(Ljava/util/TimerTask;J)V",
                "Ljava/util/Timer;.cancel()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Timer timer = new Timer("probe-timer", true);
                final CountDownLatch latch = new CountDownLatch(1);
                final long start = SystemClock.uptimeMillis();
                final long[] elapsed = {-1};
                boolean delivered;
                try {
                    timer.schedule(new TimerTask() {
                        @Override public void run() {
                            elapsed[0] = SystemClock.uptimeMillis() - start;
                            latch.countDown();
                        }
                    }, 100);
                    delivered = latch.await(1, TimeUnit.SECONDS);
                } finally {
                    timer.cancel();
                }
                return check(delivered && elapsed[0] >= 95 && elapsed[0] <= 600,
                        obj("delivered", delivered, "elapsed_ms", delivered ? elapsed[0] : null),
                        obj("minimum_ms", 95, "maximum_ms", 600), "Timer executes a delayed task within the scheduling tolerance");
            }
        });
        add(probes, new Probe("runtime.thread.executor", "in_scope", false,
                "Ljava/util/concurrent/ThreadPoolExecutor;.<init>(IIJLjava/util/concurrent/TimeUnit;Ljava/util/concurrent/BlockingQueue;)V",
                "Ljava/util/concurrent/Future;.get(JLjava/util/concurrent/TimeUnit;)Ljava/lang/Object;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final Thread caller = Thread.currentThread();
                ThreadPoolExecutor executor = new ThreadPoolExecutor(1, 1, 100, TimeUnit.MILLISECONDS,
                        new LinkedBlockingQueue<Runnable>());
                int first;
                int second;
                try {
                    Future<Integer> left = executor.submit(new Callable<Integer>() {
                        @Override public Integer call() { return Thread.currentThread() == caller ? -1 : 7; }
                    });
                    Future<Integer> right = executor.submit(new Callable<Integer>() {
                        @Override public Integer call() { return Thread.currentThread() == caller ? -1 : 12; }
                    });
                    first = left.get(1, TimeUnit.SECONDS);
                    second = right.get(1, TimeUnit.SECONDS);
                } finally {
                    executor.shutdownNow();
                }
                return check(first == 7 && second == 12, obj("first", first, "second", second),
                        obj("first", 7, "second", 12), "A thread pool runs both queued Callables on a worker");
            }
        });
        add(probes, new Probe("runtime.thread.sleep_wait", "in_scope", false,
                "Ljava/lang/Object;.wait(J)V", "Ljava/lang/Thread;.sleep(J)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Object monitor = new Object();
                long first = SystemClock.elapsedRealtime();
                Thread.sleep(120);
                long second = SystemClock.elapsedRealtime();
                synchronized (monitor) { monitor.wait(120); }
                long third = SystemClock.elapsedRealtime();
                long sleep = second - first;
                long wait = third - second;
                return check(sleep >= 115 && sleep <= 600 && wait >= 115 && wait <= 600,
                        obj("sleep_ms", sleep, "wait_ms", wait), obj("minimum_ms", 115, "maximum_ms", 600),
                        "Timed wait and sleep respect their requested delay within tolerance");
            }
        });
        add(probes, new Probe("runtime.thread.synchronized_counter", "in_scope", false,
                "Ljava/lang/Thread;.start()V", "Ljava/lang/Thread;.join(J)V", "Ljava/util/concurrent/CountDownLatch;.await()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final Object monitor = new Object();
                final int[] counts = {0};
                final CountDownLatch gate = new CountDownLatch(1);
                final CountDownLatch done = new CountDownLatch(3);
                for (int t = 0; t < 3; t++) {
                    Thread worker = new Thread(new Runnable() {
                        @Override public void run() {
                            try {
                                gate.await();
                                for (int i = 0; i < 211; i++) synchronized (monitor) { counts[0]++; }
                            } catch (InterruptedException ignored) {
                                Thread.currentThread().interrupt();
                            } finally {
                                done.countDown();
                            }
                        }
                    }, "probe-contention-" + t);
                    worker.setDaemon(true);
                    worker.start();
                }
                gate.countDown();
                boolean complete = done.await(2, TimeUnit.SECONDS);
                int total;
                synchronized (monitor) { total = counts[0]; }
                return check(complete && total == 633, obj("completed", complete, "counter", total),
                        obj("completed", true, "counter", 633), "The monitor serializes concurrent increments");
            }
        });
    }

    private static void classes(Map<String, Probe> probes) {
        add(probes, new Probe("runtime.class.dex_loader", "in_scope", false,
                "Ldalvik/system/DexClassLoader;.<init>(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V",
                "Ljava/lang/ClassLoader;.loadClass(Ljava/lang/String;)Ljava/lang/Class;",
                "Ljava/lang/reflect/Method;.invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File dex = new File(session.activity.getCacheDir(), "probe-secondary.dex");
                byte[] bytes = ProbeFiles.read(session.activity.getAssets().open("fixtures/secondary.dex"), 1024 * 1024);
                FileOutputStream out = new FileOutputStream(dex);
                try { out.write(bytes); } finally { out.close(); }
                DexClassLoader loader = new DexClassLoader(dex.getAbsolutePath(), session.activity.getCacheDir().getAbsolutePath(),
                        null, session.activity.getClassLoader());
                Class<?> target = loader.loadClass("org.ogplay.probe.secondary.Secondary");
                Object value = target.getMethod("evaluate", Integer.TYPE).invoke(null, 7);
                return check(Integer.valueOf(50).equals(value) && target.getClassLoader() == loader,
                        obj("value", value, "loaded_by_child", target.getClassLoader() == loader),
                        obj("value", 50, "loaded_by_child", true), "DexClassLoader defines and invokes the secondary dex fixture");
            }
        });
        add(probes, new Probe("runtime.class.framework_lookup", "in_scope", false,
                "Ljava/lang/Class;.forName(Ljava/lang/String;)Ljava/lang/Class;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                JSONArray resolved = new JSONArray();
                String[] names = {"android.app.Activity", "android.graphics.Bitmap", "android.view.View",
                        "android.media.MediaPlayer", "android.database.sqlite.SQLiteDatabase", "android.opengl.GLSurfaceView"};
                for (String name : names) resolved.put(Class.forName(name).getName());
                return check(resolved.toString().equals(new JSONArray(Arrays.asList(names)).toString()),
                        resolved, new JSONArray(Arrays.asList(names)), "Common framework classes resolve by binary name");
            }
        });
    }

    private static void crypto(Map<String, Probe> probes) {
        final String[] hashes = {"MD5", "SHA-1", "SHA-256"};
        final String[] vectors = {"900150983cd24fb0d6963f7d28e17f72", "a9993e364706816aba3e25717850c26c9cd0d89d",
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
        for (int i = 0; i < hashes.length; i++) {
            final String algorithm = hashes[i];
            final String expected = vectors[i];
            add(probes, new Probe("runtime.crypto." + algorithm.toLowerCase(java.util.Locale.US).replace("-", ""),
                    "in_scope", false, "Ljava/security/MessageDigest;.getInstance(Ljava/lang/String;)Ljava/security/MessageDigest;",
                    "Ljava/security/MessageDigest;.digest([B)[B") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    String actual = hex(MessageDigest.getInstance(algorithm).digest("abc".getBytes("UTF-8")));
                    return check(expected.equals(actual), obj("hex", actual), obj("hex", expected), algorithm + " digest of abc");
                }
            });
        }
        add(probes, new Probe("runtime.crypto.aes_cbc", "in_scope", false,
                "Ljavax/crypto/Cipher;.getInstance(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                "Ljavax/crypto/Cipher;.init(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                "Ljavax/crypto/Cipher;.doFinal([B)[B") {
            @Override Outcome run(ProbeSession session) throws Exception {
                SecretKeySpec key = new SecretKeySpec(unhex("2b7e151628aed2a6abf7158809cf4f3c"), "AES");
                IvParameterSpec iv = new IvParameterSpec(unhex("000102030405060708090a0b0c0d0e0f"));
                String expected = "2374fdfc3b4ffa0ba97a19327689fd5e6b861707111bb8588b9b2cf585a416f6";
                Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
                byte[] data = "Android KitKat probe".getBytes("UTF-8");
                cipher.init(Cipher.ENCRYPT_MODE, key, iv);
                String encrypted = hex(cipher.doFinal(data));
                cipher.init(Cipher.DECRYPT_MODE, key, iv);
                boolean decrypted = Arrays.equals(cipher.doFinal(unhex(expected)), data);
                return check(expected.equals(encrypted) && decrypted, obj("ciphertext", encrypted, "known_vector_decrypted", decrypted),
                        obj("ciphertext", expected, "known_vector_decrypted", true), "AES-CBC with padding matches the fixed key, IV and plaintext vector");
            }
        });
        add(probes, new Probe("runtime.crypto.hmac_sha1", "in_scope", false,
                "Ljavax/crypto/Mac;.getInstance(Ljava/lang/String;)Ljavax/crypto/Mac;",
                "Ljavax/crypto/Mac;.doFinal([B)[B") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] key = new byte[20];
                Arrays.fill(key, (byte) 0x0b);
                Mac mac = Mac.getInstance("HmacSHA1");
                mac.init(new SecretKeySpec(key, "HmacSHA1"));
                String actual = hex(mac.doFinal("Hi There".getBytes("UTF-8")));
                String expected = "b617318655057264e28bc0b6fb378c8ef146be00";
                return check(expected.equals(actual), obj("hex", actual), obj("hex", expected), "HMAC-SHA1 matches the known key and message vector");
            }
        });
        add(probes, new Probe("runtime.crypto.secure_random", "in_scope", false, "Ljava/security/SecureRandom;.nextBytes([B)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] buf = new byte[32];
                new SecureRandom().nextBytes(buf);
                boolean nonzero = !Arrays.equals(buf, new byte[32]);
                return check(nonzero, obj("nonzero", nonzero, "length", buf.length),
                        obj("nonzero", true, "length", 32), "SecureRandom populates a zero-initialized buffer");
            }
        });
        add(probes, new Probe("runtime.codec.base64", "in_scope", false,
                "Landroid/util/Base64;.encodeToString([BI)Ljava/lang/String;", "Landroid/util/Base64;.decode(Ljava/lang/String;I)[B") {
            @Override Outcome run(ProbeSession session) throws Exception {
                String expected = "AAEC/v8=";
                byte[] fragment = unhex("000102feff");
                String encoded = Base64.encodeToString(fragment, Base64.NO_WRAP);
                String decoded = hex(Base64.decode(expected, Base64.DEFAULT));
                return check(expected.equals(encoded) && hex(fragment).equals(decoded),
                        obj("encoded", encoded, "decoded_hex", decoded), obj("encoded", expected, "decoded_hex", hex(fragment)),
                        "Both Base64 directions match the known byte vector");
            }
        });
        add(probes, new Probe("runtime.codec.deflate", "in_scope", false,
                "Ljava/util/zip/Deflater;.deflate([B)I", "Ljava/util/zip/Inflater;.inflate([B)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] data = "123456789".getBytes("US-ASCII");
                byte[] buffer = new byte[256];
                Deflater deflater = new Deflater();
                byte[] compressed;
                try {
                    deflater.setInput(data);
                    deflater.finish();
                    int count = deflater.deflate(buffer);
                    if (!deflater.finished()) return check(false, obj("complete", false), obj("complete", true), "Deflate fixture did not finish");
                    compressed = Arrays.copyOf(buffer, count);
                } finally { deflater.end(); }
                boolean roundTrip = Arrays.equals(data, inflate(compressed));
                boolean known = Arrays.equals(data, inflate(unhex("7801010900f6ff313233343536373839091e01de")));
                return check(roundTrip && known, obj("round_trip", roundTrip, "fixed_vector", known),
                        obj("round_trip", true, "fixed_vector", true), "Inflater decodes both a fixed zlib stream and Deflater output");
            }
        });
        add(probes, new Probe("runtime.codec.checksums", "in_scope", false,
                "Ljava/util/zip/CRC32;.update([B)V", "Ljava/util/zip/CRC32;.getValue()J",
                "Ljava/util/zip/Adler32;.update([B)V", "Ljava/util/zip/Adler32;.getValue()J",
                "Ljava/util/zip/Adler32;.updateImpl([BIIJ)J") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] data = "123456789".getBytes("US-ASCII");
                CRC32 crc = new CRC32();
                Adler32 adler = new Adler32();
                crc.update(data);
                adler.update(data);
                return check(crc.getValue() == 0xcbf43926L && adler.getValue() == 0x091e01deL,
                        obj("crc32", crc.getValue(), "adler32", adler.getValue()),
                        obj("crc32", 0xcbf43926L, "adler32", 0x091e01deL), "CRC32 and Adler32 match their integer vectors");
            }
        });
        add(probes, new Probe("runtime.codec.gzip", "in_scope", false,
                "Ljava/util/zip/GZIPInputStream;.<init>(Ljava/io/InputStream;)V",
                "Ljava/util/zip/GZIPOutputStream;.<init>(Ljava/io/OutputStream;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] bytes = "KitKat az AZ".getBytes("UTF-8");
                ByteArrayOutputStream output = new ByteArrayOutputStream();
                GZIPOutputStream gzip = new GZIPOutputStream(output);
                try { gzip.write(bytes); } finally { gzip.close(); }
                byte[] encoded = output.toByteArray();
                byte[] decoded = ProbeFiles.read(new GZIPInputStream(new ByteArrayInputStream(encoded)), 512);
                boolean magic = encoded.length > 18 && encoded[0] == 31 && (encoded[1] & 255) == 139;
                byte[] fixture = unhex("1f8b08000000000000ff010c00f3ff4b69744b617420617a20415a740a534b0c000000");
                byte[] fixedDecoded = ProbeFiles.read(new GZIPInputStream(new ByteArrayInputStream(fixture)), 512);
                return check(magic && Arrays.equals(bytes, decoded) && Arrays.equals(bytes, fixedDecoded),
                        obj("magic", magic, "decoded", hex(decoded), "fixture_decoded", hex(fixedDecoded)),
                        obj("magic", true, "decoded", hex(bytes), "fixture_decoded", hex(bytes)),
                        "GZIP decodes a fixed stream and round-trips the same payload");
            }
        });
        final String[][] encodings = {{"UTF-8", "老游戏", "e88081e6b8b8e6888f"}, {"GBK", "游戏", "d3cecfb7"},
                {"Big5", "遊戲", "b943c0b8"}, {"Shift_JIS", "ゲーム", "8351815b8380"}, {"ISO-8859-1", "café", "636166e9"}};
        for (final String[] vector : encodings) {
            add(probes, new Probe("runtime.charset." + vector[0].toLowerCase(java.util.Locale.US).replace('-', '_'),
                    "in_scope", false, "Ljava/lang/String;.getBytes(Ljava/lang/String;)[B",
                    "Ljava/lang/String;.<init>([BLjava/lang/String;)V") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    String encoded = hex(vector[1].getBytes(vector[0]));
                    String decoded = new String(unhex(vector[2]), vector[0]);
                    return check(vector[2].equals(encoded) && vector[1].equals(decoded),
                            obj("encoded", encoded, "decoded", decoded), obj("encoded", vector[2], "decoded", vector[1]),
                            "Encode and decode known " + vector[0] + " bytes");
                }
            });
        }
    }

    private static void buffers(Map<String, Probe> probes) {
        add(probes, new Probe("runtime.nio.direct_views", "in_scope", false,
                "Ljava/nio/ByteBuffer;.allocateDirect(I)Ljava/nio/ByteBuffer;",
                "Ljava/nio/ByteBuffer;.order(Ljava/nio/ByteOrder;)Ljava/nio/ByteBuffer;",
                "Ljava/nio/ByteBuffer;.asFloatBuffer()Ljava/nio/FloatBuffer;",
                "Ljava/nio/FloatBuffer;.put([F)Ljava/nio/FloatBuffer;", "Ljava/nio/FloatBuffer;.get([F)Ljava/nio/FloatBuffer;",
                "Ljava/nio/ShortBuffer;.put([S)Ljava/nio/ShortBuffer;", "Ljava/nio/ShortBuffer;.get([S)Ljava/nio/ShortBuffer;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ByteBuffer direct = ByteBuffer.allocateDirect(16).order(ByteOrder.LITTLE_ENDIAN);
                direct.putInt(0, 0x01020304);
                int first = direct.get(0) & 255;
                int big = direct.order(ByteOrder.BIG_ENDIAN).getInt(0);
                direct.order(ByteOrder.LITTLE_ENDIAN);
                FloatBuffer floats = direct.asFloatBuffer();
                floats.put(new float[] {2.5f, -1f});
                floats.position(0);
                float[] read = new float[2];
                floats.get(read);
                float parent = direct.getFloat(0);
                direct.position(8);
                ShortBuffer shorts = direct.asShortBuffer();
                shorts.put(new short[] {300, -7});
                shorts.position(0);
                short[] tail = new short[2];
                shorts.get(tail);
                boolean same = direct.isDirect() && first == 4 && big == 0x04030201 && parent == 2.5f
                        && read[0] == 2.5f && read[1] == -1f && tail[0] == 300 && tail[1] == -7 && direct.position() == 8;
                return check(same, obj("direct", direct.isDirect(), "first_byte", first, "big_endian_int", big,
                                "parent_float", parent, "float_bulk", new JSONArray().put(read[0]).put(read[1]),
                                "short_bulk", new JSONArray().put(tail[0]).put(tail[1]), "parent_position", direct.position()),
                        obj("direct", true, "first_byte", 4, "big_endian_int", 0x04030201, "parent_float", 2.5,
                                "float_bulk", new JSONArray().put(2.5).put(-1), "short_bulk", new JSONArray().put(300).put(-7),
                                "parent_position", 8), "Typed direct-buffer views share ordered data and have independent positions");
            }
        });
        add(probes, new Probe("runtime.nio.file_channel", "in_scope", false,
                "Ljava/nio/channels/FileChannel;.write(Ljava/nio/ByteBuffer;)I",
                "Ljava/nio/channels/FileChannel;.read(Ljava/nio/ByteBuffer;)I",
                "Ljava/nio/channels/FileChannel;.position()J", "Ljava/nio/channels/FileChannel;.position(J)Ljava/nio/channels/FileChannel;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File file = new File(session.activity.getCacheDir(), "probe-channel.dat");
                try {
                    RandomAccessFile handle = new RandomAccessFile(file, "rw");
                    try {
                        handle.setLength(0);
                        FileChannel channel = handle.getChannel();
                        byte[] bytes = unhex("131421");
                        int written = channel.write(ByteBuffer.wrap(bytes));
                        long position = channel.position();
                        channel.position(1);
                        ByteBuffer output = ByteBuffer.allocate(2);
                        int read = channel.read(output);
                        String actual = hex(output.array());
                        return check(written == 3 && position == 3 && read == 2 && channel.position() == 3 && "1421".equals(actual),
                                obj("written", written, "write_position", position, "read", read,
                                        "final_position", channel.position(), "hex", actual),
                                obj("written", 3, "write_position", 3, "read", 2, "final_position", 3, "hex", "1421"),
                                "FileChannel transfers bytes and advances its shared file position");
                    } finally { handle.close(); }
                } finally { file.delete(); }
            }
        });
    }

    private static void network(Map<String, Probe> probes) {
        add(probes, new Probe("runtime.network.loopback", "info", false,
                "Ljava/net/ServerSocket;.<init>(IILjava/net/InetAddress;)V",
                "Ljava/net/ServerSocket;.accept()Ljava/net/Socket;",
                "Ljava/net/Socket;.<init>(Ljava/net/InetAddress;I)V",
                "Ljava/net/InetAddress;.getByName(Ljava/lang/String;)Ljava/net/InetAddress;",
                "Llibcore/io/Posix;.socket(III)Ljava/io/FileDescriptor;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                InetAddress loopback = InetAddress.getByAddress(new byte[] {127, 0, 0, 1});
                ServerSocket server = new ServerSocket(0, 1, loopback);
                int value;
                try {
                    server.setSoTimeout(500);
                    Socket client = new Socket(loopback, server.getLocalPort());
                    try {
                        client.getOutputStream().write(19);
                        Socket remote = server.accept();
                        try {
                            remote.setSoTimeout(500);
                            value = remote.getInputStream().read();
                        } finally { remote.close(); }
                    } finally { client.close(); }
                } finally { server.close(); }
                InetAddress localhost = InetAddress.getByName("localhost");
                return new Outcome("info", obj("received", value, "localhost", localhost.getHostAddress(),
                                "localhost_loopback", localhost.isLoopbackAddress()),
                        obj("received", 19, "localhost_loopback", true), "Loopback TCP and local hostname resolution; scope review", null);
            }
        });
    }

    private static byte[] inflate(byte[] compressed) throws Exception {
        Inflater inflater = new Inflater();
        try {
            inflater.setInput(compressed);
            byte[] output = new byte[256];
            int size = inflater.inflate(output);
            return inflater.finished() ? Arrays.copyOf(output, size) : new byte[0];
        } finally { inflater.end(); }
    }

    static String hex(byte[] bytes) {
        StringBuilder output = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            output.append(Character.forDigit((value & 255) >> 4, 16));
            output.append(Character.forDigit(value & 15, 16));
        }
        return output.toString();
    }

    static byte[] unhex(String text) {
        byte[] bytes = new byte[text.length() / 2];
        for (int i = 0; i < bytes.length; i++) bytes[i] = (byte) Integer.parseInt(text.substring(2 * i, 2 * i + 2), 16);
        return bytes;
    }
}
