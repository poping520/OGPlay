#!/usr/bin/env python3
"""Verify the vendored AOSP Dalvik reference baseline.

The baseline is a shallow submodule of platform/dalvik pinned to tag
android-4.4.4_r2 (see docs/design/dexvm/07-aosp-reference.md). It is a
reference and machine-verification data source only: it is never compiled
into the runtime. This tool asserts the pin and the anchor files used by
machine comparison so any accidental drift fails CTest.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

PINNED_COMMIT = "36e356c96640775f0a3f167bd2426ea0f0093b8b"
PINNED_TAG = "android-4.4.4_r2"

# Anchor files consumed by machine verification (07 §2 mode A) plus the
# upstream NOTICE. Hashes lock the exact bytes the generators compare against.
ANCHOR_SHA256 = {
    "opcode-gen/bytecode.txt": (
        "5aaa1939e6b11f1f1de740c8208af1448c3e1371ff7d47d1c7f7d0cc428dcdc3"
    ),
    "libdex/DexOpcodes.h": (
        "84524b4835ad0fc2a694d54f65ee86522203028d35c37f6b36a7b01cbbb7ec55"
    ),
    "libdex/InstrUtils.cpp": (
        "44baaa39a673aa0020b34dd3a07ad3b5be5fe67a9785b664b6b89c266c2e2578"
    ),
    "NOTICE": (
        "3d70cfeeeb3e0261da92808b014c744541215abcc6be41f8be54b4381ded28f3"
    ),
}


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(root: Path) -> list[str]:
    errors: list[str] = []
    if not root.is_dir():
        return [f"baseline directory missing: {root}"]

    try:
        head = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        return [f"cannot resolve baseline HEAD: {error}"]

    if head != PINNED_COMMIT:
        errors.append(
            f"baseline HEAD {head} does not match pinned {PINNED_TAG} "
            f"commit {PINNED_COMMIT}"
        )

    for relative, expected in sorted(ANCHOR_SHA256.items()):
        path = root / relative
        if not path.is_file():
            errors.append(f"anchor file missing: {relative}")
            continue
        actual = sha256_of(path)
        if actual != expected:
            errors.append(
                f"anchor file drifted: {relative} sha256 {actual} != {expected}"
            )
    return errors


def self_test() -> int:
    sample = hashlib.sha256(b"ogplay").hexdigest()
    if len(sample) != 64:
        print("self-test: sha256 helper broken", file=sys.stderr)
        return 1
    if len(PINNED_COMMIT) != 40 or set(PINNED_COMMIT) - set("0123456789abcdef"):
        print("self-test: pinned commit malformed", file=sys.stderr)
        return 1
    if len(ANCHOR_SHA256) < 4:
        print("self-test: anchor set incomplete", file=sys.stderr)
        return 1
    for digest in ANCHOR_SHA256.values():
        if len(digest) != 64:
            print("self-test: anchor digest malformed", file=sys.stderr)
            return 1
    print("self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path,
                        help="path to third_party/aosp-dalvik")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()
    if arguments.baseline is None:
        parser.error("--baseline is required unless --self-test is given")

    errors = verify(arguments.baseline)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"aosp-dalvik baseline ok: {PINNED_TAG} @ {PINNED_COMMIT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
