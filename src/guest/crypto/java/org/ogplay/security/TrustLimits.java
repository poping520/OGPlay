package org.ogplay.security;

final class TrustLimits {
    static final int MAX_CHAIN_CERTS = 16;
    static final int MAX_CERT_DER = 64 * 1024;
    static final int MAX_CHAIN_BYTES = 256 * 1024;
    static final int MAX_CA_CERTS = 512;
    static final int MAX_BUNDLE_BYTES = 2 * 1024 * 1024;
    static final int MAX_ALIAS = 128;
    static final int MAX_PATH_DEPTH = 8;
    static final int MAX_HANDSHAKE_MILLIS = 30000;
    static final int MAX_WIRE_BUFFER = 64 * 1024;
    static final int MAX_APP_BUFFER = 16 * 1024;
    static final String DEFAULT_CA_PATH = "/system/etc/security/cacerts.ogplay";

    private TrustLimits() {}
}
