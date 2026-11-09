#!/usr/bin/env python3
"""Build and check the API 19 boot classpath payload.

The ROM jars are copied unmodified from .local/aosp. OGPlay's own guest Java is
compiled into ogplay.jar, which comes first on the boot classpath, together with
the system resource table and the theme projection. The same script builds the
guest JNI library (build-guest-jni).
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
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

SYSTEM_RESOURCES = ROOT / "tools/bootdex/api19-system-resources.json"
SYSTEM_RESOURCES_ENTRY = "META-INF/ogplay/system-resources.json"
SYSTEM_RESOURCE_CONSUMERS = (
    "Landroid/database/CursorWindow;", "Landroid/database/sqlite/SQLiteGlobal;",
)
AOSP = ROOT / ".local/aosp"
SMALI = ROOT / ".local/tools"
FRAMEWORK = ROOT / "data/android/19/framework"
OGPLAY_JAR = FRAMEWORK / "ogplay.jar"
MANIFEST = ROOT / "data/android/19/manifest.json"
NOTICE = ROOT / "data/android/19/notices/boot-classpath.txt"
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
    "android.policy.jar": ("platform/frameworks/base",
                           "660fa192659bf02285cfeb559d3fad547a72d4986aa295ef5a9453b2b2372ad1"),
}

# Hidden API 19 types are absent from the public SDK android.jar. These
# declarations only satisfy javac; the ROM jars supply the real classes.
COMPILE_ONLY_SOURCES = {
    "android/hardware/usb/IUsbManager.java":
        "package android.hardware.usb; public interface IUsbManager {}",
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
# The SDK hides ActivityManager.RunningAppProcessInfo from javac; the overlay
# ActivityManager compiles against this nested shape and the ROM supplies it.
ACTIVITY_MANAGER_NESTED = """
    public static class RunningAppProcessInfo {
        public static final int REASON_UNKNOWN = 0;
        public int uid, flags, importance, lru, lastTrimLevel;
        public int importanceReasonCode, importanceReasonPid;
        public android.content.ComponentName importanceReasonComponent;
        public RunningAppProcessInfo(String name, int pid, String[] packages) {
            throw new AssertionError();
        }
    }
}
"""
COMPILE_ONLY_CLASSES = tuple(sorted(
    [name[:-len(".java")] + ".class" for name in COMPILE_ONLY_SOURCES]
    + ["android/app/ActivityManager$RunningAppProcessInfo.class"]))

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

SMALI_TOOLS = {
    "baksmali.jar": (
        "https://github.com/baksmali/smali/releases/download/3.0.10/"
        "baksmali-3.0.10-fat-release.jar",
        "37ae4a41a8886e15c20b8362fa4250f96bbdb55e1a608199ad8b5dff068b588f"),
}
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
    for name, (url, digest) in SMALI_TOOLS.items():
        ensure_tool(SMALI / name, url, digest)


def compile_guest_java(work: Path) -> bytes:
    sources = guest_java_sources()
    compile_only = work / "compile-only"
    for name, text in COMPILE_ONLY_SOURCES.items():
        path = compile_only / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    manager = ROOT / "src/guest/framework/java/android/app/ActivityManager.java"
    manager_compile = work / "activity-manager/android/app/ActivityManager.java"
    manager_compile.parent.mkdir(parents=True, exist_ok=True)
    text = manager.read_text(encoding="utf-8")
    manager_compile.write_text(text[:text.rfind("}")] + ACTIVITY_MANAGER_NESTED, encoding="utf-8")
    sources = tuple(manager_compile if source == manager else source for source in sources)
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


def rom_smali(jar: str, classes: tuple[str, ...]) -> dict[str, str]:
    with tempfile.TemporaryDirectory(prefix="ogplay-rom-smali-") as work:
        output = Path(work) / "smali"
        run([str(JAVA), "-jar", str(SMALI / "baksmali.jar"), "disassemble", "--api", "19", "--jobs", "1",
             "--classes", ",".join(classes), "--output", str(output), str(AOSP / jar)])
        return {owner: (output / (owner[1:-1] + ".smali")).read_text(encoding="utf-8") for owner in classes}


def time_system_resources(smali: str) -> list[dict]:
    # API 19 Time.format resolves three resource IDs in this order.
    formats = (
        ("time_of_day", "%-l:%M:%S %p"),
        ("month_day_year", "%B %-e, %Y"),
        ("date_and_time", "%b %-e, %Y, %-l:%M:%S %p"),
    )
    match = re.search(r"^\.method [^\n]* format\(Ljava/lang/String;\)Ljava/lang/String;\n"
                      r"(.*?)^\.end method", smali, re.M | re.S)
    if match is None:
        raise BuildError("API 19 Time.format is missing")
    calls = resource_call_ids(match[1])
    if len(calls) != len(formats) or any(kind != "string" or ident >> 24 != 1 for ident, kind in calls):
        raise BuildError("API 19 Time.format system resource lookups changed")
    return [dict(id=ident, name=name, type="string", value=value)
            for (ident, _), (name, value) in zip(calls, formats)]


def system_resources_payload() -> bytes:
    smali = rom_smali("framework.jar", (*SYSTEM_RESOURCE_CONSUMERS, "Landroid/text/format/Time;"))
    resolved = resolve_system_resource_contents(
        load_system_resources(), {owner: smali[owner] for owner in SYSTEM_RESOURCE_CONSUMERS})
    resolved["resources"].extend(time_system_resources(smali["Landroid/text/format/Time;"]))
    if len({row["id"] for row in resolved["resources"]}) != len(resolved["resources"]):
        raise BuildError("conflicting system resource ID")
    return (json.dumps(resolved, sort_keys=True, indent=2) + "\n").encode("utf-8")


def make_jar(dex: bytes, resources: dict[str, bytes]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, content in [("META-INF/MANIFEST.MF", JAR_MANIFEST), ("classes.dex", dex),
                              *sorted(resources.items())]:
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    return output.getvalue()


def build_ogplay_jar() -> tuple[bytes, dict[str, bytes]]:
    import theme_projection
    check_toolchain()
    with tempfile.TemporaryDirectory(prefix="ogplay-jar-a-") as first, \
            tempfile.TemporaryDirectory(prefix="ogplay-jar-b-") as second:
        dex = compile_guest_java(Path(first))
        if dex != compile_guest_java(Path(second)):
            raise BuildError("two guest Java builds differ")
    system = system_resources_payload()
    projection = theme_projection.generate()
    theme_projection.check_conflicts(projection, system)
    resources = {SYSTEM_RESOURCES_ENTRY: system, theme_projection.ENTRY: projection}
    return make_jar(dex, resources), resources


def rom_jar(name: str) -> bytes:
    data = (AOSP / name).read_bytes()
    if sha256(data) != ROM_JARS[name][1]:
        raise BuildError(f"unexpected ROM input: .local/aosp/{name}")
    return data


def boot_metadata(jar: bytes, resources: dict[str, bytes]) -> dict:
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
                 "d8_sha256": D8_JAR_SHA256},
             "resources": {
                 name: sha256(content) for name, content in sorted(resources.items())},
             "system_resources_table_sha256": file_sha256(SYSTEM_RESOURCES)},
            *({"path": f"framework/{name}", "source_project": project, "size": (AOSP / name).stat().st_size,
               "sha256": digest} for name, (project, digest) in ROM_JARS.items()),
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
    jar, resources = build_ogplay_jar()
    roms = {name: rom_jar(name) for name in ROM_JARS}
    manifest = manifest_bytes(boot_metadata(jar, resources))
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
