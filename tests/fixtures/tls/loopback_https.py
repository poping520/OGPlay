#!/usr/bin/env python3
"""Independent TLS 1.2 HTTP oracle for DVM-173 loopback tests."""

from __future__ import annotations

import argparse
import socket
import ssl
import sys
import threading


def handle(conn: ssl.SSLSocket) -> None:
    try:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                return
            data += chunk
            if len(data) > 65536:
                return
        body = b"tls-ok"
        response = (
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: text/plain\r\n"
            b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
            b"Connection: close\r\n"
            b"\r\n" + body
        )
        conn.sendall(response)
    except OSError:
        return
    finally:
        try:
            conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--client-ca")
    args = parser.parse_args()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.set_ciphers(
        "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-SHA256:AES128-SHA256:AES128-SHA")
    context.load_cert_chain(args.cert, args.key)
    def require_sni(sock, server_name, ctx):
        del sock, ctx
        if not server_name:
            return ssl.ALERT_DESCRIPTION_HANDSHAKE_FAILURE
        return None
    context.set_servername_callback(require_sni)
    if args.client_ca:
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(args.client_ca)
    raw = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    raw.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    raw.bind(("127.0.0.1", 0))
    raw.listen(16)
    port = raw.getsockname()[1]
    print(f"PORT={port}", flush=True)
    try:
        while True:
            conn, _ = raw.accept()
            try:
                tls = context.wrap_socket(conn, server_side=True)
            except ssl.SSLError:
                conn.close()
                continue
            threading.Thread(target=handle, args=(tls,), daemon=True).start()
    except KeyboardInterrupt:
        return 0
    finally:
        raw.close()


if __name__ == "__main__":
    sys.exit(main())
