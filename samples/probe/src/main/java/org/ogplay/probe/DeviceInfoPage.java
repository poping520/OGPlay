package org.ogplay.probe;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.util.Locale;

/** Framework-only cards; all collection (including EGL) runs off the UI thread. */
final class DeviceInfoPage extends ScrollView {
    private final ProbeActivity activity;
    private final LinearLayout cards;
    private boolean loading;

    DeviceInfoPage(ProbeActivity activity) {
        super(activity);
        this.activity = activity;
        setFillViewport(true);
        cards = new LinearLayout(activity);
        cards.setOrientation(LinearLayout.VERTICAL);
        addView(cards, new ScrollView.LayoutParams(-1, -2));
    }

    void refresh() {
        if (loading) return;
        loading = true;
        cards.removeAllViews();
        cards.addView(text(activity.getString(R.string.loading_device), 16, 0xffa6b5c4));
        scrollTo(0, 0);
        new Thread(new Runnable() {
            @Override public void run() {
                final DeviceInfo snapshot = DeviceInfo.collect(activity);
                activity.runOnUiThread(new Runnable() {
                    @Override public void run() {
                        loading = false;
                        if (!activity.isFinishing()) show(snapshot);
                    }
                });
            }
        }, "device-info-reader").start();
    }

    private void show(DeviceInfo snapshot) {
        cards.removeAllViews();
        for (DeviceInfo.Section section : snapshot.sections) {
            LinearLayout card = new LinearLayout(activity);
            card.setOrientation(LinearLayout.VERTICAL);
            card.setPadding(dp(12), dp(12), dp(12), dp(12));
            card.setBackgroundColor(0xff1e2a36);
            LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1, -2);
            layout.setMargins(0, dp(6), 0, dp(6));
            cards.addView(card, layout);
            TextView title = text(activity.getString(section.title), 18, 0xff78c6f0);
            title.setPadding(0, 0, 0, dp(8));
            card.addView(title);
            if (section.failure != null) {
                addRow(card, R.string.unavailable, section.failure.getClass().getName() + "\n" + DeviceInfo.present(section.failure.getMessage()));
                continue;
            }
            for (DeviceInfo.Row row : section.rows) addRow(card, row.label, format(row));
            if (section.extensions != null) {
                final LinearLayout extensions = new LinearLayout(activity);
                extensions.setOrientation(LinearLayout.VERTICAL);
                extensions.setVisibility(GONE);
                for (String extension : section.extensions) addRow(extensions, R.string.extension, extension);
                Button toggle = button(activity.getString(R.string.extensions_count, section.extensions.length));
                toggle.setOnClickListener(new OnClickListener() {
                    @Override public void onClick(View view) { extensions.setVisibility(extensions.getVisibility() == GONE ? VISIBLE : GONE); }
                });
                card.addView(toggle);
                card.addView(extensions);
            }
        }
        Button refresh = button(activity.getString(R.string.refresh_device));
        refresh.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View view) { refresh(); }
        });
        cards.addView(refresh);
        final TextView raw = text(pretty(snapshot), 12, 0xffa6b5c4);
        raw.setVisibility(GONE);
        Button toggle = button(activity.getString(R.string.raw_data));
        toggle.setOnClickListener(new OnClickListener() {
            @Override public void onClick(View view) { raw.setVisibility(raw.getVisibility() == GONE ? VISIBLE : GONE); }
        });
        cards.addView(toggle);
        cards.addView(raw);
    }

    private String pretty(DeviceInfo snapshot) {
        try { return snapshot.raw.toString(2); } catch (Exception ignored) { return snapshot.raw.toString(); }
    }

    private String format(DeviceInfo.Row row) {
        if (row.value == null || row.value == org.json.JSONObject.NULL) return "—";
        if (row.format == DeviceInfo.BOOLEAN) return activity.getString(Boolean.TRUE.equals(row.value) ? R.string.yes : R.string.no);
        if (!(row.value instanceof Number)) return DeviceInfo.present(row.value);
        Number value = (Number) row.value;
        switch (row.format) {
            case DeviceInfo.BYTES:
                double bytes = value.doubleValue();
                return bytes >= 1073741824L ? String.format(Locale.US, "%.1f GB", bytes / 1073741824L)
                        : String.format(Locale.US, "%.1f MB", bytes / 1048576L);
            case DeviceInfo.MHZ: return String.format(Locale.US, "%.1f MHz", value.doubleValue() / 1000);
            case DeviceInfo.GLES: return (value.intValue() >> 16) + "." + (value.intValue() & 65535);
            case DeviceInfo.DURATION:
                long seconds = value.longValue() / 1000;
                return String.format(Locale.US, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
            default: return DeviceInfo.present(value);
        }
    }

    private void addRow(LinearLayout card, int label, final String value) {
        final String name = activity.getString(label);
        LinearLayout row = new LinearLayout(activity);
        row.setPadding(0, dp(5), 0, dp(5));
        TextView key = text(name, 13, 0xffa6b5c4);
        key.setPadding(0, 0, dp(10), 0);
        row.addView(key, new LinearLayout.LayoutParams(0, -2, 0.38f));
        TextView content = text(value, 14, 0xffedf3fa);
        row.addView(content, new LinearLayout.LayoutParams(0, -2, 0.62f));
        row.setOnLongClickListener(new OnLongClickListener() {
            @Override public boolean onLongClick(View view) {
                try {
                    ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
                    if (clipboard == null) throw new IllegalStateException();
                    clipboard.setPrimaryClip(ClipData.newPlainText(name, value));
                    Toast.makeText(activity, R.string.value_copied, Toast.LENGTH_SHORT).show();
                } catch (Throwable failure) {
                    Toast.makeText(activity, R.string.clipboard_unavailable, Toast.LENGTH_SHORT).show();
                }
                return true;
            }
        });
        card.addView(row, new LinearLayout.LayoutParams(-1, -2));
    }

    private TextView text(String value, int size, int color) {
        TextView text = new TextView(activity);
        text.setText(value);
        text.setTextSize(size);
        text.setTextColor(color);
        return text;
    }

    private Button button(String label) {
        Button button = new Button(activity);
        button.setText(label);
        return button;
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
}
