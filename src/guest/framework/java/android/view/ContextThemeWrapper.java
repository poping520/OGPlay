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
import android.content.ContextWrapper;

/** Bounded API19 wrapper; theme resource identity only, without theme resolution. */
public class ContextThemeWrapper extends ContextWrapper {
    private int mThemeResource;
    private LayoutInflater mInflater;
    public ContextThemeWrapper() { super(null); }
    public ContextThemeWrapper(Context base, int themeResId) { super(base); mThemeResource = themeResId; }
    public void setTheme(int resid) { mThemeResource = resid; }
    public int getThemeResId() { return mThemeResource; }
    public Object getSystemService(String name) {
        if (Context.LAYOUT_INFLATER_SERVICE.equals(name)) {
            if (mInflater == null) mInflater = LayoutInflater.from(getBaseContext()).cloneInContext(this);
            return mInflater;
        }
        return super.getSystemService(name);
    }
}
