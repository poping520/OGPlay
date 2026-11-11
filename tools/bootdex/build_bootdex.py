#!/usr/bin/env python3
"""Build and check the API 19 boot classpath payload.

The ROM jars are copied unmodified from .local/aosp. OGPlay's own guest Java is
compiled into ogplay.jar, which comes first on the boot classpath. The same
script builds the guest JNI library (build-guest-jni).
"""

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
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))


AOSP = ROOT / ".local/aosp"
FRAMEWORK = ROOT / "data/android/19/framework"
OGPLAY_JAR = FRAMEWORK / "ogplay.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/boot-classpath.txt"
ROM_NOTICES = {"telephony-common.jar": "notices/telephony-common.jar.txt"}
GUEST_JAVA_ROOTS = tuple(ROOT / "src/guest" / name / "java"
                         for name in ("crypto", "framework", "io", "math", "xml", "zip"))

# API 19 ROM jars in boot classpath order, after ogplay.jar.
ROM_JARS = {
    "core.jar": ("platform/libcore",
                 "60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee"),
    "conscrypt.jar": ("platform/libcore",
                      "c7d0849cde38cd0d0dc99d89ee61bd33a65618567fc18171d6605dc6b8e15004"),
    "ext.jar": ("platform/external/apache-http",
                "e95d35989f0e1b438b3b6fea2777a798a0398f457a32875cdadfe3e2573a4ca2"),
    "framework.jar": ("platform/frameworks/base",
                      "422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b"),
    "framework2.jar": ("platform/frameworks/base",
                       "5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973"),
    "telephony-common.jar": ("platform/frameworks/opt/telephony",
                             "b1e4f98857dd07d9fa7bc1d6c2d6cdb49d41adc2973533db3982ef0d5348a908"),
    "android.policy.jar": ("platform/frameworks/base",
                           "660fa192659bf02285cfeb559d3fad547a72d4986aa295ef5a9453b2b2372ad1"),
}

# Hidden API 19 types are absent from the public SDK android.jar. These
# declarations only satisfy javac; the ROM jars supply the real classes.
COMPILE_ONLY_SOURCES = {
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
        (classes / name).unlink()
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


def make_jar(dex: bytes) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, content in [("META-INF/MANIFEST.MF", JAR_MANIFEST), ("classes.dex", dex)]:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    return output.getvalue()


def build_ogplay_jar() -> bytes:
    check_toolchain()
    with tempfile.TemporaryDirectory(prefix="ogplay-jar-a-") as first, \
            tempfile.TemporaryDirectory(prefix="ogplay-jar-b-") as second:
        dex = compile_guest_java(Path(first))
        if dex != compile_guest_java(Path(second)):
            raise BuildError("two guest Java builds differ")
    return make_jar(dex)


def rom_jar(name: str) -> bytes:
    data = (AOSP / name).read_bytes()
    if sha256(data) != ROM_JARS[name][1]:
        raise BuildError(f"unexpected ROM input: .local/aosp/{name}")
    return data


def boot_metadata(jar: bytes) -> dict:
    def rom_metadata(name: str, project: str, digest: str) -> dict:
        entry = {"path": f"framework/{name}", "source_project": project,
                 "size": (AOSP / name).stat().st_size, "sha256": digest}
        if name in ROM_NOTICES:
            notice = ROM_NOTICES[name]
            entry.update(notice=notice, notice_sha256=file_sha256(MANIFEST.parent / notice))
        return entry

    return {
        "directory": "framework",
        "generator": "tools/bootdex/build_bootdex.py",
        "classpath": [
            {"path": "framework/ogplay.jar", "source_project": "OGPlay", "size": len(jar), "sha256": sha256(jar),
             "sources": {
                 "roots": [root.relative_to(ROOT).as_posix() for root in GUEST_JAVA_ROOTS],
                 "sha256": sha256(b"".join(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0"
                                           + path.read_bytes() for path in guest_java_sources())),
                 "javac": javac_release(), "android_jar_sha256": ANDROID_JAR_SHA256,
                 "d8_sha256": D8_JAR_SHA256}},
            *(rom_metadata(name, project, digest) for name, (project, digest) in ROM_JARS.items()),
        ],
        "notice": NOTICE.relative_to(MANIFEST.parent).as_posix(),
        "notice_sha256": file_sha256(NOTICE),
    }


def manifest_bytes(metadata: dict) -> bytes:
    document = json.loads(MANIFEST.read_text(encoding="utf-8"))
    document.pop("boot_dex", None)
    document["boot_classpath"] = metadata
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode()


def build_or_check(mode: str) -> int:
    jar = build_ogplay_jar()
    roms = {name: rom_jar(name) for name in ROM_JARS}
    manifest = manifest_bytes(boot_metadata(jar))
    outputs = {OGPLAY_JAR: jar, **{FRAMEWORK / name: data for name, data in roms.items()}, MANIFEST: manifest}
    if mode == "build":
        FRAMEWORK.mkdir(parents=True, exist_ok=True)
        for path, data in outputs.items():
            if not path.is_file() or path.read_bytes() != data:
                path.write_bytes(data)
    else:
        stale = [path.relative_to(ROOT).as_posix() for path, data in outputs.items()
                 if not path.is_file() or path.read_bytes() != data]
        if stale:
            raise BuildError("boot classpath payload is stale (run build): " + ", ".join(stale))
    print(f"boot classpath {mode}: ogplay.jar {sha256(jar)} + {len(roms)} ROM jars")
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
               ROOT / "src/guest/crypto/trust_jni.c",
               ROOT / "src/guest/crypto/tls_jni.c",
               ROOT / "src/guest/icu/icu_jni.c",
               ROOT / "src/guest/icu/regex_jni.c",
               ROOT / "src/guest/zip/crc32_jni.c",
               ROOT / "src/guest/zip/zip_stream_jni.c",
               ROOT / "src/guest/math/big_integer_jni.c"]
    source_inputs = [*sources, ROOT / "src/guest/icu/icu51_capi.h"]
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
             str(MANIFEST.parent / "lib/libc.so"), "-o", str(library)])
        dynamic = subprocess.run([str(readelf), "-h", "-d", str(library)], check=True,
                                 text=True, stdout=subprocess.PIPE).stdout
        required = ("Class:                             ELF32", "Machine:                           ARM",
                    "Type:                              DYN", "Library soname: [libogplay_jni.so]")
        if any(value not in dynamic for value in required):
            raise BuildError("guest JNI ELF ABI or SONAME check failed")
        needed = re.findall(r"Shared library: \[([^]]+)\]", dynamic)
        if needed != ["libssl.so", "libcrypto.so", "libicui18n.so", "libicuuc.so", "libz.so", "libc.so"]:
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
