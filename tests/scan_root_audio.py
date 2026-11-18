"""Verify that a scan includes audio files directly under the library root."""

import json
import hashlib
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import closing
from pathlib import Path


def main():
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: scan_root_audio.py CONVERTER FFMPEG REPORT_VALIDATOR REPORT_SCHEMA")

    converter, ffmpeg, report_validator, report_schema = sys.argv[1:]

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
        subprocess.run(
            [converter, "scan", str(library), "--output-dir", str(output)],
            check=True,
        )
        files = list((output / library.name).glob("media_out_*.json"))
        if len(files) != 1:
            raise AssertionError(f"Expected one generated JSON file, found {len(files)}")
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
        if report["schemaVersion"] != 1 or report["status"] != "completed":
            raise AssertionError("A successful scan did not write a completed scan report")
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

        if os.name == "nt":
            long_output = root / "long-output" / ("a" * 90) / ("b" * 90)
            try:
                subprocess.run(
                    [converter, "scan", str(library), "--output-dir", str(long_output)],
                    check=True,
                )
                validate_report(long_output / library.name / "scan-report.json")
                long_json = Path("\\\\?\\" + str(
                    long_output / library.name / "media_out_library_1.json"))
                if not long_json.is_file():
                    raise AssertionError("Scan did not install JSON under a long Windows output path")
                long_albums = json.loads(long_json.read_text(encoding="utf-8"))
                if len(long_albums) != 1 or len(long_albums[0].get("Tracks", [])) != 1:
                    raise AssertionError("Long-path scan output did not preserve the root-level track")
            finally:
                extended_output = "\\\\?\\" + str(long_output)
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
        fixture_json = files[0].read_bytes()
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
        duplicate_run = subprocess.run(
            [converter, "duplicates", str(duplicate_output / duplicates_library.name)],
            capture_output=True, text=True, timeout=30,
        )
        if duplicate_run.returncode != 0 or "Found 1 duplicated albums" not in duplicate_run.stdout:
            raise AssertionError(f"Compilation duplicate was not found: {duplicate_run.stdout}")
        if "Different number of tracks" in duplicate_run.stdout or \
                "Album duration check (album1/2)" in duplicate_run.stdout:
            raise AssertionError("Nested or unordered tracks caused a false mismatch")
        if "44100" not in duplicate_run.stdout or "48000" not in duplicate_run.stdout:
            raise AssertionError("Basic audio property difference was not reported")
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
                         "Album duration check (album1/2), track 2:"):
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
                "Album duration check (album1/2), track 2:" not in legacy_run.stdout:
            raise AssertionError(f"Legacy duplicate reconciliation failed: {legacy_run.stdout}\n"
                                 f"{legacy_run.stderr}")
        mixed_dir = root / "mixed-duplicates"
        mixed_dir.mkdir()
        mixed_albums = json.loads(scanned_json.read_text(encoding="utf-8"))
        mixed_index = next(index for index, album in enumerate(mixed_albums)
                           if Path(album["Album Path"]).name == "copy-one")
        mixed_albums[mixed_index] = legacy_albums[mixed_index]
        (mixed_dir / "media_out_mixed.json").write_text(json.dumps(mixed_albums), encoding="utf-8")
        mixed_run = subprocess.run(
            [converter, "duplicates", str(mixed_dir)], capture_output=True, text=True, timeout=30,
        )
        if mixed_run.returncode != 0 or "Found 1 duplicated albums" not in mixed_run.stdout or \
                "Scanned track is missing:" not in mixed_run.stdout:
            raise AssertionError(f"Mixed catalog duplicate reconciliation failed: {mixed_run.stdout}\n"
                                 f"{mixed_run.stderr}")
        invalid_dir = root / "invalid-duplicate-path"
        invalid_dir.mkdir()
        invalid_albums = json.loads(scanned_json.read_text(encoding="utf-8"))
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
