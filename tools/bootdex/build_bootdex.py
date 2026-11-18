#!/usr/bin/env python3
"""Build and check the API 19 boot classpath payload.

bootdex.jar contains OGPlay's compiled guest Java followed by the original DEX
files and resources from the pinned .aosp/api19/rom jars. build/check also
produce framework-res.apk from the pinned ROM original (wallpapers and META-INF
removed, everything else kept). The same script builds the guest JNI library
(build-guest-jni).
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import io
import json
import os
import re
import subprocess
import shutil
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from aosp_sources import rom_dir  # noqa: E402

AOSP = rom_dir(19)
FRAMEWORK = ROOT / "data/android/19/framework"
BOOT_JAR = FRAMEWORK / "bootdex.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/boot-classpath.txt"
ROM_NOTICES = {"telephony-common.jar": "notices/telephony-common.jar.txt"}
GUEST_JAVA_ROOTS = tuple(ROOT / "src/guest" / name / "java"
                         for name in ("crypto", "framework", "io", "math", "webview", "xml", "zip"))

# API 19 ROM jars in boot classpath order, after OGPlay's DEX.
# Each entry pins the source project, original jar hash and original DEX hash.
ROM_JARS = {
    "core.jar": ("platform/libcore",
                 "60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee",
                 "68edab3c434076cdeb8fcc408dfb208706a2c54081598a117563352b671f80de"),
    "conscrypt.jar": ("platform/libcore",
                      "c7d0849cde38cd0d0dc99d89ee61bd33a65618567fc18171d6605dc6b8e15004",
                      "a8c3f9df9b31a9fc9b78d16d618f21af5ed80a703ee67a3cea8b8738244fb621"),
    "ext.jar": ("platform/external/apache-http",
                "e95d35989f0e1b438b3b6fea2777a798a0398f457a32875cdadfe3e2573a4ca2",
                "25315c2c3ec8ca7ec2aacebadadaeade8af55de4e2f584db35b9c8337f307c23"),
    "framework.jar": ("platform/frameworks/base",
                      "422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b",
                      "905d1ba05d4f64eb889fdc02b9bcaca382e991c642913810f8949c1c19d0f590"),
    "framework2.jar": ("platform/frameworks/base",
                       "5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973",
                       "ef843c09e8a5bde9916e08f3b2b15a4789a35792dc2cc4ad2ce7737920ea9fcf"),
    "telephony-common.jar": ("platform/frameworks/opt/telephony",
                             "b1e4f98857dd07d9fa7bc1d6c2d6cdb49d41adc2973533db3982ef0d5348a908",
                             "2887c593e7656dbe925d6d432a7703f37290a93f23b19ae9bbf4b05a75eb5f25"),
    "android.policy.jar": ("platform/frameworks/base",
                           "660fa192659bf02285cfeb559d3fad547a72d4986aa295ef5a9453b2b2372ad1",
                           "2919a52dd8b950ed086dc70779f76cab9fec25fa4528d04f66d8ef02573deeed"),
}
LEGACY_BOOT_JARS = ("ogplay.jar", *ROM_JARS)
DEX_ENTRIES = ("classes.dex", *(f"classes{index}.dex" for index in range(2, len(ROM_JARS) + 2)))

# Hidden API 19 types are absent from the public SDK android.jar. These
# declarations only satisfy javac; the ROM jars supply the real classes.
COMPILE_ONLY_SOURCES = {
    "com/android/org/conscrypt/NativeCrypto.java": """package com.android.org.conscrypt;
public final class NativeCrypto {
    public static native long EVP_get_digestbyname(String name);
    public static native long EVP_DigestInit(long algorithm);
    public static native void EVP_DigestUpdate(long ctx, byte[] input, int offset, int length);
    public static native int EVP_DigestFinal(long ctx, byte[] output, int offset);
    public static native void EVP_MD_CTX_destroy(long ctx);
    public static native long EVP_MD_CTX_copy(long ctx);
}""",
    "android/os/SystemProperties.java":
        "package android.os; public class SystemProperties {"
        " public static String get(String key, String def) { throw new AssertionError(); } }",
    "org/kxml2/io/KXmlParser.java":
        "package org.kxml2.io; public abstract class KXmlParser"
        " implements org.xmlpull.v1.XmlPullParser { public abstract String getRootElementName(); }",
    "android/app/Application.java": """package android.app;
import android.os.Bundle;
public class Application extends android.content.ContextWrapper {
    public Application() { super(null); }
    void dispatchActivityCreated(Activity activity, Bundle state) {}
    void dispatchActivityStarted(Activity activity) {}
    void dispatchActivityResumed(Activity activity) {}
    void dispatchActivityPaused(Activity activity) {}
    void dispatchActivityStopped(Activity activity) {}
    void dispatchActivitySaveInstanceState(Activity activity, Bundle state) {}
    void dispatchActivityDestroyed(Activity activity) {}
}""",
    "libcore/io/BufferIterator.java": """package libcore.io;
public abstract class BufferIterator {
    public abstract void seek(int offset);
    public abstract void skip(int count);
    public abstract void readByteArray(byte[] dst, int offset, int count);
    public abstract byte readByte();
    public abstract int readInt();
    public abstract void readIntArray(int[] dst, int offset, int count);
    public abstract short readShort();
}""",
    "libcore/util/ZoneInfo.java": """package libcore.util;
public abstract class ZoneInfo extends java.util.TimeZone {
    public static java.util.TimeZone makeTimeZone(String id, libcore.io.BufferIterator it) {
        throw new AssertionError();
    }
}""",
}
WEBVIEW_COMPILE_ONLY = Path(__file__).resolve().parent / "compile-only" / "webview"
COMPILE_ONLY_SOURCES.update({
    path.relative_to(WEBVIEW_COMPILE_ONLY).as_posix(): path.read_text(encoding="utf-8")
    for path in sorted(WEBVIEW_COMPILE_ONLY.rglob("*.java"))
})

COMPILE_ONLY_CLASSES = tuple(sorted(name[:-len(".java")] + ".class" for name in COMPILE_ONLY_SOURCES))

WINDOWS_JAVAC = Path(r"D:\01_software\jdk-17.0.2\bin\javac.exe")
WINDOWS_ANDROID_JAR = Path(
    r"D:\01_software\android-sdk\platforms\android-19\android.jar")
WINDOWS_D8_JAR = Path(
    r"D:\01_software\android-sdk\build-tools\29.0.2\lib\d8.jar")
LOCAL_ANDROID_JAR = ROOT / ".local/toolchain/android-19/android.jar"
LOCAL_D8_JAR = ROOT / ".local/toolchain/build-tools-29.0.2/d8.jar"
ANDROID_JAR_SHA256 = "4032a201eeb1d0430c7d9f1075151dd280427de5ca9e36f734f464ab605cb690"
D8_JAR_SHA256 = "d9e6acde0cb6f2453d6835b52c31d2066054394fb79e91115dd4978bde6afd16"


def _first_file(*candidates: Path) -> Path:
    for path in candidates:
        if path.is_file():
            return path
    return candidates[-1]


def _macos_javacs() -> tuple[Path, ...]:
    found: list[Path] = []
    home = os.environ.get("JAVA_HOME")
    if home:
        found.append(Path(home) / "bin" / "javac")
    java_home = Path("/usr/libexec/java_home")
    if java_home.is_file():
        result = subprocess.run(
            [str(java_home), "-v", "17"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", check=False)
        if result.returncode == 0 and result.stdout.strip():
            found.append(Path(result.stdout.strip()) / "bin" / "javac")
    found.extend((
        Path("/Library/Java/JavaVirtualMachines/openjdk-17.jdk/Contents/Home/bin/javac"),
        Path("/opt/homebrew/opt/openjdk@17/bin/javac"),
    ))
    which = shutil.which("javac")
    if which:
        found.append(Path(which))
    return tuple(found)


def resolve_javac() -> Path:
    env = os.environ.get("OGPLAY_JAVAC")
    if env:
        return Path(env)
    if os.name == "nt":
        return WINDOWS_JAVAC
    return _first_file(*_macos_javacs(), WINDOWS_JAVAC)


def resolve_java(javac: Path) -> Path:
    return javac.with_name("java.exe" if os.name == "nt" else "java")


def resolve_android_jar() -> Path:
    env = os.environ.get("OGPLAY_ANDROID_19_JAR")
    if env:
        return Path(env)
    return _first_file(LOCAL_ANDROID_JAR, WINDOWS_ANDROID_JAR)


def resolve_d8_jar() -> Path:
    env = os.environ.get("OGPLAY_D8_JAR")
    if env:
        return Path(env)
    return _first_file(LOCAL_D8_JAR, WINDOWS_D8_JAR)


JAVAC = resolve_javac()
JAVA = resolve_java(JAVAC)
ANDROID_JAR = resolve_android_jar()
D8_JAR = resolve_d8_jar()

JAR_MANIFEST = (
    b"Manifest-Version: 1.0\r\n"
    b"Created-By: OGPlay Boot Classpath Builder\r\n\r\n"
)


class BuildError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha256(path: Path) -> str:
    return sha256(path.read_bytes())


def run(command: list[str]) -> None:
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", check=False)
    if result.returncode:
        raise BuildError(result.stdout)


def javac_release() -> str:
    result = subprocess.run(
        [str(JAVAC), "-version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", check=False)
    version = result.stdout.strip()
    prefix = "javac "
    if result.returncode or not version.startswith(prefix):
        raise BuildError(f"unexpected javac: {version}")
    return version[len(prefix):]


def guest_java_sources() -> tuple[Path, ...]:
    return tuple(sorted(path for root in GUEST_JAVA_ROOTS for path in root.rglob("*.java")))


def check_toolchain() -> None:
    if not JAVAC.is_file() or not JAVA.is_file() or not ANDROID_JAR.is_file() or not D8_JAR.is_file():
        raise BuildError("javac 17, API 19 android.jar or d8.jar is missing")
    if file_sha256(ANDROID_JAR) != ANDROID_JAR_SHA256:
        raise BuildError("unexpected API 19 android.jar")
    if file_sha256(D8_JAR) != D8_JAR_SHA256:
        raise BuildError("unexpected Android build-tools 29.0.2 d8.jar")
    if not javac_release().startswith("17."):
        raise BuildError(f"unexpected javac: {javac_release()}")


def compile_guest_java(work: Path) -> bytes:
    sources = guest_java_sources()
    compile_only = work / "compile-only"
    for name, text in COMPILE_ONLY_SOURCES.items():
        path = compile_only / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    classes = work / "classes"
    classes.mkdir()
    run([
        str(JAVAC), "-J-Duser.language=en", "-encoding", "UTF-8", "-source", "7", "-target", "7",
        "-Xlint:-options", "-g:none", "-bootclasspath", str(ANDROID_JAR),
        "-d", str(classes), *(str(source) for source in sources),
        *(str(compile_only / name) for name in COMPILE_ONLY_SOURCES),
    ])
    for name in COMPILE_ONLY_CLASSES:
        path = classes / name
        path.unlink()
        for nested in path.parent.glob(f"{path.stem}$*.class"):
            nested.unlink()
    class_files = sorted(classes.rglob("*.class"))
    if not class_files:
        raise BuildError("guest Java compilation produced no classes")
    dex_dir = work / "dex"
    dex_dir.mkdir()
    run([str(JAVA), "-cp", str(D8_JAR), "com.android.tools.r8.D8", "--min-api", "19",
         "--output", str(dex_dir), *(str(path) for path in class_files)])
    if sorted(path.name for path in dex_dir.iterdir()) != ["classes.dex"]:
        raise BuildError("guest Java must fit one classes.dex")
    return (dex_dir / "classes.dex").read_bytes()


class BootZipWriter:
    """Write a canonical ZIP32 archive from stored or already compressed data."""

    def __init__(self) -> None:
        self.output = io.BytesIO()
        self.directory = io.BytesIO()
        self.names: set[str] = set()

    def add(self, name: str, compressed: bytes, method: int, crc: int, size: int) -> None:
        if name in self.names:
            raise BuildError(f"duplicate boot archive entry: {name}")
        self.names.add(name)
        encoded = name.encode("utf-8")
        flags = 0 if name.isascii() else 0x800
        offset = self.output.tell()
        # ZIP version, flags, method, DOS time/date, CRC, sizes and name length.
        fields = (20, flags, method, 0, 0x21, crc, len(compressed), size, len(encoded))
        self.output.write(struct.pack("<I5H3I2H", 0x04034B50, *fields, 0))
        self.output.write(encoded)
        self.output.write(compressed)
        self.directory.write(struct.pack("<I6H3I5H2I", 0x02014B50, 0x314, *fields,
                                         0, 0, 0, 0, 0o100644 << 16, offset))
        self.directory.write(encoded)

    def add_stored(self, name: str, contents: bytes) -> None:
        self.add(name, contents, zipfile.ZIP_STORED, binascii.crc32(contents), len(contents))

    def bytes(self) -> bytes:
        offset = self.output.tell()
        directory = self.directory.getvalue()
        count = len(self.names)
        self.output.write(directory)
        self.output.write(struct.pack("<I4H2IH", 0x06054B50, 0, 0, count, count, len(directory), offset, 0))
        return self.output.getvalue()


def compressed_zip_data(source: bytes, info: zipfile.ZipInfo) -> bytes:
    # Central-directory lengths also cover sources that use a data descriptor.
    offset = info.header_offset
    if source[offset:offset + 4] != b"PK\x03\x04" or offset + 30 > len(source):
        raise BuildError(f"invalid ZIP local header: {info.filename}")
    name_size, extra_size = struct.unpack_from("<HH", source, offset + 26)
    start = offset + 30 + name_size + extra_size
    data = source[start:start + info.compress_size]
    if len(data) != info.compress_size:
        raise BuildError(f"truncated ZIP entry: {info.filename}")
    return data


def build_ogplay_dex() -> bytes:
    check_toolchain()
    with tempfile.TemporaryDirectory(prefix="ogplay-dex-a-") as first, \
            tempfile.TemporaryDirectory(prefix="ogplay-dex-b-") as second:
        dex = compile_guest_java(Path(first))
        if dex != compile_guest_java(Path(second)):
            raise BuildError("two guest Java builds differ")
    return dex


def rom_jar(name: str) -> bytes:
    data = (AOSP / name).read_bytes()
    if sha256(data) != ROM_JARS[name][1]:
        raise BuildError(f"unexpected ROM input: {AOSP / name}")
    return data


def pack_boot_classpath(dex: bytes) -> tuple[bytes, list[dict]]:
    writer = BootZipWriter()
    writer.add_stored("META-INF/MANIFEST.MF", JAR_MANIFEST)
    writer.add_stored(DEX_ENTRIES[0], dex)
    dexes = [{"entry": DEX_ENTRIES[0], "size": len(dex), "sha256": sha256(dex)}]
    resources = []
    for entry, (name, (project, source_hash, dex_hash)) in zip(DEX_ENTRIES[1:], ROM_JARS.items()):
        source = rom_jar(name)
        with zipfile.ZipFile(io.BytesIO(source)) as rom:
            info = rom.getinfo("classes.dex")
            if sha256(rom.read(info)) != dex_hash:
                raise BuildError(f"unexpected ROM DEX: {name}/classes.dex")
            writer.add(entry, compressed_zip_data(source, info), info.compress_type, info.CRC, info.file_size)
            for resource in rom.infolist():
                if resource.is_dir() or resource.filename.startswith("META-INF/") or resource.filename.endswith(".dex"):
                    continue
                resources.append((resource.filename, compressed_zip_data(source, resource),
                                  resource.compress_type, resource.CRC, resource.file_size))
        metadata = {"entry": entry, "size": info.file_size, "sha256": dex_hash,
                    "source": name, "source_project": project, "source_sha256": source_hash}
        if name in ROM_NOTICES:
            notice = ROM_NOTICES[name]
            metadata.update(notice=notice, notice_sha256=file_sha256(MANIFEST.parent / notice))
        dexes.append(metadata)
    for resource in resources:
        writer.add(*resource)
    return writer.bytes(), dexes


FRAMEWORK_RES = FRAMEWORK / "framework-res.apk"
WALLPAPERS = {
    f"res/drawable-{qualifier}nodpi/default_wallpaper.jpg"
    for qualifier in ("", "sw600dp-", "sw720dp-")
}


def trim_framework_resources(source: bytes) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(source)) as original, zipfile.ZipFile(output, "w") as archive:
        if original.getinfo("resources.arsc").compress_type != zipfile.ZIP_STORED:
            raise BuildError("original resources.arsc must be STORED")
        if not WALLPAPERS <= set(original.namelist()):
            raise BuildError("original framework-res.apk is missing a wallpaper to remove")
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


def framework_resources() -> bytes:
    source = (AOSP / "framework-res.apk").read_bytes()
    expected = json.loads(MANIFEST.read_text(encoding="utf-8"))["framework_resources"]["original_sha256"]
    if sha256(source) != expected:
        raise BuildError(f"unexpected ROM input: {AOSP / 'framework-res.apk'}")
    return trim_framework_resources(source)


def boot_metadata(jar: bytes, dexes: list[dict]) -> dict:
    dexes[0]["sources"] = {
        "roots": [root.relative_to(ROOT).as_posix() for root in GUEST_JAVA_ROOTS],
        "sha256": sha256(b"".join(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0"
                                  + path.read_bytes() for path in guest_java_sources())),
        "javac": javac_release(), "android_jar_sha256": ANDROID_JAR_SHA256, "d8_sha256": D8_JAR_SHA256,
    }
    return {
        "path": BOOT_JAR.relative_to(MANIFEST.parent).as_posix(),
        "size": len(jar),
        "sha256": sha256(jar),
        "dex": dexes,
        "generator": "tools/bootdex/build_bootdex.py",
        "notice": NOTICE.relative_to(MANIFEST.parent).as_posix(),
        "notice_sha256": file_sha256(NOTICE),
    }


def manifest_bytes(metadata: dict, resources: bytes) -> bytes:
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    document.pop("boot_dex", None)
    document["boot_classpath"] = metadata
    document["framework_resources"].update(size=len(resources), sha256=sha256(resources))
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode()


def build_or_check(mode: str) -> int:
    jar, dexes = pack_boot_classpath(build_ogplay_dex())
    resources = framework_resources()
    manifest = manifest_bytes(boot_metadata(jar, dexes), resources)
    outputs = {BOOT_JAR: jar, FRAMEWORK_RES: resources, MANIFEST: manifest}
    legacy = [FRAMEWORK / name for name in LEGACY_BOOT_JARS]
    if mode == "build":
        FRAMEWORK.mkdir(parents=True, exist_ok=True)
        for path, data in outputs.items():
            if not path.is_file() or path.read_bytes() != data:
                path.write_bytes(data)
        for path in legacy:
            path.unlink(missing_ok=True)
    else:
        stale = [path.relative_to(ROOT).as_posix() for path, data in outputs.items()
                 if not path.is_file() or path.read_bytes() != data]
        stale.extend(path.relative_to(ROOT).as_posix() for path in legacy if path.exists() or path.is_symlink())
        if stale:
            raise BuildError("boot classpath payload is stale (run build): " + ", ".join(stale))
    print(f"boot classpath {mode}: bootdex.jar {sha256(jar)} ({len(dexes)} DEX) + framework-res.apk {sha256(resources)}")
    return 0


CRYPTO_SHA256 = "7d38659dfd49d7a02d229a4712c5090fdfbdb3db9b6618b773bf86ace9703f2a"
SSL_SHA256 = "8b1a7d20e405ff73edcaad592cf846f8e28b78bb446874208d65990b16d79734"
ICUUC_SHA256 = "1e47c2d57db1573ac4f6c09a8b1b815ed89a72d0686c032d0916644a44e9acfd"
ICUI18N_SHA256 = "08596ab1ed097f953cc681e4cc61e69c1cea5f639e9014f5789265923ccd5149"
ICU_DATA_SHA256 = "8275408cb7161606c9a1b55edf12df538a7110ad53103a00f8ac7ba5b092a96f"
ZLIB_SHA256 = "c2f0143900e491639a0ceac70d1d24a008ab719ec8283465acb4449f2a83c48e"
NDK_REVISION = "25.2.9519653"

def build_guest_jni() -> int:
    """Build the unified API 19 ARM guest JNI library with NDK r25c."""
    inputs = {
        "lib/libcrypto.so": CRYPTO_SHA256,
        "lib/libssl.so": SSL_SHA256,
        "lib/libicuuc.so": ICUUC_SHA256,
        "lib/libicui18n.so": ICUI18N_SHA256,
        "lib/libz.so": ZLIB_SHA256,
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
    hosts = [path for path in (ndk / "toolchains/llvm/prebuilt").glob("*")
             if path.is_dir()]
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
               ROOT / "src/guest/crypto/rsa_jni.c",
               ROOT / "src/guest/crypto/trust_jni.c",
               ROOT / "src/guest/crypto/tls_jni.c",
               ROOT / "src/guest/icu/icu_jni.c",
               ROOT / "src/guest/icu/regex_jni.c",
               ROOT / "src/guest/icu/converter_jni.c",
               ROOT / "src/guest/zip/crc32_jni.c",
               ROOT / "src/guest/zip/zip_stream_jni.c",
               ROOT / "src/guest/math/big_integer_jni.c"]
    source_inputs = [*sources, ROOT / "src/guest/icu/icu51_capi.h", ROOT / "src/guest/crypto/crypto_jni.h"]
    for source in source_inputs:
        if not source.is_file():
            raise BuildError(f"guest JNI source is missing: {source}")
    flags = ["-march=armv7-a", "-mfloat-abi=softfp", "-fPIC", "-fno-stack-protector",
             "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I" + str(ROOT / "src/guest/icu")]
    def compile_at(work: Path):
        objects = []
        for source in sources:
            obj = work / (source.stem + ".o")
            run([str(clang), *flags, "-c", str(source), "-o", str(obj)])
            objects.append(obj)
        library = work / "libogplay_jni.so"
        run([str(linker), "-shared", "--hash-style=sysv", "-soname", library.name,
             "-z", "max-page-size=4096", "--no-undefined", *map(str, objects),
             str(MANIFEST.parent / "lib/libssl.so"),
             str(MANIFEST.parent / "lib/libcrypto.so"),
             str(MANIFEST.parent / "lib/libicui18n.so"),
             str(MANIFEST.parent / "lib/libicuuc.so"),
             str(MANIFEST.parent / "lib/libz.so"),
             str(MANIFEST.parent / "lib/libm.so"),
             str(MANIFEST.parent / "lib/libc.so"), "-o", str(library)])
        dynamic = subprocess.run([str(readelf), "-h", "-d", str(library)], check=True,
                                 text=True, stdout=subprocess.PIPE).stdout
        required = ("Class:                             ELF32", "Machine:                           ARM",
                    "Type:                              DYN", "Library soname: [libogplay_jni.so]")
        if any(value not in dynamic for value in required):
            raise BuildError("guest JNI ELF ABI or SONAME check failed")
        needed = re.findall(r"Shared library: \[([^]]+)\]", dynamic)
        if needed != ["libssl.so", "libcrypto.so", "libicui18n.so", "libicuuc.so", "libz.so", "libm.so", "libc.so"]:
            raise BuildError(f"unexpected guest JNI DT_NEEDED: {needed}")
        return library.read_bytes(), needed
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        library, needed = compile_at(Path(a))
        second, second_needed = compile_at(Path(b))
        if library != second or needed != second_needed:
            raise BuildError("two guest JNI builds differ")
    (MANIFEST.parent / "lib/libogplay_jni.so").write_bytes(library)
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    entries = {entry.get("path"): entry for entry in document.get("libraries", [])}
    adapter = entries.get("lib/libogplay_jni.so")
    if not isinstance(adapter, dict):
        raise BuildError("manifest is missing lib/libogplay_jni.so")
    adapter["size"] = len(library)
    adapter["sha256"] = sha256(library)
    adapter["needed"] = needed
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
    parser.add_argument("mode", choices=("build", "check", "build-guest-jni"))
    arguments = parser.parse_args()
    try:
        if arguments.mode == "build-guest-jni":
            return build_guest_jni()
        return build_or_check(arguments.mode)
    except (BuildError, OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
