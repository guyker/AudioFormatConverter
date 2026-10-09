---
applyTo: "FlacConvert/src/**,FlacConvert/include/**"
---

# Audio / C++

- Keep core C++ sources portable across Windows, Linux and macOS. Treat Visual Studio projects as a build wrapper;
  isolate OS-specific APIs in narrow platform adapters or conditional implementations.
- Follow existing naming and error-handling conventions; use RAII with matching FFmpeg deleters and explicit
  ownership. Do not edit vendored `FlacConvert/include/rapidjson/` or `external/` code unless requested.
- Check FFmpeg return codes; distinguish `EAGAIN`, EOF and failure. For decode/encode/resample changes, drain buffered
  output and verify sample formats (planar/interleaved), channel layouts, time bases and sample counts.
- FFmpeg log callback context is opaque: do not assume `AVFormatContext`. Keep callbacks and shared log access
  thread-safe; ensure asynchronous tasks finish before their captured objects are destroyed.
- Preserve existing conversion settings unless a change is requested. Do not silently change sample rate, bit depth,
  channels, tags or artwork. Lossless encoding does not make resampling or bit-depth reduction lossless.
- Require successful conversion and output verification before replacing/deleting an original. Use collision-safe
  temporary paths and recoverable replacement; failed conversion or replacement must leave the original recoverable.
- For CLI paths, check process exit status and handle Windows Unicode paths and argument quoting without interpreting
  filenames as shell syntax. Report failures explicitly.
- Validate only with generated fixtures or disposable copies, never the user's library. For lossless re-encoding
  without signal changes, compare decoded PCM/sample counts and required metadata, not compressed-file hashes.
  For intentional signal changes, check requested properties and defined tolerances.
- When changing conversion behaviour, cover short/silent audio, representative formats/channels, Unicode paths,
  corrupt input and destination collisions; verify failure leaves originals intact. Build/exit success alone is not
  evidence of correct audio.
