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

package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.view.View;

/** Text-and-button API19 client, with explicit rejection of unsupported content. */
public class AlertDialog extends Dialog {
    protected AlertDialog(Context context) { super(context); }
    protected AlertDialog(Context context, int theme) { super(context, theme); }
    protected AlertDialog(Context context, boolean cancelable, DialogInterface.OnCancelListener listener) {
        super(context, cancelable, listener);
    }
    public void setMessage(CharSequence message) { mMessage = message; changed(); }
    public void setButton(int which, CharSequence text, DialogInterface.OnClickListener listener) {
        int index = -which - 1;
        if (index < 0 || index >= 3) throw new IllegalArgumentException("unknown Dialog button");
        mButtonLabels[index] = text;
        mButtonListeners[index] = listener;
        changed();
    }
    public static class Builder {
        private final Context mContext;
        private CharSequence mTitle, mMessage;
        private final CharSequence[] mLabels = new CharSequence[3];
        private final DialogInterface.OnClickListener[] mListeners = new DialogInterface.OnClickListener[3];
        private boolean mCancelable = true;
        private DialogInterface.OnCancelListener mCancel;
        private DialogInterface.OnDismissListener mDismiss;
        private DialogInterface.OnKeyListener mKey;
        public Builder(Context context) { this(context, 0); }
        public Builder(Context context, int theme) {
            if (context == null) throw new NullPointerException("context == null");
            if (theme != 0) Dialog.nativeUnsupported("AlertDialog theme");
            mContext = context;
        }
        public Context getContext() { return mContext; }
        public Builder setTitle(CharSequence title) { mTitle = title; return this; }
        public Builder setTitle(int id) { return setTitle(mContext.getResources().getText(id)); }
        public Builder setMessage(CharSequence message) { mMessage = message; return this; }
        public Builder setMessage(int id) { return setMessage(mContext.getResources().getText(id)); }
        public Builder setCancelable(boolean cancelable) { mCancelable = cancelable; return this; }
        public Builder setOnCancelListener(DialogInterface.OnCancelListener listener) { mCancel = listener; return this; }
        public Builder setOnDismissListener(DialogInterface.OnDismissListener listener) { mDismiss = listener; return this; }
        public Builder setOnKeyListener(DialogInterface.OnKeyListener listener) { mKey = listener; return this; }
        private Builder button(int index, CharSequence text, DialogInterface.OnClickListener listener) {
            mLabels[index] = text; mListeners[index] = listener; return this;
        }
        public Builder setPositiveButton(CharSequence text, DialogInterface.OnClickListener listener) { return button(0, text, listener); }
        public Builder setNegativeButton(CharSequence text, DialogInterface.OnClickListener listener) { return button(1, text, listener); }
        public Builder setNeutralButton(CharSequence text, DialogInterface.OnClickListener listener) { return button(2, text, listener); }
        public Builder setPositiveButton(int id, DialogInterface.OnClickListener listener) { return setPositiveButton(mContext.getResources().getText(id), listener); }
        public Builder setNegativeButton(int id, DialogInterface.OnClickListener listener) { return setNegativeButton(mContext.getResources().getText(id), listener); }
        public Builder setNeutralButton(int id, DialogInterface.OnClickListener listener) { return setNeutralButton(mContext.getResources().getText(id), listener); }
        public Builder setItems(CharSequence[] items, DialogInterface.OnClickListener listener) { Dialog.nativeUnsupported("AlertDialog items"); return this; }
        public Builder setView(View view) { Dialog.nativeUnsupported("AlertDialog custom View"); return this; }
        public AlertDialog create() {
            AlertDialog dialog = new AlertDialog(mContext);
            dialog.setTitle(mTitle); dialog.setMessage(mMessage); dialog.setCancelable(mCancelable);
            dialog.setOnCancelListener(mCancel); dialog.setOnDismissListener(mDismiss); dialog.setOnKeyListener(mKey);
            for (int i = 0; i < 3; i++) dialog.setButton(-i - 1, mLabels[i], mListeners[i]);
            return dialog;
        }
        public AlertDialog show() { AlertDialog dialog = create(); dialog.show(); return dialog; }
    }
}
