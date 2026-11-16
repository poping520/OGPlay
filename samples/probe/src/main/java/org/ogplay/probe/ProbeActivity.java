package org.ogplay.probe;

import android.app.Activity;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.TextView;
import java.util.ArrayList;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

public final class ProbeActivity extends Activity {
    private static final int BG = 0xff111820;
    private static final int INK = 0xffedf3fa;
    private static final int MUTED = 0xffa6b5c4;
    private static final int ACCENT = 0xff78c6f0;
    private static final String[] CATEGORIES = {"device", "services", "runtime", "storage", "media", "graphics", "ui", "native"};
    private static final int[] CATEGORY_LABELS = {R.string.category_device, R.string.category_services, R.string.category_runtime,
            R.string.category_storage, R.string.category_media, R.string.category_graphics, R.string.category_ui, R.string.category_native};
    private static final int[] STATUS_LABELS = {R.string.pass, R.string.fail, R.string.error, R.string.crash,
            R.string.timeout, R.string.skip, R.string.info};

    private LinearLayout root;
    private LinearLayout counts;
    private LinearLayout body;
    private LinearLayout actions;
    private LinearLayout actionRow;
    private TextView heading;
    private TextView progress;
    private ListView list;
    private ScrollView reader;
    private TextView detail;
    private Rows adapter;
    private ProbeSession session;
    private DeviceInfoPage devicePage;

    private JSONArray results = new JSONArray();
    private JSONObject summary = new JSONObject();
    private String category;
    private String selected;
    private boolean devices;
    private String running;
    private String lastCrash = "";
    private boolean done;
    private String error;
    private boolean ready;
    private boolean active;
    private String interrupted;
    private long completedAt;
    private boolean previousResult;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        createUi();
        session = new ProbeSession(this);
        root.getViewTreeObserver().addOnGlobalLayoutListener(new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
            private boolean started;

            @Override public void onGlobalLayout() {
                if (!started && contentWidth() > 0 && contentHeight() > 0) {
                    started = true;
                    root.getViewTreeObserver().removeOnGlobalLayoutListener(this);
                    session.start();
                }
            }
        });
    }

    private void createUi() {
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG);
        root.setPadding(dp(12), dp(6), dp(12), dp(4));
        heading = text(20, INK);
        root.addView(heading);
        counts = new LinearLayout(this);
        counts.setOrientation(LinearLayout.VERTICAL);
        root.addView(counts);
        progress = text(13, MUTED);
        root.addView(progress);
        body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
        actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.VERTICAL);
        actions.setBaselineAligned(false);
        root.addView(actions);
        list = new ListView(this);
        list.setCacheColorHint(BG);
        adapter = new Rows();
        list.setAdapter(adapter);
        list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                Row row = adapter.rows.get(position);
                if (category == null) category = row.key;
                else selected = row.key;
                render();
            }
        });
        body.addView(list, new LinearLayout.LayoutParams(-1, -1));
        reader = new ScrollView(this);
        detail = text(14, INK);
        detail.setPadding(dp(6), dp(8), dp(6), dp(8));
        detail.setTextIsSelectable(false);
        reader.addView(detail);
        body.addView(reader, new LinearLayout.LayoutParams(-1, -1));
        devicePage = new DeviceInfoPage(this);
        body.addView(devicePage, new LinearLayout.LayoutParams(-1, -1));
        setContentView(root);
        render();
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        render();
    }

    @Override protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        ActivityFlow.deliver(request, code, data);
    }

    int contentWidth() { return root.getWidth(); }
    int contentHeight() { return root.getHeight(); }

    void refresh(JSONObject summary, JSONArray results, String running, boolean done, String crash,
            boolean active, String interrupted, long completedAt, boolean previousResult) {
        this.summary = summary;
        this.results = results;
        this.running = running;
        this.done = done;
        this.lastCrash = crash;
        this.active = active;
        this.interrupted = interrupted;
        this.completedAt = completedAt;
        this.previousResult = previousResult;
        ready = true;
        error = null;
        render();
    }

    void displayError(String message) {
        error = message;
        done = true;
        ready = true;
        active = false;
        render();
    }

    private void render() {
        boolean home = category == null && selected == null && !devices;
        boolean reading = selected != null;
        heading.setText(devices ? getString(R.string.device_info) : selected != null ? selected
                : category != null ? categoryName(category) : getString(R.string.app_name));
        counts.setVisibility(home ? View.VISIBLE : View.GONE);
        if (home) {
            counts.removeAllViews();
            int columns = getResources().getConfiguration().screenWidthDp < 420 ? 4 : Probe.STATUSES.length;
            LinearLayout row = null;
            for (int i = 0; i < Probe.STATUSES.length; i++) {
                if (i % columns == 0) {
                    row = new LinearLayout(this);
                    counts.addView(row, new LinearLayout.LayoutParams(-1, -2));
                }
                String status = Probe.STATUSES[i];
                TextView count = text(13, color(status));
                count.setGravity(Gravity.CENTER);
                count.setText(getString(STATUS_LABELS[i]) + " " + summary.optInt(status));
                row.addView(count, new LinearLayout.LayoutParams(0, -2, 1));
            }
        }
        String statusLine = error != null ? error : active ? running != null ? getString(R.string.running, running)
                : getString(R.string.preparing) : interrupted != null ? getString(R.string.interrupted, interrupted)
                : previousResult ? getString(R.string.previous_result, completionTime())
                : getString(done ? R.string.finished : R.string.not_run);
        if (lastCrash.length() > 0) statusLine += "\n" + getString(R.string.last_crash, lastCrash);
        progress.setText(statusLine);
        list.setVisibility(reading || devices ? View.GONE : View.VISIBLE);
        reader.setVisibility(reading ? View.VISIBLE : View.GONE);
        if (devicePage != null) devicePage.setVisibility(devices ? View.VISIBLE : View.GONE);
        if (reading) detail.setText(describeResult(find(selected)));
        else if (!devices) {
            adapter.rows.clear();
            if (home) categoryRows();
            else resultRows();
            adapter.notifyDataSetChanged();
        }

        actions.removeAllViews();
        actionRow = new LinearLayout(this);
        actionRow.setBaselineAligned(false);
        actions.addView(actionRow, new LinearLayout.LayoutParams(-1, -2));
        if (home) {
            if (interrupted != null) action(R.string.continue_run, ready && !active, new Runnable() {
                public void run() {
                    active = true;
                    interrupted = null;
                    session.resume();
                    render();
                }
            });
            action(interrupted != null ? R.string.restart_run : results.length() > 0 ? R.string.run_all : R.string.start_run,
                    ready && !active, new Runnable() {
                public void run() { beginAgain(null); }
            });
            actionRow = new LinearLayout(this);
            actionRow.setBaselineAligned(false);
            actions.addView(actionRow, new LinearLayout.LayoutParams(-1, -2));
            if (interrupted == null) action(R.string.run_failed, ready && !active && results.length() > 0, new Runnable() {
                public void run() { beginAgain("failed"); }
            });
            action(R.string.device_info, true, new Runnable() {
                public void run() { devices = true; devicePage.refresh(); render(); }
            });
        } else {
            action(R.string.back, true, new Runnable() {
                public void run() { goBack(); }
            });
            if (selected != null) action(R.string.run_one, ready && !active && interrupted == null && find(selected).has("status"), new Runnable() {
                public void run() { beginAgain(selected); }
            });
        }
    }

    private void beginAgain(String choice) {
        done = false;
        active = true;
        interrupted = null;
        previousResult = false;
        lastCrash = "";
        error = null;
        session.rerun(choice);
        render();
    }

    private void categoryRows() {
        for (String key : CATEGORIES) {
            int passed = 0;
            int total = 0;
            int tint = MUTED;
            for (int i = 0; i < results.length(); i++) {
                JSONObject row = results.optJSONObject(i);
                if (row == null || !key.equals(row.optString("category"))) continue;
                total++;
                String status = row.optString("status");
                if ("pass".equals(status)) passed++;
                if (color(status) == 0xffff8888) tint = color(status);
                else if ("timeout".equals(status) && tint == MUTED) tint = color(status);
            }
            if (total > 0 && total == passed) tint = color("pass");
            adapter.rows.add(new Row(key, categoryName(key), getString(R.string.category_count, passed, total), tint));
        }
    }

    private void resultRows() {
        for (int i = 0; i < results.length(); i++) {
            JSONObject row = results.optJSONObject(i);
            if (row == null || !category.equals(row.optString("category"))) continue;
            String status = row.optString("status");
            adapter.rows.add(new Row(row.optString("id"), row.optString("id"),
                    label(status) + " · " + row.optString("message"), color(status)));
        }
    }

    private JSONObject find(String id) {
        for (int i = 0; i < results.length(); i++) {
            JSONObject row = results.optJSONObject(i);
            if (row != null && id.equals(row.optString("id"))) return row;
        }
        return new JSONObject();
    }

    private String describeResult(JSONObject row) {
        return getString(R.string.scope) + "  " + row.optString("scope") + "\n"
                + getString(R.string.status) + "  " + label(row.optString("status")) + "\n"
                + row.optString("message") + "\n\nAPI\n" + pretty(row.opt("api")) + "\n\n"
                + getString(R.string.observed) + "\n" + pretty(row.opt("observed")) + "\n\n"
                + getString(R.string.expected) + "\n" + pretty(row.opt("expected")) + "\n\n"
                + getString(R.string.exception) + "\n" + pretty(row.opt("exception"));
    }

    private String completionTime() {
        if (completedAt <= 0) return "—";
        return java.text.DateFormat.getDateTimeInstance().format(new java.util.Date(completedAt));
    }

    private static String pretty(Object value) {
        try {
            if (value instanceof JSONObject) return ((JSONObject) value).toString(2);
            if (value instanceof JSONArray) return ((JSONArray) value).toString(2);
        } catch (JSONException ignored) {
            return String.valueOf(value);
        }
        return String.valueOf(value);
    }

    @Override public void onBackPressed() {
        if (!goBack()) super.onBackPressed();
    }

    private boolean goBack() {
        if (devices) devices = false;
        else if (selected != null) selected = null;
        else if (category != null) category = null;
        else return false;
        render();
        return true;
    }

    private String categoryName(String key) {
        for (int i = 0; i < CATEGORIES.length; i++) if (CATEGORIES[i].equals(key)) return getString(CATEGORY_LABELS[i]);
        return key;
    }

    private String label(String status) {
        for (int i = 0; i < Probe.STATUSES.length; i++) if (Probe.STATUSES[i].equals(status)) return getString(STATUS_LABELS[i]);
        return status;
    }

    private static int color(String status) {
        if ("pass".equals(status)) return 0xff75dda5;
        if ("fail".equals(status) || "error".equals(status) || "crash".equals(status)) return 0xffff8888;
        if ("timeout".equals(status)) return 0xfff7ce78;
        return MUTED;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView text(int size, int color) {
        TextView text = new TextView(this);
        text.setTextSize(size);
        text.setTextColor(color);
        return text;
    }

    private void action(int label, boolean enabled, final Runnable callback) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(13);
        button.setEnabled(enabled);
        button.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { callback.run(); }
        });
        actionRow.addView(button, new LinearLayout.LayoutParams(0, dp(48), 1));
    }

    private static final class Row {
        final String key;
        final String title;
        final String subtitle;
        final int tint;

        Row(String key, String title, String subtitle, int tint) {
            this.key = key;
            this.title = title;
            this.subtitle = subtitle;
            this.tint = tint;
        }
    }

    private final class Rows extends BaseAdapter {
        final ArrayList<Row> rows = new ArrayList<Row>();

        @Override public int getCount() { return rows.size(); }
        @Override public Row getItem(int position) { return rows.get(position); }
        @Override public long getItemId(int position) { return position; }

        @Override public View getView(int position, View recycled, ViewGroup parent) {
            Row row = getItem(position);
            LinearLayout line = recycled == null ? createRow() : (LinearLayout) recycled;
            line.getChildAt(0).setBackgroundColor(row.tint);
            LinearLayout content = (LinearLayout) line.getChildAt(1);
            ((TextView) content.getChildAt(0)).setText(row.title);
            ((TextView) content.getChildAt(1)).setText(row.subtitle);
            return line;
        }

        private LinearLayout createRow() {
            LinearLayout row = new LinearLayout(ProbeActivity.this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(4), dp(7), dp(4), dp(7));
            row.addView(new View(ProbeActivity.this), new LinearLayout.LayoutParams(dp(5), dp(28)));
            LinearLayout content = new LinearLayout(ProbeActivity.this);
            content.setOrientation(LinearLayout.VERTICAL);
            content.setPadding(dp(12), 0, 0, 0);
            content.addView(text(16, ACCENT));
            content.addView(text(13, MUTED));
            row.addView(content, new LinearLayout.LayoutParams(0, -2, 1));
            return row;
        }
    }
}
