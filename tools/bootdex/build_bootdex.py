#!/usr/bin/env python3
"""Build, check and audit curated API 19 BootDex from pinned AOSP inputs."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import dex_survey_lib

RECIPE = ROOT / "tools/bootdex/api19.json"
AOSP = ROOT / ".local/aosp"
SMALI = ROOT / ".local/tools"
OUTPUT = ROOT / "data/android/19/framework/bootdex.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/bootdex.jar.txt"
AUDIT_REPORT = ROOT / ".local/dvm102-date-family-audit.json"
CATEGORIES = ("boot_dex", "existing_vm_intrinsic", "native_boundary", "deferred")
NATIVE_DISPOSITIONS = ("required_backend", "explicit_failure")


def source_path(source: str) -> Path:
    return AOSP / source

SOURCES = {
    "conscrypt.jar": (
        "platform/libcore",
        "c7d0849cde38cd0d0dc99d89ee61bd33a65618567fc18171d6605dc6b8e15004"),
    "core.jar": (
        "platform/libcore",
        "60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee"),
    "framework.jar": (
        "platform/frameworks/base",
        "422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b"),
    "framework2.jar": (
        "platform/frameworks/base",
        "5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973"),
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


def load_recipe(document: dict | None = None) -> dict[str, tuple[str, ...]]:
    if document is None:
        document = json.loads(RECIPE.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or set(document) != {
            "api_level", "classes", "date_family_audit"}:
        raise BuildError("recipe must contain api_level, classes and date_family_audit")
    if document["api_level"] != 19 or not isinstance(document["classes"], dict):
        raise BuildError("recipe must describe API 19 classes")
    result = {}
    all_classes = []
    for source, classes in document["classes"].items():
        if source not in SOURCES or not isinstance(classes, list) or not classes:
            raise BuildError(f"invalid source class list: {source}")
        values = tuple(classes)
        if any(not isinstance(item, str) or not item.startswith("L") or
               not item.endswith(";") for item in values) or \
                values != tuple(sorted(set(values))):
            raise BuildError(f"invalid class list: {source}")
        result[source] = values
        all_classes.extend(values)
    if not result or len(all_classes) != len(set(all_classes)):
        raise BuildError("recipe has no classes or contains duplicates")
    validate_date_family_audit(document["date_family_audit"], result)
    return result


def verify_inputs(recipe: dict[str, tuple[str, ...]]) -> None:
    expected = {source_path(source): SOURCES[source][1] for source in recipe}
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
        destination = work / (source + "-smali")
        # Keep each command below the Windows command-line length limit.
        for offset in range(0, len(classes), 100):
            run([
                "java", "-jar", str(SMALI / "baksmali.jar"), "disassemble",
                "--api", "19", "--jobs", "1",
                "--classes", ",".join(classes[offset:offset + 100]),
                "--output", str(destination), str(source_path(source)),
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


def sorted_unique_strings(value: object, name: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not value or any(
            not isinstance(item, str) or not item for item in value):
        raise BuildError(f"{name} must be a non-empty string list")
    result = tuple(value)
    if result != tuple(sorted(set(result))):
        raise BuildError(f"{name} must be sorted and unique")
    return result


def validate_date_family_audit(
        document: dict, recipe: dict[str, tuple[str, ...]]) -> None:
    required = {
        "source", "icu", "roots", "classifications",
        "native_methods", "retained_overlays", "expected",
    }
    if not isinstance(document, dict) or set(document) != required:
        raise BuildError("policy has unexpected top-level keys")
    if document["source"] != "core.jar":
        raise BuildError("date-family audit must use pinned core.jar")
    icu = document["icu"]
    if not isinstance(icu, dict) or set(icu) != {
            "project", "tag", "tag_object", "commit", "tree",
            "archive_sha256", "version", "data_version", "license_files"}:
        raise BuildError("invalid ICU policy")
    for key in ("tag_object", "commit", "tree"):
        if not isinstance(icu[key], str) or len(icu[key]) != 40:
            raise BuildError(f"invalid ICU {key}")
    if not isinstance(icu["archive_sha256"], str) or \
            len(icu["archive_sha256"]) != 64:
        raise BuildError("invalid ICU archive SHA-256")
    sorted_unique_strings(icu["license_files"], "icu.license_files")
    roots = sorted_unique_strings(document["roots"], "roots")
    classifications = document["classifications"]
    if not isinstance(classifications, dict) or set(classifications) != set(CATEGORIES):
        raise BuildError("invalid classification categories")
    classified: dict[str, str] = {}
    for category in CATEGORIES:
        for descriptor in sorted_unique_strings(
                classifications[category], f"classifications.{category}"):
            if not descriptor.startswith("L") or not descriptor.endswith(";"):
                raise BuildError(f"invalid descriptor: {descriptor}")
            if descriptor in classified:
                raise BuildError(f"descriptor classified twice: {descriptor}")
            classified[descriptor] = category
    if any(classified.get(root) != "boot_dex" for root in roots):
        raise BuildError("every root must be a BootDex class")
    selected = set(classifications["boot_dex"])
    if not selected.issubset(recipe.get(document["source"], ())):
        raise BuildError("date-family BootDex classes are missing from the source recipe")
    all_classes = {item for values in recipe.values() for item in values}
    if (set(classified) - selected).intersection(all_classes):
        raise BuildError("date-family classification conflicts with the BootDex recipe")
    natives = document["native_methods"]
    if not isinstance(natives, dict) or set(natives) != set(NATIVE_DISPOSITIONS):
        raise BuildError("invalid native method dispositions")
    seen_native: set[str] = set()
    for disposition in NATIVE_DISPOSITIONS:
        values = sorted_unique_strings(
            natives[disposition], f"native_methods.{disposition}")
        overlap = seen_native.intersection(values)
        if overlap:
            raise BuildError(f"native method classified twice: {sorted(overlap)[0]}")
        seen_native.update(values)
    overlays = sorted_unique_strings(document["retained_overlays"], "retained_overlays")
    if any("->" not in item for item in overlays):
        raise BuildError("retained overlays must be exact members")
    expected = document["expected"]
    if not isinstance(expected, dict) or set(expected) != {
            "class_defs", "object_types", "method_ids", "field_ids",
            "native_methods", "classified_members_sha256"}:
        raise BuildError("invalid expected metrics")
    if any(not isinstance(expected[key], int) or expected[key] <= 0
           for key in ("class_defs", "object_types", "method_ids",
                       "field_ids", "native_methods")) or \
            not isinstance(expected["classified_members_sha256"], str) or \
            len(expected["classified_members_sha256"]) != 64:
        raise BuildError("invalid expected metric values")


def object_descriptor(descriptor: str) -> str | None:
    while descriptor.startswith("["):
        descriptor = descriptor[1:]
    return descriptor if descriptor.startswith("L") and descriptor.endswith(";") else None


def field_signature(dex: dex_survey_lib.DexFile, index: int) -> str:
    owner_index, type_index, name_index = dex.field_ids[index]
    return (f"{dex.type_name(owner_index)}->{dex.strings[name_index]}:"
            f"{dex.type_name(type_index)}")


def selected_dex(source: str, classes: tuple[str, ...]) -> bytes:
    recipe = {source: classes}
    verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix="ogplay-date-family-audit-") as work:
        return assemble(recipe, Path(work))


def classified_records(policy: dict, dex: dex_survey_lib.DexFile) -> dict:
    categories = {
        descriptor: category
        for category, descriptors in policy["classifications"].items()
        for descriptor in descriptors
    }
    observed_types = sorted({
        item for raw in dex.type_descriptors
        if (item := object_descriptor(raw)) is not None
    })
    missing = sorted(set(observed_types).difference(categories))
    stale = sorted(set(categories).difference(observed_types))
    if missing or stale:
        raise BuildError(
            f"type classification mismatch; missing={missing}, stale={stale}")

    def member_category(owner: str) -> str:
        if owner.startswith("["):
            return "existing_vm_intrinsic"
        return categories[owner]

    methods = []
    for index in range(len(dex.method_ids)):
        owner, name, descriptor = dex.method_signature(index)
        methods.append({
            "member": f"{owner}->{name}{descriptor}",
            "classification": member_category(owner),
        })
    fields = []
    for index, (owner_index, _, _) in enumerate(dex.field_ids):
        owner = dex.type_name(owner_index)
        fields.append({
            "member": field_signature(dex, index),
            "classification": member_category(owner),
        })

    native_dispositions = {
        member: disposition
        for disposition, members in policy["native_methods"].items()
        for member in members
    }
    observed_natives = []
    for parsed_class in dex.classes:
        for method in parsed_class.direct_methods + parsed_class.virtual_methods:
            if method.access_flags & dex_survey_lib.ACC_NATIVE:
                owner, name, descriptor = dex.method_signature(method.method_index)
                observed_natives.append(f"{owner}->{name}{descriptor}")
    observed_natives.sort()
    if set(observed_natives) != set(native_dispositions):
        raise BuildError(
            "native method policy mismatch; "
            f"missing={sorted(set(observed_natives).difference(native_dispositions))}, "
            f"stale={sorted(set(native_dispositions).difference(observed_natives))}")

    types = [{"descriptor": item, "classification": categories[item]}
             for item in observed_types]
    methods.sort(key=lambda item: item["member"])
    fields.sort(key=lambda item: item["member"])
    natives = [{"member": item, "disposition": native_dispositions[item]}
               for item in observed_natives]
    method_members = {item["member"] for item in methods}
    stale_overlays = sorted(set(policy["retained_overlays"]).difference(method_members))
    if stale_overlays:
        raise BuildError(f"retained overlay is not in the selected DEX: {stale_overlays}")
    return {"types": types, "methods": methods, "fields": fields, "natives": natives}


def make_audit_report(policy: dict) -> dict:
    classes = tuple(policy["roots"])
    dex = dex_survey_lib.parse_dex(selected_dex(policy["source"], classes))
    actual_classes = tuple(sorted(dex.type_name(item.type_index) for item in dex.classes))
    if actual_classes != classes:
        raise BuildError("selected class_defs differ from BootDex policy")
    records = classified_records(policy, dex)
    canonical = json.dumps(records, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":")).encode("utf-8")
    metrics = {
        "class_defs": len(actual_classes),
        "object_types": len(records["types"]),
        "method_ids": len(records["methods"]),
        "field_ids": len(records["fields"]),
        "native_methods": len(records["natives"]),
        "classified_members_sha256": hashlib.sha256(canonical).hexdigest(),
    }
    return {
        "schema": 1,
        "source_sha256": SOURCES[policy["source"]][1],
        "icu_commit": policy["icu"]["commit"],
        "metrics": metrics,
        "records": records,
    }


def audit_date_family(report_path: Path, emit_expectations: bool = False) -> int:
    document = json.loads(RECIPE.read_text(encoding="utf-8"))
    load_recipe(document)
    policy = document["date_family_audit"]
    report = make_audit_report(policy)
    if emit_expectations:
        print(json.dumps(report["metrics"], indent=2))
        return 0
    if report["metrics"] != policy["expected"]:
        raise BuildError(f"audit metrics changed: {report['metrics']!r}")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Date-family audit passed: {report['metrics']['class_defs']} classes, "
          f"{report['metrics']['native_methods']} native methods; report={report_path}")
    return 0


def self_test() -> int:
    document = json.loads(RECIPE.read_text(encoding="utf-8"))
    load_recipe(document)
    if any(source_path(source) != AOSP / source
           for source in SOURCES if source.endswith(".jar")):
        raise BuildError("BootDex jars must come from .local/aosp")
    if object_descriptor("[[Ljava/lang/String;") != "Ljava/lang/String;" or \
            object_descriptor("[I") is not None:
        raise BuildError("descriptor normalization failed")

    def reject_recipe(candidate: dict, reason: str) -> None:
        try:
            load_recipe(candidate)
        except BuildError:
            return
        raise BuildError(f"invalid recipe accepted: {reason}")

    # Each mutation exercises an integration boundary without AOSP inputs or Java.
    for case in ("missing audit", "missing class", "conflicting classification",
                 "duplicate native", "unordered roots", "wrong source"):
        candidate = json.loads(json.dumps(document))
        policy = candidate["date_family_audit"]
        if case == "missing audit":
            del candidate["date_family_audit"]
        elif case == "missing class":
            candidate["classes"][policy["source"]].remove(
                policy["classifications"]["boot_dex"][0])
        elif case == "conflicting classification":
            candidate["classes"][policy["source"]].append(
                policy["classifications"]["deferred"][0])
            candidate["classes"][policy["source"]].sort()
        elif case == "duplicate native":
            policy["native_methods"]["explicit_failure"].append(
                policy["native_methods"]["required_backend"][0])
            policy["native_methods"]["explicit_failure"].sort()
        elif case == "unordered roots":
            policy["roots"].reverse()
        else:
            policy["source"] = "framework.jar"
        reject_recipe(candidate, case)
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
        # The builder must consume only the explicitly prepared, pinned payload.
        fake_manifest = Path(work) / "payload/manifest.json"
        with patch.dict(globals(), {"MANIFEST": fake_manifest,
                                   "CRYPTO_SHA256": sha256(sample)}):
            try:
                build_guest_jni()
            except BuildError as error:
                if "data/android/19/lib/libcrypto.so" not in str(error):
                    raise
            else:
                raise BuildError("guest crypto build accepted an unprepared input")
        if fake_manifest.parent.exists():
            raise BuildError("guest crypto build restored an unprepared payload")
    print("BootDex builder self-test passed")
    return 0


CRYPTO_SHA256 = "7d38659dfd49d7a02d229a4712c5090fdfbdb3db9b6618b773bf86ace9703f2a"
ICUUC_SHA256 = "1e47c2d57db1573ac4f6c09a8b1b815ed89a72d0686c032d0916644a44e9acfd"
ICUI18N_SHA256 = "08596ab1ed097f953cc681e4cc61e69c1cea5f639e9014f5789265923ccd5149"
ICU_DATA_SHA256 = "8275408cb7161606c9a1b55edf12df538a7110ad53103a00f8ac7ba5b092a96f"
NDK_REVISION = "25.2.9519653"

def build_guest_jni() -> int:
    """Build the unified API 19 ARM guest JNI library with NDK r25c."""
    inputs = {
        "lib/libcrypto.so": CRYPTO_SHA256,
        "lib/libicuuc.so": ICUUC_SHA256,
        "lib/libicui18n.so": ICUI18N_SHA256,
        "icu/icudt51l.dat": ICU_DATA_SHA256,
    }
    for relative, expected in inputs.items():
        path = MANIFEST.parent / relative
        if not path.is_file() or file_sha256(path) != expected:
            raise BuildError(f"missing or unexpected data/android/19/{relative}")
    default_ndk = Path(r"D:\01_software\android-sdk\ndk\r25c")
    ndk = Path(os.environ.get("OGPLAY_ANDROID_NDK", default_ndk))
    properties = ndk / "source.properties"
    if not properties.is_file() or f"Pkg.Revision = {NDK_REVISION}" not in properties.read_text(encoding="utf-8"):
        raise BuildError("build-guest-jni requires Android NDK r25c (25.2.9519653); "
                         "set OGPLAY_ANDROID_NDK if it is not at the documented path")
    hosts = list((ndk / "toolchains/llvm/prebuilt").glob("*"))
    if len(hosts) != 1:
        raise BuildError("NDK r25c must contain exactly one host LLVM prebuilt")
    tool_bin = hosts[0] / "bin"
    suffix = ".exe" if os.name == "nt" else ""
    driver_suffix = ".cmd" if os.name == "nt" else ""
    clang = tool_bin / f"armv7a-linux-androideabi19-clang{driver_suffix}"
    linker = tool_bin / f"ld.lld{suffix}"
    readelf = tool_bin / f"llvm-readelf{suffix}"
    for tool in (clang, linker, readelf):
        if not tool.is_file():
            raise BuildError(f"NDK tool is missing: {tool}")
    sources = [ROOT / "src/guest/crypto/crypto_jni.c",
               ROOT / "src/guest/icu/icu_jni.c"]
    source_inputs = [*sources, ROOT / "src/guest/icu/icu51_capi.h"]
    for source in source_inputs:
        if not source.is_file():
            raise BuildError(f"guest JNI source is missing: {source}")
    flags = ["-march=armv7-a", "-mfloat-abi=softfp", "-fPIC", "-fno-stack-protector",
             "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I" + str(ROOT / "src/guest/icu")]
    def compile_at(work: Path) -> bytes:
        objects = []
        for source in sources:
            obj = work / (source.stem + ".o")
            run([str(clang), *flags, "-c", str(source), "-o", str(obj)])
            objects.append(obj)
        library = work / "libogplay_jni.so"
        run([str(linker), "-shared", "--hash-style=sysv", "-soname", library.name,
             "-z", "max-page-size=4096", "--no-undefined", *map(str, objects),
             str(MANIFEST.parent / "lib/libcrypto.so"),
             str(MANIFEST.parent / "lib/libicui18n.so"),
             str(MANIFEST.parent / "lib/libicuuc.so"),
             str(MANIFEST.parent / "lib/libc.so"), "-o", str(library)])
        dynamic = subprocess.run([str(readelf), "-h", "-d", str(library)], check=True,
                                 text=True, stdout=subprocess.PIPE).stdout
        required = ("Class:                             ELF32", "Machine:                           ARM",
                    "Type:                              DYN", "Library soname: [libogplay_jni.so]")
        if any(value not in dynamic for value in required):
            raise BuildError("guest JNI ELF ABI or SONAME check failed")
        needed = re.findall(r"Shared library: \[([^]]+)\]", dynamic)
        if needed != ["libcrypto.so", "libicui18n.so", "libicuuc.so", "libc.so"]:
            raise BuildError(f"unexpected guest JNI DT_NEEDED: {needed}")
        return library.read_bytes()
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        library = compile_at(Path(a))
        if library != compile_at(Path(b)):
            raise BuildError("two guest JNI builds differ")
    (MANIFEST.parent / "lib/libogplay_jni.so").write_bytes(library)
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    entries = {entry.get("path"): entry for entry in document.get("libraries", [])}
    adapter = entries.get("lib/libogplay_jni.so")
    if not isinstance(adapter, dict):
        raise BuildError("manifest is missing lib/libogplay_jni.so")
    adapter["size"] = len(library)
    adapter["sha256"] = sha256(library)
    adapter["build"] = {
        "generator": "tools/bootdex/build_bootdex.py",
        "generator_sha256": file_sha256(Path(__file__)),
        "ndk_revision": NDK_REVISION,
        "target": "armv7a-linux-androideabi19",
        "language": "C11",
        "sources": [
            {"path": source.relative_to(ROOT).as_posix(),
             "sha256": file_sha256(source)}
            for source in source_inputs
        ],
        "inputs": [
            {"path": relative, "sha256": expected}
            for relative, expected in inputs.items()
        ],
    }
    MANIFEST.write_bytes(
        (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print(f"Built deterministic API 19 ARM guest JNI {sha256(library)} using NDK r25c")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=("build", "check", "audit", "build-guest-jni"))
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--report", type=Path, help="audit report destination")
    parser.add_argument("--emit-expectations", action="store_true",
                        help="print observed audit metrics without checking expectations")
    arguments = parser.parse_args()
    try:
        if arguments.self_test:
            return self_test()
        if arguments.mode is None:
            parser.error("mode is required")
        if arguments.mode == "audit":
            return audit_date_family(
                arguments.report or AUDIT_REPORT, arguments.emit_expectations)
        if arguments.report is not None or arguments.emit_expectations:
            parser.error("--report and --emit-expectations require audit mode")
        if arguments.mode == "build-guest-jni":
            return build_guest_jni()
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
