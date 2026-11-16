package org.ogplay.probe;

import android.os.Process;
import android.os.SystemClock;
import java.io.File;
import java.io.FileInputStream;
import java.util.Arrays;
import java.util.Map;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class NativeProbes {
    static void register(Map<String, Probe> probes) {
        basics(probes);
        interop(probes);
        ndk(probes);
    }

    private static void basics(Map<String, Probe> probes) {
        add(probes, new NativeCase("native.bionic.identifiers", "in_scope", "uname", "sysconf", "getpid", "gettid") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] nativeFacts = NativeProbe.processIds();
                String machine = NativeProbe.machine();
                int cores = Runtime.getRuntime().availableProcessors();
                long bySysconf = NativeProbe.cpuCount(true);
                long page = nativeFacts[2];
                return new Checks()
                        .require("native_pid_positive", nativeFacts[0] > 0)
                        .require("pid_matches_android", nativeFacts[0] == Process.myPid())
                        .require("native_tid_positive", nativeFacts[1] > 0)
                        .require("tid_matches_android", nativeFacts[1] == Process.myTid())
                        .require("page_size_positive_power_of_two", page > 0 && (page & (page - 1)) == 0)
                        .require("android_cpu_count_positive", cores > 0)
                        .require("cpu_count_matches_sysconf", cores == bySysconf)
                        .require("uname_reports_arm", machine != null && machine.startsWith("arm"))
                        .outcome(obj("native_pid", nativeFacts[0], "native_tid", nativeFacts[1], "page_size", page,
                                "machine", machine, "online_cpus", bySysconf),
                        obj("pid", Process.myPid(), "tid", Process.myTid(), "page_size_positive_power_of_two", true,
                                "machine_prefix", "arm", "online_cpus", cores),
                        "bionic and Android agree on process/thread and CPU identity");
            }
        });
        add(probes, new NativeCase("native.bionic.pthread", "in_scope",
                "pthread_create", "pthread_join", "pthread_mutex_init", "pthread_mutex_lock",
                "pthread_mutex_unlock", "pthread_mutex_destroy") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                return fields(NativeProbe.threads(), new long[] {0, 0, 0, 0, 134, 1},
                        "A native thread joins with its result and both producers increment under the mutex",
                        "mutex_init", "create", "join", "child_lock_error", "counter", "child_return_matches");
            }
        });
        add(probes, new NativeCase("native.bionic.cond_timedwait", "in_scope",
                "pthread_cond_timedwait", "pthread_mutex_lock", "clock_gettime") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] clock = NativeProbe.condWait();
                double millis = (clock[3] - clock[2]) / 1e6;
                boolean realtimeSampled = clock[4] >= 0 && clock[6] >= 0;
                return new Checks()
                        .require("returns_etimedout", clock[0] == clock[1])
                        .require("monotonic_start_read", clock[2] >= 0)
                        .require("monotonic_end_read", clock[3] >= 0)
                        .require("monotonic_elapsed_at_least_115_ms", millis >= 115)
                        .require("monotonic_elapsed_at_most_600_ms", millis <= 600)
                        .outcome(obj("result", clock[0], "elapsed_ms", millis,
                                        "monotonic_start_ns", clock[2], "monotonic_after_unlock_ns", clock[3],
                                        "realtime_start_ns", clock[4], "realtime_deadline_ns", clock[5],
                                        "realtime_at_return_ns", clock[6], "monotonic_at_return_ns", clock[7],
                                        "realtime_elapsed_ms", realtimeSampled ? (clock[6] - clock[4]) / 1e6 : null,
                                        "zero_returns", clock[8],
                                        "realtime_at_deadline", clock[5] >= 0 && clock[6] >= 0 ? clock[6] >= clock[5] : null),
                                obj("result", clock[1], "delay_ms", 120, "min_ms", 115, "max_ms", 600),
                                "An unsignalled condition variable times out near its absolute deadline");
            }
        });
        add(probes, new NativeCase("native.bionic.nanosleep", "in_scope", "nanosleep", "clock_gettime") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long before = SystemClock.elapsedRealtime();
                long[] clock = NativeProbe.sleep();
                long javaMs = SystemClock.elapsedRealtime() - before;
                double nativeMs = (clock[3] - clock[2]) / 1e6;
                return check(clock[0] == 0 && clock[1] == 0 && clock[2] >= 0 && clock[3] >= 0
                                && nativeMs >= 115 && nativeMs <= 650 && Math.abs(javaMs - nativeMs) <= 100,
                        obj("rc", clock[0], "errno", clock[1], "native_ms", nativeMs, "java_ms", javaMs),
                        obj("rc", 0, "errno", 0, "delay_ms", 120, "min_ms", 115, "max_ms", 650, "max_spread_ms", 100),
                        "Native nanosleep advances both the native and Java monotonic clocks by the requested interval");
            }
        });
        add(probes, new NativeCase("native.bionic.mmap_protect", "in_scope", "mmap", "mprotect", "munmap") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                return fields(NativeProbe.memory(), new long[] {1, 0, 1, 0, 19, 0, 29, 0},
                        "Anonymous pages are zeroed, retain their data while read-only and become writable again",
                        "mapped", "mmap_errno", "initially_zero", "readonly_errno", "readonly_byte",
                        "readwrite_errno", "written_byte", "munmap_errno");
            }
        });
        add(probes, new NativeCase("native.bionic.proc_maps", "in_scope", "fopen", "fgets", "fclose") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] value = NativeProbe.maps();
                return check(value[0] == 1 && value[1] == 0 && value[2] > 0 && value[3] == 1,
                        obj("opened", value[0] == 1, "errno", value[1], "lines_read", value[2], "library_found", value[3] == 1),
                        obj("opened", true, "errno", 0, "library_found", true),
                        "Process maps include the JNI library that is currently executing");
            }
        });
        add(probes, new NativeCase("native.bionic.stdio_proc", "in_scope", "fopen", "fread", "ferror", "fclose") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                byte[] java = ProbeFiles.read(new FileInputStream(new File("/proc/cpuinfo")), 1024 * 1024);
                return fields(NativeProbe.readCpuInfo(), new long[] {java.length, fnv(java), 0},
                        "stdio reads the same procfs bytes as the Java stream", "length", "fnv1a32", "error");
            }
        });
    }

    private static void interop(Map<String, Probe> probes) {
        add(probes, new NativeCase("native.bionic.getenv", "in_scope", "getenv",
                "Ljava/lang/System;.getenv(Ljava/lang/String;)Ljava/lang/String;") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                String java = System.getenv("ANDROID_ROOT");
                String nativeValue = NativeProbe.environmentValue();
                return check(java != null && java.length() > 0 && java.equals(nativeValue),
                        obj("java", java, "native", nativeValue), obj("same_nonempty_value", true),
                        "Java and bionic share the process ANDROID_ROOT value");
            }
        });
        add(probes, new NativeCase("native.bionic.setjmp", "in_scope", "setjmp", "longjmp") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                int code = NativeProbe.jump();
                return check(code == 19, obj("restored_code", code), obj("restored_code", 19),
                        "longjmp resumes the saved context with the requested nonzero code");
            }
        });
        add(probes, new NativeCase("native.bionic.cpp_runtime", "in_scope",
                "__cxa_throw", "__cxa_begin_catch", "__dynamic_cast", "std::atomic<int>::fetch_add") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                return fields(NativeProbe.cppRuntime(true), new long[] {1, 1, 19, 19, 1, 40},
                        "Static libc++ unwinding, RTTI and atomic read-modify-write agree with the fixture",
                        "dynamic_cast", "typeid", "caught", "fetch_add_before", "compare_exchange", "final_count");
            }
        });
        add(probes, new NativeCase("native.dl", "in_scope", "dlopen", "dlsym", "dlerror", "dlclose") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                return fields(NativeProbe.dynamicLibraries(), new long[] {1, 1, 1, 1, 1},
                        "libdl resolves a platform symbol, describes a missing object and returns the main handle",
                        "loaded_gles", "found_glclear", "missing_returned_null", "missing_error_nonempty", "main_handle");
            }
        });
        add(probes, new NativeCase("native.process.fork", "expected_reject", "fork", "_exit", "__errno") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] value = NativeProbe.tryFork();
                return check(value[0] == -1 && value[1] > 0, obj("fork_return", value[0], "errno", value[1]),
                        obj("fork_return", -1, "errno_positive", true),
                        "Process creation is rejected with a POSIX error");
            }
        });
    }

    private static void ndk(Map<String, Probe> probes) {
        add(probes, new NativeCase("native.asset", "in_scope",
                "AAssetManager_fromJava", "AAssetManager_open", "AAsset_read", "AAsset_close",
                "Landroid/content/res/AssetManager;.open(Ljava/lang/String;)Ljava/io/InputStream;") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                android.content.res.AssetManager manager = session.activity.getAssets();
                byte[] java = ProbeFiles.read(manager.open("fixtures/deflated.bin"), 32768);
                return fields(NativeProbe.asset(manager), new long[] {java.length, fnv(java), 0},
                        "The NDK asset stream and Java asset stream expose the same bytes", "length", "fnv1a32", "last_read");
            }
        });
        add(probes, new NativeCase("native.looper", "in_scope", "ALooper_prepare", "ALooper_addFd", "ALooper_pollOnce",
                "ALooper_removeFd", "pipe", "read", "write") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] value = NativeProbe.looper();
                return new Checks()
                        .require("looper_prepared", value[0] == 1)
                        .require("pipe_created", value[1] == 0)
                        .require("fd_registered", value[2] == 1)
                        .require("byte_written", value[3] == 1)
                        .require("one_callback_delivered", value[4] == 1)
                        .require("callback_received_expected_byte", value[5] == 19)
                        .outcome(obj("prepared", value[0], "pipe_errno", value[1], "registered", value[2], "written", value[3],
                                "callback_count", value[4], "received_byte", value[5], "poll_result", value[6]),
                        obj("prepared", 1, "pipe_errno", 0, "registered", 1, "written", 1, "callback_count", 1, "received_byte", 19),
                        "A native Looper delivers an input callback for a ready pipe");
            }
        });
        add(probes, new NativeCase("native.opensl", "in_scope", "slCreateEngine", "SLObjectItf.Realize",
                "SLEngineItf.CreateOutputMix", "SLEngineItf.CreateAudioPlayer", "SLPlayItf.SetPlayState",
                "SLAndroidSimpleBufferQueueItf.RegisterCallback", "SLAndroidSimpleBufferQueueItf.Enqueue", "SLObjectItf.Destroy") {
            @Override Outcome measure(ProbeSession session) throws Exception {
                long[] value = NativeProbe.openSl();
                String[] stages = {"engine_create", "engine_realize", "engine_interface", "mix_create", "mix_realize",
                        "player_create", "player_realize", "play_interface", "queue_interface", "register_callback",
                        "enqueue_1", "enqueue_2", "enqueue_3", "play", "stop"};
                String last = value[0] > 0 && value[0] <= stages.length ? stages[(int) value[0] - 1] : "unknown";
                return check(value[0] == 15 && value[1] == 0 && value[2] == 3 && value[3] == 3,
                        obj("last_stage", last, "sl_result", value[1], "queued", value[2], "callbacks", value[3]),
                        obj("last_stage", "stop", "sl_result", 0, "queued", 3, "callbacks", 3),
                        "The OpenSL ES queue plays and acknowledges exactly three PCM buffers");
            }
        });
    }

    private static long fnv(byte[] bytes) {
        int hash = 0x811c9dc5;
        for (byte value : bytes) hash = (hash ^ (value & 255)) * 16777619;
        return hash & 0xffffffffL;
    }

    private static Probe.Outcome fields(long[] actual, long[] expected, String message, String... names) throws JSONException {
        JSONObject observed = new JSONObject();
        JSONObject wanted = new JSONObject();
        if (actual.length != names.length || expected.length != names.length) throw new IllegalArgumentException("Native result shape");
        for (int i = 0; i < names.length; i++) {
            observed.put(names[i], actual[i]);
            wanted.put(names[i], expected[i]);
        }
        return Probe.check(Arrays.equals(actual, expected), observed, wanted, message);
    }

    private abstract static class NativeCase extends Probe {
        NativeCase(String id, String scope, String... api) { super(id, scope, false, api); }

        abstract Outcome measure(ProbeSession session) throws Exception;

        @Override final Outcome run(ProbeSession session) throws Exception {
            NativeProbe.load();
            return measure(session);
        }
    }
}
