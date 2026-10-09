/*
 * Copyright (C) 2007 The Android Open Source Project
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

package android.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import android.widget.RemoteViews;

/** API19 notification values; system templates and parcel transport are unavailable. */
public class Notification implements Parcelable {
    public static final int DEFAULT_SOUND = 1;
    public static final int DEFAULT_VIBRATE = 2;
    public static final int DEFAULT_LIGHTS = 4;
    public static final int DEFAULT_ALL = -1;
    public static final int STREAM_DEFAULT = -1;
    public static final int FLAG_SHOW_LIGHTS = 0x00000001;
    public static final int FLAG_ONGOING_EVENT = 0x00000002;
    public static final int FLAG_INSISTENT = 0x00000004;
    public static final int FLAG_ONLY_ALERT_ONCE = 0x00000008;
    public static final int FLAG_AUTO_CANCEL = 0x00000010;
    public static final int FLAG_NO_CLEAR = 0x00000020;
    public static final int FLAG_FOREGROUND_SERVICE = 0x00000040;
    public static final int FLAG_HIGH_PRIORITY = 0x00000080;
    public static final int PRIORITY_DEFAULT = 0;
    public static final int PRIORITY_LOW = -1;
    public static final int PRIORITY_MIN = -2;
    public static final int PRIORITY_HIGH = 1;
    public static final int PRIORITY_MAX = 2;

    public long when;
    public int icon;
    public int iconLevel;
    public int number;
    public PendingIntent contentIntent;
    public PendingIntent deleteIntent;
    public PendingIntent fullScreenIntent;
    public CharSequence tickerText;
    public RemoteViews tickerView;
    public RemoteViews contentView;
    public RemoteViews bigContentView;
    public Bitmap largeIcon;
    public Uri sound;
    public int audioStreamType = STREAM_DEFAULT;
    public long[] vibrate;
    public int ledARGB;
    public int ledOnMS;
    public int ledOffMS;
    public int defaults;
    public int flags;
    public int priority;
    public String[] kind;
    public Bundle extras = new Bundle();

    // These two constructor bodies are unchanged from API19 Notification.java.
    public Notification() {
        this.when = System.currentTimeMillis();
        this.priority = PRIORITY_DEFAULT;
    }

    @Deprecated
    public Notification(int icon, CharSequence tickerText, long when) {
        this.icon = icon;
        this.tickerText = tickerText;
        this.when = when;
    }

    @Deprecated
    public void setLatestEventInfo(Context context, CharSequence contentTitle,
            CharSequence contentText, PendingIntent contentIntent) {
        if (context == null) throw new NullPointerException("context");
        nativeUnsupported("system_template");
    }

    public Notification(Parcel parcel) {
        if (parcel == null) throw new NullPointerException("parcel");
        nativeUnsupported("parcel");
    }

    @Override public int describeContents() {
        return 0;
    }

    @Override public void writeToParcel(Parcel parcel, int flags) {
        if (parcel == null) throw new NullPointerException("parcel");
        nativeUnsupported("parcel");
    }

    public static final Parcelable.Creator<Notification> CREATOR = new Parcelable.Creator<Notification>() {
        public Notification createFromParcel(Parcel parcel) { return new Notification(parcel); }
        public Notification[] newArray(int size) { return new Notification[size]; }
    };

    private static native void nativeUnsupported(String operation);
}
