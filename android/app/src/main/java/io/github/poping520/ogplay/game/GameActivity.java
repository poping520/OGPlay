package io.github.poping520.ogplay.game;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.os.Bundle;

import io.github.poping520.ogplay.runtime.RuntimeData;

import org.libsdl.app.SDLActivity;

/** Runs one game in the :game process; the native runtime starts from SDL_main. */
public final class GameActivity extends SDLActivity {
    private static final String EXTRA_ARGUMENTS = "io.github.poping520.ogplay.extra.ARGUMENTS";
    // SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT in SDL_messagebox.h.
    private static final int ESCAPE_DEFAULT = 0x2;

    /** The launcher's launch plan: the run-apk options exactly as the native side formatted them. */
    public static Intent intent(Context context, String[] arguments) {
        return new Intent(context, GameActivity.class).putExtra(EXTRA_ARGUMENTS, arguments);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        RuntimeData.publishEnvironment(this);
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "ogplay_android"};
    }

    @Override
    protected String[] getArguments() {
        String[] arguments = getIntent().getStringArrayExtra(EXTRA_ARGUMENTS);
        return arguments != null ? arguments : new String[0];
    }

    /**
     * SDL's message boxes (the close confirmation among them) as a system-styled dialog that follows the dark
     * setting. The escape-default button sits on the cancel side and is what the back key or a tap outside
     * answers; boxes with more buttons than a dialog holds keep SDL's own layout.
     */
    @Override
    protected void messageboxCreateAndShow(Bundle args) {
        final int[] flags = args.getIntArray("buttonFlags");
        final int[] ids = args.getIntArray("buttonIds");
        final String[] texts = args.getStringArray("buttonTexts");
        if (flags == null || ids == null || texts == null || texts.length > 3) {
            super.messageboxCreateAndShow(args);
            return;
        }
        boolean night = (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                == Configuration.UI_MODE_NIGHT_YES;
        AlertDialog.Builder builder = new AlertDialog.Builder(this,
                night ? android.R.style.Theme_DeviceDefault_Dialog_Alert
                      : android.R.style.Theme_DeviceDefault_Light_Dialog_Alert)
                .setIcon(getApplicationInfo().icon)
                .setTitle(args.getString("title"))
                .setMessage(args.getString("message"));
        int escape = -1;
        boolean positive = false;
        for (int i = 0; i < texts.length; ++i) {
            final int id = ids[i];
            DialogInterface.OnClickListener click = (dialog, which) -> messageboxSelection[0] = id;
            if (escape < 0 && (flags[i] & ESCAPE_DEFAULT) != 0) {
                escape = id;
                builder.setNegativeButton(texts[i], click);
            } else if (!positive) {
                positive = true;
                builder.setPositiveButton(texts[i], click);
            } else {
                builder.setNeutralButton(texts[i], click);
            }
        }
        final int cancel = escape;
        builder.setCancelable(cancel >= 0);
        builder.setOnCancelListener(dialog -> messageboxSelection[0] = cancel);
        builder.setOnDismissListener(dialog -> {
            synchronized (messageboxSelection) {
                messageboxSelection.notify();
            }
        });
        AlertDialog dialog = builder.create();
        dialog.setCanceledOnTouchOutside(cancel >= 0);
        dialog.show();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Native state is process-wide; the next game starts from a new process.
        System.exit(0);
    }
}
