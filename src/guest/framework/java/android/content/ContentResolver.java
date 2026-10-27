/*
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.content;

import android.database.ContentObserver;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import java.util.ArrayList;

/** Bounded API19 client, adapted from ContentResolver and ContentService.ObserverNode. */
public abstract class ContentResolver {
    private static final ObserverNode sObservers = new ObserverNode("");

    public ContentResolver(Context context) { }

    // Exact integration overlays retain the existing provider acquisition behavior.
    public final native android.database.Cursor query(Uri uri, String[] projection,
            String selection, String[] selectionArgs, String sortOrder);
    public final native String getType(Uri uri);

    public final void registerContentObserver(Uri uri, boolean notifyForDescendents,
            ContentObserver observer) {
        if (observer == null) throw new NullPointerException("observer == null");
        if (uri == null) throw new IllegalArgumentException("uri == null");
        synchronized (sObservers) {
            sObservers.add(uri, 0, new ObserverEntry(observer, notifyForDescendents));
        }
    }

    public final void unregisterContentObserver(ContentObserver observer) {
        if (observer == null) throw new NullPointerException("observer == null");
        synchronized (sObservers) { sObservers.remove(observer); }
    }

    public void notifyChange(Uri uri, ContentObserver observer) {
        notifyChange(uri, observer, true);
    }

    public void notifyChange(final Uri uri, ContentObserver observer, boolean syncToNetwork) {
        // API19 also permits null here: it collects the entire observer tree.
        boolean wantsSelf = observer != null && observer.deliverSelfNotifications();
        ArrayList<ObserverEntry> calls = new ArrayList<ObserverEntry>();
        synchronized (sObservers) { sObservers.collect(uri, 0, observer, wantsSelf, calls); }
        for (int i = 0; i < calls.size(); i++) {
            final ContentObserver target = calls.get(i).observer;
            final boolean selfChange = target == observer;
            if (hasHandler(target)) {
                target.dispatchChange(selfChange, uri);
            } else {
                // No Binder callback thread exists; use the process main Looper.
                new Handler(Looper.getMainLooper()).post(new Runnable() {
                    public void run() { target.dispatchChange(selfChange, uri); }
                });
            }
        }
        // No sync service exists in this process.
    }

    // Read only the ROM observer's package-private Handler; no shadow state.
    private static native boolean hasHandler(ContentObserver observer);

    private static final class ObserverEntry {
        final ContentObserver observer;
        final boolean descendants;
        ObserverEntry(ContentObserver observer, boolean descendants) {
            this.observer = observer;
            this.descendants = descendants;
        }
    }

    private static final class ObserverNode {
        final String name;
        final ArrayList<ObserverNode> children = new ArrayList<ObserverNode>();
        final ArrayList<ObserverEntry> observers = new ArrayList<ObserverEntry>();
        ObserverNode(String name) { this.name = name; }
        static int count(Uri uri) { return uri == null ? 0 : uri.getPathSegments().size() + 1; }
        static String segment(Uri uri, int index) {
            return index == 0 ? uri.getAuthority() : uri.getPathSegments().get(index - 1);
        }
        void add(Uri uri, int index, ObserverEntry entry) {
            if (index == count(uri)) { observers.add(entry); return; }
            String part = segment(uri, index);
            if (part == null) throw new IllegalArgumentException("Invalid Uri used for observer");
            for (int i = 0; i < children.size(); i++) {
                ObserverNode node = children.get(i);
                if (node.name.equals(part)) { node.add(uri, index + 1, entry); return; }
            }
            ObserverNode node = new ObserverNode(part);
            children.add(node);
            node.add(uri, index + 1, entry);
        }
        boolean remove(ContentObserver observer) {
            for (int i = children.size() - 1; i >= 0; i--) {
                if (children.get(i).remove(observer)) children.remove(i);
            }
            for (int i = observers.size() - 1; i >= 0; i--) {
                if (observers.get(i).observer == observer) observers.remove(i);
            }
            return children.isEmpty() && observers.isEmpty();
        }
        void collect(Uri uri, int index, ContentObserver sender, boolean wantsSelf,
                ArrayList<ObserverEntry> calls) {
            boolean leaf = index >= count(uri);
            for (int i = 0; i < observers.size(); i++) {
                ObserverEntry entry = observers.get(i);
                if (entry.observer == sender && !wantsSelf) continue;
                if (leaf || entry.descendants) calls.add(entry);
            }
            String part = leaf ? null : segment(uri, index);
            for (int i = 0; i < children.size(); i++) {
                ObserverNode node = children.get(i);
                if (part == null || node.name.equals(part)) {
                    node.collect(uri, index + 1, sender, wantsSelf, calls);
                    if (part != null) break;
                }
            }
        }
    }
}
