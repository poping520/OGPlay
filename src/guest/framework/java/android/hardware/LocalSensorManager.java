/*
 * Copyright (C) 2026 OGPlay contributors
 * SPDX-License-Identifier: Apache-2.0
 */
package android.hardware;

import android.os.Handler;
import java.util.Arrays;
import java.util.List;

/** Process-local device boundary; API19 SensorManager owns the client algorithms. */
public final class LocalSensorManager extends SensorManager {
    private static native Sensor[] nativeGetSensors();

    @Override
    protected List<Sensor> getFullSensorList() {
        return Arrays.asList(nativeGetSensors());
    }

    @Override
    protected native boolean registerListenerImpl(SensorEventListener listener, Sensor sensor,
            int delayUs, Handler handler, int maxBatchReportLatencyUs, int reservedFlags);

    @Override
    protected native void unregisterListenerImpl(SensorEventListener listener, Sensor sensor);

    @Override
    protected native boolean requestTriggerSensorImpl(TriggerEventListener listener, Sensor sensor);

    @Override
    protected native boolean cancelTriggerSensorImpl(TriggerEventListener listener, Sensor sensor,
            boolean disable);

    @Override
    protected native boolean flushImpl(SensorEventListener listener);
}
