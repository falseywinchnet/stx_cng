# V7: phase coding in independent frames

V7 is the standalone codec of release 0.3.0 (`src/phase_codec.cpp` is this
kernel). It puts the V6 phase codec (`../version6`) inside independently
decodable frames: 128×128 by default, each with its own adaptive state,
listed in a directory with a CRC32 per frame; the header fields and the
directory are checksummed and stored twice. Frames decode in any order and
in parallel; a damaged frame is detected and does not affect the others; a
damaged index copy is repaired from the other. See `FORMAT.md` for the bytes
and `RESULTS.md` for the measurements, including the OptiPNG comparison and
the malformed-input sweep.

Against the one-stream V6: files are 1.5% larger at side 128 (0.3% at
side 255), serial encode and decode time are at or below V6's, and four
threads decode in about 0.4 of V6's time. Against PNG level 9 the files are
about 0.75 the size; encoding is within 20% of PNG9 on photographs and
several times faster on smooth and user-interface content; single-thread
decoding is about ten times PNG's and divides by the number of frames
decoded concurrently.

## Files

- `phase_codec.hpp`, `phase_codec.cpp`, `range_coder.hpp`, `phase_tables.hpp`:
  the kernel (house style, no intrinsics; `encode`, `encode_with_frame`,
  `read_header`, `frame_count`, `decode_frame`, `decode`).
- `phase_driver.cpp`: file adapter (`encode`, `decode`, `check`, `time`,
  `trace`, `frames` for threaded frame decoding, `encode-side`).
- `reference.py` + `reference_frame_engine.py` + `phase_moment_coefficients.json`:
  the exact Python reference.
- `evidence/vectors/`: ten bit-exact vectors (one of six frames) and their
  manifest; `evidence/*.json`: corpus exactness, cross-platform hashes,
  sizes, OptiPNG reference sizes, frame-side sweep, frame-parallel timing
  and the oracle studies; `evidence/malformed_sweep.txt`: the sanitizer run.
- `run.py`: checks the probe against the vectors (used by `ctest`).

## Reproduce

```sh
cmake -S experiments/version7 -B .build/version7 -DCMAKE_BUILD_TYPE=Release
cmake --build .build/version7 -j 4
ctest --test-dir .build/version7 --output-on-failure
```

## Not claimed

PNG-class single-thread decode speed; a malformed-input sweep or
memory-safety review; SIMD intrinsics; a stable format; forward error
correction of the directory (measured as a cheap addition, not implemented).
