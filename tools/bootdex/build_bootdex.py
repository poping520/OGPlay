#!/usr/bin/env python3
"""Build, check and audit curated API 19 BootDex from API 19 ROM/AOSP inputs."""

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
SYSTEM_RESOURCES = ROOT / "tools/bootdex/api19-system-resources.json"
SYSTEM_RESOURCES_ENTRY = "META-INF/ogplay/system-resources.json"
SYSTEM_RESOURCE_CONSUMERS = (
    "Landroid/database/CursorWindow;", "Landroid/database/sqlite/SQLiteGlobal;",
)
AOSP = ROOT / ".local/aosp"
SMALI = ROOT / ".local/tools"
OUTPUT = ROOT / "data/android/19/framework/bootdex.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/bootdex.jar.txt"
AUDIT_REPORT = ROOT / ".local/dvm102-date-family-audit.json"
NATIVE_CRYPTO_AUDIT_REPORT = ROOT / ".local/nativecrypto-api19-audit.json"
JAVA_SOURCE_ROOT = ROOT / "src/guest/crypto/java"
JAVA_SOURCE_NAMES = tuple(sorted((
    "org/ogplay/security/AesCbcZeroBytePadding.java",
    "org/ogplay/security/AndroidCaStoreSpi.java",
    "org/ogplay/security/BksKeyStoreSpi.java",
    "org/ogplay/security/BksLimits.java",
    "org/ogplay/security/BksPrivateKey.java",
    "org/ogplay/security/CaBundle.java",
    "org/ogplay/security/NativeKeyStoreCrypto.java",
    "org/ogplay/security/NativeTls.java",
    "org/ogplay/security/NativeTrust.java",
    "org/ogplay/security/OgPlayHttpURLConnection.java",
    "org/ogplay/security/OgPlayHttpsURLConnection.java",
    "org/ogplay/security/OgPlayJsseProvider.java",
    "org/ogplay/security/OgPlayCryptoProvider.java",
    "org/ogplay/security/OgPlayKeyManager.java",
    "org/ogplay/security/OgPlayKeyManagerFactorySpi.java",
    "org/ogplay/security/OgPlayKeyStoreProvider.java",
    "org/ogplay/security/OgPlaySslContextSpi.java",
    "org/ogplay/security/OgPlaySslSession.java",
    "org/ogplay/security/OgPlaySslSessionContext.java",
    "org/ogplay/security/OgPlaySslSocket.java",
    "org/ogplay/security/OgPlaySslSocketFactory.java",
    "org/ogplay/security/Pkcs12Kdf.java",
    "org/ogplay/security/Pkcs12AesSecretKeyFactory.java",
    "org/ogplay/security/PkixTrustManagerFactorySpi.java",
    "org/ogplay/security/TrustLimits.java",
    "org/ogplay/security/X509TrustManagerImpl.java",
)))
MATH_JAVA_ROOT = ROOT / "src/guest/math/java"
MATH_JAVA_NAMES = ("org/ogplay/math/NativeBigInteger.java",)

ZIP_JAVA_ROOT = ROOT / "src/guest/zip/java"
ZIP_JAVA_NAMES = ("org/ogplay/zip/NativeZip.java",)

IO_JAVA_ROOT = ROOT / "src/guest/io/java"
IO_JAVA_NAMES = ("org/ogplay/io/LogOutputStream.java",)

FRAMEWORK_JAVA_ROOT = ROOT / "src/guest/framework/java"
FRAMEWORK_JAVA_NAMES = (
    "android/app/Activity.java",
    "android/app/ActivityManager.java",
    "android/app/Notification.java",
    "android/app/NotificationManager.java",
    "android/hardware/LegacySensorManager.java",
    "android/hardware/LocalSensorManager.java",
    "android/hardware/Sensor.java",
    "android/hardware/SensorManager.java",
    "android/net/wifi/WifiManager.java",
    "android/nfc/NfcAdapter.java",
    "android/widget/VideoView.java",
    "com/google/android/gles_jni/GLImpl.java",
)


def guest_java_sources() -> tuple[Path, ...]:
    return tuple(JAVA_SOURCE_ROOT / name for name in JAVA_SOURCE_NAMES) + tuple(
        FRAMEWORK_JAVA_ROOT / name for name in FRAMEWORK_JAVA_NAMES) + tuple(
        ZIP_JAVA_ROOT / name for name in ZIP_JAVA_NAMES) + tuple(
        MATH_JAVA_ROOT / name for name in MATH_JAVA_NAMES) + tuple(
        IO_JAVA_ROOT / name for name in IO_JAVA_NAMES)


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
CATEGORIES = ("boot_dex", "existing_vm_intrinsic", "native_boundary", "deferred")
NATIVE_DISPOSITIONS = ("required_backend", "explicit_failure")
NATIVE_CRYPTO_OWNER = "Lcom/android/org/conscrypt/NativeCrypto;"
NATIVE_CRYPTO_ADAPTED = {f"{NATIVE_CRYPTO_OWNER}->clinit()V"}
NATIVE_CRYPTO_BACKENDS = {
    f"{NATIVE_CRYPTO_OWNER}->{member}" for member in (
        "EVP_get_cipherbyname(Ljava/lang/String;)J",
        "EVP_CIPHER_CTX_new()J", "EVP_CIPHER_CTX_cleanup(J)V",
        "EVP_CIPHER_CTX_block_size(J)I", "get_EVP_CIPHER_CTX_buf_len(J)I",
        "EVP_CIPHER_CTX_set_padding(JZ)V", "EVP_CIPHER_CTX_set_key_length(JI)V",
        "EVP_CIPHER_iv_length(J)I", "EVP_CipherInit_ex(JJ[B[BZ)V",
        "EVP_CipherUpdate(J[BI[BII)I", "EVP_CipherFinal_ex(J[BI)I",
        "EVP_get_digestbyname(Ljava/lang/String;)J", "EVP_MD_size(J)I",
        "EVP_DigestInit(J)J", "EVP_DigestUpdate(J[BII)V",
        "EVP_DigestFinal(J[BI)I", "EVP_MD_CTX_copy(J)J",
        "EVP_MD_CTX_create()J", "EVP_MD_CTX_init(J)V",
        "EVP_MD_CTX_destroy(J)V", "EVP_PKEY_new_mac_key(I[B)J",
        "EVP_PKEY_free(J)V", "EVP_DigestSignInit(JJJ)V",
        "EVP_DigestSignFinal(J)[B", "RAND_seed([B)V", "RAND_bytes([B)V",
    )
}


def source_path(source: str) -> Path:
    return AOSP / source

SOURCES = {
    "conscrypt.jar": ("platform/libcore",),
    "core.jar": ("platform/libcore",),
    "ext.jar": ("platform/external/apache-http",),
    "framework.jar": ("platform/frameworks/base",),
    "framework2.jar": ("platform/frameworks/base",),
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
            "api_level", "classes", "resources", "date_family_audit"}:
        raise BuildError(
            "recipe must contain api_level, classes, resources and date_family_audit")
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
    load_resources(document)
    return result


def load_resources(document: dict | None = None) -> dict[str, tuple[str, ...]]:
    if document is None:
        document = json.loads(RECIPE.read_text(encoding="utf-8"))
    resources = document.get("resources")
    if not isinstance(resources, dict) or not resources:
        raise BuildError("resources must be a non-empty object")
    result = {}
    for source, names in resources.items():
        if source not in SOURCES or not isinstance(names, list) or not names:
            raise BuildError(f"invalid resource list: {source}")
        values = tuple(names)
        if values != tuple(sorted(set(values))) or any(
                not isinstance(name, str) or not name or name.startswith("/") or
                "/../" in f"/{name}/" or "/./" in f"/{name}/"
                for name in values):
            raise BuildError(f"invalid resource list: {source}")
        result[source] = values
    return result


def verify_inputs(recipe: dict[str, tuple[str, ...]]) -> None:
    for source in recipe:
        path = source_path(source)
        if not path.is_file():
            raise BuildError(f"missing input: {path}")
    for name, (url, digest) in TOOLS.items():
        ensure_tool(SMALI / name, url, digest)


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


def compile_guest_java(work: Path) -> tuple[Path, tuple[str, ...]]:
    sources = guest_java_sources()
    observed = tuple(sorted(
        path.relative_to(JAVA_SOURCE_ROOT).as_posix()
        for path in JAVA_SOURCE_ROOT.rglob("*.java")))
    if observed != JAVA_SOURCE_NAMES:
        added = sorted(set(observed) - set(JAVA_SOURCE_NAMES))
        missing = sorted(set(JAVA_SOURCE_NAMES) - set(observed))
        details = []
        if added:
            details.append("added: " + ", ".join(added))
        if missing:
            details.append("missing: " + ", ".join(missing))
        raise BuildError("guest Java source list changed (" + "; ".join(details) + ")")
    framework_observed = tuple(sorted(
        path.relative_to(FRAMEWORK_JAVA_ROOT).as_posix()
        for path in FRAMEWORK_JAVA_ROOT.rglob("*.java")))
    if framework_observed != FRAMEWORK_JAVA_NAMES:
        raise BuildError("framework guest Java source list changed")
    io_observed = tuple(sorted(path.relative_to(IO_JAVA_ROOT).as_posix()
                              for path in IO_JAVA_ROOT.rglob("*.java")))
    if io_observed != IO_JAVA_NAMES:
        raise BuildError("IO guest Java source list changed")
    if not all(path.is_file() for path in sources) or not JAVAC.is_file() or \
            not JAVA.is_file() or not ANDROID_JAR.is_file() or not D8_JAR.is_file():
        raise BuildError("KeyStore Java toolchain or sources are missing")
    if file_sha256(ANDROID_JAR) != ANDROID_JAR_SHA256:
        raise BuildError("unexpected API 19 android.jar")
    if file_sha256(D8_JAR) != D8_JAR_SHA256:
        raise BuildError("unexpected Android build-tools 29.0.2 d8.jar")
    version = "javac " + javac_release()
    if not version.startswith("javac 17."):
        raise BuildError(f"unexpected javac: {version}")
    classes = work / "guest-java-classes"
    classes.mkdir()
    run([
        str(JAVAC), "-J-Duser.language=en", "-encoding", "UTF-8", "-source", "7", "-target", "7",
        "-Xlint:-options", "-g:none", "-bootclasspath", str(ANDROID_JAR),
        "-d", str(classes), *(str(source) for source in sources),
    ])
    class_files = tuple(sorted(classes.rglob("*.class")))
    if not class_files:
        raise BuildError("KeyStore Java compilation produced no classes")
    dex_dir = work / "guest-java-dex"
    dex_dir.mkdir()
    run([
        str(JAVA), "-cp", str(D8_JAR), "com.android.tools.r8.D8",
        "--min-api", "19", "--output", str(dex_dir),
        *(str(path) for path in class_files),
    ])
    dex_path = dex_dir / "classes.dex"
    dex = dex_path.read_bytes()
    descriptors = class_names(dex)
    if any(item.startswith("Lcom/android/org/bouncycastle/") for item in
           dex_survey_lib.parse_dex(dex).type_descriptors):
        raise BuildError("guest Java production code references BouncyCastle")
    destination = work / "guest-java-smali"
    run([
        str(JAVA), "-jar", str(SMALI / "baksmali.jar"), "disassemble",
        "--api", "19", "--jobs", "1", "--output", str(destination), str(dex_path),
    ])
    return destination, descriptors


def assemble(recipe: dict[str, tuple[str, ...]], work: Path,
             include_guest_java: bool = True) -> bytes:
    smali_roots = []
    for source, classes in recipe.items():
        destination = work / (source + "-smali")
        # Keep each command below the Windows command-line length limit.
        for offset in range(0, len(classes), 100):
            run([
                str(JAVA), "-jar", str(SMALI / "baksmali.jar"), "disassemble",
                "--api", "19", "--jobs", "1",
                "--classes", ",".join(classes[offset:offset + 100]),
                "--output", str(destination), str(source_path(source)),
            ])
        smali_roots.append(str(destination))
    if include_guest_java:
        destination, custom_classes = compile_guest_java(work)
        selected = {item for values in recipe.values() for item in values}
        duplicate = selected.intersection(custom_classes)
        if duplicate:
            raise BuildError(f"duplicate guest Java class: {sorted(duplicate)[0]}")
        smali_roots.append(str(destination))
    output = work / "classes.dex"
    run([
        str(JAVA), "-jar", str(SMALI / "smali.jar"), "assemble",
        "--api", "19", "--jobs", "1", "--output", str(output), *smali_roots,
    ])
    return output.read_bytes()


def class_names(dex: bytes) -> tuple[str, ...]:
    parsed = dex_survey_lib.parse_dex(dex)
    return tuple(sorted(parsed.type_name(item.type_index)
                        for item in parsed.classes))


def expected_class_names(
        recipe: dict[str, tuple[str, ...]],
        custom_classes: tuple[str, ...]) -> tuple[str, ...]:
    selected = tuple(item for values in recipe.values() for item in values)
    if len(custom_classes) != len(set(custom_classes)):
        raise BuildError("guest Java compilation contains duplicate classes")
    duplicate = set(selected).intersection(custom_classes)
    if duplicate:
        raise BuildError(f"duplicate guest Java class: {sorted(duplicate)[0]}")
    return tuple(sorted(selected + custom_classes))


def compile_expected_class_names(
        recipe: dict[str, tuple[str, ...]], work: Path) -> tuple[str, ...]:
    _, custom_classes = compile_guest_java(work)
    return expected_class_names(recipe, custom_classes)


def audit_crc32(dex_bytes: bytes) -> None:
    """Class-wide guest admission must not silently acquire new native methods."""
    dex = dex_survey_lib.parse_dex(dex_bytes)
    observed = {}
    for parsed_class in dex.classes:
        if dex.type_name(parsed_class.type_index) != "Ljava/util/zip/CRC32;":
            continue
        for method in parsed_class.direct_methods + parsed_class.virtual_methods:
            if method.access_flags & dex_survey_lib.ACC_NATIVE:
                _, name, descriptor = dex.method_signature(method.method_index)
                observed[name + descriptor] = method.access_flags
    # ACC_PRIVATE | ACC_NATIVE; neither method is static.
    expected = {"updateImpl([BIIJ)J": 0x102, "updateByteImpl(BJ)J": 0x102}
    if observed != expected:
        raise BuildError(f"CRC32 private instance native signature drift: {observed}")


def zip_stream_native_signatures() -> dict[str, dict[str, int]]:
    common = {"endImpl(J)V", "getAdlerImpl(J)I", "getTotalInImpl(J)J",
              "getTotalOutImpl(J)J", "resetImpl(J)V", "setDictionaryImpl([BIIJ)V",
              "setInputImpl([BIIJ)V"}
    return {
        "Landroid/app/Activity;": {
            "nativeStartActivityForResult(Landroid/content/Intent;ILandroid/os/Bundle;)V": 0x102,
            "nativeFinish(ILandroid/content/Intent;)V": 0x102},
        "Landroid/nfc/NfcAdapter;": {
            "nativeGetAdapter()Landroid/nfc/NfcAdapter;": 0x10a,
            "nativeUnsupported(Ljava/lang/String;)V": 0x10a},
        "Landroid/widget/VideoView;": {m: 0x102 for m in {
            "nativeOpenPath(Ljava/lang/String;I)V", "nativeOpenFd(Ljava/io/FileDescriptor;JJI)V",
            "nativeOpenError(ILjava/lang/String;)V", "nativeUnhandledError()V",
            "nativeUnsupported(Ljava/lang/String;)V", "nativeStart(I)V", "nativePause(I)V",
            "nativeStop(I)V", "nativeRelease(I)V", "nativeSeek(II)V", "nativeVolume(IFF)V",
            "nativeDuration(I)I", "nativePosition(I)I", "nativeWidth(I)I", "nativeHeight(I)I",
            "nativeIsPlaying(I)Z"}},
        "Lorg/ogplay/math/NativeBigInteger;": {
            **{m + "([B[B)[B": 0x109 for m in ("add", "multiply", "power")},
            "divide([B[B)[B": 0x109, "shift([BI)[B": 0x109,
            "multiplyWord([BI)[B": 0x109},
        "Ljava/util/zip/Deflater;": {m: 0x102 for m in common | {
            "createStream(IIZ)J", "deflateImpl([BIIJI)I", "setLevelsImpl(IIJ)V"}},
        "Ljava/util/zip/Inflater;": {m: 0x102 for m in common | {
            "createStream(Z)J", "inflateImpl([BIIJ)I",
            "setFileInputImpl(Ljava/io/FileDescriptor;JIJ)I"}},
        "Lorg/ogplay/zip/NativeZip;": {"release(J)V": 0x10a},
        "Landroid/app/NotificationManager;": {
            "nativeCancel(Ljava/lang/String;Ljava/lang/String;IZ)V": 0x102,
            "nativeRejectPost(Ljava/lang/String;)V": 0x102},
    }


def audit_zip_streams(dex_bytes: bytes) -> None:
    """Pin ZIP and value-arithmetic native ABIs, including the explicit ZIP FD gap."""
    expected = zip_stream_native_signatures()
    dex = dex_survey_lib.parse_dex(dex_bytes)
    observed = {owner: {} for owner in expected}
    for cls in dex.classes:
        owner = dex.type_name(cls.type_index)
        if owner not in observed:
            continue
        for method in cls.direct_methods + cls.virtual_methods:
            if method.access_flags & dex_survey_lib.ACC_NATIVE:
                _, name, descriptor = dex.method_signature(method.method_index)
                observed[owner][name + descriptor] = method.access_flags
    if observed != expected:
        raise BuildError(f"ZIP stream native signature drift: {observed}")


def audit_native_crypto(dex_bytes: bytes) -> dict:
    dex = dex_survey_lib.parse_dex(dex_bytes)
    observed = set()
    for parsed_class in dex.classes:
        if dex.type_name(parsed_class.type_index) != NATIVE_CRYPTO_OWNER:
            continue
        for method in parsed_class.direct_methods + parsed_class.virtual_methods:
            if method.access_flags & dex_survey_lib.ACC_NATIVE:
                owner, name, descriptor = dex.method_signature(method.method_index)
                observed.add(f"{owner}->{name}{descriptor}")
    required = NATIVE_CRYPTO_BACKENDS | NATIVE_CRYPTO_ADAPTED
    missing = sorted(required.difference(observed))
    if missing:
        raise BuildError(f"NativeCrypto backend signature drift: missing={missing}")
    records = {
        "source": "conscrypt.jar",
        "owner": NATIVE_CRYPTO_OWNER,
        "native_methods": {
            "existing_backend": sorted(NATIVE_CRYPTO_BACKENDS),
            "ogplay_initialization_adapter": sorted(NATIVE_CRYPTO_ADAPTED),
            "explicit_failure": sorted(observed.difference(required)),
        },
        "counts": {
            "total": len(observed),
            "existing_backend": len(NATIVE_CRYPTO_BACKENDS),
            "ogplay_initialization_adapter": len(NATIVE_CRYPTO_ADAPTED),
            "explicit_failure": len(observed.difference(required)),
        },
    }
    NATIVE_CRYPTO_AUDIT_REPORT.parent.mkdir(parents=True, exist_ok=True)
    NATIVE_CRYPTO_AUDIT_REPORT.write_text(
        json.dumps(records, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return records


def make_jar(dex: bytes, resources: dict[str, bytes] | None = None) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        entries = [
                ("META-INF/MANIFEST.MF", JAR_MANIFEST),
                ("classes.dex", dex)]
        entries.extend(sorted((resources or {}).items()))
        for name, content in entries:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    return output.getvalue()


def resource_payload(
        recipe: dict[str, tuple[str, ...]]) -> dict[str, bytes]:
    resources = {}
    for source, names in recipe.items():
        with zipfile.ZipFile(source_path(source)) as archive:
            for name in names:
                try:
                    content = archive.read(name)
                except KeyError as error:
                    raise BuildError(
                        f"missing resource {name} in {source}") from error
                if name in resources:
                    raise BuildError(f"duplicate resource: {name}")
                resources[name] = content
    return resources


def load_system_resources(document: dict | None = None) -> dict:
    if document is None:
        document = json.loads(SYSTEM_RESOURCES.read_text(encoding="utf-8"))
    if document.get("api_level") != 19 or \
            document.get("framework", {}).get("jar") != "framework.jar":
        raise BuildError("invalid system resource source identity")
    entries = document.get("resources", [])
    if not entries:
        raise BuildError("system resource table is empty")
    names, consumers = set(), set()
    for entry in entries:
        name, kind = entry.get("name", ""), entry.get("type")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name) or \
                kind not in ("string", "integer") or \
                name in names:
            raise BuildError("invalid or duplicate system resource")
        names.add(name)
        values = [entry.get("aosp_default")]
        if "override" in entry:
            if not entry["override"].get("reason", "").strip():
                raise BuildError("system resource override requires a reason")
            values.append(entry["override"].get("value"))
        for value in values:
            if (kind == "string" and (not isinstance(value, str) or "\0" in value)) or \
                    (kind == "integer" and (type(value) is not int or
                                           not -(2**31) <= value < 2**31)):
                raise BuildError("system resource value has wrong type or range")
        consumer = entry.get("consumer", {})
        if consumer.get("class") not in SYSTEM_RESOURCE_CONSUMERS or \
                not re.fullmatch(r"(?:[A-Za-z]+|<clinit>)\(\)(?:I|V|Ljava/lang/String;)",
                                 consumer.get("method", "")):
            raise BuildError("invalid system resource consumer")
        identity = (consumer["class"], consumer["method"])
        if identity in consumers:
            raise BuildError("duplicate system resource consumer")
        consumers.add(identity)
    return document


def resource_call_ids(body: str) -> list[tuple[int, str]]:
    # Conservative straight-line propagation. Branches and labels discard facts;
    # an ID crossing a control-flow join must be re-established or is rejected.
    registers: dict[str, int] = {}
    calls = []
    for raw in body.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.startswith("."):
            continue
        if line.startswith(":") or re.match(r"(?:if-|goto|packed-switch|sparse-switch)", line):
            registers.clear()
            continue
        lookup = re.search(r"Landroid/content/res/Resources;->(getString|getInteger)\(I\)", line)
        if lookup:
            args = re.search(r"\{([^}]+)\}", line)
            if not args:
                raise BuildError("system resource call has no arguments")
            argument_text = args[1].strip()
            if ".." in argument_text:
                span = re.fullmatch(r"([vp])(\d+) \.\. ([vp])(\d+)", argument_text)
                if not span or span[1] != span[3] or int(span[4]) != int(span[2]) + 1:
                    raise BuildError("unsupported system resource invoke range")
                arguments = [span[1] + span[2], span[3] + span[4]]
            else:
                arguments = [value.strip() for value in argument_text.split(",")]
            if len(arguments) != 2 or arguments[1] not in registers:
                raise BuildError("system resource ID is not a proven constant")
            calls.append((registers[arguments[1]], "string" if lookup[1] == "getString" else "integer"))
        constant = re.fullmatch(r"const(?:/4|/16|/high16)? ([vp]\d+), (-?0x[0-9a-f]+|-?\d+)", line)
        move = re.fullmatch(r"move(?:/from16|/16)? ([vp]\d+), ([vp]\d+)", line)
        if constant:
            # baksmali writes the expanded value for const/high16.
            registers[constant[1]] = int(constant[2], 0) & 0xffffffff
        elif move:
            value = registers.get(move[2])
            registers.pop(move[1], None)
            if value is not None:
                registers[move[1]] = value
        elif not line.startswith(("invoke-", "return", "throw", "iput", "sput", "aput", "monitor-")):
            destination = re.match(r"\S+ ([vp]\d+)", line)
            if destination:
                registers.pop(destination[1], None)
                # Wide writes also invalidate the adjacent register.
                if "wide" in line.split()[0]:
                    registers.pop(destination[1][0] + str(int(destination[1][1:]) + 1), None)
    return calls


def resolve_system_resource_contents(document: dict, consumers: dict[str, str]) -> dict:
    declared = {(e["consumer"]["class"], e["consumer"]["method"]) for e in document["resources"]}
    methods = {}
    observed = set()
    for owner, text in consumers.items():
        for signature, body in re.findall(r"^\.method [^\n]* ([^ \n]+)\n(.*?)^\.end method", text, re.M | re.S):
            methods[(owner, signature)] = body
            if re.search(r"Landroid/content/res/Resources;->get(?:String|Integer)\(I\)", body):
                observed.add((owner, signature))
    if observed != declared:
        raise BuildError("system resource consumer coverage differs from reviewed table: "
                         f"missing={sorted(declared - observed)}, extra={sorted(observed - declared)}")
    resources, ids = [], set()
    for entry in document["resources"]:
        consumer = entry["consumer"]
        try:
            calls = resource_call_ids(methods[(consumer["class"], consumer["method"])])
        except BuildError as error:
            raise BuildError(f"{consumer['class']}->{consumer['method']}: {error}") from error
        if len(calls) != 1 or calls[0][1] != entry["type"]:
            raise BuildError(f"system resource call/type mismatch: {entry['name']}")
        resource_id = calls[0][0]
        if resource_id >> 24 != 1 or resource_id in ids:
            raise BuildError(f"invalid or conflicting system resource ID: {entry['name']}")
        ids.add(resource_id)
        resources.append({"id": resource_id, "name": entry["name"], "type": entry["type"],
                          "value": entry.get("override", {}).get("value", entry["aosp_default"])})
    return {"schema_version": 1, "api_level": 19, "resources": resources}


def system_resources_payload(dex: bytes) -> bytes:
    document = load_system_resources()
    with tempfile.TemporaryDirectory(prefix="ogplay-system-resources-") as work:
        source = Path(work) / "classes.dex"
        source.write_bytes(dex)
        output = Path(work) / "smali"
        run([str(JAVA), "-jar", str(SMALI / "baksmali.jar"), "disassemble",
             "--api", "19", "--jobs", "1", "--classes", ",".join(SYSTEM_RESOURCE_CONSUMERS),
             "--output", str(output), str(source)])
        consumers = {owner: (output / (owner[1:-1] + ".smali")).read_text(encoding="utf-8")
                     for owner in SYSTEM_RESOURCE_CONSUMERS}
    resolved = resolve_system_resource_contents(document, consumers)
    return (json.dumps(resolved, sort_keys=True, indent=2) + "\n").encode("utf-8")


def build() -> tuple[bytes, bytes, dict[str, tuple[str, ...]]]:
    recipe = load_recipe()
    resource_recipe = load_resources()
    verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix="ogplay-bootdex-a-") as first, \
            tempfile.TemporaryDirectory(prefix="ogplay-bootdex-b-") as second:
        dex = assemble(recipe, Path(first))
        if dex != assemble(recipe, Path(second)):
            raise BuildError("two BootDex assemblies differ")
    with tempfile.TemporaryDirectory(prefix="ogplay-bootdex-java-audit-") as work:
        selected = compile_expected_class_names(recipe, Path(work))
    if class_names(dex) != selected:
        raise BuildError("generated classes differ from recipe")
    resources = resource_payload(resource_recipe)
    if SYSTEM_RESOURCES_ENTRY in resources:
        raise BuildError("reserved system resource entry in input")
    resources[SYSTEM_RESOURCES_ENTRY] = system_resources_payload(dex)
    return make_jar(dex, resources), dex, recipe


def boot_metadata(jar: bytes, dex: bytes,
                  recipe: dict[str, tuple[str, ...]]) -> dict:
    return {
        "path": "framework/bootdex.jar",
        "generator": "tools/bootdex/build_bootdex.py",
        "recipe": "tools/bootdex/api19.json",
        "recipe_sha256": file_sha256(RECIPE),
        "system_resources": {
            "table": SYSTEM_RESOURCES.relative_to(ROOT).as_posix(),
            "table_sha256": file_sha256(SYSTEM_RESOURCES),
            "entry": SYSTEM_RESOURCES_ENTRY,
            "payload_sha256": sha256(zipfile.ZipFile(io.BytesIO(jar)).read(SYSTEM_RESOURCES_ENTRY)),
        },
        "sources": [
            {
                "source_project": SOURCES[source][0],
                "source_jar": source,
                "source_jar_sha256": file_sha256(source_path(source)),
            }
            for source in recipe
        ] + [{
            "source_project": "OGPlay",
            "source_root": "src/guest",
            "source_roots": ["src/guest/crypto/java", "src/guest/framework/java", "src/guest/io/java"],
            "source_sha256": sha256(b"".join(
                path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0" +
                path.read_bytes() for path in
                guest_java_sources())),
            "javac": javac_release(),
            "android_jar_sha256": ANDROID_JAR_SHA256,
            "d8_sha256": D8_JAR_SHA256,
        }],
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
        return assemble(recipe, Path(work), include_guest_java=False)


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
        "source_sha256": file_sha256(source_path(policy["source"])),
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
    # Native admission mutations need neither AOSP inputs nor Java tooling.
    from types import SimpleNamespace
    for target_index in (0, 1, 2, 3, 4, 7):  # Activity, NFC, video, value arithmetic, ZIP and notification admission.
        for mutation in ("valid", "signature", "flags", "extra", "missing"):
            names, signatures, classes = [], [], []
            for owner, members in zip_stream_native_signatures().items():
                names.append(owner)
                methods = []
                for signature, flags in sorted(members.items()):
                    name, descriptor = signature.split("(", 1)
                    methods.append(SimpleNamespace(method_index=len(signatures), access_flags=flags))
                    signatures.append((owner, name, "(" + descriptor))
                classes.append(SimpleNamespace(type_index=len(names)-1,
                    direct_methods=methods, virtual_methods=[]))
            native = classes[target_index].direct_methods[0]
            if mutation == "flags":
                native.access_flags ^= 8
            elif mutation == "missing":
                classes[target_index].direct_methods.remove(native)
            elif mutation == "extra":
                classes[target_index].direct_methods.append(SimpleNamespace(method_index=len(signatures), access_flags=0x102))
                signatures.append((names[target_index], "unreviewedZipNative", "()V"))
            elif mutation == "signature":
                owner, _, descriptor = signatures[native.method_index]
                signatures[native.method_index] = (owner, "unreviewedZipNative", descriptor)
            parsed = SimpleNamespace(classes=classes, type_name=lambda i: names[i],
                                     method_signature=lambda i: signatures[i])
            with patch.object(dex_survey_lib, "parse_dex", return_value=parsed):
                try:
                    audit_zip_streams(b"")
                except BuildError:
                    if mutation == "valid":
                        raise
                else:
                    if mutation != "valid":
                        raise BuildError(f"ZIP native drift accepted: {mutation}")
    resources = load_system_resources()
    # Mutation checks need neither local framework inputs nor Java tooling.
    sample_resource = json.loads(json.dumps(resources))
    sample_resource["resources"] = [sample_resource["resources"][0]]
    owner = sample_resource["resources"][0]["consumer"]["class"]
    consumer = """.method public static getDefaultJournalMode()Ljava/lang/String;
    .registers 3
    const v2, 0x104011c
    .line 77
    invoke-virtual {v1, v2}, Landroid/content/res/Resources;->getString(I)Ljava/lang/String;
.end method
"""
    for resource_id in (0x0104011c, 0x01040029):
        text = consumer.replace("0x104011c", hex(resource_id))
        resolved = resolve_system_resource_contents(sample_resource, {owner: text})
        assert resolved["resources"][0]["id"] == resource_id
        assert resolved["resources"][0]["value"] == "DELETE"
    moved = consumer.replace("const v2, 0x104011c", "const v0, 0x1040029\n    move/from16 v2, v0")
    assert resolve_system_resource_contents(sample_resource, {owner: moved})["resources"][0]["id"] == 0x01040029
    ranged = consumer.replace("invoke-virtual {v1, v2}", "invoke-virtual/range {v1 .. v2}")
    assert resolve_system_resource_contents(sample_resource, {owner: ranged})["resources"][0]["id"] == 0x0104011c
    for case in ("ID", "unknown ID", "branch", "type",
                 "method", "register", "duplicate", "override reason", "unreviewed query"):
        changed = json.loads(json.dumps(sample_resource))
        entry = changed["resources"][0]
        text = consumer
        if case == "ID":
            text = text.replace("0x104011c", "0x7f040029")
        elif case == "unknown ID":
            text = text.replace("const v2, 0x104011c", "move-result v2")
        elif case == "branch":
            text = text.replace(".line 77", ":join")
        elif case == "type":
            entry["type"] = "integer"
        elif case == "method":
            entry["consumer"]["method"] = "missing()I"
        elif case == "register":
            text = text.replace("{v1, v2}", "{v1, v0}")
        elif case == "duplicate":
            changed["resources"].append(entry)
        elif case == "unreviewed query":
            text += consumer.replace("getDefaultJournalMode", "getNewConfig")
        else:
            entry["override"]["reason"] = ""
        try:
            load_system_resources(changed)
            resolve_system_resource_contents(changed, {owner: text})
        except BuildError:
            continue
        raise BuildError(f"invalid system resource mapping accepted: {case}")
    collision = json.loads(json.dumps(sample_resource))
    extra = json.loads(json.dumps(collision["resources"][0]))
    extra["name"] = "second_resource"
    extra["consumer"]["method"] = "getOtherMode()Ljava/lang/String;"
    collision["resources"].append(extra)
    try:
        resolve_system_resource_contents(collision, {owner: consumer + consumer.replace("getDefaultJournalMode", "getOtherMode")})
    except BuildError:
        pass
    else:
        raise BuildError("conflicting resource IDs accepted")
    wrong_type = consumer.replace("getString(I)Ljava/lang/String;", "getInteger(I)I")
    try:
        resolve_system_resource_contents(sample_resource, {owner: wrong_type})
    except BuildError:
        pass
    else:
        raise BuildError("consumer type mismatch accepted")
    sample = b"dex\n035\0sample"
    if make_jar(sample) != make_jar(sample):
        raise BuildError("JAR output is not deterministic")
    recipe_classes = {"source.jar": ("Ltest/Original;",)}
    expected = expected_class_names(recipe_classes, ("Ltest/Custom;",))
    if expected != ("Ltest/Custom;", "Ltest/Original;") or \
            expected_class_names(recipe_classes, ()) == expected or \
            expected_class_names(recipe_classes,
                                 ("Ltest/Custom;", "Ltest/Extra;")) == expected:
        raise BuildError("final BootDex class selection does not include exact custom classes")
    try:
        expected_class_names(recipe_classes, ("Ltest/Original;",))
    except BuildError:
        pass
    else:
        raise BuildError("custom/original duplicate class was accepted")
    with tempfile.TemporaryDirectory(prefix="ogplay-bootdex-tool-") as work:
        source = Path(work) / "source.jar"
        destination = Path(work) / "downloaded.jar"
        source.write_bytes(sample)
        ensure_tool(destination, source.as_uri(), sha256(sample))
        if destination.read_bytes() != sample:
            raise BuildError("tool download failed")
        with patch.dict(globals(), {
                "AOSP": Path(work),
                "SOURCES": {"source.jar": ("test",)},
                "TOOLS": {},
        }):
            verify_inputs({"source.jar": ()})
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
        audit_crc32(dex)
        audit_zip_streams(dex)
        native_crypto = audit_native_crypto(dex)
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
        print(f"NativeCrypto audit: {native_crypto['counts']}; "
              f"report={NATIVE_CRYPTO_AUDIT_REPORT}")
        return 0
    except (BuildError, OSError, json.JSONDecodeError,
            dex_survey_lib.DexFormatError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
