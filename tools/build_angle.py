#!/usr/bin/env python3
"""Prepare and build the pinned ANGLE checkout used by OGPlay."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform as host_platform
import shlex
import shutil
import subprocess
import sys
from typing import Iterable, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPOSITORY_ROOT / "third_party" / "angle"
DEFAULT_OUTPUT = DEFAULT_SOURCE / "out" / "ogplay"
TARGETS = ("libEGL", "libGLESv2")


def detect_platform() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    raise RuntimeError(f"unsupported host platform: {sys.platform}")


def detect_target_cpu() -> str:
    machine = host_platform.machine().lower()
    if machine in {"amd64", "x86_64"}:
        return "x64"
    if machine in {"arm64", "aarch64"}:
        return "arm64"
    raise RuntimeError(f"unsupported host CPU: {machine}")


def gn_args_for(platform_name: str, target_cpu: str, debug: bool) -> tuple[str, ...]:
    args = {
        "angle_build_tests": "false",
        "angle_enable_cl": "false",
        "angle_enable_gl": "false",
        "angle_enable_null": "false",
        "angle_enable_vulkan": "true",
        "angle_enable_wgpu": "false",
        "is_component_build": "false",
        "is_debug": "true" if debug else "false",
        "symbol_level": "1" if debug else "0",
        "target_cpu": json.dumps(target_cpu),
    }
    if platform_name == "windows":
        args.update({
            "angle_enable_d3d11": "true",
            "angle_enable_swiftshader": "false",
            "is_clang": "false",
            "use_custom_libcxx": "false",
        })
    elif platform_name == "linux":
        args.update({
            "angle_enable_swiftshader": "true",
            "is_clang": "true",
        })
    elif platform_name == "macos":
        args.update({
            "angle_enable_metal": "true",
            "angle_enable_swiftshader": "true",
            "is_clang": "true",
        })
    else:
        raise ValueError(f"unsupported platform: {platform_name}")
    return tuple(f"{name}={args[name]}" for name in sorted(args))


def command_text(command: Sequence[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(command)
    return shlex.join(command)


def gclient_sync_command(gclient: str, jobs: int | None) -> list[str]:
    command = [gclient, "sync", "--no-history"]
    if jobs:
        command.append(f"--jobs={jobs}")
    return command


def resolve_tool(name: str, depot_tools: Path | None) -> str:
    candidates = [name]
    if os.name == "nt":
        candidates = [f"{name}.bat", f"{name}.exe", name]
    if depot_tools is not None:
        for candidate in candidates:
            path = depot_tools / candidate
            if path.is_file():
                return str(path)
    for candidate in candidates:
        path = shutil.which(candidate)
        if path:
            return path
    location = f" under {depot_tools}" if depot_tools else " on PATH"
    raise RuntimeError(f"required depot_tools command '{name}' was not found{location}")


def run(command: Sequence[str], cwd: Path, env: dict[str, str], dry_run: bool) -> None:
    print(f"[{cwd}] {command_text(command)}")
    if dry_run:
        return
    subprocess.run(command, cwd=cwd, env=env, check=True)


def git_output(arguments: Sequence[str], cwd: Path) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=cwd, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding="utf-8",
    )
    return result.stdout.strip()


def pinned_angle_commit(repository_root: Path) -> str:
    entry = git_output(["ls-files", "--stage", "third_party/angle"], repository_root)
    fields = entry.split()
    if len(fields) < 4 or fields[0] != "160000" or fields[2] != "0":
        raise RuntimeError("the index does not contain the third_party/angle gitlink")
    return fields[1]


def verify_checkout(source: Path, repository_root: Path) -> str:
    expected = pinned_angle_commit(repository_root)
    actual = git_output(["rev-parse", "HEAD"], source)
    if actual != expected:
        raise RuntimeError(
            f"ANGLE checkout is {actual}, but the superproject pins {expected}"
        )
    return actual


def expected_artifact_groups(platform_name: str) -> tuple[tuple[str, ...], ...]:
    if platform_name == "windows":
        return (
            ("libEGL.dll.lib", "libEGL.lib"), ("libEGL.dll",),
            ("libGLESv2.dll.lib", "libGLESv2.lib"), ("libGLESv2.dll",),
        )
    if platform_name == "macos":
        return (("libEGL.dylib",), ("libGLESv2.dylib",))
    if platform_name == "linux":
        return (
            ("libEGL.so", "libEGL.so.1"),
            ("libGLESv2.so", "libGLESv2.so.2"),
        )
    raise ValueError(f"unsupported platform: {platform_name}")


def verify_artifacts(output: Path, platform_name: str) -> tuple[str, ...]:
    artifacts: list[str] = []
    missing: list[str] = []
    for alternatives in expected_artifact_groups(platform_name):
        found = next((output / name for name in alternatives if (output / name).is_file()), None)
        if found is None:
            missing.append(" or ".join(alternatives))
        else:
            artifacts.append(found.name)
    if missing:
        raise RuntimeError(
            f"ANGLE output is incomplete at {output}; missing: {', '.join(missing)}"
        )
    return tuple(artifacts)


def write_manifest(
    output: Path,
    platform_name: str,
    target_cpu: str,
    configuration: str,
    commit: str,
    gn_args: Iterable[str],
    artifacts: Iterable[str],
) -> None:
    manifest = {
        "schema_version": 1,
        "angle_commit": commit,
        "platform": platform_name,
        "target_cpu": target_cpu,
        "configuration": configuration,
        "gn_args": list(gn_args),
        "targets": list(TARGETS),
        "artifacts": list(artifacts),
    }
    path = output / "ogplay-angle-manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"verified ANGLE output; manifest: {path}")


def self_test() -> int:
    windows = gn_args_for("windows", "x64", False)
    assert windows == tuple(sorted(windows))
    assert "is_clang=false" in windows
    assert "angle_enable_d3d11=true" in windows
    assert "angle_enable_vulkan=true" in windows
    assert "angle_enable_swiftshader=false" in windows
    assert "angle_build_tests=false" in windows
    assert "use_custom_libcxx=false" in windows
    macos = gn_args_for("macos", "arm64", False)
    assert "target_cpu=\"arm64\"" in macos
    assert "angle_enable_metal=true" in macos
    assert "angle_enable_swiftshader=true" in macos
    linux = gn_args_for("linux", "x64", True)
    assert "is_debug=true" in linux
    assert "symbol_level=1" in linux
    assert expected_artifact_groups("windows")[0] == (
        "libEGL.dll.lib", "libEGL.lib"
    )
    assert expected_artifact_groups("linux")[0] == ("libEGL.so", "libEGL.so.1")
    assert gclient_sync_command("gclient", 4) == [
        "gclient", "sync", "--no-history", "--jobs=4"
    ]
    print("ANGLE build driver self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action", nargs="?", default="all",
        choices=("all", "sync", "configure", "build", "verify", "print-config"),
    )
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--depot-tools", type=Path)
    parser.add_argument(
        "--platform", choices=("auto", "windows", "linux", "macos"), default="auto"
    )
    parser.add_argument("--target-cpu", choices=("auto", "x64", "arm64"), default="auto")
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return self_test()
    if args.jobs is not None and args.jobs < 1:
        raise ValueError("--jobs must be positive")

    source = args.source.resolve()
    output = args.output.resolve()
    depot_tools = args.depot_tools.resolve() if args.depot_tools else None
    platform_name = detect_platform() if args.platform == "auto" else args.platform
    target_cpu = detect_target_cpu() if args.target_cpu == "auto" else args.target_cpu
    gn_args = gn_args_for(platform_name, target_cpu, args.debug)

    if not (source / "scripts" / "bootstrap.py").is_file():
        raise RuntimeError(f"ANGLE source is missing or incomplete: {source}")
    commit = verify_checkout(source, REPOSITORY_ROOT)

    if args.action == "print-config":
        print("\n".join(gn_args))
        return 0

    env = os.environ.copy()
    if depot_tools:
        env["PATH"] = str(depot_tools) + os.pathsep + env.get("PATH", "")
    if platform_name == "windows":
        env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"

    if args.action in {"all", "sync"}:
        gclient = resolve_tool("gclient", depot_tools)
        if not (source / ".gclient").is_file():
            run([sys.executable, str(source / "scripts" / "bootstrap.py")], source, env,
                args.dry_run)
        run(gclient_sync_command(gclient, args.jobs), source, env, args.dry_run)
        if not args.dry_run:
            commit = verify_checkout(source, REPOSITORY_ROOT)

    if args.action in {"all", "configure"}:
        gn = resolve_tool("gn", depot_tools)
        output.mkdir(parents=True, exist_ok=True)
        run([gn, "gen", str(output), f"--args={' '.join(gn_args)}"], source, env,
            args.dry_run)

    if args.action in {"all", "build"}:
        autoninja = resolve_tool("autoninja", depot_tools)
        command = [autoninja, "-C", str(output), *TARGETS]
        if args.jobs:
            command.append(f"-j{args.jobs}")
        run(command, source, env, args.dry_run)

    if args.action in {"all", "verify"} and not args.dry_run:
        artifacts = verify_artifacts(output, platform_name)
        configuration = "debug" if args.debug else "release"
        write_manifest(
            output, platform_name, target_cpu, configuration, commit, gn_args, artifacts
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"ANGLE build failed: {error}", file=sys.stderr)
        raise SystemExit(1)
