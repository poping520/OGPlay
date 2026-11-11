package org.ogplay.fixture.window;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.view.View;

public final class ResultActivity extends Activity {
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        Button finish = new Button(this);
        finish.setText("Finish");
        finish.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) {
                setResult(RESULT_OK, new Intent().putExtra("fixture", 42));
                finish();
            }
        });
        setContentView(finish);
    }
}
