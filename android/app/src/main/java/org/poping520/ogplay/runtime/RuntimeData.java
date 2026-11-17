package org.poping520.ogplay.runtime;

import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.system.ErrnoException;
import android.system.Os;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.Locale;
import java.util.stream.Stream;

/** Extracts the bundled data/ assets, because the native runtime reads them as ordinary files. */
public final class RuntimeData {
    private static final String ASSET_ROOT = "data";

    private RuntimeData() {}

    /** Directory the native side treats as its executable directory; data/ sits inside it. */
    public static File runtimeDirectory(Context context) {
        return new File(runtimesRoot(context), Long.toString(installStamp(context)));
    }

    public static File userDataDirectory(Context context) {
        return new File(context.getFilesDir(), "user");
    }

    /** Publishes both roots and the locale for hal/android, which reads them from the process environment. */
    public static void publishEnvironment(Context context) {
        setEnvironment("OGPLAY_ANDROID_RUNTIME_DIR", runtimeDirectory(context).getPath());
        setEnvironment("OGPLAY_ANDROID_USER_DATA_DIR", userDataDirectory(context).getPath());
        Locale locale = context.getResources().getConfiguration().getLocales().get(0);
        String country = locale.getCountry();
        setEnvironment("OGPLAY_ANDROID_LOCALE", locale.getLanguage() + (country.isEmpty() ? "" : "-" + country));
    }

    /** Publishes the current version's data atomically; call it off the main thread. */
    public static File prepare(Context context) throws IOException {
        File runtime = runtimeDirectory(context);
        if (runtime.isDirectory()) return runtime;

        File root = runtimesRoot(context);
        deleteChildren(root);
        File staging = new File(root, ".staging");
        copyAssets(context.getAssets(), ASSET_ROOT, new File(staging, ASSET_ROOT));
        if (!staging.renameTo(runtime)) {
            throw new IOException("Cannot publish " + runtime);
        }
        return runtime;
    }

    private static void setEnvironment(String name, String value) {
        try {
            Os.setenv(name, value, true);
        } catch (ErrnoException error) {
            throw new IllegalStateException("Cannot set " + name, error);
        }
    }

    private static File runtimesRoot(Context context) {
        return new File(context.getFilesDir(), "runtime");
    }

    private static long installStamp(Context context) {
        try {
            return context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0).lastUpdateTime;
        } catch (PackageManager.NameNotFoundException error) {
            throw new IllegalStateException(error);
        }
    }

    private static void copyAssets(AssetManager assets, String path, File target)
            throws IOException {
        String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            Files.createDirectories(target.toPath().getParent());
            try (InputStream input = assets.open(path)) {
                Files.copy(input, target.toPath());
            }
            return;
        }
        for (String child : children) {
            copyAssets(assets, path + "/" + child, new File(target, child));
        }
    }

    private static void deleteChildren(File directory) throws IOException {
        if (!directory.isDirectory()) return;
        try (Stream<Path> paths = Files.walk(directory.toPath())) {
            for (Path path : (Iterable<Path>) paths.sorted(Comparator.reverseOrder())::iterator) {
                if (!path.equals(directory.toPath())) Files.delete(path);
            }
        }
    }
}
