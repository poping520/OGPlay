package org.ogplay.fixture.window;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.os.Bundle;
import android.view.View;

public final class ExitFocusActivity extends Activity {
    public static int decorGained, dialogGained, dialogLost, paused, destroyed, lateFocus;
    private int sequence;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        View content = new View(this);
        content.setBackgroundColor(Color.BLUE);
        content.setOnClickListener(view -> {
            Dialog dialog = new Dialog(this) {
                @Override public void onBackPressed() { finish(); }

                @Override public void onWindowFocusChanged(boolean focused) {
                    super.onWindowFocusChanged(focused);
                    if (destroyed != 0) ++lateFocus;
                    if (focused) dialogGained = ++sequence;
                    else dialogLost = ++sequence;
                }
            };
            View panel = new View(this);
            panel.setMinimumWidth(80);
            panel.setMinimumHeight(80);
            panel.setBackgroundColor(Color.RED);
            dialog.setContentView(panel);
            dialog.show();
        });
        setContentView(content);
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (destroyed != 0) ++lateFocus;
        if (focused) decorGained = ++sequence;
    }

    @Override protected void onPause() {
        super.onPause();
        paused = ++sequence;
    }

    @Override protected void onDestroy() {
        destroyed = ++sequence;
        super.onDestroy();
    }
}
