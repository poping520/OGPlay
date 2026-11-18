package org.ogplay.probe;

import java.math.BigInteger;
import java.security.AlgorithmParameters;
import java.security.Key;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.interfaces.DSAPublicKey;
import java.security.interfaces.ECPublicKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.DSAParameterSpec;
import java.security.spec.MGF1ParameterSpec;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.RSAPrivateCrtKeySpec;
import java.security.spec.RSAPrivateKeySpec;
import java.security.spec.RSAPublicKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;
import java.util.Map;
import javax.crypto.Cipher;
import javax.crypto.KeyAgreement;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactory;
import javax.crypto.interfaces.DHPublicKey;
import javax.crypto.spec.DESKeySpec;
import javax.crypto.spec.DESedeKeySpec;
import javax.crypto.spec.DHParameterSpec;
import javax.crypto.spec.DHPrivateKeySpec;
import javax.crypto.spec.DHPublicKeySpec;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.OAEPParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.PBEParameterSpec;
import javax.crypto.spec.PSource;
import javax.crypto.spec.SecretKeySpec;

import static org.ogplay.probe.CryptoProbes.*;
import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.RuntimeProbes.hex;
import static org.ogplay.probe.RuntimeProbes.unhex;

final class CryptoKeyProbes {
    private static final byte[] MESSAGE = unhex("4b69744b6174");
    private static final String KF = "Ljava/security/KeyFactory;.getInstance(Ljava/lang/String;)Ljava/security/KeyFactory;";
    private static final String PUBLIC_KEY =
            "Ljava/security/KeyFactory;.generatePublic(Ljava/security/spec/KeySpec;)Ljava/security/PublicKey;";
    private static final String PRIVATE_KEY =
            "Ljava/security/KeyFactory;.generatePrivate(Ljava/security/spec/KeySpec;)Ljava/security/PrivateKey;";
    private static final String KEY_SPEC =
            "Ljava/security/KeyFactory;.getKeySpec(Ljava/security/Key;Ljava/lang/Class;)Ljava/security/spec/KeySpec;";
    private static final String AGREEMENT = "Ljavax/crypto/KeyAgreement;.getInstance(Ljava/lang/String;)Ljavax/crypto/KeyAgreement;";
    private static final String AGREE_INIT = "Ljavax/crypto/KeyAgreement;.init(Ljava/security/Key;)V";
    private static final String AGREE_PHASE = "Ljavax/crypto/KeyAgreement;.doPhase(Ljava/security/Key;Z)Ljava/security/Key;";
    private static final String AGREE_SECRET = "Ljavax/crypto/KeyAgreement;.generateSecret()[B";

    static void register(Map<String, Probe> probes) {
        rsa(probes);
        signatures(probes);
        agreements(probes);
        pairGenerators(probes);
        generators(probes);
        secretFactories(probes);
        parameters(probes);
        CryptoStoreProbes.register(probes);
        CryptoPbeProbes.register(probes);
        // The first uninitialized BC call generates a 1024-bit safe prime.
        // Run it last with a separate budget (about ten minutes in calibration).
        pairGenerator(probes, "DH", 1024, 3540000);
    }

    private static void rsa(Map<String, Probe> probes) {
        // Fixed test-only 1536-bit key and independently generated RFC 8017 §5/7
        // answers. This modulus can exercise even OAEP with SHA-512.
        rsaRaw(probes, "nopadding", "RSA/ECB/NoPadding",
                "4d5ba6b129a5700cc582beb43ea6260c2c23ed5e124a7bcf9fe43d56b2e4b8b77e80d06f315e256d8c24d93553bf8581" +
                "1e9c0eff18f01ce3ff05d2463811eec2966e0fbe9397a4d47e17950ee2a62eba4c6a9ee9708c3b72d048e0c198d63173" +
                "0a12522878d352f91348e4fde54cf1a63600b32226c9ae8c6dd646cba32f11e69f8ec597fb43e3b004ca41f358b8534e" +
                "85cb8491f193aa69191e95d88830ddc86238431f081fa62148b708a1ea39b2d1abd95ceb51fcedff752e4090761621af");
        rsaRaw(probes, "default", "RSA",
                "4d5ba6b129a5700cc582beb43ea6260c2c23ed5e124a7bcf9fe43d56b2e4b8b77e80d06f315e256d8c24d93553bf8581" +
                "1e9c0eff18f01ce3ff05d2463811eec2966e0fbe9397a4d47e17950ee2a62eba4c6a9ee9708c3b72d048e0c198d63173" +
                "0a12522878d352f91348e4fde54cf1a63600b32226c9ae8c6dd646cba32f11e69f8ec597fb43e3b004ca41f358b8534e" +
                "85cb8491f193aa69191e95d88830ddc86238431f081fa62148b708a1ea39b2d1abd95ceb51fcedff752e4090761621af");
        rsaPkcs1(probes,
                "acf16e87d01b97655866dab8c39d71b2424fd5f5273dfd1bf896237171ede868a2c89423c09fc0a85e958f12b0383aad" +
                "27fc4e9af237247b1936e043f5d54a7d5300fd9d24f8e0d4dcf2e4320a82ef044c9b3aabbb172ceebf9c9858a14984bb" +
                "3f527d34f32a798bd3122d30dcb40f3bf79988d43997d9073a04f52bdda693d3242801e742effb6b6a09fe1a1d950bab" +
                "9099d11e16fed771e7553a35c134044080dd2f5d69c604bd0bdad9f74b8e7f2ac2be8199e0f62307fc36853acb2e2983");
        rsaOaep(probes, "default", "OAEPPadding", null,
                "04bac18953dc55be7fe9f3b92ab03cbf25d414786be79bfee0ee9fb1238bc7ac73f7d7a264d7b0ed5c0536e172955812" +
                "3e23ca936d51f89d41912f73f8447fcf78e45c16d9541dec81e885e3ae93c06128c43b03c0cfaf59c1f5096dbb6da129" +
                "3ff0f6166c668e3e38453eb85a31f9d142868600ede33d63857daa89bbcc43ffd87da7478bfc75db511448fb6b4a63a1" +
                "b4ce3289983f8feb0c08b0c2b72ef1ff174adada2e54b01876e5f7a666b68efa939ae00a57ed8c2b95fdcfeae3a5eaa1");
        rsaOaep(probes, "md5", "OAEPWithMD5AndMGF1Padding", null,
                "53ee72b77f735de03145bf379353224544c3eb096b7e21bf38ed27dc9585c54f68d37c042f843963bca73eec8646287a" +
                "d992a7ac4e04362302a8354b4c9e070f21a60ff6bb4964dfc32f42eaf2e653d15a5159e5a64b0bfb2d995342337de830" +
                "f260beb2bba52760be7ff7a9da108abbef0aeaac00a554565153c7010fad2790690b9b57ef92325a6da8c5c44c5d3c19" +
                "89c1a7f9742775da38d9514f533083dc72e29e1909f267c54784a96a2a659766f1030c4b9060698733ecf63995e29951");
        rsaOaep(probes, "sha1", "OAEPWithSHA-1AndMGF1Padding", null,
                "04bac18953dc55be7fe9f3b92ab03cbf25d414786be79bfee0ee9fb1238bc7ac73f7d7a264d7b0ed5c0536e172955812" +
                "3e23ca936d51f89d41912f73f8447fcf78e45c16d9541dec81e885e3ae93c06128c43b03c0cfaf59c1f5096dbb6da129" +
                "3ff0f6166c668e3e38453eb85a31f9d142868600ede33d63857daa89bbcc43ffd87da7478bfc75db511448fb6b4a63a1" +
                "b4ce3289983f8feb0c08b0c2b72ef1ff174adada2e54b01876e5f7a666b68efa939ae00a57ed8c2b95fdcfeae3a5eaa1");
        rsaOaep(probes, "sha256", "OAEPWithSHA-256AndMGF1Padding", null,
                "5124307aeafb72ca36da4c4a85a353f876d6012877dcf8dc78de93ebc2065cac06c13c84afb5f35555c751003a0bb688" +
                "24b0f31b92a54347c428819430054e42ce5ee5d63d14ffb6d00dc6b0145e29c83f87a909bc421ba755c554aa49b4fe94" +
                "ad10216efe7c1b9e476e6b940209bf11255de535c9515f43f1a46671375d583a67c9a4a7f9abe46eae4a323c65e7cb15" +
                "a0467caea8919e9310bb69d0b292da935c94851e7f00126898a8d52b8a3bc5b6d0895f60415e5e1185da952629079345");
        rsaOaep(probes, "sha384", "OAEPWithSHA-384AndMGF1Padding", null,
                "53822d03442e46cfb8db01cfcdd828d2bd00c8662f2f36ae30a89deeb0b9dba9c3260138167152c3f1ea56b929806048" +
                "f4f65923f3cf3d52c5c945e70398bfa6610b9b287ec4b8096af3c8376ccc59b771df4b15397a6c7553f28527723aaa3e" +
                "03edc34ec027d5de6b51db918c521ca9809d130aeacf2ade78798cb15bbaf78b36ae70f9affa2cfb278abb73b0bc7311" +
                "3e2f56f88774770735e047abdd73583af629fb05d90306bdff18b0d681d699c4a9c736ed1159319daae9de49977c62ca");
        rsaOaep(probes, "sha512", "OAEPWithSHA-512AndMGF1Padding", null,
                "3082b963c190906ac2745d075b5dce2577d20a8ff1a435929a4dbbb9aa066e529d8d49f41177a31253f5ec7a03088dd9" +
                "a0765b60d2a5327545703c83fcbbdd05ad3b6b50b4ffb9737ec912fd6b17b1947bd7029ab969a95eb2cffce94bc55458" +
                "60a465bbd883c8103a458399dfb84e49876ee81403b3b41eea54ef6d53e3483f83960a589e7b3ed3e341dd45f6fb27be" +
                "915a49f0619ae4bffdb1f627bdc636a614bc6949591b60d40a1fd12f08cf970790862a413a6c38df2204826036056fca");
        rsaOaep(probes, "sha256_mgf1_sha1_label", "OAEPPadding", mixedOaep(),
                "75ce0e1d0aabddf2625a7b95bcaa029ca9f73c456711c0fb805590283c035323ea66be4a574fc08e163fe4af00357aef" +
                "16039de72329da319355a6286eb4b639be6a6a719bca3f98010cb98b259a7719946abcf8116e1708590a9762034f2344" +
                "13ce7b81dca83979cb247c9440fdef7edd2fa928b171c42c8f030c131bafe6f8602e0b12bb56be76397a4ee7d9356a5f" +
                "2f8827fb02613024641433462f7e2c91d2241a3aca4799d2fcb946a830120a5b8403a931ccb27d116a49fb56cd693159");
        rsaFactory(probes);
    }

    private static void signatures(Map<String, Probe> probes) {
        // RFC 8017 §9.2 EMSA-PKCS1-v1_5 (NONEwithRSA uses the message as the payload).
        signature(probes, "MD5withRSA", "RSA",
                "8793401a517c3bcaa6ca255edac14872268e9427fda6a70b960b92bd8d13a1e68dd12c57ea6ed24b1a92d1bb37c760fd" +
                "6382b26b80d33e34995d5500bd55f94c71273ccdcf0bee2fa6baf935e34be981ff62951e4604a0631f7feb0ace63c43e" +
                "8a31aaf8597c8e6ac596f300f2607e51fb443dbdbde0154e79333dd93b8bff8cb038903548628f410917892ef6fc9355" +
                "cdf6d570265e84b9978f31b80a1e165af303c2794bdf168042275b819a26416390dbe33510abda504e032393a3d17c48");
        signature(probes, "SHA1withRSA", "RSA",
                "78f0c3349bf5042785e57ac029c789fe06200bc22d92bdf00d8bb2fffa0ee67b347ff796c042153e74ac96dc20c399e1" +
                "b525f95ab09c3e6bf73dada2e58619bb1cb59952cd33a9afbb90f23dc18595744ff18a1dd14458f8a6cc9ec63e10d42f" +
                "821eb43c24b49970c8b6ddd903890c15d29dae549655e525383f56d4921a1100e4f5d7bb9d45b4ec11482d852fef43f3" +
                "95c80ec3775ecd0176a2813d1927db594bb752d46180176460d056d13392a9dea07872be6b0468c3c624e4c690dda458");
        signature(probes, "SHA256withRSA", "RSA",
                "50f92358ff5c75817e2c4a226a4e53a24123b4e1e0a7fbd67b18a835f96473271c07867197173b4841ea461c1963f239" +
                "f0271f50f3adee4088383f35ecb1a2eb1c0d2e7ebc9890338f21931f511021e93e0b2f0635f465f00cba41fbd6ed42b8" +
                "a3798b87b2671770b408f7e84d95d2bfa89c948e4ef3a5e6fbcef56626a01aba30804c6b08cde8c6f2b4cdd9d268487f" +
                "d6c79300c6e6881d1b60d4bf90ae165793231a1362f610782da24eb7f9e29c4674c4595f03f74e4c5b5eecea2eebda09");
        signature(probes, "SHA384withRSA", "RSA",
                "b029618b97b1fce7ec297b9303b9b531ee954e51c9ec6d036840c1a6d94ada89393e3eb23f4880cffd38f46997be0e8e" +
                "196d173b87bbc920959550b05393f06e5b3fee8051d3c956452f09a6ef7b416af8887de5a69f61ca0b0ba79e335d2010" +
                "a1c76dff538947d5f4513075adaae8b798fca9237054da5d190cc1e4a014a66b707a95eef4d1dbe13e0a68ba6133d976" +
                "a65e1b0ae77c12554e803609db7264bbfa4dfcf84b1c25a54ab599887d3be79f13ea3128af3478c4a0c7c3fd465108cf");
        signature(probes, "SHA512withRSA", "RSA",
                "27c6bf4a61c6105ea9f43bdae374441e9702f492c851b07fd9be26ce2a268bfce5c5eba437f0934e9a954009db166ea2" +
                "0c0495a244aedbebfb8d6a9857eaf7f106e5281e18a88e535df503b3cb79fe8f6f4ba5d94ede0366b9cfbf03d05c5b8a" +
                "1e4c4a39312bdd253f47647f60f9a0d474a6be7ec718e1c3855fe3c3bfa4af0ce8183e22f887796dddfbaf520d10b25a" +
                "4fc3f773191615320bd533fc1cdd6ac4f5541c9d9e9ede561c77564ca3e0e5c6bdbff16c424b869d893e6301b1bd8473");
        signature(probes, "NONEwithRSA", "RSA",
                "952c49eb1f666b6fdfb9ac65341043b9a7949c28546ed92471769b46a9e88b622e6c6e78f9dfa172bf12cda152c40b23" +
                "828d2ee947624627c686ddb1f3996ef984213579661db6e6a90b1299bd2d7ae31bad9e76a654b64c62c85b357acbeccc" +
                "7d1aa7f967176635d969d0aad6bf43a2e508bd9740b858efe3994ad8412283f5103b7cd709d13a6ed2c6cb89a2932f96" +
                "f71e9d49caf904a0ff91284e4042bf2c4a1d58e077725d7faa122ed3946620cb76900b868e2085c92e2bd89b5b8c55d1");
        for (String algorithm : new String[]{"SHA1withDSA", "NONEwithDSA"}) {
            signature(probes, algorithm, "DSA", null);
        }
        for (String algorithm : new String[]{"SHA1withECDSA", "SHA256withECDSA",
                "SHA384withECDSA", "SHA512withECDSA", "NONEwithECDSA"}) {
            signature(probes, algorithm, "EC", null);
        }
    }

    private static void rsaRaw(Map<String, Probe> probes, String id, final String transformation, final String answer) {
        add(probes, new Probe("crypto.rsa." + id, "in_scope", false, KF, PUBLIC_KEY, PRIVATE_KEY, CIPHER, INIT_KEY, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair = CryptoFixtures.rsa();
                Cipher cipher = Cipher.getInstance(transformation);
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                result.equal("encrypted", hex(cipher.doFinal(MESSAGE)), answer);
                cipher.init(Cipher.DECRYPT_MODE, pair.getPrivate());
                byte[] plain = cipher.doFinal(unhex(answer));
                // Conscrypt returns a full modulus block while BC trims its leading zeroes.
                byte[] unsigned = new BigInteger(1, plain).toByteArray();
                result.bytes("decrypted_unsigned", unsigned, MESSAGE);
                result.observed.put("decrypted_length", plain.length);
                return result.outcome(transformation + " raw RSA known answer");
            }
        });
    }

    private static void rsaPkcs1(Map<String, Probe> probes, final String known) {
        add(probes, new Probe("crypto.rsa.pkcs1padding", "in_scope", false,
                KF, PUBLIC_KEY, PRIVATE_KEY, CIPHER, INIT_KEY, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair = CryptoFixtures.rsa();
                Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                byte[] encrypted = cipher.doFinal(MESSAGE);
                result.equal("encrypted_length", encrypted.length, 192);
                cipher.init(Cipher.DECRYPT_MODE, pair.getPrivate());
                result.bytes("round_trip", cipher.doFinal(encrypted), MESSAGE);
                cipher.init(Cipher.DECRYPT_MODE, pair.getPrivate());
                result.bytes("known_ciphertext", cipher.doFinal(unhex(known)), MESSAGE);
                return result.outcome("RSA PKCS#1 v1.5 round trip and known ciphertext");
            }
        });
        add(probes, new Probe("crypto.rsa.pkcs1padding.exceptions", "in_scope", false,
                KF, PUBLIC_KEY, PRIVATE_KEY, CIPHER, INIT_KEY, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair = CryptoFixtures.rsa();
                Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                Vector result = new Vector();
                result.equal("larger_than_modulus", CryptoCipherProbes.finalException(cipher, new byte[193]),
                        "javax.crypto.IllegalBlockSizeException");
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                // RSA_crypt_operation maps RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE to RuntimeException.
                String oversized;
                try {
                    oversized = CryptoCipherProbes.finalException(cipher, new byte[182]);
                } catch (RuntimeException rejected) {
                    oversized = rejected.getClass().getName();
                }
                result.equal("larger_than_pkcs1_limit", oversized, "java.lang.RuntimeException");
                cipher.init(Cipher.DECRYPT_MODE, pair.getPrivate());
                // OpenSSLCipherRSA wraps the native SignatureException for this representative.
                result.equal("invalid_ciphertext", CryptoCipherProbes.finalException(cipher, new byte[192]),
                        "javax.crypto.IllegalBlockSizeException");
                return result.outcome("RSA oversized plaintext and malformed ciphertext exceptions");
            }
        });
    }

    private static void rsaOaep(Map<String, Probe> probes, String id, final String padding,
                                final OAEPParameterSpec parameters, final String answer) {
        add(probes, new Probe("crypto.rsa.oaep." + id, "in_scope", false,
                KF, PUBLIC_KEY, PRIVATE_KEY, CIPHER, INIT_KEY, INIT_SPEC, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair = CryptoFixtures.rsa();
                Cipher cipher = Cipher.getInstance("RSA/ECB/" + padding);
                initOaep(cipher, Cipher.ENCRYPT_MODE, pair.getPublic(), parameters);
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                byte[] encrypted = cipher.doFinal(MESSAGE);
                result.equal("encrypted_length", encrypted.length, 192);
                initOaep(cipher, Cipher.DECRYPT_MODE, pair.getPrivate(), parameters);
                result.bytes("round_trip", cipher.doFinal(encrypted), MESSAGE);
                initOaep(cipher, Cipher.DECRYPT_MODE, pair.getPrivate(), parameters);
                result.bytes("known_ciphertext", cipher.doFinal(unhex(answer)), MESSAGE);
                return result.outcome(padding + " randomized round trip and fixed ciphertext");
            }
        });
    }

    private static void initOaep(Cipher cipher, int mode, Key key, OAEPParameterSpec spec) throws Exception {
        if (spec == null) cipher.init(mode, key);
        else cipher.init(mode, key, spec);
    }

    private static OAEPParameterSpec mixedOaep() {
        return new OAEPParameterSpec("SHA-256", "MGF1", MGF1ParameterSpec.SHA1, new PSource.PSpecified(MESSAGE));
    }

    private static void rsaFactory(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.key_factory.rsa", "in_scope", false, KF, PUBLIC_KEY, PRIVATE_KEY, KEY_SPEC,
                "Ljava/security/KeyFactory;.translateKey(Ljava/security/Key;)Ljava/security/Key;", CIPHER, INIT_KEY, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair = CryptoFixtures.rsa();
                KeyFactory factory = KeyFactory.getInstance("RSA");
                Vector result = new Vector();
                result.observed.put("provider", factory.getProvider().getName());
                byte[] spki = CryptoFixtures.decode(CryptoFixtures.RSA_PUBLIC);
                byte[] pkcs8 = CryptoFixtures.decode(CryptoFixtures.RSA_PRIVATE);
                result.bytes("x509_spec", factory.getKeySpec(pair.getPublic(), X509EncodedKeySpec.class).getEncoded(), spki);
                result.bytes("pkcs8_spec", factory.getKeySpec(pair.getPrivate(), PKCS8EncodedKeySpec.class).getEncoded(), pkcs8);
                RSAPublicKeySpec pub = factory.getKeySpec(pair.getPublic(), RSAPublicKeySpec.class);
                result.equal("modulus_bits", pub.getModulus().bitLength(), 1536);
                result.equal("public_exponent", pub.getPublicExponent().intValue(), 65537);
                result.bytes("public_numbers_to_spki", factory.generatePublic(pub).getEncoded(), spki);
                RSAPrivateKeySpec privateSpec = factory.getKeySpec(pair.getPrivate(), RSAPrivateKeySpec.class);
                // Conscrypt's PKCS#8 path can expose an RSAPrivateKey without the CRT interface.
                RSAPrivateCrtKeySpec inputCrt = CryptoFixtures.rsaCrt(pub, privateSpec.getPrivateExponent());
                PrivateKey crtKey = factory.generatePrivate(inputCrt);
                result.bytes("crt_to_pkcs8", crtKey.getEncoded(), pkcs8);
                RSAPrivateCrtKeySpec crt = factory.getKeySpec(crtKey, RSAPrivateCrtKeySpec.class);
                result.equal("crt_modulus", crt.getPrimeP().multiply(crt.getPrimeQ()).equals(pub.getModulus()), true);
                result.equal("crt_coefficient", crt.getCrtCoefficient().equals(inputCrt.getCrtCoefficient()), true);
                PrivateKey privateOnly = factory.generatePrivate(privateSpec);
                result.equal("private_exponent", factory.getKeySpec(privateOnly, RSAPrivateKeySpec.class)
                        .getPrivateExponent().equals(crt.getPrivateExponent()), true);
                result.bytes("translated_public", factory.translateKey(pair.getPublic()).getEncoded(), spki);
                Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                byte[] ciphertext = cipher.doFinal(MESSAGE);
                cipher.init(Cipher.DECRYPT_MODE, privateOnly);
                result.bytes("non_crt_private_usable", cipher.doFinal(ciphertext), MESSAGE);
                return result.outcome("RSA encoded, public exponent, private and CRT KeySpec conversions");
            }
        });
    }

    private static void signature(Map<String, Probe> probes, final String algorithm, final String family, final String answer) {
        add(probes, new Probe("crypto.signature." + id(algorithm), "in_scope", false, KF, PUBLIC_KEY, PRIVATE_KEY,
                "Ljava/security/Signature;.getInstance(Ljava/lang/String;)Ljava/security/Signature;",
                "Ljava/security/Signature;.initSign(Ljava/security/PrivateKey;)V",
                "Ljava/security/Signature;.initVerify(Ljava/security/PublicKey;)V",
                "Ljava/security/Signature;.update([B)V", "Ljava/security/Signature;.sign()[B",
                "Ljava/security/Signature;.verify([B)Z") {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPair pair;
                if ("RSA".equals(family)) pair = CryptoFixtures.rsa();
                else if ("EC".equals(family)) pair = CryptoFixtures.pair("EC", CryptoFixtures.EC_PUBLIC, CryptoFixtures.EC_PRIVATE);
                else pair = CryptoFixtures.pair("DSA", CryptoFixtures.DSA_PUBLIC, CryptoFixtures.DSA_PRIVATE);
                // Raw DSA requires exactly 20 bytes (the size of a SHA-1 digest).
                byte[] data = "NONEwithDSA".equals(algorithm) ? sequence(20) : MESSAGE;
                Signature signature = Signature.getInstance(algorithm);
                signature.initSign(pair.getPrivate());
                signature.update(data);
                byte[] signed = signature.sign();
                Vector result = new Vector();
                result.observed.put("provider", signature.getProvider().getName());
                if (answer != null) result.equal("signature", hex(signed), answer);
                signature.initVerify(pair.getPublic());
                signature.update(data);
                result.equal("verified", signature.verify(answer == null ? signed : unhex(answer)), true);
                byte[] tampered = data.clone();
                tampered[0] ^= 1;
                signature.initVerify(pair.getPublic());
                signature.update(tampered);
                result.equal("tampered_message_verified", signature.verify(signed), false);
                return result.outcome(algorithm + " signing, verification and tampered message");
            }
        });
    }

    private static void agreements(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.agreement.dh", "in_scope", false, KF, PUBLIC_KEY, PRIVATE_KEY,
                AGREEMENT, AGREE_INIT, AGREE_PHASE, AGREE_SECRET) {
            @Override Outcome run(ProbeSession session) throws Exception {
                // Toy modulus strictly for this fixed Diffie-Hellman known answer.
                BigInteger p = BigInteger.valueOf(23);
                BigInteger g = BigInteger.valueOf(5);
                KeyFactory factory = KeyFactory.getInstance("DH");
                PrivateKey a = factory.generatePrivate(new DHPrivateKeySpec(BigInteger.valueOf(6), p, g));
                PublicKey aPublic = factory.generatePublic(new DHPublicKeySpec(BigInteger.valueOf(8), p, g));
                PrivateKey b = factory.generatePrivate(new DHPrivateKeySpec(BigInteger.valueOf(15), p, g));
                PublicKey bPublic = factory.generatePublic(new DHPublicKeySpec(BigInteger.valueOf(19), p, g));
                return agree("DH", a, aPublic, b, bPublic, "02");
            }
        });
        add(probes, new Probe("crypto.agreement.ecdh", "in_scope", false, KF, PUBLIC_KEY, PRIVATE_KEY,
                AGREEMENT, AGREE_INIT, AGREE_PHASE, AGREE_SECRET) {
            @Override Outcome run(ProbeSession session) throws Exception {
                // RFC 5903 §8.1 shared x coordinate on secp256r1.
                KeyPair a = CryptoFixtures.pair("EC", CryptoFixtures.EC_PUBLIC, CryptoFixtures.EC_PRIVATE);
                KeyPair b = CryptoFixtures.pair("EC", CryptoFixtures.EC_PEER_PUBLIC, CryptoFixtures.EC_PEER_PRIVATE);
                return agree("ECDH", a.getPrivate(), a.getPublic(), b.getPrivate(), b.getPublic(),
                        "d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03812464d04b9442de");
            }
        });
    }

    private static Probe.Outcome agree(String algorithm, PrivateKey a, PublicKey aPublic, PrivateKey b,
                                       PublicKey bPublic, String answer) throws Exception {
        KeyAgreement tunnel = KeyAgreement.getInstance(algorithm);
        Vector result = new Vector();
        result.observed.put("provider", tunnel.getProvider().getName());
        tunnel.init(a);
        tunnel.doPhase(bPublic, true);
        result.equal("a_shared_secret", hex(tunnel.generateSecret()), answer);
        tunnel.init(b);
        tunnel.doPhase(aPublic, true);
        result.equal("b_shared_secret", hex(tunnel.generateSecret()), answer);
        return result.outcome(algorithm + " fixed reciprocal shared secret");
    }

    private static void pairGenerators(Map<String, Probe> probes) {
        pairGenerator(probes, "RSA", 2048, 30000);
        pairGenerator(probes, "DSA", 1024, 30000);
        pairGenerator(probes, "EC", 192, 10000);
        add(probes, new Probe("crypto.keypair.rsa.1024", "crypto", "in_scope", false, 15000,
                KPG, GENERATE_PAIR, "Ljava/security/KeyPairGenerator;.initialize(I)V", CIPHER, INIT_KEY, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
                generator.initialize(1024);
                KeyPair pair = generator.generateKeyPair();
                Vector result = new Vector();
                result.observed.put("provider", generator.getProvider().getName());
                result.equal("bits", ((RSAPublicKey) pair.getPublic()).getModulus().bitLength(), 1024);
                Cipher cipher = Cipher.getInstance("RSA/ECB/PKCS1Padding");
                cipher.init(Cipher.ENCRYPT_MODE, pair.getPublic());
                byte[] encrypted = cipher.doFinal(MESSAGE);
                cipher.init(Cipher.DECRYPT_MODE, pair.getPrivate());
                result.bytes("round_trip", cipher.doFinal(encrypted), MESSAGE);
                return result.outcome("A generated 1024-bit RSA pair encrypts and decrypts");
            }
        });
        add(probes, new Probe("crypto.keypair.dh.fixed", "in_scope", false, KPG, GENERATE_PAIR,
                "Ljava/security/KeyPairGenerator;.initialize(Ljava/security/spec/AlgorithmParameterSpec;)V",
                AGREEMENT, AGREE_INIT, AGREE_PHASE, AGREE_SECRET) {
            @Override Outcome run(ProbeSession session) throws Exception {
                // RFC 2409 §6.2 safe-prime group at BC's default 1024-bit width.
                // Uninitialized BC generates its safe prime from scratch, which can take minutes.
                BigInteger p = new BigInteger(1, unhex("ffffffffffffffffc90fdaa22168c234c4c6628b80dc1cd1"
                        + "29024e088a67cc74020bbea63b139b22514a08798e3404ddef9519b3cd3a431b"
                        + "302b0a6df25f14374fe1356d6d51c245e485b576625e7ec6f44c42e9a637ed6b"
                        + "0bff5cb6f406b7edee386bfb5a899fa5ae9f24117c4b1fe649286651ece65381"
                        + "ffffffffffffffff"));
                BigInteger g = BigInteger.valueOf(2);
                KeyPairGenerator generator = KeyPairGenerator.getInstance("DH");
                generator.initialize(new DHParameterSpec(p, g));
                KeyPair a = generator.generateKeyPair();
                KeyPair b = generator.generateKeyPair();
                Vector result = new Vector();
                result.observed.put("provider", generator.getProvider().getName());
                DHPublicKey pub = (DHPublicKey) a.getPublic();
                result.equal("modulus_bits", pub.getParams().getP().bitLength(), 1024);
                result.equal("generator", pub.getParams().getG().intValue(), 2);
                KeyAgreement agreement = KeyAgreement.getInstance("DH");
                agreement.init(a.getPrivate());
                agreement.doPhase(b.getPublic(), true);
                byte[] secret = agreement.generateSecret();
                agreement.init(b.getPrivate());
                agreement.doPhase(a.getPublic(), true);
                result.equal("generated_agreement_matches", Arrays.equals(secret, agreement.generateSecret()), true);
                return result.outcome("DH generates agreeing pairs for a fixed 1024-bit group");
            }
        });
    }

    private static void pairGenerator(Map<String, Probe> probes, final String algorithm, final int bits, int timeout) {
        add(probes, new Probe("crypto.keypair." + id(algorithm) + ".default", "crypto", "in_scope",
                false, timeout, KPG, GENERATE_PAIR) {
            @Override Outcome run(ProbeSession session) throws Exception {
                KeyPairGenerator generator = KeyPairGenerator.getInstance(algorithm);
                PublicKey key = generator.generateKeyPair().getPublic();
                Vector result = new Vector();
                result.observed.put("provider", generator.getProvider().getName());
                int size;
                if ("RSA".equals(algorithm)) {
                    RSAPublicKey rsa = (RSAPublicKey) key;
                    size = rsa.getModulus().bitLength();
                    result.equal("public_exponent", rsa.getPublicExponent().intValue(), 65537);
                } else if ("DSA".equals(algorithm)) {
                    DSAPublicKey dsa = (DSAPublicKey) key;
                    size = dsa.getParams().getP().bitLength();
                    result.equal("subgroup_bits", dsa.getParams().getQ().bitLength(), 160);
                } else if ("DH".equals(algorithm)) {
                    DHParameterSpec params = ((DHPublicKey) key).getParams();
                    size = params.getP().bitLength();
                    result.equal("generator_in_range", params.getG().compareTo(BigInteger.ONE) > 0
                            && params.getG().compareTo(params.getP().subtract(BigInteger.ONE)) < 0, true);
                } else {
                    size = ((ECPublicKey) key).getParams().getCurve().getField().getFieldSize();
                }
                result.equal("default_bits", size, bits);
                return result.outcome(algorithm + " uninitialized key-pair generator");
            }
        });
    }

    private static void generators(Map<String, Probe> probes) {
        // Android BC AES, DES, DESede, ARC4, Blowfish and digest provider mappings.
        String[] algorithms = {"AES", "DES", "DESede", "ARC4", "Blowfish",
                "HmacMD5", "HmacSHA1", "HmacSHA256", "HmacSHA384", "HmacSHA512"};
        int[] lengths = {24, 8, 24, 16, 16, 16, 20, 32, 48, 64};
        for (int i = 0; i < algorithms.length; i++) {
            final String algorithm = algorithms[i];
            final int length = lengths[i];
            add(probes, new Probe("crypto.keygen." + id(algorithm), "in_scope", false,
                    "Ljavax/crypto/KeyGenerator;.getInstance(Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
                    "Ljavax/crypto/KeyGenerator;.generateKey()Ljavax/crypto/SecretKey;",
                    "Ljavax/crypto/KeyGenerator;.init(I)V") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    KeyGenerator generator = KeyGenerator.getInstance(algorithm);
                    SecretKey key = generator.generateKey();
                    Vector result = new Vector();
                    result.observed.put("provider", generator.getProvider().getName());
                    result.equal("default_length", key.getEncoded().length, length);
                    result.equal("format", key.getFormat(), "RAW");
                    result.equal("algorithm_matches", algorithm.equalsIgnoreCase(key.getAlgorithm()), true);
                    if ("AES".equals(algorithm)) {
                        for (int size : new int[]{128, 192, 256}) {
                            generator.init(size);
                            result.equal("initialized_" + size, generator.generateKey().getEncoded().length, size / 8);
                        }
                    }
                    return result.outcome(algorithm + " default key length and encoding");
                }
            });
        }
    }

    private static void secretFactories(Map<String, Probe> probes) {
        for (final String algorithm : new String[]{"PBKDF2WithHmacSHA1", "PBKDF2WithHmacSHA1And8BIT"}) {
            add(probes, new Probe("crypto.key_factory." + id(algorithm), "in_scope", false, SKF, GENERATE_SECRET) {
                @Override Outcome run(ProbeSession session) throws Exception {
                    // RFC 6070 §2 ASCII and embedded-NUL vectors.
                    SecretKeyFactory factory = SecretKeyFactory.getInstance(algorithm);
                    Vector result = new Vector();
                    result.observed.put("provider", factory.getProvider().getName());
                    byte[] salt = unhex("73616c74");
                    for (int iterations : new int[]{1, 2, 4096}) {
                        String answer = iterations == 1 ? "0c60c80f961f0e71f3a9b524af6012062fe037a6"
                                : iterations == 2 ? "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"
                                : "4b007901b765489abead49d926f721d065a429c1";
                        PBEKeySpec spec = new PBEKeySpec("password".toCharArray(), salt, iterations, 160);
                        result.equal("iterations_" + iterations, hex(factory.generateSecret(spec).getEncoded()), answer);
                    }
                    PBEKeySpec nul = new PBEKeySpec("pass\u0000word".toCharArray(), unhex("7361006c74"), 4096, 128);
                    result.equal("embedded_nul", hex(factory.generateSecret(nul).getEncoded()),
                            "56fa6aa75548099dcc37d7f03425e0c3");
                    return result.outcome(algorithm + " RFC 6070 password-based key derivation");
                }
            });
        }
        for (final String algorithm : new String[]{"DES", "DESede"}) {
            add(probes, new Probe("crypto.key_factory." + id(algorithm), "in_scope", false, SKF, GENERATE_SECRET,
                    "Ljavax/crypto/SecretKeyFactory;.getKeySpec(Ljavax/crypto/SecretKey;Ljava/lang/Class;)Ljava/security/spec/KeySpec;",
                    "Ljavax/crypto/SecretKeyFactory;.translateKey(Ljavax/crypto/SecretKey;)Ljavax/crypto/SecretKey;") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    boolean triple = "DESede".equals(algorithm);
                    byte[] raw = unhex(triple ? "0123456789abcdef23456789abcdef01456789abcdef0123" : "0123456789abcdef");
                    SecretKeyFactory factory = SecretKeyFactory.getInstance(algorithm);
                    SecretKey key = factory.generateSecret(triple ? new DESedeKeySpec(raw) : new DESKeySpec(raw));
                    Vector result = new Vector();
                    result.observed.put("provider", factory.getProvider().getName());
                    result.bytes("generated", key.getEncoded(), raw);
                    byte[] encodedSpec = triple ? ((DESedeKeySpec) factory.getKeySpec(key, DESedeKeySpec.class)).getKey()
                            : ((DESKeySpec) factory.getKeySpec(key, DESKeySpec.class)).getKey();
                    result.bytes("key_spec", encodedSpec, raw);
                    result.bytes("translated", factory.translateKey(new SecretKeySpec(raw, algorithm)).getEncoded(), raw);
                    return result.outcome(algorithm + " raw SecretKeySpec and typed key spec conversions");
                }
            });
        }
    }

    private static void parameters(Map<String, Probe> probes) {
        for (final String algorithm : new String[]{"AES", "DES", "DESede", "Blowfish", "OAEP", "DH", "DSA", "PKCS12PBE"}) {
            add(probes, new Probe("crypto.parameters." + id(algorithm), "in_scope", false, PARAMS,
                    "Ljava/security/AlgorithmParameters;.init(Ljava/security/spec/AlgorithmParameterSpec;)V",
                    "Ljava/security/AlgorithmParameters;.init([B)V",
                    "Ljava/security/AlgorithmParameters;.getEncoded()[B",
                    "Ljava/security/AlgorithmParameters;.getParameterSpec(Ljava/lang/Class;)Ljava/security/spec/AlgorithmParameterSpec;") {
                @Override Outcome run(ProbeSession session) throws Exception {
                    AlgorithmParameters original = AlgorithmParameters.getInstance(algorithm);
                    AlgorithmParameters decoded = AlgorithmParameters.getInstance(algorithm);
                    Vector result = new Vector();
                    result.observed.put("provider", original.getProvider().getName());
                    if ("OAEP".equals(algorithm)) {
                        original.init(mixedOaep());
                        // RFC 8017 A.2.1 defaulted MGF1-SHA1 and explicit SHA-256 / label.
                        byte[] encoded = original.getEncoded();
                        result.equal("der", hex(encoded), "3028a00f300d06096086480165030402010500"
                                + "a215301306092a864886f70d01010904064b69744b6174");
                        decoded.init(encoded);
                        OAEPParameterSpec spec = decoded.getParameterSpec(OAEPParameterSpec.class);
                        String digest = spec.getDigestAlgorithm();
                        String mgf = spec.getMGFAlgorithm();
                        String mgfDigest = ((MGF1ParameterSpec) spec.getMGFParameters()).getDigestAlgorithm();
                        // BC AlgorithmParametersSpi returns the ASN.1 OIDs. JCA names denote the same algorithms.
                        result.observed.put("identifiers", Probe.obj("digest", digest, "mgf", mgf, "mgf_digest", mgfDigest));
                        result.equal("digest", "2.16.840.1.101.3.4.2.1".equals(digest) ? "SHA-256" : digest, "SHA-256");
                        result.equal("mgf", "1.2.840.113549.1.1.8".equals(mgf) ? "MGF1" : mgf, "MGF1");
                        result.equal("mgf_digest", "1.3.14.3.2.26".equals(mgfDigest) ? "SHA-1" : mgfDigest, "SHA-1");
                        result.bytes("label", ((PSource.PSpecified) spec.getPSource()).getValue(), MESSAGE);
                    } else if ("DH".equals(algorithm)) {
                        original.init(new DHParameterSpec(BigInteger.valueOf(23), BigInteger.valueOf(5), 4));
                        result.equal("der", hex(original.getEncoded()), "3009020117020105020104");
                        decoded.init(original.getEncoded());
                        DHParameterSpec spec = decoded.getParameterSpec(DHParameterSpec.class);
                        result.equal("modulus", spec.getP().intValue(), 23);
                        result.equal("generator", spec.getG().intValue(), 5);
                        result.equal("private_length", spec.getL(), 4);
                    } else if ("DSA".equals(algorithm)) {
                        original.init(new DSAParameterSpec(BigInteger.valueOf(23), BigInteger.valueOf(11), BigInteger.valueOf(2)));
                        result.equal("der", hex(original.getEncoded()), "300902011702010b020102");
                        decoded.init(original.getEncoded());
                        DSAParameterSpec spec = decoded.getParameterSpec(DSAParameterSpec.class);
                        result.equal("p", spec.getP().intValue(), 23);
                        result.equal("q", spec.getQ().intValue(), 11);
                        result.equal("g", spec.getG().intValue(), 2);
                    } else if ("PKCS12PBE".equals(algorithm)) {
                        byte[] salt = sequence(8);
                        original.init(new PBEParameterSpec(salt, 1000));
                        result.equal("der", hex(original.getEncoded()), "300e04080001020304050607020203e8");
                        decoded.init(original.getEncoded());
                        PBEParameterSpec spec = decoded.getParameterSpec(PBEParameterSpec.class);
                        result.bytes("salt", spec.getSalt(), salt);
                        result.equal("iterations", spec.getIterationCount(), 1000);
                    } else {
                        byte[] iv = sequence("AES".equals(algorithm) ? 16 : 8);
                        original.init(new IvParameterSpec(iv));
                        byte[] der = join(new byte[]{4, (byte) iv.length}, iv);
                        result.bytes("der", original.getEncoded(), der);
                        decoded.init(der);
                        result.bytes("iv", decoded.getParameterSpec(IvParameterSpec.class).getIV(), iv);
                    }
                    result.bytes("reencoded", decoded.getEncoded(), original.getEncoded());
                    return result.outcome(algorithm + " parameter specification and DER round trip");
                }
            });
        }
    }
}
