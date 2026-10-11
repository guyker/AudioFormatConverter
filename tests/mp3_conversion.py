"""Generated-fixture integration tests; requires a built converter and FFmpeg/ffprobe."""

import argparse
from contextlib import closing, contextmanager
import hashlib
import json
import os
from pathlib import Path
import struct
import sqlite3
import subprocess
import tempfile
import unittest

DEFAULT_MP3_SETTINGS = "\n\n\n"


def run(*args):
    return subprocess.run([str(arg) for arg in args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def checked(*args):
    result = run(*args)
    if result.returncode:
        raise AssertionError(f"{args}\n{result.stdout}\n{result.stderr}")
    return result


@contextmanager
def deny_delete(path):
    import ctypes
    from ctypes import wintypes
    create = ctypes.windll.kernel32.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    close = ctypes.windll.kernel32.CloseHandle
    close.argtypes = [wintypes.HANDLE]
    handle = create(str(path), 0x80000000, 3, None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError()
    try:
        yield
    finally:
        close(handle)


def blocks(data):
    assert data[:4] == b"fLaC"
    offset = 4
    result = []
    while True:
        flags = data[offset]
        size = int.from_bytes(data[offset + 1:offset + 4], "big")
        result.append((flags & 127, data[offset + 4:offset + 4 + size]))
        offset += 4 + size
        if flags & 128:
            return result, offset


def picture(image, kind, description, mime=b"image/png"):
    description = description.encode("utf-8")
    return (struct.pack(">II", kind, len(mime)) + mime +
            struct.pack(">I", len(description)) + description +
            struct.pack(">IIIII", 1, 1, 24, 0, len(image)) + image)


def id3_frames(data):
    assert data[:4] == b"ID3\x04"
    synchsafe = lambda value: sum(byte << shift for byte, shift in zip(value, (21, 14, 7, 0)))
    end = 10 + synchsafe(data[6:10])
    offset = 10
    result = []
    while offset + 10 <= end and data[offset] != 0:
        size = synchsafe(data[offset + 4:offset + 8])
        result.append((data[offset:offset + 4], data[offset + 10:offset + 10 + size]))
        offset += 10 + size
    return result


class ConversionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="afc-mp3-tests-")
        cls.root = Path(cls.temporary.name)
        cls.config = cls.root / "config.json"
        cls.config.write_text(json.dumps({
            "FLACSettings": {"ffmpeg_exe_name": str(ARGS.ffmpeg)},
            "LogLevel": 2}), encoding="utf-8")
        cls.fixture = cls.root / "fixture.flac"
        checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "sine=frequency=997:sample_rate=48000:duration=1.3", "-ac", "2",
                "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
                "-metadata", "title=Title \u97f3\u697d", "-metadata", "album=Album",
                "-metadata", "comment=Comment", "-metadata", "track=2/12", cls.fixture)
        image_path = cls.root / "image.png"
        checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "color=red:size=16x16", "-frames:v", "1", "-threads", "1", image_path)
        cls.image = image_path.read_bytes()
        original = cls.fixture.read_bytes()
        metadata, audio_offset = blocks(original)
        for index, (kind, payload) in enumerate(metadata):
            if kind == 4:
                vendor_size = int.from_bytes(payload[:4], "little")
                count_offset = 4 + vendor_size
                count = int.from_bytes(payload[count_offset:count_offset + 4], "little")
                additions = [b"ARTIST=Artist One", b"ARTIST=Artist Two",
                             "X_CUSTOM=Custom \u00e9".encode("utf-8")]
                metadata[index] = (kind, payload[:count_offset] + struct.pack("<I", count + 3) +
                                   payload[count_offset + 4:] +
                                   b"".join(struct.pack("<I", len(tag)) + tag for tag in additions))
        samples = int.from_bytes(metadata[0][1][10:18], "big") & ((1 << 36) - 1)
        track = lambda offset, number, indices: (
            struct.pack(">QB", offset, number) + b"TESTISRC1234" + bytes(14) +
            bytes([len(indices)]) + b"".join(indices))
        cue = (bytes(128) + bytes(8) + bytes(1) + bytes(258) + bytes([2]) +
               track(0, 1, [bytes(8) + b"\x01" + bytes(3)]) + track(samples, 255, []))
        metadata += [(2, b"TEST\x00opaque application bytes\xff"),
                     (3, struct.pack(">QQH", 0, 0, 1024)), (5, cue),
                     (6, picture(cls.image, 3, "Front \u97f3")),
                     (6, picture(cls.image, 4, "Back")),
                     (10, b"opaque reserved block")]
        prefix = b"fLaC" + b"".join(
            bytes([kind | (128 if index == len(metadata) - 1 else 0)]) +
            len(payload).to_bytes(3, "big") + payload
            for index, (kind, payload) in enumerate(metadata))
        cls.fixture.write_bytes(prefix + original[audio_offset:])
        cls.prefix = prefix

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.work = Path(tempfile.mkdtemp(dir=self.root))
        self.source = self.work / "input \u97f3"
        self.source.mkdir()
        self.output = self.work / "output \u00e9"
        self.name = "track's (one) \u97f3.flac"
        self.track = self.source / self.name
        self.track.write_bytes(self.fixture.read_bytes())
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()

    def convert(self, *extra, success=True, target="mp3"):
        result = run(ARGS.converter, "convert", self.source, "--output-dir", self.output,
                     "--config", self.config, "--to", target, *extra)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertNotIn("Completion summary:", result.stdout)
        return result

    def assert_summary(self, result, status, completed, unit, copied=0, failed=0, started=True):
        self.assertIn(f"Completion summary: {status}", result.stdout)
        self.assertIn(f"Completed: {completed} {unit}; copied: {copied} companion files; "
                      f"failed: {failed} {'work items' if started else 'preflight checks'}", result.stdout)
        self.assertRegex(result.stdout, r"Elapsed: \d+ ms")
        self.assertEqual(result.stdout.count("Completion summary:"), 1)

    def probe(self, path):
        return json.loads(checked(ARGS.ffprobe, "-v", "error", "-show_format", "-show_streams",
                                  "-of", "json", path).stdout)

    def mp3(self):
        return self.output / Path(self.name).with_suffix(".mp3")

    def test_metadata_artwork_companions_and_quality(self):
        (self.source / "cover.png").write_bytes(self.image)
        (self.source / "booklet.pdf").write_bytes(b"disposable companion")
        (self.source / "album.cue").write_bytes(b"FILE source.flac WAVE")
        self.convert()
        output = self.probe(self.mp3())
        audio = next(stream for stream in output["streams"] if stream["codec_type"] == "audio")
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["channels"]), ("mp3", "48000", 2))
        for key, expected in {"title": "Title \u97f3\u697d", "album": "Album", "comment": "Comment",
                              "track": "2/12", "X_CUSTOM": "Custom \u00e9"}.items():
            self.assertEqual(output["format"]["tags"][key], expected)
        self.assertEqual(Path(str(self.mp3()) + ".flac-metadata").read_bytes(), self.prefix)
        frames = id3_frames(self.mp3().read_bytes())
        pictures = [payload for name, payload in frames if name == b"APIC"]
        self.assertEqual(len(pictures), 2)
        for payload in pictures:
            self.assertEqual(payload[0], 3)  # UTF-8
            description_start = payload.index(b"\0", 1) + 2
            self.assertEqual(payload[payload.index(b"\0", description_start) + 1:], self.image)
        self.assertEqual(sorted(payload[payload.index(b"\0", 1) + 1] for payload in pictures), [3, 4])
        self.assertTrue(any(name == b"CHAP" for name, _ in frames))
        for companion in ("cover.png", "booklet.pdf", "album.cue"):
            self.assertEqual((self.output / companion).read_bytes(), (self.source / companion).read_bytes())
        high_size = self.mp3().stat().st_size
        self.convert("--overwrite", "--mp3-quality", "9")
        self.assertLess(self.mp3().stat().st_size, high_size)
        self.assertEqual(Path(str(self.mp3()) + ".flac-metadata").read_bytes(), self.prefix)
        self.assertFalse(list(self.output.glob(".afc-convert-*")))

    def test_existing_destination_and_sidecar(self):
        self.output.mkdir()
        sidecar = Path(str(self.mp3()) + ".flac-metadata")
        sidecar.write_bytes(b"existing metadata")
        self.convert(success=False)
        self.assertEqual(sidecar.read_bytes(), b"existing metadata")
        self.assertFalse(self.mp3().exists())
        sidecar.unlink()
        self.mp3().write_bytes(b"existing mp3")
        self.convert(success=False)
        self.assertEqual(self.mp3().read_bytes(), b"existing mp3")

    def test_companion_collision(self):
        (self.source / (self.mp3().name + ".flac-metadata")).write_bytes(b"companion")
        self.convert(success=False)
        self.assertFalse(self.mp3().exists())

    def test_multiple_tracks_and_nested_companions(self):
        album = self.source / "album"
        album.mkdir()
        (album / "second.FLAC").write_bytes(self.fixture.read_bytes())
        (album / "folder.png").write_bytes(self.image)
        (album / "booklet.\u65e5\u672c").write_bytes(b"unchanged companion")
        result = self.convert()
        self.assertTrue((self.output / "album" / "second.mp3").exists())
        self.assertEqual((self.output / "album" / "second.mp3.flac-metadata").read_bytes(), self.prefix)
        self.assertEqual((self.output / "album" / "folder.png").read_bytes(), self.image)
        self.assertEqual((self.output / "album" / "booklet.\u65e5\u672c").read_bytes(), b"unchanged companion")
        self.assertEqual((result.stdout + result.stderr).count("Copied companion file unchanged"), 2)

    def legacy(self, libraries, selection="6\n" + DEFAULT_MP3_SETTINGS + "RUN\n", **extra_settings):
        settings = json.loads(self.config.read_text(encoding="utf-8"))
        settings.update({"OutDirectory": str(self.output), "MediaDirectoryList": libraries})
        settings.update(extra_settings)
        (self.work / "config.json").write_text(json.dumps(settings), encoding="utf-8")
        (self.work / "AppSettingsJsonPersistent.txt").write_text(
            "UTF8:" + str(self.work), encoding="utf-8")
        return subprocess.run([str(ARGS.converter)], input=selection, cwd=self.work,
                              capture_output=True, text=True, encoding="utf-8", errors="replace")

    def test_manual_mp3_selection(self):
        second = self.work / "second"
        second.mkdir()
        (second / "two.flac").write_bytes(self.fixture.read_bytes())
        (self.source / "cover.png").write_bytes(self.image)
        libraries = [
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "Library \u97f3"},
            {"isActive": True, "mediaPath": str(second), "mediaName": "Second"},
            {"isActive": False, "mediaPath": str(self.work / "missing"), "mediaName": "Inactive"}]
        result = self.legacy(libraries)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 2, "audio files", copied=1)
        target = self.output / "MP3" / "Library \u97f3" / self.mp3().name
        self.assertTrue(target.exists())
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)
        self.assertEqual((target.parent / "cover.png").read_bytes(), self.image)
        self.assertTrue((self.output / "MP3" / "Second" / "two.mp3").exists())
        self.assertFalse((self.output / "MP3" / "Inactive").exists())
        before = target.read_bytes()
        result = self.legacy(libraries)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), before)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_menu_descriptions_exit_eof_and_invalid_selections(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        for selection in ("0\n", "", "\n", "12\nx\n5\n 0 \n"):
            result = self.legacy(libraries, selection)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(self.output.exists())
            self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
            for description in ("WARNING: replaces existing FLAC files", "NOT IMPLEMENTED",
                                "Exit without running an action", "? - Help"):
                self.assertIn(description, result.stdout)
            if "5" not in selection:
                self.assertNotIn("      Input:", result.stdout)
                self.assertNotIn("      Output:", result.stdout)
                self.assertNotIn("      Changes:", result.stdout)
            if "5" in selection:
                self.assertIn("Option 5 is not implemented", result.stdout)
                self.assertIn("Invalid selection", result.stdout)

    def test_menu_help_displays_details_without_preparing_actions(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.work / "missing"), "mediaName": "Library"}],
            "?\n0\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for description in ("opens matching album folders", "creates/updates database records",
                            ".mp3.flac-metadata sidecars", "Input:", "Output:", "Changes:"):
            self.assertIn(description, result.stdout)
        self.assertNotIn("Operation preview:", result.stdout)
        self.assertNotIn("===Loading album collection from storage===", result.stdout)
        self.assertFalse(self.output.exists())

    def test_menu_shows_only_selected_action_details_before_preview(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "6\n" + DEFAULT_MP3_SETTINGS + "EXIT\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(".mp3.flac-metadata sidecars", result.stdout)
        self.assertNotIn("opens matching album folders", result.stdout)
        self.assertNotIn("creates/updates database records", result.stdout)
        self.assertLess(result.stdout.index("      Input:"), result.stdout.index("Operation preview:"))
        self.assertEqual(result.stdout.count("Type RUN to run"), 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_back_and_exit_from_each_action_leave_files_unchanged(self):
        album = self.source / "album"
        album.mkdir()
        (album / "one.flac").write_bytes(self.fixture.read_bytes())
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "2\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        def snapshot():
            return {path: path.read_bytes() for root in (self.source, self.output)
                    for path in root.rglob("*") if path.is_file()}

        before = snapshot()
        for option in (1, 2, 3, 4, 6):
            for response in (" \tBACK \t\n0\n", "EXIT\n", "BACK\n"):
                settings = DEFAULT_MP3_SETTINGS if option == 6 else ""
                result = self.legacy(libraries, f"{option}\n{settings}{response}")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(snapshot(), before)
                self.assertNotIn("Completion summary:", result.stdout)
                self.assertIn("BACK for the menu, EXIT to quit", result.stdout)
                self.assertEqual(result.stdout.count("Select run option"), 2 if "BACK" in response else 1)
        self.assertFalse((self.output / "MP3").exists())
        self.assertFalse((self.output / "Library" / "media_out_Library.db").exists())

    def test_back_from_source_replacement_can_select_mp3_without_replacing_sources(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "1\nBACK\n?\n6\n" + DEFAULT_MP3_SETTINGS + "RUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("Select run option"), 2)
        self.assertTrue((self.output / "MP3" / "Library" / self.mp3().name).exists())
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertEqual(result.stdout.count("Type REPLACE to run"), 1)
        self.assertEqual(result.stdout.count("Type RUN to run"), 1)

    def test_menu_retries_then_runs_selected_action(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "16\ninvalid\n5\n \t6 \t\n" + DEFAULT_MP3_SETTINGS + "RUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.output / "MP3" / "Library" / self.mp3().name).exists())

    def test_source_replacement_defaults_to_cancel(self):
        libraries = [
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "Active library"},
            {"isActive": False, "mediaPath": str(self.work / "inactive"), "mediaName": "Inactive"}]
        for selection in ("1\n\n", "1\n", "1\nno\n", "1\ny\n", "1\nreplace\n", "1\nREPLACE extra\n"):
            result = self.legacy(libraries, selection)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Cancelled. No conversion was started", result.stdout)
            confirmation = result.stdout.split("Affected active libraries:\n", 1)[1]
            self.assertIn(str(self.source), confirmation)
            self.assertIn("[Active library]", confirmation)
            self.assertNotIn(str(self.work / "inactive"), confirmation)
            self.assertIn("[Cancel]", confirmation)
            self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
            self.assertEqual(list(self.source.iterdir()), [self.track])
            self.assertFalse(self.output.exists())

    def test_source_replacement_requires_confirmation(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "1\n \tREPLACE \t\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 1, "audio files")
        self.assertIn("Output location: " + str(self.source), result.stdout)
        self.assertNotEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        audio = self.probe(self.track)["streams"][0]
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))
        self.assertEqual(list(self.source.iterdir()), [self.track])
        self.assertFalse(self.output.exists())

    def test_source_replacement_without_active_libraries_fails(self):
        result = self.legacy([{"isActive": False, "mediaPath": str(self.source)}], "1\nREPLACE\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No active media libraries", result.stdout + result.stderr)
        self.assertNotIn("Type REPLACE", result.stdout)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_mp3_preview_cancel_and_cross_library_preflight(self):
        (self.source / "cover.png").write_bytes(self.image)
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "6\n" + DEFAULT_MP3_SETTINGS + "\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Operation preview: FLAC to MP3", result.stdout)
        self.assertIn("Audio files: 1; companions: 1; metadata sidecars: 1", result.stdout)
        self.assertIn("Existing destinations: 0; duplicate destinations: 0", result.stdout)
        self.assertIn("Quality: LAME VBR 0", result.stdout)
        self.assertFalse(self.output.exists())
        second = self.work / "second"
        second.mkdir()
        (second / "two.flac").write_bytes(self.fixture.read_bytes())
        blocked = self.output / "MP3" / "Second" / "two.mp3"
        blocked.parent.mkdir(parents=True)
        blocked.write_bytes(b"existing output")
        libraries.append({"isActive": True, "mediaPath": str(second), "mediaName": "Second"})
        result = self.legacy(libraries)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Existing destinations: 1", result.stdout)
        self.assertIn(str(blocked), result.stdout)
        self.assertFalse((self.output / "MP3" / "Library").exists())
        self.assertEqual(blocked.read_bytes(), b"existing output")

    def test_in_place_preview_collision_never_converts(self):
        checked(ARGS.ffmpeg, "-v", "error", "-i", self.track, "-c:a", "pcm_s16le",
                self.track.with_suffix(".wav"))
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "1\nREPLACE\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("COLLISION:", result.stdout)
        self.assertIn("duplicate destinations: 1", result.stdout)
        self.assertNotIn("Type REPLACE", result.stdout)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_scan_preview_reuses_discovery_and_cancels_without_writes(self):
        album = self.source / "album"
        album.mkdir()
        (album / "one.flac").write_bytes(self.fixture.read_bytes())
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "2\n\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Albums: 1; tracks: 1; JSON batches to write: 1", result.stdout)
        self.assertFalse(self.output.exists())
        result = self.legacy(libraries, "2\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 1, "album JSON files")
        self.assertEqual(result.stdout.count("===Loading album collection from storage==="), 1)
        json_path = self.output / "Library" / "media_out_Library_1.json"
        self.assertTrue(json_path.exists())
        before = json_path.read_bytes()
        result = self.legacy(libraries, "2\n\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Existing generated JSON files to replace: 1", result.stdout)
        self.assertEqual(json_path.read_bytes(), before)

        result = self.legacy(libraries, "3\n\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Operation preview: find duplicate albums", result.stdout)
        self.assertIn("JSON input files: 1", result.stdout)
        self.assertIn(str(json_path), result.stdout)
        self.assertNotIn("Finding duplicated albums...", result.stdout)
        result = self.legacy(libraries, "3\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 0, "duplicate pairs processed")
        self.assertIn("Output: console reports and matching album folders", result.stdout)
        self.assertIn("Finding duplicated albums...", result.stdout)
        result = self.legacy(libraries, "4\n\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Operation preview: import JSON into SQLite", result.stdout)
        self.assertIn("Database: new file", result.stdout)
        db = self.output / "Library" / "media_out_Library.db"
        self.assertFalse(db.exists())
        result = self.legacy(libraries, "4\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 1, "JSON batches imported")
        self.assertIn("Output location: " + str(db), result.stdout)
        self.assertTrue(db.exists())
        before_db = db.read_bytes()
        result = self.legacy(libraries, "4\n\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Database: existing records will be updated", result.stdout)
        self.assertEqual(db.read_bytes(), before_db)

    def test_missing_json_preview_fails_without_output(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        for option in (3, 4):
            result = self.legacy(libraries, f"{option}\nRUN\n")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.output.exists())
            unit = "duplicate pairs processed" if option == 3 else "JSON batches imported"
            self.assert_summary(result, "Failed", 0, unit, failed=1, started=False)
            self.assertIn("No action was started.", result.stdout)

    def test_mp3_partial_completion_counts_only_installed_outputs(self):
        second = self.work / "second"
        second.mkdir()
        corrupt = second / "corrupt.flac"
        corrupt.write_bytes(self.fixture.read_bytes()[:50])
        (self.source / "cover.png").write_bytes(self.image)
        libraries = [
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "First"},
            {"isActive": True, "mediaPath": str(second), "mediaName": "Second"}]
        result = self.legacy(libraries)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Partially completed", 1, "audio files", copied=1, failed=1)
        first_output = self.output / "MP3" / "First"
        self.assertTrue((first_output / self.mp3().name).exists())
        self.assertEqual((first_output / "cover.png").read_bytes(), self.image)
        self.assertFalse((self.output / "MP3" / "Second" / "corrupt.mp3").exists())
        self.assertEqual(corrupt.read_bytes(), self.fixture.read_bytes()[:50])
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertIn("Output location: " + str(first_output), result.stdout)
        self.assertIn("Output location: " + str(self.output / "MP3" / "Second"), result.stdout)
        self.assertIn("unattempted work is not counted as failed", result.stdout)

    def test_database_partial_completion_reports_committed_batches(self):
        album = self.source / "album"
        album.mkdir()
        (album / "one.flac").write_bytes(self.fixture.read_bytes())
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "2\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (self.output / "Library" / "media_out_Library_2.json").write_bytes(b"{invalid JSON")
        result = self.legacy(libraries, "4\nRUN\n")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Partially completed", 1, "JSON batches imported", failed=1)
        database = self.output / "Library" / "media_out_Library.db"
        with closing(sqlite3.connect(database)) as connection:
            self.assertGreater(connection.execute("SELECT count(*) FROM TracksDB").fetchone()[0], 0)

    def test_source_replacement_partial_completion_is_not_reported_as_success(self):
        second = self.work / "second"
        second.mkdir()
        corrupt = second / "corrupt.flac"
        corrupt.write_bytes(self.fixture.read_bytes()[:50])
        result = self.legacy([
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "First"},
            {"isActive": True, "mediaPath": str(second), "mediaName": "Second"}], "1\nREPLACE\n")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Partially completed", 1, "audio files", failed=1)
        self.assertNotEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertEqual(corrupt.read_bytes(), self.fixture.read_bytes()[:50])
        self.assertIn("Failure does not imply rollback", result.stdout)

    def test_manual_mp3_quality_and_explicit_overwrite_reuse_metadata_policy(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        (self.source / "cover.png").write_bytes(self.image)
        result = self.legacy(libraries)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        target = self.output / "MP3" / "Library" / self.mp3().name
        high_size = target.stat().st_size
        (self.source / "cover.png").write_bytes(b"updated companion")
        result = self.legacy(libraries, "6\n9\nNO\nNO\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.stat().st_size, high_size)
        result = self.legacy(libraries, "6\n9\nno\nyes\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Quality: LAME VBR 9", result.stdout)
        self.assertIn("Overwrite: enabled", result.stdout)
        self.assertLess(target.stat().st_size, high_size)
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)
        self.assertEqual((target.parent / "cover.png").read_bytes(), b"updated companion")
        self.assertEqual(sum(name == b"APIC" for name, _ in id3_frames(target.read_bytes())), 2)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assert_summary(result, "Completed", 1, "audio files", copied=1)

    def test_manual_mp3_multichannel_requires_explicit_downmix(self):
        self.track.unlink()
        checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "anullsrc=channel_layout=5.1:sample_rate=48000", "-t", "0.4",
                "-c:a", "flac", self.track)
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries)
        self.assertNotEqual(result.returncode, 0)
        target = self.output / "MP3" / "Library" / self.mp3().name
        self.assertFalse(target.exists())
        result = self.legacy(libraries, "6\n\nYES\n\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("multichannel stereo downmix enabled", result.stdout)
        self.assertEqual(self.probe(target)["streams"][0]["channels"], 2)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_manual_mp3_setting_validation_retries_without_silent_defaults(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "6\n10\n-1\n1x\n \t9 \t\nmaybe\nNO\nmaybe\nNO\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("Invalid quality."), 3)
        self.assertEqual(result.stdout.count("Invalid selection. Enter YES or NO."), 2)
        self.assertIn("Quality: LAME VBR 9", result.stdout)
        self.assertIn("Overwrite: disabled", result.stdout)
        self.assertEqual(result.stdout.count("Type RUN to run"), 1)

    def test_manual_mp3_overwrite_does_not_allow_duplicate_destinations(self):
        (self.source / (self.mp3().name + ".flac-metadata")).write_bytes(b"companion collision")
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "6\n\n\nYES\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Overwrite: enabled", result.stdout)
        self.assertIn("COLLISION:", result.stdout)
        self.assertFalse(self.output.exists())
        self.assertNotIn("Type RUN to run", result.stdout)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_manual_mp3_settings_back_exit_eof_never_start_conversion(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        for answers in ("", "9\n", "9\nYES\n", "9\nYES\nYES\n"):
            for response in ("EXIT\n", "", "BACK\n0\n"):
                result = self.legacy(libraries, "6\n" + answers + response)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(self.output.exists())
                self.assertNotIn("Completion summary:", result.stdout)
                self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        result = self.legacy(libraries, "6\n9\nBACK\n6\n" + DEFAULT_MP3_SETTINGS + "RUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Quality: LAME VBR 0", result.stdout)
        self.assertIn("Overwrite: disabled", result.stdout)

    @unittest.skipUnless(os.name == "nt", "Windows deny-delete scan installation failure")
    def test_scan_partial_completion_counts_installed_json_only(self):
        album = self.source / "album"
        album.mkdir()
        (album / "one.flac").write_bytes(self.fixture.read_bytes())
        second = self.work / "second"
        (second / "album").mkdir(parents=True)
        (second / "album" / "two.flac").write_bytes(self.fixture.read_bytes())
        libraries = [
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "First"},
            {"isActive": True, "mediaPath": str(second), "mediaName": "Second"}]
        result = self.legacy(libraries, "2\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        second_json = self.output / "Second" / "media_out_Second_1.json"
        before = second_json.read_bytes()
        with deny_delete(second_json):
            result = self.legacy(libraries, "2\nRUN\n")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Partially completed", 1, "album JSON files", failed=1)
        self.assertEqual(second_json.read_bytes(), before)
        self.assertTrue((self.output / "First" / "media_out_First_1.json").exists())

    def test_database_import_preserves_legacy_unnamed_library_paths(self):
        album = self.source / "album"
        album.mkdir()
        (album / "one.flac").write_bytes(self.fixture.read_bytes())
        scanned = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "2\nRUN\n")
        self.assertEqual(scanned.returncode, 0, scanned.stdout + scanned.stderr)
        albums = (self.output / "Library" / "media_out_Library_1.json").read_bytes()
        for name, stem in ((None, self.source.name), ("", "")):
            destination = self.output / stem
            destination.mkdir(exist_ok=True)
            json_path = destination / f"media_out_{stem}_1.json"
            json_path.write_bytes(albums)
            library = {"isActive": True, "mediaPath": str(self.source)}
            if name is not None:
                library["mediaName"] = name
            result = self.legacy([library], "4\nRUN\n")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue((destination / f"media_out_{stem}.db").exists())

    def test_manual_scan_matches_explicit_scan_across_batches(self):
        for name in ("a", "b", "c"):
            album = self.source / name
            album.mkdir()
            (album / "one.flac").write_bytes(self.fixture.read_bytes())
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": self.source.name}]
        for asynchronous in (False, True):
            result = self.legacy(libraries, "2\nRUN\n", AlbumsSplitThreshold=2,
                                 UseAsyncFFmpegCalls=asynchronous)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.count("===Loading album collection from storage==="), 1)
            menu_files = sorted((self.output / self.source.name).glob("media_out_*_[12].json"))
            self.assertEqual(len(menu_files), 2)
            expected = [json.loads(path.read_text(encoding="utf-8")) for path in menu_files]
            explicit_output = self.work / "explicit"
            result = run(ARGS.converter, "scan", self.source, "--output-dir", explicit_output,
                         "--config", self.work / "config.json")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            explicit_files = sorted((explicit_output / self.source.name).glob("media_out_*_[12].json"))
            self.assertEqual([json.loads(path.read_text(encoding="utf-8")) for path in explicit_files], expected)

    def test_in_place_wav_conversion_retains_non_flac_source(self):
        wav = self.track.with_suffix(".wav")
        checked(ARGS.ffmpeg, "-v", "error", "-i", self.track, "-c:a", "pcm_s24le", wav)
        self.track.unlink()
        original = wav.read_bytes()
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "1\nREPLACE\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(wav.read_bytes(), original)
        audio = self.probe(self.track)["streams"][0]
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))

    def test_in_place_failure_preserves_original(self):
        self.track.write_bytes(self.fixture.read_bytes()[:50])
        before = self.track.read_bytes()
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "1\nREPLACE\n")
        self.assertNotEqual(result.returncode, 0)
        self.assert_summary(result, "Failed", 0, "audio files", failed=1)
        self.assertEqual(self.track.read_bytes(), before)
        self.assertEqual(list(self.source.iterdir()), [self.track])
        self.assertFalse(self.output.exists())

    def test_manual_mp3_rejects_unsafe_names_and_colliding_libraries(self):
        for names in ([".."], ["Same", "Same"]):
            libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": name}
                         for name in names]
            result = self.legacy(libraries)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(self.output.exists())

    def test_manual_mp3_default_library_name_and_no_active_libraries(self):
        result = self.legacy([{"isActive": False, "mediaPath": str(self.source)}])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())
        result = self.legacy([{"isActive": True, "mediaPath": str(self.source)}])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.output / "MP3" / self.source.name / self.mp3().name).exists())

    def test_multichannel_requires_explicit_downmix(self):
        self.track.unlink()
        checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "anullsrc=channel_layout=5.1:sample_rate=48000", "-t", "0.4",
                "-c:a", "flac", self.track)
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.assertFalse(self.mp3().exists())
        result = self.convert("--downmix-to-stereo")
        self.assertIn("downmixing", result.stdout + result.stderr)
        audio = self.probe(self.mp3())["streams"][0]
        self.assertEqual(audio["channels"], 2)

    def test_rates_mono_silence_and_short_audio(self):
        for rate in (44100, 48000, 96000):
            self.track.unlink()
            checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                    f"anullsrc=channel_layout=mono:sample_rate={rate}", "-t", "0.02",
                    "-c:a", "flac", self.track)
            self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
            self.convert("--overwrite")
            audio = self.probe(self.mp3())["streams"][0]
            self.assertEqual(audio["sample_rate"], str(44100 if rate == 44100 else 48000))
            self.assertEqual(audio["channels"], 1)

    def test_below_cd_and_corrupt_input(self):
        self.track.unlink()
        checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                "sine=sample_rate=22050:duration=0.1", "-c:a", "flac", self.track)
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.track.write_bytes(self.fixture.read_bytes()[:50])
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.assertFalse(self.mp3().exists())

    def test_missing_encoder_process(self):
        broken = self.work / "broken.json"
        broken.write_text(json.dumps({"FLACSettings": {"ffmpeg_exe_name": str(self.work / "missing.exe")}}))
        result = run(ARGS.converter, "convert", self.source, "--output-dir", self.output,
                     "--config", broken, "--to", "mp3")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.mp3().exists())
        self.assertFalse(list(self.output.glob(".afc-convert-*")))
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_quality_validation_and_flac_default(self):
        self.convert("--mp3-quality", "10", success=False)
        self.convert("--mp3-quality", "0", success=False, target="flac")
        self.convert(target="flac")
        audio = self.probe(self.output / self.name)["streams"][0]
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))

    def test_leading_id3_and_trailing_tags(self):
        leading = b"ID3\x04\x00\x00\x00\x00\x00\x00"
        ape = b"APETAGEX" + struct.pack("<IIII", 2000, 32, 0, 0) + bytes(8)
        id3v1 = b"TAG" + bytes(125)
        self.track.write_bytes(leading + self.fixture.read_bytes() + ape + id3v1)
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert()
        self.assertEqual(Path(str(self.mp3()) + ".flac-metadata").read_bytes(),
                         leading + self.prefix + ape + id3v1)

    def test_unsupported_picture_stays_in_sidecar(self):
        source = self.fixture.read_bytes()
        metadata, offset = blocks(source)
        metadata.append((6, picture(b"<svg/>", 0, "Unsupported artwork", b"image/svg+xml")))
        prefix = b"fLaC" + b"".join(
            bytes([kind | (128 if index == len(metadata) - 1 else 0)]) +
            len(payload).to_bytes(3, "big") + payload
            for index, (kind, payload) in enumerate(metadata))
        self.track.write_bytes(prefix + source[offset:])
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert()
        self.assertEqual(Path(str(self.mp3()) + ".flac-metadata").read_bytes(), prefix)
        self.assertEqual(sum(name == b"APIC" for name, _ in id3_frames(self.mp3().read_bytes())), 2)

    def test_truncated_audio_never_installed(self):
        source = self.fixture.read_bytes()
        self.track.write_bytes(source[:len(self.prefix) + (len(source) - len(self.prefix)) // 2])
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.assertFalse(self.mp3().exists())
        self.assertFalse(list(self.output.glob(".afc-convert-*")))

    @unittest.skipUnless(os.name == "nt", "Windows deny-delete replacement failure")
    def test_failed_replacement_restores_sidecar(self):
        self.output.mkdir()
        self.mp3().write_bytes(b"existing MP3")
        sidecar = Path(str(self.mp3()) + ".flac-metadata")
        sidecar.write_bytes(b"existing sidecar")
        with deny_delete(self.mp3()):
            self.convert("--overwrite", success=False)
            self.assertEqual(self.mp3().read_bytes(), b"existing MP3")
            self.assertEqual(sidecar.read_bytes(), b"existing sidecar")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--ffmpeg", type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--ffprobe", type=lambda value: Path(value).resolve(), required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
