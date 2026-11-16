package org.ogplay.probe;

import android.content.res.AssetFileDescriptor;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaPlayer;
import android.media.SoundPool;
import android.os.SystemClock;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

final class MediaProbes {
    static void register(Map<String, Probe> probes) {
        add(probes, new Probe("media.audio_track.stream", "in_scope", false,
                "Landroid/media/AudioTrack;.getMinBufferSize(III)I",
                "Landroid/media/AudioTrack;.write([SII)I", "Landroid/media/AudioTrack;.play()V",
                "Landroid/media/AudioTrack;.getPlaybackHeadPosition()I", "Landroid/media/AudioTrack;.stop()V",
                "Landroid/media/AudioTrack;.flush()V", "Landroid/media/AudioTrack;.release()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                int minimum = AudioTrack.getMinBufferSize(8000, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
                if (minimum <= 0) return check(false, obj("minimum_buffer", minimum), obj("minimum_buffer_positive", true),
                        "AudioTrack must advertise a buffer for mono 8kHz PCM");
                AudioTrack track = new AudioTrack(AudioManager.STREAM_MUSIC, 8000, AudioFormat.CHANNEL_OUT_MONO,
                        AudioFormat.ENCODING_PCM_16BIT, Math.max(minimum, 8000), AudioTrack.MODE_STREAM);
                try {
                    if (track.getState() != AudioTrack.STATE_INITIALIZED) {
                        return check(false, obj("state", track.getState()), obj("state", AudioTrack.STATE_INITIALIZED),
                                "The stream could not be initialized");
                    }
                    short[] data = new short[4000];
                    for (int i = 0; i < data.length; i++) data[i] = (short) (Math.sin(i * Math.PI / 10) * 3000);
                    int headBefore = track.getPlaybackHeadPosition();
                    track.play();
                    int written = 0;
                    int lastWrite = 0;
                    while (written < data.length) {
                        lastWrite = track.write(data, written, data.length - written);
                        if (lastWrite <= 0) break;
                        written += lastWrite;
                    }
                    long until = SystemClock.elapsedRealtime() + 1100;
                    int head = track.getPlaybackHeadPosition();
                    boolean increasing = true;
                    while (head < data.length && SystemClock.elapsedRealtime() < until) {
                        Thread.sleep(50);
                        int current = track.getPlaybackHeadPosition();
                        increasing &= current >= head;
                        head = current;
                    }
                    track.stop();
                    track.flush();
                    return check(written == data.length && increasing && head > headBefore && head <= data.length,
                            obj("written_samples", written, "last_write", lastWrite, "head_before", headBefore,
                                    "head_after", head, "head_monotonic", increasing, "stopped_and_flushed", true),
                            obj("written_samples", data.length, "head_advanced", true, "head_bounded_by_samples", true),
                            "A streaming track accepts half a second of PCM and the playback head advances");
                } finally { track.release(); }
            }
        });
        add(probes, new Probe("media.player.raw_completion", "in_scope", false,
                "Landroid/media/MediaPlayer;.create(Landroid/content/Context;I)Landroid/media/MediaPlayer;",
                "Landroid/media/MediaPlayer;.start()V",
                "Landroid/media/MediaPlayer;.setOnCompletionListener(Landroid/media/MediaPlayer$OnCompletionListener;)V",
                "Landroid/media/MediaPlayer;.setOnErrorListener(Landroid/media/MediaPlayer$OnErrorListener;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final CountDownLatch completed = new CountDownLatch(1);
                final long[] when = {-1};
                final int[] error = {0, 0};
                MediaPlayer player = MediaPlayer.create(session.activity, R.raw.probe_tone);
                if (player == null) return check(false, obj("created", false), obj("created", true),
                        "MediaPlayer could not open the packaged WAV fixture");
                try {
                    player.setOnCompletionListener(new MediaPlayer.OnCompletionListener() {
                        @Override public void onCompletion(MediaPlayer finished) {
                            when[0] = SystemClock.elapsedRealtime();
                            completed.countDown();
                        }
                    });
                    player.setOnErrorListener(new MediaPlayer.OnErrorListener() {
                        @Override public boolean onError(MediaPlayer source, int what, int extra) {
                            error[0] = what;
                            error[1] = extra;
                            completed.countDown();
                            return true;
                        }
                    });
                    int duration = player.getDuration();
                    long started = SystemClock.elapsedRealtime();
                    player.start();
                    boolean callback = completed.await(2200, TimeUnit.MILLISECONDS);
                    Long elapsed = callback && when[0] >= started ? when[0] - started : null;
                    return check(duration >= 470 && duration <= 530 && elapsed != null && elapsed >= 400
                                    && elapsed <= 1700 && error[0] == 0,
                            obj("duration_ms", duration, "completion_ms", elapsed, "error", error[0], "extra", error[1]),
                            obj("duration_ms", 500, "completion_min_ms", 400, "completion_max_ms", 1700, "error", 0),
                            "A packaged half-second WAV emits a completion callback near its media duration");
                } finally { player.release(); }
            }
        });
        add(probes, new Probe("media.sound_pool.load_play", "in_scope", false,
                "Landroid/media/SoundPool;.load(Landroid/content/res/AssetFileDescriptor;I)I",
                "Landroid/media/SoundPool;.setOnLoadCompleteListener(Landroid/media/SoundPool$OnLoadCompleteListener;)V",
                "Landroid/media/SoundPool;.play(IFFIIF)I") {
            @Override Outcome run(ProbeSession session) throws Exception {
                final CountDownLatch loaded = new CountDownLatch(1);
                final int[] event = {-1, -1};
                SoundPool pool = new SoundPool(2, AudioManager.STREAM_MUSIC, 0);
                try {
                    pool.setOnLoadCompleteListener(new SoundPool.OnLoadCompleteListener() {
                        @Override public void onLoadComplete(SoundPool source, int id, int status) {
                            event[0] = id;
                            event[1] = status;
                            loaded.countDown();
                        }
                    });
                    AssetFileDescriptor fd = session.activity.getResources().openRawResourceFd(R.raw.probe_tone);
                    int sound;
                    try { sound = pool.load(fd, 1); } finally { fd.close(); }
                    boolean callback = sound != 0 && loaded.await(1500, TimeUnit.MILLISECONDS);
                    int stream = callback && event[1] == 0 ? pool.play(sound, 0.4f, 0.4f, 1, 0, 1f) : 0;
                    if (stream != 0) pool.stop(stream);
                    return check(sound != 0 && callback && event[0] == sound && event[1] == 0 && stream != 0,
                            obj("sound_id", sound, "load_callback", callback, "callback_id", event[0],
                                    "load_status", event[1], "stream_id", stream),
                            obj("loaded_id_matches", true, "load_status", 0, "nonzero_stream", true),
                            "SoundPool loads the packaged sample asynchronously and creates a playing stream");
                } finally { pool.release(); }
            }
        });
    }
}
