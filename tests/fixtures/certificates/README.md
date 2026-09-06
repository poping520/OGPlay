# Certificate fixtures

Generated locally with OpenSSL on 2026-09-06 solely for tests; these are public,
self-created test certificates, not production trust anchors. Private keys and CSRs
remain under ignored `.local/review/dvm106/` and are unnecessary at test runtime.

- `rsa.der` / `rsa.pem`: RSA 2048, SHA256withRSA, X.509 v3, serial
  `1234567890abcdef1234567890abcdef12345678`, self-signed CA with pathlen 1,
  digitalSignature/keyCertSign and two DNS subject alternative names.
- `ec.der` / `ec.pem`: P-256, SHA256withECDSA, X.509 v1, serial 42, self-signed.
- `leaf.der` / `leaf.pem`: RSA 2048, SHA256withRSA, signed by the RSA fixture CA.
- `chain.p7b`: OpenSSL `crl2pkcs7 -nocrl` DER bag containing leaf and RSA CA.
- `signatures.txt`: `<JCA algorithm> <hex signature>` from `openssl dgst` using
  both RSA and EC fixture keys with SHA1/SHA224/SHA256/SHA384/SHA512. The signed
  bytes are exactly UTF-8 `OGPlay certificate signature fixture` plus LF.

Root validity: 2026-09-06 to 2036-09-03. Tests use explicit dates for expiry checks;
results do not depend on the host clock. The runtime verifies these signatures
with the ARM guest OpenSSL; host OpenSSL is only the independent fixture oracle.
PKCS7 is a certificate set, so its encoded order is not a path-order guarantee.
