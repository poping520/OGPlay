package org.ogplay.probe;

import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.Probe.obj;

final class ProbeSession {
    static final String VERSION = "1.0 (1)";
    static final String TAG = "OGProbe";
    private final File directory = new File(Environment.getExternalStorageDirectory(), "OGPlayProbe");
    private final File progress = new File(directory, "progress.json");
    private final File report = new File(directory, "report.json");
    private final File journal = new File(directory, "results");
    private final Handler main = new Handler(Looper.getMainLooper());
    private final LinkedHashMap<String, Probe> probes = new LinkedHashMap<String, Probe>();
    private final TreeMap<String, JSONObject> results = new TreeMap<String, JSONObject>();
    private final TreeMap<String, JSONObject> environment = new TreeMap<String, JSONObject>();
    final ProbeActivity activity;
    JSONObject expect;
    private JSONObject autorun;
    private JSONArray selected;
    private int next;
    private int run;
    private int batch;
    private long lastPublish;
    private String lastCrash = "";
    private String token = "";
    private String prefsSetupError = "";
    private String androidIdBeforeRestart;
    private boolean restarted;
    private boolean exitAfter;
    private volatile boolean busy;
    private boolean automatic;
    private JSONObject pending;
    private long completedAt;
    private boolean previousResult;

    ProbeSession(ProbeActivity activity) {
        this.activity = activity;
    }

    void start() {
        launch(null, true);
    }

    void rerun(String id) {
        launch(id, false);
    }

    void resume() {
        launch("resume", false);
    }

    private void launch(final String selection, final boolean opening) {
        if (busy) return;
        busy = true;
        new Thread(new Runnable() {
            @Override public void run() {
                try {
                    execute(selection, opening);
                } catch (Throwable failure) {
                    broken(failure);
                }
            }
        }, "probe-watchdog").start();
    }

    private void execute(String selection, boolean opening) throws Exception {
        if (!directory.isDirectory() && !directory.mkdirs() && !directory.isDirectory()) {
            throw new IOException("Cannot create " + directory);
        }
        if (opening) {
            File config = new File(directory, "autorun.json");
            automatic = config.exists();
            autorun = config.exists() ? ProbeFiles.json(config) : new JSONObject();
            expect = autorun.optJSONObject("expect");
            if (expect == null) expect = new JSONObject();
            exitAfter = autorun.optBoolean("exit");
            PhaseOne.register(probes, autorun.optBoolean("selftest"));
            DeviceProbes.register(probes);
            ServiceProbes.register(probes);
            RuntimeProbes.register(probes);
            StorageProbes.register(probes);
            MediaProbes.register(probes);
            GraphicsProbes.register(probes);
            UiProbes.register(probes);
            NativeProbes.register(probes);
            // Cross-restart assertions run after the operations which may force a relaunch.
            probes.put(DeviceProbes.IDENTITY, probes.remove(DeviceProbes.IDENTITY));
            probes.put(PhaseOne.PERSIST, probes.remove(PhaseOne.PERSIST));
        }
        if (!journal.isDirectory() && !journal.mkdirs() && !journal.isDirectory()) {
            throw new IOException("Cannot create " + journal);
        }
        JSONObject previous = opening && progress.exists() ? ProbeFiles.json(progress) : null;
        boolean compatible = previous != null && VERSION.equals(previous.optString("probe_version"))
                && autorun.optString("session").equals(previous.optString("session"));
        boolean resume = compatible && !previous.optBoolean("complete");
        if (opening && compatible) batch = previous.optInt("batch");
        if (opening && !automatic) {
            if (compatible) {
                restore(previous);
                if (resume) pending = previous;
                else completedAt = previous.optLong("completed_at_ms", progress.lastModified());
            }
            if (!resume && report.exists()) {
                try {
                    JSONObject saved = ProbeFiles.json(report);
                    if ("ogplay.probe.report/1".equals(saved.optString("schema"))
                            && VERSION.equals(saved.optString("probe_version"))) {
                        JSONArray rows = saved.getJSONArray("results");
                        for (int i = 0; i < rows.length(); i++) {
                            JSONObject row = rows.getJSONObject(i);
                            if (probes.containsKey(row.getString("id"))) results.put(row.getString("id"), row);
                        }
                        completedAt = saved.optLong("completed_at_ms", report.lastModified());
                        rebuildEnvironment();
                    }
                } catch (IOException failure) {
                    Log.w(TAG, "Cannot read last report: " + failure);
                } catch (JSONException failure) {
                    Log.w(TAG, "Cannot decode last report: " + failure);
                }
            }
            if (!compatible) recoverJournal();
            previousResult = pending == null && results.size() > 0;
            busy = false;
            publish(null, previousResult);
            return;
        }
        if (!opening && "resume".equals(selection)) {
            previous = pending;
            if (previous == null) throw new IOException("No interrupted run to resume");
            resume = true;
        }
        pending = null;
        previousResult = false;
        completedAt = 0;
        if (!automatic && !resume && selection == null) {
            // A fresh full manual run must not resurrect old rows after a crash.
            results.clear();
            environment.clear();
            lastCrash = "";
            File[] entries = journal.listFiles();
            if (entries == null) throw new IOException("Cannot list " + journal);
            for (File entry : entries) {
                if (entry.getName().endsWith(".json") && !entry.delete()) throw new IOException("Cannot delete " + entry);
            }
            if (report.exists() && !report.delete()) throw new IOException("Cannot delete " + report);
        }
        if (resume) {
            restore(previous);
            run++;
            restarted = true;
        } else {
            if (opening && compatible) lastCrash = previous.optString("last_crash");
            run = 1;
            batch++;
            restarted = false;
            selected = select(selection);
            next = 0;
            preparePrefs();
            androidIdBeforeRestart = null;
            for (int i = 0; i < selected.length(); i++) {
                if (DeviceProbes.IDENTITY.equals(selected.optString(i))) {
                    try {
                        androidIdBeforeRestart = DeviceProbes.androidId(activity);
                    } catch (Throwable ignored) {
                        // The identity probe reports its own read failure.
                    }
                    break;
                }
            }
            ProbeFiles.atomic(plan(batch), ProbeFiles.encode(obj("selected", selected)));
        }
        log(obj("event", "begin", "probe_version", VERSION, "run", run, "selected", selected));
        if (resume && !previous.isNull("running")) {
            String id = previous.getString("running");
            if (!id.equals(selected.getString(next))) throw new IOException("Invalid running checkpoint: " + id);
            File path = rowPath(id);
            JSONObject saved = path.exists() ? ProbeFiles.json(path) : null;
            if (saved != null && saved.optInt("batch") == batch && saved.optInt("index", -1) == next) {
                next++;
            } else {
                lastCrash = id;
                Probe probe = probes.get(id);
                complete(probe, new Probe.Outcome("crash", obj("checkpoint_run", run - 1),
                        null, "Process stopped with this probe active", null), 0);
            }
        }
        checkpoint(null, false);
        publish(null, false);
        while (next < selected.length()) {
            Probe probe = probes.get(selected.getString(next));
            checkpoint(probe.id, false);
            publish(probe.id, false);
            if (executeProbe(probe)) return;
        }
        completedAt = System.currentTimeMillis();
        JSONObject document = obj("schema", "ogplay.probe.report/1", "probe_version", VERSION,
                "runs", run, "environment", environment(), "summary", summary(), "results", rows(),
                "completed_at_ms", completedAt);
        ProbeFiles.atomic(report, ProbeFiles.encode(document));
        // Preserve the completion time when only the plan and result journal survive.
        ProbeFiles.atomic(plan(batch), ProbeFiles.encode(obj("selected", selected, "completed_at_ms", completedAt)));
        checkpoint(null, true);
        log(obj("event", "end", "run", run, "summary", document.getJSONObject("summary")));
        busy = false;
        publish(null, true);
        if (exitAfter) {
            main.post(new Runnable() {
                @Override public void run() {
                    activity.finish();
                    System.exit(0);
                }
            });
        }
    }

    String identityBeforeRestart() {
        return restarted ? androidIdBeforeRestart : null;
    }

    boolean restarted() {
        return restarted;
    }

    private void preparePrefs() {
        token = "";
        prefsSetupError = "";
        for (int i = 0; i < selected.length(); i++) {
            if (!PhaseOne.PERSIST.equals(selected.optString(i))) continue;
            try {
                String value = Long.toHexString(System.nanoTime());
                if (!activity.getSharedPreferences("probe", 0).edit().putString("restart_token", value).commit()) {
                    throw new IOException("SharedPreferences.commit returned false");
                }
                token = value;
            } catch (Throwable failure) {
                // A missing prefs implementation must not prevent the other probes from starting.
                prefsSetupError = failure.toString();
            }
            break;
        }
    }

    Probe.Outcome checkPrefs() throws JSONException {
        if (!restarted) return Probe.skip("No restart was observed in this run");
        if (token.length() == 0) {
            return Probe.check(false, obj("setup_error", prefsSetupError), obj("token_saved", true),
                    "Could not save a token before restart");
        }
        String actual = activity.getSharedPreferences("probe", 0).getString("restart_token", null);
        return Probe.check(token.equals(actual), obj("token", actual), obj("token", token),
                "Read the committed token after restarting the same installation");
    }

    private JSONArray select(String selection) {
        JSONArray ids = new JSONArray();
        JSONArray filters = autorun.optJSONArray("filter");
        for (String id : probes.keySet()) {
            JSONObject old = results.get(id);
            boolean choose = selection == null || selection.equals(id);
            if ("failed".equals(selection)) {
                String status = old == null ? "" : old.optString("status");
                choose = "fail".equals(status) || "error".equals(status) || "crash".equals(status)
                        || "timeout".equals(status);
            }
            if (!choose) continue;
            if (filters == null || filters.length() == 0) {
                ids.put(id);
                continue;
            }
            for (int i = 0; i < filters.length(); i++) {
                if (id.startsWith(filters.optString(i))) {
                    ids.put(id);
                    break;
                }
            }
        }
        return ids;
    }

    private void restore(JSONObject previous) throws JSONException, IOException {
        run = previous.getInt("run");
        selected = ProbeFiles.json(plan(batch)).getJSONArray("selected");
        next = previous.getInt("next");
        if (next < 0 || next > selected.length()) throw new IOException("Invalid probe cursor");
        for (int i = 0; i < selected.length(); i++) {
            if (!probes.containsKey(selected.getString(i))) throw new IOException("Unknown probe in checkpoint");
        }
        lastCrash = previous.optString("last_crash");
        token = previous.optString("token");
        prefsSetupError = previous.optString("prefs_setup_error");
        androidIdBeforeRestart = previous.isNull("android_id_before_restart") ? null : previous.optString("android_id_before_restart");
        File[] entries = journal.listFiles();
        if (entries == null) throw new IOException("Cannot list " + journal);
        for (File entry : entries) {
            if (!entry.getName().endsWith(".json")) continue;
            JSONObject saved = ProbeFiles.json(entry);
            if (!autorun.optString("session").equals(saved.optString("session"))) continue;
            JSONObject row = saved.getJSONObject("result");
            String id = row.getString("id");
            if (!probes.containsKey(id)) continue;
            if (saved.optInt("batch") < batch || saved.optInt("batch") == batch
                    && (saved.optInt("index") < next || id.equals(previous.optString("running")) && saved.optInt("index") == next)) {
                results.put(id, row);
            }
        }
        rebuildEnvironment();
    }

    private void rebuildEnvironment() throws JSONException {
        environment.clear();
        // Replay in probe order, not VFS enumeration order.
        for (String id : probes.keySet()) {
            JSONObject row = results.get(id);
            if (row != null) applyEnvironment(id, row.opt("observed"));
        }
    }

    private void recoverJournal() throws Exception {
        File[] entries = journal.listFiles();
        if (entries == null) throw new IOException("Cannot list " + journal);
        for (File entry : entries) {
            if (!entry.getName().endsWith(".json")) continue;
            JSONObject saved = ProbeFiles.json(entry);
            if (saved.optString("session").length() == 0) batch = Math.max(batch, saved.optInt("batch"));
        }
        if (results.size() > 0 || batch == 0 || !plan(batch).exists()) return;
        JSONObject plan = ProbeFiles.json(plan(batch));
        JSONArray ids = plan.getJSONArray("selected");
        long journalTime = 0;
        for (int i = 0; i < ids.length(); i++) {
            File entry = rowPath(ids.getString(i));
            if (!entry.exists()) return;
            JSONObject saved = ProbeFiles.json(entry);
            if (saved.optString("session").length() != 0 || saved.optInt("batch") != batch) return;
            journalTime = Math.max(journalTime, entry.lastModified());
        }
        completedAt = plan.optLong("completed_at_ms", journalTime);
        // The entire last plan is durable: recover even if report/progress was lost.
        for (File entry : entries) {
            if (!entry.getName().endsWith(".json")) continue;
            JSONObject saved = ProbeFiles.json(entry);
            JSONObject row = saved.getJSONObject("result");
            if (saved.optString("session").length() == 0 && probes.containsKey(row.getString("id"))) {
                results.put(row.getString("id"), row);
            }
        }
        rebuildEnvironment();
    }

    private boolean executeProbe(final Probe probe) throws Exception {
        final CountDownLatch finished = new CountDownLatch(1);
        final Probe.Outcome[] result = new Probe.Outcome[1];
        Runnable task = new Runnable() {
            @Override public void run() {
                try {
                    result[0] = probe.run(ProbeSession.this);
                } catch (Throwable failure) {
                    result[0] = Probe.error(failure);
                } finally {
                    finished.countDown();
                }
            }
        };
        long start = SystemClock.elapsedRealtime();
        if (probe.main) {
            if (!main.post(task)) throw new IOException("The UI queue is closed");
        } else {
            Thread worker = new Thread(task, probe.id);
            worker.setDaemon(true);
            worker.start();
        }
        boolean done = finished.await(probe.timeoutMs, TimeUnit.MILLISECONDS);
        Probe.Outcome outcome = done ? result[0] : new Probe.Outcome("timeout", null,
                obj("timeout_ms", probe.timeoutMs), "Probe did not return before its deadline", null);
        if (!done && probe.main) main.removeCallbacks(task);
        complete(probe, outcome, SystemClock.elapsedRealtime() - start);
        if (!done && probe.main) {
            Process.killProcess(Process.myPid());
            System.exit(1);
            return true;
        }
        return false;
    }

    private File plan(int number) {
        return new File(directory, "plan-" + number + ".json");
    }

    private File rowPath(String id) {
        return new File(journal, id + ".json");
    }

    private void checkpoint(String running, boolean complete) throws Exception {
        JSONObject state = obj("probe_version", VERSION, "session", autorun.optString("session"),
                "batch", batch, "running", running, "run", run, "next", next, "complete", complete,
                "last_crash", lastCrash, "token", token, "prefs_setup_error", prefsSetupError,
                "android_id_before_restart", androidIdBeforeRestart, "completed_at_ms", completedAt);
        ProbeFiles.atomic(progress, ProbeFiles.encode(state));
    }

    private void applyEnvironment(String id, Object observed) throws JSONException {
        PhaseOne.environment(id, observed, environment);
        DeviceProbes.environment(id, observed, environment);
        ServiceProbes.environment(id, observed, environment);
    }

    private void complete(Probe probe, Probe.Outcome outcome, long duration) throws Exception {
        JSONObject row = probe.result(outcome, Math.max(0, duration));
        JSONObject saved = obj("session", autorun.optString("session"), "batch", batch, "index", next, "result", row);
        ProbeFiles.atomic(rowPath(probe.id), ProbeFiles.encode(saved));
        results.put(probe.id, row);
        applyEnvironment(probe.id, outcome.observed);
        next++;
        checkpoint(null, false);
        JSONObject entry = obj("event", "result", "run", run, "result", row);
        if (logSize(entry) > 3500) {
            row = probe.result(outcome, Math.max(0, duration));
            row.put("observed", JSONObject.NULL);
            row.put("truncated", true);
            entry.put("result", row);
        }
        if (logSize(entry) > 3500) {
            row.put("exception", exceptionHead(row.optJSONObject("exception")));
            row.put("expected", JSONObject.NULL);
        }
        if (logSize(entry) > 3500) row.put("message", "See report.json");
        log(entry);
    }

    private JSONObject exceptionHead(JSONObject exception) throws JSONException {
        if (exception == null) return null;
        String message = exception.optString("message");
        return obj("class", exception.optString("class"),
                "message", message.substring(0, Math.min(message.length(), 100)),
                "stack", new JSONArray(), "causes", new JSONArray());
    }

    private JSONArray rows() {
        JSONArray array = new JSONArray();
        for (JSONObject row : results.values()) array.put(row);
        return array;
    }

    private JSONObject environment() throws JSONException {
        JSONObject data = new JSONObject();
        for (Map.Entry<String, JSONObject> group : environment.entrySet()) {
            if ("locale".equals(group.getKey())) {
                data.put("locale", group.getValue().opt("java_locale"));
                data.put("locale_details", group.getValue());
            } else data.put(group.getKey(), group.getValue());
        }
        return data;
    }

    private JSONObject summary() throws JSONException {
        JSONObject data = obj("total", results.size());
        for (String status : Probe.STATUSES) data.put(status, 0);
        for (JSONObject row : results.values()) {
            String status = row.getString("status");
            data.put(status, data.getInt(status) + 1);
        }
        return data;
    }

    private void publish(final String running, final boolean done) throws JSONException {
        long now = SystemClock.elapsedRealtime();
        if (running != null && now - lastPublish < 200) return;
        lastPublish = now;
        final JSONObject state = summary();
        final JSONArray rows = rows();
        final String crash = lastCrash;
        final boolean active = busy;
        final boolean old = previousResult;
        final long finished = completedAt;
        final String interrupted = pending == null ? null : pending.isNull("running")
                ? selected.optString(next, selected.optString(Math.max(0, next - 1))) : pending.optString("running");
        main.post(new Runnable() {
            @Override public void run() {
                activity.refresh(state, rows, running, done, crash, active, interrupted, finished, old);
            }
        });
    }

    private void broken(Throwable failure) {
        busy = false;
        final String message = failure.toString();
        try {
            log(obj("event", "framework_error", "exception", exceptionHead(Probe.exception(failure))));
        } catch (Exception ignored) {
            Log.e(TAG, message);
        }
        main.post(new Runnable() {
            @Override public void run() {
                activity.displayError(message);
            }
        });
        if (exitAfter) System.exit(2);
    }

    private static int logSize(JSONObject object) throws IOException, JSONException {
        return ProbeFiles.encode(object).getBytes("UTF-8").length;
    }

    private static void log(JSONObject object) throws IOException, JSONException {
        if (logSize(object) > 3500) {
            object = obj("event", object.getString("event"), "run", object.optInt("run"), "truncated", true);
        }
        Log.i(TAG, ProbeFiles.encode(object));
    }
}
