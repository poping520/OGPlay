package org.ogplay.webview;

import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup.LayoutParams;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.webkit.WebView;
import android.webkit.WebViewProvider;

final class WebViewDelegates implements WebViewProvider.ViewDelegate, WebViewProvider.ScrollDelegate {
    private final WebView view;
    private final WebView.PrivateAccess access;
    private int backgroundColor = Color.WHITE;

    WebViewDelegates(WebView view, WebView.PrivateAccess access) {
        this.view = view;
        this.access = access;
    }

    @Override public void onDraw(Canvas canvas) {
        int saved = canvas.save();
        int x = view.getScrollX();
        int y = view.getScrollY();
        canvas.clipRect(x, y, x + view.getWidth(), y + view.getHeight());
        canvas.drawColor(backgroundColor);
        canvas.restoreToCount(saved);
    }
    @Override public void setBackgroundColor(int color) {
        backgroundColor = color;
        view.invalidate();
    }

    @Override public boolean requestFocus(int direction, Rect previous) {
        return access.super_requestFocus(direction, previous);
    }

    @Override public void setLayoutParams(LayoutParams params) { access.super_setLayoutParams(params); }
    @Override public boolean setFrame(int left, int top, int right, int bottom) {
        return access.super_setFrame(left, top, right, bottom);
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) { return access.super_dispatchKeyEvent(event); }
    @Override public boolean onHoverEvent(MotionEvent event) { return access.super_onHoverEvent(event); }
    @Override public boolean onGenericMotionEvent(MotionEvent event) { return access.super_onGenericMotionEvent(event); }
    @Override public boolean performLongClick() { return access.super_performLongClick(); }
    @Override public boolean performAccessibilityAction(int action, Bundle arguments) {
        return access.super_performAccessibilityAction(action, arguments);
    }

    @Override public void onDrawVerticalScrollBar(Canvas canvas, Drawable scrollBar,
            int left, int top, int right, int bottom) {
        access.super_onDrawVerticalScrollBar(canvas, scrollBar, left, top, right, bottom);
    }

    @Override public void onOverScrolled(int x, int y, boolean clampedX, boolean clampedY) {
        access.super_scrollTo(x, y);
    }

    @Override public int computeHorizontalScrollRange() { return view.getWidth(); }
    @Override public int computeHorizontalScrollOffset() { return view.getScrollX(); }
    @Override public int computeVerticalScrollRange() { return view.getHeight(); }
    @Override public int computeVerticalScrollOffset() { return view.getScrollY(); }
    @Override public int computeVerticalScrollExtent() { return view.getHeight(); }
    @Override public void computeScroll() { access.super_computeScroll(); }

    // ROM WebView already calls super for measurement, attachment and the view lifecycle.
    @Override public void onMeasure(int widthSpec, int heightSpec) {}
    @Override public void onAttachedToWindow() {}
    @Override public void onDetachedFromWindow() {}
    @Override public void onConfigurationChanged(Configuration configuration) {}
    @Override public void onSizeChanged(int w, int h, int oldW, int oldH) {}
    @Override public void onScrollChanged(int x, int y, int oldX, int oldY) {}
    @Override public void onVisibilityChanged(View changedView, int visibility) {}
    @Override public void onWindowVisibilityChanged(int visibility) {}
    @Override public void onWindowFocusChanged(boolean focused) {}
    @Override public void onFocusChanged(boolean focused, int direction, Rect previous) {}
    @Override public void setOverScrollMode(int mode) {}
    @Override public void setScrollBarStyle(int style) {}
    @Override public void setLayerType(int type, Paint paint) {}
    @Override public void preDispatchDraw(Canvas canvas) {}

    // With no document there is no web input target or accessibility node tree.
    @Override public boolean shouldDelayChildPressedState() { return false; }
    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider() { return null; }
    @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {}
    @Override public void onInitializeAccessibilityEvent(AccessibilityEvent event) {}
    @Override public InputConnection onCreateInputConnection(EditorInfo attrs) { return null; }
    @Override public boolean onKeyMultiple(int key, int repeat, KeyEvent event) { return false; }
    @Override public boolean onKeyDown(int key, KeyEvent event) { return false; }
    @Override public boolean onKeyUp(int key, KeyEvent event) { return false; }
    @Override public boolean onTouchEvent(MotionEvent event) { return false; }
    @Override public boolean onTrackballEvent(MotionEvent event) { return false; }
    @Override public boolean requestChildRectangleOnScreen(View child, Rect rect, boolean immediate) { return false; }
}
