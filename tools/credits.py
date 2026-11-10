#!/usr/bin/env python3
"""Generate data/credits.json: the open-source acknowledgements shown on the launcher's About page.

PROJECTS lists the third-party code OGPlay release builds ship (native libraries, the
Web UI and the API 19 guest runtime). License texts are copied verbatim from the source tree into the
one JSON file; the guest runtime's NOTICE files already ship under data/android/19/notices and are
referenced in place. PLATFORMS optionally restricts a project to Windows, Darwin or Linux; omitted
projects apply everywhere. All platforms ship this same catalog; the native About service filters it.
Run after adding, removing or updating a dependency:

    python3 tools/credits.py           # rewrite data/credits.json
    python3 tools/credits.py --check   # fail when data/credits.json is stale
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
OUT = DATA / "credits.json"
NOTICES = "android/19/notices"


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8").strip() + "\n"


def npm_version(package: str) -> str:
    return json.loads((ROOT / "webui/node_modules" / package / "package.json").read_text(encoding="utf-8"))["version"]


def stb_license() -> str:
    # stb ships its dual MIT / public-domain license at the end of each header.
    source = text("third_party/stb/stb_image_write.h")
    start = source.index("This software is available under 2 licenses")
    end = source.index("*/", start)
    return source[start:end].rstrip() + "\n"


def sqlite_blessing() -> str:
    # SQLite is public domain; its files carry this blessing in place of a license.
    source = text("third_party/sqlite-amalgamation/sqlite3.c")
    start = source.index("The author disclaims copyright")
    end = source.index("\n", source.index("May you share freely", start))
    lines = [re.sub(r"^\*\*\s?", "", line) for line in source[start:end].splitlines()]
    return "\n".join(lines).strip() + "\n"


def angle_licenses() -> list[tuple[str, str]]:
    base = "third_party/angle-prebuilt/licenses"
    result = [("ANGLE", text(f"{base}/ANGLE.txt"))]
    for path in sorted((ROOT / base / "third-party").glob("*.txt")):
        result.append((path.stem, text(str(path.relative_to(ROOT)))))
    return result


def skia_licenses() -> list[tuple[str, str]]:
    base = ROOT / "third_party/skia-prebuilt/licenses"
    manifest = json.loads((base / "manifest.json").read_text(encoding="utf-8"))
    return [(item["path"], text(str((base / item["path"]).relative_to(ROOT))))
            for item in manifest["files"]]


def dynarmic_external(name: str, license_file: str = "LICENSE") -> list[tuple[str, str]]:
    return [(name, text(f"third_party/dynarmic/externals/{name}/{license_file}"))]


# id, name, version, url, SPDX license (None: several, see the texts), texts. A text is either
# (title, content) stored inline or a path under data/ to a notice that already ships.
PROJECTS = [
    ("aosp", "Android Open Source Project", "4.4.4_r2.0.1", "https://source.android.com", None,
     [f"{NOTICES}/{name}.txt" for name in
      ("boot-classpath", "libc.so", "libm.so", "libdl.so", "libstdc++.so", "libgabi++.so")]),
    ("openssl", "OpenSSL", "", "https://www.openssl.org", "OpenSSL", [f"{NOTICES}/libcrypto.so.txt"]),
    ("stlport", "STLport", "", "https://sourceforge.net/projects/stlport", None, [f"{NOTICES}/libstlport.so.txt"]),
    ("zlib", "zlib", "", "https://zlib.net", "Zlib", [f"{NOTICES}/libz.so.txt"]),
    ("icu", "ICU", "", "https://icu.unicode.org", "ICU",
     [f"{NOTICES}/icu4c-license.html", f"{NOTICES}/icu4c-unicode-license.txt"]),
    ("fonts", "Roboto · Droid Sans · Droid Serif", "", "https://source.android.com", "Apache-2.0",
     [f"{NOTICES}/fonts.txt"]),
    ("tzdata", "tz database", "2014a", "https://www.iana.org/time-zones", "Public Domain",
     [f"{NOTICES}/tzdata.txt"]),
    ("sdl", "SDL", "3.4.10", "https://github.com/libsdl-org/SDL", "Zlib",
     [("SDL", text("third_party/SDL/LICENSE.txt"))]),
    ("angle", "ANGLE", "c24d9971", "https://chromium.googlesource.com/angle/angle", "BSD-3-Clause",
     angle_licenses()),
    ("skia", "Skia", "chrome/m157", "https://skia.googlesource.com/skia", None, skia_licenses()),
    ("dynarmic", "Dynarmic", "6.7.0", "https://git.eden-emu.dev/eden-emu/dynarmic", "0BSD",
     [("Dynarmic", text("third_party/dynarmic/LICENSE.txt"))]),
    ("fmt", "{fmt}", "11.1.4", "https://github.com/fmtlib/fmt", "MIT", dynarmic_external("fmt")),
    ("mcl", "mcl", "0.1.13", "https://github.com/azahar-emu/mcl", "MIT", dynarmic_external("mcl")),
    ("unordered_dense", "unordered_dense", "4.5.0", "https://github.com/martinus/unordered_dense", "MIT",
     dynarmic_external("unordered_dense")),
    ("oaknut", "oaknut", "2.0.2", "https://github.com/merryhime/oaknut", "MIT", dynarmic_external("oaknut")),
    ("xbyak", "Xbyak", "7.22", "https://github.com/herumi/xbyak", "BSD-3-Clause",
     dynarmic_external("xbyak", "COPYRIGHT")),
    ("zydis", "Zydis", "4.0.0", "https://github.com/zyantific/zydis", "MIT", dynarmic_external("zydis")),
    ("zycore", "Zycore", "1.5.2", "https://github.com/zyantific/zycore-c", "MIT", dynarmic_external("zycore")),
    ("boost", "Boost", "1.90", "https://www.boost.org", "BSL-1.0",
     [("Boost", text("third_party/ext-boost/LICENSE_1_0.txt"))]),
    ("ffmpeg", "FFmpeg", "8.1.3", "https://ffmpeg.org", "LGPL-2.1-or-later",
     [("FFmpeg", text("third_party/ffmpeg-prebuilt/licenses/FFmpeg-LICENSE.md")),
      ("GNU LGPL 2.1", text("third_party/ffmpeg-prebuilt/licenses/FFmpeg-COPYING.LGPLv2.1"))]),
    ("sqlite", "SQLite", "3.53.4", "https://sqlite.org", "Public Domain", [("SQLite", sqlite_blessing())]),
    ("yyjson", "yyjson", "0.12.0", "https://github.com/ibireme/yyjson", "MIT",
     [("yyjson", text("third_party/yyjson/LICENSE"))]),
    ("stb", "stb", "", "https://github.com/nothings/stb", "MIT OR Unlicense", [("stb", stb_license())]),
    ("minimp3", "minimp3", "", "https://github.com/lieff/minimp3", "CC0-1.0",
     [("minimp3", text("third_party/minimp3/LICENSE"))]),
    ("compressonator", "Compressonator", "", "https://github.com/GPUOpen-Tools/compressonator", "MIT",
     [("Compressonator", text("third_party/compressonator/LICENSE.txt"))]),
    ("powervr", "PowerVR SDK", "2b1bf2f1", "https://github.com/powervr-graphics/Native_SDK", "MIT",
     [("PowerVR SDK", text("third_party/powervr/LICENSE.md"))]),
    ("webview", "webview", "", "https://github.com/webview/webview", "MIT",
     [("webview", text("third_party/webview/LICENSE"))]),
    ("preact", "Preact", npm_version("preact"), "https://preactjs.com", "MIT",
     [("Preact", text("webui/node_modules/preact/LICENSE"))]),
    ("three", "three.js", npm_version("three"), "https://threejs.org", "MIT",
     [("three.js", text("webui/node_modules/three/LICENSE"))]),
    ("draco", "Draco", "", "https://github.com/google/draco", "Apache-2.0",
     [("Draco", "Copyright Google LLC.\n\n" + text("webui/node_modules/typescript/LICENSE.txt"))]),
    ("uplot", "uPlot", npm_version("uplot"), "https://github.com/leeoniya/uPlot", "MIT",
     [("uPlot", text("webui/node_modules/uplot/LICENSE"))]),
    ("sn-pro", "SN Pro", "e025ac47", "https://github.com/supernotes/sn-pro", "OFL-1.1",
     [("SIL Open Font License 1.1", text("webui/apps/gui/assets/fonts/OFL.txt"))]),

]

# Optional host restrictions, using CMAKE_SYSTEM_NAME. Unlisted projects apply to every platform.
PLATFORMS = {"sn-pro": ["Windows"]}

# Historical repository of a project whose current source is a fork or continuation.
ORIGINAL_URLS = {"dynarmic": "https://github.com/merryhime/dynarmic"}

# Projects OGPlay only gets through another listed project, nested under it on the About page. Whatever src/ or
# webui/ uses itself stays top level, even when a listed project needs it too (Boost, for Dynarmic).
PARENTS = {
    "fmt": "dynarmic", "mcl": "dynarmic", "unordered_dense": "dynarmic", "oaknut": "dynarmic", "xbyak": "dynarmic",
    "zydis": "dynarmic", "zycore": "zydis",
    "draco": "three",
    "openssl": "aosp", "stlport": "aosp", "zlib": "aosp", "icu": "aosp", "fonts": "aosp", "tzdata": "aosp",
}


def render() -> str:
    projects = []
    seen = set()
    for project_id, name, version, url, license_id, texts in PROJECTS:
        notices = []
        for item in texts:
            if isinstance(item, str):
                if not (DATA / item).is_file():
                    raise SystemExit(f"{project_id}: shipped notice {item} is missing")
                notices.append({"title": Path(item).name, "path": item})
            else:
                notices.append({"title": item[0], "text": item[1]})
        parent = PARENTS.get(project_id)
        if parent is not None and parent not in seen:
            raise SystemExit(f"{project_id}: parent {parent} must be listed before it")
        seen.add(project_id)
        projects.append({"id": project_id, "name": name, "version": version, "url": url, "license": license_id,
                         **({"parent": parent} if parent else {}),
                         **({"original_url": ORIGINAL_URLS[project_id]} if project_id in ORIGINAL_URLS else {}),
                         **({"platforms": PLATFORMS[project_id]} if project_id in PLATFORMS else {}), "notices": notices})
    return json.dumps({"schema": 1, "projects": projects}, ensure_ascii=False, indent=1) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="report a stale data/credits.json instead of writing")
    args = parser.parse_args()
    content = render()
    if args.check:
        if not OUT.is_file() or OUT.read_text(encoding="utf-8") != content:
            print("data/credits.json is stale; run python3 tools/credits.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(content, encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(content.encode())} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
