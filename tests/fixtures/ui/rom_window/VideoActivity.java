package org.ogplay.fixture.window;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.widget.VideoView;
import android.widget.FrameLayout;

public final class VideoActivity extends Activity {
    public static int prepared, error;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        FrameLayout content = new FrameLayout(this);
        VideoView view = new VideoView(this);
        content.addView(view, new FrameLayout.LayoutParams(-1, -1));
        setContentView(content);
        view.setOnPreparedListener(new android.media.MediaPlayer.OnPreparedListener() {
            @Override public void onPrepared(android.media.MediaPlayer player) {
                prepared = player.getVideoWidth();
            }
        });
        view.setOnErrorListener(new android.media.MediaPlayer.OnErrorListener() {
            @Override public boolean onError(android.media.MediaPlayer player, int what, int extra) {
                error = what;
                return true;
            }
        });
        int video = getResources().getIdentifier("fixture", "raw", getPackageName());
        view.setVideoURI(Uri.parse("android.resource://" + getPackageName() + "/" + video));
        view.start();
    }
}
