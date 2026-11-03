#!/usr/bin/env python3
"""Build, package, sign, and verify the sweetshop match-3 sample APK."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import assets  # noqa: E402


def version_key(path: Path) -> tuple[int, ...]:
    return tuple(int(value) for value in re.findall(r"\d+", path.name))


def newest_directory(parent: Path) -> Path:
    choices = [path for path in parent.iterdir() if path.is_dir()]
    if not choices:
        raise RuntimeError(f"no installed versions under {parent}")
    return max(choices, key=version_key)


def require(path: Path, description: str) -> Path:
    if not path.exists():
        raise RuntimeError(f"{description} was not found: {path}")
    return path


def run(arguments: list[os.PathLike[str] | str], *, echo: bool = True) -> str:
    command = [str(value) for value in arguments]
    completed = subprocess.run(command, check=False, text=True, encoding="utf-8",
                               errors="replace", stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    if completed.stdout and echo:
        print(completed.stdout, end="")
    if completed.returncode != 0:
        if not echo:
            print(completed.stdout, end="")
        raise subprocess.CalledProcessError(completed.returncode, command,
                                            output=completed.stdout)
    return completed.stdout


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--abi", default="armeabi-v7a")
    parser.add_argument("--api", type=int, default=19)
    parser.add_argument("--build-tools", dest="build_tools_version")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--assets-only", action="store_true",
                        help="only regenerate runtime art; needs no SDK or NDK")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    sample_root = Path(__file__).resolve().parents[1]
    repository_root = sample_root.parents[1]
    output = (arguments.output or repository_root / "out" / "sweetshop").resolve()
    generated = output / "generated"
    if generated.exists():
        shutil.rmtree(generated)
    assets.build(sample_root / "art" / "source", generated / "assets", generated / "res",
                 repository_root / "data" / "android" / "19" / "fonts",
                 repository_root / "assets" / "icons" / "ogplay.png")
    if arguments.assets_only:
        print(f"assets: {generated}")
        return 0
    if arguments.sdk is None or arguments.ndk is None:
        raise RuntimeError("provide --sdk and --ndk or set Android SDK/NDK environment variables")

    sdk = require(arguments.sdk.resolve(), "Android SDK")
    ndk = require(arguments.ndk.resolve(), "Android NDK")
    cmake_install = newest_directory(require(sdk / "cmake", "SDK CMake directory"))
    build_tools = (sdk / "build-tools" / arguments.build_tools_version
                   if arguments.build_tools_version else
                   newest_directory(require(sdk / "build-tools", "SDK build-tools directory")))
    platform = require(sdk / "platforms" / f"android-{arguments.api}" / "android.jar",
                       "Android platform jar")

    windows = os.name == "nt"
    executable_suffix = ".exe" if windows else ""
    batch_suffix = ".bat" if windows else ""
    cmake = require(cmake_install / "bin" / f"cmake{executable_suffix}", "CMake")
    ninja = require(cmake_install / "bin" / f"ninja{executable_suffix}", "Ninja")
    aapt2 = require(build_tools / f"aapt2{executable_suffix}", "aapt2")
    zipalign = require(build_tools / f"zipalign{executable_suffix}", "zipalign")
    apksigner = require(build_tools / f"apksigner{batch_suffix}", "apksigner")
    d8 = require(build_tools / f"d8{batch_suffix}", "d8")
    javac = shutil.which(f"javac{executable_suffix}")
    keytool = shutil.which(f"keytool{executable_suffix}")
    if javac is None or keytool is None:
        raise RuntimeError("a JDK with javac and keytool is required")
    toolchain = require(ndk / "build" / "cmake" / "android.toolchain.cmake", "NDK CMake toolchain")
    hosts = [path for path in require(ndk / "toolchains" / "llvm" / "prebuilt", "NDK LLVM tools").iterdir()
             if path.is_dir()]
    if not hosts:
        raise RuntimeError("no NDK LLVM host toolchain")
    readelf = require(hosts[0] / "bin" / f"llvm-readelf{executable_suffix}", "llvm-readelf")
    strip = require(hosts[0] / "bin" / f"llvm-strip{executable_suffix}", "llvm-strip")

    build = output / "build" / arguments.abi
    package = output / "package"
    for folder in (build, package):
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir(parents=True)

    run([cmake, "-S", sample_root, "-B", build, "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         f"-DANDROID_ABI={arguments.abi}", f"-DANDROID_PLATFORM=android-{arguments.api}",
         "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release"], echo=False)
    run([cmake, "--build", build], echo=False)
    library = require(build / "libsweetshop.so", "sweetshop native library")
    header = run([readelf, "-h", library], echo=False)
    symbols = run([readelf, "--dyn-syms", "--wide", library], echo=False)
    if "ELF32" not in header or "ARM" not in header:
        raise RuntimeError("sweetshop library is not an ELF32 ARM binary")
    if "ANativeActivity_onCreate" not in symbols:
        raise RuntimeError("sweetshop library does not export ANativeActivity_onCreate")

    classes = package / "classes"
    classes.mkdir()
    java_sources = sorted(str(path) for path in (sample_root / "src" / "main" / "java").rglob("*.java"))
    run([javac, "-source", "8", "-target", "8", "-bootclasspath", platform, "-nowarn",
         "-Xlint:-options", "-d", classes, *java_sources], echo=False)
    dex_dir = package / "dex"
    dex_dir.mkdir()
    run([d8, "--release", "--min-api", str(arguments.api), "--lib", platform,
         "--output", dex_dir, *sorted(str(path) for path in classes.rglob("*.class"))], echo=False)
    dex = require(dex_dir / "classes.dex", "classes.dex")

    resources = package / "res"
    shutil.copytree(sample_root / "res", resources)
    shutil.copytree(generated / "res", resources, dirs_exist_ok=True)
    compiled = package / "compiled.zip"
    run([aapt2, "compile", "--dir", resources, "-o", compiled], echo=False)
    unsigned_apk = package / "unsigned.apk"
    run([aapt2, "link", "-o", unsigned_apk, "-I", platform, "--manifest", sample_root / "AndroidManifest.xml",
         "-A", generated / "assets", "--min-sdk-version", str(arguments.api),
         "--target-sdk-version", str(arguments.api), "--version-code", "1", "--version-name", "1.0",
         compiled], echo=False)
    # Ship without debug sections, as Gradle does; the unstripped copy stays in build/.
    stripped = package / library.name
    run([strip, "--strip-unneeded", library, "-o", stripped], echo=False)
    packed_apk = package / "packed.apk"
    shutil.copy2(unsigned_apk, packed_apk)
    with zipfile.ZipFile(packed_apk, "a") as archive:
        archive.write(dex, "classes.dex", compress_type=zipfile.ZIP_DEFLATED)
        archive.write(stripped, f"lib/{arguments.abi}/{library.name}", compress_type=zipfile.ZIP_DEFLATED)
    aligned_apk = package / "aligned.apk"
    run([zipalign, "-f", "-p", "4", packed_apk, aligned_apk], echo=False)

    keystore = output / "debug.keystore"
    if not keystore.exists():
        run([keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android",
             "-alias", "androiddebugkey", "-keypass", "android",
             "-dname", "CN=OGPlay Sample,O=OGPlay,C=US", "-keyalg", "RSA",
             "-keysize", "2048", "-validity", "10000", "-noprompt"], echo=False)
    final_apk = output / "ogplay-sweetshop.apk"
    run([apksigner, "sign", "--ks", keystore, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", final_apk, aligned_apk], echo=False)
    run([apksigner, "verify", final_apk], echo=False)
    run([zipalign, "-c", "-p", "4", final_apk], echo=False)
    badging = run([aapt2, "dump", "badging", final_apk], echo=False)
    for marker in ("package: name='org.ogplay.sweetshop'", f"native-code: '{arguments.abi}'",
                   "launchable-activity: name='org.ogplay.sweetshop.SweetShopActivity'"):
        if marker not in badging:
            raise RuntimeError(f"APK metadata is missing {marker}")
    with zipfile.ZipFile(final_apk) as archive:
        names = set(archive.namelist())
    for entry in ("classes.dex", f"lib/{arguments.abi}/libsweetshop.so", "assets/atlas.png", "assets/text.png",
                  "assets/atlas.txt", "assets/background.jpg", "assets/logo.jpg", "assets/logo_alpha.png",
                  "assets/title_zh.jpg", "assets/title_zh_alpha.png",
                  "assets/title_en.jpg", "assets/title_en_alpha.png"):
        if entry not in names:
            raise RuntimeError(f"packaged APK does not contain {entry}")

    print(f"APK: {final_apk}")
    print(f"Size: {final_apk.stat().st_size} bytes")
    print(f"SHA-256: {hashlib.sha256(final_apk.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"build failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
