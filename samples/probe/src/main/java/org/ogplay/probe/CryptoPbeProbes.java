package org.ogplay.probe;

import java.util.Map;
import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.PBEParameterSpec;

import static org.ogplay.probe.CryptoProbes.*;
import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.RuntimeProbes.hex;
import static org.ogplay.probe.RuntimeProbes.unhex;

final class CryptoPbeProbes {
    private static final byte[] SALT = {1, 2, 3, 4, 5, 6, 7, 8};
    private static final byte[] MESSAGE = sequence(32);
    private static final int ITERATIONS = 1000;

    static void register(Map<String, Probe> probes) {
        // Android BC symmetric mappings (AES/DES/DESede/RC2/ARC4/Twofish) and
        // symmetric/util/PBE.Util, including the OpenSSL and PKCS#12 derivations.
        // Derived keys and ciphertexts calibrated on IM-A870L
        // syhost/VEGA/IM-A870L:4.4.4/KTU84Q/S1235210:user/release-keys.
        pbe(probes, "PBEWITHMD5AND128BITAES-CBC-OPENSSL", "e7b0971e52ca5cc8d0539fb3412f6316",
                "b0438ab8e61f817c4768efd243cc2fdcc11359411408783810ba4efe82c862710f98f6853a761c7a27e637b5bf18b621");
        pbe(probes, "PBEWITHMD5AND192BITAES-CBC-OPENSSL", "e7b0971e52ca5cc8d0539fb3412f6316f7ba2e6ee293d9f3",
                "9698680e2f7954fc8e947416396f19e4fdae93c7cbefbaecda706d1466060a6e609b07c0320cbd10f1abac7fadd0a880");
        pbe(probes, "PBEWITHMD5AND256BITAES-CBC-OPENSSL", "e7b0971e52ca5cc8d0539fb3412f6316f7ba2e6ee293d9f3457b99436b51ce02",
                "dd243bf000eddffefe8156de46e32b688c0549c3b1cc2ab05c7757b8d2bec3f2be552e67a6eec6e0e1d153315f22ae67");
        pbe(probes, "PBEWITHMD5ANDDES", "2698a413f4437519",
                "a87727869ccdf20a14461057c58a13230c18014557a1bbf7abd0fc16c01b571142d9be4fe0fc095d");
        pbe(probes, "PBEWITHMD5ANDRC2", "2699a412f5427519",
                "6f246dd85cbc960fb08229ed9e2c58fab5b7c4260892bfdcd3f381c01266ac7835c915f242bec90d");
        pbe(probes, "PBEWITHSHA1ANDDES", "86dfb36dae45f73e",
                "2bbacfd20d89d09663ff6b5af6b5363f8e1c82ba561e49c13ced778941935c1c8e21995a2866c305");
        pbe(probes, "PBEWITHSHA1ANDRC2", "87dfb26daf45f63f",
                "cd104863a94b82a2e6f4bcf2a226275126aca96641c476dc92586b91edac32303fa85aba692bfae9");
        pbe(probes, "PBEWITHSHA256AND128BITAES-CBC-BC", "3a432f3aa936a51f71f8734aa4431d4d",
                "3c37683227e9c1499c4c65688329dd4da9a90ebb420b6e0f31fc340155fddd57f0a4f352200f035a2e2a615f1334ee9f");
        pbe(probes, "PBEWITHSHA256AND192BITAES-CBC-BC", "3a432f3aa936a51f71f8734aa4431d4d93355c06e9a7ec97",
                "f1278c0cd9efb0e79f3fd5e2ee4ee973a947797c975544f7c98435c490975e6936a171d45ed41cf36e9c17803765a7eb");
        pbe(probes, "PBEWITHSHA256AND256BITAES-CBC-BC", "3a432f3aa936a51f71f8734aa4431d4d93355c06e9a7ec9701a5e4bc25731678",
                "cf429f3d34b293e07f8de2ca333294861b90bdc9b7c0d8baca6235e022c38429a43eedc5e731b51e29fa0b8bd3a3582b");
        pbe(probes, "PBEWITHSHAAND128BITAES-CBC-BC", "e80bdd020155317f30b854cb9f781181",
                "7fbdc0cafcea19cc186909be70634353c1263ef7c68758e22fd58751967d8740b96bd2c1915b3ac8fd83cb25e6996c13");
        pbe(probes, "PBEWITHSHAAND128BITRC2-CBC", "e80bdd020155317f30b854cb9f781181",
                "b4ea1311b966310695935adedf12c8a4424207792f65b420144f2530d51254d00131bb01f6fe6a8e");
        pbe(probes, "PBEWITHSHAAND128BITRC4", "e80bdd020155317f30b854cb9f781181",
                "81728a84e333ee623e6a1ea7099776212b3a071b388151fd9ae47a6937b63895");
        pbe(probes, "PBEWITHSHAAND192BITAES-CBC-BC", "e80bdd020155317f30b854cb9f7811817672285bf09ef90f",
                "61a1c063e956ccc677b84a3a3163ece9463a389ac43aa60ab945e388a3e4737dc2694b44f192edce2f3a2ee1c75f3084");
        pbe(probes, "PBEWITHSHAAND2-KEYTRIPLEDES-CBC", "e90bdc020154317f31b954cb9e791080",
                "6e4b492f03c27aadb67c555cdfecfe6eda7b7f397834459e13b204275fa938a89239b4696eee1c6b");
        pbe(probes, "PBEWITHSHAAND256BITAES-CBC-BC", "e80bdd020155317f30b854cb9f7811817672285bf09ef90feaf0a5320e3fb41a",
                "24935a76caaf2ac37c93643cd3737974959da05ee3dbbdc729c3cc6cff040cc6c896bfbf99c71851c2e0355625356926");
        pbe(probes, "PBEWITHSHAAND3-KEYTRIPLEDES-CBC", "e90bdc020154317f31b954cb9e7910807673295bf19ef80e",
                "ccfe736bc3de3a2b337c5a0b0cc080e3fa199c63a55426b8bcab6e7c2a4e6b247a62994e84e3c87b");
        pbe(probes, "PBEWITHSHAAND40BITRC2-CBC", "e80bdd0201",
                "b2770b179fdbddaed49cf8b015d33146fcc24a9f6a0e07603066a92f57860daa0802372f08931e89");
        pbe(probes, "PBEWITHSHAAND40BITRC4", "e80bdd0201",
                "5c97ab65dbae744bb3a4a0f3301c64230becf1793dbe6bb82911c2b22319b1fe");
        pbe(probes, "PBEWITHSHAANDTWOFISH-CBC", "e80bdd020155317f30b854cb9f7811817672285bf09ef90feaf0a5320e3fb41a",
                "cf68027985989246637951f5aa2356530fdefa3699382cb8a1ecd4d4cfe6095825bf3de5d669783acf6a55e8994e58e6");
        // RFC 7292 Appendix B ID 3 derivation and RFC 2104 HMAC-SHA1.
        pbeMac(probes, "PBEWithHmacSHA1", "212bab71422d31a5d3934c20e5e77eb7ed1af838",
                "1dc915895626e1f5e76f10da3559d67605abf5d6");
        pbeMac(probes, "PBEWithHmacSHA", "212bab71422d31a5d3934c20e5e77eb7ed1af838",
                "1dc915895626e1f5e76f10da3559d67605abf5d6");
    }

    private static void pbe(Map<String, Probe> probes, final String algorithm,
                            final String derived, final String ciphertext) {
        String probe = "crypto.pbe." + id(algorithm);
        add(probes, new Probe(probe + ".key", "in_scope", false, SKF, GENERATE_SECRET) {
            @Override Outcome run(ProbeSession session) throws Exception {
                SecretKeyFactory factory = SecretKeyFactory.getInstance(algorithm);
                Vector result = new Vector();
                result.observed.put("provider", factory.getProvider().getName());
                result.equal("derived_key", hex(key(factory).getEncoded()), derived);
                return result.outcome(algorithm + " fixed password, salt and iterations");
            }
        });
        add(probes, new Probe(probe + ".cipher", "in_scope", false, CIPHER, SKF, GENERATE_SECRET, INIT_SPEC, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                Cipher cipher = Cipher.getInstance(algorithm);
                SecretKey key = key(SecretKeyFactory.getInstance(algorithm));
                PBEParameterSpec parameters = new PBEParameterSpec(SALT, ITERATIONS);
                cipher.init(Cipher.ENCRYPT_MODE, key, parameters);
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                result.equal("ciphertext", hex(cipher.doFinal(MESSAGE)), ciphertext);
                cipher.init(Cipher.DECRYPT_MODE, key, parameters);
                result.bytes("known_ciphertext_decrypted", cipher.doFinal(unhex(ciphertext)), MESSAGE);
                return result.outcome(algorithm + " fixed ciphertext and known-answer decryption");
            }
        });
    }

    private static SecretKey key(SecretKeyFactory factory) throws Exception {
        return factory.generateSecret(new PBEKeySpec("password".toCharArray(), SALT, ITERATIONS));
    }

    private static void pbeMac(Map<String, Probe> probes, final String algorithm,
                               final String derived, final String answer) {
        add(probes, new Probe("crypto.pbe." + id(algorithm) + ".mac", "in_scope", false, SKF, GENERATE_SECRET,
                "Ljavax/crypto/Mac;.getInstance(Ljava/lang/String;)Ljavax/crypto/Mac;",
                "Ljavax/crypto/Mac;.init(Ljava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                "Ljavax/crypto/Mac;.doFinal([B)[B") {
            @Override Outcome run(ProbeSession session) throws Exception {
                SecretKey key = key(SecretKeyFactory.getInstance(algorithm));
                Mac mac = Mac.getInstance(algorithm);
                mac.init(key, new PBEParameterSpec(SALT, ITERATIONS));
                Vector result = new Vector();
                result.observed.put("provider", mac.getProvider().getName());
                result.equal("derived_key", hex(key.getEncoded()), derived);
                result.equal("tag", hex(mac.doFinal(MESSAGE)), answer);
                return result.outcome(algorithm + " password-derived SHA-1 HMAC");
            }
        });
    }
}
