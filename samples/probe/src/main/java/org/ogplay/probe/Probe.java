package org.ogplay.probe;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

abstract class Probe {
    static final String[] STATUSES = {
        "pass", "fail", "error", "crash", "timeout", "skip", "info"
    };
    final String id;
    final String category;
    final String scope;
    final int timeoutMs;
    final boolean main;
    final JSONArray api;

    Probe(String id, String category, String scope, boolean main, int timeoutMs, String... api) {
        this.id = id;
        this.category = category;
        this.scope = scope;
        this.main = main;
        this.timeoutMs = timeoutMs;
        this.api = new JSONArray();
        for (String entry : api) this.api.put(entry);
    }

    Probe(String id, String scope, boolean main, String... api) {
        this(id, id.substring(0, id.indexOf('.')), scope, main, 5000, api);
    }

    abstract Outcome run(ProbeSession session) throws Exception;

    final JSONObject result(Outcome outcome, long durationMs) throws JSONException {
        return obj("id", id, "category", category, "scope", scope, "status", outcome.status,
                "api", api, "observed", outcome.observed, "expected", outcome.expected,
                "message", outcome.message, "exception", exception(outcome.failure),
                "duration_ms", durationMs, "timeout_ms", timeoutMs, "thread", main ? "main" : "worker");
    }

    static JSONObject obj(Object... entries) throws JSONException {
        JSONObject result = new JSONObject();
        for (int i = 0; i < entries.length; i += 2) {
            Object value = entries[i + 1];
            result.put((String) entries[i], value == null ? JSONObject.NULL : value);
        }
        return result;
    }

    static Outcome check(boolean ok, Object observed, Object expected, String message) {
        return new Outcome(ok ? "pass" : "fail", observed, expected, message, null);
    }

    static final class Checks {
        private final JSONArray failed = new JSONArray();

        Checks require(String name, boolean condition) {
            if (!condition) failed.put(name);
            return this;
        }

        Outcome outcome(JSONObject observed, Object expected, String message) throws JSONException {
            observed.put("failed_checks", failed);
            return check(failed.length() == 0, observed, expected, message);
        }
    }

    static Outcome info(Object observed, String message) {
        return new Outcome("info", observed, null, message, null);
    }

    static Outcome skip(String message) {
        return new Outcome("skip", null, null, message, null);
    }

    static Outcome error(Throwable failure) {
        return new Outcome("error", null, null, failure.getClass().getName(), failure);
    }

    static JSONObject exception(Throwable failure) throws JSONException {
        if (failure == null) return null;
        JSONObject first = exceptionFrame(failure);
        JSONArray causes = new JSONArray();
        Throwable cause = failure.getCause();
        for (int count = 0; cause != null && cause != failure && count < 24; count++) {
            causes.put(exceptionFrame(cause));
            cause = cause.getCause();
        }
        first.put("causes", causes);
        return first;
    }

    private static JSONObject exceptionFrame(Throwable failure) throws JSONException {
        JSONArray stack = new JSONArray();
        StackTraceElement[] frames = failure.getStackTrace();
        for (int i = 0; i < frames.length && i < 24; i++) stack.put(frames[i].toString());
        return obj("class", failure.getClass().getName(), "message", failure.getMessage(), "stack", stack);
    }

    static final class Outcome {
        final String status;
        final Object observed;
        final Object expected;
        final String message;
        final Throwable failure;

        Outcome(String status, Object observed, Object expected, String message, Throwable failure) {
            this.status = status;
            this.observed = observed;
            this.expected = expected;
            this.message = message;
            this.failure = failure;
        }
    }
}
