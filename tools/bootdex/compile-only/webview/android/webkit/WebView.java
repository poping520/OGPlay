// Compile-only API 19 ROM declarations. Excluded from ogplay.jar.
package android.webkit;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;

public abstract class WebView extends android.widget.AbsoluteLayout {
    public WebView(Context context) { super(context); }

    public class PrivateAccess {
        public native void super_scrollTo(int x, int y);
        public native void super_computeScroll();
        public native boolean super_onHoverEvent(MotionEvent event);
        public native boolean super_performAccessibilityAction(int action, Bundle arguments);
        public native boolean super_performLongClick();
        public native boolean super_setFrame(int left, int top, int right, int bottom);
        public native boolean super_dispatchKeyEvent(KeyEvent event);
        public native boolean super_onGenericMotionEvent(MotionEvent event);
        public native boolean super_requestFocus(int direction, Rect previouslyFocusedRect);
        public native void super_setLayoutParams(ViewGroup.LayoutParams params);
        public native void super_onDrawVerticalScrollBar(Canvas canvas, Drawable scrollBar,
                int left, int top, int right, int bottom);
    }

    public static class HitTestResult {
        public HitTestResult() {}
    }

    public interface PictureListener {
        void onNewPicture(WebView view, android.graphics.Picture picture);
    }

    public interface FindListener {
        void onFindResultReceived(int activeMatchOrdinal, int numberOfMatches, boolean done);
    }
}
