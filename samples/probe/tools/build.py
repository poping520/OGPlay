#!/usr/bin/env python3
"""Build and verify the API 19 Android capability probe."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import zipfile

sys.dont_write_bytecode = True
import assets

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out"
ABI = "armeabi-v7a"
PACKAGE = "org.ogplay.probe"


def newest(parent: Path) -> Path:
    return max((path for path in parent.iterdir() if path.is_dir()),
               key=lambda path: tuple(map(int, re.findall(r"\d+", path.name))))


def run(*args: Path | str) -> str:
    result = subprocess.run([str(arg) for arg in args], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    if result.returncode:
        print(result.stdout, file=sys.stderr, end="")
        result.check_returncode()
    return result.stdout


def type_spec_offsets(table: bytes):
    def chunks(start: int, end: int):
        while start < end:
            kind, header, size = struct.unpack_from("<HHI", table, start)
            if not 8 <= header <= size <= end - start:
                raise RuntimeError("invalid aapt2 resource chunk")
            yield start, kind, header, size
            start += size

    for table_start, kind, header, size in chunks(0, len(table)):
        if kind != 2:
            raise RuntimeError("invalid aapt2 resource table")
        for package, kind, package_header, package_size in chunks(table_start + header, table_start + size):
            if kind != 0x200:
                continue
            for offset, kind, header, _ in chunks(package + package_header, package + package_size):
                if kind == 0x202:
                    if header < 16:
                        raise RuntimeError("invalid type-spec header")
                    yield offset


def api19_resource_table(data: bytes) -> bytes:
    """Clear type-spec res1 for the current OGPlay reader."""
    table = bytearray(data)
    for offset in type_spec_offsets(data):
        struct.pack_into("<H", table, offset + 10, 0)
    return bytes(table)


RESOURCE_PACKS = {"aapt2.apk": True, "control.apk": False}


def build_resource_packs(aapt: Path, platform: Path, work: Path) -> Path:
    fixture = ROOT / "fixtures/restable"
    resources = work / "restable.zip"
    packs = work / "restable"
    packs.mkdir()
    raw = packs / "aapt2.apk"
    run(aapt, "compile", "--dir", fixture / "res", "-o", resources)
    run(aapt, "link", "-o", raw, "-I", platform, "--manifest", fixture / "AndroidManifest.xml", resources)
    with zipfile.ZipFile(raw) as linked, zipfile.ZipFile(packs / "control.apk", "w") as control:
        for entry in linked.infolist():
            payload = linked.read(entry)
            if entry.filename == "resources.arsc":
                payload = api19_resource_table(payload)
            control.writestr(entry, payload)
    return packs


def verify_resource_packs(apk: zipfile.ZipFile) -> None:
    for name, nonzero in RESOURCE_PACKS.items():
        entry = apk.getinfo(f"assets/restable/{name}")
        if entry.compress_type != zipfile.ZIP_STORED:
            raise RuntimeError(f"resource pack must be stored: {name}")
        with zipfile.ZipFile(io.BytesIO(apk.read(entry))) as pack:
            table = pack.read("resources.arsc")
        values = [struct.unpack_from("<H", table, offset + 10)[0] for offset in type_spec_offsets(table)]
        if not values or any(values) != nonzero:
            expected = "at least one nonzero res1" if nonzero else "all res1 fields zero"
            raise RuntimeError(f"{name}: expected {expected}, got {values}")
        print(f"Resource pack {name}: stored, type-spec res1={values}")


def service_registry(generated: Path) -> None:
    tables = ROOT.parents[1] / "tools/conformance/api19_platform_tables.json"
    reference = json.loads(tables.read_text(encoding="utf-8"))
    services, types = reference["system_services"], reference["system_service_types"]
    if reference["api"] != 19 or services.keys() != types.keys():
        raise RuntimeError("inconsistent API 19 service tables")
    rows = "".join(f'        {{"{name}", "{types[name]}"}},\n' for name in sorted(services))
    (generated / "ServiceRegistry.java").write_text(
        "package org.ogplay.probe;\nfinal class ServiceRegistry {\n"
        "    static final String[][] SERVICES = {\n" + rows + "    };\n}\n", encoding="utf-8")


def build(sdk: Path, ndk: Path, tools_version: str | None) -> Path:
    platform = sdk / "platforms/android-19/android.jar"
    toolchain = ndk / "build/cmake/android.toolchain.cmake"
    tools = sdk / "build-tools" / tools_version if tools_version else newest(sdk / "build-tools")
    cmake_bin = newest(sdk / "cmake") / "bin"
    llvm_bin = next(path for path in (ndk / "toolchains/llvm/prebuilt").iterdir() if path.is_dir()) / "bin"
    exe = ".exe" if os.name == "nt" else ""
    bat = ".bat" if os.name == "nt" else ""
    cmake, ninja = (cmake_bin / (name + exe) for name in ("cmake", "ninja"))
    aapt, zipalign = (tools / (name + exe) for name in ("aapt2", "zipalign"))
    d8, signer = (tools / (name + bat) for name in ("d8", "apksigner"))
    readelf, strip = (llvm_bin / ("llvm-" + name + exe) for name in ("readelf", "strip"))
    jdk = Path(os.environ["JAVA_HOME"]) / "bin" if "JAVA_HOME" in os.environ else None
    javac, keytool, java = (jdk / (name + exe) if jdk else name for name in ("javac", "keytool", "java"))
    for path in (platform, toolchain):
        if not path.is_file():
            raise RuntimeError(f"missing build input {path}")

    work = OUT / "package"
    native = OUT / "native"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    run(cmake, "-S", ROOT / "src/main/cpp", "-B", native, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DANDROID_ABI={ABI}", "-DANDROID_PLATFORM=android-19",
        "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release")
    run(cmake, "--build", native)
    library = work / "libogprobe.so"
    run(strip, "--strip-unneeded", native / library.name, "-o", library)
    header = run(readelf, "-h", library)
    symbols = run(readelf, "--dyn-syms", "--wide", library)
    dynamic = run(readelf, "-d", library)
    if ("ELF32" not in header or "ARM" not in header or
            "Java_org_ogplay_probe_NativeProbe_ping" not in symbols or "(GNU_HASH)" in dynamic or
            "libc++_shared.so" in dynamic):
        raise RuntimeError("invalid ARMv7 JNI library")

    generated = work / "generated"
    generated.mkdir()
    service_registry(generated)
    resources = work / "resources.zip"
    res = work / "res"
    shutil.copytree(ROOT / "res", res)
    fixtures = assets.generate(work, res, generated)
    resource_packs = build_resource_packs(aapt, platform, work)
    run(java, "-Djava.awt.headless=true", ROOT / "tools/GenerateJpeg.java", fixtures / "pixels.jpg")
    secondary = work / "secondary"
    secondary.mkdir()
    secondary_source = secondary / "Secondary.java"
    secondary_source.write_text(
        "package org.ogplay.probe.secondary;\npublic final class Secondary {\n"
        "    public static int evaluate(int value) { return value * value + 1; }\n}\n", encoding="utf-8")
    run(javac, "-encoding", "UTF-8", "-source", "1.7", "-target", "1.7",
        "-bootclasspath", platform, "-Xlint:-options", "-d", secondary, secondary_source)
    run(d8, "--release", "--min-api", "9", "--lib", platform, "--output", secondary,
        *sorted(secondary.rglob("*.class")))
    (secondary / "classes.dex").replace(fixtures / "secondary.dex")
    run(aapt, "compile", "--dir", res, "-o", resources)
    unsigned = work / "unsigned.apk"
    run(aapt, "link", "-o", unsigned, "-I", platform, "--manifest", ROOT / "AndroidManifest.xml",
        "--java", generated, resources)
    classes, dex = work / "classes", work / "dex"
    classes.mkdir()
    dex.mkdir()
    sources = sorted((ROOT / "src/main/java").rglob("*.java")) + sorted(generated.rglob("*.java"))
    run(javac, "-encoding", "UTF-8", "-source", "1.7", "-target", "1.7",
        "-bootclasspath", platform, "-Xlint:-options", "-d", classes, *sources)
    run(d8, "--release", "--min-api", "9", "--lib", platform, "--output", dex,
        *sorted(classes.rglob("*.class")))
    members = {"classes.dex": dex / "classes.dex", f"lib/{ABI}/{library.name}": library}
    packed = work / "packed.apk"
    with zipfile.ZipFile(unsigned) as linked, zipfile.ZipFile(packed, "w") as apk:
        for entry in linked.infolist():
            payload = linked.read(entry)
            if entry.filename == "resources.arsc":
                payload = api19_resource_table(payload)
            apk.writestr(entry, payload)
        for name, path in members.items():
            apk.write(path, name, compress_type=zipfile.ZIP_DEFLATED)
        for path in sorted(fixtures.iterdir()):
            method = assets.FIXTURES.get(path.name, (zipfile.ZIP_DEFLATED,))[0]
            name = f"assets/fixtures/{path.name}"
            apk.writestr(zipfile.ZipInfo(name), path.read_bytes(), compress_type=method)
        for name in RESOURCE_PACKS:
            apk.writestr(zipfile.ZipInfo(f"assets/restable/{name}"), (resource_packs / name).read_bytes(),
                         compress_type=zipfile.ZIP_STORED)
    aligned = work / "aligned.apk"
    run(zipalign, "-f", "-p", "4", packed, aligned)

    keystore = OUT / "debug.keystore"
    if not keystore.exists():
        run(keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android",
            "-alias", "androiddebugkey", "-keypass", "android", "-dname", "CN=OGPlay Probe",
            "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-noprompt")
    signed = work / "signed.apk"
    run(signer, "sign", "--ks", keystore, "--ks-pass", "pass:android",
        "--key-pass", "pass:android", "--v1-signing-enabled", "true", "--out", signed, aligned)
    run(signer, "verify", "--min-sdk-version", "9", signed)
    run(zipalign, "-c", "-p", "4", signed)
    badging = run(aapt, "dump", "badging", signed)
    for marker in (f"package: name='{PACKAGE}'", f"native-code: '{ABI}'",
                   f"launchable-activity: name='{PACKAGE}.ProbeActivity'",
                   "minSdkVersion:'9'", "targetSdkVersion:'19'", "locales:", "'zh'"):
        if marker not in badging:
            raise RuntimeError(f"missing APK declaration: {marker}")
    with zipfile.ZipFile(signed) as zip_file:
        verify_resource_packs(zip_file)
        required = set(members) | {f"assets/fixtures/{path.name}" for path in fixtures.iterdir()}
        if zip_file.testzip() or not required <= set(zip_file.namelist()):
            raise RuntimeError("invalid APK contents")
        for name, (method, data) in assets.FIXTURES.items():
            entry = zip_file.getinfo(f"assets/fixtures/{name}")
            if entry.compress_type != method or zip_file.read(entry) != data:
                raise RuntimeError(f"invalid asset fixture {name}")
    final = OUT / "ogplay-probe.apk"
    signed.replace(final)
    return final


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--build-tools")
    args = parser.parse_args()
    if args.sdk is None or args.ndk is None:
        parser.error("provide --sdk and --ndk or set ANDROID_SDK_ROOT and ANDROID_NDK_ROOT")
    try:
        apk = build(args.sdk.resolve(), args.ndk.resolve(), args.build_tools)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"build failed: {error}\n")
    print(f"APK: {apk}\nSHA-256: {hashlib.sha256(apk.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
