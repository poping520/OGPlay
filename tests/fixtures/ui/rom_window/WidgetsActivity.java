package org.ogplay.fixture.window;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.Spinner;
import android.widget.Toast;

public final class WidgetsActivity extends Activity {
    public static int textLength, lastCharacter, imeAccepted, dialogClicks, alertClicks, cancelled, popupClicks, result;
    public static int lifecycle, dialogX, dialogY, alertX, alertY, dialogKeys, activityKeys;
    private int toastCount;
    private Dialog dialog;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setBackgroundColor(Color.rgb(35, 53, 75));
        final EditText editor = new EditText(this);
        editor.setSingleLine(true);
        editor.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {}
            public void onTextChanged(CharSequence text, int start, int before, int count) {
                textLength = text.length();
                lastCharacter = textLength == 0 ? 0 : text.charAt(textLength - 1);
            }
            public void afterTextChanged(Editable text) {}
        });
        editor.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                InputMethodManager ime = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                imeAccepted = ime.showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT) ? 1 : 0;
            }
        });
        content.addView(editor, new LinearLayout.LayoutParams(-1, 48));
        final DialogInterface.OnCancelListener cancel = new DialogInterface.OnCancelListener() {
            @Override public void onCancel(DialogInterface dismissed) { ++cancelled; }
        };
        add(content, "Dialog", new View.OnClickListener() {
            @Override public void onClick(View view) {
                dialog = new Dialog(WidgetsActivity.this);
                Button choice = new Button(WidgetsActivity.this);
                choice.setText("Dialog choice");
                choice.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View button) { ++dialogClicks; dialog.dismiss(); }
                });
                dialog.setContentView(choice);
                dialog.setOnCancelListener(cancel);
                dialog.setOnKeyListener(new DialogInterface.OnKeyListener() {
                    @Override public boolean onKey(DialogInterface d, int key, android.view.KeyEvent event) {
                        dialogKeys = dialogKeys * 10 + event.getAction() + 1;
                        return false;
                    }
                });
                dialog.show();
                dialog.getWindow().setLayout(220, 96);
                trackButton(choice, false);
            }
        });
        add(content, "Alert", new View.OnClickListener() {
            @Override public void onClick(View view) {
                AlertDialog alert = new AlertDialog.Builder(WidgetsActivity.this).setTitle("Alert").setMessage("ROM window")
                        .setOnCancelListener(cancel).setPositiveButton("Choose", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface d, int which) { ++alertClicks; }
                        }).show();
                trackButton(alert.getButton(DialogInterface.BUTTON_POSITIVE), true);
            }
        });
        add(content, "Popup", new View.OnClickListener() {
            @Override public void onClick(View anchor) {
                final Button entry = new Button(WidgetsActivity.this);
                entry.setText("Popup entry");
                final PopupWindow popup = new PopupWindow(entry, 180, 48, true);
                popup.setBackgroundDrawable(new ColorDrawable(Color.YELLOW));
                entry.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View view) { ++popupClicks; popup.dismiss(); }
                });
                popup.showAsDropDown(anchor);
            }
        });
        Spinner spinner = new Spinner(this, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> options = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item,
                new String[] { "First", "Second" });
        options.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(options);
        content.addView(spinner, new LinearLayout.LayoutParams(-1, 48));
        add(content, "Toast", new View.OnClickListener() {
            @Override public void onClick(View view) {
                Toast toast = new Toast(WidgetsActivity.this);
                View indicator = new View(WidgetsActivity.this);
                indicator.setMinimumWidth(128);
                indicator.setMinimumHeight(48);
                indicator.setBackgroundColor(Color.MAGENTA);
                toast.setView(indicator);
                toast.setDuration(toastCount++ == 0 ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG);
                toast.show();
            }
        });
        add(content, "Activity result", new View.OnClickListener() {
            @Override public void onClick(View view) {
                startActivityForResult(new Intent(WidgetsActivity.this, ResultActivity.class), 19);
            }
        });
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        setContentView(content);
    }

    private void trackButton(final View button, final boolean alert) {
        button.getViewTreeObserver().addOnGlobalLayoutListener(new android.view.ViewTreeObserver.OnGlobalLayoutListener() {
            @Override public void onGlobalLayout() {
                int[] at = new int[2];
                button.getLocationOnScreen(at);
                int x = at[0] + button.getWidth() / 2;
                int y = at[1] + button.getHeight() / 2;
                if (alert) {
                    alertX = x;
                    alertY = y;
                } else {
                    dialogX = x;
                    dialogY = y;
                }
            }
        });
    }

    private void add(LinearLayout parent, String name, View.OnClickListener action) {
        Button button = new Button(this);
        button.setText(name);
        button.setOnClickListener(action);
        parent.addView(button, new LinearLayout.LayoutParams(-1, 48));
    }

    @Override public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        activityKeys = activityKeys * 10 + event.getAction() + 1;
        return super.dispatchKeyEvent(event);
    }

    @Override protected void onStart() { super.onStart(); lifecycle = lifecycle * 10 + 1; }
    @Override protected void onResume() { super.onResume(); lifecycle = lifecycle * 10 + 2; }
    @Override protected void onPause() { lifecycle = lifecycle * 10 + 3; super.onPause(); }
    @Override protected void onStop() { lifecycle = lifecycle * 10 + 4; super.onStop(); }
    @Override protected void onRestart() { super.onRestart(); lifecycle = lifecycle * 10 + 5; }
    @Override protected void onActivityResult(int request, int code, Intent data) {
        super.onActivityResult(request, code, data);
        result = request == 19 && data != null ? code + data.getIntExtra("fixture", 0) : 0;
    }
}
