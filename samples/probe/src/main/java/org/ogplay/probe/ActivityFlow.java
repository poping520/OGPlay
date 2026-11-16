package org.ogplay.probe;

import android.app.Activity;
import android.content.Intent;
import java.util.ArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;

final class ActivityFlow {
    private static volatile ActivityFlow current;
    private static int serial = 100;
    final int request;
    final CountDownLatch returned = new CountDownLatch(1);
    final CountDownLatch drawn = new CountDownLatch(1);
    private final ArrayList<String> events = new ArrayList<String>();
    volatile Activity child;
    volatile Probe.Outcome frame;
    volatile Throwable error;
    volatile int resultCode;
    volatile Intent result;
    volatile boolean cancelled;
    volatile boolean restored;

    private ActivityFlow(int request) {
        this.request = request;
    }

    static synchronized ActivityFlow create() {
        ActivityFlow flow = new ActivityFlow(++serial & 0xffff);
        current = flow;
        return flow;
    }

    static ActivityFlow attach(Activity activity) {
        ActivityFlow flow = current;
        if (flow == null || flow.request != activity.getIntent().getIntExtra("request", -1) || flow.cancelled) return null;
        flow.child = activity;
        return flow;
    }

    static void deliver(int request, int code, Intent data) {
        ActivityFlow flow = current;
        if (flow == null || request != flow.request || flow.cancelled) return;
        flow.resultCode = code;
        flow.result = data;
        flow.event("activity_result");
        flow.returned.countDown();
    }

    void start(final ProbeActivity activity, final Intent intent) {
        intent.putExtra("request", request);
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (cancelled) return;
                try {
                    activity.startActivityForResult(intent, request);
                } catch (Throwable failure) {
                    failed(failure);
                    returned.countDown();
                }
            }
        });
    }

    void finish(final ProbeActivity parent) {
        cancelled = true;
        final Activity target = child;
        parent.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    if (target != null && !target.isFinishing()) target.finish();
                } catch (Throwable failure) {
                    failed(failure);
                }
            }
        });
        if (current == this) current = null;
    }

    void endDrawing(final ProbeActivity parent) {
        parent.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    if (child != null && !child.isFinishing()) {
                        child.setResult(Activity.RESULT_OK);
                        child.finish();
                    }
                } catch (Throwable failure) {
                    failed(failure);
                    returned.countDown();
                }
            }
        });
    }

    synchronized void event(String event) {
        events.add(event);
    }

    synchronized JSONArray trace() {
        return new JSONArray(events);
    }

    synchronized boolean sawInOrder(String... order) {
        int next = 0;
        for (String event : events) {
            if (event.equals(order[next]) && ++next == order.length) return true;
        }
        return false;
    }

    synchronized void failed(Throwable failure) {
        if (error == null) error = failure;
        drawn.countDown();
    }

    boolean awaitReturn(long timeout) throws InterruptedException {
        return returned.await(timeout, TimeUnit.MILLISECONDS);
    }
}
