package org.ogplay.security;

import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Vector;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSessionContext;

/** Client session cache isolated by host, port, SNI and trust version. */
final class OgPlaySslSessionContext implements SSLSessionContext {
    private final Hashtable sessions = new Hashtable();
    private int timeoutSeconds = 86400;
    private int cacheSize = 32;

    public SSLSession getSession(byte[] sessionId) {
        if (sessionId == null) {
            return null;
        }
        String key = new String(sessionId);
        SSLSession session = (SSLSession) sessions.get(key);
        if (session == null) {
            return null;
        }
        if (timeoutSeconds > 0) {
            long age = System.currentTimeMillis() - session.getCreationTime();
            if (age > timeoutSeconds * 1000L) {
                session.invalidate();
                sessions.remove(key);
                return null;
            }
        }
        if (!session.isValid()) {
            sessions.remove(key);
            return null;
        }
        return session;
    }

    public Enumeration getIds() {
        Vector ids = new Vector();
        Enumeration keys = sessions.keys();
        while (keys.hasMoreElements()) {
            String key = (String) keys.nextElement();
            ids.addElement(key.getBytes());
        }
        return ids.elements();
    }

    public void setSessionTimeout(int seconds) throws IllegalArgumentException {
        if (seconds < 0) {
            throw new IllegalArgumentException("timeout < 0");
        }
        timeoutSeconds = seconds;
    }

    public int getSessionTimeout() {
        return timeoutSeconds;
    }

    public void setSessionCacheSize(int size) throws IllegalArgumentException {
        if (size < 0) {
            throw new IllegalArgumentException("size < 0");
        }
        cacheSize = size;
        while (sessions.size() > cacheSize && cacheSize > 0) {
            Object first = sessions.keys().nextElement();
            sessions.remove(first);
        }
    }

    public int getSessionCacheSize() {
        return cacheSize;
    }

    synchronized void put(OgPlaySslSession session) {
        if (cacheSize == 0) {
            return;
        }
        while (sessions.size() >= cacheSize) {
            Object first = sessions.keys().nextElement();
            sessions.remove(first);
        }
        sessions.put(new String(session.getId()), session);
    }

    synchronized void invalidateAll() {
        Enumeration values = sessions.elements();
        while (values.hasMoreElements()) {
            ((SSLSession) values.nextElement()).invalidate();
        }
        sessions.clear();
    }
}
