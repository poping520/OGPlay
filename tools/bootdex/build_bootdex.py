#!/usr/bin/env python3
"""Build the curated API 19 bootdex.jar from pinned AOSP jars."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dex_survey_lib

RECIPE = ROOT / "tools/bootdex/api19.json"
AOSP = ROOT / ".local/aosp"
SMALI = ROOT / ".local/tools"
OUTPUT = ROOT / "data/android/19/framework/bootdex.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/bootdex.jar.txt"

SOURCES = {
    "core.jar": (
        "platform/libcore",
        "996557954e45f7192b187b2394259bc8aca6e3946652e03b99d837432f83d1ef"),
    "framework.jar": (
        "platform/frameworks/base",
        "90fc9fd36b45db1810ecf51e40119222da081b552f9a64a6d0c09cac09f98a48"),
}
TOOLS = {
    "baksmali.jar": (
        "https://github.com/baksmali/smali/releases/download/3.0.10/"
        "baksmali-3.0.10-fat-release.jar",
        "37ae4a41a8886e15c20b8362fa4250f96bbdb55e1a608199ad8b5dff068b588f"),
    "smali.jar": (
        "https://github.com/baksmali/smali/releases/download/3.0.10/"
        "smali-3.0.10-fat-release.jar",
        "32fa0e88a6c397b3922201adf5f3e534fbaed5a663c71d0c558c3ddce0af844a"),
}
JAR_MANIFEST = (
    b"Manifest-Version: 1.0\r\n"
    b"Created-By: OGPlay BootDex Builder\r\n\r\n"
)


class BuildError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    return sha256(path.read_bytes())


def ensure_tool(path: Path, url: str, digest: str) -> None:
    if path.is_file():
        if file_sha256(path) != digest:
            raise BuildError(f"unexpected tool: {path}")
        return
    print(f"Downloading {path.name} ...")
    with urllib.request.urlopen(url) as response:
        data = response.read()
    if sha256(data) != digest:
        raise BuildError(f"downloaded tool has unexpected SHA-256: {path.name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def load_recipe() -> dict[str, tuple[str, ...]]:
    document = json.loads(RECIPE.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != {"api_level", "classes"}:
        raise BuildError("recipe must contain only api_level and classes")
    if document["api_level"] != 19 or not isinstance(document["classes"], dict):
        raise BuildError("recipe must describe API 19 classes")
    result = {}
    all_classes = []
    for source, classes in document["classes"].items():
        if source not in SOURCES or not isinstance(classes, list) or not classes:
            raise BuildError(f"invalid source class list: {source}")
        values = tuple(classes)
        if values != tuple(sorted(set(values))) or any(
                not isinstance(item, str) or not item.startswith("L") or
                not item.endswith(";") for item in values):
            raise BuildError(f"invalid class list: {source}")
        result[source] = values
        all_classes.extend(values)
    if not result or len(all_classes) != len(set(all_classes)):
        raise BuildError("recipe has no classes or contains duplicates")
    return result


def verify_inputs(recipe: dict[str, tuple[str, ...]]) -> None:
    expected = {AOSP / source: SOURCES[source][1] for source in recipe}
    for path, digest in expected.items():
        if not path.is_file() or file_sha256(path) != digest:
            raise BuildError(f"missing or unexpected input: {path}")
    for name, (url, digest) in TOOLS.items():
        ensure_tool(SMALI / name, url, digest)


def run(command: list[str]) -> None:
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", check=False)
    if result.returncode:
        raise BuildError(result.stdout)


def assemble(recipe: dict[str, tuple[str, ...]], work: Path) -> bytes:
    smali_roots = []
    for source, classes in recipe.items():
        destination = work / source[:-4]
        run([
            "java", "-jar", str(SMALI / "baksmali.jar"), "disassemble",
            "--api", "19", "--jobs", "1", "--classes", ",".join(classes),
            "--output", str(destination), str(AOSP / source),
        ])
        smali_roots.append(str(destination))
    output = work / "classes.dex"
    run([
        "java", "-jar", str(SMALI / "smali.jar"), "assemble",
        "--api", "19", "--jobs", "1", "--output", str(output), *smali_roots,
    ])
    return output.read_bytes()


def class_names(dex: bytes) -> tuple[str, ...]:
    parsed = dex_survey_lib.parse_dex(dex)
    return tuple(sorted(parsed.type_name(item.type_index)
                        for item in parsed.classes))


def make_jar(dex: bytes) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, content in (
                ("META-INF/MANIFEST.MF", JAR_MANIFEST),
                ("classes.dex", dex)):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    return output.getvalue()


def build() -> tuple[bytes, bytes, dict[str, tuple[str, ...]]]:
    recipe = load_recipe()
    verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix="ogplay-bootdex-a-") as first, \
            tempfile.TemporaryDirectory(prefix="ogplay-bootdex-b-") as second:
        dex = assemble(recipe, Path(first))
        if dex != assemble(recipe, Path(second)):
            raise BuildError("two BootDex assemblies differ")
    selected = tuple(sorted(item for values in recipe.values() for item in values))
    if class_names(dex) != selected:
        raise BuildError("generated classes differ from recipe")
    return make_jar(dex), dex, recipe


def boot_metadata(jar: bytes, dex: bytes,
                  recipe: dict[str, tuple[str, ...]]) -> dict:
    return {
        "path": "framework/bootdex.jar",
        "generator": "tools/bootdex/build_bootdex.py",
        "recipe": "tools/bootdex/api19.json",
        "recipe_sha256": file_sha256(RECIPE),
        "sources": [
            {
                "source_project": SOURCES[source][0],
                "source_jar": source,
                "source_jar_sha256": SOURCES[source][1],
            }
            for source in recipe
        ],
        "size": len(jar),
        "sha256": sha256(jar),
        "dex_entry": "classes.dex",
        "dex_size": len(dex),
        "dex_sha256": sha256(dex),
        "class_count": len(class_names(dex)),
        "selection_policy": "load-all",
        "notice": "notices/bootdex.jar.txt",
        "notice_sha256": file_sha256(NOTICE),
    }


def manifest_bytes(metadata: dict) -> bytes:
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    document["boot_dex"] = metadata
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode()


def self_test() -> int:
    if sum(map(len, load_recipe().values())) != 11:
        raise BuildError("API 19 recipe must currently contain 11 classes")
    sample = b"dex\n035\0sample"
    if make_jar(sample) != make_jar(sample):
        raise BuildError("JAR output is not deterministic")
    with tempfile.TemporaryDirectory(prefix="ogplay-bootdex-tool-") as work:
        source = Path(work) / "source.jar"
        destination = Path(work) / "downloaded.jar"
        source.write_bytes(sample)
        ensure_tool(destination, source.as_uri(), sha256(sample))
        if destination.read_bytes() != sample:
            raise BuildError("tool download failed")
    print("BootDex builder self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("build", "check"))
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    try:
        if arguments.self_test:
            return self_test()
        if arguments.mode is None:
            parser.error("mode is required")
        jar, dex, recipe = build()
        manifest = manifest_bytes(boot_metadata(jar, dex, recipe))
        if arguments.mode == "build":
            OUTPUT.parent.mkdir(parents=True, exist_ok=True)
            OUTPUT.write_bytes(jar)
            MANIFEST.write_bytes(manifest)
        elif not OUTPUT.is_file() or OUTPUT.read_bytes() != jar or \
                MANIFEST.read_bytes() != manifest:
            raise BuildError("BootDex payload is stale; run build")
        print(f"BootDex {arguments.mode}: {len(class_names(dex))} classes, "
              f"DEX {sha256(dex)}, JAR {sha256(jar)}")
        return 0
    except (BuildError, OSError, json.JSONDecodeError,
            dex_survey_lib.DexFormatError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
