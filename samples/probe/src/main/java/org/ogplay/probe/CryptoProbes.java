package org.ogplay.probe;

import java.security.AlgorithmParameters;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.Provider;
import java.security.SecureRandom;
import java.security.Security;
import java.util.Arrays;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.SSLContext;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.Probe.obj;
import static org.ogplay.probe.RuntimeProbes.hex;
import static org.ogplay.probe.RuntimeProbes.unhex;

final class CryptoProbes {
    static void register(Map<String, Probe> probes) {
        inventory(probes);
        digests(probes);
        macs(probes);
        random(probes);
        sslContexts(probes);
        CryptoCipherProbes.register(probes);
        CryptoKeyProbes.register(probes);
    }

    private static void inventory(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.inventory.providers", "informational", false,
                "Ljava/security/Security;.getProviders()[Ljava/security/Provider;",
                "Ljava/security/Provider;.getServices()Ljava/util/Set;",
                "Ljava/security/MessageDigest;.getInstance(Ljava/lang/String;)Ljava/security/MessageDigest;",
                "Ljavax/crypto/Mac;.getInstance(Ljava/lang/String;)Ljavax/crypto/Mac;",
                "Ljava/security/Signature;.getInstance(Ljava/lang/String;)Ljava/security/Signature;", CIPHER, PARAMS) {
            @Override Outcome run(ProbeSession session) throws Exception {
                Map<String, Map<String, Set<String>>> services =
                        new TreeMap<String, Map<String, Set<String>>>();
                JSONArray providers = new JSONArray();
                for (Provider provider : Security.getProviders()) {
                    String name = provider.getName();
                    providers.put(name);
                    for (Provider.Service service : provider.getServices()) {
                        Map<String, Set<String>> algorithms = services.get(service.getType());
                        if (algorithms == null) {
                            algorithms = new TreeMap<String, Set<String>>(String.CASE_INSENSITIVE_ORDER);
                            services.put(service.getType(), algorithms);
                        }
                        Set<String> names = algorithms.get(service.getAlgorithm());
                        if (names == null) {
                            names = new TreeSet<String>();
                            algorithms.put(service.getAlgorithm(), names);
                        }
                        names.add(name);
                    }
                }
                JSONObject inventory = new JSONObject();
                for (Map.Entry<String, Map<String, Set<String>>> service : services.entrySet()) {
                    JSONObject algorithms = new JSONObject();
                    for (Map.Entry<String, Set<String>> algorithm : service.getValue().entrySet()) {
                        algorithms.put(algorithm.getKey(), new JSONArray(algorithm.getValue()));
                    }
                    inventory.put(service.getKey(), algorithms);
                }
                return info(obj("providers", providers, "services", inventory, "additional_lookups", additionalLookups(),
                                "out_of_scope", obj("KeyStore.AndroidKeyStore", "External keystore daemon")),
                        "Registered JCA services by algorithm and provider");
            }
        });
    }

    static final String CIPHER = "Ljavax/crypto/Cipher;.getInstance(Ljava/lang/String;)Ljavax/crypto/Cipher;";
    static final String INIT_KEY = "Ljavax/crypto/Cipher;.init(ILjava/security/Key;)V";
    static final String INIT_SPEC =
            "Ljavax/crypto/Cipher;.init(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V";
    static final String FINAL = "Ljavax/crypto/Cipher;.doFinal([B)[B";
    static final String SKF =
            "Ljavax/crypto/SecretKeyFactory;.getInstance(Ljava/lang/String;)Ljavax/crypto/SecretKeyFactory;";
    static final String GENERATE_SECRET =
            "Ljavax/crypto/SecretKeyFactory;.generateSecret(Ljava/security/spec/KeySpec;)Ljavax/crypto/SecretKey;";
    static final String PARAMS =
            "Ljava/security/AlgorithmParameters;.getInstance(Ljava/lang/String;)Ljava/security/AlgorithmParameters;";
    static final String KPG =
            "Ljava/security/KeyPairGenerator;.getInstance(Ljava/lang/String;)Ljava/security/KeyPairGenerator;";
    static final String GENERATE_PAIR = "Ljava/security/KeyPairGenerator;.generateKeyPair()Ljava/security/KeyPair;";

    private static JSONObject additionalLookups() throws Exception {
        JSONObject result = new JSONObject();
        for (String name : new String[]{"MessageDigest.SHA-224", "Mac.HmacSHA224", "Signature.SHA224withRSA",
                "Signature.SHA224withECDSA", "AlgorithmParameters.EC", "Cipher.RC2", "Cipher.RSA/ECB/OAEPWithSHA-224AndMGF1Padding"}) {
            String algorithm = name.substring(name.indexOf('.') + 1);
            try {
                Provider provider;
                if (name.startsWith("MessageDigest")) provider = MessageDigest.getInstance(algorithm).getProvider();
                else if (name.startsWith("Mac")) provider = Mac.getInstance(algorithm).getProvider();
                else if (name.startsWith("Signature")) provider = java.security.Signature.getInstance(algorithm).getProvider();
                else if (name.startsWith("AlgorithmParameters")) provider = AlgorithmParameters.getInstance(algorithm).getProvider();
                else provider = Cipher.getInstance(algorithm).getProvider();
                result.put(name, provider.getName());
            } catch (NoSuchAlgorithmException unavailable) {
                result.put(name, unavailable.getClass().getName());
            } catch (javax.crypto.NoSuchPaddingException unavailable) {
                result.put(name, unavailable.getClass().getName());
            }
        }
        return result;
    }

    // Store each comparison under the same key in both halves of the report.
    static final class Vector {
        private final Probe.Checks checks = new Probe.Checks();
        final JSONObject observed = new JSONObject();
        final JSONObject expected = new JSONObject();

        void equal(String name, Object actual, Object wanted) throws JSONException {
            observed.put(name, actual == null ? JSONObject.NULL : actual);
            expected.put(name, wanted == null ? JSONObject.NULL : wanted);
            checks.require(name, wanted == null ? actual == null : wanted.equals(actual));
        }

        void bytes(String name, byte[] actual, byte[] wanted) throws JSONException {
            equal(name, actual == null ? null : hex(actual), hex(wanted));
        }

        Probe.Outcome outcome(String message) throws JSONException {
            return checks.outcome(observed, expected, message);
        }
    }

    static String id(String algorithm) {
        return algorithm.toLowerCase(java.util.Locale.US).replace("-", "").replace('/', '.');
    }

    static byte[] filled(int size, int value) {
        byte[] bytes = new byte[size];
        Arrays.fill(bytes, (byte) value);
        return bytes;
    }

    static byte[] sequence(int size) {
        byte[] bytes = new byte[size];
        for (int i = 0; i < size; i++) bytes[i] = (byte) i;
        return bytes;
    }

    static byte[] join(byte[] first, byte[] last) {
        if (first == null) return last;
        if (last == null) return first;
        byte[] output = Arrays.copyOf(first, first.length + last.length);
        System.arraycopy(last, 0, output, first.length, last.length);
        return output;
    }

    private static void digests(Map<String, Probe> probes) {
        // RFC 1321 Appendix A.5 and FIPS 180-4 (NIST short-message "abc" examples).
        digest(probes, "MD5",
                "900150983cd24fb0d6963f7d28e17f72");
        digest(probes, "SHA-1",
                "a9993e364706816aba3e25717850c26c9cd0d89d");
        digest(probes, "SHA-256",
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        digest(probes, "SHA-384",
                "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
        digest(probes, "SHA-512",
                "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    }

    private static void digest(Map<String, Probe> probes, final String algorithm, final String answer) {
        add(probes, new Probe("crypto.digest." + id(algorithm), "in_scope", false,
                "Ljava/security/MessageDigest;.getInstance(Ljava/lang/String;)Ljava/security/MessageDigest;",
                "Ljava/security/MessageDigest;.update([BII)V", "Ljava/security/MessageDigest;.digest([B)[B",
                "Ljava/security/MessageDigest;.digest()[B", "Ljava/security/MessageDigest;.clone()Ljava/lang/Object;",
                "Ljava/security/MessageDigest;.reset()V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                byte[] message = unhex("616263");
                Vector result = new Vector();
                MessageDigest digest = MessageDigest.getInstance(algorithm);
                result.observed.put("provider", digest.getProvider().getName());
                result.equal("one_shot", hex(digest.digest(message)), answer);
                digest.update(message, 0, 1);
                MessageDigest copy = (MessageDigest) digest.clone();
                digest.update(message, 1, 2);
                result.equal("incremental", hex(digest.digest()), answer);
                copy.update(message, 1, 2);
                result.equal("clone", hex(copy.digest()), answer);
                digest.update(message, 0, 2);
                digest.reset();
                result.equal("reset", hex(digest.digest(message)), answer);
                return result.outcome(algorithm + " digest of abc, incremental update, clone and reset");
            }
        });
    }

    private static void macs(Map<String, Probe> probes) {
        // RFC 2202 cases 1 and 6 and RFC 4231 cases 1 and 6. Empty-message answers
        // independently computed with the same short key using RFC 2104's HMAC construction.
        mac(probes, "HmacMD5", 16, 80,
                "9294727a3638bb1c13f48ef8158bfc9d",
                "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd",
                "c9e99a43cd8fa24a840aa85c7cca0061");
        mac(probes, "HmacSHA1", 20, 80,
                "b617318655057264e28bc0b6fb378c8ef146be00",
                "aa4ae5e15272d00e95705637ce8a3b55ed402112",
                "123fd78bda0100786ae86b76f50f01bd18e477f3");
        mac(probes, "HmacSHA256", 20, 131,
                "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
                "999a901219f032cd497cadb5e6051e97b6a29ab297bd6ae722bd6062a2f59542");
        mac(probes, "HmacSHA384", 20, 131,
                "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59cfaea9ea9076ede7f4af152e8b2fa9cb6",
                "4ece084485813e9088d2c63a041bc5b44f9ef1012a2b588f3cd11f05033ac4c60c2ef6ab4030fe8296248df163f44952",
                "ac1e68c7036381efc1ae8514e489ed0eb1c40778d6156bf0a85b0e0c6a6b80f2476d135c9668ae9f3f8ed8fc2405e46c");
        mac(probes, "HmacSHA512", 20, 131,
                "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854",
                "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598",
                "ad8da3d882af6e9b872457adcdd638e9b87af44825425085f8ce81a4122bab781b92f5ab92ac24948ad369f86558fd469ca3f4861cb0f0dfb33154428ed03dfb");
    }

    private static void mac(Map<String, Probe> probes, final String algorithm, final int shortLength,
                            final int longLength, final String shortAnswer, final String longAnswer,
                            final String emptyAnswer) {
        add(probes, new Probe("crypto.mac." + id(algorithm), "in_scope", false,
                "Ljavax/crypto/Mac;.getInstance(Ljava/lang/String;)Ljavax/crypto/Mac;",
                "Ljavax/crypto/Mac;.init(Ljava/security/Key;)V", "Ljavax/crypto/Mac;.update([BII)V",
                "Ljavax/crypto/Mac;.doFinal([B)[B", "Ljavax/crypto/Mac;.doFinal()[B",
                "Ljavax/crypto/Mac;.clone()Ljava/lang/Object;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Vector result = new Vector();
                Mac mac = Mac.getInstance(algorithm);
                result.observed.put("provider", mac.getProvider().getName());
                mac.init(new SecretKeySpec(filled(shortLength, 0x0b), algorithm));
                byte[] message = "Hi There".getBytes("US-ASCII");
                mac.update(message, 0, 2);
                // API 19 OpenSSLMac is not Cloneable. JCA permits a provider to additionally
                // support cloning, in which case the independent copy must preserve the prefix.
                try {
                    Mac copy = (Mac) mac.clone();
                    copy.update(message, 2, message.length - 2);
                    result.equal("cloned_mac", hex(copy.doFinal()), shortAnswer);
                    result.observed.put("clone_supported", true);
                } catch (CloneNotSupportedException unsupported) {
                    result.observed.put("clone_exception", unsupported.getClass().getName());
                }
                mac.update(message, 2, message.length - 2);
                result.equal("short_key", hex(mac.doFinal()), shortAnswer);
                result.equal("empty_message_after_final", hex(mac.doFinal()), emptyAnswer);
                mac.init(new SecretKeySpec(filled(longLength, 0xaa), algorithm));
                result.equal("long_key", hex(mac.doFinal(
                        "Test Using Larger Than Block-Size Key - Hash Key First".getBytes("US-ASCII"))), longAnswer);
                return result.outcome(algorithm + " standard vectors, empty input and clone behavior");
            }
        });
    }

    private static void random(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.random.default", "in_scope", false,
                "Ljava/security/SecureRandom;.<init>()V", "Ljava/security/SecureRandom;.getAlgorithm()Ljava/lang/String;",
                "Ljava/security/SecureRandom;.nextBytes([B)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                SecureRandom random = new SecureRandom();
                Vector result = new Vector();
                result.observed.put("provider", random.getProvider().getName());
                result.equal("algorithm", random.getAlgorithm(), "SHA1PRNG");
                byte[] bytes = new byte[32];
                random.nextBytes(bytes);
                result.equal("nonzero", !Arrays.equals(bytes, new byte[32]), true);
                return result.outcome("Default SecureRandom algorithm and nonzero output");
            }
        });
        add(probes, new Probe("crypto.random.sha1prng.seeded", "in_scope", false,
                "Ljava/security/SecureRandom;.getInstance(Ljava/lang/String;Ljava/security/Provider;)Ljava/security/SecureRandom;",
                "Ljava/security/SecureRandom;.setSeed([B)V", "Ljava/security/SecureRandom;.nextBytes([B)V") {
            @Override Outcome run(ProbeSession session) throws Exception {
                // Harmony SHA1PRNG_SecureRandomImpl.engineSetSeed/engineNextBytes.
                // The same JCA name can also select OpenSSL's shared RAND pool.
                // IM-A870L syhost/VEGA/IM-A870L:4.4.4/KTU84Q/S1235210:user/release-keys.
                String answer = "7cf0552f4e8d355b228521a47b7bf869ac2838514bee97f9494d68408a4cb332";
                String repeatableOutput = null;
                JSONArray implementations = new JSONArray();
                for (Provider provider : Security.getProviders()) {
                    if (provider.getService("SecureRandom", "SHA1PRNG") == null) continue;
                    try {
                        String first = seededOutput(provider);
                        String second = seededOutput(provider);
                        boolean repeatable = first.equals(second);
                        implementations.put(obj("provider", provider.getName(), "repeatable", repeatable,
                                "hex", repeatable ? first : null));
                        if (repeatable && (repeatableOutput == null || answer.equals(first))) repeatableOutput = first;
                    } catch (NoSuchAlgorithmException | RuntimeException unavailable) {
                        implementations.put(obj("provider", provider.getName(),
                                "exception", unavailable.getClass().getName()));
                    }
                }
                Vector result = new Vector();
                result.observed.put("implementations", implementations);
                result.equal("repeatable_seeded_output", repeatableOutput, answer);
                return result.outcome("SHA1PRNG deterministic seed fixtures across independent instances");
            }
        });
    }

    private static String seededOutput(Provider provider) throws NoSuchAlgorithmException {
        SecureRandom random = SecureRandom.getInstance("SHA1PRNG", provider);
        random.setSeed(sequence(32));
        byte[] bytes = new byte[32];
        random.nextBytes(bytes);
        return hex(bytes);
    }

    private static void sslContexts(Map<String, Probe> probes) {
        for (final String protocol : new String[]{"Default", "SSL", "SSLv3", "TLS", "TLSv1", "TLSv1.1", "TLSv1.2"}) {
            add(probes, new Probe("crypto.ssl_context." + id(protocol).replace('.', '_'), "in_scope", false,
                    "Ljavax/net/ssl/SSLContext;.getInstance(Ljava/lang/String;)Ljavax/net/ssl/SSLContext;",
                    "Ljavax/net/ssl/SSLContext;.init([Ljavax/net/ssl/KeyManager;[Ljavax/net/ssl/TrustManager;Ljava/security/SecureRandom;)V") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    SSLContext context = SSLContext.getInstance(protocol);
                    Vector result = new Vector();
                    result.observed.put("provider", context.getProvider().getName());
                    result.equal("protocol", context.getProtocol(), protocol);
                    String exception = null;
                    try {
                        context.init(null, null, null);
                    } catch (java.security.KeyManagementException rejected) {
                        exception = rejected.getClass().getName();
                        result.observed.put("init_message", rejected.getMessage());
                        if (rejected.getCause() != null) {
                            result.observed.put("init_cause", rejected.getCause().getClass().getName());
                        }
                    }
                    // Conscrypt DefaultSSLContextImpl is already initialized.
                    result.equal("init_exception", exception,
                            "Default".equals(protocol) ? "java.security.KeyManagementException" : null);
                    return result.outcome(protocol + " SSLContext lookup and initialization");
                }
            });
        }
    }
}
