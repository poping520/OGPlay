package org.ogplay.probe;

import android.util.Base64;
import java.math.BigInteger;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.RSAPrivateCrtKeySpec;
import java.security.spec.RSAPublicKeySpec;
import java.security.spec.X509EncodedKeySpec;

final class CryptoFixtures {
    // Test-only DER key pairs generated outside the Probe. The EC keys are the
    // P-256 scalars and points in RFC 5903 §8.1, encoded as PKCS#8 and SPKI.
    static KeyPair pair(String algorithm, String publicKey, String privateKey) throws Exception {
        KeyFactory factory = KeyFactory.getInstance(algorithm);
        return new KeyPair(factory.generatePublic(new X509EncodedKeySpec(decode(publicKey))),
                factory.generatePrivate(new PKCS8EncodedKeySpec(decode(privateKey))));
    }

    static KeyPair rsa() throws Exception {
        return pair("RSA", RSA_PUBLIC, RSA_PRIVATE);
    }

    static RSAPrivateCrtKeySpec rsaCrt(RSAPublicKeySpec publicSpec, BigInteger privateExponent) {
        return new RSAPrivateCrtKeySpec(publicSpec.getModulus(), publicSpec.getPublicExponent(),
                privateExponent,
                new BigInteger(1, RuntimeProbes.unhex(
                "f047102f20a5a1e8a681010fd7cc394f2c28b51c868022a8a693bc8f6d9e26596cb4ccbcac6fabd2a57e89ebb54b7309" +
                "c5bfd59b50729065c70390ba1464d5020f42c7907f0b650d3651c4ffb8ecc737267c723cf28f5f32090e5c47487a8c41")),
                new BigInteger(1, RuntimeProbes.unhex(
                "cb3dd07de363dce4230c38c0b4dae55aba08d57cd560565f96f66ba4bbb06d4496d97b46c0d9eb6244d3567594985f23" +
                "a80dd4fdbcfc5c4e1978a5d82fec8e20e00cd7998d66b46dfb49329f032f0317273f0fca8c1ef98c6a4abd8360a91355")),
                new BigInteger(1, RuntimeProbes.unhex(
                "10464ff3513bc6ceb2507498f51df117cabeeb1d8682b156490a22b0785548f5afa64363d82e2c2e35656fc4166088e7" +
                "358cbbd42c4bc63a0e93d03eba58e610a0a5fe4c6c3e8256b5c22e1cc012d19f8baccc34129de16dcb2d3cd4db34e9c1")),
                new BigInteger(1, RuntimeProbes.unhex(
                "3ab61ca8812bb3ffe37000650ad9c12fb8561cf5f6dde195b39273fe1e8a14c69f9d2bd3809a7338ec10f104fe6c15ef" +
                "c2f85bf300c9161129a284a63311eeece84c4d6bfa411516406520ce76a8fa07923ad160fda1650d7f17202ff7986011")),
                new BigInteger(1, RuntimeProbes.unhex(
                "b96405ecd9d42d9d8129baad9b4e81bc103ffe5d672cb65047f562e0b8bb523eaa4e8a9a83b01716f9aebf8e7b324b8a" +
                "b4e02be37ff1c21fcadbec55749a39d56b62fa473efe34ec0a600e810b483ea5ffbdb409b5d5ec1f9a416c55d9482b93")));
    }

    static byte[] decode(String value) {
        return Base64.decode(value, Base64.DEFAULT);
    }

    static final String RSA_PRIVATE =
              "MIIDlwIBADANBgkqhkiG9w0BAQEFAASCA4EwggN9AgEAAoHBAL7CXnQfZA6M0ymm7OBGtdob6wNT7jpO2Aay1VRi10WqABxz"
            + "yam0YhDC9NDSygLaNC3lQvpHNv1auRsyNF3HHTWETG8fM+NjAfjO9NqHU3QblfZmVuAUOGc0HAOBaCOMNrjEuD/chsBjX6vK"
            + "U/UxzIkFQKo7uwfBtG2kCeXECgvMEMwC8HxVvLc/uVAtW763DsgCCt5SQ5/NiTtozif3kYuU3pSDkRIBNv/ETAyiCr92/11a"
            + "EQc+Jma95n3OIAJklQIDAQABAoHBAIJ3f5BeU9mbuWS4dTQjSIcnJwI8qieY+ykBsOQpQeC0l6TeEGQQKi4amMQmFuICiUOG"
            + "lNuGT3bSUN3n5WbqIvESThCGP7/aXGPmcBLyEULNBzDh57MUQUtcmXhQToBZiXhX7MHTbT+plXzMDAQ5LRf4LyjMrdW5X4L6"
            + "gKEvGd7laEr7CN2Myi6s6LTO1rtmZTaDsgBORkdEWmSCH/HgG8eYzADEO+/91zX2kHclltx3+52cm1CaQedDLhbkQ62zAQJh"
            + "APBHEC8gpaHopoEBD9fMOU8sKLUchoAiqKaTvI9tniZZbLTMvKxvq9KlfonrtUtzCcW/1ZtQcpBlxwOQuhRk1QIPQseQfwtl"
            + "DTZRxP+47Mc3JnxyPPKPXzIJDlxHSHqMQQJhAMs90H3jY9zkIww4wLTa5Vq6CNV81WBWX5b2a6S7sG1Eltl7RsDZ62JE01Z1"
            + "lJhfI6gN1P28/FxOGXil2C/sjiDgDNeZjWa0bftJMp8DLwMXJz8Pyowe+YxqSr2DYKkTVQJgEEZP81E7xs6yUHSY9R3xF8q+"
            + "6x2GgrFWSQoisHhVSPWvpkNj2C4sLjVlb8QWYIjnNYy71CxLxjoOk9A+uljmEKCl/kxsPoJWtcIuHMAS0Z+LrMw0Ep3hbcst"
            + "PNTbNOnBAmA6thyogSuz/+NwAGUK2cEvuFYc9fbd4ZWzknP+HooUxp+dK9OAmnM47BDxBP5sFe/C+FvzAMkWESmihKYzEe7s"
            + "6ExNa/pBFRZAZSDOdqj6B5I60WD9oWUNfxcgL/eYYBECYQC5ZAXs2dQtnYEpuq2bToG8ED/+XWcstlBH9WLguLtSPqpOipqD"
            + "sBcW+a6/jnsyS4q04Cvjf/HCH8rb7FV0mjnVa2L6Rz7+NOwKYA6BC0g+pf+9tAm11ewfmkFsVdlIK5M=";

    static final String RSA_PUBLIC =
              "MIHfMA0GCSqGSIb3DQEBAQUAA4HNADCByQKBwQC+wl50H2QOjNMppuzgRrXaG+sDU+46TtgGstVUYtdFqgAcc8mptGIQwvTQ"
            + "0soC2jQt5UL6Rzb9WrkbMjRdxx01hExvHzPjYwH4zvTah1N0G5X2ZlbgFDhnNBwDgWgjjDa4xLg/3IbAY1+rylP1McyJBUCq"
            + "O7sHwbRtpAnlxAoLzBDMAvB8Vby3P7lQLVu+tw7IAgreUkOfzYk7aM4n95GLlN6Ug5ESATb/xEwMogq/dv9dWhEHPiZmveZ9"
            + "ziACZJUCAwEAAQ==";

    static final String EC_PRIVATE =
              "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgyI8B9RDZrD9wopLaojFt5UTpqriv6EBJxiqcV4YtFDOhRANC"
            + "AATa0LZTlCIc+bBR4f7KV4fQmN/mN/yQue+UXQw3clgRgFJxoEYc24JS1h8cRW+j5Zqx9FszrM9fWDieBXe4mQuz";

    static final String EC_PUBLIC =
              "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE2tC2U5QiHPmwUeH+yleH0Jjf5jf8kLnvlF0MN3JYEYBScaBGHNuCUtYfHEVv"
            + "o+WasfRbM6zPX1g4ngV3uJkLsw==";

    static final String EC_PEER_PUBLIC =
              "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE0S37UonI1PgSCLcCcDmMNCKWlwoLzLdMc2/HVUSUv2NW+/PKNmzCPoFXhUwT"
            + "xY1qrCPwRq2jD4NT508zA5hyqw==";

    static final String EC_PEER_PRIVATE =
              "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgxu+cXXiuASoBEWSss5fOIIhoXY8Gv5vgsoOrRkdr7lOhRANC"
            + "AATRLftSicjU+BIItwJwOYw0IpaXCgvMt0xzb8dVRJS/Y1b788o2bMI+gVeFTBPFjWqsI/BGraMPg1PnTzMDmHKr";

    static final String DSA_PRIVATE =
              "MIIBSgIBADCCASsGByqGSM44BAEwggEeAoGBAK4+VKmtcIaIO+egpjK+wpdRw/j7czyrIkfjJowySFrqn1blhgy9i9ccLSGl"
            + "5PXLYVNbXEb3gx36Ztjf2QECyp4mrZGQjk9MUncyrejXtANHiDRKXJsafwUC8uMBfis40LiBT/TW3/q9tr1XAXWPZuHrdBpk"
            + "UMdOGi2TR/xlift7AhUArIPTuPxq0TB5mX2HVg/MXzs7sCMCgYAPK1E+So9v4reNp0L0M7uQrn9J/A5G1gTZie/EK2DcWaWO"
            + "e8qYUrckyQpRy0YybbTyBi9Tp5aYI9AtpH6KiJPXZfGxqKjuIZX1wqcxfdThofehnYIb1M0bZRKBNt0K6Sohzmlog1MTw0Hw"
            + "WbVZgTe5DH0QRTc2gzFz8CAqDdS7NgQWAhQ6d+IBwuaRcnT17je4/caTcabMpw==";

    static final String DSA_PUBLIC =
              "MIIBtjCCASsGByqGSM44BAEwggEeAoGBAK4+VKmtcIaIO+egpjK+wpdRw/j7czyrIkfjJowySFrqn1blhgy9i9ccLSGl5PXL"
            + "YVNbXEb3gx36Ztjf2QECyp4mrZGQjk9MUncyrejXtANHiDRKXJsafwUC8uMBfis40LiBT/TW3/q9tr1XAXWPZuHrdBpkUMdO"
            + "Gi2TR/xlift7AhUArIPTuPxq0TB5mX2HVg/MXzs7sCMCgYAPK1E+So9v4reNp0L0M7uQrn9J/A5G1gTZie/EK2DcWaWOe8qY"
            + "UrckyQpRy0YybbTyBi9Tp5aYI9AtpH6KiJPXZfGxqKjuIZX1wqcxfdThofehnYIb1M0bZRKBNt0K6Sohzmlog1MTw0HwWbVZ"
            + "gTe5DH0QRTc2gzFz8CAqDdS7NgOBhAACgYBlQIRCpyHDQBiCWexjeY9Xm2Qse8+76GOwizfxZYNMe8lB7Lb9tKnnfVvjhI3/"
            + "bm55qZr5pg0+bfCuqvodspkgHHQ4D895ExNxQ7h/7NQ201aBrW9tsEEo0Y+BbU/FvvD62/EcGO98elCODfrIZoF745Fi+SoZ"
            + "Z8PwzjOIya/NIA==";

    static final String CERTIFICATE =
              "MIICIjCCAUugAwIBAgIBEzANBgkqhkiG9w0BAQsFADAXMRUwEwYDVQQDDAxPR1BsYXkgUHJvYmUwHhcNMjAwMTAxMDAwMDAw"
            + "WhcNNDAwMTAxMDAwMDAwWjAXMRUwEwYDVQQDDAxPR1BsYXkgUHJvYmUwgd8wDQYJKoZIhvcNAQEBBQADgc0AMIHJAoHBAL7C"
            + "XnQfZA6M0ymm7OBGtdob6wNT7jpO2Aay1VRi10WqABxzyam0YhDC9NDSygLaNC3lQvpHNv1auRsyNF3HHTWETG8fM+NjAfjO"
            + "9NqHU3QblfZmVuAUOGc0HAOBaCOMNrjEuD/chsBjX6vKU/UxzIkFQKo7uwfBtG2kCeXECgvMEMwC8HxVvLc/uVAtW763DsgC"
            + "Ct5SQ5/NiTtozif3kYuU3pSDkRIBNv/ETAyiCr92/11aEQc+Jma95n3OIAJklQIDAQABMA0GCSqGSIb3DQEBCwUAA4HBAJYB"
            + "2gjoguO//T56RJsvL+O8ToLnmTs3fsQ7xWNLnpDwl17t7IznN0jFbwJtOzPmCFHvEFk1bp8omCIFRbtBuMfH4G2YtI2rqXIU"
            + "kMjCKzteBf97mcY1Fz8PgOPiik0Q6ZjW1TuT9ACbXsdzax9LwbggqhNM4l/vVABP0bHkGsdglGFrRzWBvxjHmYUeHC+pOEJ2"
            + "0KD/1AC23S0BZjxNEExUjtwzSe+lw2SbRhM5Sm4AbnLvIZxD49zD0L3cQii0nQ==";

    // PKCS#12 fixture independently serialized from the same RSA pair and
    // self-signed certificate, using PBESv1 SHA-1/TripleDES and a SHA-1 MAC.
    static final String PKCS12 =
              "MIIHdAIBAzCCBzoGCSqGSIb3DQEHAaCCBysEggcnMIIHIzCCAucGCSqGSIb3DQEHBqCCAtgwggLUAgEAMIICzQYJKoZIhvcN"
            + "AQcBMBwGCiqGSIb3DQEMAQMwDgQInKJ5D/VAMOoCAggAgIICoHstBSACeSHWRU8nb34qXRNz10U3z+a9Y1ckp9jFaoE9nUeg"
            + "1HAG3DkA4rj+5zMKwSn5+5riH8LUf5jy/39Gv4chBRSAqjItbtWkeznw37/7Slzv8Qwb42FLzUFObyHGDB30oq7dzJQk2evs"
            + "yqL8aZm7w388s/RvKVdgACNpete8hm9rLupLz+o0XfqJeWYXMZoc9fVfpnuvOSaQxRZ5MdZz1lZhZiWGTVk577o6qqaF/KDV"
            + "f5ZqMlMjjelqfvoKPEG1wVnvf0Daco2nmPDV+hNRlF2C+74OCD05nLsaW2+hrmSXTI7h39m5HChljj2qUKNhc0NEvGtIQIBO"
            + "T5rMeBNGiNPmglO2c52YfX+ZPXWxwDx7Qe5sCCpRmTfHsWmppx/dh0nltd0mr+tHLJnEp+WPEPf9DMYKdQT9m3ARUALDkTtK"
            + "eDBFRL7oUFM31dgqL7isUCXWbkuO3ZOH6UCaSebCtRVEYuIhMJ0I9Jd4mGgws9mVDK72Kg3dEraWMiEtZ0GbsyMrlqSW23Nf"
            + "5em8rKQOfMJSH+gjhAiqSGjPk2dM/fIxeBczq8qGtzdgzrZwEIXS4CVXZpSt8ysowCRwvmJ8WrctXgRCNGz7HTt/S1jiGTvD"
            + "MHgJOptOiuRRemtla4iFBRDo4U5ezf1S92g0MKmzxsv/5TEKOspb86Cq7l0plu60xKv4ZLj3bLoMd/k1vkeeyo3wdxGPDN/p"
            + "qOxawe4P+CxtTEouUD8JLi7zRXONDgT3/i1az0qUQ6UxhA9XC8BmvKgjtDkBrrrBdz5XapJ2EzvuY1+05T+xuKepY3q2+QUr"
            + "U+lwicOGkFJsZhmpW40e6Cu5VZOuklIeToe93orNlKZBzna2BQvBOGxyZo28KKBYKRJQXzqYPO69ViGJlDCCBDQGCSqGSIb3"
            + "DQEHAaCCBCUEggQhMIIEHTCCBBkGCyqGSIb3DQEMCgECoIIDxjCCA8IwHAYKKoZIhvcNAQwBAzAOBAht0U2qS32SGgICCAAE"
            + "ggOgsu9KuoLFFTUPblssN4KAFPChCK/yWVTiG/oRZSFLknCoaNd/YHBz6Nr4bJq3suHvQawvzM4pjVw0isLBSjKAuBtHpExo"
            + "0B+Gaq88QwkpukD7F2gpJXIc20hdDGWDkjsEObS/I+val9rdais23tLksThUcXUgaR0QJLWAF0edFtDSo6lkQMB78oTcAl63"
            + "SqfcE2NxtxkERwX1cSN3I9zbXDoYH8MSHXYsdH9Fw1hQEoeTXPFSVJeCJlKhp4+4NYcNl7VROMjM9UIMAcgVfQuuNxkiAF3b"
            + "MXaMY12y63Gtl9P2JM9pBiwg+0k563nIUnCuETmma3VJrp3xISbllMjdJTsoawaioS7Nst/YvdeetI8owfKOSNng20dmcenZ"
            + "k9SBHVWHMGz5ABS6I7U9tW1IZgAYd6rBjqNrGO48nOOTyUI1rVyuLDYJA+fU5z5t1dqdiJxtGv4tVYE1Cgv6oK2M1X9comnG"
            + "SIExO6C8JjEn8l0vXvx3TOeErXQgvOTyX5aPZqq08Tg8wp3mhe8VEI7LvaXC33j7XUUsTH8uN1NT2gvBCKDs/c+jCXZnvW1r"
            + "Yi+Xhpjutpuz8wUugxILz1PK0vqho0tAEgKpbWzMZknkoebF9UttnA4jNaDkVIY6urYUpAyNJz9cu6mc6eZq3LEAwTbRmCvP"
            + "udNin1lgj2MCm3gGqorqqLp6Sm8dGtPUW21/wE24o5DIVEJ88bWLqSkCpHMIjQZbU7prdxz6aDIupLJ29+3+WMJJ4lQlHZuA"
            + "UsYVJRbN/GQqPBpc0zqqbUrhFnwzu/QFQiHaoY1AvO7A/HJA71W+2/lnmdUthbnzdH5Mb0wzFh/DUIMnADpBRqCv0AhuVq1B"
            + "fpqMV+DZBBFk9yGHVFgAva5ahIQUVgi9M3InyzEvnDb6eCzVzGks3FXQHba7L8/B91ZSoJ1YZAK1VMXxCqiOfflKzw+5szsl"
            + "6NFC2kRLePaL2RagjAcieNYE12ko6VRG+libJDrXbdk2ncKiBvRYpmm2WMjWoynPa4g9d+RH3kBVWChoM2Q5nPvXTtr7381j"
            + "pzl6TaYCtcAGNP2HSZhAL5cYWRMpWyFuPdSHqBIAY0MovcFc+NwsRhWVmjWa/k+qzitsDTEiMHs5ReWUS3MdY6mUBUFmss46"
            + "4o6hC7uifFR/wNsGbAdu/oAyc6+iuIZp7EvZODlRTlVqGeiC5TLIe5DmxhPdiT5q0hAlWNz2MlzWfUBLjPUTIw96rDFAMBkG"
            + "CSqGSIb3DQEJFDEMHgoAcAByAG8AYgBlMCMGCSqGSIb3DQEJFTEWBBTKqIvap/bcvU7j6FoyHydwF/fgTjAxMCEwCQYFKw4D"
            + "AhoFAAQUBglEg/ipKLzA7QT1nHTK1vEnyNQECI3MmOhA2f8JAgIIAA==";
    // Fixed BKS v2 certificate entry, built from the AOSP BcKeyStoreSpi
    // entry format and RFC 7292 Appendix B MAC derivation (ID 3).
    static final String BKS =
            "AAAAAgAAABQAAQIDBAUGBwgJCgsMDQ4PEBESEwAABAABAAVwcm9iZQAAAAAAAAAAAAAAAAAFWC41MDkAAAImMIICIjCCAUug" +
            "AwIBAgIBEzANBgkqhkiG9w0BAQsFADAXMRUwEwYDVQQDDAxPR1BsYXkgUHJvYmUwHhcNMjAwMTAxMDAwMDAwWhcNNDAwMTAx" +
            "MDAwMDAwWjAXMRUwEwYDVQQDDAxPR1BsYXkgUHJvYmUwgd8wDQYJKoZIhvcNAQEBBQADgc0AMIHJAoHBAL7CXnQfZA6M0ymm" +
            "7OBGtdob6wNT7jpO2Aay1VRi10WqABxzyam0YhDC9NDSygLaNC3lQvpHNv1auRsyNF3HHTWETG8fM+NjAfjO9NqHU3QblfZm" +
            "VuAUOGc0HAOBaCOMNrjEuD/chsBjX6vKU/UxzIkFQKo7uwfBtG2kCeXECgvMEMwC8HxVvLc/uVAtW763DsgCCt5SQ5/NiTto" +
            "zif3kYuU3pSDkRIBNv/ETAyiCr92/11aEQc+Jma95n3OIAJklQIDAQABMA0GCSqGSIb3DQEBCwUAA4HBAJYB2gjoguO//T56" +
            "RJsvL+O8ToLnmTs3fsQ7xWNLnpDwl17t7IznN0jFbwJtOzPmCFHvEFk1bp8omCIFRbtBuMfH4G2YtI2rqXIUkMjCKzteBf97" +
            "mcY1Fz8PgOPiik0Q6ZjW1TuT9ACbXsdzax9LwbggqhNM4l/vVABP0bHkGsdglGFrRzWBvxjHmYUeHC+pOEJ20KD/1AC23S0B" +
            "ZjxNEExUjtwzSe+lw2SbRhM5Sm4AbnLvIZxD49zD0L3cQii0nQBFLBfq3lH1lhc4e8RTyb/jLZ9b/A==";
}
