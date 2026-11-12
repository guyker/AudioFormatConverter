"""Generated-fixture integration tests; requires a built converter and FFmpeg/ffprobe."""

import argparse
import base64
from contextlib import closing, contextmanager
import hashlib
import json
import os
from pathlib import Path
import struct
import shutil
import sqlite3
import subprocess
import tempfile
import unittest

DEFAULT_MP3_SETTINGS = "\n\n\n"


def run(*args, cwd=None, env=None):
    return subprocess.run([str(arg) for arg in args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", cwd=cwd, env=env)


def checked(*args):
    result = run(*args)
    if result.returncode:
        raise AssertionError(f"{args}\n{result.stdout}\n{result.stderr}")
    return result


@contextmanager
def deny_delete(path, share=3):
    import ctypes
    from ctypes import wintypes
    create = ctypes.windll.kernel32.CreateFileW
    create.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    create.restype = wintypes.HANDLE
    close = ctypes.windll.kernel32.CloseHandle
    close.argtypes = [wintypes.HANDLE]
    handle = create(str(path), 0x80000000, share, None, 3, 0, None)
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
            "LogLevel": 2,
            "MetadataSidecars": {
                "Format": "binary",
                "ConvertToFlac": "required",
                "ConvertToMp3": "required",
            }}), encoding="utf-8")
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
        cls.plain = original

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

    def convert(self, *extra, success=True, target="mp3", config=None):
        result = run(ARGS.converter, "convert", self.source, "--output-dir", self.output,
                     "--config", config or self.config, "--to", target, *extra)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertNotIn("Completion summary:", result.stdout)
        return result

    def assert_summary(self, result, status, completed, unit, copied=0, failed=0, started=True):
        self.assertIn(f"Completion summary: {status}", result.stdout)
        self.assertIn(f"Completed: {completed} {unit}; copied: {copied} files unchanged; "
                      f"failed: {failed} {'work items' if started else 'preflight checks'}", result.stdout)
        self.assertRegex(result.stdout, r"Elapsed: \d+ ms")
        self.assertEqual(result.stdout.count("Completion summary:"), 1)

    def probe(self, path):
        return json.loads(checked(ARGS.ffprobe, "-v", "error", "-show_format", "-show_streams",
                                  "-of", "json", path).stdout)

    def mp3(self):
        return self.destination() / Path(self.name).with_suffix(".mp3")

    def destination(self, target="mp3"):
        return self.output / target.upper() / self.source.name

    def audit(self, directory):
        report = json.loads((directory / "conversion-audit.json").read_text(encoding="utf-8"))
        self.assertEqual(report["schemaVersion"], 1)
        self.assertEqual(Path(report["output"]), directory)
        return report

    def test_audit_success_descriptors_and_companion_bytes(self):
        (self.source / "cover.png").write_bytes(self.image)
        self.convert()
        report = self.audit(self.destination())
        self.assertEqual(report["status"], "completed")
        self.assertEqual(report["counts"]["installedAudio"], 1)
        self.assertEqual(report["counts"]["installedCompanions"], 1)
        self.assertTrue(all(item["status"] == "installed" for item in report["files"]))
        self.assertFalse((self.destination() / ".conversion-in-progress").exists())
        resources = report["files"][0]["resources"]
        self.assertTrue(any(item["resource"] == "picture-descriptor" and item["status"] == "Preserved"
                            for item in resources))
        self.assertFalse(any(item["fatal"] for item in resources))
        self.convert(target="flac")
        flac = self.audit(self.destination("flac"))
        self.assertTrue(any(item["status"] == "Normalized" and "dimensions" in item["detail"]
                            for item in flac["files"][0]["resources"]))

    def test_failure_report_unique_folder_and_unattempted_companions(self):
        self.track.write_bytes(b"invalid audio")
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        (self.source / "booklet.pdf").write_bytes(b"original")
        self.convert(success=False)
        failed = next((self.output / "MP3").glob("ERROR_AUDIO_*"))
        report = self.audit(failed)
        self.assertEqual(report["errorType"], "AUDIO")
        self.assertEqual(report["counts"]["installedAudio"], 0)
        self.assertEqual([entry["status"] for entry in report["files"]], ["failed", "not-attempted"])
        self.assertTrue((failed / ".conversion-in-progress").is_dir())
        self.convert(success=False)
        self.assertEqual(len(list((self.output / "MP3").glob("ERROR_AUDIO_*"))), 2)
        self.assertTrue(Path(str(failed) + "-1").is_dir())
        self.assertFalse(self.destination().exists())

    def test_failure_does_not_rename_preexisting_folder(self):
        self.destination().mkdir(parents=True)
        (self.destination() / "keep.txt").write_bytes(b"unrelated existing file")
        self.track.write_bytes(b"invalid audio")
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.assertTrue(self.destination().is_dir())
        self.assertEqual((self.destination() / "keep.txt").read_bytes(), b"unrelated existing file")
        self.assertEqual(self.audit(self.destination())["status"], "failed")
        self.assertFalse(list((self.output / "MP3").glob("ERROR_*")))
        self.convert("--overwrite", success=False)
        self.assertEqual(self.audit(self.destination())["errorType"], "AUDIO")

    def test_reserved_state_filenames_block_without_writes(self):
        for reserved in ("conversion-audit.json", ".conversion-in-progress", "CONVERSION-AUDIT.JSON"):
            path = self.source / reserved
            path.write_bytes(b"original companion")
            result = self.convert(success=False)
            self.assertFalse(self.output.exists(), result.stdout + result.stderr)
            self.assertEqual(path.read_bytes(), b"original companion")
            path.unlink()

    def test_existing_marker_blocks_even_overwrite(self):
        self.destination().mkdir(parents=True)
        marker = self.destination() / ".conversion-in-progress"
        marker.mkdir()
        self.convert("--overwrite", success=False)
        self.assertTrue(marker.exists())
        self.assertFalse((self.destination() / "conversion-audit.json").exists())

    def test_audit_lists_skipped_audio(self):
        (self.source / "untouched.mp3").write_bytes(b"recognized audio extension")
        self.convert()
        report = self.audit(self.destination())
        self.assertEqual(report["skippedAudio"], ["untouched.mp3"])
        self.assertEqual(report["counts"]["skippedAudio"], 1)
        self.assertFalse((self.destination() / "untouched.mp3").exists())

    def test_malformed_comment_fails_preservation_before_install(self):
        original = self.fixture.read_bytes()
        self.track.write_bytes(original.replace(b"X_CUSTOM=", b"X_CUSTOM?", 1))
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        failed = next((self.output / "MP3").glob("ERROR_PRESERVATION_*"))
        report = self.audit(failed)
        self.assertTrue(any(item["fatal"] for item in report["files"][0]["resources"]))
        self.assertFalse((failed / self.mp3().name).exists())
        self.assertFalse(list(failed.glob(".afc-convert-*")))

    @unittest.skipUnless(os.name == "nt", "Windows locked audit report replacement")
    def test_report_install_failure_is_not_success(self):
        self.convert()
        path = self.destination() / "conversion-audit.json"
        original = path.read_bytes()
        with deny_delete(path):
            result = self.convert("--overwrite", success=False)
        self.assertEqual(path.read_bytes(), original)
        self.assertTrue((self.destination() / ".conversion-in-progress").exists())
        self.assertIn("Conversion failed (IO)", result.stdout + result.stderr)
        self.assertFalse(list((self.output / "MP3").glob("ERROR_*")))

    @unittest.skipUnless(os.name == "nt", "Windows component length limit")
    def test_rename_failure_reports_original_output_path(self):
        long_name = "L" * 245
        self.track.write_bytes(b"invalid audio")
        original = self.track.read_bytes()
        extendedOutput = "\\\\?\\" + str(self.output)
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": long_name}], "1\nRUN\n",
            OutDirectory=extendedOutput)
        self.assertNotEqual(result.returncode, 0)
        directory = Path(extendedOutput) / "FLAC" / long_name
        self.assertTrue(directory.is_dir(), result.stdout + result.stderr)
        self.assertIn("Cannot mark failed output folder", result.stdout + result.stderr)
        self.assertIn("Output location: " + str(directory), result.stdout)
        self.assertEqual(self.audit(directory)["status"], "failed")
        self.assertTrue((directory / ".conversion-in-progress").exists())
        self.assertEqual(self.track.read_bytes(), original)
        shutil.rmtree(directory)

    def test_native_audit_negative_controls(self):
        if not ARGS.audit_checker:
            self.skipTest("Native audit checker not supplied")
        scratch = self.work / "native"
        scratch.mkdir()
        checked(ARGS.audit_checker, self.track, scratch)

    @unittest.skipUnless(os.name == "nt", "Windows sharing failure during companion copy")
    def test_copy_failure_marks_new_output_and_keeps_completed_audio(self):
        companion = self.source / "booklet.pdf"
        companion.write_bytes(b"original companion")
        with deny_delete(companion, share=0):
            self.convert(success=False)
        failed = next((self.output / "MP3").glob("ERROR_COPY_*"))
        report = self.audit(failed)
        self.assertEqual(report["counts"]["installedAudio"], 1)
        self.assertEqual(report["counts"]["installedCompanions"], 0)
        self.assertTrue((failed / self.mp3().name).exists())
        self.assertEqual([item["status"] for item in report["files"]], ["installed", "failed"])
        self.assertEqual(companion.read_bytes(), b"original companion")
        self.assertFalse(list(failed.glob(".afc-convert-*")))

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
            self.assertEqual((self.destination() / companion).read_bytes(), (self.source / companion).read_bytes())
        high_size = self.mp3().stat().st_size
        self.convert("--overwrite", "--mp3-quality", "9")
        self.assertLess(self.mp3().stat().st_size, high_size)
        self.assertEqual(Path(str(self.mp3()) + ".flac-metadata").read_bytes(), self.prefix)
        self.assertFalse(list(self.output.glob(".afc-convert-*")))

    def test_flac_metadata_artwork_companions_and_relative_layout(self):
        nested = self.source / "album"
        nested.mkdir()
        track = nested / self.track.name
        self.track.rename(track)
        (nested / "cover.png").write_bytes(self.image)
        (nested / "album.cue").write_bytes(b"FILE source.flac WAVE")
        before = {p.relative_to(self.source): p.read_bytes()
                  for p in self.source.rglob("*") if p.is_file()}
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "1\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 1, "audio files", copied=2)
        self.assertEqual({p.relative_to(self.source): p.read_bytes()
                          for p in self.source.rglob("*") if p.is_file()}, before)
        target = self.output / "FLAC" / "Library" / "album" / track.name
        output = self.probe(target)
        audio = next(s for s in output["streams"] if s["codec_type"] == "audio")
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))
        for key in ("title", "album", "comment", "track", "X_CUSTOM"):
            self.assertEqual(output["format"]["tags"][key], self.probe(track)["format"]["tags"][key])
        pictures = [payload for kind, payload in blocks(target.read_bytes())[0] if kind == 6]
        original_pictures = [payload for kind, payload in blocks(track.read_bytes())[0] if kind == 6]
        def picture_content(payload):
            mime_length = int.from_bytes(payload[4:8], "big")
            description_offset = 8 + mime_length
            description_length = int.from_bytes(payload[description_offset:description_offset + 4], "big")
            fields_end = description_offset + 4 + description_length
            return payload[:fields_end], payload[fields_end + 20:]
        self.assertEqual(sorted(map(picture_content, pictures)), sorted(map(picture_content, original_pictures)))
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)
        for companion in ("cover.png", "album.cue"):
            self.assertEqual((target.parent / companion).read_bytes(), (nested / companion).read_bytes())
        self.assertFalse(list(self.output.rglob(".afc-convert-*")))

    def test_flac_skips_unchanged_audio_folders_and_keeps_written_folders_complete(self):
        artist = self.source / "Artist \u97f3"
        album = artist / "AlbumA"
        artwork = album / "Artwork"
        empty = album / "Empty"
        unchanged = artist / "AlbumB"
        nested = unchanged / "nested"
        scans = artist / "Scans"
        for directory in (artwork, empty, nested, scans):
            directory.mkdir(parents=True)
        (album / "second.FLAC").write_bytes(self.fixture.read_bytes())
        (album / "bonus.MP3").write_bytes(b"copied beside a conversion")
        (artwork / "cover.png").write_bytes(self.image)
        (scans / "scan.png").write_bytes(self.image)
        (self.source / "README").write_bytes(b"extensionless companion")
        extensions = ("MP3", "aac", "ogg", "oga", "opus", "wma", "aif", "aiff",
                      "alac", "mp4", "mka", "ac3", "m4b", "m4p", "mp2", "mp1",
                      "au", "snd", "aifc", "caf", "tak", "tta", "shn", "amr")
        for extension in extensions:
            (nested / ("track." + extension)).write_bytes(("unchanged " + extension).encode())
        (unchanged / "cover.png").write_bytes(self.image)
        (unchanged / "notes").write_bytes(b"skipped with its album")
        before = {p.relative_to(self.source): p.read_bytes()
                  for p in self.source.rglob("*") if p.is_file()}
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "1\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 2, "audio files", copied=4)
        self.assertIn("Skipped: " + str(unchanged.relative_to(self.source)), result.stdout)
        self.assertEqual({p.relative_to(self.source): p.read_bytes()
                          for p in self.source.rglob("*") if p.is_file()}, before)
        destination = self.output / "FLAC" / "Library"
        skipped = unchanged.relative_to(self.source)
        kept = {relative: content for relative, content in before.items()
                if skipped not in relative.parents}
        for relative, content in kept.items():
            if relative.suffix.lower() != ".flac":
                self.assertEqual((destination / relative).read_bytes(), content)
        self.assertEqual(
            {p.relative_to(destination) for p in destination.rglob("*") if p.is_dir()},
            {p.relative_to(self.source) for p in self.source.rglob("*")
             if p.is_dir() and p != unchanged and unchanged not in p.parents})
        expected_files = {p.with_suffix(".flac") if p.suffix.lower() == ".flac" else p for p in kept}
        expected_files.update(Path(str(p.with_suffix(".flac")) + ".flac-metadata")
                              for p in kept if p.suffix.lower() == ".flac")
        expected_files.add(Path("conversion-audit.json"))
        self.assertEqual({p.relative_to(destination) for p in destination.rglob("*") if p.is_file()},
                         expected_files)
        report = self.audit(destination)
        self.assertEqual(report["skippedAudio"], [])
        self.assertEqual(report["counts"]["installedCompanions"], 4)
        self.assertEqual(report["counts"]["skippedUnchangedDirectories"], 1)
        self.assertEqual([(Path(item["source"]), item["files"], item["audioFiles"])
                          for item in report["skippedUnchangedDirectories"]],
                         [(skipped, len(extensions) + 2, len(extensions))])

    def test_flac_library_without_conversions_writes_only_report(self):
        for name in ("copy-only", "empty"):
            with self.subTest(name=name):
                source = self.work / name
                source.mkdir()
                (source / "empty child").mkdir()
                if name == "copy-only":
                    (source / "song.mp3").write_bytes(b"unchanged audio")
                    (source / "README").write_bytes(b"unchanged document")
                before = {p.relative_to(source): p.read_bytes()
                          for p in source.rglob("*") if p.is_file()}
                result = self.legacy(
                    [{"isActive": True, "mediaPath": str(source), "mediaName": name}], "1\nRUN\n",
                    FLACSettings={"ffmpeg_exe_name": str(self.work / "must-not-run.exe")})
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assert_summary(result, "Completed", 0, "audio files")
                destination = self.output / "FLAC" / name
                self.assertEqual([p.name for p in destination.iterdir()], ["conversion-audit.json"])
                report = self.audit(destination)
                self.assertEqual(report["status"], "completed")
                self.assertEqual(report["counts"]["plannedAudio"], 0)
                self.assertEqual(report["counts"]["plannedCompanions"], 0)
                self.assertEqual(report["skippedUnchangedDirectories"], [
                    {"source": ".", "files": len(before), "audioFiles": 1 if before else 0}])
                self.assertEqual({p.relative_to(source): p.read_bytes()
                                  for p in source.rglob("*") if p.is_file()}, before)

    def test_flac_directory_collision_fails_before_writing(self):
        (self.source / "collision.wav").write_bytes(b"not decoded during preflight")
        (self.source / "collision.flac").mkdir()
        self.convert(target="flac", success=False)
        self.assertFalse(self.output.exists())

    def test_flac_overwrite_writes_formerly_skipped_folder_completely(self):
        album = self.source / "album"
        album.mkdir()
        (album / "song.mp3").write_bytes(b"unchanged audio")
        (album / "cover.png").write_bytes(self.image)
        self.convert(target="flac")
        written = self.destination("flac") / "album"
        self.assertFalse(written.exists())
        self.assertEqual([item["source"] for item in
                          self.audit(self.destination("flac"))["skippedUnchangedDirectories"]], ["album"])
        (album / "new.flac").write_bytes(self.fixture.read_bytes())
        self.convert("--overwrite", target="flac")
        self.assertEqual({p.name for p in written.iterdir()},
                         {"song.mp3", "cover.png", "new.flac", "new.flac.flac-metadata"})
        self.assertEqual(self.audit(self.destination("flac"))["skippedUnchangedDirectories"], [])

    def test_flac_rejects_directory_links_instead_of_omitting_their_contents(self):
        linked_source = self.work / "linked target"
        linked_source.mkdir()
        (linked_source / "keep.txt").write_bytes(b"must not disappear silently")
        link = self.source / "directory link"
        if os.name == "nt":
            result = run(os.environ["ComSpec"], "/c", "mklink", "/J", link, linked_source)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            link.symlink_to(linked_source, target_is_directory=True)
        self.convert(target="flac", success=False)
        self.assertFalse(self.output.exists())
        self.assertEqual((linked_source / "keep.txt").read_bytes(), b"must not disappear silently")

    @unittest.skipUnless(os.name == "nt", "Windows sharing failure during copied audio")
    def test_flac_unsupported_audio_copy_failure_marks_output_failed(self):
        track = self.source / "bonus.mp3"
        track.write_bytes(b"original unconverted audio")
        with deny_delete(track, share=0):
            self.convert(target="flac", success=False)
        failed = next((self.output / "FLAC").glob("ERROR_COPY_*"))
        report = self.audit(failed)
        self.assertEqual(report["errorType"], "COPY")
        self.assertEqual(report["counts"]["installedAudio"], 1)
        self.assertEqual(report["counts"]["installedCompanions"], 0)
        self.assertTrue((failed / ".conversion-in-progress").is_dir())
        self.assertEqual(track.read_bytes(), b"original unconverted audio")

    def test_flac_existing_destinations_and_cross_library_preflight(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        target = self.output / "FLAC" / "Library" / self.track.name
        target.parent.mkdir(parents=True)
        sidecar = Path(str(target) + ".flac-metadata")
        sidecar.write_bytes(b"existing metadata")
        result = self.legacy(libraries, "1\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(target.exists())
        self.assertEqual(sidecar.read_bytes(), b"existing metadata")
        sidecar.unlink()
        second = self.work / "second"
        second.mkdir()
        (second / "two.flac").write_bytes(self.fixture.read_bytes())
        blocked = self.output / "FLAC" / "Second" / "two.flac"
        blocked.parent.mkdir(parents=True)
        blocked.write_bytes(b"existing output")
        libraries.append({"isActive": True, "mediaPath": str(second), "mediaName": "Second"})
        result = self.legacy(libraries, "1\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(target.exists())
        self.assertEqual(blocked.read_bytes(), b"existing output")
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)

    def test_flac_rejects_output_overlap_names_and_companion_sidecar_collision(self):
        for name in ("..", "Same"):
            libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": name}]
            if name == "Same":
                libraries += libraries
            result = self.legacy(libraries, "1\nRUN\n")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.output.exists())
        library = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(library, "1\nRUN\n", OutDirectory=str(self.source))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.source / "FLAC").exists())
        (self.source / (self.track.name + ".flac-metadata")).write_bytes(b"collision")
        result = self.legacy(library, "1\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.output.exists())

    def test_explicit_flac_preserves_metadata_and_companions(self):
        (self.source / "cover.png").write_bytes(self.image)
        self.convert(target="flac")
        target = self.destination("flac") / self.track.name
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)
        self.assertEqual((self.destination("flac") / "cover.png").read_bytes(), self.image)
        self.assertEqual(len([s for s in self.probe(target)["streams"]
                             if s.get("disposition", {}).get("attached_pic")]), 2)
        self.convert("--overwrite", target="flac")
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)

    def sidecar_config(self, **sidecars):
        settings = json.loads(self.config.read_text(encoding="utf-8"))
        settings["MetadataSidecars"].update(sidecars)
        path = self.work / "sidecar-config.json"
        path.write_text(json.dumps(settings), encoding="utf-8")
        return path

    def test_json_sidecar_holds_readable_metadata_and_exact_archive(self):
        config = self.sidecar_config(Format="json")
        for target, audio in (("mp3", self.mp3()), ("flac", self.destination("flac") / self.track.name)):
            self.convert(config=config, target=target)
            self.assertFalse(Path(str(audio) + ".flac-metadata").exists())
            sidecar = json.loads(Path(str(audio) + ".flac-metadata.json").read_text(encoding="utf-8"))
            self.assertEqual(sidecar["schema"], "AudioFormatConverter.FlacMetadataSidecar")
            self.assertEqual(base64.b64decode(sidecar["archive"]["data"]), self.prefix)
            self.assertIn("\"Album\"", json.dumps(sidecar["metadata"]["format"]))
            report = self.audit(audio.parent)
            self.assertEqual(report["metadataSidecars"], "json")
            self.assertTrue(any(f.get("metadataArchive", "").endswith(".flac-metadata.json")
                                for f in report["files"]))

    def test_disabled_sidecars_fail_closed_on_metadata_output_cannot_carry(self):
        config = self.sidecar_config(ConvertToMp3="disabled", ConvertToFlac="disabled")
        self.convert(config=config, success=False)
        self.assertFalse(self.mp3().exists())
        self.track.write_bytes(self.plain)
        self.original_hash = hashlib.sha256(self.plain).digest()
        self.convert(config=config, target="flac")
        target = self.destination("flac") / self.track.name
        self.assertTrue(target.exists())
        self.assertFalse(Path(str(target) + ".flac-metadata").exists())
        self.assertEqual(self.audit(target.parent)["metadataSidecars"], "none")

    def test_config_comments_and_omitted_policies_default_to_disabled(self):
        settings = json.loads(self.config.read_text(encoding="utf-8"))
        settings["MetadataSidecars"] = {}
        config_contents = json.dumps(settings, indent=2).replace(
            '  "MetadataSidecars": {}',
            '  "MetadataSidecars": {\n'
            '    // The following line selects the archive representation.\n'
            '    "Format": "binary"\n'
            '    // Omitted target policies use the disabled default.\n'
            '  }')
        config = self.work / "commented-config.json"
        config.write_text(config_contents, encoding="utf-8")

        self.track.write_bytes(self.plain)
        self.original_hash = hashlib.sha256(self.plain).digest()
        self.convert(config=config, target="flac")
        target = self.destination("flac") / self.track.name
        self.assertTrue(target.exists())
        self.assertFalse(Path(str(target) + ".flac-metadata").exists())
        self.assertEqual(self.audit(target.parent)["metadataSidecars"], "none")

    def test_sidecar_policy_controls_no_metadata_sidecar_flag(self):
        self.track.write_bytes(self.plain)
        self.original_hash = hashlib.sha256(self.plain).digest()
        result = self.convert("--no-metadata-sidecar", target="flac", success=False)
        self.assertIn("required", result.stderr)
        self.assertFalse(self.destination("flac").exists())
        config = self.sidecar_config(ConvertToFlac="optional")
        self.convert("--no-metadata-sidecar", target="flac", config=config)
        target = self.destination("flac") / self.track.name
        self.assertTrue(target.exists())
        self.assertFalse(Path(str(target) + ".flac-metadata").exists())
        self.convert("--overwrite", target="flac", config=config)
        self.assertTrue(Path(str(target) + ".flac-metadata").exists())

    def test_invalid_sidecar_setting_is_rejected(self):
        config = self.sidecar_config(Format="xml")
        result = self.convert(config=config, success=False)
        self.assertIn("MetadataSidecars.Format", result.stdout + result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_destination_and_sidecar(self):
        self.destination().mkdir(parents=True)
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
        self.assertTrue((self.destination() / "album" / "second.mp3").exists())
        self.assertEqual((self.destination() / "album" / "second.mp3.flac-metadata").read_bytes(), self.prefix)
        self.assertEqual((self.destination() / "album" / "folder.png").read_bytes(), self.image)
        self.assertEqual((self.destination() / "album" / "booklet.\u65e5\u672c").read_bytes(), b"unchanged companion")
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
            for description in ("Convert to FLAC [sources unchanged]", "NOT IMPLEMENTED",
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

    def test_back_from_flac_preview_can_select_mp3_without_replacing_sources(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "1\nBACK\n?\n6\n" + DEFAULT_MP3_SETTINGS + "RUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("Select run option"), 2)
        self.assertTrue((self.output / "MP3" / "Library" / self.mp3().name).exists())
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertNotIn("Type REPLACE to run", result.stdout)
        self.assertEqual(result.stdout.count("Type RUN to run"), 2)

    def test_menu_retries_then_runs_selected_action(self):
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "16\ninvalid\n5\n \t6 \t\n" + DEFAULT_MP3_SETTINGS + "RUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.output / "MP3" / "Library" / self.mp3().name).exists())

    def test_manual_flac_defaults_to_cancel(self):
        libraries = [
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "Active library"},
            {"isActive": False, "mediaPath": str(self.work / "inactive"), "mediaName": "Inactive"}]
        for selection in ("1\n\n", "1\n", "1\nno\n", "1\ny\n", "1\nreplace\n", "1\nREPLACE extra\n"):
            result = self.legacy(libraries, selection)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Cancelled. No action was started", result.stdout)
            preview = result.stdout.split("Operation preview:", 1)[1]
            self.assertIn(str(self.source), preview)
            self.assertIn("Active library", preview)
            self.assertNotIn(str(self.work / "inactive"), preview)
            self.assertIn("[Cancel]", preview)
            self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
            self.assertEqual(list(self.source.iterdir()), [self.track])
            self.assertFalse(self.output.exists())

    def test_manual_flac_requires_confirmation_and_preserves_sources(self):
        libraries = [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}]
        result = self.legacy(libraries, "1\n \tRUN \t\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Completed", 1, "audio files")
        target = self.output / "FLAC" / "Library" / self.track.name
        self.assertIn("Output location: " + str(target.parent), result.stdout)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        audio = self.probe(target)["streams"][0]
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))
        self.assertEqual(list(self.source.iterdir()), [self.track])
        self.assertEqual(Path(str(target) + ".flac-metadata").read_bytes(), self.prefix)

    def test_manual_flac_without_active_libraries_fails(self):
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

    def test_flac_preview_collision_never_converts(self):
        checked(ARGS.ffmpeg, "-v", "error", "-i", self.track, "-c:a", "pcm_s16le",
                self.track.with_suffix(".wav"))
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}],
            "1\nRUN\n")
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
        failed_output = next((self.output / "MP3").glob("ERROR_*_Second"))
        self.assertIn("Output location: " + str(failed_output), result.stdout)
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

    def test_flac_partial_completion_preserves_sources_and_completed_outputs(self):
        second = self.work / "second"
        second.mkdir()
        corrupt = second / "corrupt.flac"
        corrupt.write_bytes(self.fixture.read_bytes()[:50])
        result = self.legacy([
            {"isActive": True, "mediaPath": str(self.source), "mediaName": "First"},
            {"isActive": True, "mediaPath": str(second), "mediaName": "Second"}], "1\nRUN\n")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_summary(result, "Partially completed", 1, "audio files", failed=1)
        self.assertEqual(hashlib.sha256(self.track.read_bytes()).digest(), self.original_hash)
        self.assertTrue((self.output / "FLAC" / "First" / self.track.name).exists())
        self.assertFalse((self.output / "FLAC" / "Second" / "corrupt.flac").exists())
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

    def test_database_import_round_trips_string_fields_without_format_tags(self):
        json_directory = self.work / "metadata"
        json_directory.mkdir()
        json_file = json_directory / "media_out_metadata_1.json"
        json_file.write_text(json.dumps([{
            "Album Path": str(self.source / "album"),
            "Folder": "album",
            "Tracks": [{
                "format": {
                    "filename": str(self.track),
                    "size": "1234",
                    "nb_streams": 1,
                    "format_name": "flac",
                    "format_long_name": "raw FLAC",
                    "start_time": "0.250000",
                    "duration": "1.250000",
                    "bit_rate": "320000",
                    "probe_score": 100
                },
                "streams": [{
                    "index": 0,
                    "codec_name": "flac",
                    "codec_type": "audio",
                    "sample_rate": "44100",
                    "channels": 2,
                    "channel_layout": "stereo",
                    "bits_per_sample": 16,
                    "bit_rate": "320000",
                    "time_base": "1/44100",
                    "r_frame_rate": "0/0",
                    "avg_frame_rate": "0/0",
                    "start_pts": 11025,
                    "start_time": 0.25,
                    "duration_ts": 55125,
                    "nb_frames": "12",
                    "disposition": {"attached_pic": 0}
                }]
            }]
        }]), encoding="utf-8")

        database_output = self.work / "database-output"
        result = run(ARGS.converter, "db", "import", json_directory, "--output-dir", database_output)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        database = database_output / "metadata" / "media_out_metadata.db"
        with closing(sqlite3.connect(database)) as connection:
            row = connection.execute(
                "SELECT file_size, start_time, duration, bit_rate, title, stream1_index, "
                "stream1_codec_name, stream1_sample_rate, stream1_channels, stream1_start_time "
                "FROM TracksDB").fetchone()
        self.assertEqual(row[0], 1234)
        self.assertAlmostEqual(row[1], 0.25)
        self.assertAlmostEqual(row[2], 1.25)
        self.assertEqual(row[3], 320000)
        self.assertIsNone(row[4])
        self.assertEqual(row[5:9], (0, "flac", "44100", 2))
        self.assertAlmostEqual(row[9], 0.25)

    def test_database_import_rejects_malformed_track_without_partial_rows(self):
        json_directory = self.work / "malformed-metadata"
        json_directory.mkdir()
        json_file = json_directory / "media_out_malformed_1.json"
        valid_track = {
            "format": {"filename": str(self.track), "format_name": "flac"},
            "streams": [{"codec_type": "audio", "codec_name": "flac"}]
        }
        json_file.write_text(json.dumps([{
            "Album Path": str(self.source / "album"),
            "Folder": "album",
            "Tracks": [valid_track, {"streams": []}]
        }]), encoding="utf-8")

        database_output = self.work / "malformed-database-output"
        result = run(ARGS.converter, "db", "import", json_directory, "--output-dir", database_output)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

        database = database_output / "malformed-metadata" / "media_out_malformed-metadata.db"
        if database.exists():
            with closing(sqlite3.connect(database)) as connection:
                row_count = connection.execute("SELECT COUNT(*) FROM TracksDB").fetchone()[0]
            self.assertEqual(row_count, 0)

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

    def test_ffprobe_scan_treats_shell_metacharacters_as_filename_data(self):
        album = self.source / "album"
        album.mkdir()
        marker = self.work / "ffprobe-shell-marker"
        if os.name == "nt":
            variable = "AFC_SCAN_REVIEW_MARKER"
            environment = os.environ.copy()
            environment[variable] = "expanded"
            filename = f"track%{variable}%.flac"
        else:
            environment = os.environ.copy()
            filename = "track$(touch ffprobe-shell-marker).flac"
        (album / filename).write_bytes(self.fixture.read_bytes())

        config = json.loads(self.config.read_text(encoding="utf-8"))
        config.update({"UseFFmpegLibraryAPI": False, "UseAsyncFFmpegCalls": False})
        config_path = self.work / "ffprobe-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path, cwd=self.work, env=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        data = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(len(data[0]["Tracks"]), 1)
        self.assertEqual(data[0]["Tracks"][0]["format"]["tags"]["title"], "Title \u97f3\u697d")
        if os.name != "nt":
            self.assertFalse(marker.exists(), "Filename was interpreted as shell syntax")

    def test_async_ffprobe_scan_retains_raw_stream_fields(self):
        album = self.source / "album"
        album.mkdir()
        (album / "track.flac").write_bytes(self.fixture.read_bytes())

        config = json.loads(self.config.read_text(encoding="utf-8"))
        config.update({"UseFFmpegLibraryAPI": False, "UseAsyncFFmpegCalls": True})
        config_path = self.work / "async-ffprobe-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        data = json.loads(output.read_text(encoding="utf-8"))
        stream = data[0]["Tracks"][0]["streams"][0]
        self.assertEqual(stream["codec_long_name"], "FLAC (Free Lossless Audio Codec)")

    def test_metadata_scan_worker_limit_handles_thousand_track_album(self):
        album = self.source / "album"
        album.mkdir()
        fixture_bytes = self.fixture.read_bytes()
        for index in range(1000):
            (album / f"track-{index:04d}.flac").write_bytes(fixture_bytes)

        config = json.loads(self.config.read_text(encoding="utf-8"))
        config.update({"UseAsyncFFmpegCalls": True, "MetadataScanMaxThreads": 2})
        config_path = self.work / "bounded-metadata-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        data = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(len(data[0]["Tracks"]), 1000)

    def test_metadata_scan_rejects_negative_worker_limit(self):
        for name, value in (("negative", -1), ("string", "two")):
            with self.subTest(value=value):
                config = json.loads(self.config.read_text(encoding="utf-8"))
                config["MetadataScanMaxThreads"] = value
                config_path = self.work / f"invalid-metadata-config-{name}.json"
                config_path.write_text(json.dumps(config), encoding="utf-8")

                result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                             "--config", config_path)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("MetadataScanMaxThreads", result.stdout + result.stderr)

    def test_incomplete_scan_preserves_previous_json_and_accepts_uppercase_extension(self):
        album = self.source / "album"
        album.mkdir()
        track = album / "track.FLAC"
        track.write_bytes(self.fixture.read_bytes())
        config = json.loads(self.config.read_text(encoding="utf-8"))
        config.update({"UseFFmpegLibraryAPI": False, "UseAsyncFFmpegCalls": False})
        config_path = self.work / "incomplete-scan-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")

        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        previous = output.read_bytes()
        data = json.loads(previous)
        self.assertEqual(len(data[0]["Tracks"]), 1)

        track.write_bytes(b"")
        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(output.read_bytes(), previous)

    def test_scan_retains_chapters_and_attached_picture_disposition(self):
        album = self.source / "album"
        album.mkdir()
        chapter_metadata = self.work / "chapters.txt"
        chapter_metadata.write_text(
            ";FFMETADATA1\n"
            "[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=600\ntitle=First chapter\n"
            "[CHAPTER]\nTIMEBASE=1/1000\nSTART=600\nEND=1300\ntitle=Second chapter\n",
            encoding="utf-8")
        chapter_track = album / "chapters.flac"
        checked(ARGS.ffmpeg, "-y", "-v", "error", "-f", "lavfi", "-i",
                "sine=frequency=997:sample_rate=48000:duration=1.3", "-i", chapter_metadata,
                "-map", "0:a:0", "-map_metadata", "1", "-map_chapters", "1",
                "-c:a", "flac", "-f", "matroska", chapter_track)

        artwork_track = album / "artwork.flac"
        checked(ARGS.ffmpeg, "-y", "-v", "error", "-i", self.fixture, "-i",
                self.root / "image.png", "-map", "0:a:0", "-map", "1:v:0",
                "-c:a", "copy", "-c:v", "copy", "-disposition:v", "attached_pic", artwork_track)

        config = json.loads(self.config.read_text(encoding="utf-8"))
        config["UseFFmpegLibraryAPI"] = False
        config_path = self.work / "chapter-ffprobe-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", config_path)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        data = json.loads(output.read_text(encoding="utf-8"))
        tracks = {Path(track["format"]["filename"]).name: track for track in data[0]["Tracks"]}
        self.assertEqual(len(tracks["chapters.flac"]["chapters"]), 2)
        self.assertEqual(tracks["chapters.flac"]["chapters"][0]["tags"]["title"], "First chapter")
        chapter_stream = tracks["chapters.flac"]["streams"][0]
        self.assertEqual(chapter_stream["r_frame_rate"], "0/0")
        self.assertTrue(chapter_stream["time_base"].count("/") == 1)
        artwork_stream = next(stream for stream in tracks["artwork.flac"]["streams"]
                              if stream["codec_type"] == "video")
        self.assertEqual(artwork_stream["disposition"]["attached_pic"], 1)

    def test_scan_sort_uses_album_artist_order(self):
        for folder, artist in (("a", "Zebra"), ("b", "Alpha"), ("c", "Beta")):
            album = self.source / folder
            album.mkdir()
            track = album / "one.flac"
            checked(ARGS.ffmpeg, "-y", "-v", "error", "-i", self.fixture, "-c", "copy",
                    "-metadata", f"album_artist={artist}", track)

        result = run(ARGS.converter, "scan", self.source, "--output-dir", self.output,
                     "--config", self.config)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = self.output / self.source.name / f"media_out_{self.source.name}_1.json"
        data = json.loads(output.read_text(encoding="utf-8"))
        artists = [album["Tracks"][0]["format"]["tags"]["album_artist"] for album in data]
        self.assertEqual(artists, ["Alpha", "Beta", "Zebra"])

    def test_manual_flac_wav_conversion_retains_non_flac_source(self):
        wav = self.track.with_suffix(".wav")
        checked(ARGS.ffmpeg, "-v", "error", "-i", self.track, "-c:a", "pcm_s24le", wav)
        self.track.unlink()
        original = wav.read_bytes()
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "1\nRUN\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(wav.read_bytes(), original)
        target = self.output / "FLAC" / "Library" / self.track.name
        audio = self.probe(target)["streams"][0]
        self.assertEqual((audio["codec_name"], audio["sample_rate"], audio["bits_per_raw_sample"]),
                         ("flac", "44100", "16"))
        self.assertFalse(self.track.exists())
        self.assertFalse(Path(str(target) + ".flac-metadata").exists())
        audit = self.audit(target.parent)
        self.assertTrue(any(item["name"] == "comment" and item["status"] == "Normalized"
                            for item in audit["files"][0]["resources"]))

    def test_manual_flac_failure_preserves_original(self):
        self.track.write_bytes(self.fixture.read_bytes()[:50])
        before = self.track.read_bytes()
        result = self.legacy(
            [{"isActive": True, "mediaPath": str(self.source), "mediaName": "Library"}], "1\nRUN\n")
        self.assertNotEqual(result.returncode, 0)
        self.assert_summary(result, "Failed", 0, "audio files", failed=1)
        self.assertEqual(self.track.read_bytes(), before)
        self.assertEqual(list(self.source.iterdir()), [self.track])
        failed = next((self.output / "FLAC").glob("ERROR_*_Library"))
        self.assertFalse((failed / self.track.name).exists())
        self.assertTrue((failed / ".conversion-in-progress").exists())
        self.assertEqual(json.loads((failed / "conversion-audit.json").read_text())["status"], "failed")

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
        audio = self.probe(self.destination("flac") / self.name)["streams"][0]
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
        self.convert(target="flac")
        self.assertEqual(Path(str(self.destination("flac") / self.name) + ".flac-metadata").read_bytes(),
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
        self.convert(target="flac")
        self.assertEqual(Path(str(self.destination("flac") / self.name) + ".flac-metadata").read_bytes(), prefix)
        self.assertEqual(len([s for s in self.probe(self.destination("flac") / self.name)["streams"]
                             if s.get("disposition", {}).get("attached_pic")]), 2)

    def test_truncated_audio_never_installed(self):
        source = self.fixture.read_bytes()
        self.track.write_bytes(source[:len(self.prefix) + (len(source) - len(self.prefix)) // 2])
        self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
        self.convert(success=False)
        self.assertFalse(self.mp3().exists())
        self.assertFalse(list(self.output.glob(".afc-convert-*")))
        self.convert(success=False, target="flac")
        self.assertFalse((self.output / self.name).exists())
        self.assertFalse(Path(str(self.output / self.name) + ".flac-metadata").exists())

    @unittest.skipUnless(os.name == "nt", "Windows deny-delete replacement failure")
    def test_failed_replacement_restores_sidecar(self):
        self.destination().mkdir(parents=True)
        self.mp3().write_bytes(b"existing MP3")
        sidecar = Path(str(self.mp3()) + ".flac-metadata")
        sidecar.write_bytes(b"existing sidecar")
        with deny_delete(self.mp3()):
            self.convert("--overwrite", success=False)
            self.assertEqual(self.mp3().read_bytes(), b"existing MP3")
            self.assertEqual(sidecar.read_bytes(), b"existing sidecar")
            self.assertEqual(self.audit(self.destination())["errorType"], "IO")
            self.assertFalse(list((self.output / "MP3").glob("ERROR_*")))

    @unittest.skipUnless(os.name == "nt", "Windows deny-delete replacement failure")
    def test_failed_flac_replacement_restores_sidecar(self):
        self.destination("flac").mkdir(parents=True)
        target = self.destination("flac") / self.name
        target.write_bytes(b"existing FLAC")
        sidecar = Path(str(target) + ".flac-metadata")
        sidecar.write_bytes(b"existing sidecar")
        with deny_delete(target):
            self.convert("--overwrite", success=False, target="flac")
            self.assertEqual(target.read_bytes(), b"existing FLAC")
            self.assertEqual(sidecar.read_bytes(), b"existing sidecar")

    def test_flac_rates_channels_silence_and_short_audio(self):
        for rate, channels, duration in ((44100, 1, "0.02"), (96000, 2, "0.1"), (48000, 6, "0.4")):
            self.track.unlink()
            checked(ARGS.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
                    f"anullsrc=r={rate}:cl={channels}c", "-t", duration, "-c:a", "flac", self.track)
            self.original_hash = hashlib.sha256(self.track.read_bytes()).digest()
            self.convert("--overwrite", target="flac")
            audio = self.probe(self.destination("flac") / self.name)["streams"][0]
            self.assertEqual((audio["sample_rate"], audio["channels"]), ("44100", channels))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--ffmpeg", type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--ffprobe", type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--audit-checker", type=lambda value: Path(value).resolve())
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[__file__, *remaining])
