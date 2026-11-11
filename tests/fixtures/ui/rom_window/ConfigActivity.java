package org.ogplay.fixture.window;

import android.app.Activity;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.os.Bundle;
import android.view.View;

public class ConfigActivity extends Activity {
    public static int creates, changes, restored, order, width, height, metricWidth, orientation;
    private void event(int value) { order = order * 10 + value; }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        ++creates;
        if (saved != null) { event(1); restored = saved.getInt("value"); }
        View content = new View(this) {
            @Override protected void onDraw(Canvas canvas) {
                canvas.drawColor(0xff20a040);
                width = getWidth(); height = getHeight();
                metricWidth = getResources().getDisplayMetrics().widthPixels;
                orientation = getResources().getConfiguration().orientation;
            }
            @Override public boolean performClick() {
                super.performClick(); order = 0; setRequestedOrientation(0); return true;
            }
        };
        content.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { order = 0; setRequestedOrientation(0); }
        });
        setContentView(content);
    }
    @Override protected void onStart() { super.onStart(); if (creates > 1) event(2); }
    @Override protected void onResume() { super.onResume(); if (creates > 1) event(3); }
    @Override protected void onPause() { event(4); super.onPause(); }
    @Override protected void onSaveInstanceState(Bundle out) { event(5); out.putInt("value", 73); super.onSaveInstanceState(out); }
    @Override protected void onStop() { event(6); super.onStop(); }
    @Override protected void onDestroy() { event(7); super.onDestroy(); }
    @Override protected void onRestoreInstanceState(Bundle saved) { super.onRestoreInstanceState(saved); event(8); }
    @Override public void onConfigurationChanged(Configuration config) { super.onConfigurationChanged(config); ++changes; }
}
