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

package android.view;

import android.content.Context;

/** API19 client protocol over the bounded registry XML builder. */
public abstract class LayoutInflater {
    protected final Context mContext;
    protected LayoutInflater(Context context) {
        if (context == null) throw new NullPointerException("context");
        mContext = context;
    }
    protected LayoutInflater(LayoutInflater original, Context newContext) { this(newContext); }
    public static LayoutInflater from(Context context) {
        LayoutInflater inflater = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
        if (inflater == null) throw new AssertionError("LayoutInflater not found.");
        return inflater;
    }
    public Context getContext() { return mContext; }
    public abstract LayoutInflater cloneInContext(Context newContext);
    public View inflate(int resource, ViewGroup root) { return inflate(resource, root, root != null); }
    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        synchronized (this) { return nativeInflate(mContext, resource, root, attachToRoot); }
    }
    // Context's local service factory, without a system service or factory reflection.
    public static LayoutInflater createLocal(Context context) { return new LocalInflater(context); }
    private static final class LocalInflater extends LayoutInflater {
        LocalInflater(Context context) { super(context); }
        public LayoutInflater cloneInContext(Context context) { return new LocalInflater(context); }
    }
    private native View nativeInflate(Context context, int resource, ViewGroup root, boolean attachToRoot);
}
