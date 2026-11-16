package org.ogplay.probe;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;

public final class CallbackActivity extends Activity {
    private ActivityFlow flow;
    private View screen;
    private boolean rotating;
    private boolean restored;
    private boolean queued;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        flow = ActivityFlow.attach(this);
        if (flow == null) {
            finish();
            return;
        }
        try {
            rotating = getIntent().getBooleanExtra("rotate", false);
            restored = state != null && state.getInt("probe_state", -1) == 19;
            flow.restored |= restored;
            flow.event(state == null ? "create" : "recreate");
            screen = new View(this);
            screen.setBackgroundColor(0xff224460);
            setContentView(screen);
        } catch (Throwable failure) {
            flow.failed(failure);
            finish();
        }
    }

    @Override protected void onStart() {
        super.onStart();
        event("start");
    }

    @Override protected void onResume() {
        super.onResume();
        event("resume");
        if (flow == null || screen == null || queued) return;
        queued = true;
        try {
            screen.post(new Runnable() {
                @Override public void run() {
                    if (flow.cancelled) return;
                    try {
                        if (rotating && !restored) {
                            int orientation = getResources().getConfiguration().orientation;
                            setRequestedOrientation(orientation == Configuration.ORIENTATION_LANDSCAPE
                                    ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT : ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
                        } else {
                            setResult(RESULT_OK, new Intent().putExtra("answer", 19));
                            finish();
                        }
                    } catch (Throwable failure) {
                        flow.failed(failure);
                        finish();
                    }
                }
            });
        } catch (Throwable failure) {
            flow.failed(failure);
            finish();
        }
    }

    @Override protected void onPause() {
        event("pause");
        super.onPause();
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        event("save");
        state.putInt("probe_state", 19);
    }

    @Override protected void onStop() {
        event("stop");
        super.onStop();
    }

    @Override protected void onDestroy() {
        event("destroy");
        super.onDestroy();
    }

    private void event(String name) {
        if (flow != null) flow.event(name);
    }
}
