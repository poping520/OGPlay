package org.ogplay.probe;

import java.lang.reflect.InvocationTargetException;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Calls a list of framework methods independently. A thrown exception is recorded for that call and the sweep
 * continues, so one report shows every failing method of a service instead of only the first one.
 */
final class ApiSweep {
    abstract static class Step {
        abstract Object run() throws Exception;
    }

    private final JSONObject calls = new JSONObject();
    private final JSONArray failedCalls = new JSONArray();
    private final Probe.Checks checks = new Probe.Checks();
    private final Set<String> api = new LinkedHashSet<String>();
    private Throwable first;

    /** Runs a call and records a JSON summary of its result. */
    Object call(String owner, String method, Step step) throws JSONException {
        return record(owner, method, null, step, false);
    }

    /** Runs a call distinguished by a label, such as one stream of an overloaded getter. */
    Object call(String owner, String method, String label, Step step) throws JSONException {
        return record(owner, method, label, step, false);
    }

    /** Runs a call whose result identifies a device or user; only its presence and length are recorded. */
    Object identifier(String owner, String method, Step step) throws JSONException {
        return record(owner, method, null, step, true);
    }

    /** Records a call whose result is an observation only; neither a value nor an exception fails the sweep. */
    void observe(String owner, String method, String label, Step step) throws JSONException {
        String key = key(owner, method, label);
        api.add(owner + "." + method);
        try {
            calls.put(key, Probe.obj("observed", summary(step.run(), false)));
        } catch (Throwable failure) {
            Throwable cause = unwrap(failure);
            calls.put(key, Probe.obj("observed_throw", cause.getClass().getName(), "message", cause.getMessage()));
        }
    }

    /** Requires the call to throw the given type. Any other outcome is a failed check. */
    void throwsType(String owner, String method, String label, Class<? extends Throwable> type, Step step)
            throws JSONException {
        String key = key(owner, method, label);
        api.add(owner + "." + method);
        try {
            Object value = step.run();
            calls.put(key, Probe.obj("returned", summary(value, false)));
            checks.require(key + "_throws_" + type.getSimpleName(), false);
        } catch (Throwable failure) {
            Throwable cause = unwrap(failure);
            calls.put(key, Probe.obj("threw", cause.getClass().getName(), "message", cause.getMessage()));
            checks.require(key + "_throws_" + type.getSimpleName(), type.isInstance(cause));
        }
    }

    ApiSweep require(String name, boolean condition) {
        checks.require(name, condition);
        return this;
    }

    /** Merges the called methods into the probe's API list so the runner can match ledger entries. */
    Probe.Outcome outcome(Probe probe, Object expected, String message) throws JSONException {
        for (String entry : api) {
            boolean present = false;
            for (int i = 0; i < probe.api.length(); i++) present |= entry.equals(probe.api.optString(i));
            if (!present) probe.api.put(entry);
        }
        JSONObject observed = Probe.obj("calls", calls, "failed_calls", failedCalls);
        Probe.Outcome checked = checks.outcome(observed, expected, message);
        if (failedCalls.length() == 0) return checked;
        return new Probe.Outcome("error", observed, expected, message + "; " + failedCalls.length() + " call(s) threw",
                first);
    }

    private Object record(String owner, String method, String label, Step step, boolean secret) throws JSONException {
        String key = key(owner, method, label);
        api.add(owner + "." + method);
        try {
            Object value = step.run();
            calls.put(key, summary(value, secret));
            return value;
        } catch (Throwable failure) {
            Throwable cause = unwrap(failure);
            calls.put(key, Probe.obj("threw", cause.getClass().getName(), "message", cause.getMessage()));
            failedCalls.put(key);
            if (first == null) first = cause;
            return null;
        }
    }

    private static String key(String owner, String method, String label) {
        String simple = owner.substring(owner.lastIndexOf('/') + 1, owner.length() - 1);
        return simple + "." + method + (label == null ? "" : "[" + label + "]");
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable cause = failure;
        while (cause instanceof InvocationTargetException && cause.getCause() != null) cause = cause.getCause();
        return cause;
    }

    static Object summary(Object value, boolean secret) throws JSONException {
        if (value == null) return JSONObject.NULL;
        if (secret) {
            String text = String.valueOf(value);
            return Probe.obj("present", text.length() > 0, "length", text.length());
        }
        if (value instanceof Boolean || value instanceof Integer || value instanceof Long) return value;
        if (value instanceof Float || value instanceof Double) {
            double number = ((Number) value).doubleValue();
            return Double.isNaN(number) || Double.isInfinite(number) ? String.valueOf(number) : number;
        }
        if (value instanceof Number) return ((Number) value).longValue();
        if (value instanceof CharSequence) {
            String text = value.toString();
            return text.length() <= 160 ? text : text.substring(0, 160) + "…";
        }
        if (value instanceof Collection) return Probe.obj("size", ((Collection<?>) value).size());
        if (value instanceof Map) return Probe.obj("size", ((Map<?, ?>) value).size());
        if (value.getClass().isArray()) return Probe.obj("length", java.lang.reflect.Array.getLength(value));
        if (value instanceof JSONObject || value instanceof JSONArray) return value;
        return Probe.obj("class", value.getClass().getName());
    }
}
