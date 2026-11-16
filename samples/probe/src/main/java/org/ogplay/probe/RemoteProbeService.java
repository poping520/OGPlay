package org.ogplay.probe;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;

public final class RemoteProbeService extends Service {
    @Override public IBinder onBind(Intent intent) {
        return new Binder();
    }
}
