package org.ogplay.probe;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;
import static org.ogplay.probe.RuntimeProbes.hex;

final class StorageProbes {
    private static final String STORED = "fixtures/stored.bin";
    private static final String DEFLATED = "fixtures/deflated.bin";

    static void register(Map<String, Probe> probes) {
        filesystem(probes);
        assets(probes);
        resources(probes);
        databases(probes);
    }

    private static void filesystem(Map<String, Probe> probes) {
        add(probes, new Probe("storage.prefs.commit_apply", "in_scope", false,
                "Landroid/content/Context;.getSharedPreferences(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
                "Landroid/content/SharedPreferences$Editor;.commit()Z",
                "Landroid/content/SharedPreferences$Editor;.apply()V",
                "Landroid/content/SharedPreferences;.getString(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                SharedPreferences preferences = session.activity.getSharedPreferences("commit_apply", Context.MODE_PRIVATE);
                boolean committed = preferences.edit().putString("text", "KitKat").putInt("number", 19).commit();
                String text = preferences.getString("text", null);
                int number = preferences.getInt("number", -1);
                preferences.edit().remove("text").putInt("number", 20).apply();
                SharedPreferences sameFile = session.activity.getSharedPreferences("commit_apply", Context.MODE_PRIVATE);
                boolean removed = !sameFile.contains("text");
                int applied = sameFile.getInt("number", -1);
                return check(committed && "KitKat".equals(text) && number == 19 && removed && applied == 20,
                        obj("committed", committed, "text", text, "number", number, "removed_immediately", removed, "applied", applied),
                        obj("committed", true, "text", "KitKat", "number", 19, "removed_immediately", true, "applied", 20),
                        "Commit and apply make the right typed values visible immediately");
            }
        });
        add(probes, new Probe("storage.file.round_trip", "in_scope", false,
                "Ljava/io/RandomAccessFile;.seek(J)V", "Ljava/io/RandomAccessFile;.writeInt(I)V",
                "Ljava/io/RandomAccessFile;.readInt()I", "Ljava/io/File;.mkdirs()Z", "Ljava/io/File;.list()[Ljava/lang/String;",
                "Ljava/io/File;.length()J", "Ljava/io/File;.lastModified()J", "Ljava/io/File;.setLastModified(J)Z",
                "Ljava/io/File;.getFreeSpace()J", "Ljava/io/FileInputStream;.read()I", "Ljava/io/File;.delete()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File top = new File(session.activity.getCacheDir(), "file-probe-" + Long.toHexString(System.nanoTime()));
                File dir = new File(top, "nested");
                File file = new File(dir, "bytes");
                try {
                    boolean made = dir.mkdirs();
                    RandomAccessFile random = new RandomAccessFile(file, "rw");
                    int value;
                    try {
                        random.writeInt(0x01020304);
                        random.seek(1);
                        random.write(19);
                        random.seek(0);
                        value = random.readInt();
                    } finally { random.close(); }
                    String[] listed = dir.list();
                    long length = file.length();
                    long fixed = 1388534400000L;
                    boolean changed = file.setLastModified(fixed);
                    long modified = file.lastModified();
                    long free = file.getFreeSpace();
                    FileInputStream input = new FileInputStream(file);
                    boolean eof;
                    int total = 0;
                    try {
                        while (input.read() != -1 && total <= 4) total++;
                        eof = input.read() == -1;
                    } finally { input.close(); }
                    boolean deleted = file.delete() && !file.exists();
                    return new Checks()
                            .require("nested_directory_created", made)
                            .require("random_access_round_trip", value == 0x01130304)
                            .require("directory_lists_created_file", listed != null && listed.length == 1 && "bytes".equals(listed[0]))
                            .require("file_length_matches", length == 4)
                            .require("set_last_modified_succeeds", changed)
                            .require("last_modified_matches", Math.abs(modified - fixed) <= 2000)
                            .require("free_space_positive", free > 0)
                            .require("stream_read_byte_count_matches", total == 4)
                            .require("repeat_read_reports_eof", eof)
                            .require("file_deleted", deleted)
                            .outcome(obj("mkdirs", made, "random_read", value,
                                    "list", listed == null ? null : new JSONArray(Arrays.asList(listed)), "length", length,
                                    "timestamp_set", changed, "modified", modified, "free_bytes", free, "read_bytes", total,
                                    "repeat_eof", eof, "deleted", deleted),
                            obj("mkdirs", true, "random_read", 0x01130304, "list", new JSONArray().put("bytes"), "length", 4,
                                    "modified", fixed, "timestamp_tolerance_ms", 2000, "free_positive", true, "read_bytes", 4,
                                    "repeat_eof", true, "deleted", true),
                            "Created paths, random writes, file metadata, EOF and deletion follow file API semantics");
                } finally {
                    file.delete();
                    dir.delete();
                    top.delete();
                }
            }
        });
        add(probes, new Probe("storage.file.rename_replace", "in_scope", false,
                "Ljava/io/File;.renameTo(Ljava/io/File;)Z", "Ljava/io/FileInputStream;.<init>(Ljava/io/File;)V",
                "Ljava/io/FileDescriptor;.sync()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File from = new File(session.activity.getCacheDir(), "rename-from");
                File to = new File(session.activity.getCacheDir(), "rename-to");
                try {
                    ProbeFiles.atomic(from, "new contents");
                    ProbeFiles.atomic(to, "old contents");
                    FileInputStream previous = new FileInputStream(to);
                    try {
                        boolean renamed = from.renameTo(to);
                        String fresh = ProbeFiles.text(to, 512);
                        String old = new String(ProbeFiles.read(previous, 512), "UTF-8");
                        return check(renamed && !from.exists() && "new contents".equals(fresh) && "old contents".equals(old),
                                obj("renamed", renamed, "source_exists", from.exists(), "new_handle", fresh, "old_handle", old),
                                obj("renamed", true, "source_exists", false, "new_handle", "new contents", "old_handle", "old contents"),
                                "Rename atomically replaces the destination path while the old open handle stays readable");
                    } finally { previous.close(); }
                } finally { from.delete(); to.delete(); }
            }
        });
        add(probes, new Probe("storage.external.round_trip", "in_scope", false,
                "Landroid/content/Context;.getExternalFilesDir(Ljava/lang/String;)Ljava/io/File;",
                "Ljava/io/FileOutputStream;.write([B)V", "Ljava/io/FileInputStream;.read([B)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File directory = session.activity.getExternalFilesDir(null);
                if (directory == null) return check(false, obj("directory", null), obj("directory_present", true),
                        "Mounted external storage should offer an app-specific directory");
                File file = new File(directory, "probe-external");
                try {
                    byte[] input = RuntimeProbes.unhex("00137f80ff");
                    FileOutputStream output = new FileOutputStream(file);
                    try { output.write(input); } finally { output.close(); }
                    byte[] result = ProbeFiles.read(new FileInputStream(file), 64);
                    return check(Arrays.equals(input, result), obj("hex", hex(result)), obj("hex", hex(input)),
                            "Writing app-specific external storage preserves bytes");
                } finally { file.delete(); }
            }
        });
    }

    private static void assets(Map<String, Probe> probes) {
        final String[] paths = {STORED, DEFLATED};
        final String[] hashes = {AssetFixtures.STORED_SHA256, AssetFixtures.DEFLATED_SHA256};
        for (int i = 0; i < paths.length; i++) {
            final String path = paths[i];
            final String expected = hashes[i];
            final String id = i == 0 ? "storage.assets.stored" : "storage.assets.deflated";
            add(probes, new Probe(id, "in_scope", false,
                    "Landroid/content/res/AssetManager;.open(Ljava/lang/String;)Ljava/io/InputStream;") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    String actual = digest(ProbeFiles.read(session.activity.getAssets().open(path), 32768));
                    return check(expected.equals(actual), obj("path", path, "sha256", actual), obj("sha256", expected),
                            "Asset payload matches the build-time SHA-256");
                }
            });
        }
        add(probes, new Probe("storage.assets.open_fd", "in_scope", false,
                "Landroid/content/res/AssetManager;.openFd(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;",
                "Landroid/content/res/AssetFileDescriptor;.getStartOffset()J",
                "Landroid/content/res/AssetFileDescriptor;.getLength()J",
                "Landroid/content/res/AssetFileDescriptor;.createInputStream()Ljava/io/FileInputStream;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                AssetManager assets = session.activity.getAssets();
                AssetFileDescriptor fd = assets.openFd(STORED);
                try {
                    long offset = fd.getStartOffset();
                    long size = fd.getLength();
                    if (offset < 0 || size < 1 || size > 1024) {
                        return check(false, obj("offset", offset, "length", size), obj("valid_range", true), "Stored asset range is not valid");
                    }
                    byte[] direct = new byte[(int) size];
                    RandomAccessFile apk = new RandomAccessFile(session.activity.getApplicationInfo().sourceDir, "r");
                    try {
                        apk.seek(offset);
                        apk.readFully(direct);
                    } finally { apk.close(); }
                    byte[] stream = ProbeFiles.read(fd.createInputStream(), 1024);
                    String sha = digest(direct);
                    return check(size == stream.length && Arrays.equals(direct, stream) && AssetFixtures.STORED_SHA256.equals(sha),
                            obj("offset", offset, "length", size, "stream_length", stream.length, "apk_slice_sha256", sha),
                            obj("stream_and_apk_slice_match", true, "apk_slice_sha256", AssetFixtures.STORED_SHA256),
                            "The stored asset's FD points at the exact fixture bytes in the APK");
                } finally { fd.close(); }
            }
        });
        add(probes, new Probe("storage.assets.list", "in_scope", false,
                "Landroid/content/res/AssetManager;.list(Ljava/lang/String;)[Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                String[] entries = session.activity.getAssets().list("fixtures");
                Arrays.sort(entries);
                java.util.List<String> names = Arrays.asList(entries);
                java.util.List<String> expected = Arrays.asList("deflated.bin", "nested.zip", "pixels.jpg", "pixels.png", "secondary.dex", "stored.bin");
                return check(names.equals(expected), new JSONArray(names), new JSONArray(expected),
                        "AssetManager lists exactly the embedded fixture directory entries");
            }
        });
    }

    private static void resources(Map<String, Probe> probes) {
        add(probes, new Probe("storage.resources.localized", "in_scope", true,
                "Landroid/content/Context;.createConfigurationContext(Landroid/content/res/Configuration;)Landroid/content/Context;",
                "Landroid/content/res/Resources;.getString(I)Ljava/lang/String;",
                "Landroid/content/res/Resources;.getIdentifier(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Context context = session.activity;
                Configuration en = new Configuration(context.getResources().getConfiguration());
                en.locale = Locale.US;
                Configuration zh = new Configuration(en);
                zh.locale = Locale.SIMPLIFIED_CHINESE;
                Resources english = context.createConfigurationContext(en).getResources();
                Resources chinese = context.createConfigurationContext(zh).getResources();
                int id = english.getIdentifier("fixture_localized", "string", context.getPackageName());
                String a = english.getString(R.string.fixture_localized);
                String b = chinese.getString(R.string.fixture_localized);
                return check(id == R.string.fixture_localized && "Probe fixture".equals(a) && "探针素材".equals(b),
                        obj("id", id, "en", a, "zh", b), obj("id", R.string.fixture_localized, "en", "Probe fixture", "zh", "探针素材"),
                        "Resource identifier and both locale variants match the compiled fixtures");
            }
        });
        add(probes, new Probe("storage.resources.payload", "in_scope", false,
                "Landroid/content/res/Resources;.openRawResource(I)Ljava/io/InputStream;",
                "Landroid/content/res/Resources;.getDrawable(I)Landroid/graphics/drawable/Drawable;",
                "Landroid/content/res/Resources;.getDimension(I)F", "Landroid/graphics/Bitmap;.getPixel(II)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Resources resources = session.activity.getResources();
                InputStream input = resources.openRawResource(R.raw.fixture_bytes);
                String raw = hex(ProbeFiles.read(input, 128));
                Bitmap bitmap = ((BitmapDrawable) resources.getDrawable(R.drawable.fixture_pixels)).getBitmap();
                int pixel = bitmap.getPixel(2, 2);
                float dim = resources.getDimension(R.dimen.fixture_dimension);
                float expected = 24 * resources.getDisplayMetrics().density;
                return check("70726f62652d3139007fff".equals(raw) && pixel == 0xff088658 && Math.abs(dim - expected) < 0.01f,
                        obj("raw_hex", raw, "pixel", pixel, "dimension_px", dim),
                        obj("raw_hex", "70726f62652d3139007fff", "pixel", 0xff088658, "dimension_px", expected),
                        "Compiled raw bytes, drawable pixels and density-converted dp match the resource fixtures");
            }
        });
    }

    private static void databases(Map<String, Probe> probes) {
        add(probes, new Probe("storage.sqlite.transaction", "in_scope", false,
                "Landroid/database/sqlite/SQLiteDatabase;.openOrCreateDatabase(Ljava/io/File;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
                "Landroid/database/sqlite/SQLiteDatabase;.execSQL(Ljava/lang/String;[Ljava/lang/Object;)V",
                "Landroid/database/sqlite/SQLiteDatabase;.rawQuery(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/Cursor;",
                "Landroid/database/sqlite/SQLiteDatabase;.beginTransaction()V", "Landroid/database/sqlite/SQLiteDatabase;.endTransaction()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File file = new File(session.activity.getCacheDir(), "probe-sql-" + Long.toHexString(System.nanoTime()));
                try {
                    SQLiteDatabase db = SQLiteDatabase.openOrCreateDatabase(file, null);
                    try {
                        db.execSQL("CREATE TABLE probe (id INTEGER PRIMARY KEY, value TEXT NOT NULL)");
                        db.execSQL("INSERT INTO probe VALUES (?, ?)", new Object[] {19, "KitKat"});
                        db.beginTransaction();
                        try {
                            db.execSQL("UPDATE probe SET value=? WHERE id=?", new Object[] {"discard", 19});
                        } finally { db.endTransaction(); }
                        Cursor cursor = db.rawQuery("SELECT value FROM probe WHERE id=?", new String[] {"19"});
                        try {
                            String actual = cursor.moveToFirst() ? cursor.getString(0) : null;
                            int count = cursor.getCount();
                            return check(count == 1 && "KitKat".equals(actual), obj("count", count, "value", actual),
                                    obj("count", 1, "value", "KitKat"), "SQLite binds query parameters and rolls back an uncommitted update");
                        } finally { cursor.close(); }
                    } finally { db.close(); }
                } finally { file.delete(); }
            }
        });
    }

    static String digest(byte[] bytes) throws Exception {
        return hex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }
}
