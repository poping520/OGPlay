#!/usr/bin/env python3
"""Build the deterministic API19 framework resource payload from the pinned ROM."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
INPUT = ".local/aosp/framework-res.apk"
MANIFEST = ROOT / "data/android/19/manifest.json"
OUTPUT = ROOT / "data/android/19/framework/framework-res.apk"
WALLPAPERS = {
    f"res/drawable-{qualifier}nodpi/default_wallpaper.jpg"
    for qualifier in ("", "sw600dp-", "sw720dp-")
}


def trim(source: bytes) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(source)) as original, zipfile.ZipFile(output, "w") as archive:
        if original.getinfo("resources.arsc").compress_type != zipfile.ZIP_STORED:
            raise ValueError("original resources.arsc must be STORED")
        if not WALLPAPERS <= set(original.namelist()):
            raise ValueError("original archive is missing a wallpaper to remove")
        for entry in original.infolist():
            if entry.filename in WALLPAPERS or entry.filename.startswith("META-INF/"):
                continue
            info = zipfile.ZipInfo(entry.filename, (1980, 1, 1, 0, 0, 0))
            info.compress_type = entry.compress_type
            info.create_system = 3
            info.external_attr = (0o40755 if entry.is_dir() else 0o100644) << 16
            if info.compress_type == zipfile.ZIP_STORED:
                start = output.tell() + 30 + len(info.filename.encode("utf-8"))
                if start % 4:
                    # zipalign's alignment extra field followed by zero padding.
                    padding = (-(start + 6)) % 4
                    info.extra = struct.pack("<HHH", 0xD935, 2 + padding, 4) + bytes(padding)
            archive.writestr(info, original.read(entry), compresslevel=9)
    return output.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=ROOT / INPUT)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    source = args.input.read_bytes()
    original_sha256 = hashlib.sha256(source).hexdigest()
    expected = json.loads(MANIFEST.read_text(encoding="utf-8"))["framework_resources"]["original_sha256"]
    if original_sha256 != expected:
        parser.error("framework-res.apk does not match the pinned original SHA-256")
    payload = trim(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(json.dumps({"original_sha256": original_sha256, "sha256": hashlib.sha256(payload).hexdigest(),
                      "size": len(payload)}))


if __name__ == "__main__":
    main()
