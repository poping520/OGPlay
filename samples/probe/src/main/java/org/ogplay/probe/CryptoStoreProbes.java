package org.ogplay.probe;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.security.Key;
import java.security.KeyPair;
import java.security.KeyStore;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.Map;
import javax.crypto.spec.SecretKeySpec;

import static org.ogplay.probe.CryptoProbes.Vector;
import static org.ogplay.probe.CryptoProbes.sequence;
import static org.ogplay.probe.PhaseOne.add;

final class CryptoStoreProbes {
    private static final char[] PASSWORD = "probe-password".toCharArray();
    private static final String ALIAS = "probe";
    private static final String STORE = "Ljava/security/KeyStore;.getInstance(Ljava/lang/String;)Ljava/security/KeyStore;";
    private static final String LOAD = "Ljava/security/KeyStore;.load(Ljava/io/InputStream;[C)V";
    private static final String GET_CERT = "Ljava/security/KeyStore;.getCertificate(Ljava/lang/String;)Ljava/security/cert/Certificate;";
    private static final String GET_KEY = "Ljava/security/KeyStore;.getKey(Ljava/lang/String;[C)Ljava/security/Key;";
    private static final String CERT =
            "Ljava/security/cert/CertificateFactory;.getInstance(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;";
    private static final String GEN_CERT =
            "Ljava/security/cert/CertificateFactory;.generateCertificate(Ljava/io/InputStream;)Ljava/security/cert/Certificate;";

    static void register(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.keystore.default", "in_scope", false, STORE,
                "Ljava/security/KeyStore;.getDefaultType()Ljava/lang/String;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                String type = KeyStore.getDefaultType();
                Vector result = new Vector();
                result.equal("default_type", type, "BKS");
                result.observed.put("provider", KeyStore.getInstance(type).getProvider().getName());
                return result.outcome("Default keystore type resolves");
            }
        });
        store(probes, "BKS", "bks", CryptoFixtures.BKS);
        store(probes, "PKCS12", "pkcs12", CryptoFixtures.PKCS12);
        add(probes, new Probe("crypto.certificate.x509", "in_scope", false, CERT, GEN_CERT,
                "Ljava/security/cert/Certificate;.verify(Ljava/security/PublicKey;)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                CertificateFactory factory = CertificateFactory.getInstance("X.509");
                byte[] der = CryptoFixtures.decode(CryptoFixtures.CERTIFICATE);
                X509Certificate certificate = (X509Certificate) factory.generateCertificate(new ByteArrayInputStream(der));
                Vector result = new Vector();
                result.observed.put("provider", factory.getProvider().getName());
                result.bytes("der", certificate.getEncoded(), der);
                result.bytes("public_key", certificate.getPublicKey().getEncoded(), CryptoFixtures.decode(CryptoFixtures.RSA_PUBLIC));
                certificate.verify(certificate.getPublicKey());
                result.equal("self_signature_verified", true, true);
                return result.outcome("X.509 fixed DER sample, SPKI and self-signature");
            }
        });
    }

    private static void store(Map<String, Probe> probes, final String type, String id, final String fixture) {
        add(probes, new Probe("crypto.keystore." + id + ".sample", "in_scope", false, STORE, LOAD, GET_CERT, GET_KEY) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyStore store = KeyStore.getInstance(type);
                store.load(new ByteArrayInputStream(CryptoFixtures.decode(fixture)), PASSWORD);
                Vector result = new Vector();
                result.observed.put("provider", store.getProvider().getName());
                result.equal("entry_count", store.size(), 1);
                checkCertificate(result, store);
                if ("PKCS12".equals(type)) checkPrivateKey(result, store);
                return result.outcome(type + " fixed certificate sample and private key where present");
            }
        });
        // BKS performs several password derivations across sealing, storing and reloading.
        add(probes, new Probe("crypto.keystore." + id + ".roundtrip", "crypto", "in_scope", false, 90000,
                STORE, LOAD, GET_CERT, GET_KEY, CERT, GEN_CERT,
                "Ljava/security/KeyStore;.setKeyEntry(Ljava/lang/String;Ljava/security/Key;[C[Ljava/security/cert/Certificate;)V",
                "Ljava/security/KeyStore;.store(Ljava/io/OutputStream;[C)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyStore store = KeyStore.getInstance(type);
                store.load(null, PASSWORD);
                KeyPair pair = CryptoFixtures.rsa();
                byte[] der = CryptoFixtures.decode(CryptoFixtures.CERTIFICATE);
                Certificate certificate = CertificateFactory.getInstance("X.509")
                        .generateCertificate(new ByteArrayInputStream(der));
                store.setKeyEntry(ALIAS, pair.getPrivate(), PASSWORD, new Certificate[]{certificate});
                boolean bks = "BKS".equals(type);
                byte[] secret = sequence(16);
                if (bks) store.setKeyEntry("secret", new SecretKeySpec(secret, "AES"), PASSWORD, null);
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                store.store(bytes, PASSWORD);
                KeyStore loaded = KeyStore.getInstance(type);
                loaded.load(new ByteArrayInputStream(bytes.toByteArray()), PASSWORD);
                Vector result = new Vector();
                result.observed.put("provider", loaded.getProvider().getName());
                result.equal("entry_count", loaded.size(), bks ? 2 : 1);
                checkCertificate(result, loaded);
                checkPrivateKey(result, loaded);
                if (bks) {
                    Key key = loaded.getKey("secret", PASSWORD);
                    result.bytes("secret_key", key == null ? null : key.getEncoded(), secret);
                }
                return result.outcome(type + " store and load private key and certificate chain");
            }
        });
    }

    private static void checkCertificate(Vector result, KeyStore store) throws Exception {
        Certificate certificate = store.getCertificate(ALIAS);
        result.bytes("certificate", certificate == null ? null : certificate.getEncoded(),
                CryptoFixtures.decode(CryptoFixtures.CERTIFICATE));
    }

    private static void checkPrivateKey(Vector result, KeyStore store) throws Exception {
        Key key = store.getKey(ALIAS, PASSWORD);
        result.bytes("private_key", key == null ? null : key.getEncoded(), CryptoFixtures.decode(CryptoFixtures.RSA_PRIVATE));
        Certificate[] chain = store.getCertificateChain(ALIAS);
        result.equal("chain_length", chain == null ? 0 : chain.length, 1);
    }
}
