#!/usr/bin/env python3
"""Weekly update, run by .github/workflows/weekly-sync.yml.

  1. If upstream (sqlite/sqlite, checked out in original/sqlite) has a release
     newer than the newest release of this project (tags v<VERSION>), publish
     it: python3 tools/release.py <VERSION> --publish
  2. Always: python3 tools/sync_main.py main --commit --push

    python3 tools/weekly_update.py --dry-run    # show what would run, change nothing

Any failing step stops the script with a non-zero exit code.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

LIB_ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = LIB_ROOT / "original" / "sqlite"
SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def run(cmd: list[str], cwd: Path = LIB_ROOT) -> str:
    print("    $ " + " ".join(cmd), flush=True)
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        tail = (result.stderr or result.stdout).strip().splitlines()[-20:]
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(cmd)}\n" + "\n".join(tail))
    return result.stdout


def parse(version: str) -> tuple[int, int, int] | None:
    match = SEMVER_RE.match(version)
    return tuple(int(x) for x in match.groups()) if match else None


def latest(versions: list[str]) -> str | None:
    parsed = [(parse(v), v) for v in versions if parse(v)]
    return max(parsed)[1] if parsed else None


def upstream_latest() -> str | None:
    """Newest upstream release: tags version-<VERSION>."""
    run(["git", "fetch", "--tags", "origin"], cwd=UPSTREAM)
    tags = run(["git", "tag", "--list", "version-*"], cwd=UPSTREAM).split()
    return latest([t[len("version-"):] for t in tags])


def project_latest() -> str | None:
    """Newest release of this project: tags v<VERSION>."""
    run(["git", "fetch", "--tags", "origin"])
    tags = run(["git", "tag", "--list", "v*"]).split()
    return latest([t[1:] for t in tags])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="show the decision, change nothing")
    args = parser.parse_args()

    try:
        print("==> 1. Check for a new upstream release")
        upstream = upstream_latest()
        ours = project_latest()
        print(f"    upstream newest: {upstream}   project newest: {ours}")

        if upstream is None:
            raise RuntimeError("no version-* tags found in original/sqlite")
        if ours is None or parse(upstream) > parse(ours):
            print(f"    new version {upstream} is not released yet")
            if args.dry_run:
                print(f"    [dry run] would run: tools/release.py {upstream} --publish")
            else:
                run([sys.executable, "tools/release.py", upstream, "--publish"])
        else:
            print("    no new upstream release")

        print("\n==> 2. Sync main with the upstream development branch")
        if args.dry_run:
            print("    [dry run] would run: tools/sync_main.py main --commit --push")
        else:
            run([sys.executable, "tools/sync_main.py", "main", "--commit", "--push"])
    except RuntimeError as err:
        print(f"\nWEEKLY UPDATE FAILED: {err}", file=sys.stderr)
        return 1
    print("\nWeekly update complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
