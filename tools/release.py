#!/usr/bin/env python3
"""Release the SQLite Arduino library for a given version.

    python3 tools/release.py 3.54.1              # steps 1-3 only (dry run)
    python3 tools/release.py 3.54.1 --publish    # steps 1-4: commit, tag, push, GitHub release

The version is the upstream SQLite release (https://github.com/sqlite/sqlite),
so it must exist as the tag version-<VERSION> in original/sqlite.

Steps:
  1. Get the latest code: switch to main, pull it (fast-forward only) and fetch
     the upstream tags.
  2. Set the version on original/: check out the upstream tag version-<VERSION>.
  3. Run the migration (tools/migrate_upstream.py --regenerate) and verify the
     build: host tests (CMake/CTest) and an arduino-cli compile of BasicUsage
     for the Raspberry Pi Pico (rp2040:rp2040:rpipico). Any failure stops the release.
  4. (--publish only) Commit the migrated sources on main, tag v<VERSION>, push
     main and the tag, and create the GitHub release with generated notes (gh CLI).
  5. (--publish only) Set main to track origin/main and pull the latest changes.

Requirements: git, cmake, arduino-cli with the rp2040 core, and
gh (authenticated) for step 4. Python standard library only.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

LIB_ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = LIB_ROOT / "original" / "sqlite"
BUILD_DIR = LIB_ROOT / "build" / "release-host"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
MAIN_BRANCH = "main"  # releases are always cut from and pushed to this branch

# Boards that must compile the BasicUsage example: Raspberry Pi Pico only.
BOARDS = [
    "rp2040:rp2040:rpipico",
]
# PrebuiltDatabase uses LittleFS through the stdio backend (ESP32 only), so it
# is not verified on the Pico.
PREBUILT_BOARDS: list[str] = []


class ReleaseError(RuntimeError):
    pass


def step(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def run(cmd: list[str], cwd: Path = LIB_ROOT, check: bool = True) -> subprocess.CompletedProcess:
    print("    $ " + " ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-25:]
        raise ReleaseError(f"command failed ({result.returncode}): {' '.join(cmd)}\n" + "\n".join(tail))
    return result


def is_git_repo(path: Path) -> bool:
    return run(["git", "rev-parse", "--is-inside-work-tree"], cwd=path, check=False).returncode == 0


def require_tools(publish: bool) -> None:
    needed = ["git", "cmake", "arduino-cli", "python3"]
    if publish:
        needed.append("gh")
    missing = [t for t in needed if shutil.which(t) is None]
    if missing:
        raise ReleaseError("missing tools: " + ", ".join(missing))


# --- step 1 ----------------------------------------------------------------

def get_latest_code(publish: bool) -> None:
    step("1. Get the latest code")
    if is_git_repo(LIB_ROOT):
        dirty = run(["git", "status", "--porcelain", "--untracked-files=no"]).stdout.strip()
        if dirty:
            raise ReleaseError("working tree has uncommitted changes; commit or stash them first:\n" + dirty)
        if run(["git", "remote"]).stdout.strip():
            run(["git", "fetch", "origin"])
            current = run(["git", "branch", "--show-current"]).stdout.strip()
            if current != MAIN_BRANCH:
                print(f"    switching from {current or 'detached HEAD'} to {MAIN_BRANCH}")
                run(["git", "checkout", MAIN_BRANCH])
            run(["git", "pull", "--ff-only", "origin", MAIN_BRANCH])
        else:
            print("    no git remote configured: skipping pull")
    else:
        if publish:
            raise ReleaseError("publishing needs the library to be a git repository with a remote")
        print("    not a git repository: skipping pull (dry run)")

    if not is_git_repo(UPSTREAM):
        raise ReleaseError(f"{UPSTREAM} is not a git checkout of https://github.com/sqlite/sqlite")
    run(["git", "fetch", "--tags", "origin"], cwd=UPSTREAM)


# --- step 2 ----------------------------------------------------------------

def set_upstream_version(version: str) -> str:
    step(f"2. Set the version on original/: checkout version-{version}")
    tag = f"version-{version}"
    if run(["git", "rev-parse", "--verify", f"refs/tags/{tag}"], cwd=UPSTREAM, check=False).returncode != 0:
        raise ReleaseError(f"upstream tag {tag} does not exist in {UPSTREAM}")
    run(["git", "checkout", "--detach", "--quiet", tag], cwd=UPSTREAM)
    return tag


# --- step 3 ----------------------------------------------------------------

def migrate(tag: str) -> None:
    step("3a. Run the migration")
    run([sys.executable, "tools/migrate_upstream.py", "--tag", tag, "--regenerate"])


def verify_host_build() -> None:
    step("3b. Host build and tests (CMake, CTest)")
    run(["cmake", "-S", "tests/host", "-B", str(BUILD_DIR)])
    result = run(["cmake", "--build", str(BUILD_DIR), "-j8"])
    if re.search(r"\berror\b", result.stdout + result.stderr, re.IGNORECASE):
        raise ReleaseError("host build reported errors")
    run(["ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure"])


def verify_arduino_builds() -> None:
    step("3c. Arduino builds")
    jobs = [("BasicUsage", b) for b in BOARDS] + [("PrebuiltDatabase", b) for b in PREBUILT_BOARDS]
    for example, fqbn in jobs:
        print(f"    compiling examples/{example} for {fqbn}", flush=True)
        result = run(["arduino-cli", "compile", "--library", str(LIB_ROOT),
                      "--warnings", "default", "--fqbn", fqbn, f"examples/{example}"], check=False)
        if result.returncode != 0:
            tail = (result.stderr or result.stdout).strip().splitlines()[-15:]
            raise ReleaseError(f"{example} failed for {fqbn}:\n" + "\n".join(tail))
        sizes = [line for line in result.stdout.splitlines() if "Sketch uses" in line]
        print("      " + (sizes[-1].strip() if sizes else "ok"))


# --- step 4 ----------------------------------------------------------------

def publish(version: str, tag: str) -> None:
    step(f"4. Commit, tag and publish v{version} from {MAIN_BRANCH}")
    release_tag = f"v{version}"
    current = run(["git", "branch", "--show-current"]).stdout.strip()
    if current != MAIN_BRANCH:
        raise ReleaseError(f"must publish from branch {MAIN_BRANCH}, currently on {current or 'detached HEAD'}")
    if run(["git", "rev-parse", "--verify", f"refs/tags/{release_tag}"], check=False).returncode == 0:
        raise ReleaseError(f"tag {release_tag} already exists")

    run(["git", "add", "-A", "src", "library.properties", "examples", "tests", "tools", "docs", "README.md", ".gitignore"])
    if run(["git", "diff", "--cached", "--quiet"], check=False).returncode != 0:
        run(["git", "commit", "-m", f"Release {version}: SQLite {version} (upstream {tag})"])
    else:
        print("    nothing to commit")

    run(["git", "tag", "-a", release_tag, "-m", f"SQLite Arduino library {version} (upstream {tag})"])
    run(["git", "push", "origin", MAIN_BRANCH])
    run(["git", "push", "origin", release_tag])
    run(["gh", "release", "create", release_tag,
         "--title", f"SQLite {version} for Arduino",
         "--generate-notes",
         "--notes", f"Migrated from upstream sqlite/sqlite {tag}: https://github.com/sqlite/sqlite/releases/tag/{tag}"])
    print(f"    release {release_tag} created")


# --- step 5 ----------------------------------------------------------------

def sync_main_with_origin() -> None:
    step(f"5. Set upstream of {MAIN_BRANCH} to origin/{MAIN_BRANCH} and get the latest changes")
    if not is_git_repo(LIB_ROOT) or not run(["git", "remote"]).stdout.strip():
        print("    no git repository or remote: nothing to sync")
        return
    run(["git", "checkout", MAIN_BRANCH])
    run(["git", "fetch", "origin", MAIN_BRANCH])
    run(["git", "branch", f"--set-upstream-to=origin/{MAIN_BRANCH}", MAIN_BRANCH])
    run(["git", "pull", "--ff-only"])
    print(f"    {MAIN_BRANCH} tracks origin/{MAIN_BRANCH} and is up to date")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("version", help="SQLite version to release, e.g. 3.54.1")
    parser.add_argument("--publish", action="store_true",
                        help="commit, tag, push and create the GitHub release (default: dry run)")
    args = parser.parse_args()

    version = args.version
    if not VERSION_RE.match(version):
        parser.error(f"version must look like 3.54.1, got {version!r}")

    try:
        require_tools(args.publish)
        get_latest_code(args.publish)
        tag = set_upstream_version(version)
        migrate(tag)
        verify_host_build()
        verify_arduino_builds()
        if args.publish:
            publish(version, tag)
            sync_main_with_origin()
        else:
            print(f"\nDry run complete: SQLite {version} migrated and verified.")
            print("Re-run with --publish to commit, tag v" + version + " and create the GitHub release.")
    except ReleaseError as err:
        print(f"\nRELEASE FAILED: {err}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
