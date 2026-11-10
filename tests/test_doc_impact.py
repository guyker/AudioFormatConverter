"""Tests for the advisory documentation impact report and hook installer."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "tools" / "doc_impact.py"
INSTALLER = ROOT / "tools" / "install_doc_hook.py"
HOOK = ROOT / "tools" / "hooks" / "pre-commit"
DESIGN_HEADINGS = (
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


class DocumentationImpactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="afc-doc-impact-")
        self.addCleanup(self.temporary.cleanup)
        self.sandbox = Path(self.temporary.name)
        self.repo = self.sandbox / "repo"
        self.repo.mkdir()
        self.env = os.environ.copy()
        self.env["GIT_CONFIG_NOSYSTEM"] = "1"
        self.env["GIT_CONFIG_GLOBAL"] = os.devnull
        self.env["GIT_TERMINAL_PROMPT"] = "0"
        self.env.pop("GIT_CONFIG_COUNT", None)
        self.git("init", "--quiet")
        self.git("config", "user.name", "Documentation test")
        self.git("config", "user.email", "docs-test@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.autocrlf", "false")
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 1; }\n")
        self.write("FlacConvert/include/Audio.h", "int Run();\n")
        self.write(
            "FlacConvert/DESIGN.md",
            "# Design\n\n" + "".join(f"## {heading}\n\nText.\n\n" for heading in DESIGN_HEADINGS),
        )
        self.write(
            "docs/features/index.md",
            "# Feature registry\n\n| [F-001](F-001-convert.md) | active |\n",
        )
        self.write(
            "docs/features/F-001-convert.md",
            "# F-001: Convert\n\nStatus: active\n\n"
            "## Behaviour\nCurrent behaviour.\n\n"
            "## Entry points\n`FlacConvert/src/Audio.cpp#Run`\n\n"
            "## Design and knowledge\nK-001\n\n"
            "## Acceptance checks\n- [ ] Works.\n\n"
            "## Known limitations\nNone.\n",
        )
        self.write(
            "docs/knowledge/K-001-source-safety.md",
            "---\nid: K-001\nstatus: active\ncites:\n"
            "  - path: FlacConvert/src/Audio.cpp\n    symbol: Run\n---\n\n"
            "**Fact:** Source safety matters.\n",
        )
        self.git("add", "--all")
        self.git("commit", "--quiet", "-m", "Fixture baseline")

    def write(self, name, content):
        path = self.repo.joinpath(*name.split("/"))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def run_process(self, *arguments, check=False):
        result = subprocess.run(
            [str(argument) for argument in arguments],
            cwd=self.repo,
            env=self.env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if check and result.returncode:
            self.fail(f"{arguments} returned {result.returncode}:\n{result.stdout}\n{result.stderr}")
        return result

    def git(self, *arguments):
        return self.run_process("git", *arguments, check=True).stdout.strip()

    def report(self, *arguments):
        return self.run_process(sys.executable, REPORT, *arguments)

    def prepare_installer(self):
        self.write("tools/doc_impact.py", REPORT.read_text(encoding="utf-8"))
        self.write("tools/hooks/pre-commit", HOOK.read_text(encoding="utf-8"))

    def test_reminder_is_concise_read_only_and_details_are_opt_in(self):
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 2; }\n")
        before = self.git("status", "--porcelain")
        entry = (self.repo / "docs" / "features" / "F-001-convert.md").read_bytes()

        brief = self.report()
        self.assertEqual(brief.returncode, 0, brief.stderr)
        self.assertIn("assess whether", brief.stdout)
        self.assertNotIn("F-001", brief.stdout)
        self.assertNotIn("Audio.cpp", brief.stdout)
        self.assertEqual(self.git("status", "--porcelain"), before)
        self.assertEqual((self.repo / "docs" / "features" / "F-001-convert.md").read_bytes(), entry)

        details = self.report("--details")
        self.assertEqual(details.returncode, 0, details.stderr)
        self.assertIn("FlacConvert/src/Audio.cpp", details.stdout)
        self.assertIn("F-001", details.stdout)
        self.assertIn("K-001", details.stdout)
        self.assertIn("No missing required documentation sections", details.stdout)
        self.assertIn("do not record routine refactors", details.stdout)

    def test_test_only_and_unrelated_changes_are_quiet(self):
        self.write("tests/new_test.cpp", "int test() { return 0; }\n")
        self.write("notes.txt", "No application code was changed.\n")
        brief = self.report()
        self.assertEqual(brief.returncode, 0, brief.stderr)
        self.assertEqual(brief.stdout, "")
        self.assertIn("tests/new_test.cpp", self.report("--details").stdout)

    def test_formatting_does_not_create_documentation_history(self):
        self.write("FlacConvert/src/Audio.cpp", "int Run()   { return 1; }\n")
        before = self.git("status", "--porcelain")
        result = self.report()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("update documentation only when needed", result.stdout)
        self.assertNotIn("F-001", result.stdout)
        self.assertEqual(self.git("status", "--porcelain"), before)

    def test_untracked_source_is_shown_as_unmapped(self):
        self.write("FlacConvert/src/New feature.cpp", "int Added() { return 1; }\n")
        result = self.report("--details")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FlacConvert/src/New feature.cpp", result.stdout)
        self.assertIn("Paths without a feature citation", result.stdout)

    def test_staged_snapshot_ignores_unstaged_document_edits(self):
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 3; }\n")
        self.git("add", "FlacConvert/src/Audio.cpp")
        feature = self.repo / "docs" / "features" / "F-001-convert.md"
        feature.write_text(
            feature.read_text(encoding="utf-8").replace("Audio.cpp#Run", "Other.cpp#Run"),
            encoding="utf-8",
        )

        staged = self.report("--staged", "--details")
        self.assertEqual(staged.returncode, 0, staged.stderr)
        self.assertIn('F-001: "docs/features/F-001-convert.md" (not included)', staged.stdout)
        working = self.report("--details")
        self.assertNotIn('F-001: "docs/features/F-001-convert.md"', working.stdout)

    def test_staged_new_file_is_compared_to_paired_header_citations(self):
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 4; }\n")
        self.git("add", "FlacConvert/src/Audio.cpp")
        header = self.repo / "FlacConvert" / "include" / "Audio.h"
        header.write_text("int Run(int value);\n", encoding="utf-8")
        self.git("add", "FlacConvert/include/Audio.h")
        output = self.report("--staged", "--details").stdout
        self.assertIn("FlacConvert/include/Audio.h", output)
        self.assertIn("F-001", output)
        self.assertIn("K-001", output)

    def test_rename_reports_both_old_and_new_paths(self):
        self.git("mv", "FlacConvert/src/Audio.cpp", "FlacConvert/src/Renamed.cpp")
        result = self.report("--staged", "--details")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FlacConvert/src/Audio.cpp", result.stdout)
        self.assertIn("FlacConvert/src/Renamed.cpp", result.stdout)
        self.assertIn("Paths without a feature citation", result.stdout)

    def test_base_scope_excludes_uncommitted_changes(self):
        base = self.git("rev-parse", "HEAD")
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 5; }\n")
        self.git("add", "FlacConvert/src/Audio.cpp")
        self.git("commit", "--quiet", "-m", "Fixture change")
        self.write("FlacConvert/src/Uncommitted.cpp", "int Extra() { return 0; }\n")
        feature = self.repo / "docs" / "features" / "F-001-convert.md"
        feature.write_text(
            feature.read_text(encoding="utf-8").replace("Audio.cpp#Run", "Other.cpp#Run"),
            encoding="utf-8",
        )
        result = self.report("--base", base, "--details")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FlacConvert/src/Audio.cpp", result.stdout)
        self.assertNotIn("FlacConvert/src/Uncommitted.cpp", result.stdout)
        self.assertIn('F-001: "docs/features/F-001-convert.md"', result.stdout)

    def test_structure_and_inactive_knowledge_are_warned_without_writes(self):
        feature = self.repo / "docs" / "features" / "F-001-convert.md"
        feature.write_text(
            feature.read_text(encoding="utf-8").replace("## Acceptance checks", "## Old checks"),
            encoding="utf-8",
        )
        knowledge = self.repo / "docs" / "knowledge" / "K-001-source-safety.md"
        knowledge.write_text(
            knowledge.read_text(encoding="utf-8").replace("status: active", "status: superseded"),
            encoding="utf-8",
        )
        before = self.git("status", "--porcelain")
        brief = self.report()
        self.assertIn("documentation structure or reference issue(s)", brief.stdout)
        self.assertNotIn("F-001", brief.stdout)
        details = self.report("--details")
        self.assertIn("missing sections: Acceptance checks", details.stdout)
        self.assertIn("K-001 is superseded, not active", details.stdout)
        self.assertEqual(self.git("status", "--porcelain"), before)

    def test_missing_index_and_design_are_reported(self):
        (self.repo / "FlacConvert" / "DESIGN.md").unlink()
        (self.repo / "docs" / "features" / "index.md").unlink()
        result = self.report("--details")
        self.assertIn("FlacConvert/DESIGN.md: missing", result.stdout)
        self.assertIn("docs/features/index.md: missing", result.stdout)

    def test_invalid_base_fails_explicitly(self):
        result = self.report("--base", "missing-ref")
        self.assertEqual(result.returncode, 2)
        self.assertIn("Documentation impact check failed", result.stderr)

    def test_installer_is_idempotent_and_preserves_existing_hook(self):
        self.prepare_installer()
        first = self.run_process(sys.executable, INSTALLER)
        self.assertEqual(first.returncode, 0, first.stderr)
        target = self.repo / ".git" / "hooks" / "pre-commit"
        self.assertEqual(target.read_bytes(), HOOK.read_bytes().replace(b"\r\n", b"\n"))
        second = self.run_process(sys.executable, INSTALLER)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("already installed", second.stdout)
        target.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        original = target.read_bytes()
        third = self.run_process(sys.executable, INSTALLER)
        self.assertNotEqual(third.returncode, 0)
        self.assertIn("was not changed", third.stderr)
        self.assertEqual(target.read_bytes(), original)

    def test_installer_refuses_a_shared_external_hook_directory(self):
        self.prepare_installer()
        shared = self.sandbox / "shared-hooks"
        self.git("config", "core.hooksPath", str(shared))
        result = self.run_process(sys.executable, INSTALLER)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("outside this checkout", result.stderr)
        self.assertFalse(shared.exists())

    def test_git_runs_hook_and_commits_when_tool_is_missing(self):
        self.prepare_installer()
        installed = self.run_process(sys.executable, INSTALLER)
        self.assertEqual(installed.returncode, 0, installed.stderr)
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 6; }\n")
        self.git("add", "FlacConvert/src/Audio.cpp")
        result = self.run_process("git", "commit", "--quiet", "-m", "Fixture hook")
        self.assertEqual(result.returncode, 0)
        output = result.stdout + result.stderr
        self.assertIn("Documentation reminder", output)
        self.assertNotIn("F-001", output)
        self.write("FlacConvert/src/Audio.cpp", "int Run() { return 7; }\n")
        self.git("add", "FlacConvert/src/Audio.cpp")
        (self.repo / "tools" / "doc_impact.py").unlink()
        missing = self.run_process("git", "commit", "--quiet", "-m", "Fixture missing tool")
        self.assertEqual(missing.returncode, 0)
        self.assertIn("unavailable", missing.stderr)


if __name__ == "__main__":
    unittest.main()
