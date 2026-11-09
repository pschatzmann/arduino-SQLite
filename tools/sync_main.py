#!/usr/bin/env python3
"""Synchronize the main branch with origin and with an upstream SQLite release.

    python3 tools/sync_main.py main                    # migrate upstream's development branch
    python3 tools/sync_main.py 3.54.1                  # sync and show the changes
    python3 tools/sync_main.py 3.54.1 --commit         # also commit the migrated sources
    python3 tools/sync_main.py 3.54.1 --commit --push  # also push main to origin

Steps:
  1. Switch to main and fast-forward it from origin/main.
  2. Fetch upstream (original/sqlite) and check out either the release tag
     version-<VERSION>, or, when VERSION is "main", the upstream default branch
     (origin/master, the development head).
  3. Run tools/migrate_upstream.py --regenerate (the version comes from the
     upstream sqlite3.h).
  4. Show the resulting changes; with --commit, commit them on main; with
     --push, push main to origin.

Without --commit nothing is committed or pushed. Only the migrated files
(src/, library.properties) are staged, so unrelated edits are never included.
Python standard library only.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

LIB_ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = LIB_ROOT / "original" / "sqlite"
MAIN_BRANCH = "main"
MIGRATED_PATHS = ["src", "library.properties"]
VERSION_RE = re.compile(r"^(\d+\.\d+\.\d+|main)$")
UPSTREAM_HEAD = "main"  # upstream development head: origin/<default branch>


class SyncError(RuntimeError):
    pass


def step(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def run(cmd: list[str], cwd: Path = LIB_ROOT, check: bool = True) -> subprocess.CompletedProcess:
    print("    $ " + " ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-20:]
        raise SyncError(f"command failed ({result.returncode}): {' '.join(cmd)}\n" + "\n".join(tail))
    return result


def sync_main_from_origin() -> None:
    step(f"1. Switch to {MAIN_BRANCH} and fast-forward from origin")
    if run(["git", "remote"]).stdout.strip() == "":
        raise SyncError("no git remote configured")
    current = run(["git", "branch", "--show-current"]).stdout.strip()
    if current != MAIN_BRANCH:
        run(["git", "checkout", MAIN_BRANCH])
    run(["git", "fetch", "origin", MAIN_BRANCH])
    run(["git", "pull", "--ff-only", "origin", MAIN_BRANCH])


def check_clean_tree() -> None:
    """Refuse to migrate over unrelated uncommitted edits to tracked files."""
    dirty = run(["git", "status", "--porcelain", "--untracked-files=no"]).stdout.strip().splitlines()
    unrelated = [line for line in dirty
                 if not any(line[3:].startswith(p) for p in MIGRATED_PATHS)]
    if unrelated:
        raise SyncError("uncommitted changes outside the migrated files; commit or stash them first:\n"
                        + "\n".join(unrelated))


def checkout_upstream(version: str) -> str:
    """Checks out the requested upstream state and returns a label for it."""
    run(["git", "fetch", "--tags", "origin"], cwd=UPSTREAM)
    if version == UPSTREAM_HEAD:
        step("2. Check out the upstream development branch")
        default = run(["git", "symbolic-ref", "--short", "refs/remotes/origin/HEAD"], cwd=UPSTREAM).stdout.strip()
        if not default:
            raise SyncError("cannot determine upstream default branch (origin/HEAD)")
        run(["git", "checkout", "--detach", "--quiet", default], cwd=UPSTREAM)
        sha = run(["git", "rev-parse", "--short", "HEAD"], cwd=UPSTREAM).stdout.strip()
        return f"{default.split('/', 1)[-1]}@{sha}"
    step(f"2. Check out upstream release version-{version}")
    tag = f"version-{version}"
    if run(["git", "rev-parse", "--verify", f"refs/tags/{tag}"], cwd=UPSTREAM, check=False).returncode != 0:
        raise SyncError(f"upstream tag {tag} does not exist in {UPSTREAM}")
    run(["git", "checkout", "--detach", "--quiet", tag], cwd=UPSTREAM)
    return tag


def migrate() -> str:
    """Runs the migration on the checked-out upstream tree; returns the SQLite version."""
    step("3. Migrate the library to the checked-out upstream sources")
    run([sys.executable, "tools/migrate_upstream.py", "--regenerate"])
    header = (LIB_ROOT / "src" / "sqlite3.h").read_text(encoding="utf-8", errors="replace")
    match = re.search(r'#define\s+SQLITE_VERSION\s+"([0-9.]+)"', header)
    if not match:
        raise SyncError("cannot read SQLITE_VERSION from src/sqlite3.h")
    return match.group(1)


def show_changes() -> bool:
    step("4. Changes to the migrated files")
    status = run(["git", "status", "--short", "--", *MIGRATED_PATHS]).stdout.strip()
    if not status:
        print("    no changes: main already matches this upstream release")
        return False
    print(status)
    return True


def commit_and_push(version: str, label: str, push: bool) -> None:
    step("5. Commit and push")
    run(["git", "add", "--", *MIGRATED_PATHS])
    message = (f"Migrate to SQLite {version} (upstream {label})\n\n"
               "Co-Authored-By: Claude Haiku 5.5 <noreply@anthropic.com>")
    run(["git", "commit", "-m", message])
    if push:
        run(["git", "push", "origin", MAIN_BRANCH])
    else:
        print("    committed locally; re-run with --push or run: git push origin main")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="upstream SQLite release, e.g. 3.54.1, or 'main' for the development branch")
    parser.add_argument("--commit", action="store_true", help="commit the migrated sources on main")
    parser.add_argument("--push", action="store_true", help="push main to origin (implies --commit)")
    args = parser.parse_args()

    if not VERSION_RE.match(args.version):
        parser.error(f"version must look like 3.54.1 or be 'main', got {args.version!r}")
    commit = args.commit or args.push

    try:
        sync_main_from_origin()
        check_clean_tree()
        label = checkout_upstream(args.version)
        version = migrate()
        print(f"    upstream: {label} (SQLite {version})")
        changed = show_changes()
        if changed and commit:
            commit_and_push(version, label, args.push)
        elif changed:
            print("\nNot committed. Re-run with --commit to commit, or --push to commit and push.")
    except SyncError as err:
        print(f"\nSYNC FAILED: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
