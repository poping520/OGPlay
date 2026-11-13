#!/usr/bin/env python3
"""Prepare the local AOSP reference tree described by tools/aosp_sources.json.

Layout: <root>/api<N>/<AOSP project path>/ for sources (shallow, partial, optionally
sparse git checkouts at the API's tag) and <root>/api<N>/rom/ for the hash-pinned ROM
binaries the payload tools consume. <root> is .aosp unless OGPLAY_AOSP_DIR is set.
Each sync also writes <root>/api<N>/README.md (layout, revisions, notes and the source
lookup table from the config) so readers of the tree need no other document.

    python3 tools/aosp_sources.py sync [--api 19] [--project frameworks/base]
    python3 tools/aosp_sources.py rom [--api 19] [--from DIR]
    python3 tools/aosp_sources.py status [--api 19]
    python3 tools/aosp_sources.py readme [--api 19]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = Path(__file__).with_name("aosp_sources.json")
DEFAULT_API = "19"


def aosp_root() -> Path:
    override = os.environ.get("OGPLAY_AOSP_DIR")
    return Path(override).expanduser().resolve() if override else ROOT / ".aosp"


def api_dir(api: str | int = DEFAULT_API) -> Path:
    """Source tree root for one API level, e.g. .aosp/api19 (contains frameworks/base, libcore, ...)."""
    return aosp_root() / f"api{api}"


def rom_dir(api: str | int = DEFAULT_API) -> Path:
    """Hash-pinned ROM binaries (framework jars, framework-res.apk) for one API level."""
    return api_dir(api) / "rom"


def load_config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8"))


def git(*args: str, cwd: Path | None = None, capture: bool = False) -> str:
    result = subprocess.run(["git", "-c", "advice.detachedHead=false", *args], cwd=cwd, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else ""


def tag_commit(checkout: Path, tag: str) -> str | None:
    try:
        return git("rev-parse", "--verify", "-q", f"refs/tags/{tag}^{{commit}}", cwd=checkout, capture=True)
    except subprocess.CalledProcessError:
        return None


def sync_project(remote: str, tag: str, base: Path, project: dict) -> None:
    path = project["path"]
    checkout = base / path
    url = remote + project.get("name", "platform/" + path)
    if not (checkout / ".git").exists():
        if checkout.exists() and any(checkout.iterdir()):
            raise SystemExit(f"{checkout} exists and is not a git checkout; move it away first")
        print(f"[clone] {path} @ {tag}", flush=True)
        checkout.parent.mkdir(parents=True, exist_ok=True)
        git("clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse", "--branch", tag, url, str(checkout))
    else:
        target = tag_commit(checkout, tag)
        if target is None:
            print(f"[fetch] {path} @ {tag}", flush=True)
            git("fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", "tag", tag, cwd=checkout)
            target = tag_commit(checkout, tag)
        if git("rev-parse", "HEAD", cwd=checkout, capture=True) != target:
            git("checkout", "-q", "--detach", target, cwd=checkout)
    sparse = project.get("sparse")
    if sparse:
        git("sparse-checkout", "set", "--cone", *sparse, cwd=checkout)
    else:
        git("sparse-checkout", "disable", cwd=checkout)
    head = git("rev-parse", "--short=12", "HEAD", cwd=checkout, capture=True)
    print(f"[ok] {path} {head}" + (f" sparse={len(sparse)} dirs" if sparse else " full"), flush=True)


def write_readme(api: str, entry: dict) -> Path:
    base = api_dir(api)
    lines = [f"# AOSP API {api} 参考源码", "",
             f"由 `tools/aosp_sources.py` 按 `tools/aosp_sources.json` 生成，请勿手改；"
             f"修改清单后重新 `sync` 或 `readme`。", "",
             f"- tag：`{entry['tag']}`",
             "- 目录与 AOSP 仓库路径一致，只读参考；ROM 二进制在 `rom/`。", ""]
    for note in entry.get("notes", []):
        lines.append(f"- {note}")
    lines += ["", "## 项目", "", "| 路径 | 提交 | 检出范围 |", "| --- | --- | --- |"]
    for project in entry["projects"]:
        checkout = base / project["path"]
        head = (git("rev-parse", "--short=12", "HEAD", cwd=checkout, capture=True)
                if (checkout / ".git").exists() else "未同步")
        scope = "、".join(f"`{d}`" for d in project["sparse"]) if project.get("sparse") else "整个仓库"
        lines.append(f"| `{project['path']}/` | `{head}` | {scope} |")
    if entry.get("index"):
        lines += ["", "## 源码速查", "", "| 主题 | 位置 |", "| --- | --- |"]
        lines += [f"| {topic} | `{path}` |" for topic, path in entry["index"]]
    rom = entry.get("rom")
    if rom:
        lines += ["", "## ROM", "", f"来源：{rom['source']}。", "",
                  "、".join(f"`rom/{name}`" for name in rom["files"]) + "，SHA-256 见清单。"]
    base.mkdir(parents=True, exist_ok=True)
    readme = base / "README.md"
    readme.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[ok] {readme}")
    return readme


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_rom(api: str, files: dict, source: Path | None) -> bool:
    target = rom_dir(api)
    ok = True
    for name, expected in files.items():
        dest = target / name
        if source is not None:
            candidate = source / name
            if not candidate.is_file():
                print(f"[missing] {candidate}")
                ok = False
                continue
            if file_sha256(candidate) != expected:
                print(f"[mismatch] {candidate}")
                ok = False
                continue
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(candidate, dest)
        if not dest.is_file():
            print(f"[missing] {dest}")
            ok = False
        elif file_sha256(dest) != expected:
            print(f"[mismatch] {dest}")
            ok = False
        else:
            print(f"[ok] rom/{name}")
    return ok


def selected_apis(config: dict, api: str | None) -> list[str]:
    if api is None:
        return list(config["apis"])
    if api not in config["apis"]:
        raise SystemExit(f"unknown API {api}; known: {', '.join(config['apis'])}")
    return [api]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sync = sub.add_parser("sync", help="clone or update source projects at the API tag")
    sync.add_argument("--api")
    sync.add_argument("--project", action="append", help="limit to these project paths")
    rom = sub.add_parser("rom", help="verify ROM binaries, optionally importing them from a directory")
    rom.add_argument("--api")
    rom.add_argument("--from", dest="source", type=Path)
    status = sub.add_parser("status", help="show checkout revisions and ROM state")
    status.add_argument("--api")
    readme = sub.add_parser("readme", help="regenerate api<N>/README.md without syncing")
    readme.add_argument("--api")
    args = parser.parse_args(argv)

    config = load_config()
    ok = True
    for api in selected_apis(config, args.api):
        entry = config["apis"][api]
        if args.command == "sync":
            wanted = set(args.project or [])
            unknown = wanted - {p["path"] for p in entry["projects"]}
            if unknown:
                raise SystemExit(f"api{api} has no project {', '.join(sorted(unknown))}")
            for project in entry["projects"]:
                if not wanted or project["path"] in wanted:
                    sync_project(config["remote"], entry["tag"], api_dir(api), project)
            write_readme(api, entry)
        elif args.command == "readme":
            write_readme(api, entry)
        elif args.command == "rom":
            ok &= check_rom(api, entry["rom"]["files"], args.source)
        else:
            print(f"api{api}: {entry['tag']} -> {api_dir(api)}")
            for project in entry["projects"]:
                checkout = api_dir(api) / project["path"]
                if (checkout / ".git").exists():
                    head = git("rev-parse", "--short=12", "HEAD", cwd=checkout, capture=True)
                    match = head and tag_commit(checkout, entry["tag"]) or ""
                    state = "ok" if match.startswith(head) else "off-tag"
                    print(f"  [{state}] {project['path']} {head}")
                else:
                    print(f"  [missing] {project['path']}")
                    ok = False
            ok &= check_rom(api, entry["rom"]["files"], None)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
