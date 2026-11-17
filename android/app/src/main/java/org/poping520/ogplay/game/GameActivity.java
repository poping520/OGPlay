package org.poping520.ogplay.game;

import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.os.Build;
import android.os.Bundle;

import org.poping520.ogplay.runtime.RuntimeData;

import org.libsdl.app.SDLActivity;

/** Runs one game in the :game process; the native runtime starts from SDL_main. */
public final class GameActivity extends SDLActivity {
    private static final String EXTRA_ARGUMENTS = "org.poping520.ogplay.extra.ARGUMENTS";
    private static final String ACTION_RESUME = "org.poping520.ogplay.action.RESUME_GAME";
    private static final String ACTION_QUIT = "org.poping520.ogplay.action.QUIT_GAME";
    // SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT in SDL_messagebox.h.
    private static final int ESCAPE_DEFAULT = 0x2;

    /** The launcher's launch plan: the run-apk options exactly as the native side formatted them. */
    public static Intent intent(Context context, String[] arguments) {
        return new Intent(context, GameActivity.class).putExtra(EXTRA_ARGUMENTS, arguments);
    }

    /** Back to the running game: its single-instance task comes to the front with the game as it was. */
    public static Intent resume(Context context) {
        return new Intent(context, GameActivity.class).setAction(ACTION_RESUME);
    }

    /** For the launcher to close the running game (a broadcast, so the game does not first come to the front). */
    public static Intent quit(Context context) {
        return new Intent(ACTION_QUIT).setPackage(context.getPackageName());
    }

    // Leaves through the game's own quit path, as its close confirmation does: the session ends and its saves and
    // run report are written. SDL wakes its thread for this even while the game is in the background.
    private final BroadcastReceiver quitReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            SDLActivity.nativeSendQuit();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        RuntimeData.publishEnvironment(this);
        super.onCreate(savedInstanceState);
        // A resume that finds no game (the process ended meanwhile) has nothing to show.
        if (ACTION_RESUME.equals(getIntent().getAction())) {
            finish();
            return;
        }
        IntentFilter filter = new IntentFilter(ACTION_QUIT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(quitReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(quitReceiver, filter);
        }
        quitRegistered = true;
    }

    private boolean quitRegistered;

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

    /**
     * Called by the native close confirmation ("回到游戏库"): the launcher's task comes forward as from its home screen
     * icon, and this game keeps running in its own task.
     */
    @SuppressWarnings("unused")
    public void showLibrary() {
        runOnUiThread(() -> {
            Intent launcher = getPackageManager().getLaunchIntentForPackage(getPackageName());
            if (launcher != null) startActivity(launcher);
        });
    }

    @Override
    protected void onDestroy() {
        if (quitRegistered) unregisterReceiver(quitReceiver);
        super.onDestroy();
        // Native state is process-wide; the next game starts from a new process.
        System.exit(0);
    }
}
