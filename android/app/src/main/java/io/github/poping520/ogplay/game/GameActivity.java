package io.github.poping520.ogplay.game;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;

import io.github.poping520.ogplay.runtime.RuntimeData;

import org.libsdl.app.SDLActivity;

/** Runs one game in the :game process; the native runtime starts from SDL_main. */
public final class GameActivity extends SDLActivity {
    private static final String EXTRA_ARGUMENTS = "io.github.poping520.ogplay.extra.ARGUMENTS";

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

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Native state is process-wide; the next game starts from a new process.
        System.exit(0);
    }
}
