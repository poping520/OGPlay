package org.ogplay.fixture.window;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.os.Bundle;
import android.view.View;

public final class WindowActivity extends Activity {
    public static int entryOrientation;

    @Override public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        entryOrientation = getRequestedOrientation();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT);
        setContentView(new ColorView(this));
    }

    private static final class ColorView extends View {
        private final Paint paint = new Paint();
        private boolean changed;
        ColorView(final Activity activity) {
            super(activity);
            setOnClickListener(new OnClickListener() {
                @Override public void onClick(View view) {
                    changed = true;
                    activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
                    invalidate(8, 8, 24, 24);
                }
            });
        }

        @Override protected void onDraw(Canvas canvas) {
            canvas.drawColor(changed ? 0xff972f42 : 0xff1b395d);
            paint.setColor(changed ? 0xff70dc72 : 0xfff8a22a);
            canvas.drawRect(8, 8, 24, 24, paint);
        }
    }
}
