"""Verify that a scan includes audio files directly under the library root."""

import json
import hashlib
import os
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import closing
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: scan_root_audio.py CONVERTER FFMPEG")

    converter, ffmpeg = sys.argv[1:]
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
        numbering_output = root / "numbering-output"
        subprocess.run(
            [converter, "scan", str(numbering_library),
             "--output-dir", str(numbering_output)],
            check=True,
        )
        numbering_json = next(
            (numbering_output / numbering_library.name).glob("media_out_*.json"))
        numbering_records = json.loads(
            numbering_json.read_text(encoding="utf-8"))[0]["Tracks"]
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
                    [converter, "db", "import", str(tier_output / library.name),
                     "--output-dir", str(database_output)],
                    check=True,
                )
                database = database_output / library.name / "catalog-v2.sqlite"
                with closing(sqlite3.connect(database)) as connection:
                    measurements = connection.execute(
                        "SELECT kind,status FROM measurements ORDER BY kind").fetchall()
                    scan_tier = connection.execute("SELECT scan_tier FROM files").fetchone()[0]
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
        if any(hashlib.sha256(track.read_bytes()).hexdigest() != source_hash
               for track, source_hash in source_hashes.items()):
            raise AssertionError("A scan changed one or more source files")


if __name__ == "__main__":
    main()
