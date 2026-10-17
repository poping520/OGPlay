#!/usr/bin/env python3
"""Query, lint and run verification entries of the OGPlay capability ledger."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from typing import Sequence

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11: the stricter line format below still applies.
    tomllib = None


ROOT = Path(__file__).resolve().parents[1]
LEDGER = ROOT / "capabilities.toml"
STATUSES = ("unimplemented", "stub", "partial", "complete")
FIELDS = ("status", "test", "note")
MAX_NOTE_CHARS = 300
SECTION = re.compile(r"\[([a-z0-9_]+(?:\.[a-z0-9_]+)*)\]\Z")
ASSIGNMENT = re.compile(r'([a-z]+) = "([^"\\]*)"\Z')
TEST_CASE = re.compile(
    r'\bTEST_CASE(?:_FIXTURE)?\(\s*(?:[A-Za-z_]\w*\s*,\s*)?"((?:[^"\\]|\\.)*)"')
ADD_TEST = re.compile(r"\badd_test\(\s*NAME\s+([A-Za-z0-9_.\-]+)")
CTEST_NAME = re.compile(r"[a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+\Z")
# doctest filters treat these characters as wildcards, separators or escapes.
FILTER_UNSAFE = re.compile(r"[,*?\\\[\]]")


class LedgerError(ValueError):
    pass


@dataclass
class Capability:
    id: str
    line: int
    status: str = ""
    test: str = ""
    note: str = ""

    def selectors(self) -> list[str]:
        return [part.strip() for part in self.test.split(";") if part.strip()]


@dataclass(frozen=True)
class Selector:
    """One verification entry: doctest filter, ctest name, tools command or Web UI test."""

    text: str
    source: str | None = None
    name_part: str | None = None
    ctest: str | None = None
    command: tuple[str, ...] | None = None
    webui: str | None = None


def load(path: Path = LEDGER) -> list[Capability]:
    """Parse the restricted format shared with core::CapabilityLedger::Load."""
    text = path.read_text(encoding="utf-8")
    if tomllib is not None:
        try:
            tomllib.loads(text)
        except tomllib.TOMLDecodeError as error:
            raise LedgerError(f"{path.name}: invalid TOML: {error}") from error
    capabilities: list[Capability] = []
    current: Capability | None = None
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        section = SECTION.match(line)
        if section:
            current = Capability(section.group(1), number)
            capabilities.append(current)
            continue
        assignment = ASSIGNMENT.match(line)
        if current is None or not assignment or assignment.group(1) not in FIELDS:
            raise LedgerError(f"{path.name}:{number}: expected [id] or status/test/note = \"...\"")
        setattr(current, assignment.group(1), assignment.group(2))
    return capabilities


def parse_selector(text: str) -> Selector:
    if text.startswith("tools/"):
        return Selector(text, command=tuple(text.split()))
    if text.startswith("webui/"):
        return Selector(text, webui=text)
    if "#" in text:
        source, name_part = text.split("#", 1)
        return Selector(text, source=source, name_part=name_part)
    if text.startswith("tests/"):
        return Selector(text, source=text)
    return Selector(text, ctest=text)


class Index:
    """Lazily collected doctest case names and ctest names of the repository."""

    def __init__(self, root: Path = ROOT) -> None:
        self.root = root
        self._cases: dict[str, list[str]] = {}
        self._ctests: set[str] | None = None

    def cases(self, source: str) -> list[str] | None:
        if source not in self._cases:
            path = self.root / source
            if not path.is_file():
                return None
            text = path.read_text(encoding="utf-8", errors="replace")
            self._cases[source] = TEST_CASE.findall(text)
        return self._cases[source]

    def ctests(self) -> set[str]:
        if self._ctests is None:
            paths = [self.root / "CMakeLists.txt", *sorted((self.root / "cmake").glob("*.cmake"))]
            self._ctests = {
                name for path in paths
                for name in ADD_TEST.findall(path.read_text(encoding="utf-8"))
            }
        return self._ctests


def selector_errors(selector: Selector, index: Index) -> list[str]:
    if selector.command is not None:
        script = selector.command[0]
        if not script.endswith(".py") or not (index.root / script).is_file():
            return [f"'{selector.text}' must start with an existing tools/**/*.py script"]
        if any(char in selector.text for char in "<>|&"):
            return [f"'{selector.text}' must be a literal command without placeholders"]
        return []
    if selector.webui is not None:
        if not selector.webui.endswith(".test.ts") or not (index.root / selector.webui).is_file():
            return [f"'{selector.text}' must name an existing webui/**/*.test.ts file"]
        return []
    if selector.ctest is not None:
        if not CTEST_NAME.match(selector.ctest):
            return [f"'{selector.text}' is not tests/<file>.cpp[#name], a ctest name, "
                    "a tools/<script>.py command or webui/<file>.test.ts"]
        if selector.ctest not in index.ctests():
            return [f"ctest '{selector.ctest}' is not registered in project CMake files"]
        return []
    source = selector.source or ""
    if not source.startswith("tests/") or not source.endswith(".cpp"):
        return [f"'{selector.text}' must name a tests/**/*.cpp file"]
    cases = index.cases(source)
    if cases is None:
        return [f"'{source}' does not exist"]
    if not cases:
        return [f"'{source}' declares no doctest TEST_CASE"]
    if selector.name_part is None:
        return []
    if not selector.name_part.strip() or FILTER_UNSAFE.search(selector.name_part):
        return [f"'{selector.text}': name filter must be non-empty without , * ? [ ] \\"]
    if not any(selector.name_part in case for case in cases):
        return [f"'{selector.text}': no TEST_CASE name contains '{selector.name_part}'"]
    return []


def check(capabilities: Sequence[Capability], index: Index) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()
    for capability in capabilities:
        where = f"{LEDGER.name}:{capability.line} [{capability.id}]"
        if capability.id in seen:
            errors.append(f"{where}: duplicate id")
        seen.add(capability.id)
        if capability.status not in STATUSES:
            errors.append(f"{where}: status must be one of {', '.join(STATUSES)}")
        if not capability.note:
            errors.append(f"{where}: note is required")
        if len(capability.note) > MAX_NOTE_CHARS:
            errors.append(f"{where}: note has {len(capability.note)} chars, limit "
                          f"{MAX_NOTE_CHARS}; keep scope/gaps here and history in task files")
        selectors = capability.selectors()
        if not selectors and capability.status != "unimplemented":
            errors.append(f"{where}: {capability.status} requires at least one test selector")
        for text in selectors:
            errors.extend(f"{where}: {message}"
                          for message in selector_errors(parse_selector(text), index))
    return errors


def find_executable(build_dir: Path, name: str, config: str | None) -> Path:
    suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [build_dir / f"{name}{suffix}"]
    configs = [config] if config else ["Debug", "RelWithDebInfo", "Release"]
    candidates += [build_dir / item / f"{name}{suffix}" for item in configs]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise LedgerError(f"{name} not found under {build_dir}; build it first")


def doctest_command(executable: Path, selector: Selector) -> list[str]:
    source = PurePosixPath(selector.source or "")
    # '?' matches either path separator in __FILE__.
    command = [str(executable), f"--source-file=*{source.parent.name}?{source.name}"]
    if selector.name_part is not None:
        command.append(f"--test-case=*{selector.name_part}*")
    return command


def ctest_command(build_dir: Path, names: Sequence[str], config: str | None) -> list[str]:
    pattern = "^(" + "|".join(re.escape(name) for name in names) + ")$"
    command = ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-R", pattern]
    if config:
        command += ["-C", config]
    return command


def select(capabilities: Sequence[Capability], patterns: Sequence[str]) -> list[Capability]:
    chosen = [c for c in capabilities
              if any(c.id == p or c.id.startswith(p.rstrip(".") + ".") for p in patterns)]
    if not chosen:
        raise LedgerError(f"no capability matches {', '.join(patterns)}")
    return chosen


def print_entry(capability: Capability) -> None:
    print(f"[{capability.id}]  {capability.status}  ({LEDGER.name}:{capability.line})")
    print(f"  note: {capability.note}")
    for text in capability.selectors():
        print(f"  test: {text}")


def command_list(args: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    for capability in capabilities:
        if args.prefix and not (capability.id == args.prefix
                                or capability.id.startswith(args.prefix.rstrip(".") + ".")):
            continue
        if args.status and capability.status != args.status:
            continue
        note = capability.note if args.full else capability.note[:60]
        print(f"{capability.status:<13} {capability.id:<48} {note}")
    return 0


def command_show(args: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    for capability in select(capabilities, args.ids):
        print_entry(capability)
    return 0


def command_find(args: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    needle = args.text.casefold()
    for capability in capabilities:
        if any(needle in field.casefold()
               for field in (capability.id, capability.note, capability.test)):
            print_entry(capability)
    return 0


def command_stats(_: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    domains: dict[str, dict[str, int]] = {}
    for capability in capabilities:
        counts = domains.setdefault(capability.id.split(".")[0], dict.fromkeys(STATUSES, 0))
        counts[capability.status] = counts.get(capability.status, 0) + 1
    print(f"{'domain':<12} " + " ".join(f"{status:>13}" for status in STATUSES))
    for domain, counts in sorted(domains.items()):
        print(f"{domain:<12} " + " ".join(f"{counts[status]:>13}" for status in STATUSES))
    return 0


def command_check(_: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    errors = check(capabilities, Index())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"capability ledger ok: {len(capabilities)} entries")
    return 0


def command_verify(args: argparse.Namespace, capabilities: Sequence[Capability]) -> int:
    chosen = select(capabilities, args.ids)
    selectors: list[Selector] = []
    for capability in chosen:
        for text in capability.selectors():
            selector = parse_selector(text)
            if selector not in selectors:
                selectors.append(selector)
    build_dir = (ROOT / args.build_dir).resolve()
    # (command, working directory); ogplay_tests loads staged ANGLE libraries from its own dir.
    commands: list[tuple[list[str], Path]] = []
    doctest = [s for s in selectors if s.source is not None]
    if doctest:
        executable = find_executable(build_dir, "ogplay_tests", args.config)
        commands += [(doctest_command(executable, s), executable.parent) for s in doctest]
    ctests = [s.ctest for s in selectors if s.ctest is not None]
    if ctests:
        commands.append((ctest_command(build_dir, ctests, args.config), ROOT))
    commands += [([sys.executable, *s.command], ROOT) for s in selectors if s.command is not None]
    webui = [str(PurePosixPath(s.webui).relative_to("webui")) for s in selectors if s.webui]
    if webui:
        npm = shutil.which("npm") or "npm"
        commands.append(([npm, "--prefix", "webui", "exec", "--", "vitest", "run", *webui], ROOT))
    failed = 0
    for command, cwd in commands:
        print("$ " + " ".join(command), flush=True)
        if not args.dry_run and subprocess.run(command, cwd=cwd).returncode != 0:
            failed += 1
    if failed:
        print(f"{failed} of {len(commands)} verification commands failed", file=sys.stderr)
    return 1 if failed else 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    listing = commands.add_parser("list", help="list entries, optionally by id prefix/status")
    listing.add_argument("prefix", nargs="?")
    listing.add_argument("--status", choices=STATUSES)
    listing.add_argument("--full", action="store_true", help="print the whole note")
    show = commands.add_parser("show", help="print entries by id or id prefix")
    show.add_argument("ids", nargs="+")
    find = commands.add_parser("find", help="case-insensitive search of id/note/test")
    find.add_argument("text")
    commands.add_parser("stats", help="count entries per domain and status")
    commands.add_parser("check", help="lint format, note length and test selectors")
    verify = commands.add_parser("verify", help="run the test selectors of entries")
    verify.add_argument("ids", nargs="+", help="capability ids or id prefixes")
    verify.add_argument("--build-dir", default="build/dev")
    verify.add_argument("--config", help="multi-config build configuration")
    verify.add_argument("--dry-run", action="store_true", help="print commands only")
    args = parser.parse_args(argv)
    handlers = {"list": command_list, "show": command_show, "find": command_find,
                "stats": command_stats, "check": command_check, "verify": command_verify}
    try:
        return handlers[args.command](args, load())
    except LedgerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
