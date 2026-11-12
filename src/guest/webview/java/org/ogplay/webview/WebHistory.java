package org.ogplay.webview;

import android.graphics.Bitmap;
import android.webkit.WebBackForwardList;
import android.webkit.WebHistoryItem;

import java.util.ArrayList;
import java.util.List;

final class WebHistory extends WebBackForwardList {
    static final class Item extends WebHistoryItem {
        private final String url;
        private final String title;

        Item(String url, String title) {
            this.url = url;
            this.title = title;
        }

        @Override public int getId() { return -1; }
        @Override public String getUrl() { return url; }
        @Override public String getOriginalUrl() { return url; }
        @Override public String getTitle() { return title; }
        @Override public Bitmap getFavicon() { return null; }
        @Override protected Item clone() { return this; }
    }

    private final List<Item> items;
    private final int currentIndex;

    WebHistory(List<Item> items, int currentIndex) {
        this.items = new ArrayList<Item>(items);
        this.currentIndex = currentIndex;
    }

    @Override public Item getCurrentItem() { return getItemAtIndex(currentIndex); }
    @Override public int getCurrentIndex() { return currentIndex; }
    @Override public int getSize() { return items.size(); }
    @Override public Item getItemAtIndex(int index) {
        return index >= 0 && index < items.size() ? items.get(index) : null;
    }

    @Override protected WebHistory clone() { return this; }
}
