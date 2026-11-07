package io.github.poping520.ogplay.launcher;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import io.github.poping520.ogplay.R;
import io.github.poping520.ogplay.game.GameActivity;
import io.github.poping520.ogplay.runtime.RuntimeData;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Minimal entry until the library UI arrives: prepares data and starts the bundled demo game. */
public final class LauncherActivity extends Activity {
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button demoGame;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        status = new TextView(this);
        status.setText(R.string.preparing_runtime);
        demoGame = new Button(this);
        demoGame.setText(R.string.demo_game);
        demoGame.setEnabled(false);

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.addView(status);
        content.addView(demoGame);
        content.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(
                    insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            return insets;
        });
        setContentView(content);

        worker.execute(this::prepareRuntime);
    }

    @Override
    protected void onDestroy() {
        worker.shutdown();
        super.onDestroy();
    }

    private void prepareRuntime() {
        try {
            File runtime = RuntimeData.prepare(this);
            runOnUiThread(() -> showDemoGame(runtime));
        } catch (IOException error) {
            runOnUiThread(() -> status.setText(
                    getString(R.string.prepare_runtime_failed, error.getMessage())));
        }
    }

    private void showDemoGame(File runtime) {
        status.setText("");
        File apk = new File(runtime, "data/demos/ogplay-sweetshop.apk");
        demoGame.setOnClickListener(view -> startActivity(GameActivity.intent(this, apk)));
        demoGame.setEnabled(true);
    }
}
