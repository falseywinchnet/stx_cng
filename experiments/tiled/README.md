# Tiled: a PNG/WebP-class construction

A research study, not part of the library or the CNG format. The question: can
what the phase-coding work learned make a *fast*, tiled lossless format whose
files sit between lossless WebP and PNG, keeping CNG's independent 128×128
tiles, duplicated index, mosaic layer and salvage behaviour? The decoder budget
is PNG/WebP-class work per pixel: prediction, table-driven entropy decoding and
copies, no per-pixel adaptive modelling.

The construction that came out of it (stream `CNGM0005`):

- MED prediction with the colour gate (busy pixels code red and blue minus green).
- 37 causal contexts from local activity and the green residual, each with its
  own static rANS table (zstd's kind of entropy coder, made context-aware by
  decoding each symbol in step with reconstruction).
- Per-image tables with rare symbols escaped to raw bits, stored at the front
  and the back (once on a one-tile image), so every tile decodes on its own.
- Matches that copy residuals, signalled in the green alphabet as WebP does:
  whole-pixel matches and per-plane matches, each with three repeat distances,
  chosen by a cost-aware parse.
- A 256-colour cache per tile, as WebP's.

Complete files over OptiPNG at its maximum level, on 236 images (photographs,
screen captures, smooth graphics, noise, 128 px emoji, synthetic tile
fixtures), every file decoded exactly:

| set | n | lossless WebP | this |
|---|---|---|---|
| photographs | 24 | 0.735 | 0.801 |
| screen captures | 14 | 0.657 | 0.744 |
| smooth graphics | 10 | 0.825 | 0.839 |
| 128 px emoji | 96 | 0.795 | 1.023 |
| periodic tiles | 56 | 0.879 | 1.329 |
| all | 236 | 0.747 | 0.822 |

That is 1.10 of lossless WebP and 0.79 of PNG level 9. The steps that led
there, including the negative results (phase-rounded prediction, context stream
splits for zstd, repeat offsets under a greedy parse, per-plane matches alone),
are in [RESULTS.md](RESULTS.md).

## Files

- `research/tiled_zstd_codec.py` (CNGZ0001): the transform with a zstd back end;
  also the shared tile helpers and the libzstd binding.
- `research/tiled_context_codec.py` (CNGC0001): context-coded static rANS, no matches.
- `research/tiled_match_codec.py` (CNGM0002), `tiled_match_codec_m1.py` (CNGM0001):
  residual matches, repeat distances, greedy and cost-aware parses.
- `research/tiled_cache_codec.py` (CNGM0003, CNGM0004): colour cache, escaped tables.
- `research/tiled_plane_codec.py` (CNGM0005): whole-pixel and per-plane matches.
- `research/benchmark_tiled_*.py`, `research/diagnose_*.py`: the corpus runs and
  diagnoses; `results/`: their records (sizes, exactness; no images).
- `data/`: the corpus manifests (names, hashes, reference sizes). The images are
  not distributed, so the benchmarks need the original corpus files under
  `data/` to run.

The references are exact, allocating Python (NumPy, Pillow for corpus loading,
libzstd for the table blobs). They measure size and correctness only; their
running time says nothing about the construction's speed, which needs a native
implementation.

```sh
cd experiments/tiled
python3 -B -c "import numpy as np; from research import tiled_plane_codec as T; img = np.random.default_rng(1).integers(0, 256, (200, 300, 3), dtype=np.uint8); data = T.encode(img); print(len(data), bool((T.decode(data) == img).all()))"
```
