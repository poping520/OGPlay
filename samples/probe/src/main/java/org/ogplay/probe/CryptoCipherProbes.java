package org.ogplay.probe;

import java.security.AlgorithmParameters;
import java.security.Key;
import java.util.Arrays;
import java.util.Map;
import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

import static org.ogplay.probe.CryptoProbes.*;
import static org.ogplay.probe.PhaseOne.add;
import static org.ogplay.probe.RuntimeProbes.hex;
import static org.ogplay.probe.RuntimeProbes.unhex;

final class CryptoCipherProbes {
    private static final String UPDATE = "Ljavax/crypto/Cipher;.update([BII)[B";
    private static final String FINAL_SLICE = "Ljavax/crypto/Cipher;.doFinal([BII)[B";
    private static final String OUTPUT_SIZE = "Ljavax/crypto/Cipher;.getOutputSize(I)I";
    private static final String GET_IV = "Ljavax/crypto/Cipher;.getIV()[B";
    private static final String GET_PARAMS = "Ljavax/crypto/Cipher;.getParameters()Ljava/security/AlgorithmParameters;";
    private static final String[] AES_KEYS = {
        "2b7e151628aed2a6abf7158809cf4f3c",
        "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
    };
    private static final byte[] AES_IV = sequence(16);
    private static final byte[] COUNTER = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    private static final byte[] AES_PLAINTEXT = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    // OpenSSLCipher.reset calls EVP_CipherInit_ex with a null key and IV. API 19
    // OpenSSL does not rewind CTR/RC4 in this case. These are the next stream bytes
    // for the same input, computed independently from SP 800-38A and RFC 6229.
    private static final String[] CTR_AFTER_FINAL = {
        "01ed7d9a56c9a8d95789b60a64297b6e46b1b3c8eef2345a4abd5e7750c325af",
        "453f10cf5cf7bdf7dd7ba26e3fb9ee3217ca09e413d43ed3690bf43bfb050911",
        "7000927e2f2192cbe4b6a8b2441ddd48877b6b9f77369a2d205ef3dfe6baf8e7"
    };

    static void register(Map<String, Probe> probes) {
        aes(probes);
        legacyCiphers(probes);
        wraps(probes);
    }

    private static void aes(Map<String, Probe> probes) {
        // NIST SP 800-38A Appendix F, first two blocks (CFB8 uses the first 18 bytes).
        // PKCS padding extensions computed independently using the same mode and RFC 5652 §6.3.
        // Conscrypt's EVP CFB128/OFB128/CTR have block size 1 and do not add PKCS padding.
        aesMode(probes, "ECB/NoPadding", null, AES_PLAINTEXT,
                "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf",
                "bd334f1d6e45f25ff712a214571fa5cc974104846d0ad3ad7734ecb3ecee4eef",
                "f3eed1bdb5d2a03c064b5a7e3db181f8591ccb10d410ed26dc5ba74a31362870");
        aesMode(probes, "ECB/PKCS5Padding", null, AES_PLAINTEXT,
                "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaafa254be88e037ddd9d79fb6411c3f9df8",
                "bd334f1d6e45f25ff712a214571fa5cc974104846d0ad3ad7734ecb3ecee4eefdaa0af074bd8083c8a32d4fc563c55cc",
                "f3eed1bdb5d2a03c064b5a7e3db181f8591ccb10d410ed26dc5ba74a313628704c45dfb3b3b484ec35b0512dc8c1c4d6");
        aesMode(probes, "CBC/NoPadding", AES_IV, AES_PLAINTEXT,
                "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2",
                "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145a",
                "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d");
        aesMode(probes, "CBC/PKCS5Padding", AES_IV, AES_PLAINTEXT,
                "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b255e21d7100b988ffec32feeafaf23538",
                "4f021db243bc633d7178183a9fa071e8b4d9ada9ad7dedf4e5e738763f69145ac81ca99c3a1e883fa8d834316a2275ec",
                "f58c4c04d6e5f1ba779eabfb5f7bfbd69cfc4e967edb808d679f777bc6702c7d3a3aa5e0213db1a9901f9036cf5102d2");
        aesMode(probes, "CTR/NoPadding", COUNTER, AES_PLAINTEXT,
                "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff",
                "1abc932417521ca24f2b0459fe7e6e0b090339ec0aa6faefd5ccc2c6f4ce8e94",
                "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5");
        aesMode(probes, "CTR/PKCS5Padding", COUNTER, AES_PLAINTEXT,
                "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff",
                "1abc932417521ca24f2b0459fe7e6e0b090339ec0aa6faefd5ccc2c6f4ce8e94",
                "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5");
        aesMode(probes, "CFB/NoPadding", AES_IV, AES_PLAINTEXT,
                "3b3fd92eb72dad20333449f8e83cfb4ac8a64537a0b3a93fcde3cdad9f1ce58b",
                "cdc80d6fddf18cab34c25909c99a417467ce7f7f81173621961a2b70171d3d7a",
                "dc7e84bfda79164b7ecd8486985d386039ffed143b28b1c832113c6331e5407b");
        aesMode(probes, "CFB/PKCS5Padding", AES_IV, AES_PLAINTEXT,
                "3b3fd92eb72dad20333449f8e83cfb4ac8a64537a0b3a93fcde3cdad9f1ce58b",
                "cdc80d6fddf18cab34c25909c99a417467ce7f7f81173621961a2b70171d3d7a",
                "dc7e84bfda79164b7ecd8486985d386039ffed143b28b1c832113c6331e5407b");
        aesMode(probes, "CFB8/NoPadding", AES_IV, Arrays.copyOf(AES_PLAINTEXT, 18),
                "3b79424c9c0dd436bace9e0ed4586a4f32b9",
                "cda2521ef0a905ca44cd057cbf0d47a0678a",
                "dc1f1a8520a64db55fcc8ac554844e889700");
        aesMode(probes, "CFB8/PKCS5Padding", AES_IV, Arrays.copyOf(AES_PLAINTEXT, 18),
                "3b79424c9c0dd436bace9e0ed4586a4f32b955",
                "cda2521ef0a905ca44cd057cbf0d47a0678af0",
                "dc1f1a8520a64db55fcc8ac554844e88970026");
        aesMode(probes, "OFB/NoPadding", AES_IV, AES_PLAINTEXT,
                "3b3fd92eb72dad20333449f8e83cfb4a7789508d16918f03f53c52dac54ed825",
                "cdc80d6fddf18cab34c25909c99a4174fcc28b8d4c63837c09e81700c1100401",
                "dc7e84bfda79164b7ecd8486985d38604febdc6740d20b3ac88f6ad82a4fb08d");
        aesMode(probes, "OFB/PKCS5Padding", AES_IV, AES_PLAINTEXT,
                "3b3fd92eb72dad20333449f8e83cfb4a7789508d16918f03f53c52dac54ed825",
                "cdc80d6fddf18cab34c25909c99a4174fcc28b8d4c63837c09e81700c1100401",
                "dc7e84bfda79164b7ecd8486985d38604febdc6740d20b3ac88f6ad82a4fb08d");
        aesMode(probes, "OFB8/NoPadding", AES_IV, Arrays.copyOf(AES_PLAINTEXT, 18),
                "3b95b11c62c9b759f605bbce1812c67c7a3a",
                "cdcf8d68005b50d8529bf43314eb7687fabc",
                "dc3561f46737323ceafa993008313ed9459e");
        aesMode(probes, "OFB8/PKCS5Padding", AES_IV, Arrays.copyOf(AES_PLAINTEXT, 18),
                "3b95b11c62c9b759f605bbce1812c67c7a3a1e",
                "cdcf8d68005b50d8529bf43314eb7687fabc04",
                "dc3561f46737323ceafa993008313ed9459ee5");
        aesMode(probes, "CTS/NoPadding", AES_IV, Arrays.copyOf(AES_PLAINTEXT, 23),
                "43d045af5144e2d09ddc0a5367d2f25d7649abac8119b2",
                "8f380686c854e1ad21f942635a13fd894f021db243bc63",
                "f52e6d1582515c687b148cf3d5a2f4d9f58c4c04d6e5f1");
        aesMode(probes, "SIC/NoPadding", COUNTER, AES_PLAINTEXT,
                "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff",
                "1abc932417521ca24f2b0459fe7e6e0b090339ec0aa6faefd5ccc2c6f4ce8e94",
                "601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5");
        sym(probes, "crypto.aes.default", "AES", "AES", new String[]{AES_KEYS[0]}, null, AES_PLAINTEXT,
                "3ad77bb40d7a3660a89ecaf32466ef97f5d3d58503b9699de785895a96fdbaaf"
                        + "a254be88e037ddd9d79fb6411c3f9df8");
        aesParameters(probes);
        aesFailures(probes);

        // NIST GCM example with zero key, IV and a single zero block. The CCM
        // ciphertext uses RFC 3610 §8 vector 1 with the associated data omitted,
        // and the tag recomputed with PyCryptodome using the RFC's construction.
        aead(probes, "GCM", new byte[16], new byte[12], new byte[16],
                "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf");
        aead(probes, "CCM", unhex("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"),
                unhex("00000003020100a0a1a2a3a4a5"),
                Arrays.copyOfRange(sequence(31), 8, 31),
                "588c979a61c663d2f066d0c2c0f989806d5f6b61dac3847c2051a7ae200bcf");
        extraAesPadding(probes);
    }

    private static void aesMode(Map<String, Probe> probes, String mode, byte[] iv, byte[] input, String... answers) {
        sym(probes, "crypto.aes." + id(mode), "AES/" + mode, "AES", AES_KEYS, iv, input, answers);
    }

    private static void sym(Map<String, Probe> probes, String name, final String transformation,
                            final String algorithm, final String[] keys, final byte[] iv,
                            final byte[] input, final String... answers) {
        add(probes, new Probe(name, "in_scope", false, CIPHER, INIT_KEY, INIT_SPEC,
                UPDATE, FINAL, FINAL_SLICE, OUTPUT_SIZE, GET_IV) {
            @Override Outcome run(ProbeSession session) throws Exception {
                Cipher cipher = Cipher.getInstance(transformation);
                Vector result = new Vector();
                for (int i = 0; i < keys.length; i++) {
                    byte[] keyBytes = unhex(keys[i]);
                    Key key = new SecretKeySpec(keyBytes, algorithm);
                    String prefix = (keyBytes.length * 8) + "_";
                    init(cipher, Cipher.ENCRYPT_MODE, key, iv);
                    result.observed.put("provider", cipher.getProvider().getName());
                    result.equal(prefix + "iv", cipher.getIV() == null ? null : hex(cipher.getIV()),
                            iv == null ? null : hex(iv));
                    byte[] expected = unhex(answers[i]);
                    // JCA only promises an upper bound for getOutputSize. Some BC modes round up.
                    result.equal(prefix + "output_size_fits", cipher.getOutputSize(input.length) >= expected.length, true);
                    result.equal(prefix + "one_shot", hex(cipher.doFinal(input)), answers[i]);
                    String reused = answers[i];
                    if (transformation.startsWith("AES/CTR/")) reused = CTR_AFTER_FINAL[i];
                    if ("ARC4".equals(transformation)) reused = "6982944f18fc82d589c403a47a0d0919";
                    result.equal(prefix + "reused_after_final", hex(cipher.doFinal(input)), reused);
                    init(cipher, Cipher.ENCRYPT_MODE, key, iv);
                    int split = Math.min(5, input.length);
                    byte[] initial = cipher.update(input, 0, split);
                    result.equal(prefix + "buffered_output_size_fits",
                            (initial == null ? 0 : initial.length) + cipher.getOutputSize(input.length - split)
                                    >= expected.length, true);
                    result.equal(prefix + "chunked", hex(join(initial,
                            cipher.doFinal(input, split, input.length - split))), answers[i]);
                    init(cipher, Cipher.DECRYPT_MODE, key, iv);
                    result.bytes(prefix + "known_ciphertext_decrypted", cipher.doFinal(expected), input);
                }
                return result.outcome(transformation + " fixed ciphertext, split update and reuse after doFinal");
            }
        });
    }

    private static void init(Cipher cipher, int mode, Key key, byte[] iv) throws Exception {
        if (iv == null) cipher.init(mode, key);
        else cipher.init(mode, key, new IvParameterSpec(iv));
    }

    private static void aesParameters(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.aes.cbc.parameters", "in_scope", false, CIPHER, INIT_KEY, INIT_SPEC, FINAL,
                GET_IV, GET_PARAMS, OUTPUT_SIZE,
                "Ljavax/crypto/Cipher;.init(ILjava/security/Key;Ljava/security/AlgorithmParameters;)V",
                "Ljava/security/AlgorithmParameters;.getParameterSpec(Ljava/lang/Class;)Ljava/security/spec/AlgorithmParameterSpec;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Cipher cipher = Cipher.getInstance("AES/CBC/PKCS5Padding");
                Key key = new SecretKeySpec(unhex(AES_KEYS[0]), "AES");
                cipher.init(Cipher.ENCRYPT_MODE, key);
                Vector result = new Vector();
                byte[] iv = cipher.getIV();
                result.observed.put("provider", cipher.getProvider().getName());
                result.observed.put("generated_iv", iv == null ? null : hex(iv));
                result.equal("generated_iv_length", iv == null ? 0 : iv.length, 16);
                result.equal("output_size_partial", cipher.getOutputSize(17), 32);
                result.equal("output_size_aligned", cipher.getOutputSize(32), 48);
                AlgorithmParameters parameters = cipher.getParameters();
                result.equal("has_parameters", parameters != null, true);
                byte[] encrypted = cipher.doFinal(AES_PLAINTEXT);
                if (parameters != null && iv != null) {
                    result.equal("parameters_match_get_iv",
                            Arrays.equals(parameters.getParameterSpec(IvParameterSpec.class).getIV(), iv), true);
                    cipher.init(Cipher.DECRYPT_MODE, key, parameters);
                    result.bytes("decrypted_using_parameters", cipher.doFinal(encrypted), AES_PLAINTEXT);
                }
                cipher.init(Cipher.ENCRYPT_MODE, key);
                result.equal("successive_auto_iv_differs", !Arrays.equals(cipher.getIV(), iv), true);
                return result.outcome("CBC generated IV, parameters and exact output size");
            }
        });
    }

    private static void aesFailures(Map<String, Probe> probes) {
        add(probes, new Probe("crypto.aes.cbc.exceptions", "in_scope", false, CIPHER, INIT_SPEC, FINAL) {
            @Override Outcome run(ProbeSession session) throws Exception {
                Key key = new SecretKeySpec(unhex(AES_KEYS[0]), "AES");
                Cipher plain = Cipher.getInstance("AES/CBC/NoPadding");
                init(plain, Cipher.ENCRYPT_MODE, key, AES_IV);
                // Encrypt an invalid padding block whose last byte is zero.
                byte[] malformed = plain.doFinal(new byte[16]);
                Vector result = new Vector();
                Cipher padded = Cipher.getInstance("AES/CBC/PKCS5Padding");
                init(padded, Cipher.DECRYPT_MODE, key, AES_IV);
                result.equal("bad_padding", finalException(padded, malformed), "javax.crypto.BadPaddingException");
                result.equal("unaligned_no_padding", finalException(plain, new byte[15]),
                        "javax.crypto.IllegalBlockSizeException");
                return result.outcome("CBC rejects malformed padding and unaligned NoPadding input");
            }
        });
    }

    static String finalException(Cipher cipher, byte[] data) throws Exception {
        try {
            cipher.doFinal(data);
            return null;
        } catch (BadPaddingException rejected) {
            return rejected.getClass().getName();
        } catch (IllegalBlockSizeException rejected) {
            return rejected.getClass().getName();
        }
    }

    private static void aead(Map<String, Probe> probes, final String mode, final byte[] keyBytes,
                              final byte[] iv, final byte[] input, final String answer) {
        add(probes, new Probe("crypto.aes." + id(mode) + ".nopadding", "in_scope", false,
                CIPHER, INIT_SPEC, "Ljavax/crypto/Cipher;.updateAAD([B)V", FINAL, OUTPUT_SIZE) {
            @Override Outcome run(ProbeSession session) throws Exception {
                Cipher cipher = Cipher.getInstance("AES/" + mode + "/NoPadding");
                Key key = new SecretKeySpec(keyBytes, "AES");
                init(cipher, Cipher.ENCRYPT_MODE, key, iv);
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                // The API 19 BC SPI implements AEAD internally, but lacks CipherSpi's AAD override.
                String aadException = null;
                try {
                    cipher.updateAAD(sequence(8));
                } catch (UnsupportedOperationException rejected) {
                    aadException = rejected.getClass().getName();
                }
                result.equal("aad_exception", aadException, "java.lang.UnsupportedOperationException");
                byte[] expected = unhex(answer);
                result.equal("output_size", cipher.getOutputSize(input.length), expected.length);
                result.equal("ciphertext_and_tag", hex(cipher.doFinal(input)), answer);
                init(cipher, Cipher.DECRYPT_MODE, key, iv);
                result.bytes("decrypted", cipher.doFinal(expected), input);
                init(cipher, Cipher.DECRYPT_MODE, key, iv);
                expected[expected.length - 1] ^= 1;
                result.equal("bad_tag", finalException(cipher, expected), "javax.crypto.BadPaddingException");
                return result.outcome(mode + " authenticated ciphertext and tag rejection");
            }
        });
    }

    private static void extraAesPadding(Map<String, Probe> probes) {
        // The BC CBC implementation accepts these aliases. Random padding bytes make some
        // ciphertexts nondeterministic; check final size and decryption on a nonaligned message.
        for (final String padding : new String[]{"PKCS7Padding", "ZeroBytePadding", "ISO10126Padding",
                "ISO7816-4Padding", "X923Padding", "TBCPadding", "WithCTS"}) {
            add(probes, new Probe("crypto.aes.cbc." + id(padding), "in_scope", false, CIPHER, INIT_SPEC, FINAL) {
                @Override Outcome run(ProbeSession session) throws Exception {
                    Key key = new SecretKeySpec(unhex(AES_KEYS[0]), "AES");
                    byte[] input = Arrays.copyOf(AES_PLAINTEXT, 23);
                    Cipher cipher = Cipher.getInstance("AES/CBC/" + padding);
                    init(cipher, Cipher.ENCRYPT_MODE, key, AES_IV);
                    byte[] encrypted = cipher.doFinal(input);
                    Vector result = new Vector();
                    result.observed.put("provider", cipher.getProvider().getName());
                    result.equal("ciphertext_length", encrypted.length, "WithCTS".equals(padding) ? 23 : 32);
                    init(cipher, Cipher.DECRYPT_MODE, key, AES_IV);
                    result.bytes("decrypted", cipher.doFinal(encrypted), input);
                    return result.outcome("AES/CBC/" + padding + " length and round trip");
                }
            });
        }
    }

    private static void legacyCiphers(Map<String, Probe> probes) {
        // DES FIPS 81 example key and Schneier's Blowfish all-zero ECB vector.
        sym(probes, "crypto.des.ecb.nopadding", "DES/ECB/NoPadding", "DES",
                new String[]{"0123456789abcdef"}, null, unhex("4e6f772069732074"), "3fa40e8a984d4815");
        sym(probes, "crypto.blowfish.ecb.nopadding", "Blowfish/ECB/NoPadding", "Blowfish",
                new String[]{"0000000000000000"}, null, new byte[8], "4ef997456198dd78");
        // RFC 6229 §2, first 16 bytes at offset zero.
        sym(probes, "crypto.arc4.default", "ARC4", "ARC4", new String[]{"0102030405"}, null,
                new byte[16], "b2396305f03dc027ccc3524a0a1118a8");

        // Fixed extensions for DES, TDEA and Blowfish computed independently with the
        // SP 800-38A mode definitions and RFC 5652 padding using PyCryptodome.
        byte[] input = sequence(16);
        byte[] iv = sequence(8);
        String[] des = {"0123456789abcdef"};
        String[] desede = {"0123456789abcdef23456789abcdef01", "0123456789abcdef23456789abcdef01456789abcdef0123"};
        String[] blowfish = {"000102030405060708090a0b0c0d0e0f"};
        sym(probes, "crypto.des.default", "DES", "DES", des, null,
                input, "3260266c2cf202e28325790654a444d9086f9a1d74c94d4e");
        sym(probes, "crypto.des.ecb.pkcs5padding", "DES/ECB/PKCS5Padding", "DES", des, null,
                input, "3260266c2cf202e28325790654a444d9086f9a1d74c94d4e");
        sym(probes, "crypto.des.cbc.nopadding", "DES/CBC/NoPadding", "DES", des, iv,
                input, "d5d44ff720683d0d9f3abfb054bca95d");
        sym(probes, "crypto.des.cbc.pkcs5padding", "DES/CBC/PKCS5Padding", "DES", des, iv,
                input, "d5d44ff720683d0d9f3abfb054bca95d7011ca2a9b8a2cd8");
        sym(probes, "crypto.des.ctr.nopadding", "DES/CTR/NoPadding", "DES", des, iv,
                input, "3261246f28f704e51949a6f428eee946");
        sym(probes, "crypto.des.ctr.pkcs5padding", "DES/CTR/PKCS5Padding", "DES", des, iv,
                input, "3261246f28f704e51949a6f428eee946abf645889936d9bf");
        sym(probes, "crypto.des.cfb.nopadding", "DES/CFB/NoPadding", "DES", des, iv,
                input, "3261246f28f704e5feb137666e12ac13");
        sym(probes, "crypto.des.cfb.pkcs5padding", "DES/CFB/PKCS5Padding", "DES", des, iv,
                input, "3261246f28f704e5feb137666e12ac13b05fddb310f21898");
        sym(probes, "crypto.des.cfb8.nopadding", "DES/CFB8/NoPadding", "DES", des, iv,
                input, "32e1ebb2f0a3a0623b4810f47a9d9f86");
        sym(probes, "crypto.des.cfb8.pkcs5padding", "DES/CFB8/PKCS5Padding", "DES", des, iv,
                input, "32e1ebb2f0a3a0623b4810f47a9d9f86ad");
        sym(probes, "crypto.des.ofb.nopadding", "DES/OFB/NoPadding", "DES", des, iv,
                input, "3261246f28f704e564fb943b71220674");
        sym(probes, "crypto.des.ofb.pkcs5padding", "DES/OFB/PKCS5Padding", "DES", des, iv,
                input, "3261246f28f704e564fb943b712206742c4f4642f5546ec7");
        sym(probes, "crypto.des.ofb8.nopadding", "DES/OFB8/NoPadding", "DES", des, iv,
                input, "32e196ecf227a290ce7a56ff238c0fed");
        sym(probes, "crypto.des.ofb8.pkcs5padding", "DES/OFB8/PKCS5Padding", "DES", des, iv,
                input, "32e196ecf227a290ce7a56ff238c0fed96");
        sym(probes, "crypto.des.cts.nopadding", "DES/CTS/NoPadding", "DES", des, iv,
                Arrays.copyOf(input, 13), "f1d526da8473972ed5d44ff720");
        sym(probes, "crypto.desede.default", "DESede", "DESede", desede, null,
                input, "2361ace6c517105145f0b449fb185ffd5db28100613ac225", "30329253bd2965402ea437be9266178c832846b52f9e213d");
        sym(probes, "crypto.desede.ecb.pkcs5padding", "DESede/ECB/PKCS5Padding", "DESede", desede, null,
                input, "2361ace6c517105145f0b449fb185ffd5db28100613ac225", "30329253bd2965402ea437be9266178c832846b52f9e213d");
        sym(probes, "crypto.desede.ecb.nopadding", "DESede/ECB/NoPadding", "DESede", desede, null,
                input, "2361ace6c517105145f0b449fb185ffd", "30329253bd2965402ea437be9266178c");
        sym(probes, "crypto.desede.cbc.nopadding", "DESede/CBC/NoPadding", "DESede", desede, iv,
                input, "86e965bd1ec44461dee81691ecc11016", "4eba739c998bcb602012c4745ee86610");
        sym(probes, "crypto.desede.cbc.pkcs5padding", "DESede/CBC/PKCS5Padding", "DESede", desede, iv,
                input, "86e965bd1ec44461dee81691ecc1101699f40bd34b2664e0", "4eba739c998bcb602012c4745ee866102fbca302837ae06f");
        sym(probes, "crypto.desede.ctr.nopadding", "DESede/CTR/NoPadding", "DESede", desede, iv,
                input, "2360aee5c1121656dbd8cae8cccfca6c", "30339050b92c63476ab4c379f4368a8c");
        sym(probes, "crypto.desede.ctr.pkcs5padding", "DESede/CTR/PKCS5Padding", "DESede", desede, iv,
                input, "2360aee5c1121656dbd8cae8cccfca6cb4d0874056b544a9", "30339050b92c63476ab4c379f4368a8c407c7fda8feb4955");
        sym(probes, "crypto.desede.cfb.nopadding", "DESede/CFB/NoPadding", "DESede", desede, iv,
                input, "2360aee5c1121656915940d22fc49ff8", "30339050b92c6347413d7496284f3f9c");
        sym(probes, "crypto.desede.cfb.pkcs5padding", "DESede/CFB/PKCS5Padding", "DESede", desede, iv,
                input, "2360aee5c1121656915940d22fc49ff8", "30339050b92c6347413d7496284f3f9c");
        sym(probes, "crypto.desede.cfb8.nopadding", "DESede/CFB8/NoPadding", "DESede", desede, iv,
                input, "23f10fc7f36ea9fa7b05c8f5c760d8a4", "3012178c41584f4fc81c52be045538f5");
        sym(probes, "crypto.desede.cfb8.pkcs5padding", "DESede/CFB8/PKCS5Padding", "DESede", desede, iv,
                input, "23f10fc7f36ea9fa7b05c8f5c760d8a47e", "3012178c41584f4fc81c52be045538f5cb");
        sym(probes, "crypto.desede.ofb.nopadding", "DESede/OFB/NoPadding", "DESede", desede, iv,
                input, "2360aee5c1121656d8e790cb5c903713", "30339050b92c6347ae4e38d5aa34d352");
        sym(probes, "crypto.desede.ofb.pkcs5padding", "DESede/OFB/PKCS5Padding", "DESede", desede, iv,
                input, "2360aee5c1121656d8e790cb5c903713", "30339050b92c6347ae4e38d5aa34d352");
        sym(probes, "crypto.desede.ofb8.nopadding", "DESede/OFB8/NoPadding", "DESede", desede, iv,
                input, "23f1888dd8aebc5b19f7305e2af65f5a", "30121c52833837d68da45cf5b6bdcb5b");
        sym(probes, "crypto.desede.ofb8.pkcs5padding", "DESede/OFB8/PKCS5Padding", "DESede", desede, iv,
                input, "23f1888dd8aebc5b19f7305e2af65f5a9a", "30121c52833837d68da45cf5b6bdcb5bb6");
        sym(probes, "crypto.desede.cts.nopadding", "DESede/CTS/NoPadding", "DESede", desede, iv,
                Arrays.copyOf(input, 13), "03b48d74f2f465c786e965bd1e", "0e741e5a05cd3d484eba739c99");
        sym(probes, "crypto.blowfish.default", "Blowfish", "Blowfish", blowfish, null,
                input, "866f5e72e59a19517ed71bb6156c446895fcb249bf3d4145");
        sym(probes, "crypto.blowfish.ecb.pkcs5padding", "Blowfish/ECB/PKCS5Padding", "Blowfish", blowfish, null,
                input, "866f5e72e59a19517ed71bb6156c446895fcb249bf3d4145");
        sym(probes, "crypto.blowfish.cbc.nopadding", "Blowfish/CBC/NoPadding", "Blowfish", blowfish, iv,
                input, "b995f24ddfe87bf000f62ef66a032d40");
        sym(probes, "crypto.blowfish.cbc.pkcs5padding", "Blowfish/CBC/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "b995f24ddfe87bf000f62ef66a032d40ab91bbf0bafc3fd2");
        sym(probes, "crypto.blowfish.ctr.nopadding", "Blowfish/CTR/NoPadding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f564a93bf2107c7a0ec");
        sym(probes, "crypto.blowfish.ctr.pkcs5padding", "Blowfish/CTR/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f564a93bf2107c7a0ec067cb3b9307a9822");
        sym(probes, "crypto.blowfish.cfb.nopadding", "Blowfish/CFB/NoPadding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f561f02aa8c09e06143");
        sym(probes, "crypto.blowfish.cfb.pkcs5padding", "Blowfish/CFB/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f561f02aa8c09e061438994c8586c40bdee");
        sym(probes, "crypto.blowfish.cfb8.nopadding", "Blowfish/CFB8/NoPadding", "Blowfish", blowfish, iv,
                input, "863274866fd9d05c5db58b01366ff341");
        sym(probes, "crypto.blowfish.cfb8.pkcs5padding", "Blowfish/CFB8/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "863274866fd9d05c5db58b01366ff341a3");
        sym(probes, "crypto.blowfish.ofb.nopadding", "Blowfish/OFB/NoPadding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f56bbcbd9358157eab9");
        sym(probes, "crypto.blowfish.ofb.pkcs5padding", "Blowfish/OFB/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "866e5c71e19f1f56bbcbd9358157eab9cf9c3251c0f68a86");
        sym(probes, "crypto.blowfish.ofb8.nopadding", "Blowfish/OFB8/NoPadding", "Blowfish", blowfish, iv,
                input, "86324ae2af5c8a266c0644bfcd76a37a");
        sym(probes, "crypto.blowfish.ofb8.pkcs5padding", "Blowfish/OFB8/PKCS5Padding", "Blowfish", blowfish, iv,
                input, "86324ae2af5c8a266c0644bfcd76a37a8c");
        sym(probes, "crypto.blowfish.cts.nopadding", "Blowfish/CTS/NoPadding", "Blowfish", blowfish, iv,
                Arrays.copyOf(input, 13), "869521aef8ef01c3b995f24ddf");
    }

    private static void wraps(Map<String, Probe> probes) {
        // RFC 3394 §4.1 and RFC 3217 §3.4.
        wrap(probes, "AESWrap", "AES", "000102030405060708090a0b0c0d0e0f", null,
                "00112233445566778899aabbccddeeff", "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5");
        wrap(probes, "DESedeWrap", "DESede", "255e0d1c07b646dfb3134cc843ba8aa71f025b7c0838251f",
                unhex("5dd4cbfc96f5453b"), "2923bf85e06dd6ae529149f1f1bae9eab3a7da3d860d3e98",
                "690107618ef092b3b48ca1796b234ae9fa33ebb4159604037db5d6a84eb3aac2768c632775a467d4");
    }

    private static void wrap(Map<String, Probe> probes, final String transformation, final String algorithm,
                             final String wrappingKey, final byte[] iv, final String raw, final String wrapped) {
        add(probes, new Probe("crypto.wrap." + id(algorithm), "in_scope", false, CIPHER, INIT_KEY, INIT_SPEC,
                "Ljavax/crypto/Cipher;.wrap(Ljava/security/Key;)[B",
                "Ljavax/crypto/Cipher;.unwrap([BLjava/lang/String;I)Ljava/security/Key;") {
            @Override Outcome run(ProbeSession session) throws Exception {
                Cipher cipher = Cipher.getInstance(transformation);
                Key kek = new SecretKeySpec(unhex(wrappingKey), algorithm);
                init(cipher, Cipher.WRAP_MODE, kek, iv);
                Vector result = new Vector();
                result.observed.put("provider", cipher.getProvider().getName());
                result.equal("wrapped", hex(cipher.wrap(new SecretKeySpec(unhex(raw), algorithm))), wrapped);
                cipher.init(Cipher.UNWRAP_MODE, kek);
                Key unwrapped = cipher.unwrap(unhex(wrapped), algorithm, Cipher.SECRET_KEY);
                result.equal("unwrapped", hex(unwrapped.getEncoded()), raw);
                return result.outcome(transformation + " wrap and unwrap known answers");
            }
        });
    }
}
