package org.ogplay.security;

import java.util.Enumeration;
import java.util.Hashtable;
import java.util.Vector;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSessionContext;

/** Client session cache isolated by host, port, SNI and trust version. */
final class OgPlaySslSessionContext implements SSLSessionContext {
    private final Hashtable sessions = new Hashtable();
    private final Hashtable hosts = new Hashtable();
    private int timeoutSeconds = 86400;
    private int cacheSize = 32;

    public synchronized SSLSession getSession(byte[] sessionId) {
        if (sessionId == null || sessionId.length == 0) {
            return null;
        }
        purge();
        OgPlaySslSession session = (OgPlaySslSession) sessions.get(new IdKey(sessionId));
        if (session == null || !session.isValid()) {
            return null;
        }
        session.touch();
        return session;
    }

    public synchronized Enumeration getIds() {
        purge();
        Vector ids = new Vector();
        Enumeration values = sessions.elements();
        while (values.hasMoreElements()) {
            OgPlaySslSession session = (OgPlaySslSession) values.nextElement();
            if (session.isValid()) {
                ids.addElement(session.getId());
            }
        }
        return ids.elements();
    }

    public synchronized void setSessionTimeout(int seconds) throws IllegalArgumentException {
        if (seconds < 0) {
            throw new IllegalArgumentException("timeout < 0");
        }
        timeoutSeconds = seconds;
        purge();
    }

    public int getSessionTimeout() {
        return timeoutSeconds;
    }

    public synchronized void setSessionCacheSize(int size) throws IllegalArgumentException {
        if (size < 0) {
            throw new IllegalArgumentException("size < 0");
        }
        cacheSize = size;
        purge();
        evictToSize();
    }

    public int getSessionCacheSize() {
        return cacheSize;
    }

    synchronized void put(OgPlaySslSession session) {
        byte[] id = session.getId();
        if (id.length == 0) {
            return;
        }
        purge();
        sessions.put(new IdKey(id), session);
        hosts.put(hostKey(session.getPeerHost(), session.getPeerPort()), session);
        evictToSize();
    }

    synchronized OgPlaySslSession findResumable(String host, int port) {
        purge();
        OgPlaySslSession session = (OgPlaySslSession) hosts.get(hostKey(host, port));
        if (session == null || !session.isValid() || session.encodedState().length == 0) {
            return null;
        }
        session.touch();
        return session;
    }

    synchronized void invalidateAll() {
        Enumeration values = sessions.elements();
        while (values.hasMoreElements()) {
            ((SSLSession) values.nextElement()).invalidate();
        }
        sessions.clear();
        hosts.clear();
    }

    boolean timedOut(OgPlaySslSession session) {
        if (timeoutSeconds <= 0) {
            return false;
        }
        long age = System.currentTimeMillis() - session.getCreationTime();
        return age > timeoutSeconds * 1000L;
    }

    private void purge() {
        Vector stale = new Vector();
        Enumeration keys = sessions.keys();
        while (keys.hasMoreElements()) {
            Object key = keys.nextElement();
            OgPlaySslSession session = (OgPlaySslSession) sessions.get(key);
            if (session == null || !session.isValid()) {
                stale.addElement(key);
            }
        }
        for (int i = 0; i < stale.size(); i++) {
            Object key = stale.elementAt(i);
            OgPlaySslSession session = (OgPlaySslSession) sessions.remove(key);
            if (session != null) {
                session.invalidate();
                String host = hostKey(session.getPeerHost(), session.getPeerPort());
                if (hosts.get(host) == session) {
                    hosts.remove(host);
                }
            }
        }
    }

    private void evictToSize() {
        if (cacheSize <= 0) {
            return;
        }
        while (sessions.size() > cacheSize) {
            Object first = sessions.keys().nextElement();
            OgPlaySslSession old = (OgPlaySslSession) sessions.remove(first);
            if (old != null) {
                String host = hostKey(old.getPeerHost(), old.getPeerPort());
                if (hosts.get(host) == old) {
                    hosts.remove(host);
                }
            }
        }
    }

    private static String hostKey(String host, int port) {
        return (host == null ? "" : host) + ":" + port;
    }

    private static final class IdKey {
        private final byte[] id;
        private final int hash;

        IdKey(byte[] id) {
            this.id = new byte[id.length];
            System.arraycopy(id, 0, this.id, 0, id.length);
            int value = 1;
            for (int i = 0; i < this.id.length; i++) {
                value = 31 * value + (this.id[i] & 0xff);
            }
            this.hash = value;
        }

        public boolean equals(Object other) {
            if (!(other instanceof IdKey)) {
                return false;
            }
            IdKey key = (IdKey) other;
            if (id.length != key.id.length) {
                return false;
            }
            for (int i = 0; i < id.length; i++) {
                if (id[i] != key.id[i]) {
                    return false;
                }
            }
            return true;
        }

        public int hashCode() {
            return hash;
        }
    }
}
