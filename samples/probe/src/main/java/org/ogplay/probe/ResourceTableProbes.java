package org.ogplay.probe;

import android.content.res.AssetManager;
import android.content.res.Resources;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.InvocationTargetException;
import java.util.Map;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;

final class ResourceTableProbes {
    private static final String PACKAGE = "org.ogplay.probe.restable";
    private static final String MARKER = "ogplay-probe-restable";

    static void register(Map<String, Probe> probes) {
        add(probes, pack("runtime.resources.table_control", "control.apk"));
        add(probes, pack("runtime.resources.aapt2_typespec", "aapt2.apk"));
    }

    private static Probe pack(String id, final String name) {
        return new Probe(id, "in_scope", false, "Landroid/content/res/AssetManager;.<init>()V",
                "Landroid/content/res/AssetManager;.addAssetPath(Ljava/lang/String;)I",
                "Landroid/content/res/Resources;.getIdentifier(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I",
                "Landroid/content/res/Resources;.getString(I)Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                File file = new File(session.activity.getFilesDir(), "restable-" + name);
                JSONObject observed = obj("asset", "restable/" + name, "cookie", null, "resource_id", null,
                        "marker", null, "stage", "copy_asset");
                JSONObject expected = obj("positive_cookie", true, "nonzero_resource_id", true, "marker", MARKER);
                try {
                    byte[] bytes = ProbeFiles.read(session.activity.getAssets().open("restable/" + name), 65536);
                    try (FileOutputStream output = new FileOutputStream(file)) {
                        output.write(bytes);
                    }
                    observed.put("stage", "construct_asset_manager");
                    AssetManager assets = AssetManager.class.getConstructor().newInstance();
                    try {
                        observed.put("stage", "add_asset_path");
                        int cookie = (Integer) AssetManager.class.getMethod("addAssetPath", String.class)
                                .invoke(assets, file.getAbsolutePath());
                        observed.put("cookie", cookie);
                        if (cookie <= 0) return check(false, observed, expected, "addAssetPath rejected the resource pack");

                        observed.put("stage", "construct_resources");
                        Resources app = session.activity.getResources();
                        Resources resources = new Resources(assets, app.getDisplayMetrics(), app.getConfiguration());
                        observed.put("stage", "get_identifier");
                        int resourceId = resources.getIdentifier("marker", "string", PACKAGE);
                        observed.put("resource_id", resourceId);
                        if (resourceId == 0) return check(false, observed, expected, "The resource pack has no marker identifier");

                        observed.put("stage", "get_string");
                        String marker = resources.getString(resourceId);
                        observed.put("marker", marker);
                        return check(MARKER.equals(marker), observed, expected, "The resource pack resolves the fixed marker");
                    } finally {
                        assets.close();
                    }
                } catch (Exception failure) {
                    Throwable cause = failure instanceof InvocationTargetException ? failure.getCause() : failure;
                    return new Outcome("error", observed, expected, "Resource probe failed at " + observed.getString("stage"),
                            cause == null ? failure : cause);
                } finally {
                    file.delete();
                }
            }
        };
    }
}
