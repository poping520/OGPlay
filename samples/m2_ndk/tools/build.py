#!/usr/bin/env python3
"""Build and verify the headless OGPlay M2 NDK shared library."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def newest_directory(parent: Path) -> Path:
    choices = [path for path in parent.iterdir() if path.is_dir()]
    if not choices:
        raise RuntimeError(f"no installed versions under {parent}")
    return max(choices, key=lambda path: tuple(int(v) for v in re.findall(r"\d+", path.name)))


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
        raise subprocess.CalledProcessError(completed.returncode, command,
                                            output=completed.stdout)
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path, default=os.environ.get("ANDROID_NDK_ROOT"))
    parser.add_argument("--api", type=int, default=19)
    parser.add_argument("--abi", default="armeabi-v7a")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.sdk is None or arguments.ndk is None:
        raise RuntimeError("provide --sdk and --ndk or set Android SDK/NDK environment variables")

    sample_root = Path(__file__).resolve().parents[1]
    repository_root = sample_root.parents[1]
    sdk = require(arguments.sdk.resolve(), "Android SDK")
    ndk = require(arguments.ndk.resolve(), "Android NDK")
    output = (arguments.output or repository_root / "out" / "m2-ndk").resolve()
    cmake_root = newest_directory(require(sdk / "cmake", "SDK CMake directory"))
    suffix = ".exe" if os.name == "nt" else ""
    cmake = require(cmake_root / "bin" / f"cmake{suffix}", "CMake")
    ninja = require(cmake_root / "bin" / f"ninja{suffix}", "Ninja")
    toolchain = require(ndk / "build" / "cmake" / "android.toolchain.cmake", "NDK toolchain")
    prebuilt = require(ndk / "toolchains" / "llvm" / "prebuilt", "NDK LLVM tools")
    host = next(path for path in prebuilt.iterdir() if path.is_dir())
    readelf = require(host / "bin" / f"llvm-readelf{suffix}", "llvm-readelf")
    if output.exists():
        shutil.rmtree(output)
    build = output / "build"
    build.mkdir(parents=True)
    run([cmake, "-S", sample_root, "-B", build, "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         f"-DANDROID_ABI={arguments.abi}", f"-DANDROID_PLATFORM=android-{arguments.api}",
         "-DCMAKE_BUILD_TYPE=Release"])
    run([cmake, "--build", build])
    library = require(build / "libogplay_m2_ndk.so", "M2 sample library")
    header = run([readelf, "-h", library], echo=False)
    dynamic = run([readelf, "-d", library], echo=False)
    symbols = run([readelf, "--dyn-syms", "--wide", library], echo=False)
    if "ELF32" not in header or "ARM" not in header:
        raise RuntimeError("M2 sample is not ELF32/ARM")
    if "libc.so" not in dynamic or "ogplay_m2_entry" not in symbols:
        raise RuntimeError("M2 sample lacks its libc dependency or C entry")
    for symbol in ("malloc", "free", "pthread_create", "pthread_join",
                   "open", "write", "lseek", "read", "close"):
        if symbol not in symbols:
            raise RuntimeError(f"M2 sample does not import {symbol}")
    print(f"M2 NDK sample: {library}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"build failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
