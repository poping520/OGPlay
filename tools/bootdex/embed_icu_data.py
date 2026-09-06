#!/usr/bin/env python3
"""Embed the verified ICU51 payload; no host ICU data lookup is permitted."""
import hashlib
import struct
import sys
from pathlib import Path


def main():
    data = Path(sys.argv[1]).read_bytes()
    if hashlib.sha256(data).hexdigest() != "8275408cb7161606c9a1b55edf12df538a7110ad53103a00f8ac7ba5b092a96f":
        raise ValueError("unexpected pinned ICU51 data")
    output = Path(sys.argv[2])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as stream:
        stream.write('#include <bit>\n#include <cstdint>\n'
                     'static_assert(std::endian::native == std::endian::little);\n'
                     'namespace ogplay::runtime::dexvm {\n'
                     'alignas(16) extern const std::uint64_t kPinnedIcuData[] = {\n')
        for offset in range(0, len(data), 64):
            chunk = data[offset:offset + 64]
            chunk += b"\0" * (-len(chunk) % 8)
            stream.write(",".join(f"0x{word[0]:016x}ULL" for word in struct.iter_unpack("<Q", chunk)) + ",\n")
        stream.write('};\n}\n')


if __name__ == "__main__":
    main()
