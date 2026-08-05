#!/usr/bin/env python3
"""Run a strict, incremental M4 exit verification on the current host."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
from typing import Sequence
import zipfile


class ExitPreflightError(RuntimeError):
    pass


@dataclass(frozen=True)
class HostSpec:
    platform: str
    cpu: str
    preset: str


def host_spec(system: str | None = None, machine: str | None = None) -> HostSpec:
    system_name = (platform.system() if system is None else system).lower()
    machine_name = (platform.machine() if machine is None else machine).lower()
    if machine_name in {"amd64", "x86_64"}:
        cpu = "x64"
    elif machine_name in {"arm64", "aarch64"}:
        cpu = "arm64"
    else:
        raise ExitPreflightError(f"unsupported M4 exit host CPU: {machine_name}")

    if system_name == "windows" and cpu == "x64":
        return HostSpec("windows", cpu, "windows-msvc")
    if system_name == "linux" and cpu == "x64":
        return HostSpec("linux", cpu, "dev")
    if system_name == "darwin" and cpu in {"x64", "arm64"}:
        return HostSpec("macos", cpu, "dev")
    raise ExitPreflightError(
        f"unsupported M4 exit host combination: {system_name}/{cpu}"
    )


def require_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file() or resolved.stat().st_size == 0:
        raise ExitPreflightError(f"{label} is missing or empty: {resolved}")
    return resolved


def validate_apk(path: Path, expected_entry: str, label: str) -> Path:
    apk = require_file(path, label)
    try:
        with zipfile.ZipFile(apk) as archive:
            entry = archive.getinfo(expected_entry)
    except (KeyError, zipfile.BadZipFile) as error:
        raise ExitPreflightError(
            f"{label} does not contain {expected_entry}: {apk}"
        ) from error
    if entry.compress_type != zipfile.ZIP_STORED or entry.file_size == 0:
        raise ExitPreflightError(
            f"{label} native library must be non-empty and stored: {expected_entry}"
        )
    return apk


def validate_bionic(root: Path) -> Path:
    resolved = root.expanduser().resolve()
    for name in ("libc.so", "libm.so", "libdl.so"):
        require_file(resolved / "api19" / "lib" / name, f"API 19 Bionic {name}")
    return resolved


def read_json(path: Path, label: str) -> dict[str, object]:
    require_file(path, label)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ExitPreflightError(f"invalid {label}: {path}") from error
    if not isinstance(value, dict):
        raise ExitPreflightError(f"{label} must be a JSON object: {path}")
    return value


def validate_angle_sdk(source: Path, host: HostSpec) -> Path:
    sdk = source / "third_party" / "angle-prebuilt"
    headers = read_json(sdk / "include" / "manifest.json", "ANGLE header manifest")
    package = sdk / f"{host.platform}-{host.cpu}" / "release"
    manifest = read_json(package / "manifest.json", "ANGLE platform manifest")
    expected = {
        "schema_version": 1,
        "platform": host.platform,
        "target_cpu": host.cpu,
        "configuration": "release",
    }
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ExitPreflightError(
                f"ANGLE platform manifest {key} must be {value!r}"
            )
    if manifest.get("angle_commit") != headers.get("angle_commit"):
        raise ExitPreflightError("ANGLE platform and header commits do not match")
    gn_args = manifest.get("gn_args")
    if not isinstance(gn_args, list):
        raise ExitPreflightError("ANGLE platform manifest has no GN argument list")
    swiftshader = "angle_enable_swiftshader=true" in gn_args
    declared_disabled = "angle_enable_swiftshader=false" in gn_args
    if swiftshader == declared_disabled:
        raise ExitPreflightError("ANGLE manifest must declare SwiftShader exactly once")
    if host.platform in {"linux", "macos"} and not swiftshader:
        raise ExitPreflightError(
            f"{host.platform} M4 exit requires ANGLE SwiftShader"
        )
    return package


def make_commands(
    host: HostSpec, cmake: str, ctest: str, jobs: int
) -> tuple[list[str], list[str], list[str]]:
    definitions = [
        "-DOGPLAY_ENABLE_ANGLE=ON",
        "-DOGPLAY_BUILD_TESTS=ON",
        "-DOGPLAY_WARNINGS_AS_ERRORS=ON",
    ]
    if host.platform == "linux":
        definitions.append("-DSDL_UNIX_CONSOLE_BUILD=ON")
    configure = [cmake, "--preset", host.preset, *definitions]
    build = [cmake, "--build", "--preset", host.preset, "--parallel", str(jobs)]
    test = [ctest, "--preset", host.preset, "--output-on-failure"]
    return configure, build, test


def display_command(command: Sequence[str]) -> str:
    return subprocess.list2cmdline(command)


def run_exit(args: argparse.Namespace) -> int:
    source = Path(__file__).resolve().parent.parent
    require_file(source / "CMakeLists.txt", "OGPlay source root")
    host = host_spec()
    validate_angle_sdk(source, host)
    bionic = validate_bionic(args.bionic_root)
    minimal_apk = validate_apk(
        args.minimal_apk,
        "lib/armeabi-v7a/libogplay_minimal_ndk.so",
        "minimal NDK APK",
    )
    m4_apk = validate_apk(
        args.m4_apk,
        "lib/armeabi-v7a/libogplay_m4_exit.so",
        "M4 exit APK",
    )
    commands = make_commands(host, args.cmake, args.ctest, args.jobs)
    print(f"[m4-exit] host={host.platform}/{host.cpu} preset={host.preset}")
    for command in commands:
        print(f"[m4-exit] {display_command(command)}")
    if args.dry_run:
        print("[m4-exit] preflight passed; commands not executed")
        return 0

    subprocess.run(commands[0], cwd=source, check=True)
    subprocess.run(commands[1], cwd=source, check=True)
    environment = os.environ.copy()
    environment.update({
        "OGPLAY_REQUIRE_M4_EXIT": "1",
        "OGPLAY_BIONIC_ORACLE_ROOT": str(bionic),
        "OGPLAY_MINIMAL_NDK_APK": str(minimal_apk),
        "OGPLAY_M4_EXIT_APK": str(m4_apk),
    })
    subprocess.run(commands[2], cwd=source, env=environment, check=True)
    print("[m4-exit] strict full-suite verification passed")
    return 0


def write_test_apk(path: Path, entry: str) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr(entry, b"ELF-test")


def self_test() -> int:
    assert host_spec("Windows", "AMD64") == HostSpec("windows", "x64", "windows-msvc")
    assert host_spec("Linux", "x86_64") == HostSpec("linux", "x64", "dev")
    assert host_spec("Darwin", "arm64") == HostSpec("macos", "arm64", "dev")
    try:
        host_spec("Linux", "arm64")
    except ExitPreflightError:
        pass
    else:
        raise AssertionError("unsupported Linux ARM64 was accepted")

    linux = HostSpec("linux", "x64", "dev")
    configure, build, test = make_commands(linux, "cmake", "ctest", 3)
    assert configure[:3] == ["cmake", "--preset", "dev"]
    assert "-DSDL_UNIX_CONSOLE_BUILD=ON" in configure
    assert build[-2:] == ["--parallel", "3"]
    assert test == ["ctest", "--preset", "dev", "--output-on-failure"]

    with tempfile.TemporaryDirectory(prefix="ogplay-m4-exit-") as temporary:
        root = Path(temporary)
        sdk = root / "third_party" / "angle-prebuilt"
        (sdk / "include").mkdir(parents=True)
        package = sdk / "linux-x64" / "release"
        package.mkdir(parents=True)
        (sdk / "include" / "manifest.json").write_text(
            json.dumps({"angle_commit": "test"}), encoding="utf-8"
        )
        (package / "manifest.json").write_text(json.dumps({
            "schema_version": 1,
            "angle_commit": "test",
            "platform": "linux",
            "target_cpu": "x64",
            "configuration": "release",
            "gn_args": ["angle_enable_swiftshader=true"],
        }), encoding="utf-8")
        assert validate_angle_sdk(root, linux) == package

        bionic = root / "oracle"
        (bionic / "api19" / "lib").mkdir(parents=True)
        for name in ("libc.so", "libm.so", "libdl.so"):
            (bionic / "api19" / "lib" / name).write_bytes(b"ELF-test")
        assert validate_bionic(bionic) == bionic.resolve()

        apk = root / "fixture.apk"
        entry = "lib/armeabi-v7a/libogplay_m4_exit.so"
        write_test_apk(apk, entry)
        assert validate_apk(apk, entry, "test APK") == apk.resolve()
        try:
            validate_apk(apk, "lib/armeabi-v7a/missing.so", "test APK")
        except ExitPreflightError:
            pass
        else:
            raise AssertionError("APK with a missing native entry was accepted")

    print("M4 local exit self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--bionic-root", type=Path)
    parser.add_argument("--minimal-apk", type=Path)
    parser.add_argument("--m4-apk", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--jobs", type=int, default=max(1, min(os.cpu_count() or 2, 8)))
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    missing = [name for name in ("bionic_root", "minimal_apk", "m4_apk")
               if getattr(args, name) is None]
    if missing:
        parser.error("missing required arguments: " + ", ".join(missing))
    if args.jobs < 1:
        parser.error("jobs must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    return self_test() if args.self_test else run_exit(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ExitPreflightError, OSError, subprocess.CalledProcessError) as error:
        print(f"M4 exit error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
