/*
 * Copyright (C) 2007 The Android Open Source Project
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
import android.os.Handler;
import android.os.Parcel;
import android.os.Parcelable;
import java.util.List;

/** API19 client for process-owned guest memory facts. */
public class ActivityManager {
    private final Context mContext;
    private final Handler mHandler;

    ActivityManager(Context context, Handler handler) {
        mContext = context;
        mHandler = handler;
    }

    public void getMemoryInfo(MemoryInfo outInfo) {
        if (outInfo == null) throw new NullPointerException("outInfo");
        outInfo.nativeRead();
        // AOSP ActivityManagerService's HOME/CACHED low-memory boundary.
        outInfo.lowMemory = outInfo.availMem <
            outInfo.threshold + (outInfo.hiddenAppThreshold - outInfo.threshold) / 2;
    }

    public List getRunningAppProcesses() {
        throw nativeUnsupported("getRunningAppProcesses");
    }

    private static native UnsupportedOperationException nativeUnsupported(String operation);

    public static class MemoryInfo implements Parcelable {
        /**
         * The available memory on the system.  This number should not
         * be considered absolute: due to the nature of the kernel, a significant
         * portion of this memory is actually in use and needed for the overall
         * system to run well.
         */
        public long availMem;

        /**
         * The total memory accessible by the kernel.  This is basically the
         * RAM size of the device, not including below-kernel fixed allocations
         * like DMA buffers, RAM for the baseband CPU, etc.
         */
        public long totalMem;

        /**
         * The threshold of {@link #availMem} at which we consider memory to be
         * low and start killing background services and other non-extraneous
         * processes.
         */
        public long threshold;

        /**
         * Set to true if the system considers itself to currently be in a low
         * memory situation.
         */
        public boolean lowMemory;

        /** @hide */
        public long hiddenAppThreshold;
        /** @hide */
        public long secondaryServerThreshold;
        /** @hide */
        public long visibleAppThreshold;
        /** @hide */
        public long foregroundAppThreshold;

        public MemoryInfo() {
        }

        public int describeContents() {
            return 0;
        }

        public void writeToParcel(Parcel dest, int flags) {
            dest.writeLong(availMem);
            dest.writeLong(totalMem);
            dest.writeLong(threshold);
            dest.writeInt(lowMemory ? 1 : 0);
            dest.writeLong(hiddenAppThreshold);
            dest.writeLong(secondaryServerThreshold);
            dest.writeLong(visibleAppThreshold);
            dest.writeLong(foregroundAppThreshold);
        }

        public void readFromParcel(Parcel source) {
            availMem = source.readLong();
            totalMem = source.readLong();
            threshold = source.readLong();
            lowMemory = source.readInt() != 0;
            hiddenAppThreshold = source.readLong();
            secondaryServerThreshold = source.readLong();
            visibleAppThreshold = source.readLong();
            foregroundAppThreshold = source.readLong();
        }

        public static final Creator<MemoryInfo> CREATOR
                = new Creator<MemoryInfo>() {
            public MemoryInfo createFromParcel(Parcel source) {
                return new MemoryInfo(source);
            }
            public MemoryInfo[] newArray(int size) {
                return new MemoryInfo[size];
            }
        };

        private MemoryInfo(Parcel source) {
            readFromParcel(source);
        }
        private native void nativeRead();
    }
}
