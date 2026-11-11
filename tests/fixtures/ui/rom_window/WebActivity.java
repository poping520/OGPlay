package org.ogplay.fixture.window;

import android.app.Activity;
import android.os.Bundle;
import android.view.TextureView;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;

public final class WebActivity extends Activity {
    public static int disabledLoads;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        FrameLayout content = new FrameLayout(this);
        WebView web = new WebView(this);
        web.setWebViewClient(new WebViewClient() {
            @Override public void onReceivedError(WebView view, int code, String text, String url) {
                ++disabledLoads;
            }
        });
        web.setScrollBarStyle(WebView.SCROLLBARS_INSIDE_OVERLAY);
        content.addView(web, new FrameLayout.LayoutParams(-1, -1));
        content.addView(new TextureView(this), new FrameLayout.LayoutParams(32, 32));
        setContentView(content);
        web.loadUrl("https://example.invalid/");
    }
}
