package org.ogplay.security;

final class BksLimits {
    static final int MAX_STORE_BYTES = 16 * 1024 * 1024;
    static final int MAX_BLOB_BYTES = 1024 * 1024;
    static final int MAX_ENTRIES = 4096;
    static final int MAX_CHAIN_LENGTH = 64;
    static final int MAX_SALT_BYTES = 1024;
    static final int MAX_ITERATIONS = 1000000;

    private BksLimits() {}
}
