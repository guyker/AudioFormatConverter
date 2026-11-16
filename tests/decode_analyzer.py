import argparse
import pathlib
import subprocess
import tempfile


def run(command):
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--analyzer", required=True)
    parser.add_argument("--ffmpeg", required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="afc-decode-") as directory:
        root = pathlib.Path(directory)
        clean = root / "clean.flac"
        retagged = root / "retagged.flac"
        truncated = root / "truncated.flac"
        dual_mono = root / "dual-mono.flac"
        antiphase = root / "antiphase.flac"
        full_scale = root / "full-scale.flac"
        silence = root / "silence.flac"
        source_wav = root / "sixteen-bit.wav"
        padded = root / "padded-24-bit.flac"
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "sine=frequency=997:sample_rate=48000:duration=2",
            "-c:a", "flac", "-sample_fmt", "s32", "-bits_per_raw_sample", "24",
            str(clean),
        ])
        run([
            args.ffmpeg, "-y", "-i", str(clean), "-c", "copy",
            "-metadata", "title=Retagged", str(retagged),
        ])
        data = clean.read_bytes()
        truncated.write_bytes(data[:-256])
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "aevalsrc=0.1*sin(2*PI*997*t)|0.1*sin(2*PI*997*t):s=48000:d=2",
            "-c:a", "flac", str(dual_mono),
        ])
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "aevalsrc=0.1*sin(2*PI*997*t)|-0.1*sin(2*PI*997*t):s=48000:d=2",
            "-c:a", "flac", str(antiphase),
        ])
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "aevalsrc=1|1:s=48000:d=0.25", "-c:a", "flac",
            "-sample_fmt", "s32", "-bits_per_raw_sample", "24", str(full_scale),
        ])
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "aevalsrc=if(between(t\\,0.2\\,0.8)\\,0.1*sin(2*PI*997*t)\\,0):s=48000:d=1",
            "-c:a", "flac", str(silence),
        ])
        run([
            args.ffmpeg, "-y", "-f", "lavfi", "-i",
            "sine=frequency=997:sample_rate=48000:duration=2",
            "-c:a", "pcm_s16le", str(source_wav),
        ])
        run([
            args.ffmpeg, "-y", "-i", str(source_wav), "-c:a", "flac",
            "-sample_fmt", "s32", "-bits_per_raw_sample", "24", str(padded),
        ])
        subprocess.run(
            [args.analyzer, str(clean), str(retagged), str(truncated),
             str(dual_mono), str(antiphase), str(padded), str(full_scale), str(silence)],
            check=True,
        )


if __name__ == "__main__":
    main()
