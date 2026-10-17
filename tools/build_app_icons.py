#!/usr/bin/env python3
"""Generate desktop icons from platform PNG sources (requires Pillow)."""

from io import BytesIO
from pathlib import Path
import struct

from PIL import Image


ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


def encode_icon_frame(image: Image.Image, size: int) -> bytes:
    frame = image.resize((size, size), Image.Resampling.LANCZOS)
    output = BytesIO()
    if size == 256:
        frame.save(output, format="PNG")
        return output.getvalue()

    # Classic shell consumers expect a bottom-up BGRA DIB and a DWORD-aligned
    # transparency mask. Only the 256px image uses PNG compression.
    frame.save(output, format="DIB")
    dib = bytearray(output.getvalue())
    struct.pack_into("<i", dib, 8, size * 2)  # ICO height includes XOR + AND.
    mask = frame.getchannel("A").point(lambda alpha: 255 if alpha == 0 else 0, "1")
    stride = ((size + 31) // 32) * 4
    return bytes(dib) + mask.tobytes("raw", "1", stride, -1)


def save_windows_icon(image: Image.Image, path: Path) -> None:
    directory = bytearray(struct.pack("<HHH", 0, 1, len(ICON_SIZES)))
    payload = bytearray()
    offset = 6 + 16 * len(ICON_SIZES)
    for size in ICON_SIZES:
        data = encode_icon_frame(image, size)
        extent = size if size < 256 else 0
        directory.extend(struct.pack("<BBBBHHII", extent, extent, 0, 0, 1, 32,
                                     len(data), offset + len(payload)))
        payload.extend(data)
    path.write_bytes(directory + payload)


def save_macos_icon(image: Image.Image, path: Path) -> None:
    # The PNG already contains the final artwork, padding, and transparency.
    image.save(path)


def main() -> None:
    root = Path(__file__).resolve().parents[1] / "assets" / "icons"
    with Image.open(root / "ogplay.png") as source:
        if source.width != source.height:
            raise ValueError("Application icon must be square")
        image = source.convert("RGBA")
    save_windows_icon(image, root / "ogplay.ico")
    with Image.open(root / "ogplay-macos.png") as source:
        if source.width != source.height:
            raise ValueError("macOS application icon must be square")
        save_macos_icon(source.convert("RGBA"), root / "ogplay.icns")


if __name__ == "__main__":
    main()
