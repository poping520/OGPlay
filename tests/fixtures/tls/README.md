# DVM-173 TLS test fixtures

Test-only CA material. Not a public trust list and not used as a production
AndroidCAStore snapshot.

`generate.py` writes an `OGPLAYCA` v1 pack (`cacerts.ogplay`), loopback server
and client certificates valid 2010-01-01 through 2036-12-31, and a path
validation matrix covering untrusted, expired, not-yet-valid, non-CA, pathLen
(with `pathlen-bridge.der` as the extra CA that exceeds a pathLen=0 intermediate),
EKU, name constraints, unknown critical extensions, cross-signed and
same-subject different-key cases.

`loopback_https.py` is an independent Python TLS 1.2 HTTP oracle. It is a test
dependency only and is not part of the production payload.
