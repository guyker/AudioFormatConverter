#!/usr/bin/env python3
"""Report documentation that may need review for a set of Git changes."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath


FEATURE_FILE = re.compile(r"^docs/features/(F-\d{3,})-[^/]+\.md$")
KNOWLEDGE_FILE = re.compile(r"^docs/knowledge/(K-\d{3,})-[^/]+\.md$")
FEATURE_LINK = re.compile(r"\[(F-\d{3,})\]\((F-\d{3,}-[^/)]+\.md)\)")
CODE_REFERENCE = re.compile(
    r"(?<![\w./-])"
    r"((?:FlacConvert/)?(?:src/|include/)?(?:[\w.-]+/)*[\w.-]+"
    r"\.(?:cpp|cc|cxx|c|h|hpp))#[\w:~.]+",
    re.IGNORECASE,
)
TEST_REFERENCE = re.compile(r"(?<![\w./-])(tests/[A-Za-z0-9_.-]+\.(?:py|cpp|h))")
KNOWLEDGE_REFERENCE = re.compile(r"\bK-\d{3,}\b")
FEATURE_SECTIONS = (
    "Behaviour",
    "Entry points",
    "Design and knowledge",
    "Acceptance checks",
    "Known limitations",
)
DESIGN_SECTIONS = (
    "Purpose",
    "Component map",
    "Data flow",
    "Key flows",
    "Contracts and invariants",
    "Configuration",
    "Files and schemas",
    "Threading and process lifetime",
    "Error handling",
    "Platform boundaries",
    "Build and external dependencies",
)
DESIGN_PATH = "FlacConvert/DESIGN.md"
FEATURE_INDEX = "docs/features/index.md"


def git(root: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(root), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout


def repository_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"Cannot find the Git repository: {detail}")
    return Path(result.stdout.decode("utf-8", errors="surrogateescape").rstrip("\r\n"))


def nul_paths(data: bytes) -> set[str]:
    return {
        item.decode("utf-8", errors="surrogateescape")
        for item in data.split(b"\0")
        if item
    }


def changed_paths(root: Path, staged: bool, base: str | None) -> set[str]:
    common = ("--name-only", "-z", "--no-ext-diff", "--no-renames")
    if base is not None:
        revision = git(root, "rev-parse", "--verify", "--end-of-options", f"{base}^{{commit}}")
        revision = revision.decode("ascii").strip()
        merge_base = git(root, "merge-base", revision, "HEAD").decode("ascii").strip()
        return nul_paths(git(root, "diff", *common, merge_base, "HEAD", "--"))
    if staged:
        return nul_paths(git(root, "diff", *common, "--cached", "--"))
    return (
        nul_paths(git(root, "diff", *common, "--cached", "--"))
        | nul_paths(git(root, "diff", *common, "--"))
        | nul_paths(git(root, "ls-files", "--others", "--exclude-standard", "-z", "--"))
    )


def relevant(path: str) -> bool:
    return (
        path.startswith(("FlacConvert/src/", "FlacConvert/include/", "tests/", "external/"))
        or path in {"FlacConvert/config.json", "FlacConvert/FlacConvert.vcxproj", "MediaBoost.sln"}
    )


class Snapshot:
    def __init__(self, root: Path, mode: str):
        self.root = root
        self.mode = mode
        if mode == "worktree":
            paths = set()
            for directory, pattern in (("docs/features", "F-*.md"), ("docs/knowledge", "K-*.md")):
                folder = root.joinpath(*directory.split("/"))
                if folder.is_dir():
                    paths.update(f"{directory}/{file.name}" for file in folder.glob(pattern) if file.is_file())
            for path in (DESIGN_PATH, FEATURE_INDEX):
                if root.joinpath(*path.split("/")).is_file():
                    paths.add(path)
            self.paths = paths
        else:
            scope = ("--", "docs/features", "docs/knowledge", DESIGN_PATH)
            if mode == "staged":
                self.paths = nul_paths(git(root, "ls-files", "-z", *scope))
            else:
                self.paths = nul_paths(git(root, "ls-tree", "-r", "--name-only", "-z", "HEAD", *scope))

    def read(self, path: str) -> str:
        if self.mode == "worktree":
            data = self.root.joinpath(*path.split("/")).read_bytes()
        else:
            revision = f":{path}" if self.mode == "staged" else f"HEAD:{path}"
            data = git(self.root, "show", "--no-color", revision)
        try:
            return data.decode("utf-8-sig")
        except UnicodeDecodeError as error:
            raise RuntimeError(f"Documentation file {path!r} is not valid UTF-8: {error}") from error


def sections(text: str) -> set[str]:
    return set(re.findall(r"^##\s+(.+?)\s*$", text, re.MULTILINE))


def source_reference(path: str) -> str:
    if path.startswith("FlacConvert/"):
        return path
    if path.startswith(("src/", "include/")):
        return f"FlacConvert/{path}"
    if "/" not in path:
        folder = "include" if PurePosixPath(path).suffix.lower() in {".h", ".hpp"} else "src"
        return f"FlacConvert/{folder}/{path}"
    return path


def references_match(changed: str, cited: str) -> bool:
    if changed == cited:
        return True
    prefixes = ("FlacConvert/src/", "FlacConvert/include/")
    return (
        changed.startswith(prefixes)
        and cited.startswith(prefixes)
        and PurePosixPath(changed).stem == PurePosixPath(cited).stem
    )


def feature_entries(snapshot: Snapshot, warnings: list[str]) -> dict[str, dict]:
    entries = {}
    for path in sorted(snapshot.paths):
        match = FEATURE_FILE.fullmatch(path)
        if not match:
            continue
        feature_id = match.group(1)
        text = snapshot.read(path)
        heading = re.search(r"^#\s+(F-\d{3,}):\s+(.+?)\s*$", text, re.MULTILINE)
        if not heading or heading.group(1) != feature_id:
            warnings.append(f"{path}: missing or mismatched feature heading")
        if feature_id in entries:
            warnings.append(f"{path}: duplicate feature ID {feature_id}")
        missing = set(FEATURE_SECTIONS) - sections(text)
        if missing:
            warnings.append(f"{path}: missing sections: {', '.join(sorted(missing))}")
        status = re.search(r"^Status:[ \t]*([^\r\n]+)\r?$", text, re.MULTILINE)
        if not status or status.group(1).strip() not in {
            "active", "experimental", "deprecated", "removed", "not implemented"
        }:
            warnings.append(f"{path}: missing or unrecognised status")
        refs = {source_reference(value) for value in CODE_REFERENCE.findall(text.replace("\\", "/"))}
        refs.update(TEST_REFERENCE.findall(text.replace("\\", "/")))
        if not refs:
            warnings.append(f"{path}: no source path#Symbol references found")
        entries[feature_id] = {
            "path": path,
            "refs": refs,
            "knowledge": set(KNOWLEDGE_REFERENCE.findall(text)),
        }
    return entries


def knowledge_entries(snapshot: Snapshot, warnings: list[str]) -> dict[str, dict]:
    entries = {}
    for path in sorted(snapshot.paths):
        match = KNOWLEDGE_FILE.fullmatch(path)
        if not match:
            continue
        entry_id = match.group(1)
        text = snapshot.read(path)
        lines = text.splitlines()
        if not lines or lines[0].strip() != "---":
            warnings.append(f"{path}: missing knowledge front matter")
            continue
        end = next((index for index, line in enumerate(lines[1:], 1) if line.strip() == "---"), None)
        if end is None:
            warnings.append(f"{path}: missing knowledge front matter")
            continue
        metadata = "\n".join(lines[1:end])
        identifier = re.search(r"^id:\s*(K-\d{3,})\s*$", metadata, re.MULTILINE)
        status = re.search(r"^status:\s*([\w-]+)\s*$", metadata, re.MULTILINE)
        if (not identifier or identifier.group(1) != entry_id or not status
                or status.group(1) not in {"active", "fixed", "superseded", "archived"}):
            warnings.append(f"{path}: missing or mismatched knowledge ID/status")
            continue
        if entry_id in entries:
            warnings.append(f"{path}: duplicate knowledge ID {entry_id}")
        refs = {
            value.strip().strip("\"'").replace("\\", "/")
            for value in re.findall(r"^\s*-\s*path:\s*(.+?)\s*$", metadata, re.MULTILINE)
        }
        entries[entry_id] = {"path": path, "status": status.group(1), "refs": refs}
    return entries


def check_documents(snapshot: Snapshot, features: dict, knowledge: dict, warnings: list[str]) -> None:
    if DESIGN_PATH not in snapshot.paths:
        warnings.append(f"{DESIGN_PATH}: missing from the selected snapshot")
    else:
        missing = set(DESIGN_SECTIONS) - sections(snapshot.read(DESIGN_PATH))
        if missing:
            warnings.append(f"{DESIGN_PATH}: missing sections: {', '.join(sorted(missing))}")
    if not features:
        warnings.append("docs/features: no feature entries found in the selected snapshot")
    if FEATURE_INDEX not in snapshot.paths:
        warnings.append(f"{FEATURE_INDEX}: missing from the selected snapshot")
    else:
        links = FEATURE_LINK.findall(snapshot.read(FEATURE_INDEX))
        indexed = set()
        for feature_id, filename in links:
            if feature_id in indexed:
                warnings.append(f"{FEATURE_INDEX}: duplicate link for {feature_id}")
            indexed.add(feature_id)
            entry = features.get(feature_id)
            if not entry or entry["path"] != f"docs/features/{filename}":
                warnings.append(f"{FEATURE_INDEX}: invalid link for {feature_id}: {filename}")
        for feature_id in sorted(set(features) - indexed):
            warnings.append(f"{features[feature_id]['path']}: not listed in {FEATURE_INDEX}")
    for feature in features.values():
        for entry_id in sorted(feature["knowledge"]):
            entry = knowledge.get(entry_id)
            if not entry:
                warnings.append(f"{feature['path']}: {entry_id} is missing from the selected snapshot")
            elif entry["status"] != "active":
                warnings.append(f"{feature['path']}: {entry_id} is {entry['status']}, not active")


def label(path: str) -> str:
    return json.dumps(path, ensure_ascii=True)


def report(root: Path, staged: bool, base: str | None, details: bool) -> None:
    mode = "staged" if staged else "commit" if base is not None else "worktree"
    scope = "staged snapshot" if staged else "committed changes since merge-base" if base else "working tree"
    changed = changed_paths(root, staged, base)
    relevant_changes = sorted(path for path in changed if relevant(path))
    production_changes = [path for path in relevant_changes if not path.startswith("tests/")]
    document_changes = [
        path for path in changed
        if path == DESIGN_PATH or path.startswith(("docs/features/", "docs/knowledge/"))
    ]
    if not details and not production_changes and not document_changes:
        return
    snapshot = Snapshot(root, mode)
    warnings: list[str] = []
    features = feature_entries(snapshot, warnings)
    knowledge = knowledge_entries(snapshot, warnings)
    check_documents(snapshot, features, knowledge, warnings)

    if not details:
        if production_changes:
            print("Documentation reminder: assess whether this code changes important behaviour, "
                  "design, contracts or risks; update documentation only when needed.")
        if warnings:
            print(f"WARNING: {len(warnings)} documentation structure or reference issue(s) need review.")
        if production_changes or warnings:
            option = " --staged" if staged else f" --base {label(base)}" if base else ""
            script = Path("tools") / "doc_impact.py"
            print(f"For candidates and warnings: python {script}{option} --details")
        return

    print("# Documentation impact reminder")
    print(f"Scope: {scope}. This report is advisory; it does not determine whether documentation is accurate.")
    if relevant_changes:
        print("\nChanged application, dependency, build or test paths:")
        for path in relevant_changes:
            print(f"- {label(path)}")
        if DESIGN_PATH not in snapshot.paths:
            print(f"\nDesign: {label(DESIGN_PATH)} is missing from the selected snapshot.")
        else:
            included = "included" if DESIGN_PATH in changed else "not included"
            print(f"\nDesign: {label(DESIGN_PATH)} is {included} in these changes; review whether it needs updating.")

        print("\nCandidate features (direct file citations and matching source/header names only):")
        mapped = set()
        found = False
        for feature_id, entry in sorted(features.items()):
            matches = [
                path for path in relevant_changes
                if any(references_match(path, ref) for ref in entry["refs"])
            ]
            if matches:
                found = True
                mapped.update(matches)
                included = "included" if entry["path"] in changed else "not included"
                print(f"- {feature_id}: {label(entry['path'])} ({included}); "
                      + ", ".join(label(path) for path in matches))
        if not found:
            print("- None identified from existing citations.")
        unmapped = sorted(set(relevant_changes) - mapped)
        if unmapped:
            print("\nPaths without a feature citation (investigate manually, including new features):")
            for path in unmapped:
                print(f"- {label(path)}")

        print("\nCandidate active knowledge entries (cited files and matching source/header names):")
        found = False
        for entry_id, entry in sorted(knowledge.items()):
            if entry["status"] != "active":
                continue
            matches = [
                path for path in relevant_changes
                if any(references_match(path, ref) for ref in entry["refs"])
            ]
            if matches:
                found = True
                print(f"- {entry_id}: {label(entry['path'])}; "
                      + ", ".join(label(path) for path in matches))
        if not found:
            print("- None identified from existing citations; check for new decisions or risks.")
    else:
        print("\nNo application, dependency, build or test paths changed in this scope.")

    if warnings:
        print("\nDocumentation structure warnings:")
        for warning in sorted(warnings):
            print(f"- {warning}")
    else:
        print("\nNo missing required documentation sections, index links or inactive feature knowledge links found.")
    print("\nReview the actual diff, DESIGN.md, the feature index and relevant K-entries. "
          "Update behaviour, contracts, schemas, acceptance checks and decisions only when material; "
          "do not record routine refactors, formatting or a change diary. "
          "Existing citations cannot identify every indirect impact or a new feature. "
          "Run kb.ps1 affected; a changed document is not proof that it is accurate.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--staged", action="store_true", help="inspect the staged snapshot for a commit")
    group.add_argument("--base", help="inspect committed changes from the merge-base with this Git revision")
    parser.add_argument("--details", action="store_true", help="show candidate documents and structural warnings")
    args = parser.parse_args()
    try:
        report(repository_root(), args.staged, args.base, args.details)
    except (OSError, RuntimeError) as error:
        print(f"Documentation impact check failed: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
