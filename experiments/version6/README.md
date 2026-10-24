# V6: phase-aware causal coding, streaming, bounded memory

V6 is a lossless RGB8/RGBA8 image codec with one fixed construction and no
encoder search: median-edge prediction, a sub-integer *phase* context from an
offline-designed causal plane fit (three window moments), an error-energy
activity gate between RGB and a modular opponent lift, a bounded repetition
predictor keyed by the causal neighbourhood, and binarised adaptive
arithmetic coding. The decoder recomputes every context from decoded
samples; the stream carries no tables and no parse. The construction is the
answer to why integer predictors and transforms kept losing on smooth
content: 8-bit rounding of a smooth field is deterministic given a
sub-integer phase, and every earlier codec paid for it as noise.

The published v3 library and wire format are unchanged. V6 is an
experimental stream (`FORMAT.md`, signature `CNGPHI05`). See `RESULTS.md`
for the complete-file sizes, exactness, peak memory and timing.

Over the 244-image research corpus V6 is **0.741 of PNG level 9 and
0.848 of v3** (files with a v3 reference), byte-identical between the
Python reference (`reference.py`) and the C++20 kernel. The encoder peaks at
2.8–5.5 times the image in resident memory and streams rows; the decoder
writes into caller-owned storage. Encode time is within 20% of PNG9 on the
photographs, smooth and GUI images measured (faster on seven of ten); decode
is symmetric with encode and 8–13 times slower than PNG decoding, which is
the structural price of a decoder that models rather than copies.

## Files

- `phase_codec.hpp`, `phase_codec.cpp`, `range_coder.hpp`, `phase_tables.hpp`:
  the kernel (house style, no intrinsics, caller-owned disjoint buffers;
  `encode`, `read_header`, `decode` with status results).
- `phase_driver.cpp`: file adapter (`encode`, `decode`, `check`, in-process `time`).
- `reference.py` + `phase_moment_coefficients.json`: the exact Python reference.
- `evidence/vectors/`: nine bit-exact vectors and their manifest;
  `evidence/*.json`: the M4 cross-check, sizes, memory, timing, table-bound
  and transcoding measurements.
- `run.py`: checks the probe against the vectors (used by `ctest`).

## Reproduce

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/version6 -B .build/version6 -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/version6 -j 4
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/ctest --test-dir .build/version6 --output-on-failure
```

The corpus cross-check, timing and memory measurements run from the research
tree (`research.run_native_phase`, `research.time_native_phase`,
`research.measure_native_phase_memory`) on the M4; timing on a shared machine
is contended and must be repeated when quiet.

## Not claimed

PNG-class decode speed; a malformed-input sweep or memory-safety review;
SIMD intrinsics (plain loops only; two row passes do not yet auto-vectorise);
a stable format; the five-times memory rule below about 1 Mpx, where the
fixed 175 KiB of models and the table floor dominate.
