package org.ogplay.security;

import java.security.Principal;
import java.security.cert.Certificate;
import java.security.cert.X509Certificate;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSessionContext;

/** Session metadata from a completed client handshake. */
final class OgPlaySslSession implements SSLSession {
    private final String protocol;
    private final String cipherSuite;
    private final String peerHost;
    private final int peerPort;
    private final Certificate[] peerCerts;
    private final long creationTime;
    private final OgPlaySslSessionContext context;
    private byte[] id;
    private boolean valid = true;

    OgPlaySslSession(String protocol, String cipherSuite, String peerHost, int peerPort,
                     Certificate[] peerCerts, OgPlaySslSessionContext context, byte[] sessionId) {
        this.protocol = protocol;
        this.cipherSuite = cipherSuite;
        this.peerHost = peerHost;
        this.peerPort = peerPort;
        this.peerCerts = peerCerts == null ? new Certificate[0] : peerCerts;
        this.context = context;
        this.creationTime = System.currentTimeMillis();
        if (sessionId == null || sessionId.length == 0) {
            this.id = new byte[0];
        } else {
            this.id = new byte[sessionId.length];
            System.arraycopy(sessionId, 0, this.id, 0, sessionId.length);
        }
    }

    public byte[] getId() {
        return (byte[]) id.clone();
    }

    public SSLSessionContext getSessionContext() {
        return context;
    }

    public long getCreationTime() {
        return creationTime;
    }

    public long getLastAccessedTime() {
        return creationTime;
    }

    public void invalidate() {
        valid = false;
    }

    public boolean isValid() {
        return valid;
    }

    public void putValue(String name, Object value) {
        throw new UnsupportedOperationException("SSL session values are not supported");
    }

    public Object getValue(String name) {
        return null;
    }

    public void removeValue(String name) {}

    public String[] getValueNames() {
        return new String[0];
    }

    public Certificate[] getPeerCertificates() throws SSLPeerUnverifiedException {
        if (peerCerts.length == 0) {
            throw new SSLPeerUnverifiedException("peer not authenticated");
        }
        Certificate[] copy = new Certificate[peerCerts.length];
        System.arraycopy(peerCerts, 0, copy, 0, peerCerts.length);
        return copy;
    }

    public Certificate[] getLocalCertificates() {
        return null;
    }

    public javax.security.cert.X509Certificate[] getPeerCertificateChain()
            throws SSLPeerUnverifiedException {
        Certificate[] certs = getPeerCertificates();
        javax.security.cert.X509Certificate[] legacy =
                new javax.security.cert.X509Certificate[certs.length];
        try {
            for (int i = 0; i < certs.length; i++) {
                legacy[i] = javax.security.cert.X509Certificate.getInstance(certs[i].getEncoded());
            }
        } catch (Exception e) {
            throw new SSLPeerUnverifiedException(e.toString());
        }
        return legacy;
    }

    public Principal getPeerPrincipal() throws SSLPeerUnverifiedException {
        Certificate[] certs = getPeerCertificates();
        return ((X509Certificate) certs[0]).getSubjectX500Principal();
    }

    public Principal getLocalPrincipal() {
        return null;
    }

    public String getCipherSuite() {
        return cipherSuite;
    }

    public String getProtocol() {
        return protocol;
    }

    public String getPeerHost() {
        return peerHost;
    }

    public int getPeerPort() {
        return peerPort;
    }

    public int getPacketBufferSize() {
        return TrustLimits.MAX_WIRE_BUFFER;
    }

    public int getApplicationBufferSize() {
        return TrustLimits.MAX_APP_BUFFER;
    }
}
