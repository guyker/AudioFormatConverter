"""Verify that a scan includes audio files directly under the library root."""

import json
import re
import hashlib
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import closing
from pathlib import Path


def refresh_manifest_batch(directory, batch_path):
    manifest_path = directory / "scan-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    batch = next(item for item in manifest["batches"] if item["file"] == batch_path.name)
    content = batch_path.read_bytes()
    batch["sizeBytes"] = len(content)
    batch["sha256"] = hashlib.sha256(content).hexdigest()
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")


def main():
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: scan_root_audio.py CONVERTER FFMPEG REPORT_VALIDATOR REPORT_SCHEMA MANIFEST_SCHEMA")

    converter, ffmpeg, report_validator, report_schema, manifest_schema = sys.argv[1:]

    def validate_report(report_path):
        report_path_arg = str(report_path.resolve())
        if os.name == "nt" and len(report_path_arg) >= 240:
            report_path_arg = "\\\\?\\" + report_path_arg
        subprocess.run(
            [report_validator, report_schema, report_path_arg],
            check=True,
        )

    with tempfile.TemporaryDirectory(prefix="afc-root-scan-") as temporary:
        root = Path(temporary)
        library = root / "library"
        output = root / "output"
        default_config = root / "default-config.json"
        default_config.write_text("{}", encoding="utf-8")
        library.mkdir()
        track = library / "root-track.flac"
        subprocess.run(
            [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
             "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", "flac",
             "-metadata", "title=Root-level track", "-metadata", "track=0/1",
             "-metadata", "date=2025-13-40", "-metadata", "ISRC=BAD",
             "-metadata", "barcode=1234567890123",
             "-metadata", "musicbrainz_albumid=not-a-uuid", str(track)],
            check=True,
        )
        original_track_hash = hashlib.sha256(track.read_bytes()).hexdigest()
        subprocess.run(
            [converter, "scan", str(library), "--output-dir", str(output),
             "--config", str(default_config)],
            check=True,
        )
        files = list((output / library.name).glob("media_out_*.json"))
        if len(files) != 1:
            raise AssertionError(f"Expected one generated JSON file, found {len(files)}")
        manifest_path = output / library.name / "scan-manifest.json"
        subprocess.run(
            [report_validator, manifest_schema, str(manifest_path.resolve())],
            check=True,
        )
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        batch_entry = manifest["batches"][0]
        if (batch_entry["file"] != files[0].name or
                batch_entry["sha256"] != hashlib.sha256(files[0].read_bytes()).hexdigest()):
            raise AssertionError("The scan manifest does not integrity-protect its JSON batch")
        if (manifest["archives"] or manifest["unsupportedFormats"] or
                not manifest["archivePolicy"].startswith("Disabled") or
                list((output / library.name).glob("scan_metadata_*.flac-metadata"))):
            raise AssertionError("Default scans must not create metadata archives")
        default_record = json.loads(files[0].read_text(encoding="utf-8"))[0]["Tracks"][0]
        subprocess.run(
            [converter, "scan", str(library), "--output-dir", str(output),
             "--config", str(default_config), "--archive-flac-metadata"],
            check=True,
        )
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        subprocess.run(
            [report_validator, manifest_schema, str(manifest_path.resolve())],
            check=True,
        )
        archived_record = json.loads(files[0].read_text(encoding="utf-8"))[0]["Tracks"][0]
        for field in ("tags", "rawTags", "technical", "pictures", "chapters"):
            if archived_record[field] != default_record[field]:
                raise AssertionError(f"Enabling archives changed the JSON {field}")
        if not manifest["archivePolicy"].startswith("Enabled"):
            raise AssertionError("The manifest did not report enabled metadata archiving")
        if len(manifest["archives"]) != 1:
            raise AssertionError("Native FLAC metadata was not archived")
        archive_entry = manifest["archives"][0]
        archive_path = output / library.name / archive_entry["file"]
        if (not archive_path.is_file() or archive_entry["sourcePath"] != track.name or
                archive_entry["sha256"] != hashlib.sha256(archive_path.read_bytes()).hexdigest() or
                archive_path.stat().st_size != archive_entry["sizeBytes"] or
                archive_path.stat().st_size >= track.stat().st_size):
            raise AssertionError("FLAC metadata archive is missing, unverified, or contains audio")
        if hashlib.sha256(track.read_bytes()).hexdigest() != original_track_hash:
            raise AssertionError("Scanning modified its FLAC source")

        archive_library = root / "archive-library"
        archive_album = archive_library / "album"
        archive_album.mkdir(parents=True)
        archived_flac = archive_album / "track.flac"
        shutil.copyfile(track, archived_flac)
        archived_mp3 = archive_album / "track.mp3"
        subprocess.run(
            [ffmpeg, "-v", "error", "-y", "-i", str(archived_flac), "-c:a", "libmp3lame",
             "-q:a", "4", str(archived_mp3)],
            check=True,
        )
        archive_flac_hash = hashlib.sha256(archived_flac.read_bytes()).hexdigest()
        archive_mp3_hash = hashlib.sha256(archived_mp3.read_bytes()).hexdigest()
        archive_output = root / "archive-output"
        archive_output.mkdir()
        archive_target = archive_output / archive_library.name
        archive_target.mkdir()
        unrelated_bytes = b"unrelated output must survive"
        database_bytes = b"database placeholder must survive"
        (archive_target / "unrelated.txt").write_bytes(unrelated_bytes)
        (archive_target / "catalog-v2.sqlite").write_bytes(database_bytes)
        archive_command = [
            converter, "scan", str(archive_library), "--output-dir", str(archive_output),
            "--config", str(default_config), "--archive-flac-metadata"]
        subprocess.run(archive_command, check=True)
        archive_manifest_path = archive_target / "scan-manifest.json"
        archive_manifest = json.loads(archive_manifest_path.read_text(encoding="utf-8"))
        previous_manifest_bytes = archive_manifest_path.read_bytes()
        if (len(archive_manifest["archives"]) != 1 or
                not any(item["format"] == "mp3" and item["count"] == 1
                        for item in archive_manifest["unsupportedFormats"])):
            raise AssertionError("Native FLAC-only archive coverage was not reported")
        previous_archive = archive_target / archive_manifest["archives"][0]["file"]
        previous_archive_bytes = previous_archive.read_bytes()
        previous_batch_files = list(archive_target.glob("media_out_*.json"))
        previous_batch_bytes = {item.name: item.read_bytes() for item in previous_batch_files}
        metadata_fields = ("tags", "rawTags", "technical", "pictures", "chapters")

        def catalog_metadata(batch_files):
            return {
                record["file"]["relativePath"]: {key: record[key] for key in metadata_fields}
                for batch in batch_files
                for album in json.loads(batch.read_text(encoding="utf-8"))
                for record in album["Tracks"]
            }

        previous_metadata = catalog_metadata(previous_batch_files)
        new_flac = archive_album / "new.flac"
        shutil.copyfile(archived_flac, new_flac)
        collision_name = "scan_metadata_" + hashlib.sha256(
            ("album" + os.sep + "new.flac").encode("utf-8")).hexdigest() + ".flac-metadata"
        collision_path = archive_target / collision_name
        collision_bytes = b"unmanaged collision"
        collision_path.write_bytes(collision_bytes)
        failed_archive_install = subprocess.run(
            archive_command, capture_output=True, timeout=30)
        if failed_archive_install.returncode == 0:
            raise AssertionError("A colliding unowned archive path was overwritten")
        if (archive_manifest_path.read_bytes() != previous_manifest_bytes or
                {item.name: item.read_bytes() for item in archive_target.glob("media_out_*.json")} !=
                previous_batch_bytes or previous_archive.read_bytes() != previous_archive_bytes or
                collision_path.read_bytes() != collision_bytes):
            raise AssertionError("A failed archive install did not roll back prior scan outputs")
        if hashlib.sha256(archived_flac.read_bytes()).hexdigest() != archive_flac_hash:
            raise AssertionError("A failed scan modified its FLAC source")
        new_flac.unlink()

        corrupt_track = archive_album / "corrupt.flac"
        corrupt_track.write_bytes(b"not a FLAC")
        disabled_command = [
            converter, "scan", str(archive_library), "--output-dir", str(archive_output),
            "--config", str(default_config)]
        failed_disabled = subprocess.run(disabled_command, capture_output=True, timeout=30)
        if (failed_disabled.returncode == 0 or
                archive_manifest_path.read_bytes() != previous_manifest_bytes or
                previous_archive.read_bytes() != previous_archive_bytes or
                {item.name: item.read_bytes() for item in archive_target.glob("media_out_*.json")} !=
                previous_batch_bytes):
            raise AssertionError("A failed scan with archives disabled did not preserve the previous set")
        corrupt_track.unlink()
        subprocess.run(disabled_command, check=True)
        disabled_manifest = json.loads(archive_manifest_path.read_text(encoding="utf-8"))
        if (disabled_manifest["archives"] or disabled_manifest["unsupportedFormats"] or
                not disabled_manifest["archivePolicy"].startswith("Disabled") or
                previous_archive.exists() or collision_path.read_bytes() != collision_bytes):
            raise AssertionError("Disabling archives did not retire only prior manifest-owned archives")
        if catalog_metadata(archive_target.glob("media_out_*.json")) != previous_metadata:
            raise AssertionError("Disabling archives changed FLAC or MP3 catalog metadata")
        disabled_report = json.loads((archive_target / "scan-report.json").read_text(encoding="utf-8"))
        if any(item["code"] == "metadata_archive_unsupported"
               for item in disabled_report["diagnostics"]):
            raise AssertionError("A disabled archive export reported unsupported formats")
        if (hashlib.sha256(archived_flac.read_bytes()).hexdigest() != archive_flac_hash or
                hashlib.sha256(archived_mp3.read_bytes()).hexdigest() != archive_mp3_hash):
            raise AssertionError("Disabling metadata archives modified source audio")
        subprocess.run(
            [report_validator, manifest_schema, str(archive_manifest_path.resolve())],
            check=True,
        )
        collision_path.unlink()
        subprocess.run(archive_command, check=True)
        restored_manifest = json.loads(archive_manifest_path.read_text(encoding="utf-8"))
        if len(restored_manifest["archives"]) != 1 or not previous_archive.is_file():
            raise AssertionError("Re-enabling archives did not create a new verified archive")
        archived_flac.unlink()
        subprocess.run(archive_command, check=True)
        latest_manifest = json.loads(archive_manifest_path.read_text(encoding="utf-8"))
        if latest_manifest["archives"] or previous_archive.exists():
            raise AssertionError("A successful scan did not retire the previous FLAC archives")
        if ((archive_target / "unrelated.txt").read_bytes() != unrelated_bytes or
                (archive_target / "catalog-v2.sqlite").read_bytes() != database_bytes):
            raise AssertionError("A successful scan changed unrelated output files")
        if (hashlib.sha256(archived_mp3.read_bytes()).hexdigest() != archive_mp3_hash or
                hashlib.sha256(track.read_bytes()).hexdigest() != original_track_hash):
            raise AssertionError("A metadata scan modified source audio")
        albums = json.loads(files[0].read_text(encoding="utf-8"))
        if len(albums) != 1 or len(albums[0].get("Tracks", [])) != 1:
            raise AssertionError("The root-level audio track was omitted from scan output")
        record = albums[0]["Tracks"][0]
        if record.get("schemaVersion") != 2:
            raise AssertionError("Scan output did not use media catalog schema v2")
        if record["file"]["relativePath"] != track.name:
            raise AssertionError("The catalog record does not identify the relative audio path")
        if record["tags"]["title"] != ["Root-level track"]:
            raise AssertionError("The root-level audio track metadata was not preserved")
        if not record["rawTags"]:
            raise AssertionError("The catalog record did not include source tag entries")
        finding_codes = {finding["code"] for finding in record["findings"]}
        expected_findings = {
            "MISSING_ARTIST", "MISSING_ALBUM", "INVALID_TRACK_NUMBER",
            "NON_ISO_DATE", "INVALID_ISRC", "INVALID_MUSICBRAINZ_ID", "INVALID_BARCODE",
        }
        if not expected_findings.issubset(finding_codes):
            raise AssertionError(
                f"Tag validation findings were incomplete: {sorted(finding_codes)}")
        report_path = output / library.name / "scan-report.json"
        validate_report(report_path)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if report["schemaVersion"] != 2 or report["status"] != "completed":
            raise AssertionError("A successful scan did not write a completed scan report")
        fallback_temp = root / "fallback-temp"
        fallback_temp.mkdir()
        fallback_env = dict(os.environ, TEMP=str(fallback_temp), TMP=str(fallback_temp),
                            TMPDIR=str(fallback_temp))
        def fallback_report(result):
            output_text = result.stdout + result.stderr
            match = re.search(r"Fallback scan report saved to '([^']+)'", output_text)
            if not match:
                raise AssertionError(f"Fallback report location was not logged: {output_text}")
            path = Path(match.group(1))
            validate_report(path)
            return json.loads(path.read_text(encoding="utf-8"))
        preflight = subprocess.run(
            [converter, "scan", str(library), "--output-dir", str(library)],
            capture_output=True, text=True, errors="replace", timeout=30, env=fallback_env)
        if preflight.returncode == 0:
            raise AssertionError("Overlapping scan output was accepted")
        if not any(item["code"] == "scan_preflight_failed"
                   for item in fallback_report(preflight)["diagnostics"]):
            raise AssertionError("Fallback report omitted the preflight failure")
        blocked_output = root / "blocked-output"
        blocked_output.mkdir()
        (blocked_output / library.name).write_text("not a directory", encoding="utf-8")
        blocked = subprocess.run(
            [converter, "scan", str(library), "--output-dir", str(blocked_output)],
            capture_output=True, text=True, errors="replace", timeout=30, env=fallback_env)
        if blocked.returncode == 0:
            raise AssertionError("Blocked scan output was accepted")
        if not any(item["code"] == "scan_report_write_failed"
                   for item in fallback_report(blocked)["diagnostics"]):
            raise AssertionError("Fallback report omitted the report-write failure")
        invalid_report_path = root / "invalid-report.json"
        for invalid_report in (dict(report, schemaVersion=1),
                               dict(report, diagnostics=[{"stage": "invalid"}])):
            invalid_report_path.write_text(json.dumps(invalid_report), encoding="utf-8")
            if subprocess.run([report_validator, report_schema, str(invalid_report_path)],
                              capture_output=True).returncode == 0:
                raise AssertionError("Report schema validation accepted an invalid version or diagnostic")
        counts = report["counts"]
        if (counts["albumsDiscovered"] != 1 or counts["tracksDiscovered"] != 1 or
                counts["albumsSerialized"] != 1 or counts["tracksSerialized"] != 1 or
                counts["batchesStaged"] != 1 or counts["batchesInstalled"] != 1):
            raise AssertionError("The successful scan report contains incorrect lifecycle counts")

        failed_library = root / "failed-scan-library"
        failed_library.mkdir()
        (failed_library / "valid.flac").write_bytes(track.read_bytes())
        failed_track = failed_library / "broken.flac"
        failed_track.write_bytes(b"not an audio stream")
        failed_output = root / "failed-scan-output" / failed_library.name
        failed_output.mkdir(parents=True)
        previous_json = failed_output / "media_out_previous.json"
        previous_bytes = b'[{"previous":"catalog"}]\n'
        previous_json.write_bytes(previous_bytes)
        failed_scan = subprocess.run(
            [converter, "scan", str(failed_library), "--output-dir",
             str(failed_output.parent)],
            capture_output=True, timeout=30,
        )
        if failed_scan.returncode == 0:
            raise AssertionError("A scan with an unreadable accepted track succeeded")
        if previous_json.read_bytes() != previous_bytes:
            raise AssertionError("A failed scan replaced the previously generated catalog JSON")
        failed_report_path = failed_output / "scan-report.json"
        validate_report(failed_report_path)
        failed_report = json.loads(failed_report_path.read_text(encoding="utf-8"))
        matching_diagnostics = [
            item for item in failed_report["diagnostics"]
            if item.get("filePath") == str(failed_track.resolve())
        ]
        if failed_report["status"] != "failed" or not matching_diagnostics:
            raise AssertionError("The failed scan report did not identify the unreadable track")
        if not any(item["severity"] == "error" and item["message"]
                   for item in matching_diagnostics):
            raise AssertionError("The failed track diagnostic omitted its underlying error")
        failed_counts = failed_report["counts"]
        if (failed_counts["albumsDiscovered"] != 1 or
                failed_counts["tracksDiscovered"] != 2 or
                failed_counts["albumsSerialized"] != 0 or
                failed_counts["tracksSerialized"] != 0 or
                failed_counts["batchesStaged"] != 0 or
                failed_counts["batchesInstalled"] != 0):
            raise AssertionError("The failed scan report misrepresented staged or installed output")

        multiple_library = root / "multiple-albums"
        broken_album = multiple_library / "a-broken"
        valid_album = multiple_library / "z-valid"
        broken_album.mkdir(parents=True)
        valid_album.mkdir()
        (broken_album / "broken.flac").write_bytes(b"not an audio stream")
        (valid_album / "valid.flac").write_bytes(track.read_bytes())
        small_batches = root / "small-batches.json"
        small_batches.write_text(json.dumps({"AlbumsSplitThreshold": 1}), encoding="utf-8")
        multiple_output = root / "multiple-output"
        result = subprocess.run(
            [converter, "scan", str(multiple_library), "--config", str(small_batches),
             "--output-dir", str(multiple_output)], capture_output=True, timeout=30)
        if result.returncode == 0:
            raise AssertionError("Invalid first batch unexpectedly succeeded")
        multiple_report_path = multiple_output / multiple_library.name / "scan-report.json"
        validate_report(multiple_report_path)
        multiple_report = json.loads(multiple_report_path.read_text(encoding="utf-8"))
        if (multiple_report["counts"]["albumsDiscovered"] != 2 or
                multiple_report["counts"]["tracksDiscovered"] != 2):
            raise AssertionError("Discovery counts omit albums after a failed first batch")
        generic_failures = [item for item in multiple_report["diagnostics"]
                            if item["code"] == "scan_failed"]
        if not generic_failures or any("albumPath" in item for item in generic_failures):
            raise AssertionError("Library-scoped failure was attributed to an album")

        sized_library = root / "sized-library"
        for index in range(3):
            folder = sized_library / f"album-{index}"
            folder.mkdir(parents=True)
            (folder / "track.flac").write_bytes(track.read_bytes())
        (sized_library / "album-0" / "track-2.flac").write_bytes(track.read_bytes())
        sized_output = root / "sized-output"
        size_config = root / "size-config.json"
        size_config.write_text(json.dumps({"Incremental": False}), encoding="utf-8")
        command = [converter, "scan", str(sized_library), "--config", str(size_config),
                   "--output-dir", str(sized_output)]
        subprocess.run(command, check=True)
        sized_dir = sized_output / sized_library.name
        initial_parts = list(sized_dir.glob("media_out_*.json"))
        if len(initial_parts) != 1:
            raise AssertionError("Disabled byte target changed album-count batching")
        initial_albums = json.loads(initial_parts[0].read_text(encoding="utf-8"))
        size_config.write_text(json.dumps({
            "Incremental": False, "JsonBatchTargetBytes": 10000000
        }), encoding="utf-8")
        subprocess.run(command, check=True)
        preliminary = []
        for album in initial_albums:
            if len(album["Tracks"]) == 1:
                preliminary.append(album)
            else:
                preliminary.extend({**album, "Tracks": [record],
                                    "Fragment": {"index": index, "count": len(album["Tracks"]),
                                                 "set": "x" * 40}}
                                   for index, record in enumerate(album["Tracks"]))
        byte_target = max(len(json.dumps(album, ensure_ascii=False, separators=(",", ":"))
                              .encode("utf-8")) + 18 for album in preliminary)
        size_config.write_text(json.dumps({
            "Incremental": False, "JsonBatchTargetBytes": byte_target
        }), encoding="utf-8")
        subprocess.run(command, check=True)
        preliminary = [album for part in sized_dir.glob("media_out_*.json")
                       for album in json.loads(part.read_text(encoding="utf-8"))]
        byte_target = max(len(json.dumps(album, ensure_ascii=False, separators=(",", ":"))
                              .encode("utf-8")) + 2 for album in preliminary)
        size_config.write_text(json.dumps({
            "Incremental": False, "JsonBatchTargetBytes": byte_target
        }), encoding="utf-8")
        subprocess.run(command, check=True)
        sized_parts = list(sized_dir.glob("media_out_*.json"))
        if len(sized_parts) < 3:
            raise AssertionError("Byte limit did not split the catalog")
        for part in sized_parts:
            if part.stat().st_size > byte_target:
                raise AssertionError("A batch exceeded the byte limit")
        sized_albums = [album for part in sized_parts
                        for album in json.loads(part.read_text(encoding="utf-8"))]
        identities = lambda albums: sorted((album["Album Path"], record["file"]["relativePath"])
                                            for album in albums for record in album["Tracks"])
        if identities(sized_albums) != identities(initial_albums):
            raise AssertionError("Size-aware batching lost or duplicated tracks")
        split_album = [album for album in sized_albums if "Fragment" in album]
        if len(split_album) != 2 or {album["Fragment"]["index"] for album in split_album} != {0, 1}:
            raise AssertionError("A two-track album was not represented by complete fragments")
        if not any(part.stat().st_size == byte_target for part in sized_parts):
            raise AssertionError("A record that exactly fits the byte limit was not accepted")

        database_output = root / "sized-database"
        import_command = [converter, "db", "import", str(sized_dir),
                          "--output-dir", str(database_output)]
        subprocess.run(import_command, check=True)
        database = database_output / sized_library.name / "catalog-v2.sqlite"
        def db_paths():
            with closing(sqlite3.connect(database)) as connection:
                return connection.execute("SELECT relative_path FROM files ORDER BY relative_path").fetchall()
        expected_db_paths = db_paths()
        if len(expected_db_paths) != 4:
            raise AssertionError("Database import did not reassemble the split album")
        missing_batch = sized_parts[0]
        missing_batch_bytes = missing_batch.read_bytes()
        missing_batch.unlink()
        for check_command in (import_command, [converter, "duplicates", str(sized_dir)]):
            if subprocess.run(check_command, capture_output=True, timeout=30).returncode == 0:
                raise AssertionError("A missing complete JSON batch was accepted")
        if db_paths() != expected_db_paths:
            raise AssertionError("A missing batch changed the existing database")
        missing_batch.write_bytes(missing_batch_bytes)

        manifest_bytes = (sized_dir / "scan-manifest.json").read_bytes()
        (sized_dir / "scan-manifest.json").unlink()
        if subprocess.run(import_command, capture_output=True, timeout=30).returncode == 0:
            raise AssertionError("Catalog batches were accepted without their manifest")
        (sized_dir / "scan-manifest.json").write_bytes(manifest_bytes)
        fragment_file = next(part for part in sized_parts
                             if any("Fragment" in album for album in
                                    json.loads(part.read_text(encoding="utf-8"))))
        saved = fragment_file.read_bytes()
        fragment_file.unlink()
        for check_command in (import_command, [converter, "duplicates", str(sized_dir)]):
            if subprocess.run(check_command, capture_output=True, timeout=30).returncode == 0:
                raise AssertionError("A missing album fragment was accepted")
        if db_paths() != expected_db_paths:
            raise AssertionError("Missing fragments changed the existing database")
        fragment_file.write_bytes(saved)
        duplicate = sized_dir / "media_out_duplicate.json"
        duplicate.write_bytes(saved)
        if subprocess.run(import_command, capture_output=True, timeout=30).returncode == 0:
            raise AssertionError("A duplicated album fragment was accepted")
        duplicate.unlink()
        if db_paths() != expected_db_paths:
            raise AssertionError("Duplicate fragments changed the existing database")
        altered = json.loads(saved)
        next(album for album in altered if "Fragment" in album)["Fragment"]["set"] += "-other"
        fragment_file.write_text(json.dumps(altered), encoding="utf-8")
        if subprocess.run(import_command, capture_output=True, timeout=30).returncode == 0:
            raise AssertionError("Fragments from different scans were accepted")
        if db_paths() != expected_db_paths:
            raise AssertionError("Mixed scan fragments changed the existing database")
        fragment_file.write_bytes(saved)

        previous_parts = {part.name: part.read_bytes()
                          for part in sized_dir.glob("media_out_*.json")}
        size_config.write_text(json.dumps({
            "Incremental": False, "JsonBatchTargetBytes": 1
        }), encoding="utf-8")
        if subprocess.run(command, capture_output=True, timeout=30).returncode == 0:
            raise AssertionError("An oversized track was accepted")
        oversized_report_path = sized_dir / "scan-report.json"
        validate_report(oversized_report_path)
        oversized_report = json.loads(oversized_report_path.read_text(encoding="utf-8"))
        oversized = [item for item in oversized_report["diagnostics"]
                     if item["code"] == "track_exceeds_batch_limit"]
        if not oversized or any("albumPath" not in item or "filePath" not in item for item in oversized):
            raise AssertionError("Oversized tracks were not identified")
        if {part.name: part.read_bytes() for part in sized_dir.glob("media_out_*.json")} != previous_parts:
            raise AssertionError("Oversized track replaced the prior catalog")
        size_config.write_text(json.dumps({
            "Incremental": False, "JsonBatchTargetBytes": byte_target
        }), encoding="utf-8")
        (sized_library / "album-2" / "track.flac").write_bytes(b"not an audio stream")
        failed_sized_scan = subprocess.run(command, capture_output=True, timeout=30)
        if failed_sized_scan.returncode == 0:
            raise AssertionError("Invalid track succeeded during size-aware scan")
        if {part.name: part.read_bytes() for part in sized_dir.glob("media_out_*.json")} != previous_parts:
            raise AssertionError("Failed size-aware scan changed the installed catalog")
        failed_sized_report = json.loads(oversized_report_path.read_text(encoding="utf-8"))
        if failed_sized_report["counts"]["batchesStaged"] < 1:
            raise AssertionError("The later-album failure did not exercise staged batch cleanup")
        size_config.write_text(json.dumps({"Incremental": False}), encoding="utf-8")
        if subprocess.run(command, capture_output=True, timeout=30).returncode == 0:
            raise AssertionError("Invalid track succeeded without byte splitting")
        partially_serialized = json.loads(oversized_report_path.read_text(encoding="utf-8"))["counts"]
        if (partially_serialized["albumsSerialized"] < 1 or
                partially_serialized["batchesStaged"] != 0 or
                partially_serialized["batchesInstalled"] != 0):
            raise AssertionError("Serialized albums were not distinguished from finished batches")
        if {part.name: part.read_bytes() for part in sized_dir.glob("media_out_*.json")} != previous_parts:
            raise AssertionError("Failed unsplit scan changed the installed catalog")
        for invalid_target in (-1, 1.5, "100"):
            size_config.write_text(json.dumps({
                "JsonBatchTargetBytes": invalid_target
            }), encoding="utf-8")
            invalid = subprocess.run(command, capture_output=True, timeout=30)
            if invalid.returncode == 0:
                raise AssertionError("Invalid JSON byte target was accepted")

        if os.name == "nt":
            long_output = root / "long-output" / ("a" * 90) / ("b" * 90) / ("c" * 40)
            try:
                previous_archive = None
                for scan_index, archive_enabled in enumerate((False, True, False)):
                    long_command = [
                        converter, "scan", str(library), "--output-dir", str(long_output),
                        "--config", str(default_config)]
                    if archive_enabled:
                        long_command.append("--archive-flac-metadata")
                    result = subprocess.run(long_command, capture_output=True,
                                            text=True, encoding="utf-8", errors="replace")
                    if result.returncode:
                        raise AssertionError(f"Long-path scan failed: {result.stdout}\n{result.stderr}")
                    if scan_index and "Incremental scan cache contains 1 catalog record(s)." not in result.stdout:
                        raise AssertionError(f"Long-path scan did not reuse its catalog: {result.stdout}")
                    validate_report(long_output / library.name / "scan-report.json")
                    long_target = Path("\\\\?\\" + str(long_output / library.name))
                    long_json = long_target / "media_out_library_1.json"
                    if not long_json.is_file():
                        raise AssertionError("Scan did not install JSON under a long Windows output path")
                    long_albums = json.loads(long_json.read_text(encoding="utf-8"))
                    if len(long_albums) != 1 or len(long_albums[0].get("Tracks", [])) != 1:
                        raise AssertionError("Long-path scan output did not preserve the root-level track")
                    long_manifest = json.loads((long_target / "scan-manifest.json").read_text(
                        encoding="utf-8"))
                    if archive_enabled:
                        archive = long_manifest["archives"][0]
                        previous_archive = long_target / archive["file"]
                        archive_bytes = previous_archive.read_bytes()
                        if hashlib.sha256(archive_bytes).hexdigest() != archive["sha256"]:
                            raise AssertionError("Long-path metadata archive did not match its manifest")
                    elif long_manifest["archives"] or (previous_archive and previous_archive.exists()):
                        raise AssertionError("Long-path scan did not retire its metadata archive")
            finally:
                extended_output = "\\\\?\\" + str(long_output)
                if os.path.exists(extended_output):
                    shutil.rmtree(extended_output)

            # The directory itself can be below the prefix threshold while archive paths exceed MAX_PATH.
            component_length = max(1, 210 - len(str(root)) - len(library.name) - 2)
            medium_output = root / ("m" * component_length)
            if len(str(medium_output / library.name)) < 248:
                try:
                    medium_target = Path("\\\\?\\" + str(medium_output / library.name))
                    medium_command = [
                        converter, "scan", str(library), "--output-dir", str(medium_output),
                        "--config", str(default_config)]
                    subprocess.run(medium_command + ["--archive-flac-metadata"], check=True)
                    medium_manifest = json.loads((medium_target / "scan-manifest.json").read_text(
                        encoding="utf-8"))
                    medium_archive = medium_target / medium_manifest["archives"][0]["file"]
                    if len(str(medium_output / library.name / medium_archive.name)) < 260:
                        raise AssertionError("Archive fixture did not exceed the Windows path limit")
                    result = subprocess.run(medium_command, capture_output=True,
                                            text=True, encoding="utf-8", errors="replace")
                    if result.returncode:
                        raise AssertionError(f"Long archive scan failed: {result.stdout}\n{result.stderr}")
                    if "Incremental scan cache contains 1 catalog record(s)." not in result.stdout:
                        raise AssertionError(f"Long archive path invalidated the cache: {result.stdout}")
                    if medium_archive.exists():
                        raise AssertionError("Long archive path was not retired")
                finally:
                    extended_output = "\\\\?\\" + str(medium_output)
                    if os.path.exists(extended_output):
                        shutil.rmtree(extended_output)

        incremental_library = root / "incremental-library"
        incremental_album = incremental_library / "same-album"
        incremental_album.mkdir(parents=True)
        incremental_tracks = []
        for filename, album in (
            ("track-one.flac", "Album A"),
            ("track-two.flac", "Album B"),
        ):
            incremental_track = incremental_album / filename
            subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                 "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", "flac",
                 "-metadata", f"title={filename}", "-metadata", "artist=Test artist",
                 "-metadata", f"album={album}", str(incremental_track)],
                check=True,
            )
            incremental_tracks.append(incremental_track)
        incremental_config = root / "incremental-config.json"
        incremental_config.write_text(json.dumps({
            "CatalogExtensions": [".flac"],
            "Incremental": True,
        }), encoding="utf-8")
        incremental_output = root / "incremental-output"
        first_mtime_ns = 1_700_000_000_100_000_000
        second_mtime_ns = 1_700_000_000_800_000_000
        for incremental_track in incremental_tracks:
            os.utime(incremental_track, ns=(first_mtime_ns, first_mtime_ns))
        subprocess.run(
            [converter, "scan", str(incremental_library), "--config",
             str(incremental_config), "--output-dir", str(incremental_output)],
            check=True,
        )
        incremental_json = next(
            (incremental_output / incremental_library.name).glob("media_out_*.json"))
        first_records = json.loads(incremental_json.read_text(encoding="utf-8"))[0]["Tracks"]
        old_catalog = json.loads(incremental_json.read_text(encoding="utf-8"))
        for record in old_catalog[0]["Tracks"]:
            record.pop("metadataInventoryVersion")
            record["rawTags"] = [tag for tag in record["rawTags"]
                                 if not tag["scope"].startswith("ffmpeg.")]
        incremental_json.write_text(json.dumps(old_catalog), encoding="utf-8")
        subprocess.run(
            [converter, "scan", str(incremental_library), "--config",
             str(incremental_config), "--output-dir", str(incremental_output)], check=True)
        refreshed_records = json.loads(incremental_json.read_text(encoding="utf-8"))[0]["Tracks"]
        if any(record.get("metadataInventoryVersion") != 1 or
               not any(tag["scope"] == "ffmpeg.format" for tag in record["rawTags"])
               for record in refreshed_records):
            raise AssertionError("Older incremental records were not rescanned for FFmpeg metadata")
        second_record_before = next(
            record for record in first_records
            if record["tags"].get("title") == ["track-two.flac"])
        if not any(
            finding["code"] == "INCONSISTENT_ALBUM_TAG_album"
            for record in first_records for finding in record["findings"]
        ):
            raise AssertionError("The initial inconsistent album tags were not reported")
        original_size = incremental_tracks[1].stat().st_size
        source_bytes = incremental_tracks[1].read_bytes()
        if source_bytes.count(b"Album B") != 1:
            raise AssertionError("Could not isolate the generated album tag in the FLAC fixture")
        incremental_tracks[1].write_bytes(source_bytes.replace(b"Album B", b"Album A", 1))
        if incremental_tracks[1].stat().st_size != original_size:
            raise AssertionError("The incremental cache fixture changed file size")
        os.utime(incremental_tracks[1], ns=(second_mtime_ns, second_mtime_ns))
        observed_mtime_ns = incremental_tracks[1].stat().st_mtime_ns
        if observed_mtime_ns // 1_000_000_000 != first_mtime_ns // 1_000_000_000:
            raise AssertionError("The incremental cache fixture timestamps are not in the same second")
        if observed_mtime_ns == first_mtime_ns:
            raise AssertionError("The filesystem did not preserve subsecond fixture timestamps")
        subprocess.run(
            [converter, "scan", str(incremental_library), "--config",
             str(incremental_config), "--output-dir", str(incremental_output)],
            check=True,
        )
        refreshed_records = json.loads(
            incremental_json.read_text(encoding="utf-8"))[0]["Tracks"]
        second_record_after = next(
            record for record in refreshed_records
            if record["tags"].get("title") == ["track-two.flac"])
        if second_record_after["tags"].get("album") != ["Album A"]:
            raise AssertionError("An edited source with the same size and timestamp second reused stale tags")
        if second_record_after["file"]["modifiedUtc"] == second_record_before["file"]["modifiedUtc"]:
            raise AssertionError("The catalog did not preserve subsecond modification-time precision")
        if any(
            finding["code"] == "INCONSISTENT_ALBUM_TAG_album"
            for record in refreshed_records for finding in record["findings"]
        ):
            raise AssertionError("Incremental cache reuse retained a stale album-level finding")

        unicode_library = root / "音楽 library"
        unicode_library.mkdir()
        unicode_track = unicode_library / "東京.flac"
        subprocess.run(
            [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
             "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", "flac",
             "-metadata", "title=Unicode path fixture", str(unicode_track)],
            check=True,
        )
        unicode_output = root / "unicode-output"
        subprocess.run(
            [converter, "scan", str(unicode_library), "--output-dir", str(unicode_output)],
            check=True,
        )
        unicode_json = next(
            (unicode_output / unicode_library.name).glob("media_out_*.json"))
        unicode_record = json.loads(
            unicode_json.read_text(encoding="utf-8"))[0]["Tracks"][0]
        if unicode_record["file"]["relativePath"] != unicode_track.name:
            raise AssertionError("A non-ASCII source filename was not preserved in the catalog")

        extra_tracks = {
            "root-track.wav": ("pcm_s16le", "WAV catalog track"),
            "root-track.ogg": ("libvorbis", "Ogg catalog track"),
            "root-track.opus": ("libopus", "Opus catalog track"),
            "root-track.wv": ("wavpack", "WavPack catalog track"),
            "root-track.m4a": ("aac", "MP4 catalog track"),
            "root-track.aiff": ("pcm_s16be", "AIFF catalog track"),
        }
        for filename, (codec, title) in extra_tracks.items():
            muxer_options = ["-write_id3v2", "1"] if filename.endswith(".aiff") else []
            barcode_options = (
                ["-metadata", "barcode=4006381333931", "-metadata", "upc_e=04252614"]
                if filename.endswith(".wv") else []
            )
            album = "Different album" if filename.endswith(".aiff") else "Shared album"
            subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                 "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", codec,
                 *muxer_options, *barcode_options,
                 "-metadata", f"title={title}", "-metadata", f"album={album}",
                 str(library / filename)],
                check=True,
            )
        source_hashes = {track: hashlib.sha256(track.read_bytes()).hexdigest()
                         for track in library.iterdir() if track.is_file()}
        extension_config = root / "catalog-extensions.json"
        extension_config.write_text(json.dumps({
            "CatalogExtensions": [
                ".flac", ".wav", ".ogg", ".opus", ".wv", ".m4a", ".aiff",
            ],
            "Incremental": False,
            "UseFFmpegLibraryAPI": False,
        }), encoding="utf-8")
        extended_output = root / "extended-output"
        subprocess.run(
            [converter, "scan", str(library), "--config", str(extension_config),
             "--output-dir", str(extended_output)],
            check=True,
        )
        extended_file = next((extended_output / library.name).glob("media_out_*.json"))
        extended_albums = json.loads(extended_file.read_text(encoding="utf-8"))
        extended_records = extended_albums[0]["Tracks"]
        records_by_path = {item["file"]["relativePath"]: item for item in extended_records}
        expected_paths = {"root-track.flac", *extra_tracks}
        if set(records_by_path) != expected_paths:
            raise AssertionError(
                f"Configured catalog extensions were not all scanned: {sorted(records_by_path)}")
        for filename, (_, title) in extra_tracks.items():
            if records_by_path[filename]["tags"].get("title") != [title]:
                raise AssertionError(f"TagLib did not extract title from {filename}")
            if records_by_path[filename]["support"]["nativeInventory"] != "unsupported":
                raise AssertionError(f"{filename} incorrectly claims native inventory support")
        if not any(tag["scope"] == "ffmpeg.format" and tag["value"] == "MP4 catalog track"
                   for tag in records_by_path["root-track.m4a"]["rawTags"]):
            raise AssertionError("FFmpeg metadata was not captured for the MP4 fixture")
        wavpack_record = records_by_path["root-track.wv"]
        if not {"4006381333931", "04252614"}.issubset(
            set(wavpack_record["tags"].get("barcode", []))
        ) or any(
            finding["code"] == "INVALID_BARCODE" for finding in wavpack_record["findings"]
        ):
            raise AssertionError("Valid EAN and UPC-E barcodes were not normalized and accepted")
        if not any(
            finding["code"] == "INCONSISTENT_ALBUM_TAG_album"
            for record in extended_records
            for finding in record["findings"]
        ):
            raise AssertionError("Inconsistent album tags were not reported")
        extended_database_output = root / "extended-database"
        subprocess.run(
            [converter, "db", "import", str(extended_output / library.name),
             "--output-dir", str(extended_database_output)],
            check=True,
        )
        extended_database = (
            extended_database_output / library.name / "catalog-v2.sqlite")

        def imported_paths():
            with closing(sqlite3.connect(extended_database)) as connection:
                return connection.execute(
                    "SELECT relative_path FROM files ORDER BY relative_path").fetchall()

        expected_imported_paths = sorted(expected_paths)
        if [row[0] for row in imported_paths()] != expected_imported_paths:
            raise AssertionError("Initial database import omitted catalog files")
        subprocess.run(
            [converter, "db", "import", str(extended_output / library.name),
             "--output-dir", str(extended_database_output)],
            check=True,
        )
        if [row[0] for row in imported_paths()] != expected_imported_paths:
            raise AssertionError("Re-import duplicated or removed unchanged catalog files")
        removed_path = sorted(expected_paths)[-1]
        extended_file.write_text(json.dumps([
            {**extended_albums[0], "Tracks": [
                item for item in extended_records
                if item["file"]["relativePath"] != removed_path
            ]}
        ]), encoding="utf-8")
        refresh_manifest_batch(extended_file.parent, extended_file)
        subprocess.run(
            [converter, "db", "import", str(extended_output / library.name),
             "--output-dir", str(extended_database_output)],
            check=True,
        )
        actual_imported_paths = [row[0] for row in imported_paths()]
        if removed_path in actual_imported_paths or sorted(actual_imported_paths) != sorted(
            expected_paths - {removed_path}
        ):
            raise AssertionError("Re-import retained a file removed from an imported album")
        foreign_output = root / "foreign-database"
        foreign_directory = foreign_output / library.name
        foreign_directory.mkdir(parents=True)
        foreign_database = foreign_directory / "catalog-v2.sqlite"
        with closing(sqlite3.connect(foreign_database)) as connection:
            connection.execute("CREATE TABLE unrelated(value TEXT)")
            connection.execute("PRAGMA user_version=1")
        foreign_database_hash = hashlib.sha256(foreign_database.read_bytes()).hexdigest()
        rejected_import = subprocess.run(
            [converter, "db", "import", str(extended_output / library.name),
             "--output-dir", str(foreign_output)],
            capture_output=True,
            text=True,
        )
        if rejected_import.returncode == 0:
            raise AssertionError("Import accepted an incompatible pre-existing database")
        if hashlib.sha256(foreign_database.read_bytes()).hexdigest() != foreign_database_hash:
            raise AssertionError("Rejected import modified an incompatible database")
        incomplete_output = root / "incomplete-database"
        incomplete_directory = incomplete_output / library.name
        incomplete_directory.mkdir(parents=True)
        incomplete_database = incomplete_directory / "catalog-v2.sqlite"
        with closing(sqlite3.connect(incomplete_database)) as connection:
            for name in ("libraries", "albums", "files", "tags", "raw_tags", "pictures",
                         "chapters", "chapter_tags", "measurements", "findings",
                         "schema_docs", "enum_docs", "catalog_meta"):
                connection.execute(f"CREATE TABLE {name}(record_json TEXT)")
            for name in ("v_tracks", "v_findings", "v_albums"):
                connection.execute(f"CREATE VIEW {name} AS SELECT 1")
            connection.execute("PRAGMA user_version=2")
        incomplete_hash = hashlib.sha256(incomplete_database.read_bytes()).hexdigest()
        rejected_import = subprocess.run(
            [converter, "db", "import", str(extended_output / library.name),
             "--output-dir", str(incomplete_output)], capture_output=True,
        )
        if rejected_import.returncode == 0 or hashlib.sha256(
                incomplete_database.read_bytes()).hexdigest() != incomplete_hash:
            raise AssertionError("Incompatible version-2 database was accepted or modified")
        unsupported_config = root / "unsupported-catalog-extension.json"
        unsupported_config.write_text(json.dumps({
            "CatalogExtensions": [".wma"],
        }), encoding="utf-8")
        rejected_scan = subprocess.run(
            [converter, "scan", str(library), "--config", str(unsupported_config),
             "--output-dir", str(root / "unsupported-output")],
            capture_output=True,
            text=True,
        )
        if rejected_scan.returncode == 0:
            raise AssertionError("Unsupported catalog extensions were accepted")
        numbering_library = root / "numbering-library"
        numbering_album = numbering_library / "numbered-album"
        numbering_album.mkdir(parents=True)
        for filename, track_number in (
            ("track-one-a.flac", "1/3"),
            ("track-one-b.flac", "1/3"),
            ("track-three.flac", "3/3"),
        ):
            subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                 "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", "flac",
                 "-metadata", f"title={filename}", "-metadata", "artist=Test artist",
                 "-metadata", "album=Numbering fixture", "-metadata",
                 f"track={track_number}", "-metadata", "disc=1/1",
                 str(numbering_album / filename)],
                check=True,
            )
        partial_numbering_album = numbering_library / "partially-numbered"
        partial_numbering_album.mkdir()
        for filename, number_options in (
            ("numbered.flac", ["-metadata", "track=1/2"]),
            ("unnumbered.flac", []),
        ):
            subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                 "sine=frequency=997:sample_rate=44100:duration=1", "-c:a", "flac",
                 "-metadata", "album=Partial numbering", *number_options,
                 str(partial_numbering_album / filename)], check=True,
            )
        numbering_output = root / "numbering-output"
        subprocess.run(
            [converter, "scan", str(numbering_library),
             "--output-dir", str(numbering_output)],
            check=True,
        )
        numbering_json = next(
            (numbering_output / numbering_library.name).glob("media_out_*.json"))
        numbering_albums = json.loads(numbering_json.read_text(encoding="utf-8"))
        numbering_records = next(album["Tracks"] for album in numbering_albums
                                 if album["Folder"] == "numbered-album")
        partial_records = next(album["Tracks"] for album in numbering_albums
                               if album["Folder"] == "partially-numbered")
        if any(finding["code"] == "TRACK_TOTAL_MISMATCH"
               for record in partial_records for finding in record["findings"]):
            raise AssertionError("Missing track number caused a misleading total mismatch")
        numbering_findings = {
            finding["code"]
            for record in numbering_records
            for finding in record["findings"]
        }
        expected_numbering_findings = {
            "DUPLICATE_TRACK_NUMBER", "TRACK_NUMBER_GAP", "TRACK_TOTAL_MISMATCH",
        }
        if not expected_numbering_findings.issubset(numbering_findings):
            raise AssertionError(
                f"Album numbering findings were incomplete: {sorted(numbering_findings)}")
        numbering_database_output = root / "numbering-database"
        subprocess.run(
            [converter, "db", "import", str(numbering_output / numbering_library.name),
             "--output-dir", str(numbering_database_output)],
            check=True,
        )
        numbering_database = (
            numbering_database_output / numbering_library.name / "catalog-v2.sqlite")
        with closing(sqlite3.connect(numbering_database)) as connection:
            undocumented_numbering_findings = connection.execute(
                "SELECT DISTINCT f.code FROM findings f "
                "LEFT JOIN enum_docs e ON e.domain='finding_code' AND e.value=f.code "
                "WHERE e.value IS NULL").fetchall()
        if undocumented_numbering_findings:
            raise AssertionError(
                f"Album numbering findings lack AI documentation: "
                f"{undocumented_numbering_findings}")
        if any(hashlib.sha256(track.read_bytes()).hexdigest() != source_hash
               for track, source_hash in source_hashes.items()):
            raise AssertionError("A scan changed one or more source files")

        for tier in ("integrity", "analysis"):
            tier_output = root / f"output-{tier}"
            subprocess.run(
                [converter, "scan", str(library), "--output-dir", str(tier_output),
                 "--tier", tier],
                check=True,
            )
            tier_file = next((tier_output / library.name).glob("media_out_*.json"))
            tier_record = json.loads(tier_file.read_text(encoding="utf-8"))[0]["Tracks"][0]
            if tier_record["audioAnalysis"]["tier"] != tier:
                raise AssertionError(f"Scan tier {tier} was not recorded")
            if tier_record["integrity"]["status"] != "ok":
                raise AssertionError(f"Scan tier {tier} did not complete integrity analysis")
            if not tier_record["integrity"].get("pcmSha256"):
                raise AssertionError(f"Scan tier {tier} did not produce a PCM hash")
            if tier == "analysis" and not tier_record["audioAnalysis"]["measurements"]:
                raise AssertionError("Analysis tier did not produce signal measurements")
            if tier == "analysis":
                database_output = root / "analysis-database"
                subprocess.run(
                    [converter, "db", "import", str(output / library.name),
                     "--output-dir", str(database_output)],
                    check=True,
                )
                subprocess.run(
                    [converter, "db", "import", str(tier_output / library.name),
                     "--output-dir", str(database_output)],
                    check=True,
                )
                database = database_output / library.name / "catalog-v2.sqlite"
                with closing(sqlite3.connect(database)) as connection:
                    measurements = connection.execute(
                        "SELECT kind,status FROM measurements ORDER BY kind").fetchall()
                    scan_tier = connection.execute("SELECT scan_tier FROM files").fetchone()[0]
                    hashes = connection.execute("SELECT sha256,pcm_hash FROM files").fetchone()
                    undocumented_findings = connection.execute(
                        "SELECT DISTINCT f.code FROM findings f "
                        "LEFT JOIN enum_docs e ON e.domain='finding_code' AND e.value=f.code "
                        "WHERE e.value IS NULL").fetchall()
                    objects = connection.execute(
                        "SELECT name,type,sql FROM sqlite_master "
                        "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%'").fetchall()
                    undocumented_columns = []
                    missing_ddl_comments = []
                    for name, object_type, ddl in objects:
                        pragma = f'PRAGMA table_info("{name}")'
                        columns = connection.execute(pragma).fetchall()
                        for column in columns:
                            documented = connection.execute(
                                "SELECT 1 FROM schema_docs WHERE object_name=? AND column_name=?",
                                (name, column[1])).fetchone()
                            if documented is None:
                                undocumented_columns.append(f"{name}.{column[1]}")
                        if object_type in ("table", "view") and "--" not in (ddl or ""):
                            missing_ddl_comments.append(name)
                if measurements != [("audioAnalysis", "partial"), ("integrity", "ok")]:
                    raise AssertionError(f"Analysis measurements were not imported: {measurements}")
                if scan_tier != "analysis":
                    raise AssertionError("Database did not retain the requested scan tier")
                if hashes != (tier_record["integrity"]["fileSha256"],
                              tier_record["integrity"]["pcmSha256"]):
                    raise AssertionError("Database hash projections did not update on re-import")
                if undocumented_findings:
                    raise AssertionError(
                        f"Finding codes are missing AI documentation: {undocumented_findings}")
                if undocumented_columns:
                    raise AssertionError(
                        f"Database columns are missing schema_docs entries: {undocumented_columns}")
                if missing_ddl_comments:
                    raise AssertionError(
                        f"Database objects are missing inline AI comments: {missing_ddl_comments}")
        subprocess.run(
            [converter, "duplicates", str(output / library.name)],
            check=True,
        )
        subprocess.run(
            [converter, "db", "import", str(output / library.name),
             "--output-dir", str(root / "database-output")],
            check=True,
        )
        unsafe_import = subprocess.run(
            [converter, "db", "import", str(output / library.name), "--output-dir", str(root)],
            capture_output=True,
        )
        if unsafe_import.returncode == 0 or (library / "catalog-v2.sqlite").exists():
            raise AssertionError("Import wrote a database inside the source library")

        for value in ({"AlbumsSplitThreshold": 0}, {"MediaDirectoryList": [5]}):
            invalid_config = root / "invalid-config.json"
            invalid_config.write_text(json.dumps(value), encoding="utf-8")
            invalid = subprocess.run(
                [converter, "scan", str(library), "--config", str(invalid_config),
                 "--output-dir", str(root / "invalid-output")],
                capture_output=True, timeout=15,
            )
            if invalid.returncode == 0:
                raise AssertionError(f"Invalid configuration was accepted: {value}")

        eof = subprocess.run([converter], stdin=subprocess.DEVNULL, cwd=root,
                             capture_output=True, timeout=15)
        if eof.returncode == 0:
            raise AssertionError("Menu succeeded without a configuration or input")

        unknown_library = root / "unknown-flac"
        unknown_library.mkdir()
        unknown_track = unknown_library / "unknown.flac"
        data = bytearray(track.read_bytes())
        if data[:4] != b"fLaC" or data[4] & 0x7f != 0:
            raise AssertionError("Generated FLAC does not begin with STREAMINFO")
        # STREAMINFO's last 36 packed bits are total samples; all-zero MD5 is unknown.
        data[21] &= 0xf0
        data[22:26] = b"\0" * 4
        data[26:42] = b"\0" * 16
        unknown_track.write_bytes(data)
        unknown_output = root / "unknown-output"
        subprocess.run(
            [converter, "scan", str(unknown_library), "--output-dir", str(unknown_output),
             "--tier", "integrity"],
            check=True,
        )
        unknown_json = next((unknown_output / unknown_library.name).glob("media_out_*.json"))
        unknown_record = json.loads(unknown_json.read_text(encoding="utf-8"))[0]["Tracks"][0]
        if unknown_record["integrity"]["status"] != "ok":
            raise AssertionError("Unknown FLAC STREAMINFO values caused an integrity failure")
        if unknown_record["technical"].get("totalSamples") == 0 or \
                unknown_record["technical"].get("flacMd5") == "0" * 32:
            raise AssertionError("Unknown FLAC STREAMINFO values were treated as real values")

        menu_output = root / "menu-output"
        menu_json = menu_output / library.name
        menu_json.mkdir(parents=True)
        menu_fixture = json.loads(files[0].read_text(encoding="utf-8"))
        for album in menu_fixture:
            album.pop("Scan Manifest ID", None)
        fixture_json = json.dumps(menu_fixture).encode("utf-8")
        for index in (1, 3, 1000):
            (menu_json / f"media_out_{library.name}_{index}.json").write_bytes(fixture_json)
        menu_config_dir = root / "menu-config"
        menu_config_dir.mkdir()
        (menu_config_dir / "config.json").write_text(json.dumps({
            "OutDirectory": str(menu_output),
            "ScanTier": "catalog",
            "UseAsyncFFmpegCalls": False,
            "MediaDirectoryList": [{"isActive": True, "mediaPath": str(library),
                                    "mediaName": ""}],
        }), encoding="utf-8")
        (root / "AppSettingsJsonPersistent.txt").write_text(
            "UTF8:" + str(menu_config_dir), encoding="utf-8")
        menu = subprocess.run([converter], input="4\nRUN\n", cwd=root,
                              capture_output=True, text=True, timeout=30)
        if menu.returncode != 0 or "JSON input files: 3" not in menu.stdout:
            raise AssertionError(f"Menu did not import all sparse batches: {menu.stdout}\n{menu.stderr}")
        if "Use Async Metadata Reads: false" not in menu.stdout:
            raise AssertionError("Legacy async setting was ignored with ScanTier present")
        menu_duplicates = subprocess.run(
            [converter], input="3\nBACK\n0\n", cwd=root,
            capture_output=True, text=True, timeout=30,
        )
        if "JSON input files: 3" not in menu_duplicates.stdout:
            raise AssertionError("Menu duplicate preview did not use the fallback library name")

        duplicates_library = root / "duplicates-音楽"
        albums = [duplicates_library / "copy-one", duplicates_library / "copy-two"]
        for album in albums:
            album.mkdir(parents=True)
        fixtures = (
            (albums[0], "a.flac", 1, 44100, "Alice"),
            (albums[0], "b.flac", 2, 44100, "Bob"),
            (albums[1], "z.flac", 1, 48000, "Alice"),
            (albums[1], "a.flac", 2, 44100, "Bob"),
        )
        for album, filename, duration, rate, artist in fixtures:
            subprocess.run(
                [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                 f"sine=frequency=997:sample_rate={rate}:duration={duration}",
                 "-c:a", "flac", "-metadata", "album=Compilation",
                 "-metadata", f"artist={artist}", "-metadata", f"title=Track {duration}",
                 str(album / filename)], check=True,
            )
        nested_album = albums[0] / "nested"
        nested_album.mkdir()
        subprocess.run(
            [ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
             "sine=frequency=500:sample_rate=44100:duration=1", "-c:a", "flac",
             "-metadata", "album=Different album", str(nested_album / "nested.flac")],
            check=True,
        )
        duplicate_output = root / "duplicate-output"
        subprocess.run([converter, "scan", str(duplicates_library),
                        "--output-dir", str(duplicate_output)], check=True)
        duplicate_report = root / "duplicate-report.json"
        duplicate_run = subprocess.run(
            [converter, "duplicates", str(duplicate_output / duplicates_library.name),
             "--report", str(duplicate_report)],
            capture_output=True, text=True, timeout=30,
        )
        if duplicate_run.returncode != 0 or "Found 1 duplicate album pair(s)" not in duplicate_run.stdout:
            raise AssertionError(f"Compilation duplicate was not found: {duplicate_run.stdout}")
        report = json.loads(duplicate_report.read_text(encoding="utf-8"))
        if report.get("status") != "completed" or len(report.get("groups", [])) != 1 or \
                len(report["groups"][0]["pairs"]) != 1 or \
                len(report["groups"][0]["pairs"][0]["trackPairs"]) != 2:
            raise AssertionError(f"Duplicate report content is wrong: {report}")
        repeated_report = subprocess.run(
            [converter, "duplicates", str(duplicate_output / duplicates_library.name),
             "--report", str(duplicate_report)],
            capture_output=True, text=True, timeout=30,
        )
        if repeated_report.returncode == 0:
            raise AssertionError("Existing duplicate report was overwritten")
        source_library_report = duplicates_library / "duplicate-report.json"
        inside_library = subprocess.run(
            [converter, "duplicates", str(duplicate_output / duplicates_library.name),
             "--report", str(source_library_report)],
            capture_output=True, text=True, timeout=30,
        )
        if inside_library.returncode == 0 or source_library_report.exists():
            raise AssertionError("Duplicate report was allowed inside its source library")
        if "Different number of tracks" in duplicate_run.stdout or \
                "OUTSIDE tolerance" in duplicate_run.stdout:
            raise AssertionError("Nested or unordered tracks caused a false mismatch")
        if "44100" not in duplicate_run.stdout or "48000" not in duplicate_run.stdout:
            raise AssertionError("Basic audio property difference was not reported")
        for expected in ("Pair [1] <-> [2]", "SCAN-TIME evidence", "Coverage: 2/2",
                         "Match methods:", "Stored FLAC MD5:", "Technical differences:",
                         "Current tags:", "Duplicate group review summary",
                         "Other immediate files/folders NOT compared", "nested",
                         "Tracks: 2 scanned; 2 currently present; 2 scanned paths readable.",
                         "Higher bitrate, sample rate, bit depth or larger files do not establish"):
            if expected not in duplicate_run.stdout:
                raise AssertionError(f"Duplicate decision aids missing {expected!r}: {duplicate_run.stdout}")
        if "## CONFIDENCE: " not in duplicate_run.stdout:
            raise AssertionError(f"Confidence banner missing from redirected output: {duplicate_run.stdout}")
        confidence_line = next(
            line for line in duplicate_run.stdout.splitlines() if "## CONFIDENCE: " in line
        )
        if "\x1b[" in confidence_line:
            raise AssertionError("ANSI colour leaked into confidence banner in redirected output")
        if "Skipped: Audio analysis was not run." in duplicate_run.stdout:
            raise AssertionError("Duplicate comparison printed obsolete per-track analysis noise")
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes

            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.CreateFileW.argtypes = [
                wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
            ]
            kernel.CreateFileW.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = kernel.CreateFileW(
                str(albums[0] / "a.flac"), 0x80000000, 0, None, 3, 0x80, None)
            if handle == wintypes.HANDLE(-1).value:
                raise OSError(ctypes.get_last_error(), "Could not lock disposable fixture")
            try:
                failed_probe = subprocess.run(
                    [converter, "duplicates", str(duplicate_output / duplicates_library.name)],
                    capture_output=True, timeout=30,
                )
                if failed_probe.returncode == 0:
                    raise AssertionError("Duplicate comparison ignored a locked track")
            finally:
                kernel.CloseHandle(handle)
        (albums[0] / "a.flac").rename(albums[0] / "renamed.flac")
        (albums[0] / "added.flac").write_bytes((albums[0] / "b.flac").read_bytes())
        (albums[0] / "b.flac").write_bytes((albums[1] / "z.flac").read_bytes())
        changed_tracks = {path: hashlib.sha256(path.read_bytes()).hexdigest()
                          for album in albums for path in album.glob("*.flac")}
        changed_run = subprocess.run(
            [converter, "duplicates", str(duplicate_output / duplicates_library.name)],
            capture_output=True, text=True, timeout=30,
        )
        if changed_run.returncode != 0:
            raise AssertionError(f"Changed duplicate comparison failed: {changed_run.stdout}\n"
                                 f"{changed_run.stderr}")
        for expected in ("Scanned track is missing:", "a.flac",
                         "Track added since scan;", "renamed.flac", "added.flac",
                         "Scanned track changed since scan:", "b.flac",
                         "Track missing in one of the albums.",
                         "rescan before relying on them", "added/uncompared",
                         "currently readable pairs 1/2",
                         "Track pair 2 | matched by", "delta 1.000 s"):
            if expected not in changed_run.stdout:
                raise AssertionError(f"Changed duplicate report missing {expected!r}: "
                                     f"{changed_run.stdout}")
        comparisons = [line for line in changed_run.stdout.splitlines()
                       if "Comparing scanned tracks:" in line]
        if len(comparisons) != 1 or str(Path("copy-one") / "b.flac") not in comparisons[0] or \
                str(Path("copy-two") / "a.flac") not in comparisons[0]:
            raise AssertionError(f"Changed tracks were paired with the wrong scan entries: {comparisons}")
        legacy_dir = root / "legacy-duplicates"
        legacy_dir.mkdir()
        scanned_json = next((duplicate_output / duplicates_library.name).glob("media_out_*.json"))
        legacy_albums = json.loads(scanned_json.read_text(encoding="utf-8"))
        for album in legacy_albums:
            album.pop("Scan Manifest ID", None)
            for index, record in enumerate(album["Tracks"]):
                tags = {key: values[0] for key, values in record["tags"].items() if values}
                if "artists" in tags:
                    tags["artist"] = tags.pop("artists")
                album["Tracks"][index] = {
                    "format": {
                        "filename": str(Path(album["Album Path"]) / record["file"]["relativePath"]),
                        "duration": record["technical"]["durationSeconds"],
                        "tags": tags,
                    },
                    "streams": [{"codec_type": "audio"}],
                }
        (legacy_dir / "media_out_legacy.json").write_text(json.dumps(legacy_albums), encoding="utf-8")
        legacy_run = subprocess.run(
            [converter, "duplicates", str(legacy_dir)], capture_output=True, text=True, timeout=30,
        )
        if legacy_run.returncode != 0 or "Scanned track is missing:" not in legacy_run.stdout or \
                "delta 1.000 s" not in legacy_run.stdout:
            raise AssertionError(f"Legacy duplicate reconciliation failed: {legacy_run.stdout}\n"
                                 f"{legacy_run.stderr}")
        mixed_dir = root / "mixed-duplicates"
        mixed_dir.mkdir()
        mixed_albums = json.loads(scanned_json.read_text(encoding="utf-8"))
        for album in mixed_albums:
            album.pop("Scan Manifest ID", None)
        mixed_index = next(index for index, album in enumerate(mixed_albums)
                           if Path(album["Album Path"]).name == "copy-one")
        mixed_albums[mixed_index] = legacy_albums[mixed_index]
        (mixed_dir / "media_out_mixed.json").write_text(json.dumps(mixed_albums), encoding="utf-8")
        mixed_run = subprocess.run(
            [converter, "duplicates", str(mixed_dir)], capture_output=True, text=True, timeout=30,
        )
        if mixed_run.returncode != 0 or "Found 1 duplicate album pair(s)" not in mixed_run.stdout or \
                "Scanned track is missing:" not in mixed_run.stdout:
            raise AssertionError(f"Mixed catalog duplicate reconciliation failed: {mixed_run.stdout}\n"
                                 f"{mixed_run.stderr}")
        invalid_dir = root / "invalid-duplicate-path"
        invalid_dir.mkdir()
        invalid_albums = json.loads(scanned_json.read_text(encoding="utf-8"))
        for album in invalid_albums:
            album.pop("Scan Manifest ID", None)
        invalid_index = next(index for index, album in enumerate(invalid_albums)
                             if Path(album["Album Path"]).name == "copy-one")
        invalid_albums[invalid_index]["Tracks"][0]["file"]["relativePath"] = "../outside.flac"
        (invalid_dir / "media_out_invalid.json").write_text(json.dumps(invalid_albums), encoding="utf-8")
        invalid_run = subprocess.run(
            [converter, "duplicates", str(invalid_dir)], capture_output=True, text=True, timeout=30,
        )
        if invalid_run.returncode == 0 or "Invalid or repeated track path" not in invalid_run.stdout:
            raise AssertionError(f"Unsafe duplicate track path was not rejected: {invalid_run.stdout}")
        if any(hashlib.sha256(path.read_bytes()).hexdigest() != digest
               for path, digest in changed_tracks.items()):
            raise AssertionError("Duplicate comparison changed a source track")
        if any(hashlib.sha256(track.read_bytes()).hexdigest() != source_hash
               for track, source_hash in source_hashes.items()):
            raise AssertionError("A scan changed one or more source files")


if __name__ == "__main__":
    main()
