package org.ogplay.probe;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.webkit.WebView;
import android.widget.PopupWindow;
import android.widget.Toast;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONException;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class UiProbes {
    static void register(Map<String, Probe> probes) {
        add(probes, lifecycle("ui.activity.result_lifecycle", false));
        add(probes, lifecycle("ui.activity.orientation_restore", true));
        add(probes, new Probe("ui.dialog.alert", "in_scope", true,
                "Landroid/app/AlertDialog$Builder;.create()Landroid/app/AlertDialog;",
                "Landroid/app/Dialog;.show()V", "Landroid/app/Dialog;.dismiss()V", "Landroid/app/Dialog;.isShowing()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                AlertDialog dialog = new AlertDialog.Builder(session.activity).setTitle("Probe").setMessage("Alert")
                        .setPositiveButton(android.R.string.ok, null).create();
                dialog.show();
                boolean visible;
                try { visible = dialog.isShowing(); } finally { dialog.dismiss(); }
                return dialogResult(visible, dialog.isShowing());
            }
        });
        add(probes, new Probe("ui.dialog.progress", "in_scope", true,
                "Landroid/app/ProgressDialog;.<init>(Landroid/content/Context;)V",
                "Landroid/app/Dialog;.show()V", "Landroid/app/Dialog;.dismiss()V", "Landroid/app/Dialog;.isShowing()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ProgressDialog dialog = new ProgressDialog(session.activity);
                dialog.setMessage("Probe");
                dialog.show();
                boolean visible;
                try { visible = dialog.isShowing(); } finally { dialog.dismiss(); }
                return dialogResult(visible, dialog.isShowing());
            }
        });
        add(probes, new Probe("ui.dialog.popup", "in_scope", true,
                "Landroid/widget/PopupWindow;.showAtLocation(Landroid/view/View;III)V",
                "Landroid/widget/PopupWindow;.dismiss()V", "Landroid/widget/PopupWindow;.isShowing()Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                View child = new View(session.activity);
                child.setBackgroundColor(0xff78c6f0);
                PopupWindow popup = new PopupWindow(child, 96, 48, false);
                popup.showAtLocation(session.activity.getWindow().getDecorView(), Gravity.CENTER, 0, 0);
                boolean visible;
                try { visible = popup.isShowing(); } finally { popup.dismiss(); }
                return dialogResult(visible, popup.isShowing());
            }
        });
        add(probes, new Probe("ui.toast.show", "in_scope", true,
                "Landroid/widget/Toast;.makeText(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
                "Landroid/widget/Toast;.show()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Toast toast = Toast.makeText(session.activity, "OGPlay Probe", Toast.LENGTH_SHORT);
                toast.show();
                toast.cancel();
                return check(true, obj("show_returned", true), obj("show_returned", true), "A text Toast can be enqueued");
            }
        });
        add(probes, new Probe("ui.layout.measure_dp", "in_scope", true,
                "Landroid/view/LayoutInflater;.inflate(ILandroid/view/ViewGroup;)Landroid/view/View;",
                "Landroid/view/View;.measure(II)V", "Landroid/view/View;.layout(IIII)V",
                "Landroid/view/View;.getWidth()I", "Landroid/view/View;.getHeight()I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                View form = LayoutInflater.from(session.activity).inflate(R.layout.probe_measured, null);
                form.measure(View.MeasureSpec.makeMeasureSpec(400, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(300, View.MeasureSpec.AT_MOST));
                form.layout(0, 0, form.getMeasuredWidth(), form.getMeasuredHeight());
                View leaf = form.findViewById(R.id.measured_leaf);
                float density = session.activity.getResources().getDisplayMetrics().density;
                int w = Math.round(density * 48);
                int h = Math.round(density * 32);
                return check(leaf.getWidth() == w && leaf.getHeight() == h && form.getWidth() == 400,
                        obj("leaf_width_px", leaf.getWidth(), "leaf_height_px", leaf.getHeight(), "parent_width_px", form.getWidth()),
                        obj("leaf_width_px", w, "leaf_height_px", h, "parent_width_px", 400),
                        "XML dimensions are converted from dp and applied during measure and layout");
            }
        });
        add(probes, new Probe("ui.touch.dispatch_sequence", "in_scope", true,
                "Landroid/view/MotionEvent;.obtain(JJII[Landroid/view/MotionEvent$PointerProperties;[Landroid/view/MotionEvent$PointerCoords;IIFFIIII)Landroid/view/MotionEvent;",
                "Landroid/view/View;.dispatchTouchEvent(Landroid/view/MotionEvent;)Z",
                "Landroid/view/View;.onTouchEvent(Landroid/view/MotionEvent;)Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                TouchView view = new TouchView(session.activity);
                int[] actions = {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                        MotionEvent.ACTION_MOVE, MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT), MotionEvent.ACTION_UP};
                int[] counts = {1, 2, 2, 2, 1};
                boolean delivered = true;
                JSONArray expected = new JSONArray();
                long timestamp = SystemClock.uptimeMillis();
                for (int i = 0; i < actions.length; i++) {
                    MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[counts[i]];
                    MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[counts[i]];
                    for (int n = 0; n < counts[i]; n++) {
                        properties[n] = new MotionEvent.PointerProperties();
                        properties[n].id = n + 4;
                        properties[n].toolType = MotionEvent.TOOL_TYPE_FINGER;
                        coords[n] = new MotionEvent.PointerCoords();
                        coords[n].x = 10 + i + 20 * n;
                        coords[n].y = 20 + i + 10 * n;
                        coords[n].pressure = 1;
                        coords[n].size = 1;
                    }
                    MotionEvent event = MotionEvent.obtain(timestamp, timestamp + i, actions[i], counts[i], properties, coords,
                            0, 0, 1, 1, 0, 0, android.view.InputDevice.SOURCE_TOUCHSCREEN, 0);
                    JSONArray points = new JSONArray();
                    for (int n = 0; n < counts[i]; n++) {
                        points.put(new JSONArray().put(properties[n].id).put((int) coords[n].x).put((int) coords[n].y));
                    }
                    expected.put(new JSONArray().put(actions[i]).put(points));
                    try { delivered &= view.dispatchTouchEvent(event); } finally { event.recycle(); }
                }
                return check(delivered && view.events.toString().equals(expected.toString()),
                        obj("consumed", delivered, "events", view.events), obj("consumed", true, "events", expected),
                        "A View receives the original action, pointer index, ID and coordinates for every synthetic gesture event");
            }
        });
        add(probes, new Probe("ui.intent.explicit_self", "in_scope", false,
                "Landroid/content/Intent;.resolveActivity(Landroid/content/pm/PackageManager;)Landroid/content/ComponentName;",
                "Landroid/content/pm/PackageManager;.resolveActivity(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Intent self = new Intent(session.activity, ProbeActivity.class);
                android.content.ComponentName component = self.resolveActivity(session.activity.getPackageManager());
                ResolveInfo item = session.activity.getPackageManager().resolveActivity(self, 0);
                String expected = ProbeActivity.class.getName();
                String real = item == null || item.activityInfo == null ? null : item.activityInfo.name;
                return check(self.getComponent().equals(component) && expected.equals(real),
                        obj("intent_component", component == null ? null : component.getClassName(), "manager_component", real),
                        obj("activity", expected), "Intent and PackageManager agree on the explicit application component");
            }
        });
        add(probes, new Probe("ui.intent.http_resolution", "info", false,
                "Landroid/content/pm/PackageManager;.resolveActivity(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Intent link = new Intent(Intent.ACTION_VIEW, Uri.parse("http://localhost/"));
                ResolveInfo target = session.activity.getPackageManager().resolveActivity(link, 0);
                return info(obj("handler", target == null || target.activityInfo == null ? null : target.activityInfo.name),
                        "Record whether an HTTP handler can be resolved without launching one");
            }
        });
        add(probes, new Probe("ui.webview.load_data", "info", true,
                "Landroid/webkit/WebView;.<init>(Landroid/content/Context;)V",
                "Landroid/webkit/WebView;.loadData(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                "Landroid/webkit/WebView;.getUrl()Ljava/lang/String;", "Landroid/webkit/WebView;.destroy()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                WebView web = new WebView(session.activity);
                try {
                    web.loadData("<p>OGPlay Probe</p>", "text/html", "UTF-8");
                    return info(obj("constructed", true, "load_data_returned", true, "url", web.getUrl(), "progress", web.getProgress()),
                            "Record WebView construction and loadData behavior outside the rendering assertion");
                } finally { web.destroy(); }
            }
        });
    }

    private static Probe lifecycle(String id, final boolean rotate) {
        return new Probe(id, "in_scope", false,
                "Landroid/app/Activity;.startActivityForResult(Landroid/content/Intent;I)V",
                "Landroid/app/Activity;.onActivityResult(IILandroid/content/Intent;)V",
                "Landroid/app/Activity;.setRequestedOrientation(I)V", "Landroid/app/Activity;.onSaveInstanceState(Landroid/os/Bundle;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                ActivityFlow flow = ActivityFlow.create();
                Intent intent = new Intent(session.activity, CallbackActivity.class).putExtra("rotate", rotate);
                flow.start(session.activity, intent);
                try {
                    boolean delivered = flow.awaitReturn(3000);
                    if (flow.error != null) return error(flow.error);
                    int answer = flow.result == null ? -1 : flow.result.getIntExtra("answer", -1);
                    String[] ordered = rotate ? new String[] {"create", "start", "resume", "save", "recreate", "resume", "activity_result"}
                            : new String[] {"create", "start", "resume", "pause", "activity_result"};
                    boolean correct = delivered && flow.resultCode == Activity.RESULT_OK && answer == 19
                            && flow.sawInOrder(ordered) && (!rotate || flow.restored);
                    return check(correct, obj("callbacks", flow.trace(), "result_delivered", delivered,
                                    "result_code", flow.resultCode, "answer", answer, "restored", flow.restored),
                            obj("ordered_subsequence", new JSONArray(java.util.Arrays.asList(ordered)), "result_delivered", true,
                                    "result_code", Activity.RESULT_OK, "answer", 19, "restored", rotate),
                            rotate ? "Activity recreates after orientation changes and restores its saved value"
                                    : "An explicit child Activity returns a value after its lifecycle callbacks");
                } finally { flow.finish(session.activity); }
            }
        };
    }

    private static Probe.Outcome dialogResult(boolean visible, boolean after) throws JSONException {
        return Probe.check(visible && !after, obj("after_show", visible, "after_dismiss", after),
                obj("after_show", true, "after_dismiss", false), "The visible state follows show and dismiss");
    }

    private static final class TouchView extends View {
        final JSONArray events = new JSONArray();

        TouchView(Activity context) { super(context); }

        @Override public boolean onTouchEvent(MotionEvent event) {
            JSONArray points = new JSONArray();
            for (int n = 0; n < event.getPointerCount(); n++) {
                points.put(new JSONArray().put(event.getPointerId(n)).put((int) event.getX(n)).put((int) event.getY(n)));
            }
            events.put(new JSONArray().put(event.getAction()).put(points));
            return true;
        }
    }
}
