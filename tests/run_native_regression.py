"""Generate disposable FFmpeg fixtures and run a native regression executable."""

import subprocess
import sys
import tempfile
from pathlib import Path


def run(*arguments):
    subprocess.run([str(value) for value in arguments], check=True)


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in {"preservation", "warnings"}:
        raise SystemExit("usage: run_native_regression.py preservation|warnings TEST_EXECUTABLE FFMPEG")

    kind, executable, ffmpeg = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="afc-native-test-") as temporary:
        root = Path(temporary)
        if kind == "preservation":
            fixture = root / "fixture.flac"
            cover = root / "cover.png"
            run(ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                "color=c=red:s=16x16", "-frames:v", "1", cover)
            run(ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                "sine=frequency=997:sample_rate=48000:duration=1", "-i", cover,
                "-map", "0:a", "-map", "1:v", "-c:a", "flac", "-sample_fmt", "s32",
                "-bits_per_raw_sample", "24", "-c:v", "copy", "-disposition:v", "attached_pic",
                "-metadata", "title=Native regression", fixture)
            run(executable, fixture, root)
        else:
            sine = root / "sine.wav"
            silence = root / "silence.wav"
            run(ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                "sine=frequency=997:sample_rate=48000:duration=1", "-ac", "2",
                "-c:a", "pcm_s16le", sine)
            run(ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i",
                "anullsrc=channel_layout=stereo:sample_rate=48000", "-t", "1",
                "-c:a", "pcm_s16le", silence)
            run(executable, sine, silence)


if __name__ == "__main__":
    main()
