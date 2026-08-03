#!/usr/bin/env python3
"""Build, package, sign, and verify the OGPlay minimal NativeActivity APK."""

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


def version_key(path: Path) -> tuple[int, ...]:
    numbers = re.findall(r"\d+", path.name)
    return tuple(int(value) for value in numbers)


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
        console_encoding = sys.stdout.encoding or "utf-8"
        printable = completed.stdout.encode(console_encoding, errors="replace").decode(
            console_encoding)
        print(printable, end="")
    if completed.returncode != 0:
        raise subprocess.CalledProcessError(completed.returncode, command,
                                            output=completed.stdout)
    return completed.stdout


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path,
                        default=os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path,
                        default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--abi", default="armeabi-v7a")
    parser.add_argument("--api", type=int, default=19)
    parser.add_argument("--build-tools", dest="build_tools_version")
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.sdk is None or arguments.ndk is None:
        raise RuntimeError("provide --sdk and --ndk or set ANDROID_SDK_ROOT and ANDROID_NDK_ROOT")

    sample_root = Path(__file__).resolve().parents[1]
    repository_root = sample_root.parents[1]
    sdk = require(arguments.sdk.resolve(), "Android SDK")
    ndk = require(arguments.ndk.resolve(), "Android NDK")
    output = (arguments.output or repository_root / "out" / "minimal-ndk").resolve()
    cmake_install = newest_directory(require(sdk / "cmake", "SDK CMake directory"))
    build_tools = (sdk / "build-tools" / arguments.build_tools_version
                   if arguments.build_tools_version else
                   newest_directory(require(sdk / "build-tools", "SDK build-tools directory")))
    platform = require(sdk / "platforms" / f"android-{arguments.api}" / "android.jar",
                       "Android platform jar")

    executable_suffix = ".exe" if os.name == "nt" else ""
    batch_suffix = ".bat" if os.name == "nt" else ""
    cmake = require(cmake_install / "bin" / f"cmake{executable_suffix}", "CMake")
    ninja = require(cmake_install / "bin" / f"ninja{executable_suffix}", "Ninja")
    aapt2 = require(build_tools / f"aapt2{executable_suffix}", "aapt2")
    zipalign = require(build_tools / f"zipalign{executable_suffix}", "zipalign")
    apksigner = require(build_tools / f"apksigner{batch_suffix}", "apksigner")
    toolchain = require(ndk / "build" / "cmake" / "android.toolchain.cmake",
                        "NDK CMake toolchain")
    prebuilt_root = require(ndk / "toolchains" / "llvm" / "prebuilt",
                            "NDK LLVM host tools")
    host_toolchains = [path for path in prebuilt_root.iterdir() if path.is_dir()]
    if not host_toolchains:
        raise RuntimeError(f"no LLVM host toolchain under {prebuilt_root}")
    llvm_readelf = require(host_toolchains[0] / "bin" /
                           f"llvm-readelf{executable_suffix}", "llvm-readelf")

    build = output / "build" / arguments.abi
    package = output / "package"
    if output.exists():
        shutil.rmtree(output)
    build.mkdir(parents=True)
    package.mkdir(parents=True)

    run([cmake, "-S", sample_root, "-B", build, "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
         f"-DCMAKE_MAKE_PROGRAM={ninja}",
         f"-DANDROID_ABI={arguments.abi}",
         f"-DANDROID_PLATFORM=android-{arguments.api}",
         "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release"])
    run([cmake, "--build", build])

    library = require(build / "libogplay_minimal_ndk.so", "sample shared library")
    elf_header = run([llvm_readelf, "-h", library], echo=False)
    if "Class:                             ELF32" not in elf_header or \
            "Machine:                           ARM" not in elf_header:
        raise RuntimeError("sample shared library is not an ELF32 ARM binary")
    elf_symbols = run([llvm_readelf, "-Ws", library], echo=False)
    if "android_main" not in elf_symbols:
        raise RuntimeError("sample shared library does not export android_main")
    unsigned_apk = package / "unsigned.apk"
    packed_apk = package / "packed.apk"
    aligned_apk = package / "aligned.apk"
    final_apk = output / f"ogplay-minimal-ndk-{arguments.abi}.apk"
    run([aapt2, "link", "-o", unsigned_apk, "-I", platform,
         "--manifest", sample_root / "AndroidManifest.xml",
         "--min-sdk-version", str(arguments.api),
         "--target-sdk-version", "35", "--version-code", "1",
         "--version-name", "1.0"])
    shutil.copy2(unsigned_apk, packed_apk)
    with zipfile.ZipFile(packed_apk, "a", compression=zipfile.ZIP_STORED) as archive:
        archive.write(library, f"lib/{arguments.abi}/{library.name}")
    run([zipalign, "-f", "-p", "4", packed_apk, aligned_apk])

    keytool_name = f"keytool{executable_suffix}"
    keytool_path = shutil.which(keytool_name)
    if keytool_path is None:
        raise RuntimeError("keytool is required to create the ignored debug signing key")
    keystore = package / "debug.keystore"
    run([keytool_path, "-genkeypair", "-keystore", keystore,
         "-storepass", "android", "-alias", "androiddebugkey",
         "-keypass", "android", "-dname", "CN=Android Debug,O=Android,C=US",
         "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
         "-noprompt"])
    run([apksigner, "sign", "--ks", keystore, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", final_apk, aligned_apk])
    run([apksigner, "verify", "--verbose", final_apk])
    run([zipalign, "-c", "-p", "4", final_apk])
    badging = run([aapt2, "dump", "badging", final_apk])
    expected_badging = ["minSdkVersion:'19'", "targetSdkVersion:'35'",
                        f"native-code: '{arguments.abi}'",
                        "launchable-activity: name='android.app.NativeActivity'"]
    for marker in expected_badging:
        if marker not in badging:
            raise RuntimeError(f"APK metadata is missing {marker}")

    with zipfile.ZipFile(final_apk) as archive:
        native_path = f"lib/{arguments.abi}/{library.name}"
        if native_path not in archive.namelist():
            raise RuntimeError(f"packaged APK does not contain {native_path}")
    digest = hashlib.sha256(final_apk.read_bytes()).hexdigest()
    print(f"APK: {final_apk}")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"build failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
