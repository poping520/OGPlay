#!/usr/bin/env python3
"""Build, package, sign, and verify the OGPlay M4 exit NativeActivity APK."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile


GRAPHICS_SYMBOL = re.compile(r"(?:egl|gl)[A-Z][A-Za-z0-9_]*")
EXPECTED_EGL_IMPORTS = frozenset({
    "eglChooseConfig", "eglCreateContext", "eglCreateWindowSurface",
    "eglDestroyContext", "eglDestroySurface", "eglGetConfigAttrib",
    "eglGetDisplay", "eglInitialize", "eglMakeCurrent", "eglQuerySurface",
    "eglSwapBuffers", "eglTerminate",
})


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
        encoding = sys.stdout.encoding or "utf-8"
        print(completed.stdout.encode(encoding, errors="replace").decode(encoding), end="")
    if completed.returncode != 0:
        raise subprocess.CalledProcessError(completed.returncode, command,
                                            output=completed.stdout)
    return completed.stdout


def verify_source_contract(sample_root: Path, repository_root: Path) -> set[str]:
    source = (sample_root / "src" / "main" / "cpp" / "main.cpp").read_text(
        encoding="utf-8")
    graphics_calls = set(GRAPHICS_SYMBOL.findall(source))
    egl_calls = {name for name in graphics_calls if name.startswith("egl")}
    if egl_calls != EXPECTED_EGL_IMPORTS:
        raise RuntimeError(
            "M4 exit EGL contract changed: "
            f"missing={sorted(EXPECTED_EGL_IMPORTS - egl_calls)}, "
            f"unexpected={sorted(egl_calls - EXPECTED_EGL_IMPORTS)}")

    catalog = json.loads((repository_root / "data" / "gles" / "gles2.json").read_text(
        encoding="utf-8"))
    catalog_names = {function["name"] for function in catalog["functions"]}
    gles_calls = {name for name in graphics_calls if name.startswith("gl")}
    missing = gles_calls - catalog_names
    if missing:
        raise RuntimeError(f"M4 exit GLES2 calls are absent from the IDL: {sorted(missing)}")
    if len(gles_calls) != 42:
        raise RuntimeError(f"M4 exit GLES2 contract expected 42 calls, found {len(gles_calls)}")
    return graphics_calls


def undefined_symbols(readelf_output: str) -> set[str]:
    result: set[str] = set()
    for line in readelf_output.splitlines():
        fields = line.split()
        if "UND" in fields and fields:
            result.add(fields[-1].split("@", 1)[0])
    return result


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--abi", default="armeabi-v7a")
    parser.add_argument("--api", type=int, default=19)
    parser.add_argument("--build-tools", dest="build_tools_version")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--contract-only", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    sample_root = Path(__file__).resolve().parents[1]
    repository_root = sample_root.parents[1]
    graphics_calls = verify_source_contract(sample_root, repository_root)
    if arguments.contract_only:
        print("M4 exit contract: 12 EGL and 42 GLES2 calls match the repository IDL")
        return 0
    if arguments.sdk is None or arguments.ndk is None:
        raise RuntimeError("provide --sdk and --ndk or set Android SDK/NDK environment variables")

    sdk = require(arguments.sdk.resolve(), "Android SDK")
    ndk = require(arguments.ndk.resolve(), "Android NDK")
    output = (arguments.output or repository_root / "out" / "m4-exit").resolve()
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
    prebuilt_root = require(ndk / "toolchains" / "llvm" / "prebuilt", "NDK LLVM tools")
    hosts = [path for path in prebuilt_root.iterdir() if path.is_dir()]
    if not hosts:
        raise RuntimeError(f"no LLVM host toolchain under {prebuilt_root}")
    readelf = require(hosts[0] / "bin" / f"llvm-readelf{executable_suffix}", "llvm-readelf")

    build = output / "build" / arguments.abi
    package = output / "package"
    if output.exists():
        shutil.rmtree(output)
    build.mkdir(parents=True)
    package.mkdir(parents=True)

    run([cmake, "-S", sample_root, "-B", build, "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         f"-DANDROID_ABI={arguments.abi}",
         f"-DANDROID_PLATFORM=android-{arguments.api}",
         "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release"])
    run([cmake, "--build", build])

    library = require(build / "libogplay_m4_exit.so", "M4 exit sample library")
    header = run([readelf, "-h", library], echo=False)
    dynamic = run([readelf, "-d", library], echo=False)
    symbols = run([readelf, "--dyn-syms", "--wide", library], echo=False)
    if "ELF32" not in header or "ARM" not in header:
        raise RuntimeError("M4 exit sample is not an ELF32 ARM binary")
    if "android_main" not in symbols:
        raise RuntimeError("M4 exit sample does not export android_main")
    for dependency in ("libandroid.so", "libEGL.so", "libGLESv2.so", "liblog.so"):
        if dependency not in dynamic:
            raise RuntimeError(f"M4 exit sample lacks dependency {dependency}")
    graphics_imports = {
        symbol for symbol in undefined_symbols(symbols)
        if GRAPHICS_SYMBOL.fullmatch(symbol)
    }
    if graphics_imports != graphics_calls:
        raise RuntimeError(
            "M4 exit ELF graphics imports do not match its source contract: "
            f"missing={sorted(graphics_calls - graphics_imports)}, "
            f"unexpected={sorted(graphics_imports - graphics_calls)}")

    unsigned_apk = package / "unsigned.apk"
    packed_apk = package / "packed.apk"
    aligned_apk = package / "aligned.apk"
    final_apk = output / f"ogplay-m4-exit-{arguments.abi}.apk"
    run([aapt2, "link", "-o", unsigned_apk, "-I", platform,
         "--manifest", sample_root / "AndroidManifest.xml",
         "--min-sdk-version", str(arguments.api), "--target-sdk-version", "35",
         "--version-code", "1", "--version-name", "1.0"])
    shutil.copy2(unsigned_apk, packed_apk)
    with zipfile.ZipFile(packed_apk, "a", compression=zipfile.ZIP_STORED) as archive:
        archive.write(library, f"lib/{arguments.abi}/{library.name}")
    run([zipalign, "-f", "-p", "4", packed_apk, aligned_apk])

    keytool = shutil.which(f"keytool{executable_suffix}")
    if keytool is None:
        raise RuntimeError("keytool is required to create the ignored debug signing key")
    keystore = package / "debug.keystore"
    run([keytool, "-genkeypair", "-keystore", keystore, "-storepass", "android",
         "-alias", "androiddebugkey", "-keypass", "android",
         "-dname", "CN=Android Debug,O=Android,C=US", "-keyalg", "RSA",
         "-keysize", "2048", "-validity", "10000", "-noprompt"])
    run([apksigner, "sign", "--ks", keystore, "--ks-pass", "pass:android",
         "--key-pass", "pass:android", "--out", final_apk, aligned_apk])
    run([apksigner, "verify", "--verbose", final_apk])
    run([zipalign, "-c", "-p", "4", final_apk])
    badging = run([aapt2, "dump", "badging", final_apk])
    expected = ("minSdkVersion:'19'", "targetSdkVersion:'35'",
                f"native-code: '{arguments.abi}'",
                "launchable-activity: name='android.app.NativeActivity'")
    for marker in expected:
        if marker not in badging:
            raise RuntimeError(f"APK metadata is missing {marker}")
    with zipfile.ZipFile(final_apk) as archive:
        native_path = f"lib/{arguments.abi}/{library.name}"
        if native_path not in archive.namelist():
            raise RuntimeError(f"packaged APK does not contain {native_path}")

    print(f"APK: {final_apk}")
    print(f"SHA-256: {hashlib.sha256(final_apk.read_bytes()).hexdigest()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"build failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
