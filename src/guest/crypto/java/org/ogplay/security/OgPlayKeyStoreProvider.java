package org.ogplay.security;

import java.security.Provider;

/** OGPlay software-store provider. Registration is gated by DVM-180. */
public final class OgPlayKeyStoreProvider extends Provider {
    public OgPlayKeyStoreProvider() {
        super("OGPlayKeyStore", 1.0, "OGPlay API 19 software KeyStore provider");
        put("KeyStore.BKS", "org.ogplay.security.BksKeyStoreSpi");
    }
}
