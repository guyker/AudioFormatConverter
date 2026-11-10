#!/usr/bin/env python3
"""Install the optional, advisory documentation reminder in this Git checkout."""

from __future__ import annotations

import stat
import subprocess
import sys
from pathlib import Path


def git(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout.decode("utf-8", errors="surrogateescape").rstrip("\r\n")


def within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def install() -> None:
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel")).resolve()
    common = Path(git(root, "rev-parse", "--git-common-dir"))
    common = (root / common).resolve()
    target = Path(git(root, "rev-parse", "--git-path", "hooks/pre-commit"))
    target = root / target
    parent = target.parent.resolve()
    if not (within(parent, root) or within(parent, common)):
        raise RuntimeError(
            f"Hook directory {parent} is outside this checkout and its Git directory; "
            "install manually after checking whether it is shared."
        )
    source = root / "tools" / "hooks" / "pre-commit"
    content = source.read_bytes().replace(b"\r\n", b"\n")
    if target.is_symlink():
        raise RuntimeError(f"Existing hook {target} is a symlink; it was not changed.")
    if target.exists():
        if target.is_file() and target.read_bytes() == content:
            print(f"Documentation reminder already installed: {target}")
            return
        raise RuntimeError(
            f"Existing hook {target} was not changed. Add tools/hooks/pre-commit to it manually."
        )
    parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as hook:
        hook.write(content)
    target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print(f"Installed advisory documentation reminder: {target}")


def main() -> int:
    try:
        install()
    except (OSError, RuntimeError) as error:
        print(f"Cannot install documentation reminder: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
