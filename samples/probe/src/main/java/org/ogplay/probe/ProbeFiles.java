package org.ogplay.probe;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

final class ProbeFiles {
    static byte[] read(InputStream stream, int limit) throws IOException {
        try {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int count;
            while ((count = stream.read(buffer)) != -1) {
                if (count > limit - bytes.size()) throw new IOException("Probe input exceeds " + limit + " bytes");
                bytes.write(buffer, 0, count);
            }
            return bytes.toByteArray();
        } finally {
            stream.close();
        }
    }

    static String text(File path, int limit) throws IOException {
        return new String(read(new FileInputStream(path), limit), "UTF-8");
    }

    static JSONObject json(File path) throws IOException, JSONException {
        return new JSONObject(text(path, 16 * 1024 * 1024));
    }

    static String encode(JSONObject document) throws JSONException {
        StringBuilder output = new StringBuilder();
        encodeValue(document, output);
        return output.toString();
    }

    private static void encodeValue(Object value, StringBuilder output) throws JSONException {
        if (value instanceof JSONObject) {
            JSONObject object = (JSONObject) value;
            ArrayList<String> keys = new ArrayList<String>();
            for (Iterator<?> iterator = object.keys(); iterator.hasNext();) keys.add((String) iterator.next());
            Collections.sort(keys);
            output.append('{');
            for (int i = 0; i < keys.size(); i++) {
                if (i > 0) output.append(',');
                String key = keys.get(i);
                output.append(JSONObject.quote(key)).append(':');
                encodeValue(object.get(key), output);
            }
            output.append('}');
        } else if (value instanceof JSONArray) {
            JSONArray array = (JSONArray) value;
            output.append('[');
            for (int i = 0; i < array.length(); i++) {
                if (i > 0) output.append(',');
                encodeValue(array.get(i), output);
            }
            output.append(']');
        } else if (value == JSONObject.NULL || value == null) {
            output.append("null");
        } else if (value instanceof Boolean || value instanceof Number) {
            output.append(value.toString());
        } else if (value instanceof String) {
            output.append(JSONObject.quote((String) value));
        } else {
            throw new JSONException("Unsupported report value " + value.getClass().getName());
        }
    }

    static void atomic(File path, String text) throws IOException {
        File temp = new File(path.getParentFile(), path.getName() + ".tmp");
        FileOutputStream output = new FileOutputStream(temp);
        try {
            output.write(text.getBytes("UTF-8"));
            output.getFD().sync();
        } finally {
            output.close();
        }
        if (!temp.renameTo(path)) throw new IOException("Cannot replace " + path);
    }
}
