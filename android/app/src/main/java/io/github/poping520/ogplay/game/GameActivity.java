package io.github.poping520.ogplay.game;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import io.github.poping520.ogplay.runtime.RuntimeData;

import org.libsdl.app.SDLActivity;

import java.io.File;

/** Runs one game in the :game process; the native runtime starts from SDL_main. */
public final class GameActivity extends SDLActivity {
    private static final String EXTRA_APK = "io.github.poping520.ogplay.extra.APK";

    public static Intent intent(Context context, File apk) {
        return new Intent(context, GameActivity.class).putExtra(EXTRA_APK, apk.getPath());
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // hal/android reads these from the process environment.
        setEnvironment("OGPLAY_ANDROID_RUNTIME_DIR", RuntimeData.runtimeDirectory(this));
        setEnvironment("OGPLAY_ANDROID_USER_DATA_DIR", RuntimeData.userDataDirectory(this));
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "ogplay_android"};
    }

    @Override
    protected String[] getArguments() {
        return new String[] {getIntent().getStringExtra(EXTRA_APK)};
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // Native state is process-wide; the next game starts from a new process.
        System.exit(0);
    }

    private static void setEnvironment(String name, File directory) {
        try {
            Os.setenv(name, directory.getPath(), true);
        } catch (ErrnoException error) {
            throw new IllegalStateException("Cannot set " + name, error);
        }
    }
}
