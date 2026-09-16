#!/usr/bin/env python3
"""Generate DVM-173 test CA pack, path-validation matrix and loopback TLS material.

Certificates are valid 2010-01-01 .. 2036-12-31 so both the AndroidAppProcess
clock (~2014) and Dvm87Vm clock (2024-01-01) accept the positive fixtures.
Negative fixtures use dates that fail at both clocks. Test-only; not a public CA.
"""

from __future__ import annotations

import hashlib
import ipaddress
import struct
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

ROOT = Path(__file__).resolve().parent
UTC = timezone.utc
NOT_BEFORE = datetime(2010, 1, 1, tzinfo=UTC)
NOT_AFTER = datetime(2036, 12, 31, tzinfo=UTC)
CREATED = int(datetime(2024, 1, 1, tzinfo=UTC).timestamp() * 1000)
MAGIC = b"OGPLAYCA"


def name(common: str, org: str = "OGPlay Test") -> x509.Name:
    return x509.Name(
        [
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, org),
            x509.NameAttribute(NameOID.COMMON_NAME, common),
        ]
    )


def rsa_key(bits: int = 2048):
    return rsa.generate_private_key(65537, bits)


def ec_key():
    return ec.generate_private_key(ec.SECP256R1())


def pem_key(key) -> bytes:
    return key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption(),
    )


def pkcs8_der(key) -> bytes:
    return key.private_bytes(
        serialization.Encoding.DER,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )


def pem_cert(cert: x509.Certificate) -> bytes:
    return cert.public_bytes(serialization.Encoding.PEM)


def der_cert(cert: x509.Certificate) -> bytes:
    return cert.public_bytes(serialization.Encoding.DER)


def subject_hash_old(cert: x509.Certificate) -> int:
    digest = hashlib.md5(cert.subject.public_bytes()).digest()
    return int.from_bytes(digest[:4], "little")


def write(path: Path, data: bytes) -> None:
    path.write_bytes(data)


def builder(subject: x509.Name, issuer: x509.Name, public_key, serial: int,
            not_before=NOT_BEFORE, not_after=NOT_AFTER) -> x509.CertificateBuilder:
    return (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(public_key)
        .serial_number(serial)
        .not_valid_before(not_before)
        .not_valid_after(not_after)
    )


def ca_cert(key, subject: x509.Name, serial: int, path_length=None,
            name_constraints=None) -> x509.Certificate:
    cert = builder(subject, subject, key.public_key(), serial)
    cert = cert.add_extension(x509.BasicConstraints(True, path_length), critical=True)
    cert = cert.add_extension(
        x509.KeyUsage(
            digital_signature=True, content_commitment=False, key_encipherment=False,
            data_encipherment=False, key_agreement=False, key_cert_sign=True,
            crl_sign=True, encipher_only=False, decipher_only=False),
        critical=True,
    )
    cert = cert.add_extension(
        x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
    if name_constraints is not None:
        cert = cert.add_extension(name_constraints, critical=True)
    return cert.sign(key, hashes.SHA256())


def leaf_cert(ca_key, ca, key, subject: x509.Name, serial: int, *,
              server=True, client=False, dns=(), ip=(), eku=None,
              not_before=NOT_BEFORE, not_after=NOT_AFTER, critical_unknown=False,
              basic_ca=False, path_length=None, signer=None) -> x509.Certificate:
    issuer = ca.subject
    cert = builder(subject, issuer, key.public_key(), serial, not_before, not_after)
    cert = cert.add_extension(x509.BasicConstraints(basic_ca, path_length), critical=True)
    cert = cert.add_extension(
        x509.KeyUsage(
            digital_signature=True, content_commitment=False, key_encipherment=True,
            data_encipherment=False, key_agreement=False, key_cert_sign=basic_ca,
            crl_sign=False, encipher_only=False, decipher_only=False),
        critical=True,
    )
    usages = eku
    if usages is None:
        usages = []
        if server:
            usages.append(ExtendedKeyUsageOID.SERVER_AUTH)
        if client:
            usages.append(ExtendedKeyUsageOID.CLIENT_AUTH)
    if usages:
        cert = cert.add_extension(x509.ExtendedKeyUsage(usages), critical=False)
    names = [x509.DNSName(item) for item in dns]
    names.extend(x509.IPAddress(ipaddress.ip_address(item)) for item in ip)
    if names:
        cert = cert.add_extension(x509.SubjectAlternativeName(names), critical=False)
    cert = cert.add_extension(
        x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()),
        critical=False,
    )
    if critical_unknown:
        cert = cert.add_extension(
            x509.UnrecognizedExtension(x509.ObjectIdentifier("1.3.6.1.4.1.11129.999.1"), b"\x04\x01\x01"),
            critical=True,
        )
    return cert.sign(signer or ca_key, hashes.SHA256())


def utf16(value: str) -> bytes:
    encoded = value.encode("utf-16-be")
    return struct.pack(">H", len(value)) + encoded


def encode_bundle(aliases: list[str], dates: list[int], ders: list[bytes],
                  source: str, license_text: str) -> bytes:
    payload = bytearray(MAGIC)
    payload += struct.pack(">i", 1)
    payload += struct.pack(">q", CREATED)
    payload += utf16(source)
    payload += utf16(license_text)
    payload += struct.pack(">i", len(aliases))
    for alias, date, der in zip(aliases, dates, ders):
        payload += utf16(alias)
        payload += struct.pack(">q", date)
        payload += struct.pack(">i", len(der))
        payload += der
    digest = hashlib.sha256(payload).digest()
    return bytes(payload) + digest


def main() -> int:
    ca_key = rsa_key()
    other_ca_key = rsa_key()
    server_key = rsa_key()
    client_key = rsa_key()
    ec_server_key = ec_key()
    untrusted_key = rsa_key()
    expired_key = rsa_key()
    notyet_key = rsa_key()
    non_ca_key = rsa_key()
    pathlen_mid_key = rsa_key()
    pathlen_leaf_key = rsa_key()
    wrong_eku_key = rsa_key()
    nc_leaf_key = rsa_key()
    unknown_key = rsa_key()
    same_subject_key = rsa_key()
    trusted_leaf_key = rsa_key()
    cross_mid_key = rsa_key()
    cross_leaf_key = rsa_key()

    ca = ca_cert(ca_key, name("OGPlay Test Root"), 1)
    other_ca = ca_cert(other_ca_key, name("OGPlay Other Root"), 2)
    pathlen_ca = ca_cert(ca_key, name("OGPlay PathLen Root"), 3, path_length=0)
    nc_ca = ca_cert(
        ca_key, name("OGPlay Constrained Root"), 4,
        name_constraints=x509.NameConstraints(
            permitted_subtrees=[x509.DNSName(".example.com")],
            excluded_subtrees=None,
        ),
    )
    same_subject_ca = ca_cert(same_subject_key, name("OGPlay Test Root", "OGPlay Other"), 5)

    server = leaf_cert(
        ca_key, ca, server_key, name("tls.test"), 10,
        dns=("tls.test", "localhost"), ip=("127.0.0.1",),
    )
    client = leaf_cert(
        ca_key, ca, client_key, name("client.test"), 11,
        server=False, client=True,
    )
    ec_server = leaf_cert(
        ca_key, ca, ec_server_key, name("ec.tls.test"), 12,
        dns=("ec.tls.test",),
    )
    untrusted = leaf_cert(
        other_ca_key, other_ca, untrusted_key, name("untrusted.test"), 13,
        dns=("untrusted.test",),
    )
    expired = leaf_cert(
        ca_key, ca, expired_key, name("expired.test"), 14,
        dns=("expired.test",),
        not_before=datetime(2010, 1, 1, tzinfo=UTC),
        not_after=datetime(2011, 1, 1, tzinfo=UTC),
    )
    notyet = leaf_cert(
        ca_key, ca, notyet_key, name("notyet.test"), 15,
        dns=("notyet.test",),
        not_before=datetime(2030, 1, 1, tzinfo=UTC),
        not_after=datetime(2036, 12, 31, tzinfo=UTC),
    )
    non_ca = leaf_cert(
        ca_key, ca, non_ca_key, name("nonca.test"), 16, basic_ca=False,
    )
    non_ca_leaf = leaf_cert(
        non_ca_key, non_ca, rsa_key(), name("nonca-leaf.test"), 17,
        dns=("nonca-leaf.test",), signer=non_ca_key,
    )
    pathlen_mid = leaf_cert(
        ca_key, pathlen_ca, pathlen_mid_key, name("pathlen-mid.test"), 18,
        basic_ca=True, path_length=0, server=False,
    )
    pathlen_bridge_key = rsa_key()
    pathlen_bridge = leaf_cert(
        pathlen_mid_key, pathlen_mid, pathlen_bridge_key, name("pathlen-bridge.test"), 27,
        basic_ca=True, server=False,
    )
    pathlen_leaf = leaf_cert(
        pathlen_bridge_key, pathlen_bridge, pathlen_leaf_key, name("pathlen-leaf.test"), 19,
        dns=("pathlen-leaf.test",),
    )
    wrong_eku = leaf_cert(
        ca_key, ca, wrong_eku_key, name("clientauth.test"), 20,
        server=False, client=True, eku=[ExtendedKeyUsageOID.CLIENT_AUTH],
        dns=("clientauth.test",),
    )
    nc_leaf = leaf_cert(
        ca_key, nc_ca, nc_leaf_key, name("evil.com"), 21,
        dns=("evil.com",),
    )
    unknown = leaf_cert(
        ca_key, ca, unknown_key, name("unknown.test"), 22,
        dns=("unknown.test",), critical_unknown=True,
    )
    trusted_leaf = leaf_cert(
        ca_key, ca, trusted_leaf_key, name("trusted-leaf.test"), 23,
        dns=("trusted-leaf.test",),
    )
    cross_mid = leaf_cert(
        other_ca_key, other_ca, cross_mid_key, name("cross-mid.test"), 24,
        basic_ca=True, server=False,
    )
    # Cross-sign the intermediate with the test root as well.
    cross_mid_alt = leaf_cert(
        ca_key, ca, cross_mid_key, name("cross-mid.test"), 25,
        basic_ca=True, server=False,
    )
    cross_leaf = leaf_cert(
        cross_mid_key, cross_mid_alt, cross_leaf_key, name("cross-leaf.test"), 26,
        dns=("cross-leaf.test",),
    )

    files = {
        "ca.pem": pem_cert(ca) + pem_key(ca_key),
        "ca.crt": pem_cert(ca),
        "ca.key": pem_key(ca_key),
        "ca.der": der_cert(ca),
        "other-ca.der": der_cert(other_ca),
        "server.crt": pem_cert(server),
        "server.key": pem_key(server_key),
        "server.der": der_cert(server),
        "client.crt": pem_cert(client),
        "client.key": client_key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ),
        "client.pkcs8": pkcs8_der(client_key),
        "client.der": der_cert(client),
        "ec-server.der": der_cert(ec_server),
        "untrusted.der": der_cert(untrusted),
        "expired.der": der_cert(expired),
        "notyet.der": der_cert(notyet),
        "non-ca.der": der_cert(non_ca),
        "non-ca-leaf.der": der_cert(non_ca_leaf),
        "pathlen-ca.der": der_cert(pathlen_ca),
        "pathlen-mid.der": der_cert(pathlen_mid),
        "pathlen-bridge.der": der_cert(pathlen_bridge),
        "pathlen-leaf.der": der_cert(pathlen_leaf),
        "wrong-eku.der": der_cert(wrong_eku),
        "nc-ca.der": der_cert(nc_ca),
        "nc-leaf.der": der_cert(nc_leaf),
        "unknown-critical.der": der_cert(unknown),
        "trusted-leaf.der": der_cert(trusted_leaf),
        "same-subject-ca.der": der_cert(same_subject_ca),
        "cross-mid.der": der_cert(cross_mid_alt),
        "cross-leaf.der": der_cert(cross_leaf),
        "corrupt.der": b"\x30\x82not-a-certificate",
    }
    for filename, data in files.items():
        write(ROOT / filename, data)

    alias = f"system:{subject_hash_old(ca):08x}"
    pack = encode_bundle(
        [alias],
        [CREATED],
        [der_cert(ca)],
        "OGPlay DVM-173 test CA",
        "Test-only fixture; not a public trust list.",
    )
    write(ROOT / "cacerts.ogplay", pack)
    empty = encode_bundle(
        [], [], [], "OGPlay empty test CA", "Test-only empty pack.")
    write(ROOT / "cacerts-empty.ogplay", empty)
    print(f"wrote {len(files) + 2} TLS fixtures; CA alias {alias}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
