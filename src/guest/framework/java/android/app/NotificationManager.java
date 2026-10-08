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
import android.os.Handler;

/** API19 client for the explicit disabled-notification backend. */
public class NotificationManager {
    private Context mContext;

    NotificationManager(Context context, Handler handler)
    {
        mContext = context;
    }

    public static NotificationManager from(Context context) {
        return (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
    }

    public void cancel(int id)
    {
        cancel(null, id);
    }

    public void notify(int id, Notification notification)
    {
        notify(null, id, notification);
    }

    public void cancel(String tag, int id) {
        nativeCancel(mContext.getPackageName(), tag, id, false);
    }

    public void cancelAll() {
        nativeCancel(mContext.getPackageName(), null, 0, true);
    }

    public void notify(String tag, int id, Notification notification) {
        if (notification == null) throw new NullPointerException("notification");
        nativeRejectPost(mContext.getPackageName());
    }

    private native void nativeCancel(String packageName, String tag, int id, boolean all);
    private native void nativeRejectPost(String packageName);
}
