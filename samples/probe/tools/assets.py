"""Generate original, bounded fixtures for the probe APK."""

import hashlib
import math
from pathlib import Path
import struct
import wave
import zipfile
import zlib

FIXTURES = {
    "stored.bin": (zipfile.ZIP_STORED, b"OGPlay Probe/stored\x00\x13\xff"),
    "deflated.bin": (zipfile.ZIP_DEFLATED, b"OGPlay Probe/deflated\x00\x13\xff" * 64),
}


def png(path: Path, width: int, height: int, pixel) -> None:
    def chunk(kind, contents):
        return struct.pack(">I", len(contents)) + kind + contents + struct.pack(">I", zlib.crc32(kind + contents))

    pixels = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    contents = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    path.write_bytes(contents + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def generate(work: Path, resources: Path, generated: Path) -> Path:
    fixtures = work / "assets/fixtures"
    fixtures.mkdir(parents=True)
    for name, (_, content) in FIXTURES.items():
        (fixtures / name).write_bytes(content)

    drawable = resources / "drawable-nodpi"
    drawable.mkdir(parents=True, exist_ok=True)
    png(drawable / "fixture_pixels.png", 16, 16,
        lambda x, y: (8, 134, 88, 255) if x < 8 and y < 8 else (225, 82, 25, 255))
    png(fixtures / "pixels.png", 16, 16,
        lambda x, y: (8, 134, 88, 255) if x < 8 and y < 8 else (225, 82, 25, 255))

    mipmap = resources / "mipmap-nodpi"
    mipmap.mkdir(parents=True, exist_ok=True)

    def icon(x, y):
        inside = 4 <= x < 44 and 4 <= y < 44
        trace = ((7 <= x <= 15 or 31 <= x <= 40) and abs(y - 26) <= 1
                 or 16 <= x <= 23 and abs(y - (54 - 2 * x)) <= 1
                 or 24 <= x <= 30 and abs(y - (2 * x - 35)) <= 1)
        return (120, 198, 240, 255) if trace else (17, 24, 32, 255) if inside else (0, 0, 0, 0)

    png(mipmap / "probe_icon.png", 48, 48, icon)

    raw = resources / "raw"
    raw.mkdir(exist_ok=True)
    (raw / "fixture_bytes.bin").write_bytes(b"probe-19\x00\x7f\xff")
    with wave.open(str(raw / "probe_tone.wav"), "wb") as tone:
        tone.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        pcm = (int(3600 * math.sin(2 * math.pi * 400 * i / 8000)) for i in range(4000))
        tone.writeframes(struct.pack("<4000h", *pcm))

    with zipfile.ZipFile(fixtures / "nested.zip", "w") as archive:
        for name, (method, content) in sorted(FIXTURES.items()):
            archive.writestr(zipfile.ZipInfo(name), content, compress_type=method)
    constants = "".join(
        f'    static final String {name.split(".")[0].upper()}_SHA256 = "{hashlib.sha256(content).hexdigest()}";\n'
        for name, (_, content) in FIXTURES.items()
    )
    (generated / "AssetFixtures.java").write_text(
        "package org.ogplay.probe;\nfinal class AssetFixtures {\n" + constants + "}\n", encoding="utf-8")
    return fixtures
