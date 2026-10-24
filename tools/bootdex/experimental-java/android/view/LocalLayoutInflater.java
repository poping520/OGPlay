/* AOSP 4.4 PhoneLayoutInflater protocol; experimental companion only.
 * Copyright (C) 2006 The Android Open Source Project
 * Licensed under the Apache License, Version 2.0.
 */
package android.view;

import android.content.Context;
import android.util.AttributeSet;

/** The pinned ROM has no PhoneLayoutInflater class_def. */
public final class LocalLayoutInflater extends LayoutInflater {
    public LocalLayoutInflater(Context context) { super(context); }
    private LocalLayoutInflater(LayoutInflater original, Context context) { super(original, context); }
    public LayoutInflater cloneInContext(Context context) { return new LocalLayoutInflater(this, context); }
    protected View onCreateView(String name, AttributeSet attrs) throws ClassNotFoundException {
        for (String prefix : new String[] {"android.widget.", "android.webkit."}) {
            try {
                View view = createView(name, prefix, attrs);
                if (view != null) return view;
            } catch (ClassNotFoundException expected) { }
        }
        return super.onCreateView(name, attrs);
    }
}
