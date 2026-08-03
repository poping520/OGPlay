#!/usr/bin/env python3
"""Persistent remote configure/build/test runner without stored machine details."""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import sys
import tempfile
from typing import Sequence


def validate_remote_root(value: str) -> str:
    path = PurePosixPath(value)
    if not path.is_absolute():
        raise ValueError("remote root must be absolute")
    forbidden = {"/", "/tmp", "/var", "/var/tmp", "/home", "/Users"}
    normalized = str(path)
    if normalized in forbidden or len(path.parts) < 3:
        raise ValueError("remote root is too broad")
    if any(part in {"", ".", ".."} for part in path.parts[1:]):
        raise ValueError("remote root must be normalized")
    return normalized


def quote(value: str) -> str:
    return shlex.quote(value)


def make_remote_script(
    remote_root: str,
    cmake: str,
    ctest: str,
    jobs: int,
    definitions: Sequence[str],
) -> str:
    root = validate_remote_root(remote_root)
    source = f"{root}/source"
    build = f"{root}/build"
    bundle = f"{root}/incoming.bundle"
    definition_args = " ".join(quote(item) for item in definitions)
    configure = (
        f"{quote(cmake)} -S {quote(source)} -B {quote(build)} "
        f"-G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=RelWithDebInfo "
        "-DOGPLAY_BUILD_TESTS=ON -DOGPLAY_WARNINGS_AS_ERRORS=ON "
        f"-DOGPLAY_ENABLE_DYNARMIC=ON {definition_args}"
    ).rstrip()
    return "\n".join(
        [
            "set -eu",
            f"mkdir -p {quote(root)}",
            f"if [ ! -d {quote(source + '/.git')} ]; then",
            f"  git clone -q -b main {quote(bundle)} {quote(source)}",
            "else",
            f"  git -C {quote(source)} fetch -q --force {quote(bundle)} "
            "main:refs/remotes/ogplay-incremental/main",
            f"  git -C {quote(source)} checkout -q -B ogplay-incremental "
            "refs/remotes/ogplay-incremental/main",
            "fi",
            f"git -C {quote(source)} submodule update --init --recursive "
            f">{quote(root + '/submodules.log')} 2>&1 || {{ "
            f"tail -n 100 {quote(root + '/submodules.log')}; exit 10; }}",
            f"{configure} >{quote(root + '/configure.log')} 2>&1 || {{ "
            f"tail -n 140 {quote(root + '/configure.log')}; exit 11; }}",
            f"{quote(cmake)} --build {quote(build)} --parallel {jobs} "
            f">{quote(root + '/build.log')} 2>&1 || {{ "
            f"tail -n 180 {quote(root + '/build.log')}; exit 12; }}",
            f"{quote(ctest)} --test-dir {quote(build)} --output-on-failure "
            f">{quote(root + '/test.log')} 2>&1 || {{ "
            f"cat {quote(root + '/test.log')}; exit 13; }}",
            f"tail -n 16 {quote(root + '/test.log')}",
        ]
    )


def require_clean_main(repository: Path) -> None:
    branch = subprocess.run(
        ["git", "branch", "--show-current"], cwd=repository, check=True,
        capture_output=True, text=True, encoding="utf-8"
    ).stdout.strip()
    if branch != "main":
        raise RuntimeError("remote validation requires the local main branch")
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=repository, check=True,
        capture_output=True, text=True, encoding="utf-8"
    ).stdout
    if status:
        raise RuntimeError("commit or remove local changes before remote validation")


def create_bundle(repository: Path) -> Path:
    descriptor, temporary = tempfile.mkstemp(prefix="ogplay-", suffix=".bundle")
    os.close(descriptor)
    bundle = Path(temporary)
    bundle.unlink()
    try:
        subprocess.run(
            ["git", "bundle", "create", str(bundle), "main"],
            cwd=repository,
            check=True,
        )
    except Exception:
        bundle.unlink(missing_ok=True)
        raise
    return bundle


def mkdir_remote(sftp: object, path: str) -> None:
    current = ""
    for part in PurePosixPath(path).parts[1:]:
        current += "/" + part
        try:
            sftp.stat(current)
        except OSError:
            sftp.mkdir(current)


def run_remote(args: argparse.Namespace) -> int:
    try:
        import paramiko
    except ImportError as error:
        raise RuntimeError("paramiko is required for remote execution") from error

    repository = Path(__file__).resolve().parent.parent
    require_clean_main(repository)
    remote_root = validate_remote_root(args.remote_root)
    definitions = list(args.define)
    if args.platform == "linux":
        definitions.append("-DSDL_UNIX_CONSOLE_BUILD=ON")
    script = make_remote_script(
        remote_root, args.cmake, args.ctest, args.jobs, definitions
    )
    bundle = create_bundle(repository)
    password = os.environ.get(args.password_env)
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    try:
        client.connect(
            args.host,
            port=args.port,
            username=args.user,
            password=password,
            timeout=15,
            banner_timeout=15,
            auth_timeout=15,
            look_for_keys=True,
            allow_agent=True,
        )
        sftp = client.open_sftp()
        try:
            mkdir_remote(sftp, remote_root)
            sftp.put(str(bundle), f"{remote_root}/incoming.bundle")
        finally:
            sftp.close()
        _, stdout, stderr = client.exec_command(script, timeout=args.timeout)
        exit_code = stdout.channel.recv_exit_status()
        sys.stdout.write(stdout.read().decode("utf-8", errors="replace"))
        sys.stderr.write(stderr.read().decode("utf-8", errors="replace"))
        return exit_code
    finally:
        client.close()
        bundle.unlink(missing_ok=True)


def self_test() -> int:
    assert validate_remote_root("/tmp/ogplay-cache") == "/tmp/ogplay-cache"
    for unsafe in ("relative", "/", "/tmp", "/home", "/Users"):
        try:
            validate_remote_root(unsafe)
        except ValueError:
            pass
        else:
            raise AssertionError(f"unsafe root accepted: {unsafe}")
    script = make_remote_script(
        "/tmp/ogplay-cache", "cmake", "ctest", 3,
        ["-DSDL_UNIX_CONSOLE_BUILD=ON"]
    )
    assert "submodule update --init --recursive" in script
    assert "cmake --build /tmp/ogplay-cache/build --parallel 3" in script
    assert "ctest --test-dir /tmp/ogplay-cache/build --output-on-failure" in script
    assert "/tmp/ogplay-cache/build.log" in script
    assert "/tmp/ogplay-cache/test.log" in script
    assert "rm -rf" not in script
    assert "password" not in script
    print("remote incremental self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--host")
    parser.add_argument("--user")
    parser.add_argument("--port", type=int, default=22)
    parser.add_argument("--password-env", default="OGPLAY_REMOTE_PASSWORD")
    parser.add_argument("--remote-root")
    parser.add_argument("--platform", choices=("linux", "macos"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("-D", "--define", action="append", default=[])
    args = parser.parse_args(argv)
    if args.self_test:
        return args
    required = ("host", "user", "remote_root", "platform")
    missing = [name for name in required if not getattr(args, name)]
    if missing:
        parser.error("missing required arguments: " + ", ".join(missing))
    if args.jobs < 1 or args.timeout < 1:
        parser.error("jobs and timeout must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        return self_test()
    return run_remote(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"remote incremental error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
