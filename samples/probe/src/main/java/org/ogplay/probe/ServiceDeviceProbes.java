package org.ogplay.probe;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.content.ComponentName;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Criteria;
import android.location.GpsStatus;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.location.LocationProvider;
import android.media.AudioManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.Looper;
import android.os.Vibrator;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyManager;
import java.util.List;
import java.util.Map;
import org.ogplay.probe.ApiSweep.Step;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;

/** API sweeps for the radio, sensor, audio and location services. */
final class ServiceDeviceProbes {
    private static final String CM = "Landroid/net/ConnectivityManager;";
    private static final String NI = "Landroid/net/NetworkInfo;";
    private static final String WIFI = "Landroid/net/wifi/WifiManager;";
    private static final String WIFI_INFO = "Landroid/net/wifi/WifiInfo;";
    private static final String WIFI_LOCK = "Landroid/net/wifi/WifiManager$WifiLock;";
    private static final String TM = "Landroid/telephony/TelephonyManager;";
    private static final String SM = "Landroid/hardware/SensorManager;";
    private static final String SENSOR = "Landroid/hardware/Sensor;";
    private static final String AUDIO = "Landroid/media/AudioManager;";
    private static final String LM = "Landroid/location/LocationManager;";
    private static final String LOCATION = "Landroid/location/Location;";
    private static final String BT = "Landroid/bluetooth/BluetoothAdapter;";
    private static final String BTM = "Landroid/bluetooth/BluetoothManager;";
    private static final String VIBRATOR = "Landroid/os/Vibrator;";

    static void register(Map<String, Probe> probes) {
        connectivity(probes);
        wifi(probes);
        telephony(probes);
        sensors(probes);
        audio(probes);
        location(probes);
        bluetooth(probes);
        vibrator(probes);
    }

    private static void connectivity(Map<String, Probe> probes) {
        add(probes, new Probe("services.connectivity.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final ConnectivityManager cm = (ConnectivityManager) session.activity.getSystemService(Context.CONNECTIVITY_SERVICE);
                NetworkInfo active = (NetworkInfo) s.call(CM, "getActiveNetworkInfo", new Step() { Object run() { return cm.getActiveNetworkInfo(); } });
                NetworkInfo[] all = (NetworkInfo[]) s.call(CM, "getAllNetworkInfo", new Step() { Object run() { return cm.getAllNetworkInfo(); } });
                NetworkInfo wifi = (NetworkInfo) s.call(CM, "getNetworkInfo", "wifi", new Step() { Object run() { return cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI); } });
                NetworkInfo mobile = (NetworkInfo) s.call(CM, "getNetworkInfo", "mobile", new Step() { Object run() { return cm.getNetworkInfo(ConnectivityManager.TYPE_MOBILE); } });
                Object invalid = s.call(CM, "getNetworkInfo", "invalid", new Step() { Object run() { return cm.getNetworkInfo(-5); } });
                s.call(CM, "isActiveNetworkMetered", new Step() { Object run() { return cm.isActiveNetworkMetered(); } });
                s.call(CM, "getBackgroundDataSetting", new Step() { Object run() { return cm.getBackgroundDataSetting(); } });
                s.call(CM, "getNetworkPreference", new Step() { Object run() { return cm.getNetworkPreference(); } });
                boolean consistent = true;
                boolean anyConnected = false;
                for (final NetworkInfo info : new NetworkInfo[] {wifi, mobile}) {
                    if (info == null) continue;
                    final String label = info.getType() == ConnectivityManager.TYPE_WIFI ? "wifi" : "mobile";
                    s.call(NI, "getTypeName", label, new Step() { Object run() { return info.getTypeName(); } });
                    s.call(NI, "getSubtype", label, new Step() { Object run() { return info.getSubtype(); } });
                    s.call(NI, "getSubtypeName", label, new Step() { Object run() { return info.getSubtypeName(); } });
                    s.call(NI, "getState", label, new Step() { Object run() { return String.valueOf(info.getState()); } });
                    s.call(NI, "getDetailedState", label, new Step() { Object run() { return String.valueOf(info.getDetailedState()); } });
                    s.call(NI, "isAvailable", label, new Step() { Object run() { return info.isAvailable(); } });
                    s.call(NI, "isRoaming", label, new Step() { Object run() { return info.isRoaming(); } });
                    s.call(NI, "isFailover", label, new Step() { Object run() { return info.isFailover(); } });
                    s.call(NI, "getExtraInfo", label, new Step() { Object run() { return info.getExtraInfo(); } });
                    s.call(NI, "getReason", label, new Step() { Object run() { return info.getReason(); } });
                    consistent &= (info.getState() == NetworkInfo.State.CONNECTED) == info.isConnected();
                    consistent &= !info.isConnected() || info.isConnectedOrConnecting();
                    anyConnected |= info.isConnected();
                }
                s.require("wifi_network_present", wifi != null && wifi.getType() == ConnectivityManager.TYPE_WIFI
                                && "WIFI".equals(wifi.getTypeName()))
                        .require("mobile_network_present", mobile != null && mobile.getType() == ConnectivityManager.TYPE_MOBILE
                                && "mobile".equalsIgnoreCase(mobile.getTypeName()))
                        .require("inventory_lists_both", all != null && all.length >= 2)
                        .require("invalid_type_is_null", invalid == null)
                        .require("state_flags_consistent", consistent)
                        .require("active_network_matches_inventory", active == null ? !anyConnected : active.isConnectedOrConnecting());
                return s.outcome(this, obj("networks", "wifi and mobile", "invalid_type", null, "state_flags", "consistent"),
                        "ConnectivityManager and NetworkInfo describe the phone's networks consistently");
            }
        });
    }

    private static void wifi(Map<String, Probe> probes) {
        add(probes, new Probe("services.wifi.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final WifiManager wm = (WifiManager) session.activity.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                Boolean enabled = (Boolean) s.call(WIFI, "isWifiEnabled", new Step() { Object run() { return wm.isWifiEnabled(); } });
                Integer state = (Integer) s.call(WIFI, "getWifiState", new Step() { Object run() { return wm.getWifiState(); } });
                final WifiInfo info = (WifiInfo) s.call(WIFI, "getConnectionInfo", new Step() { Object run() { return wm.getConnectionInfo(); } });
                String mac = null;
                Integer networkId = null;
                Integer ip = null;
                if (info != null) {
                    mac = (String) s.identifier(WIFI_INFO, "getMacAddress", new Step() { Object run() { return info.getMacAddress(); } });
                    s.call(WIFI_INFO, "getSSID", new Step() { Object run() { return info.getSSID(); } });
                    s.identifier(WIFI_INFO, "getBSSID", new Step() { Object run() { return info.getBSSID(); } });
                    ip = (Integer) s.call(WIFI_INFO, "getIpAddress", new Step() { Object run() { return info.getIpAddress(); } });
                    s.call(WIFI_INFO, "getLinkSpeed", new Step() { Object run() { return info.getLinkSpeed(); } });
                    s.call(WIFI_INFO, "getRssi", new Step() { Object run() { return info.getRssi(); } });
                    networkId = (Integer) s.call(WIFI_INFO, "getNetworkId", new Step() { Object run() { return info.getNetworkId(); } });
                    s.call(WIFI_INFO, "getSupplicantState", new Step() { Object run() { return String.valueOf(info.getSupplicantState()); } });
                    s.call(WIFI_INFO, "getHiddenSSID", new Step() { Object run() { return info.getHiddenSSID(); } });
                }
                s.call(WIFI, "getDhcpInfo", new Step() { Object run() { return wm.getDhcpInfo(); } });
                s.call(WIFI, "getConfiguredNetworks", new Step() { Object run() { return wm.getConfiguredNetworks(); } });
                s.call(WIFI, "getScanResults", new Step() { Object run() { return wm.getScanResults(); } });
                s.call(WIFI, "isScanAlwaysAvailable", new Step() { Object run() { return wm.isScanAlwaysAvailable(); } });
                Integer level = (Integer) s.call(WIFI, "calculateSignalLevel", new Step() { Object run() { return WifiManager.calculateSignalLevel(-60, 5); } });
                Integer compare = (Integer) s.call(WIFI, "compareSignalLevel", new Step() { Object run() { return WifiManager.compareSignalLevel(-60, -70); } });
                final WifiManager.WifiLock lock = (WifiManager.WifiLock) s.call(WIFI, "createWifiLock", new Step() {
                    Object run() { return wm.createWifiLock(WifiManager.WIFI_MODE_FULL, "OGProbe"); }
                });
                Boolean held = lock == null ? null : (Boolean) s.call(WIFI_LOCK, "acquire", new Step() { Object run() { lock.acquire(); return lock.isHeld(); } });
                Boolean released = lock == null ? null : (Boolean) s.call(WIFI_LOCK, "release", new Step() { Object run() { lock.release(); return !lock.isHeld(); } });
                final boolean current = Boolean.TRUE.equals(enabled);
                s.observe(WIFI, "setWifiEnabled", "without_change_wifi_state", new Step() { Object run() { return wm.setWifiEnabled(current); } });
                boolean connected = networkId != null && networkId >= 0;
                s.require("enabled_matches_state", enabled != null && state != null
                                && enabled == (state == WifiManager.WIFI_STATE_ENABLED))
                        .require("connection_info_present", info != null)
                        .require("mac_format", mac == null || mac.matches("([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}"))
                        .require("disconnected_has_no_address", connected || ip == null || ip == 0)
                        .require("signal_level_math", level != null && level == 3 && compare != null && compare > 0)
                        .require("wifi_lock_cycle", Boolean.TRUE.equals(held) && Boolean.TRUE.equals(released));
                return s.outcome(this, obj("enabled_matches_state", true, "mac", "xx:xx:xx:xx:xx:xx", "signal_level(-60,5)", 3,
                        "wifi_lock", "acquire/release"), "WifiManager getters agree and a Wi-Fi lock can be held");
            }
        });
    }

    private static void telephony(Map<String, Probe> probes) {
        add(probes, new Probe("services.telephony.api", "services", "in_scope", true, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final TelephonyManager tm = (TelephonyManager) session.activity.getSystemService(Context.TELEPHONY_SERVICE);
                String imei = (String) s.identifier(TM, "getDeviceId", new Step() { Object run() { return tm.getDeviceId(); } });
                s.call(TM, "getDeviceSoftwareVersion", new Step() { Object run() { return tm.getDeviceSoftwareVersion(); } });
                s.identifier(TM, "getLine1Number", new Step() { Object run() { return tm.getLine1Number(); } });
                s.identifier(TM, "getVoiceMailNumber", new Step() { Object run() { return tm.getVoiceMailNumber(); } });
                String operator = (String) s.call(TM, "getNetworkOperator", new Step() { Object run() { return tm.getNetworkOperator(); } });
                s.call(TM, "getNetworkOperatorName", new Step() { Object run() { return tm.getNetworkOperatorName(); } });
                String networkIso = (String) s.call(TM, "getNetworkCountryIso", new Step() { Object run() { return tm.getNetworkCountryIso(); } });
                s.call(TM, "getNetworkType", new Step() { Object run() { return tm.getNetworkType(); } });
                Integer phoneType = (Integer) s.call(TM, "getPhoneType", new Step() { Object run() { return tm.getPhoneType(); } });
                Integer simState = (Integer) s.call(TM, "getSimState", new Step() { Object run() { return tm.getSimState(); } });
                String simOperator = (String) s.call(TM, "getSimOperator", new Step() { Object run() { return tm.getSimOperator(); } });
                s.call(TM, "getSimOperatorName", new Step() { Object run() { return tm.getSimOperatorName(); } });
                String simIso = (String) s.call(TM, "getSimCountryIso", new Step() { Object run() { return tm.getSimCountryIso(); } });
                s.identifier(TM, "getSimSerialNumber", new Step() { Object run() { return tm.getSimSerialNumber(); } });
                String subscriber = (String) s.identifier(TM, "getSubscriberId", new Step() { Object run() { return tm.getSubscriberId(); } });
                s.call(TM, "isNetworkRoaming", new Step() { Object run() { return tm.isNetworkRoaming(); } });
                Integer callState = (Integer) s.call(TM, "getCallState", new Step() { Object run() { return tm.getCallState(); } });
                Integer dataState = (Integer) s.call(TM, "getDataState", new Step() { Object run() { return tm.getDataState(); } });
                s.call(TM, "getDataActivity", new Step() { Object run() { return tm.getDataActivity(); } });
                Boolean icc = (Boolean) s.call(TM, "hasIccCard", new Step() { Object run() { return tm.hasIccCard(); } });
                s.call(TM, "getCellLocation", new Step() { Object run() { return tm.getCellLocation(); } });
                s.call(TM, "getNeighboringCellInfo", new Step() { Object run() { return tm.getNeighboringCellInfo(); } });
                s.call(TM, "getAllCellInfo", new Step() { Object run() { return tm.getAllCellInfo(); } });
                s.call(TM, "getGroupIdLevel1", new Step() { Object run() { return tm.getGroupIdLevel1(); } });
                s.call(TM, "getMmsUserAgent", new Step() { Object run() { return tm.getMmsUserAgent(); } });
                final PhoneStateListener listener = new PhoneStateListener() {};
                s.call(TM, "listen", "call_service_signal", new Step() {
                    Object run() {
                        tm.listen(listener, PhoneStateListener.LISTEN_CALL_STATE | PhoneStateListener.LISTEN_SERVICE_STATE
                                | PhoneStateListener.LISTEN_SIGNAL_STRENGTHS | PhoneStateListener.LISTEN_DATA_CONNECTION_STATE);
                        return true;
                    }
                });
                s.call(TM, "listen", "none", new Step() { Object run() { tm.listen(listener, PhoneStateListener.LISTEN_NONE); return true; } });
                boolean absent = simState != null && simState == TelephonyManager.SIM_STATE_ABSENT;
                boolean ready = simState != null && simState == TelephonyManager.SIM_STATE_READY;
                s.require("phone_type_valid", phoneType != null && phoneType >= TelephonyManager.PHONE_TYPE_NONE
                                && phoneType <= TelephonyManager.PHONE_TYPE_SIP)
                        .require("sim_state_valid", simState != null && simState >= 0 && simState <= 5)
                        .require("gsm_has_imei", phoneType == null || phoneType != TelephonyManager.PHONE_TYPE_GSM
                                || imei != null && imei.matches("[0-9]{14,15}"))
                        .require("absent_sim_has_no_identity", !absent || (simOperator == null || simOperator.length() == 0)
                                && subscriber == null && Boolean.FALSE.equals(icc))
                        .require("ready_sim_has_operator", !ready || simOperator != null && simOperator.matches("[0-9]{5,6}"))
                        .require("operator_format", operator == null || operator.length() == 0 || operator.matches("[0-9]{5,6}"))
                        .require("country_iso_format", (networkIso == null || networkIso.matches("([a-z]{2})?"))
                                && (simIso == null || simIso.matches("([a-z]{2})?")))
                        .require("idle_call_state", callState != null && callState == TelephonyManager.CALL_STATE_IDLE)
                        .require("data_state_valid", dataState != null && dataState >= 0 && dataState <= 3);
                return s.outcome(this, obj("phone_type", "NONE..SIP", "call_state", "IDLE", "sim_identity", "consistent with SIM state",
                        "listener", "registered and removed"), "TelephonyManager values are well-formed and consistent with the SIM state");
            }
        });
    }

    private static void sensors(Map<String, Probe> probes) {
        add(probes, new Probe("services.sensor.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final SensorManager sm = (SensorManager) session.activity.getSystemService(Context.SENSOR_SERVICE);
                @SuppressWarnings("unchecked") final List<Sensor> list = (List<Sensor>) s.call(SM, "getSensorList", new Step() {
                    Object run() { return sm.getSensorList(Sensor.TYPE_ALL); }
                });
                boolean defaultsListed = true;
                int[] types = {Sensor.TYPE_ACCELEROMETER, Sensor.TYPE_MAGNETIC_FIELD, Sensor.TYPE_ORIENTATION, Sensor.TYPE_GYROSCOPE, Sensor.TYPE_LIGHT,
                        Sensor.TYPE_PROXIMITY, Sensor.TYPE_GRAVITY, Sensor.TYPE_LINEAR_ACCELERATION, Sensor.TYPE_ROTATION_VECTOR};
                for (final int type : types) {
                    final Sensor sensor = (Sensor) s.call(SM, "getDefaultSensor", String.valueOf(type), new Step() { Object run() { return sm.getDefaultSensor(type); } });
                    final List<?> typed = (List<?>) s.call(SM, "getSensorList", String.valueOf(type), new Step() {
                        Object run() { return sm.getSensorList(type); }
                    });
                    defaultsListed &= typed != null && (sensor == null ? typed.isEmpty()
                            : list != null && list.contains(sensor) && typed.contains(sensor) && sensor.getType() == type);
                    if (sensor == null) continue;
                    final String label = String.valueOf(type);
                    s.call(SENSOR, "getName", label, new Step() { Object run() { return sensor.getName(); } });
                    s.call(SENSOR, "getVendor", label, new Step() { Object run() { return sensor.getVendor(); } });
                    s.call(SENSOR, "getMaximumRange", label, new Step() { Object run() { return sensor.getMaximumRange(); } });
                    s.call(SENSOR, "getResolution", label, new Step() { Object run() { return sensor.getResolution(); } });
                    s.call(SENSOR, "getMinDelay", label, new Step() { Object run() { return sensor.getMinDelay(); } });
                }
                final SensorEventListener listener = new SensorEventListener() {
                    @Override public void onSensorChanged(SensorEvent event) {}
                    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
                };
                Boolean nullSensor = (Boolean) s.call(SM, "registerListener", "null_sensor", new Step() {
                    Object run() { return sm.registerListener(listener, null, SensorManager.SENSOR_DELAY_GAME); }
                });
                s.call(SM, "unregisterListener", new Step() { Object run() { sm.unregisterListener(listener); return true; } });
                s.call(SM, "getSensors", new Step() { Object run() { return sm.getSensors(); } });
                final float[] rotation = new float[9];
                final float[] inclination = new float[9];
                Boolean solved = (Boolean) s.call(SM, "getRotationMatrix", new Step() {
                    Object run() { return SensorManager.getRotationMatrix(rotation, inclination, new float[] {0f, 0f, 9.81f}, new float[] {0f, 25f, -40f}); }
                });
                final float[] angles = new float[3];
                s.call(SM, "getOrientation", new Step() { Object run() { SensorManager.getOrientation(rotation, angles); return true; } });
                final float[] remapped = new float[9];
                Boolean remap = (Boolean) s.call(SM, "remapCoordinateSystem", new Step() {
                    Object run() { return SensorManager.remapCoordinateSystem(rotation, SensorManager.AXIS_X, SensorManager.AXIS_Z, remapped); }
                });
                boolean identity = true;
                for (int i = 0; i < 9; i++) identity &= Math.abs(rotation[i] - (i % 4 == 0 ? 1f : 0f)) < 1e-4f;
                boolean level = Math.abs(angles[0]) < 1e-4f && Math.abs(angles[1]) < 1e-4f && Math.abs(angles[2]) < 1e-4f;
                s.require("sensor_list_present", list != null)
                        .require("default_sensors_are_listed", defaultsListed)
                        .require("null_sensor_registration_false", Boolean.FALSE.equals(nullSensor))
                        .require("rotation_matrix_identity", Boolean.TRUE.equals(solved) && identity)
                        .require("orientation_level_north", level)
                        .require("remap_succeeds", Boolean.TRUE.equals(remap));
                return s.outcome(this, obj("defaults_listed", true, "null_sensor_registration", false, "rotation_matrix", "identity",
                        "orientation_rad", "[0,0,0]"), "SensorManager inventory and the ROM orientation math agree");
            }
        });
    }

    private static void audio(Map<String, Probe> probes) {
        add(probes, new Probe("services.audio.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final AudioManager am = (AudioManager) session.activity.getSystemService(Context.AUDIO_SERVICE);
                Integer ringer = (Integer) s.call(AUDIO, "getRingerMode", new Step() { Object run() { return am.getRingerMode(); } });
                Integer mode = (Integer) s.call(AUDIO, "getMode", new Step() { Object run() { return am.getMode(); } });
                s.call(AUDIO, "isMusicActive", new Step() { Object run() { return am.isMusicActive(); } });
                s.call(AUDIO, "isSpeakerphoneOn", new Step() { Object run() { return am.isSpeakerphoneOn(); } });
                s.call(AUDIO, "isBluetoothA2dpOn", new Step() { Object run() { return am.isBluetoothA2dpOn(); } });
                s.call(AUDIO, "isBluetoothScoOn", new Step() { Object run() { return am.isBluetoothScoOn(); } });
                s.call(AUDIO, "isWiredHeadsetOn", new Step() { Object run() { return am.isWiredHeadsetOn(); } });
                s.call(AUDIO, "isMicrophoneMute", new Step() { Object run() { return am.isMicrophoneMute(); } });
                boolean ranges = true;
                int[] streams = {AudioManager.STREAM_VOICE_CALL, AudioManager.STREAM_SYSTEM, AudioManager.STREAM_RING,
                        AudioManager.STREAM_MUSIC, AudioManager.STREAM_ALARM, AudioManager.STREAM_NOTIFICATION};
                for (final int stream : streams) {
                    Integer volume = (Integer) s.call(AUDIO, "getStreamVolume", String.valueOf(stream), new Step() { Object run() { return am.getStreamVolume(stream); } });
                    Integer max = (Integer) s.call(AUDIO, "getStreamMaxVolume", String.valueOf(stream), new Step() { Object run() { return am.getStreamMaxVolume(stream); } });
                    ranges &= volume != null && max != null && max > 0 && volume >= 0 && volume <= max;
                }
                Integer vibrate = (Integer) s.call(AUDIO, "getVibrateSetting", new Step() { Object run() { return am.getVibrateSetting(AudioManager.VIBRATE_TYPE_RINGER); } });
                s.call(AUDIO, "shouldVibrate", new Step() { Object run() { return am.shouldVibrate(AudioManager.VIBRATE_TYPE_NOTIFICATION); } });
                String rate = (String) s.call(AUDIO, "getProperty", "sample_rate", new Step() { Object run() { return am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE); } });
                String frames = (String) s.call(AUDIO, "getProperty", "frames_per_buffer", new Step() {
                    Object run() { return am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER); }
                });
                s.call(AUDIO, "adjustStreamVolume", new Step() { Object run() { am.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_SAME, 0); return true; } });
                s.call(AUDIO, "loadSoundEffects", new Step() { Object run() { am.loadSoundEffects(); return true; } });
                s.call(AUDIO, "playSoundEffect", new Step() { Object run() { am.playSoundEffect(AudioManager.FX_KEY_CLICK); return true; } });
                s.call(AUDIO, "unloadSoundEffects", new Step() { Object run() { am.unloadSoundEffects(); return true; } });
                final AudioManager.OnAudioFocusChangeListener focus = new AudioManager.OnAudioFocusChangeListener() {
                    @Override public void onAudioFocusChange(int change) {}
                };
                Integer gain = (Integer) s.call(AUDIO, "requestAudioFocus", new Step() {
                    Object run() { return am.requestAudioFocus(focus, AudioManager.STREAM_MUSIC, AudioManager.AUDIOFOCUS_GAIN); }
                });
                Integer abandon = (Integer) s.call(AUDIO, "abandonAudioFocus", new Step() { Object run() { return am.abandonAudioFocus(focus); } });
                final ComponentName receiver = new ComponentName(session.activity.getPackageName(), "org.ogplay.probe.MediaButtonReceiver");
                s.call(AUDIO, "registerMediaButtonEventReceiver", new Step() { Object run() { am.registerMediaButtonEventReceiver(receiver); return true; } });
                s.call(AUDIO, "unregisterMediaButtonEventReceiver", new Step() { Object run() { am.unregisterMediaButtonEventReceiver(receiver); return true; } });
                s.require("ringer_mode_valid", ringer != null && ringer >= AudioManager.RINGER_MODE_SILENT && ringer <= AudioManager.RINGER_MODE_NORMAL)
                        .require("mode_normal", mode != null && mode == AudioManager.MODE_NORMAL)
                        .require("stream_volumes_in_range", ranges)
                        .require("vibrate_setting_valid", vibrate != null && vibrate >= 0 && vibrate <= 2)
                        .require("output_sample_rate_numeric", rate != null && rate.matches("[1-9][0-9]{3,5}"))
                        .require("output_frames_numeric", frames != null && frames.matches("[1-9][0-9]*"))
                        .require("focus_granted_and_released", gain != null && gain == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                                && abandon != null && abandon == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
                return s.outcome(this, obj("mode", "NORMAL", "volumes", "0..max for six streams", "output_properties", "numeric",
                        "audio_focus", "granted"), "AudioManager routing, volume, property and focus APIs follow API 19");
            }
        });
    }

    private static void location(Map<String, Probe> probes) {
        add(probes, new Probe("services.location.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                // GpsStatus listeners create a Handler on the calling thread, as on a device.
                if (Looper.myLooper() == null) Looper.prepare();
                final ApiSweep s = new ApiSweep();
                final LocationManager lm = (LocationManager) session.activity.getSystemService(Context.LOCATION_SERVICE);
                @SuppressWarnings("unchecked") List<String> all = (List<String>) s.call(LM, "getAllProviders", new Step() { Object run() { return lm.getAllProviders(); } });
                @SuppressWarnings("unchecked") List<String> listed = (List<String>) s.call(LM, "getProviders", "all", new Step() { Object run() { return lm.getProviders(false); } });
                @SuppressWarnings("unchecked") List<String> enabled = (List<String>) s.call(LM, "getProviders", "enabled", new Step() { Object run() { return lm.getProviders(true); } });
                boolean enabledConsistent = enabled != null && listed != null && listed.containsAll(enabled);
                if (all != null) for (final String name : all) {
                    Boolean on = (Boolean) s.call(LM, "isProviderEnabled", name, new Step() { Object run() { return lm.isProviderEnabled(name); } });
                    s.call(LM, "getLastKnownLocation", name, new Step() { Object run() { return lm.getLastKnownLocation(name); } });
                    final LocationProvider provider = (LocationProvider) s.call(LM, "getProvider", name, new Step() { Object run() { return lm.getProvider(name); } });
                    if (provider != null) {
                        s.call("Landroid/location/LocationProvider;", "getAccuracy", name, new Step() { Object run() { return provider.getAccuracy(); } });
                        s.call("Landroid/location/LocationProvider;", "requiresNetwork", name, new Step() { Object run() { return provider.requiresNetwork(); } });
                    }
                    enabledConsistent &= enabled == null || on == null || on == enabled.contains(name);
                }
                final Criteria criteria = new Criteria();
                criteria.setAccuracy(Criteria.ACCURACY_COARSE);
                String best = (String) s.call(LM, "getBestProvider", "any", new Step() { Object run() { return lm.getBestProvider(criteria, false); } });
                String bestEnabled = (String) s.call(LM, "getBestProvider", "enabled", new Step() { Object run() { return lm.getBestProvider(criteria, true); } });
                final LocationListener listener = new LocationListener() {
                    @Override public void onLocationChanged(Location location) {}
                    @Override public void onStatusChanged(String provider, int status, Bundle extras) {}
                    @Override public void onProviderEnabled(String provider) {}
                    @Override public void onProviderDisabled(String provider) {}
                };
                s.call(LM, "requestLocationUpdates", "gps", new Step() {
                    Object run() { lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000, 0, listener, Looper.getMainLooper()); return true; }
                });
                s.call(LM, "requestSingleUpdate", "passive", new Step() {
                    Object run() { lm.requestSingleUpdate(LocationManager.PASSIVE_PROVIDER, listener, Looper.getMainLooper()); return true; }
                });
                s.call(LM, "removeUpdates", new Step() { Object run() { lm.removeUpdates(listener); return true; } });
                s.throwsType(LM, "requestLocationUpdates", "missing", IllegalArgumentException.class, new Step() {
                    Object run() { lm.requestLocationUpdates("ogplay_missing", 1000, 0, listener, Looper.getMainLooper()); return true; }
                });
                lm.removeUpdates(listener);
                final GpsStatus.Listener gps = new GpsStatus.Listener() { @Override public void onGpsStatusChanged(int event) {} };
                s.call(LM, "addGpsStatusListener", new Step() { Object run() { return lm.addGpsStatusListener(gps); } });
                s.call(LM, "removeGpsStatusListener", new Step() { Object run() { lm.removeGpsStatusListener(gps); return true; } });
                final float[] distance = new float[2];
                s.call(LOCATION, "distanceBetween", new Step() { Object run() { Location.distanceBetween(0, 0, 0, 1, distance); return distance[0]; } });
                Float bearing = (Float) s.call(LOCATION, "bearingTo", new Step() {
                    Object run() {
                        Location from = new Location("probe");
                        Location to = new Location("probe");
                        to.setLatitude(1);
                        return from.bearingTo(to);
                    }
                });
                s.require("passive_and_gps_exist", all != null && all.contains(LocationManager.PASSIVE_PROVIDER)
                                && all.contains(LocationManager.GPS_PROVIDER))
                        .require("enabled_list_matches_flags", enabledConsistent)
                        .require("best_provider_any_present", best != null)
                        .require("best_enabled_provider_is_enabled", bestEnabled == null || enabled != null && enabled.contains(bestEnabled))
                        .require("equator_degree_distance", Math.abs(distance[0] - 111319.49f) < 1f)
                        .require("north_bearing", bearing != null && Math.abs(bearing) < 1e-3f);
                return s.outcome(this, obj("providers", "passive and gps exist", "missing_provider", "IllegalArgumentException",
                        "distance(0,0→0,1)_m", 111319.49, "bearing_north", 0), "LocationManager queries, subscriptions and Location math follow API 19");
            }
        });
    }

    private static void bluetooth(Map<String, Probe> probes) {
        add(probes, new Probe("services.bluetooth.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final BluetoothAdapter adapter = (BluetoothAdapter) s.call(BT, "getDefaultAdapter", new Step() { Object run() { return BluetoothAdapter.getDefaultAdapter(); } });
                final BluetoothManager manager = (BluetoothManager) session.activity.getSystemService(Context.BLUETOOTH_SERVICE);
                Object managed = s.call(BTM, "getAdapter", new Step() { Object run() { return manager == null ? null : manager.getAdapter(); } });
                Boolean valid = (Boolean) s.call(BT, "checkBluetoothAddress", new Step() {
                    Object run() { return BluetoothAdapter.checkBluetoothAddress("00:11:22:AA:BB:CC") && !BluetoothAdapter.checkBluetoothAddress("probe"); }
                });
                if (adapter == null) {
                    s.require("adapter_present", false);
                    return s.outcome(this, obj("adapter", "present"), "A phone exposes its Bluetooth adapter");
                }
                Boolean enabled = (Boolean) s.call(BT, "isEnabled", new Step() { Object run() { return adapter.isEnabled(); } });
                Integer state = (Integer) s.call(BT, "getState", new Step() { Object run() { return adapter.getState(); } });
                s.call(BT, "getName", new Step() { Object run() { return adapter.getName(); } });
                String address = (String) s.identifier(BT, "getAddress", new Step() { Object run() { return adapter.getAddress(); } });
                Integer scan = (Integer) s.call(BT, "getScanMode", new Step() { Object run() { return adapter.getScanMode(); } });
                Boolean discovering = (Boolean) s.call(BT, "isDiscovering", new Step() { Object run() { return adapter.isDiscovering(); } });
                Object bonded = s.call(BT, "getBondedDevices", new Step() { Object run() { return adapter.getBondedDevices(); } });
                Integer headset = (Integer) s.call(BT, "getProfileConnectionState", new Step() { Object run() { return adapter.getProfileConnectionState(BluetoothProfile.HEADSET); } });
                Boolean start = (Boolean) s.call(BT, "startDiscovery", new Step() { Object run() { return adapter.startDiscovery(); } });
                Boolean cancel = (Boolean) s.call(BT, "cancelDiscovery", new Step() { Object run() { return adapter.cancelDiscovery(); } });
                boolean off = Boolean.FALSE.equals(enabled);
                s.require("manager_returns_default_adapter", managed == adapter)
                        .require("address_validation", Boolean.TRUE.equals(valid))
                        .require("enabled_matches_state", enabled != null && state != null && enabled == (state == BluetoothAdapter.STATE_ON))
                        .require("address_format", address == null || address.matches("([0-9A-F]{2}:){5}[0-9A-F]{2}"))
                        .require("bonded_set_present", bonded != null)
                        .require("off_adapter_is_idle", !off || scan != null && scan == BluetoothAdapter.SCAN_MODE_NONE
                                && Boolean.FALSE.equals(discovering) && headset != null && headset == BluetoothProfile.STATE_DISCONNECTED
                                && Boolean.FALSE.equals(start) && Boolean.FALSE.equals(cancel));
                return s.outcome(this, obj("adapter", "present", "enabled_matches_state", true, "when_off",
                        "scan NONE, not discovering, discovery returns false"), "BluetoothAdapter follows the API 19 state rules");
            }
        });
    }

    private static void vibrator(Map<String, Probe> probes) {
        add(probes, new Probe("services.vibrator.api", "services", "in_scope", false, 5000) {
            @Override Outcome run(final ProbeSession session) throws Exception {
                final ApiSweep s = new ApiSweep();
                final Vibrator vibrator = (Vibrator) session.activity.getSystemService(Context.VIBRATOR_SERVICE);
                s.call(VIBRATOR, "hasVibrator", new Step() { Object run() { return vibrator.hasVibrator(); } });
                s.call(VIBRATOR, "vibrate", "duration", new Step() { Object run() { vibrator.vibrate(15); return true; } });
                s.call(VIBRATOR, "vibrate", "pattern_once", new Step() { Object run() { vibrator.vibrate(new long[] {0, 10, 20, 10}, -1); return true; } });
                s.call(VIBRATOR, "vibrate", "pattern_repeat", new Step() { Object run() { vibrator.vibrate(new long[] {0, 10, 20}, 0); return true; } });
                s.call(VIBRATOR, "cancel", new Step() { Object run() { vibrator.cancel(); return true; } });
                s.throwsType(VIBRATOR, "vibrate", "repeat_out_of_range", ArrayIndexOutOfBoundsException.class, new Step() {
                    Object run() { vibrator.vibrate(new long[] {0, 10, 20}, 5); return true; }
                });
                vibrator.cancel();
                return s.outcome(this, obj("vibration_requests", "accepted", "repeat_out_of_range", "ArrayIndexOutOfBoundsException"),
                        "Vibrator accepts duration and pattern requests and validates the repeat index");
            }
        });
    }
}
